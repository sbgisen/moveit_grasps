/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2013, University of Colorado, Boulder
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Univ of CO, Boulder nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

#include <moveit_grasps/grasp_filter.h>
#include <moveit/transforms/transforms.h>
#include <moveit/collision_detection/collision_tools.h>

// Conversions
#include <eigen_conversions/eigen_msg.h> // add to pkg TODO

namespace moveit_grasps
{

// Constructor
GraspFilter::GraspFilter( robot_state::RobotStatePtr robot_state,
                          moveit_visual_tools::MoveItVisualToolsPtr& visual_tools )
  : visual_tools_(visual_tools)
{
  // Make a copy of the robot state so that we are sure outside influence does not break our grasp generator
  robot_state_.reset(new moveit::core::RobotState(*robot_state));

  ROS_DEBUG_STREAM_NAMED("filter","Loaded grasp filter");
}

GraspFilter::~GraspFilter()
{
}

bool GraspFilter::chooseBestGrasp( std::vector<GraspSolution>& filtered_grasps,
                                   GraspSolution& chosen )
{
  // TODO: better logic here
  if( filtered_grasps.empty() )
  {
    ROS_ERROR_NAMED("filter","There are no grasps to choose from");
    return false;
  }

  // Find max grasp quality
  double max_quality = -1;
  for (std::size_t i = 0; i < filtered_grasps.size(); ++i)
  {
    // METHOD 1 - use score
    if (false)
    {
      if (filtered_grasps[i].grasp_.grasp_quality > max_quality)
      {
        max_quality = filtered_grasps[i].grasp_.grasp_quality;
        chosen = filtered_grasps[i];
      }
    }
    else // METHOD 2 - use yall angle
    {
      const geometry_msgs::Pose& pose = filtered_grasps[i].grasp_.grasp_pose.pose;
      //double roll = atan2(2*(pose.orientation.x*pose.orientation.y + pose.orientation.w*pose.orientation.z), pose.orientation.w*pose.orientation.w + pose.orientation.x*pose.orientation.x - pose.orientation.y*pose.orientation.y - pose.orientation.z*pose.orientation.z);
      double yall = asin(-2*(pose.orientation.x*pose.orientation.z - pose.orientation.w*pose.orientation.y));
      //double pitch = atan2(2*(pose.orientation.y*pose.orientation.z + pose.orientation.w*pose.orientation.x), pose.orientation.w*pose.orientation.w - pose.orientation.x*pose.orientation.x - pose.orientation.y*pose.orientation.y + pose.orientation.z*pose.orientation.z);
      //std::cout << "ROLL: " << roll << " YALL: " << yall << " PITCH: " << pitch << std::endl;
      std::cout << "YALL: " << yall << std::endl;
      if (yall > max_quality)
      {
        max_quality = yall;
        chosen = filtered_grasps[i];
      }
    }

  }

  ROS_INFO_STREAM_NAMED("grasp_filter","Chose grasp with quality " << max_quality);

  return true;
}

bool GraspFilter::filterGrasps(const std::vector<moveit_msgs::Grasp>& possible_grasps,
                               std::vector<GraspSolution>& filtered_grasps,
                               bool filter_pregrasp,
                               const robot_model::JointModelGroup* arm_jmg)
{
  // -----------------------------------------------------------------------------------------------
  // Error check
  if( possible_grasps.empty() )
  {
    ROS_ERROR_NAMED("filter","Unable to filter grasps because vector is empty");
    return false;
  }

  // -----------------------------------------------------------------------------------------------
  // Get the solver timeout from kinematics.yaml
  solver_timeout_ = arm_jmg->getDefaultIKTimeout();
  ROS_DEBUG_STREAM_NAMED("grasp_filter","Grasp filter IK timeout " << solver_timeout_);

  // -----------------------------------------------------------------------------------------------
  // Choose how many degrees of freedom
  num_variables_ = arm_jmg->getVariableCount();
  ROS_DEBUG_STREAM_NAMED("grasp_filter","Solver for " << num_variables_ << " degrees of freedom");

  // -----------------------------------------------------------------------------------------------
  // Get the end effector joint model group
  if (arm_jmg->getAttachedEndEffectorNames().size() == 0)
  {
    ROS_ERROR_STREAM_NAMED("grasp_filter","No end effectors attached to this arm");
    return false;
  }
  else if (arm_jmg->getAttachedEndEffectorNames().size() > 1)
  {
    ROS_ERROR_STREAM_NAMED("grasp_filter","More than one end effectors attached to this arm");
    return false;
  }  
  const robot_model::JointModelGroup* ee_jmg = arm_jmg->getParentModel().getJointModelGroup(arm_jmg->getAttachedEndEffectorNames()[0]);

  // ----------------------------------------------------------------------------------------
  // Make sure solution starts empty
  filtered_grasps.clear();

  // Try to filter grasps not in verbose mode
  bool verbose = false;
  bool result = filterGraspsHelper(possible_grasps, filtered_grasps, filter_pregrasp, ee_jmg, arm_jmg, verbose);

  if (!result || filtered_grasps.size() == 0)
  {
    ROS_ERROR_STREAM_NAMED("filter","IK filter unable to find any valid grasps! Re-running in verbose mode");
    verbose = true;
    result = filterGraspsHelper(possible_grasps, filtered_grasps, filter_pregrasp, ee_jmg, arm_jmg, verbose);
  }
  return result;
}


bool GraspFilter::filterGraspsHelper(const std::vector<moveit_msgs::Grasp>& possible_grasps,
                                     std::vector<GraspSolution>& filtered_grasps,
                                     bool filter_pregrasp, 
                                     const robot_model::JointModelGroup* ee_jmg,
                                     const robot_model::JointModelGroup* arm_jmg, bool verbose)                                     
{
  // -----------------------------------------------------------------------------------------------
  // how many cores does this computer have and how many do we need?
  std::size_t num_threads = boost::thread::hardware_concurrency();
  if( num_threads > possible_grasps.size() )
    num_threads = possible_grasps.size();

  // Debug
  if(verbose)
  {
    num_threads = 1;
    ROS_WARN_STREAM_NAMED("grasp_filter","Using only " << num_threads << " threads");
  }
  ROS_INFO_STREAM_NAMED("filter", "Filtering possible grasps with " << num_threads << " threads");

  // -----------------------------------------------------------------------------------------------
  // Load kinematic solvers if not already loaded
  if( kin_solvers_[arm_jmg->getName()].size() != num_threads )
  {
    kin_solvers_[arm_jmg->getName()].clear();

    // Create an ik solver for every thread
    for (int i = 0; i < num_threads; ++i)
    {
      //ROS_DEBUG_STREAM_NAMED("filter","Creating ik solver " << i);
      kin_solvers_[arm_jmg->getName()].push_back(arm_jmg->getSolverInstance());

      // Test to make sure we have a valid kinematics solver
      if( !kin_solvers_[arm_jmg->getName()][i] )
      {
        ROS_ERROR_STREAM_NAMED("grasp_filter","No kinematic solver found");
        return false;
      }
    }
  }

  // Transform poses -------------------------------------------------------------------------------
  // bring the pose to the frame of the IK solver
  const std::string &ik_frame = kin_solvers_[arm_jmg->getName()][0]->getBaseFrame();
  Eigen::Affine3d link_transform;
  ROS_DEBUG_STREAM_NAMED("temp","Frame transform: ik_frame: " << ik_frame << " and robot model frame: " << robot_state_->getRobotModel()->getModelFrame());
  if (!moveit::core::Transforms::sameFrame(ik_frame, robot_state_->getRobotModel()->getModelFrame()))
  {
    const robot_model::LinkModel *lm = robot_state_->getLinkModel((!ik_frame.empty() && ik_frame[0] == '/') ? ik_frame.substr(1) : ik_frame);

    if (!lm)
      return false;
    //pose = getGlobalLinkTransform(lm).inverse() * pose;
    //ROS_WARN_STREAM_NAMED("temp","remove this update call");
    //robot_state_->update();// TODO remove?
    link_transform = robot_state_->getGlobalLinkTransform(lm).inverse();
  }

  // Benchmark time
  ros::Time start_time;
  start_time = ros::Time::now();

  // -----------------------------------------------------------------------------------------------
  // Loop through poses and find those that are kinematically feasible
  boost::thread_group bgroup; // create a group of threads
  boost::mutex lock; // used for sharing the same data structures

  // split up the work between threads
  double num_grasps_per_thread = double(possible_grasps.size()) / num_threads;
  int grasps_id_start;
  int grasps_id_end = 0;

  for(int i = 0; i < num_threads; ++i)
  {
    grasps_id_start = grasps_id_end;
    grasps_id_end = ceil(num_grasps_per_thread*(i+1));
    if( grasps_id_end >= possible_grasps.size() )
      grasps_id_end = possible_grasps.size();
    //ROS_INFO_STREAM_NAMED("filter","low " << grasps_id_start << " high " << grasps_id_end);

    IkThreadStruct tc(possible_grasps, filtered_grasps, link_transform, grasps_id_start,
                      grasps_id_end, kin_solvers_[arm_jmg->getName()][i], filter_pregrasp, ee_jmg,
                      solver_timeout_, 
                      &lock, verbose, i);
    bgroup.create_thread( boost::bind( &GraspFilter::filterGraspThread, this, tc ) );
  }

  ROS_DEBUG_STREAM_NAMED("filter","Waiting to join " << num_threads << " ik threads...");
  bgroup.join_all(); // wait for all threads to finish

  ROS_INFO_STREAM_NAMED("filter", "Grasp filter complete, found " << filtered_grasps.size() << " IK solutions out of " <<
                        possible_grasps.size() );

  if (false)
  {
    // End Benchmark time
    double duration = (ros::Time::now() - start_time).toNSec() * 1e-6;
    ROS_INFO_STREAM_NAMED("filter","Grasp generator IK grasp filtering benchmark time:");
    std::cout << duration << "\t" << possible_grasps.size() << "\n";
  }

  return true;
}

void GraspFilter::filterGraspThread(IkThreadStruct ik_thread_struct)
{
  // Seed state - start at zero
  std::vector<double> ik_seed_state(num_variables_); // fill with zeros
  std::vector<double> grasp_ik_solution;
  std::vector<double> pregrasp_ik_solution;
  moveit_msgs::MoveItErrorCodes error_code;
  geometry_msgs::PoseStamped ik_pose;

  // Process the assigned grasps
  for( int i = ik_thread_struct.grasps_id_start_; i < ik_thread_struct.grasps_id_end_; ++i )
  {
    //ROS_DEBUG_STREAM_NAMED("filter", "Checking grasp #" << i);

    // Clear out previous solution just in case - not sure if this is needed
    grasp_ik_solution.clear(); // TODO remove
    pregrasp_ik_solution.clear(); // TODO remove

    // Transform current pose to frame of planning group
    ik_pose = ik_thread_struct.possible_grasps_[i].grasp_pose;

    Eigen::Affine3d eigen_pose;
    tf::poseMsgToEigen(ik_pose.pose, eigen_pose);
    eigen_pose = ik_thread_struct.link_transform_ * eigen_pose;
    tf::poseEigenToMsg(eigen_pose, ik_pose.pose);
    ik_pose.header.frame_id = "jaco_link_base";

    //std::cout << "after link transform " << ik_pose << std::endl;
    //visual_tools_->publishArrow(ik_pose, rviz_visual_tools::ORANGE, rviz_visual_tools::LARGE, 0.1);

    // if (ik_thread_struct.verbose_)
    // {
    //   std::cout << std::endl;
    //   std::cout << "-------------------------------------------------------" << std::endl;
    //   std::cout << "Msg: \n" << ik_thread_struct.possible_grasps_[i] << std::endl;
    //   std::cout << "link transform: " << ik_thread_struct.link_transform_.translation() << std::endl;
    // }

    // Debug: display grasp position
    if (ik_thread_struct.verbose_)
    {
      visual_tools_->publishEEMarkers(ik_pose.pose, ik_thread_struct.ee_jmg_, rviz_visual_tools::RED);
      ros::Duration(0.1).sleep();
    }

    // Test it with IK
    ik_thread_struct.kin_solver_->searchPositionIK(ik_pose.pose, ik_seed_state, ik_thread_struct.timeout_, 
                                                   grasp_ik_solution, error_code);      

    // Results
    if( error_code.val == moveit_msgs::MoveItErrorCodes::SUCCESS )
    {
      //ROS_INFO_STREAM_NAMED("filter","Found IK Solution");

      // Copy solution to seed state so that next solution is faster
      ik_seed_state = grasp_ik_solution;

      // Start pre-grasp section ----------------------------------------------------------
      if (ik_thread_struct.filter_pregrasp_)       // optionally check the pregrasp
      {
        // Convert to a pre-grasp
        const std::string &ee_parent_link_name = ik_thread_struct.ee_jmg_->getEndEffectorParentGroup().second;
        ik_pose = Grasps::getPreGraspPose(ik_thread_struct.possible_grasps_[i], ee_parent_link_name);

        // Transform current pose to frame of planning group
        Eigen::Affine3d eigen_pose;
        tf::poseMsgToEigen(ik_pose.pose, eigen_pose);
        eigen_pose = ik_thread_struct.link_transform_ * eigen_pose;
        tf::poseEigenToMsg(eigen_pose, ik_pose.pose);

        // Test it with IK
        ik_thread_struct.kin_solver_->searchPositionIK(ik_pose.pose, ik_seed_state, ik_thread_struct.timeout_, pregrasp_ik_solution, error_code);          

        // Results
        if( error_code.val == moveit_msgs::MoveItErrorCodes::NO_IK_SOLUTION )
        {
          // The grasp was valid but the pre-grasp was not
          //ROS_WARN_STREAM_NAMED("filter","Unable to find IK solution for pre-grasp pose.");
          continue;
        }
        else if( error_code.val == moveit_msgs::MoveItErrorCodes::TIMED_OUT )
        {
          //ROS_DEBUG_STREAM_NAMED("filter","Unable to find IK solution for pre-grasp pose: Timed Out.");
          continue;
        }
        else if( error_code.val != moveit_msgs::MoveItErrorCodes::SUCCESS )
        {
          ROS_INFO_STREAM_NAMED("filter","IK solution error for pre-grasp: MoveItErrorCodes.msg = " << error_code);
          continue;
        }
      }
      else
      {
        ROS_WARN_STREAM_NAMED("filter","Not filtering pre-grasp - GraspSolution may have bad data");
      }
      // Both grasp and pre-grasp have passed, create the solution
      GraspSolution grasp_solution;
      grasp_solution.grasp_ = ik_thread_struct.possible_grasps_[i];
      grasp_solution.grasp_ik_solution_ = grasp_ik_solution;
      grasp_solution.pregrasp_ik_solution_ = pregrasp_ik_solution;

      // Lock the result vector so we can add to it for a second
      {
        boost::mutex::scoped_lock slock(*ik_thread_struct.lock_);
        ik_thread_struct.filtered_grasps_.push_back( grasp_solution );
      }

      // End pre-grasp section -------------------------------------------------------
    }
    else if( error_code.val == moveit_msgs::MoveItErrorCodes::NO_IK_SOLUTION )
    {
      //ROS_WARN_STREAM_NAMED("filter","Unable to find IK solution for pose: No Solution");
    }
    else if( error_code.val == moveit_msgs::MoveItErrorCodes::TIMED_OUT )
    {
      //ROS_DEBUG_STREAM_NAMED("filter","Unable to find IK solution for pose: Timed Out.");
    }
    else
      ROS_INFO_STREAM_NAMED("filter","IK solution error: MoveItErrorCodes.msg = " << error_code);
  }

  //ROS_DEBUG_STREAM_NAMED("filter","Thread " << ik_thread_struct.thread_id_ << " finished");
}

bool GraspFilter::filterGraspsInCollision(std::vector<GraspSolution>& possible_grasps,
                                          planning_scene_monitor::PlanningSceneMonitorPtr planning_scene_monitor,
                                          const robot_model::JointModelGroup* arm_jmg,
                                          robot_state::RobotStatePtr robot_state,
                                          bool verbose)
{
  // Error check
  if (possible_grasps.size() == 0)
  {
    ROS_ERROR_STREAM_NAMED("filter","Unable to filter grasps for collision because none were passed in");
    return false;
  }

  std::vector<GraspSolution> original_possible_grasps = possible_grasps;
  const std::size_t original_size = possible_grasps.size();

  // Initial run
  if (!filterGraspsInCollisionHelper(possible_grasps, planning_scene_monitor, arm_jmg, robot_state, verbose))
  {
    ROS_ERROR_STREAM_NAMED("filter","Grasp filter failed");
    return false;
  }

  assert(original_possible_grasps.size() == original_size); // make sure the copy worked

  // Check if enough passed. If not, go to debug mode if we weren't already in verbose mode
  if (!possible_grasps.size() && !verbose)
  {
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    ROS_WARN_STREAM_NAMED("filter","All grasps were filtered due to collision, possible error");
    ROS_WARN_STREAM_NAMED("filter","Re-running again in debug mode");

    if (!filterGraspsInCollisionHelper(original_possible_grasps, planning_scene_monitor, arm_jmg, robot_state, true))
    {
      ROS_ERROR_STREAM_NAMED("filter","Grasp filter failed");
      return false;
    }
  }

  return true;
}

bool GraspFilter::filterGraspsInCollisionHelper(std::vector<GraspSolution>& possible_grasps,
                                                planning_scene_monitor::PlanningSceneMonitorPtr planning_scene_monitor,
                                                const robot_model::JointModelGroup* arm_jmg,
                                                robot_state::RobotStatePtr robot_state,
                                                bool verbose)
{
  // Copy the current state positions
  *robot_state_ = *robot_state;

  // Copy planning scene that is locked
  planning_scene::PlanningScenePtr cloned_scene;
  {
    planning_scene_monitor::LockedPlanningSceneRO scene(planning_scene_monitor);
    cloned_scene = planning_scene::PlanningScene::clone(scene);
  }

  ROS_DEBUG_STREAM_NAMED("filter","Filtering " << possible_grasps.size() << " possible grasps");

  // Start checking all grasps
  for (std::vector<GraspSolution>::iterator grasp_it = possible_grasps.begin();
       grasp_it != possible_grasps.end(); /*it++*/)
  {
    // -----------------------------------------------------------------------------------
    // Check grasp ik solution
    robot_state_->setJointGroupPositions(arm_jmg, grasp_it->grasp_ik_solution_);

    if (cloned_scene->isStateColliding(*robot_state_, arm_jmg->getName(), verbose))
    {
      // Remove this grasp
      if (verbose)
      {
        std::cout << std::endl;
        ROS_INFO_STREAM_NAMED("filter","Grasp solution colliding");
        visual_tools_->publishRobotState(robot_state_, rviz_visual_tools::RED);
        visual_tools_->publishContactPoints(*robot_state_, cloned_scene.get());

        ros::Duration(4.0).sleep();
      }

      possible_grasps.erase(grasp_it);
      continue; // next grasp
    }

    // -----------------------------------------------------------------------------------
    // Check PRE-grasp ik solution
    /*
      robot_state_->setJointGroupPositions(arm_jmg, grasp_it->pregrasp_ik_solution_);

      if (cloned_scene->isStateColliding(*robot_state_, arm_jmg->getName(), verbose))
      {
      // Remove this grasp
      if (verbose)
      {
      ROS_INFO_STREAM_NAMED("filter","Pre-grasp solution colliding");
      visual_tools_->publishRobotState(robot_state_, rviz_visual_tools::RED);
      ros::Duration(1.0).sleep();
      }

      possible_grasps.erase(grasp_it);
      continue; // next grasp
      }
    */

    // Else
    grasp_it++; // move to next grasp
  }

  ROS_INFO_STREAM_NAMED("filter","After collision checking " << possible_grasps.size() << " grasps were found valid");

  if (verbose)
  {
    visual_tools_->hideRobot();
  }

  return true;
}


} // namespace