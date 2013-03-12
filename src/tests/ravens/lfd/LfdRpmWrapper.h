/** Wrapper for calling John's learning from demonstrations python functions using boost python.*/

#pragma once

#include <boost/python.hpp>
#include <boost/shared_ptr.hpp>
#include "simulation/util.h"
#include "utils_python.h"


using namespace std;
namespace py = boost::python;


/** Wraps around lfd rpm module. */
class RegistrationModule {
	py::object registration_module;
	py::object tps_rpm_func;

public:

	typedef boost::shared_ptr<RegistrationModule> Ptr;

	/** tps-rpm algorithm mostly as described by chui and rangaran
	 *  @params:
	 *    src_pts           : pts in the original demonstration (meters)
	 *    target_pts        : pts int the new situation         (meters)
	 *    n_iter            : number of iterations
	 *    reg_init/reg_final: regularization on curvature; affineness vs. non-affineness
	 *    rad_init/rad_final: radius for correspondence calculation (meters) */
	RegistrationModule(vector<btVector3> src_pts, vector<btVector3> target_pts,
			int n_iter = 5, float reg_init = .1, float reg_final = .001,
			float rad_init = .2, float rad_final = .001);

    /** Transform a btVector using tps.
     *  Performs the mapping: pt in demonstration |--> pt in new setting. */
	btVector3 transform_point(btVector3 pt);
    vector<btVector3> & transform_points(vector<btVector3> &pts);


    /** Transform a 4x4 btTransform using tps.
     *  Performs the mapping: pt in demonstration |--> pt in new setting. */
	btTransform transform_frame(btTransform &frame);
	vector<btTransform> & transform_frames(vector<btTransform> &frames);
};
