
#include "kimm_franka_gui/kimm_franka_gui.h"
#include <pluginlib/class_list_macros.h>
#include <iostream>
#include <vector>
#include <QDebug>
#include "qxtglobalshortcut.h"

using namespace pinocchio;

namespace kimm_franka_gui
{
   FrankaGui::FrankaGui()
   : rqt_gui_cpp::Plugin(), widget_(0)
    {
        qRegisterMetaType<std_msgs::Float32ConstPtr>();
        qRegisterMetaType<std_msgs::Float64ConstPtr>();
        qRegisterMetaType<sensor_msgs::JointStateConstPtr>();
        qRegisterMetaType<mujoco_ros_msgs::JointSetConstPtr>();
        qRegisterMetaType<geometry_msgs::TransformConstPtr>();

        setObjectName("FrankaGui");
        nh_.getParam("/issimulation", issimulation_);  
        nh_.getParam("/issimulation", issimulation_);  


        string model_path, urdf_name;
        string group_name;
        nh_.getParam("/robot_group", group_name);   
        nh_.getParam("/" + group_name +"/rviz_urdf_path", model_path);    
        nh_.getParam("/" + group_name +"/rviz_urdf", urdf_name);  

        vector<string> package_dirs;
        package_dirs.push_back(model_path);
        string urdfFileName = package_dirs[0] + urdf_name;
        pinocchio::urdf::buildModel(urdfFileName, model_, false);       
        ROS_INFO_STREAM(urdfFileName);
        run_pub_ = nh_.advertise<std_msgs::Bool>("/mujoco_ros_interface/sim_run", 100);
        quit_pub_ = nh_.advertise<std_msgs::Bool>("/mujoco_ros_interface/sim_quit", 100);

        
        ee_traj_resp_pub_ = nh_.advertise<visualization_msgs::MarkerArray>("/" + group_name + "/kimm_se3_plan_markers/robot/response", 100);
        joint_state_pub_ = nh_.advertise<sensor_msgs::JointState>("/" + group_name +"/joint_states", 100);

        if (issimulation_){
            custom_ctrl_pub_ = nh_.advertise<std_msgs::Int16>("/mujoco_ros_interface/ctrl_type", 100);
            simtime_sub_ = nh_.subscribe("/mujoco_ros_interface/sim_time", 100, &FrankaGui::timerCallback, this);
            jointstate_sub_ = nh_.subscribe("/mujoco_ros_interface/joint_states", 100, &FrankaGui::jointStateCallback, this);
            torquestate_sub_ = nh_.subscribe("/mujoco_ros_interface/joint_set", 100, &FrankaGui::torqueStateCallback, this);
            ee_state_sub_ = nh_.subscribe("/mujoco_ros_interface/ee_state", 100, &FrankaGui::eeStateCallback, this);
        }
        else {
            custom_ctrl_pub_ = nh_.advertise<std_msgs::Int16>("/" + group_name + "/real_robot/ctrl_type", 100);
            simtime_sub_ = nh_.subscribe("/" + group_name + "/time", 100, &FrankaGui::timerCallback, this);
            jointstate_sub_ = nh_.subscribe("/" + group_name + "/real_robot/joint_states", 100, &FrankaGui::jointStateCallback, this);
            torquestate_sub_ = nh_.subscribe("/" + group_name + "/real_robot/joint_set", 100, &FrankaGui::torqueStateCallback, this);
            ee_state_sub_ = nh_.subscribe("/" + group_name + "/real_robot/ee_state", 100, &FrankaGui::eeStateCallback, this);
        }
	

        joint_plan_client_ = nh_.serviceClient<kimm_joint_planner_ros_interface::plan_joint_path>("/" + group_name + "/kimm_joint_planner_ros_interface_server/plan_joint_path");
        se3_plan_client_ = nh_.serviceClient<kimm_se3_planner_ros_interface::plan_se3_path>("/" + group_name + "/kimm_se3_planner_ros_interface_server/plan_se3_path");

        int nq = 9;
        joint_state_msg_.name.resize(nq);
        joint_state_msg_.position.resize(nq);
        joint_state_msg_.velocity.resize(nq);
        joint_state_msg_.effort.resize(nq);
        
        for (int i=0; i<nq; i++){
            joint_state_msg_.name[i] = model_.names[i+1];
            joint_state_msg_.position[i] = 0.0;
            joint_state_msg_.velocity[i] = 0.0;
            joint_state_msg_.effort[i] = 0.0;
        }

        q_.setZero(9);
        x_.setZero(6);
    }
    void FrankaGui::initPlugin(qt_gui_cpp::PluginContext& context)
    {
        widget_ = new QWidget();
        ui_.setupUi(widget_);

        if (context.serialNumber() > 1)
        {
            widget_->setWindowTitle(widget_->windowTitle() + " (" + QString::number(context.serialNumber()) + ")");
        }
        context.addWidget(widget_);

        ui_.simulon_button->setShortcut(QKeySequence(Qt::Key_R));
        ui_.simuloff_button->setShortcut(QKeySequence(Qt::Key_G));
        ui_.grasp_button->setShortcut(QKeySequence(Qt::Key_Z));

        // Basic Interface from Mujoco
        connect(this, &FrankaGui::timerCallback, this, &FrankaGui::Timercb);
        connect(this, &FrankaGui::jointStateCallback, this, &FrankaGui::Jointcb);
        connect(this, &FrankaGui::torqueStateCallback, this, &FrankaGui::Torquecb);
        connect(this, &FrankaGui::eeStateCallback, this, &FrankaGui::EEcb);

        // Basic Interface to Mujoco
        connect(ui_.simulon_button, SIGNAL(pressed()), this, SLOT(EnableSimulCallback()));
        connect(ui_.simuloff_button, SIGNAL(pressed()), this, SLOT(DisableSimulCallback()));
        connect(ui_.quit_button, SIGNAL(pressed()), this, SLOT(QuitCallback()));

        connect(ui_.init_button, SIGNAL(pressed()), this, SLOT(InitCallback()));
        connect(ui_.grasp_button, SIGNAL(pressed()), this, SLOT(GraspCallback()));
       
        // Custom Controller Interface to Mujoco
        connect(ui_.custom_ctrl_btn, SIGNAL(pressed()), this, SLOT(customctrlbtn()));
        ui_.custom_ctrl_btn->setShortcut(QKeySequence(Qt::Key_1));
        connect(ui_.Customctrlbtn_1, SIGNAL(pressed()), this, SLOT(CustomCtrl1Callback()));
        connect(ui_.Customctrlbtn_2, SIGNAL(pressed()), this, SLOT(CustomCtrl2Callback()));
        connect(ui_.Customctrlbtn_3, SIGNAL(pressed()), this, SLOT(CustomCtrl3Callback()));
        connect(ui_.Customctrlbtn_4, SIGNAL(pressed()), this, SLOT(CustomCtrl4Callback()));
        connect(ui_.Customctrlbtn_5, SIGNAL(pressed()), this, SLOT(CustomCtrl5Callback()));
        connect(ui_.Customctrlbtn_6, SIGNAL(pressed()), this, SLOT(CustomCtrl6Callback()));
        connect(ui_.Customctrlbtn_7, SIGNAL(pressed()), this, SLOT(CustomCtrl7Callback()));
        connect(ui_.Customctrlbtn_8, SIGNAL(pressed()), this, SLOT(CustomCtrl8Callback()));
        connect(ui_.Customctrlbtn_9, SIGNAL(pressed()), this, SLOT(CustomCtrl9Callback()));
        connect(ui_.Customctrlbtn_10, SIGNAL(pressed()), this, SLOT(CustomCtrl10Callback()));

        // Joint Controller Interface to Mujoco
        connect(ui_.joint_ctrl_btn, SIGNAL(pressed()), this, SLOT(jointctrlbtn()));
        ui_.joint_ctrl_btn->setShortcut(QKeySequence(Qt::Key_2));
        connect(ui_.joint_trajectory_send, SIGNAL(pressed()), this, SLOT(jointctrlcb()));
        connect(ui_.joint_trajectory_send_2, SIGNAL(pressed()), this, SLOT(jointctrlcb2()));
        connect(ui_.c_joint, SIGNAL(pressed()), this, SLOT(jointctrlcb3()));
        connect(ui_.waypoint_add_btn, SIGNAL(pressed()), this, SLOT(jointctrlcb4()));
        connect(ui_.waypoint_clear_btn, SIGNAL(pressed()), this, SLOT(jointctrlcb5()));

        // SE3 Controller Interface to Mujoco
        connect(ui_.se3_ctrl_btn, SIGNAL(pressed()), this, SLOT(se3ctrlbtn()));
        ui_.se3_ctrl_btn->setShortcut(QKeySequence(Qt::Key_3));
        connect(ui_.se3_trajectory_send_1, SIGNAL(pressed()), this, SLOT(se3ctrlcb1()));
        connect(ui_.se3_trajectory_send_2, SIGNAL(pressed()), this, SLOT(se3ctrlcb2()));
        connect(ui_.c_joint_2, SIGNAL(pressed()), this, SLOT(se3ctrlcb3()));
        connect(ui_.waypoint_add_btn_2, SIGNAL(pressed()), this, SLOT(se3ctrlcb4()));
        connect(ui_.waypoint_clear_btn_2, SIGNAL(pressed()), this, SLOT(se3ctrlcb5()));
    }

} // namespace kimm_franka_gui

PLUGINLIB_EXPORT_CLASS(kimm_franka_gui::FrankaGui, rqt_gui_cpp::Plugin)
