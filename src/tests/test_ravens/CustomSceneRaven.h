#ifndef _CUSTOM_SCENE_PIERCING_
#define _CUSTOM_SCENE_PIERCING_

#include "simulation/simplescene.h"
#include <BulletSoftBody/btSoftBodyHelpers.h>

#include <omp.h>
#include <iostream>
#include <sstream>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>

#include <openrave/kinbody.h>
#include "robots/pr2.h"
#include "utils/config.h"
#include "simulation/config_viewer.h"
#include "simulation/softBodyHelpers.h"

#include "RavePlanners.h"

class CustomScene : public Scene {
public:

	typedef boost::shared_ptr<CustomScene> Ptr;

	/** Class to represent the hole. */
	class Hole {
	public:
		typedef boost::shared_ptr<Hole> Ptr;

		CustomScene::Ptr h_scene;
		// h_center: Center of the hole
		// h_prev_center: Center where needle first touched hole when piercing
		btVector3 h_center, h_prev_center;
		vector <btSoftBody::Node *> h_nodes;
		bool h_currently_piercing, h_started_piercing, h_pierced;

		Hole (CustomScene * scene): h_scene(scene), h_pierced (false),
										h_center(0,0,0), h_currently_piercing(false),
														h_started_piercing(false){};

		/** Toggle piercing:
		 *  If the hole is already pierced, h_currently_piercing will be set
		 *  to false. */
		void togglePiercing () {
			h_currently_piercing = h_pierced ? false : !h_currently_piercing;
			h_started_piercing = h_currently_piercing ? h_started_piercing:false;
			std::cout<<"Hole piercing: "<<h_currently_piercing<<std::endl;
		}

		/** Increases the number of nodes around the hole on the softbody to
		 *  make needle-cloth interaction realistic around the hole. */
		void refineAroundHole (int iter=3);

		/** Calculate center of hole from the vector of nodes. */
		void calculateCenter ();

		/** Callback to cut the hole. */
		void holeCutCallback ();

		/** Returns normal at the hole. */
		btVector3 normal();
	};

	/* Class to represent the suturing needle. */
	class SuturingNeedle {
	public:
		typedef boost::shared_ptr<SuturingNeedle> Ptr;

		CustomScene &scene;

		RaveObject::Ptr s_needle;
		const float 			s_needle_radius, s_needle_mass;
		const float 			s_end_angle;

		// Is the needle currently piercing?
		bool s_piercing;
		// max distance between needle tip and point to cut at
		float s_pierce_threshold;

		// Manipulator currently grasping the gripper.
		RaveRobotObject::Manipulator::Ptr gripperManip;
		// Is the needle being grasped?
		bool grasped;
		// Correction matrix of the needle
		btMatrix3x3 corrRot;

		SuturingNeedle (CustomScene * scene);

		/** Toggle's needle piercing state. */
		void togglePiercing () {s_piercing = !s_piercing;}

		btTransform getNeedleTipTransform ();
		btTransform getNeedleHandleTransform ();

		void setGraspingTransformCallback ();
	};

	/** A class to represent the cloth in the scene.
	 * Holds the cloth object and other supporting structures. */
	class SutureCloth {
	public:
		typedef boost::shared_ptr<SutureCloth> Ptr;

		// the cloth
		BulletSoftObject::Ptr cloth;
		// node indices of the nodes on the cut
		std::vector<int> cut_nodes1, cut_nodes2;

		SutureCloth(CustomScene &scene, btScalar side_length, btScalar z, btVector3 center);


		/** Returns the line of maximum variance of the cut-points
		 * Performs a PCA on the points.
		 * SIDE_NUM  \in {1, 2} : if 1 : cut-points on the left.
		 *                        if 2 : cut-points on the right.
		 * The return value is a point on the line found and the direction-cosine of the line. */
		pair<pair<btVector3, btVector3> , pair<int, int> > fitLine(int side_num);


		/** See the DOC for fitLine.
		 *  In addition to fitting a line to the cut-points it aligns
		 *  the direction of the cut with the x-axis of the robot's (PR2) transform. */
		pair<pair<btVector3, btVector3> , pair<int, int> >fitLineAligned(int side_num, RaveRobotObject::Ptr robot);

		/** Returns a transform for grasping.
		 *  @param SIDE_NUM  \in {1, 2} : if 1 : transform for left-cut
		 *                                if 2 : transform for right-cut */
		btTransform getCutGraspTransform(int side_num, RaveRobotObject::Ptr robot, float frac=0.5);
		btTransform getCutGraspTransform(int side_num, RaveRobotObject::Ptr robot, btVector3 point);
	};

	//PR2SoftBodyGripperAction::Ptr leftSBAction, rightSBAction;
	//PR2RigidBodyGripperAction::Ptr leftRBAction, rightRBAction;
	//PR2OpenCloseGripperAction::Ptr leftOCAction, rightOCAction;

	BulletInstance::Ptr bullet2;
	OSGInstance::Ptr osg2;
	Fork::Ptr fork;
	RaveRobotObject::Ptr origRobot, tmpRobot;
	//PR2Manager pr2m;

	// the cloth to be sutured
	SutureCloth::Ptr sCloth;
	// vector representing holes
	vector <Hole::Ptr> holes;
	// Suturing needle
	SuturingNeedle::Ptr sNeedle;
	// the table in the scene
	BoxObject::Ptr table;

	OpenRAVE::ViewerBasePtr rave_viewer;
	bool isRaveViewer;

	/** Points which are to be plotted in the scene : correspond to
	    nodes in the soft-body (cloth). */
	PlotPoints::Ptr plot_points;
	// Points for plotting center and tip of needle
	PlotPoints::Ptr plot_needle;
	// Points for plotting nodes and center of hole
	PlotPoints::Ptr plot_holes;

	//// Ankush's stuff: Not sure about this stuff
	std::vector<btVector3> plotpoints;
	std::vector<btVector4> plotcolors;
	/** Axes corresponding to the location where the
     *  left grippers are. **/
	PlotAxes::Ptr plot_axes1;
	PlotAxes::Ptr plot_axes2;
	///////////////////////////////////////

	CustomScene() : isRaveViewer(false)
		{Load(env, rave, "/home/sibi/sandbox/bulletsim/data/xml/raven_env.xml",false);}

	void createFork();
	void swapFork();

	/** Returns the coordinates of the last point directly below (-z) SOURCE_PT
	      on the cloth represented by PSB. */
	btVector3 getDownPoint(btVector3 &source_pt, boost::shared_ptr<btSoftBody> psb,
			btScalar radius=3.0);

	/** Raycasts from SOURCE to all the nodes of PSB
	      and returns a vector of the same size as the nodes of PSB
	      depicting whether that node is visible or not. */
	std::vector<btVector3> checkNodeVisibility(btVector3 camera_origin,
			boost::shared_ptr<btSoftBody> psb);

	/** Plots needle tip, flag for removing plots. */
	void plotNeedle(bool remove = false);
	/** Plot holes, flag for removing plots */
	void plotHoles(bool remove = false);

	/**  Creates a square cloth with side length 2s.
	     The four coordinates of the cloth are:
	     {(s,s) (-s,s,) (-s,-s) (s,-s)}
	     Then, the center of the cloth (initially at (0,0,0)
	     is translated to CENTER.*/
	BulletSoftObject::Ptr createCloth(btScalar s, btScalar z, btVector3 center,
									  std::vector<int> &cut_nodes1, std::vector<int> &cut_nodes2,
									  bool getCutIndices=true,
				                      bool shouldCut = true,
				                      unsigned int resx = 50, unsigned int resy =50);

	/** Returns ||(v1.x, v1.y) - (v2.x, v2.y)||. */
	btScalar inline getXYDistance(btVector3 &v1, btVector3 &v2);

	/** Draws the axes at LEFT_GRIPPER1 and LEFT_GRIPPER2. */
	void drawAxes();

	/** to compute nodes near a point and add it to hole. */
	void findNearbyNodes (Hole::Ptr hole, btVector3 holePt);

	void run();

	//PreStepCallbacks and such
	/* to compute centers of holes. */
	void computeHoleCentersCallBack();

};

#endif