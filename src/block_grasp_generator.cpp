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

#include <block_grasp_generator/block_grasp_generator.h>

namespace block_grasp_generator
{

// Constructor
BlockGraspGenerator::BlockGraspGenerator(RobotVizToolsPtr rviz_tools) :
  rviz_tools_(rviz_tools)
{
}

// Deconstructor
BlockGraspGenerator::~BlockGraspGenerator()
{
  ROS_DEBUG_STREAM_NAMED("block_grasp_generator","Descontructor for Block Grasp Generator");
}

// Create all possible grasp positions for a block
bool BlockGraspGenerator::generateGrasps(const geometry_msgs::Pose& block_pose, const RobotGraspData& grasp_data,
  std::vector<manipulation_msgs::Grasp>& possible_grasps)
{
  // ---------------------------------------------------------------------------------------------
  // Create a transform from the block's frame (center of block) to /base_link
  tf::poseMsgToEigen(block_pose, block_global_transform_);

  // ---------------------------------------------------------------------------------------------
  // Calculate grasps in two axis in both directions
  //generateAxisGrasps( possible_grasps, X_AXIS, DOWN, grasp_data);
  //generateAxisGrasps( possible_grasps, X_AXIS, UP,   grasp_data);
  generateAxisGrasps( possible_grasps, Y_AXIS, DOWN, grasp_data);
  //generateAxisGrasps( possible_grasps, Y_AXIS, UP,   grasp_data);
  ROS_INFO_STREAM_NAMED("grasp", "Generated " << possible_grasps.size() << " grasps." );

  // Visualize results
  visualizeGrasps(possible_grasps, block_pose, grasp_data);

  return true;
}

// Create grasp positions in one axis
bool BlockGraspGenerator::generateAxisGrasps(std::vector<manipulation_msgs::Grasp>& possible_grasps, grasp_axis_t axis,
  grasp_direction_t direction, const RobotGraspData& grasp_data)
{

  // ---------------------------------------------------------------------------------------------
  // Grasp parameters

  // Create re-usable approach motion
  manipulation_msgs::GripperTranslation gripper_approach;
  gripper_approach.direction.header.stamp = ros::Time::now();
  gripper_approach.desired_distance = grasp_data.approach_retreat_desired_dist_; // The distance the origin of a robot link needs to travel
  gripper_approach.min_distance = grasp_data.approach_retreat_min_dist_; // half of the desired? Untested.

  // Create re-usable retreat motion
  manipulation_msgs::GripperTranslation gripper_retreat;
  gripper_retreat.direction.header.stamp = ros::Time::now();
  gripper_retreat.desired_distance = grasp_data.approach_retreat_desired_dist_; // The distance the origin of a robot link needs to travel
  gripper_retreat.min_distance = grasp_data.approach_retreat_min_dist_; // half of the desired? Untested.

  // Create re-usable blank pose
  geometry_msgs::PoseStamped grasp_pose_msg;
  grasp_pose_msg.header.stamp = ros::Time::now();
  grasp_pose_msg.header.frame_id = grasp_data.base_link_;

  // ---------------------------------------------------------------------------------------------
  // Variables needed for calculations
  double radius = grasp_data.grasp_depth_; //0.12
  double xb;
  double yb = 0.0; // stay in the y plane of the block
  double zb;
  double theta1 = 0.0; // Where the point is located around the block
  double theta2 = 0.0; // UP 'direction'

  // Gripper direction (UP/DOWN) rotation. UP set by default
  if( direction == DOWN )
  {
    theta2 = M_PI;
  }

  // ---------------------------------------------------------------------------------------------
  // ---------------------------------------------------------------------------------------------
  // Begin Grasp Generator Loop
  // ---------------------------------------------------------------------------------------------
  // ---------------------------------------------------------------------------------------------

  /* Developer Note:
   * Create angles 180 degrees around the chosen axis at given resolution
   * We create the grasps in the reference frame of the block, then later convert it to the base link
   */
  for(int i = 0; i <= grasp_data.angle_resolution_; ++i)
  {
    // Create a Grasp message
    manipulation_msgs::Grasp new_grasp;

    // Calculate grasp pose
    xb = radius*cos(theta1);
    zb = radius*sin(theta1);

    Eigen::Affine3d grasp_pose;

    switch(axis)
    {
    case X_AXIS:
      grasp_pose = Eigen::AngleAxisd(theta1, Eigen::Vector3d::UnitX())
        * Eigen::AngleAxisd(-0.5*M_PI, Eigen::Vector3d::UnitZ())
        * Eigen::AngleAxisd(theta2, Eigen::Vector3d::UnitX()); // Flip 'direction'

      grasp_pose.translation() = Eigen::Vector3d( yb, xb ,zb);

      break;
    case Y_AXIS:
      grasp_pose =
        Eigen::AngleAxisd(M_PI - theta1, Eigen::Vector3d::UnitY())
        *Eigen::AngleAxisd(theta2, Eigen::Vector3d::UnitX()); // Flip 'direction'

      grasp_pose.translation() = Eigen::Vector3d( xb, yb ,zb);

      break;
    case Z_AXIS:
      ROS_ERROR_STREAM_NAMED("grasp","Z Axis not implemented!");
      return false;

      break;
    }

    /* The estimated probability of success for this grasp, or some other measure of how "good" it is.
     * Here we base bias the score based on how far the wrist is from the surface, preferring a greater
     * distance to prevent wrist/end effector collision with the table
     */
    double score = sin(theta1);
    new_grasp.grasp_quality = std::max(score,0.1); // don't allow score to drop below 0.1 b/c all grasps are ok

    // Calculate the theta1 for next time
    theta1 += M_PI / grasp_data.angle_resolution_;

    // A name for this grasp
    static int grasp_id = 0;
    new_grasp.id = "Grasp" + boost::lexical_cast<std::string>(grasp_id);
    ++grasp_id;

    // PreGrasp and Grasp Postures --------------------------------------------------------------------------

    // The internal posture of the hand for the pre-grasp only positions are used
    new_grasp.pre_grasp_posture = grasp_data.pre_grasp_posture_;

    // The internal posture of the hand for the grasp positions and efforts are used
    new_grasp.grasp_posture = grasp_data.grasp_posture_;

    // Grasp ------------------------------------------------------------------------------------------------


    // DEBUG - show original grasp pose before tranform to gripper frame
    {
      tf::poseEigenToMsg(block_global_transform_ * grasp_pose, grasp_pose_msg.pose);
      rviz_tools_->publishArrow(grasp_pose_msg.pose, GREEN);
    }

    // Test 2
    /*
      {
      geometry_msgs::Pose grasp_pose_temp;
      tf::poseEigenToMsg(block_global_transform_, grasp_pose_temp);
      //ROS_ERROR_STREAM_NAMED("temp","block pose " << grasp_pose_temp);
      rviz_tools_->publishArrow(grasp_pose_temp, RED);
      }
    */

    // ------------------------------------------------------------------------
    // Change grasp to frame of reference of this custom end effector

    // Convert to Eigen
    Eigen::Affine3d eef_conversion_pose;
    tf::poseMsgToEigen(grasp_data.grasp_pose_to_eef_pose_, eef_conversion_pose);

    // Transform the grasp pose
    grasp_pose = grasp_pose * eef_conversion_pose;

    // ------------------------------------------------------------------------
    // Convert pose to global frame (base_link)
    tf::poseEigenToMsg(block_global_transform_ * grasp_pose, grasp_pose_msg.pose);

    // The position of the end-effector for the grasp relative to a reference frame (that is always specified elsewhere, not in this message)
    new_grasp.grasp_pose = grasp_pose_msg;

    // Other ------------------------------------------------------------------------------------------------

    // the maximum contact force to use while grasping (<=0 to disable)
    new_grasp.max_contact_force = 0;

    // -------------------------------------------------------------------------------------------------------
    // -------------------------------------------------------------------------------------------------------
    // Approach and retreat
    // -------------------------------------------------------------------------------------------------------
    // -------------------------------------------------------------------------------------------------------

    // Straight down ---------------------------------------------------------------------------------------
    // With respect to the base link/world frame

    /*
    // Approach
    gripper_approach.direction.header.frame_id = grasp_data.base_link_;
    gripper_approach.direction.vector.x = 0;
    gripper_approach.direction.vector.y = 0;
    gripper_approach.direction.vector.z = -1; // Approach direction (negative z axis)  // TODO: document this assumption
    new_grasp.approach = gripper_approach;

    // Retreat
    gripper_retreat.direction.header.frame_id = grasp_data.base_link_;
    gripper_retreat.direction.vector.x = 0;
    gripper_retreat.direction.vector.y = 0;
    gripper_retreat.direction.vector.z = 1; // Retreat direction (pos z axis)
    new_grasp.retreat = gripper_retreat;

    // Add to vector
    possible_grasps.push_back(new_grasp);
    */

    // Angled with pose -------------------------------------------------------------------------------------
    // Approach with respect to end effector orientation

    // Approach
    gripper_approach.direction.header.frame_id = grasp_data.ee_parent_link_;
    gripper_approach.direction.vector.x = 0;
    gripper_approach.direction.vector.y = 0;
    gripper_approach.direction.vector.z = 1;
    new_grasp.approach = gripper_approach;

    // Retreat
    gripper_retreat.direction.header.frame_id = grasp_data.ee_parent_link_;
    gripper_retreat.direction.vector.x = 0;
    gripper_retreat.direction.vector.y = 0;
    gripper_retreat.direction.vector.z = -1;
    new_grasp.retreat = gripper_retreat;

    // Add to vector
    possible_grasps.push_back(new_grasp);

    /*
    // Guessing -------------------------------------------------------------------------------------

    // Approach
    gripper_approach.direction.header.frame_id = grasp_data.ee_parent_link_;
    gripper_approach.direction.vector.x = -1;
    gripper_approach.direction.vector.y = 0;
    gripper_approach.direction.vector.z = 0;
    new_grasp.approach = gripper_approach;

    // Retreat
    gripper_retreat.direction.header.frame_id = grasp_data.ee_parent_link_;
    gripper_retreat.direction.vector.x = 1;
    gripper_retreat.direction.vector.y = 0;
    gripper_retreat.direction.vector.z = 0;
    new_grasp.retreat = gripper_retreat;

    // Add to vector
    possible_grasps.push_back(new_grasp);

    // Guessing -------------------------------------------------------------------------------------

    // Approach
    gripper_approach.direction.header.frame_id = grasp_data.ee_parent_link_;
    gripper_approach.direction.vector.x = 0;
    gripper_approach.direction.vector.y = 0;
    gripper_approach.direction.vector.z = -1;
    new_grasp.approach = gripper_approach;

    // Retreat
    gripper_retreat.direction.header.frame_id = grasp_data.ee_parent_link_;
    gripper_retreat.direction.vector.x = 0;
    gripper_retreat.direction.vector.y = 0;
    gripper_retreat.direction.vector.z = 1;
    new_grasp.retreat = gripper_retreat;

    // Add to vector
    possible_grasps.push_back(new_grasp);

    // Guessing -------------------------------------------------------------------------------------

    // Approach
    gripper_approach.direction.header.frame_id = grasp_data.ee_parent_link_;
    gripper_approach.direction.vector.x = 0;
    gripper_approach.direction.vector.y = -1;
    gripper_approach.direction.vector.z = 0;
    new_grasp.approach = gripper_approach;

    // Retreat
    gripper_retreat.direction.header.frame_id = grasp_data.ee_parent_link_;
    gripper_retreat.direction.vector.x = 0;
    gripper_retreat.direction.vector.y = 1;
    gripper_retreat.direction.vector.z = 0;
    new_grasp.retreat = gripper_retreat;

    // Add to vector
    possible_grasps.push_back(new_grasp);

    // Guessing -------------------------------------------------------------------------------------

    // Approach
    gripper_approach.direction.header.frame_id = grasp_data.ee_parent_link_;
    gripper_approach.direction.vector.x = 0;
    gripper_approach.direction.vector.y = 1;
    gripper_approach.direction.vector.z = 0;
    new_grasp.approach = gripper_approach;

    // Retreat
    gripper_retreat.direction.header.frame_id = grasp_data.ee_parent_link_;
    gripper_retreat.direction.vector.x = 0;
    gripper_retreat.direction.vector.y = -1;
    gripper_retreat.direction.vector.z = 0;
    new_grasp.retreat = gripper_retreat;

    // Add to vector
    possible_grasps.push_back(new_grasp);

    */
  }

  return true;
}

// Show all grasps in Rviz
void BlockGraspGenerator::visualizeGrasps(const std::vector<manipulation_msgs::Grasp>& possible_grasps,
  const geometry_msgs::Pose& block_pose, const RobotGraspData& grasp_data)
{
  if(rviz_tools_->isMuted())
  {
    ROS_DEBUG_STREAM_NAMED("grasp","Not visualizing grasps - muted.");
    return;
  }

  ROS_DEBUG_STREAM_NAMED("grasp","Visualizing " << possible_grasps.size() << " grasps");

  int i = 0;
  for(std::vector<manipulation_msgs::Grasp>::const_iterator grasp_it = possible_grasps.begin();
      grasp_it < possible_grasps.end(); ++grasp_it)
  {
    if( !ros::ok() )  // Check that ROS is still ok and that user isn't trying to quit
      break;

    // Make sure block is still visible
    rviz_tools_->publishBlock(block_pose, grasp_data.block_size_, false);

    ++i;

    //ROS_DEBUG_STREAM_NAMED("grasp","Visualizing grasp pose " << i);

    // Animate or just show final position?
    if( true )
    {
      animateGrasp(*grasp_it, grasp_data);
    }
    else
    {
      rviz_tools_->publishSphere(grasp_it->grasp_pose.pose);
      rviz_tools_->publishArrow(grasp_it->grasp_pose.pose, BLUE);
      rviz_tools_->publishEEMarkers(grasp_it->grasp_pose.pose);
    }
    //ROS_INFO_STREAM_NAMED("","Grasp: \n" << grasp_it->grasp_pose.pose);

    // Show robot joint positions if available
    /*
      if( grasp_it->grasp_posture.position.size() > 1 )
      {
      ROS_WARN_STREAM_NAMED("temp","HAS IK SOLUTION - Positions:");
      std::copy(grasp_it->grasp_posture.position.begin(), grasp_it->grasp_posture.position.end(), std::ostream_iterator<double>(std::cout, "\n"));
      rviz_tools_->publishPlanningScene(grasp_it->grasp_posture.position);
      ros::Duration(5.0).sleep();
      }
    */

    ros::Duration(0.001).sleep();
    //ros::Duration(1.00).sleep();
  }
}

void BlockGraspGenerator::animateGrasp(const manipulation_msgs::Grasp &grasp, const RobotGraspData& grasp_data)
{
  /*
    Eigen::Affine3d grasp_pose;
    // Convert grasp pose to Eigen affine
    tf::poseMsgToEigen(grasp_pose_msg.pose, grasp_pose);
    grasp_pose.translation() = Eigen::Vector3d( direction_scaled.x, direction_scaled.y, direction_scaled.z );
  */

  ROS_DEBUG_STREAM_NAMED("temp","Original Grasp: \n" << grasp.grasp_pose.pose);

  // Display Grasp Score
  std::string text = "Grasp Quality: " + boost::lexical_cast<std::string>(int(grasp.grasp_quality*100)) + "%";
  rviz_tools_->publishText(grasp.grasp_pose.pose, text);

  // Temp
  //  rviz_tools_->publishEEMarkers(grasp.grasp_pose.pose, GREEN, "ee2");
  rviz_tools_->publishArrow(grasp.grasp_pose.pose, GREEN);

  // Show pre-grasp position
  geometry_msgs::Pose pre_grasp_pose;

  /*
  // Convert the grasp pose into the frame of reference of the approach/retreat frame_id
  if( grasp.approach.direction.header.frame_id == grasp_data.ee_parent_link_ )
  {
    ROS_ERROR_STREAM_NAMED("temp","testing pose ----------------");

    Eigen::Affine3d eigen_grasp_pose;
    tf::poseMsgToEigen(grasp.grasp_pose.pose, eigen_grasp_pose);

    // convert approach direction to Eigen structures
    //    Eigen::Vector3d approach_direction;
    //    tf::vectorMsgToEigen(grasp.approach.direction.vector, approach_direction);

    //    eigen_grasp_pose.translation() = approach_direction*0.01;
    //    eigen_grasp_pose.translation() = eigen_grasp_pose.translation() + Eigen::Vector3d( 0, 0, 0.1 );
    Eigen::Affine3d eigen_conversion = eigen_grasp_pose * Eigen::Translation3f( 0.1,0,0);
    //    eigen_conversion.translation() = Eigen::Vector3d( 0.1, 0, 0 );
    //    eigen_conversion *= block_global_transform_;

    tf::poseEigenToMsg(eigen_conversion, pre_grasp_pose);
    rviz_tools_->publishArrow(pre_grasp_pose, RED);

    // Try 2
    eigen_conversion = eigen_grasp_pose * Eigen::Translation3f( 0,0.1,0);
    tf::poseEigenToMsg(eigen_conversion, pre_grasp_pose);
    rviz_tools_->publishArrow(pre_grasp_pose, ORANGE);

    // Convert back to regular pose
    tf::poseEigenToMsg(eigen_grasp_pose*eigen_conversion, pre_grasp_pose);
    rviz_tools_->publishArrow(pre_grasp_pose, BLUE);
  }
  //  ros::Duration(30).sleep();
  exit(0);
  return;
  */

  /*
  // convert approach direction and retreat direction to Eigen structures
  Eigen::Vector3d approach_direction;
  tf::vectorMsgToEigen(grasp.approach.direction.vector, approach_direction);

  ROS_WARN_STREAM_NAMED("temp","approach direction:\n" << approach_direction);

  // if translation vectors are specified in the frame of the ik link name, then we assume the
  // frame is local; otherwise, the frame is global
  bool approach_direction_is_global_frame = !robot_state::Transforms::sameFrame(
  grasp.approach.direction.header.frame_id, grasp_data.ee_parent_link_);

  ROS_DEBUG_STREAM_NAMED("temp","approach_direction_is_global_frame = " << approach_direction_is_global_frame);

  // transform the input vectors in accordance to frame specified in the header;
  if (approach_direction_is_global_frame)
  approach_direction = rviz_tools_->getPlanningSceneMonitor()->getPlanningScene()->
  getFrameTransform(grasp.approach.direction.header.frame_id).rotation() * approach_direction;

  ROS_WARN_STREAM_NAMED("temp","approach direction2:\n" << approach_direction);
  */

  ROS_INFO_STREAM_NAMED("temp","grasp pose: \n" << grasp.grasp_pose.pose);



  // Animate the movement
  double animation_resulution = 0.1; // the higher the better the resolution
  for(double percent = 0; percent < 1; percent += animation_resulution)
  {
    if( !ros::ok() )  // Check that ROS is still ok and that user isn't trying to quit
      break;

    // Calculate the current animation position based on the percent
    pre_grasp_pose = grasp.grasp_pose.pose;
    pre_grasp_pose.position.x -= grasp.approach.direction.vector.x * grasp.approach.desired_distance * (1-percent);
    pre_grasp_pose.position.y -= grasp.approach.direction.vector.y * grasp.approach.desired_distance * (1-percent);
    pre_grasp_pose.position.z -= grasp.approach.direction.vector.z * grasp.approach.desired_distance * (1-percent);

    //rviz_tools_->publishArrow(pre_grasp_pose, BLUE);
    rviz_tools_->publishEEMarkers(pre_grasp_pose);

    ros::Duration(0.001).sleep();
  }

  /*
  // Show grasp position
  rviz_tools_->publishSphere(grasp.grasp_pose.pose);
  rviz_tools_->publishArrow(grasp.grasp_pose.pose, BLUE);
  rviz_tools_->publishEEMarkers(grasp.grasp_pose.pose);

  ros::Duration(1.00).sleep();
  */
}


} // namespace
