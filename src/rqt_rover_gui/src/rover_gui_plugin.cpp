// Author: Matthew Fricke
// E-mail: matthew@fricke.co.uk
// Date: 9-16-205
// Purpose: implementation of a simple graphical front end for the UNM-NASA Swarmathon rovers.
// License: GPL3

#include <rover_gui_plugin.h>
#include <pluginlib/class_list_macros.h>

#include <QProcess>
#include <QPalette>
#include <QTabBar>
#include <QTabWidget>
#include <QCheckBox>
#include <QRadioButton>
#include <QMessageBox>
#include <QStringList>
#include <QLCDNumber>
#include <std_msgs/Float32.h>
#include <std_msgs/UInt8.h>

//#include <regex> // For regex expressions

#include <cv_bridge/cv_bridge.h>
#include <opencv/cv.h>

using namespace std;

namespace rqt_rover_gui 
{
  RoverGUIPlugin::RoverGUIPlugin() : rqt_gui_cpp::Plugin(), widget(0)
  {
    setObjectName("RoverGUI");
    log_messages = "";

  }

  void RoverGUIPlugin::initPlugin(qt_gui_cpp::PluginContext& context)
  {
    cout << "Rover GUI Starting..." << endl;

    QStringList argv = context.argv();

    widget = new QWidget();

    ui.setupUi(widget);
    
    context.addWidget(widget);

    widget->setWindowTitle("Rover Interface");

    string rover_name_msg = "<font color='white'>Rover: " + selected_rover_name + "</font>";
    QString rover_name_msg_qstr = QString::fromStdString(rover_name_msg);
    ui.rover_name->setText(rover_name_msg_qstr);

    // Setup QT message connections
    connect(ui.rover_list, SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)), this, SLOT(currentRoverChangedEventHandler(QListWidgetItem*,QListWidgetItem*)));
    connect(ui.ekf_checkbox, SIGNAL(toggled(bool)), this, SLOT(EKFCheckboxToggledEventHandler(bool)));
    connect(ui.gps_checkbox, SIGNAL(toggled(bool)), this, SLOT(GPSCheckboxToggledEventHandler(bool)));
    connect(ui.encoder_checkbox, SIGNAL(toggled(bool)), this, SLOT(encoderCheckboxToggledEventHandler(bool)));
    connect(ui.autonomous_control_radio_button, SIGNAL(toggled(bool)), this, SLOT(autonomousRadioButtonEventHandler(bool)));
    connect(ui.joystick_control_radio_button, SIGNAL(toggled(bool)), this, SLOT(joystickRadioButtonEventHandler(bool)));
    connect(ui.build_simulation_button, SIGNAL(pressed()), this, SLOT(buildSimulationButtonEventHandler()));

    // Create a subscriber to listen for joystick events
    joystick_subscriber = nh.subscribe("/joy", 1000, &RoverGUIPlugin::joyEventHandler, this);

    displayLogMessage("Searching for rovers...");

    // Add discovered rovers to the GUI list
    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(pollRoversTimerEventHandler()));
    timer->start(5000);

    // Setup the initial display parameters for the map
    ui.map_frame->setDisplayGPSData(ui.gps_checkbox->isChecked());
    ui.map_frame->setDisplayEncoderData(ui.encoder_checkbox->isChecked());
    ui.map_frame->setDisplayEKFData(ui.ekf_checkbox->isChecked());

    ui.joystick_frame->setHidden(false);

    ui.tab_widget->setCurrentIndex(0);
  }

  void RoverGUIPlugin::shutdownPlugin()
  {
    //ros::shutdown();
  }

void RoverGUIPlugin::saveSettings(qt_gui_cpp::Settings& plugin_settings, qt_gui_cpp::Settings& instance_settings) const
{
}

void RoverGUIPlugin::restoreSettings(const qt_gui_cpp::Settings& plugin_settings, const qt_gui_cpp::Settings& instance_settings)
{
}


void RoverGUIPlugin::joyEventHandler(const sensor_msgs::Joy::ConstPtr& joy_msg)
{
    // Set the gui values
    if (joy_msg->axes[4] > 0)
       ui.joy_lcd_forward->display(joy_msg->axes[4]);
    if (joy_msg->axes[4] < 0)
        ui.joy_lcd_back->display(-joy_msg->axes[4]);

    if (joy_msg->axes[3] > 0)
      {
       ui.joy_lcd_left->display(joy_msg->axes[3]);
      }
    if (joy_msg->axes[3] < 0)
      {
        ui.joy_lcd_right->display(-joy_msg->axes[3]);
      }

// Magic axis values in the code below were taken the rover_driver_rqt_motor code /joystick output for default linear and angular velocities.
// Magic indicies are taken from rover_motor.cpp.
// This way the code is consistent with the existing GUI joystick.
// A better way would be to standardize a manual movement control interface and requre all input mechanisms to take input from the user
// and repackage te information according to the interface spec.
    sensor_msgs::Joy standardized_joy_msg;
    standardized_joy_msg.axes.resize(6);

    int x_axis = 0;
    int y_axis = 1;

    if (abs(joy_msg->axes[4]) > 0.05)
    {
      standardized_joy_msg.axes[y_axis] = joy_msg->axes[4];
    }

  if (abs(joy_msg->axes[3]) > 0.05)
    {
      standardized_joy_msg.axes[x_axis] = joy_msg->axes[3];
    }

  joystick_publisher.publish(standardized_joy_msg);

}

void RoverGUIPlugin::EKFEventHandler(const nav_msgs::Odometry::ConstPtr& msg)
{

    float x = msg->pose.pose.position.x;
    float y = msg->pose.pose.position.y;

    QString x_str; x_str.setNum(x);
    QString y_str; y_str.setNum(y);

   ui.map_frame->addToEKFRoverPath(x,y);
}


void RoverGUIPlugin::encoderEventHandler(const nav_msgs::Odometry::ConstPtr& msg)
{

    float x = msg->pose.pose.position.x;
    float y = msg->pose.pose.position.y;

    QString x_str; x_str.setNum(x);
    QString y_str; y_str.setNum(y);

   ui.map_frame->addToEncoderRoverPath(x,y);
}


void RoverGUIPlugin::GPSEventHandler(const nav_msgs::Odometry::ConstPtr& msg)
{

    float x = msg->pose.pose.position.x;
    float y = msg->pose.pose.position.y;

    QString x_str; x_str.setNum(x);
    QString y_str; y_str.setNum(y);

   ui.map_frame->addToGPSRoverPath(x,y);
}

 void RoverGUIPlugin::cameraEventHandler(const sensor_msgs::ImageConstPtr& image)
 {
     cv_bridge::CvImagePtr cv_image_ptr;

     try
     {
        cv_image_ptr = cv_bridge::toCvCopy(image);
     }
     catch (cv_bridge::Exception &e)
     {
         ROS_ERROR("In rover_gui_plugin.cpp: cv_bridge exception: %s", e.what());
     }

     int image_cols = cv_image_ptr->image.cols;
     int image_rows = cv_image_ptr->image.rows;
     int image_step = cv_image_ptr->image.step;

     ostringstream cols_stream, rows_stream;
     cols_stream << image_cols;
     rows_stream << image_rows;

    // ROS_INFO_STREAM("Image received Size:" + rows_stream.str() + "x" + cols_stream.str());

     // Create QImage to hold the image
     //const uchar* image_buffer = (const uchar*)cv_image_ptr->image.data; // extract the raw data
     QImage qimg(&(image->data[0]), image_cols, image_rows, image_step, QImage::Format_RGB888);
     ui.camera_frame->setImage(qimg);
 }

set<string> RoverGUIPlugin::findConnectedRovers()
{
    set<string> rovers;

    ros::master::V_TopicInfo master_topics;
    ros::master::getTopics(master_topics);

    stringstream ss;

   for (ros::master::V_TopicInfo::iterator it = master_topics.begin() ; it != master_topics.end(); it++)
    {
        const ros::master::TopicInfo& info = *it;

        string rover_name;

        std::size_t found = info.name.find("/status");
          if (found!=std::string::npos)
          {
            rover_name = info.name.substr(1,found-1);

            found = rover_name.find("/"); // Eliminate potential names with / in them
            if (found==std::string::npos)
            {
                rovers.insert(rover_name);
            }
        }
    }

    return rovers;
}

void RoverGUIPlugin::currentRoverChangedEventHandler(QListWidgetItem *current, QListWidgetItem *previous)
{
    selected_rover_name = current->text().toStdString();
    string rover_name_msg = "<font color='white'>Rover: " + selected_rover_name + "</font>";
    QString rover_name_msg_qstr = QString::fromStdString(rover_name_msg);
    ui.rover_name->setText(rover_name_msg_qstr);

    setupSubscribers();
    setupPublishers();


    // Clear map
    ui.map_frame->clearMap();

    // Enable control mode radio group now that a rover has been selected
    ui.autonomous_control_radio_button->setEnabled(true);
    ui.joystick_control_radio_button->setEnabled(true);

}

void RoverGUIPlugin::pollRoversTimerEventHandler()
{
    set<string>new_rover_names = findConnectedRovers();

    // Wait for a rover to connect
    if (new_rover_names.empty())
    {
        displayLogMessage("Waiting for rover to connect...");
        return;
    }

    if (new_rover_names == rover_names)
    {
        return;
    }

    rover_names = new_rover_names;

   displayLogMessage("List of connected rovers has changed");

    for(set<string>::const_iterator i = rover_names.begin(); i != rover_names.end(); ++i)
    {
        QListWidgetItem* new_item = new QListWidgetItem(QString::fromStdString(*i));
        new_item->setForeground(Qt::red);
        ui.rover_list->addItem(new_item);
    }
}

void RoverGUIPlugin::setupPublishers()
{
    // Set the robot to accept manual control. Latch so even if the robot connects later it will get the message.
    string control_mode_topic = "/"+selected_rover_name+"/mode";
    control_mode_publisher = nh.advertise<std_msgs::UInt8>(control_mode_topic, 10, true); // last argument sets latch to true

    string joystick_topic = "/"+selected_rover_name+"/joystick";
    joystick_publisher = nh.advertise<sensor_msgs::Joy>(joystick_topic, 10, this);
}

void RoverGUIPlugin::setupSubscribers()
{
    // Create a subscriber to listen for camera events
    image_transport::ImageTransport it(nh);
    int frame_rate = 1;
    // Theroa codex results in the least information being transmitted
    camera_subscriber = it.subscribe("/"+selected_rover_name+"/camera/image", frame_rate, &RoverGUIPlugin::cameraEventHandler, this);//, image_transport::TransportHints("theora"));

    // Odometry and GPS subscribers
    encoder_subscriber = nh.subscribe("/"+selected_rover_name+"/odom/", 10, &RoverGUIPlugin::encoderEventHandler, this);
    ekf_subscriber = nh.subscribe("/"+selected_rover_name+"/odom/ekf", 10, &RoverGUIPlugin::EKFEventHandler, this);
    gps_subscriber = nh.subscribe("/"+selected_rover_name+"/odom/navsat", 10, &RoverGUIPlugin::GPSEventHandler, this);


    // Ultrasound Subscriptions

    us_center_subscriber = nh.subscribe("/"+selected_rover_name+"/USCenter", 10, &RoverGUIPlugin::centerUSEventHandler, this);
    us_left_subscriber = nh.subscribe("/"+selected_rover_name+"/USLeft", 10, &RoverGUIPlugin::leftUSEventHandler, this);
    us_right_subscriber = nh.subscribe("/"+selected_rover_name+"/USRight", 10, &RoverGUIPlugin::rightUSEventHandler, this);


    // IMU Subscriptions
    imu_subscriber = nh.subscribe("/"+selected_rover_name+"/imu", 10, &RoverGUIPlugin::IMUEventHandler, this);

    // Target detected topic
   // target_detection_subscriber = nh.subscribe("/"+selected_rover_name+"/targets", 10, &RoverGUIPlugin::targetDetectedEventHandler, this);

}

void RoverGUIPlugin::centerUSEventHandler(const sensor_msgs::Range::ConstPtr& msg)
{
    ui.us_frame->setCenterRange(msg->range, msg->min_range, msg->max_range);
 }

void RoverGUIPlugin::rightUSEventHandler(const sensor_msgs::Range::ConstPtr& msg)
{
    ui.us_frame->setRightRange(msg->range, msg->min_range, msg->max_range);
}

void RoverGUIPlugin::leftUSEventHandler(const sensor_msgs::Range::ConstPtr& msg)
{
    ui.us_frame->setLeftRange(msg->range, msg->min_range, msg->max_range);
}

void RoverGUIPlugin::IMUEventHandler(const sensor_msgs::Imu::ConstPtr& msg)
{
   /* ui.imu_widget->setLinearAcceleration( msg->linear_acceleration.x,
                                         msg->linear_acceleration.y,
                                         msg->linear_acceleration.z );

    ui.imu_widget->setAngularVelocity(    msg->angular_velocity.x,
                                         msg->angular_velocity.y,
                                         msg->angular_velocity.z    );

    ui.imu_widget->setOrientation(        msg->orientation.w,
                                         msg->orientation.x,
                                         msg->orientation.y,
                                         msg->orientation.z        ); */

}

void RoverGUIPlugin::GPSCheckboxToggledEventHandler(bool checked)
{
    ui.map_frame->setDisplayGPSData(checked);
}

void RoverGUIPlugin::EKFCheckboxToggledEventHandler(bool checked)
{
    ui.map_frame->setDisplayEKFData(checked);
}

void RoverGUIPlugin::encoderCheckboxToggledEventHandler(bool checked)
{
    ui.map_frame->setDisplayEncoderData(checked);
}

// Currently broken. Calling displayLogMessage from the ROS event thread causes a crash or hang
//void RoverGUIPlugin::targetDetectedEventHandler(rover_onboard_target_detection::ATag tagInfo) //rover_onboard_target_detection::ATag msg )
//{
//    // Just let the user know the event happened
//   // displayLogMessage("Tag detected");

//}

void RoverGUIPlugin::displayLogMessage(QString msg)
{
    QString new_message = msg+"<br>";
    log_messages = log_messages+new_message;
    ui.log->setText("<font color='white'>"+log_messages+"</font>");
}

void RoverGUIPlugin::autonomousRadioButtonEventHandler(bool marked)
{
    if (!marked) return;

    ui.joystick_frame->setHidden(true);

    std_msgs::UInt8 control_mode_msg;
    control_mode_msg.data = 2; // 2 indicates autonomous control
    control_mode_publisher.publish(control_mode_msg);
    displayLogMessage(QString::fromStdString(selected_rover_name)+" changed to autonomous control");
}

void RoverGUIPlugin::joystickRadioButtonEventHandler(bool marked)
{
    if (!marked) return;

    ui.joystick_frame->setHidden(false);

    std_msgs::UInt8 control_mode_msg;
    control_mode_msg.data = 1; // 1 indicates manual control
    control_mode_publisher.publish(control_mode_msg);
    displayLogMessage(QString::fromStdString(selected_rover_name)+" changed to joystick control");

    QString return_msg = startROSJoyNode();
    displayLogMessage(return_msg);
}

void RoverGUIPlugin::buildSimulationButtonEventHandler()
{
    displayLogMessage("Building simulation...");

    QString return_msg;

    return_msg = sim_creator.startGazebo();

    cout << return_msg.toStdString() << endl;
    displayLogMessage(return_msg);

    displayLogMessage("Adding rover alpha...");
    return_msg = sim_creator.removeRover("alpha");
    displayLogMessage(return_msg);

    displayLogMessage("Adding ground plane...");
    return_msg = sim_creator.addGroundPlane("mars_ground_plane");
    displayLogMessage(return_msg);

//    displayLogMessage("Adding rover alpha...");
//    return_msg = sim_creator.addRover("alpha", -1, 0);
//    displayLogMessage(return_msg);

}

QString RoverGUIPlugin::startROSJoyNode()
{
    QStringList arguments;

    QProcess sh;
//    sh.start("sh", QStringList() << "-c" << "rosrun joy joy_node");
    //sh.start("sh -c who");

    sh.waitForFinished();
    QByteArray output = sh.readAll();
    sh.close();

    QString return_msg = "<br><font color='yellow'>" + output + "</font><br>";

    return return_msg;
}

} // End namespace



PLUGINLIB_EXPORT_CLASS(rqt_rover_gui::RoverGUIPlugin, rqt_gui_cpp::Plugin)

