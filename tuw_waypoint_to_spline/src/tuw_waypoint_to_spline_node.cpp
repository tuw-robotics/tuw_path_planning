/***************************************************************************
 *   Software License Agreement (BSD License)                              *
 *   Copyright (C) 2015 by Horatiu George Todoran <todorangrg@gmail.com>   *
 *                                                                         *
 *   Redistribution and use in source and binary forms, with or without    *
 *   modification, are permitted provided that the following conditions    *
 *   are met:                                                              *
 *                                                                         *
 *   1. Redistributions of source code must retain the above copyright     *
 *      notice, this list of conditions and the following disclaimer.      *
 *   2. Redistributions in binary form must reproduce the above copyright  *
 *      notice, this list of conditions and the following disclaimer in    *
 *      the documentation and/or other materials provided with the         *
 *      distribution.                                                      *
 *   3. Neither the name of the copyright holder nor the names of its      *
 *      contributors may be used to endorse or promote products derived    *
 *      from this software without specific prior written permission.      *
 *                                                                         *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS   *
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT     *
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS     *
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE        *
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,  *
 *   INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,  *
 *   BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;      *
 *   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER      *
 *   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT    *
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY *
 *   WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE           *
 *   POSSIBILITY OF SUCH DAMAGE.                                           *
 ***************************************************************************/


#include <tuw_waypoint_to_spline/tuw_waypoint_to_spline_node.h>

#include <iostream>
#include <fstream>
#include <vector>
#include "yaml-cpp/yaml.h"

using namespace tuw;
using namespace std;

int main ( int argc, char **argv ) {

    ros::init ( argc, argv, "tuw_waypoint_to_spline" );  /// initializes the ros node with default name
    ros::NodeHandle n;

    ros::Rate r ( 5 );
    Waypoint2SplineNode node ( n );
    r.sleep();


    while ( ros::ok() ) {

        node.publishSpline();

        /// calls all callbacks waiting in the queue
        ros::spinOnce();

        /// sleeps for the time remaining to let us hit our publish rate
        r.sleep();
    }
    return 0;
}

/**
 * Constructor
 **/
Waypoint2SplineNode::Waypoint2SplineNode ( ros::NodeHandle & n )
    : Waypoint2Spline(),
      n_ ( n ),
      n_param_ ( "~" ) {

    n_param_.param<std::string>( "global_frame", global_frame_id_, "map" );
    std::string path_file;
    n_param_.getParam( "path_file", path_file);
    n_param_.param<double>( "waypoints_distance", waypoints_distance_, 0.5);
    n_param_.param<string>( "path_tmp_file", path_tmp_file_, "/tmp/waypoints_to_spline.yaml");
    spline_msg_.header.seq = 0;
    if(!path_file.empty()) {
      constructSplineFromFile(path_file);
    }

    pubSplineData_    = n.advertise<tuw_spline_msgs::Spline> ( "path_spline"  , 1 );
    sub_path_ = n.subscribe ( "path", 1, &Waypoint2SplineNode::callbackPath, this );

//     reconfigureFnc_ = boost::bind ( &Gui2IwsNode::callbackConfigBlueControl, this,  _1, _2 );
//     reconfigureServer_.setCallback ( reconfigureFnc_ );
}

// void Gui2IwsNode::callbackConfigBlueControl ( tuw_teleop::Gui2IwsConfig &config, uint32_t level ) {
//     ROS_DEBUG ( "callbackConfigBlueControl!" );
//     config_ = config;
//     init();
// }

void Waypoint2SplineNode::constructSplineFromFile (const std::string &file) {

    YAML::Node waypoints_yaml = YAML::LoadFile(file);
    points_[0] = waypoints_yaml["x"].as<std::vector<double> >();
    points_[1] = waypoints_yaml["y"].as<std::vector<double> >();
    points_[2] = waypoints_yaml["o"].as<std::vector<double> >();

    size_t N = points_[0].size();
    if ( ( N > 0 ) && ( points_[0].size() == N ) && ( points_[1].size() == N ) && ( points_[2].size() == N ) ) {
        ROS_INFO ( "constructSplineFromParam!" );

        sleep ( 1 ); // the sleep was needed

        spline_msg_ = constructSplineMsg ();
        spline_msg_.header.frame_id = global_frame_id_;
        spline_msg_.header.stamp = ros::Time::now();
        spline_msg_.header.seq = spline_msg_.header.seq + 1;
    }
}


void Waypoint2SplineNode::callbackPath ( const nav_msgs::Path &msg ) {

    size_t N = msg.poses.size();
    for ( int i = 0; i < 3; i++ ) {
        points_[i].clear();
        points_[i].reserve ( N );
    }
    for ( size_t i = 0; i < N; i++ ) {
        const geometry_msgs::Pose &pose =  msg.poses[i].pose;
        if (( i > 0 ) && (i < (N-1))){
            double dx = points_[0].back() - pose.position.x;
            double dy = points_[1].back() - pose.position.y;
            double d = sqrt ( dx*dx+dy*dy );
            if ( d < waypoints_distance_ ) continue;
        }
        points_[0].push_back ( pose.position.x );
        points_[1].push_back ( pose.position.y );
        points_[2].push_back ( 0 );
    }
    if(!path_tmp_file_.empty()) {
      YAML::Node waypoints_yaml;
      waypoints_yaml["x"] = points_[0];
      waypoints_yaml["y"] = points_[1];
      waypoints_yaml["o"] = points_[2];
      std::ofstream fout(path_tmp_file_.c_str());
      fout << waypoints_yaml;
      ROS_INFO ("%s: %s","crated waypoint file: ", path_tmp_file_.c_str());
    }
    spline_msg_ = constructSplineMsg ();
    spline_msg_.header.frame_id = msg.header.frame_id;
    spline_msg_.header.stamp = msg.header.stamp;
    spline_msg_.header.seq = spline_msg_.header.seq + 1;

}

tuw_spline_msgs::Spline Waypoint2SplineNode::constructSplineMsg ( ) {

    fitSpline();
    Eigen::MatrixXd vKnots = spline_->knots();
    Eigen::MatrixXd mCtrls = spline_->ctrls();

    tuw_spline_msgs::Spline spline;
    spline.header.seq = 0;
    spline.knots.resize ( vKnots.cols() );
    for ( int i = 0; i < vKnots.cols(); ++i ) {
        spline.knots[i] = vKnots ( i );
    }
    spline.ctrls.resize ( mCtrls.rows() );
    for ( int i = 0; i < mCtrls.rows(); ++i ) {
        spline.ctrls[i].val.resize ( mCtrls.cols() );
        for ( int j = 0; j < mCtrls.cols(); ++j ) {
            spline.ctrls[i].val[j] = mCtrls ( i,j );
        }
    }
    return spline;
}
void Waypoint2SplineNode::publishSpline() {

    if ( spline_msg_.header.seq > 0 ) {
        pubSplineData_.publish ( spline_msg_ );
    }
}
