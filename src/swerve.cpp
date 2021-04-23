//
// Created by qiayuan on 4/23/21.
//

#include "rm_chassis_controllers/swerve.h"

#include <angles/angles.h>

namespace rm_chassis_controllers {

bool SwerveController::init(hardware_interface::RobotHW *robot_hw,
                            ros::NodeHandle &root_nh,
                            ros::NodeHandle &controller_nh) {
  if (!ChassisBase::init(robot_hw, root_nh, controller_nh))
    return false;
  XmlRpc::XmlRpcValue modules;
  controller_nh.getParam("modules", modules);
  ROS_ASSERT(modules.getType() == XmlRpc::XmlRpcValue::TypeStruct);
  for (const auto &module:modules) {
    ROS_ASSERT(modules.hasMember("position"));
    ROS_ASSERT(modules["position"].getType() == XmlRpc::XmlRpcValue::TypeArray);
    ROS_ASSERT(modules["position"].size() == 2);
    ROS_ASSERT(modules.hasMember("wheel_radius"));
    ROS_ASSERT(modules.hasMember("pivot"));
    ROS_ASSERT(modules["pivot"].getType() == XmlRpc::XmlRpcValue::TypeStruct);
    ROS_ASSERT(modules["pivot"].hasMember("name"));
    ROS_ASSERT(modules.hasMember("wheel"));
    ROS_ASSERT(modules["wheel"].getType() == XmlRpc::XmlRpcValue::TypeStruct);
    ROS_ASSERT(modules["wheel"].hasMember("name"));
    ROS_ASSERT(modules["wheel"].hasMember("radius"));

    auto *effort_jnt_interface = robot_hw->get<hardware_interface::EffortJointInterface>();
    Module m{.position_= Vec2<double>(modules["position"][0], modules["position"][1]),
        .wheel_radius_ = modules["wheel"]["radius"],
        .joint_pivot_ =effort_jnt_interface->getHandle(modules["pivot"]["name"]),
        .joint_wheel_ =effort_jnt_interface->getHandle(modules["wheel"]["name"]),
        .pid_pivot_ = control_toolbox::Pid(),
        .pid_wheel_ = control_toolbox::Pid()};
    if (!m.pid_pivot_.init(ros::NodeHandle(controller_nh, "pivot/pid")) ||
        !m.pid_wheel_.init(ros::NodeHandle(controller_nh, "wheel/pid")))
      return false;
    if (modules["pivot"].hasMember("offset"))
      m.pivot_offset_ = modules["pivot"]["offset"];

    modules_.push_back(m);
  }
}

// Ref: https://dominik.win/blog/programming-swerve-drive/

void SwerveController::moveJoint(const ros::Duration &period) {
  Vec2<double> vel_center(vel_tfed_.vector.x, vel_tfed_.vector.y);
  for (auto &module:modules_) {
    Vec2<double> vel = vel_center + vel_tfed_.vector.z * module.position_;
    double vel_angle = std::atan2(vel.y(), vel.x()) + module.pivot_offset_;
    // Direction flipping and Stray module mitigation
    double error_pivot = std::min(
        angles::shortest_angular_distance(module.joint_pivot_.getPosition(), vel_angle),
        angles::shortest_angular_distance(module.joint_pivot_.getPosition(), vel_angle + M_PI));
    double wheel_des = vel.norm() / module.wheel_radius_
        * std::cos(angles::shortest_angular_distance(module.joint_pivot_.getPosition(), vel_angle));
    double error_wheel = wheel_des - module.joint_wheel_.getVelocity();
    // PID
    module.pid_pivot_.computeCommand(error_pivot, period);
    module.pid_wheel_.computeCommand(error_wheel, period);
    module.joint_pivot_.setCommand(module.pid_pivot_.getCurrentCmd());
  }
  // Effort limit
  double scale = getEffortLimitScale();
  for (auto &module:modules_)
    module.joint_wheel_.setCommand(scale * module.pid_wheel_.getCurrentCmd());
}

}
