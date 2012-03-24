#include "ropescene.h"
#include "config_bullet.h"
#include "config_viewer.h"
#include "thread_socket_interface.h"
#include "util.h"

#include <iostream>
using namespace std;


RopeScene::RopeScene() {
    osg.reset(new OSGInstance());
    bullet.reset(new BulletInstance());
    bullet->setGravity(BulletConfig::gravity);
    if (RopeSceneConfig::enableRobot)
        rave.reset(new RaveInstance());

    env.reset(new Environment(bullet, osg));

    if (RopeSceneConfig::enableHaptics)
        connectionInit(); // socket connection for haptics

    // plots for debugging
    plotPoints.reset(new PlotPoints(GeneralConfig::scale * 0.5));
    env->add(plotPoints);
    plotLines.reset(new PlotLines(GeneralConfig::scale * 0.5));
    env->add(plotLines);

    // populate the scene with some basic objects
    boost::shared_ptr<btDefaultMotionState> ms;
    ms.reset(new btDefaultMotionState(btTransform(btQuaternion(0, 0, 0, 1), btVector3(0, 0, 0))));
    ground.reset(new PlaneStaticObject(btVector3(0., 0., 1.), 0., ms));
    env->add(ground);

    if (RopeSceneConfig::enableRobot) {
      btTransform trans(btQuaternion(0, 0, 0, 1), btVector3(0, 0, 0));
      pr2.reset(new RaveRobotKinematicObject(rave, "robots/pr2-beta-sim.robot.xml", trans, GeneralConfig::scale));
      env->add(pr2);
      pr2->ignoreCollisionWith(ground->rigidBody.get()); // the robot's always touching the ground anyway
    }

    if (RopeSceneConfig::enableIK && RopeSceneConfig::enableRobot) {
        pr2Left = pr2->createManipulator("leftarm", RopeSceneConfig::useFakeGrabber);
        pr2Right = pr2->createManipulator("rightarm", RopeSceneConfig::useFakeGrabber);
        if (RopeSceneConfig::useFakeGrabber) {
            env->add(pr2Left->grabber);
            env->add(pr2Right->grabber);
        }
    }
}

void RopeScene::startViewer() {
    drawingOn = syncTime = true;
    loopState.looping = loopState.paused = false;

    dbgDraw.reset(new osgbCollision::GLDebugDrawer());
    dbgDraw->setDebugMode(btIDebugDraw::DBG_MAX_DEBUG_DRAW_MODE /*btIDebugDraw::DBG_DrawWireframe*/);
    dbgDraw->setEnabled(false);
    bullet->dynamicsWorld->setDebugDrawer(dbgDraw.get());
    osg->root->addChild(dbgDraw->getSceneGraph());

    viewer.setUpViewInWindow(0, 0, ViewerConfig::windowWidth, ViewerConfig::windowHeight);
    manip = new RopeSceneEventHandler(this);
    manip->setHomePosition(util::toOSGVector(ViewerConfig::cameraHomePosition), osg::Vec3(), osg::Z_AXIS);
    viewer.setCameraManipulator(manip);
    viewer.setSceneData(osg->root.get());
    viewer.realize();
    step(0);
}

void RopeScene::processHaptics() {
    printf("in process haptics!\n");
    if (!RopeSceneConfig::enableRobot)
        return;

    // read the haptic controllers
    btTransform trans0, trans1;
    bool buttons0[2], buttons1[2];
    static bool lastButton[2] = { false, false };
    if (!util::getHapticInput(trans0, buttons0, trans1, buttons1))
        return;

    pr2Left->moveByIK(trans0, RopeSceneConfig::enableRobotCollision, true);
    if (buttons0[0] && !lastButton[0]) {
        if (RopeSceneConfig::useFakeGrabber)
            pr2Left->grabber->grabNearestObjectAhead();
        else
            cout << "not implemented" << endl;
    }
    else if (!buttons0[0] && lastButton[0]) {
        if (RopeSceneConfig::useFakeGrabber)
            pr2Left->grabber->releaseConstraint();
        else
            cout << "not implemented" << endl;
    }
    lastButton[0] = buttons0[0];

    pr2Right->moveByIK(trans0, RopeSceneConfig::enableRobotCollision, true);
    if (buttons1[0] && !lastButton[1]) {
        if (RopeSceneConfig::useFakeGrabber)
            pr2Right->grabber->grabNearestObjectAhead();
        else
            cout << "not implemented" << endl;
    }
    else if (!buttons1[0] && lastButton[1]) {
        if (RopeSceneConfig::useFakeGrabber)
            pr2Right->grabber->releaseConstraint();
        else
            cout << "not implemented" << endl;
    }
    lastButton[1] = buttons1[0];
}

void RopeScene::step(float dt, int maxsteps, float internaldt) {
    static float startTime=viewer.getFrameStamp()->getSimulationTime(), endTime;

    if (syncTime && drawingOn)
        endTime = viewer.getFrameStamp()->getSimulationTime();

    if (RopeSceneConfig::enableHaptics)
        processHaptics();
    env->step(dt, maxsteps, internaldt);
    draw();

    if (syncTime && drawingOn) {
        float timeLeft = dt - (endTime - startTime);
        idleFor(timeLeft);
        startTime = endTime + timeLeft;
    }
}

void RopeScene::step(float dt) {
    step(dt, BulletConfig::maxSubSteps, BulletConfig::internalTimeStep);
}

// Steps for a time interval
void RopeScene::stepFor(float dt, float time) {
    while (time > 0) {
        step(dt);
        time -= dt;
    }
}

// Idles for a time interval. Physics will not run.
void RopeScene::idleFor(float time) {
    if (!drawingOn || !syncTime || time <= 0.f)
        return;
    float endTime = time + viewer.getFrameStamp()->getSimulationTime();
    while (viewer.getFrameStamp()->getSimulationTime() < endTime && !viewer.done())
        draw();
}

void RopeScene::draw() {
    if (!drawingOn)
        return;
    if (manip->state.debugDraw) {
        dbgDraw->BeginDraw();
        bullet->dynamicsWorld->debugDrawWorld();
        dbgDraw->EndDraw();
    }
    viewer.frame();
}

void RopeScene::startLoop() {
    bool oldSyncTime = syncTime;
    syncTime = false;
    loopState.looping = true;
    loopState.prevTime = loopState.currTime =
        viewer.getFrameStamp()->getSimulationTime();
    while (loopState.looping && drawingOn && !viewer.done()) {
        loopState.currTime = viewer.getFrameStamp()->getSimulationTime();
        step(loopState.currTime - loopState.prevTime);
        loopState.prevTime = loopState.currTime;
    }
    syncTime = oldSyncTime;
}

void RopeScene::startFixedTimestepLoop(float dt) {
    loopState.looping = true;
    while (loopState.looping && drawingOn && !viewer.done())
        step(dt);
}

void RopeScene::stopLoop() {
    loopState.looping = false;
}

void RopeScene::idle(bool b) {
    loopState.paused = b;
    while (loopState.paused && drawingOn && !viewer.done())
        draw();
    loopState.prevTime = loopState.currTime = viewer.getFrameStamp()->getSimulationTime();
}

void RopeScene::toggleIdle() {
    idle(!loopState.paused);
}

void RopeScene::runAction(Action &a, float dt) {
    while (!a.done()) {
        a.step(dt);
        step(dt);
    }
}

void RopeSceneEventHandler::getTransformation( osg::Vec3d& eye, osg::Vec3d& center, osg::Vec3d& up ) const
  {
    center = _center;
    eye = _center + _rotation * osg::Vec3d( 0., 0., _distance );
    up = _rotation * osg::Vec3d( 0., 1., 0. );
  }

bool RopeSceneEventHandler::handle(const osgGA::GUIEventAdapter &ea, osgGA::GUIActionAdapter &aa) {
    switch (ea.getEventType()) {
    case osgGA::GUIEventAdapter::KEYDOWN:
        switch (ea.getKey()) {
        case 'h':
            osgGA::TrackballManipulator::home(ea, aa);
            break;
        case 'd':
            state.debugDraw = !state.debugDraw;
            scene->dbgDraw->setEnabled(state.debugDraw);
            break;
        case 'p':
            scene->toggleIdle();
            break;
        case '1':
            state.moveManip0 = true; break;
        case '2':
            state.moveManip1 = true; break;
        case 'q':
            state.rotateManip0 = true; break;
        case 'w':
            state.rotateManip1 = true; break;
      }
      break;

    case osgGA::GUIEventAdapter::KEYUP:
        switch (ea.getKey()) {
        case '1':
            state.moveManip0 = false; break;
        case '2':
            state.moveManip1 = false; break;
        case 'q':
            state.rotateManip0 = false; break;
        case 'w':
            state.rotateManip1 = false; break;
        }
        break;


    case osgGA::GUIEventAdapter::PUSH:
        state.startDragging = true;
        return osgGA::TrackballManipulator::handle(ea, aa);

    case osgGA::GUIEventAdapter::DRAG:
        // drag the active manipulator in the plane of view

        if ( (ea.getButtonMask() & ea.LEFT_MOUSE_BUTTON) &&
              (state.moveManip0 || state.moveManip1 ||
               state.rotateManip0 || state.rotateManip1)) {
            if (state.startDragging) {
                dx = dy = 0;
            } else {
                dx = lastX - ea.getXnormalized();
                dy = ea.getYnormalized() - lastY;
            }
            lastX = ea.getXnormalized(); lastY = ea.getYnormalized();
            state.startDragging = false;
  
            // get our current view
            osg::Vec3d osgCenter, osgEye, osgUp;
            getTransformation(osgCenter, osgEye, osgUp);
            btVector3 from(util::toBtVector(osgEye));
            btVector3 to(util::toBtVector(osgCenter));
            btVector3 up(util::toBtVector(osgUp)); up.normalize();
  
            // compute basis vectors for the plane of view
            // (the plane normal to the ray from the camera to the center of the scene)
            btVector3 normal = (to - from).normalized();
            btVector3 yVec = (up - (up.dot(normal))*normal).normalized(); //FIXME: is this necessary with osg?
            btVector3 xVec = normal.cross(yVec);
            btVector3 dragVec = RopeSceneConfig::mouseDragScale * (dx*xVec + dy*yVec);

            btTransform origTrans;
            if (state.moveManip0 || state.rotateManip0)
                origTrans = scene->grab_left.GetTransform();
            else
                origTrans = scene->grab_right.GetTransform();

            
            btTransform newTrans(origTrans);

            if (state.moveManip0 || state.moveManip1)
                // if moving the manip, just set the origin appropriately
                newTrans.setOrigin(dragVec + origTrans.getOrigin());
            else if (state.rotateManip0 || state.rotateManip1) {
                // if we're rotating, the axis is perpendicular to the
                // direction the mouse is dragging
                btVector3 axis = normal.cross(dragVec);
                btScalar angle = dragVec.length();
                btQuaternion rot(axis, angle);
                // we must ensure that we never get a bad rotation quaternion
                // due to really small (effectively zero) mouse movements
                // this is the easiest way to do this:
                if (rot.length() > 0.99f && rot.length() < 1.01f)
                    newTrans.setRotation(rot * origTrans.getRotation());
            }
            if (state.moveManip0 || state.rotateManip0)
                scene->grab_left.SetTransform(newTrans);
            else
                scene->grab_right.SetTransform(newTrans);
            
            //manip->moveByIK(newTrans, RopeSceneConfig::enableRobotCollision, true);

/*
        if (RopeSceneConfig::enableRobot && RopeSceneConfig::enableIK &&
              (ea.getButtonMask() & ea.LEFT_MOUSE_BUTTON) &&
              (state.moveManip0 || state.moveManip1 ||
               state.rotateManip0 || state.rotateManip1)) {
            if (state.startDragging) {
                dx = dy = 0;
            } else {
                dx = lastX - ea.getXnormalized();
                dy = ea.getYnormalized() - lastY;
            }
            lastX = ea.getXnormalized(); lastY = ea.getYnormalized();
            state.startDragging = false;
  
            // get our current view
            osg::Vec3d osgCenter, osgEye, osgUp;
            getTransformation(osgCenter, osgEye, osgUp);
            btVector3 from(util::toBtVector(osgEye));
            btVector3 to(util::toBtVector(osgCenter));
            btVector3 up(util::toBtVector(osgUp)); up.normalize();
  
            // compute basis vectors for the plane of view
            // (the plane normal to the ray from the camera to the center of the scene)
            btVector3 normal = (to - from).normalized();
            btVector3 yVec = (up - (up.dot(normal))*normal).normalized(); //FIXME: is this necessary with osg?
            btVector3 xVec = normal.cross(yVec);
            btVector3 dragVec = RopeSceneConfig::mouseDragScale * (dx*xVec + dy*yVec);

            RaveRobotKinematicObject::Manipulator::Ptr manip;
            if (state.moveManip0 || state.rotateManip0)
                manip = scene->pr2Left;
            else
                manip = scene->pr2Right;

            btTransform origTrans = manip->getTransform();
            btTransform newTrans(origTrans);

            if (state.moveManip0 || state.moveManip1)
                // if moving the manip, just set the origin appropriately
                newTrans.setOrigin(dragVec + origTrans.getOrigin());
            else if (state.rotateManip0 || state.rotateManip1) {
                // if we're rotating, the axis is perpendicular to the
                // direction the mouse is dragging
                btVector3 axis = normal.cross(dragVec);
                btScalar angle = dragVec.length();
                btQuaternion rot(axis, angle);
                // we must ensure that we never get a bad rotation quaternion
                // due to really small (effectively zero) mouse movements
                // this is the easiest way to do this:
                if (rot.length() > 0.99f && rot.length() < 1.01f)
                    newTrans.setRotation(rot * origTrans.getRotation());
            }
            manip->moveByIK(newTrans, RopeSceneConfig::enableRobotCollision, true);
*/
        } else {
            // if not dragging, we want the camera to move
            return osgGA::TrackballManipulator::handle(ea, aa);
        }
        break;

    default:
        return osgGA::TrackballManipulator::handle(ea, aa);
    }
    // this event handler doesn't actually change the camera, so return false
    // to let other handlers deal with this event too
    return false;
}

bool RopeSceneConfig::enableIK = true;
bool RopeSceneConfig::enableHaptics = false;
bool RopeSceneConfig::enableRobot = true;
bool RopeSceneConfig::enableRobotCollision = true;
bool RopeSceneConfig::useFakeGrabber = false;
float RopeSceneConfig::mouseDragScale = 1.;