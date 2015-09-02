#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <aruco/aruco.h>

#include <aruco/cvdrawingutils.h>
#include "/usr/local/include/aruco/highlyreliablemarkers.h"

#include <opencv2/core/core.hpp>
#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>
#include "epuck_tracking/utils.h"
#include <tf/transform_broadcaster.h>
#include <tf/transform_listener.h>

using namespace aruco;

class EPuckTracking
{
	private:
		cv::Mat inImage, resultImg;
		aruco::CameraParameters camParam;
		bool useRectifiedImages;
		bool draw_markers;
		bool draw_markers_cube;
		bool draw_markers_axis;
		MarkerDetector mDetector;
		vector<Marker> markers;
		BoardConfiguration TheBoardConfig;
		BoardDetector TheBoardDetector;
		Board TheBoardDetected;
		ros::Subscriber cam_info_sub;
		bool cam_info_received;
		image_transport::Publisher image_pub;
		image_transport::Publisher debug_pub;
		ros::Publisher pose_pub;
		ros::Publisher transform_pub; 
		ros::Publisher position_pub;
		std::string board_frame;
		Dictionary D;
		double marker_size;
		std::string board_config;

		ros::NodeHandle nh;
		image_transport::ImageTransport it;
		image_transport::Subscriber image_sub;

		tf::TransformListener _tfListener;

	public:
		EPuckTracking()
			: cam_info_received(false),
			nh("~"),
			it(nh)
		{
			image_sub = it.subscribe("/camera1/image_rect_color", 1, &EPuckTracking::image_callback, this);
			cam_info_sub = nh.subscribe("/camera1/camera_info", 1, &EPuckTracking::cam_info_callback, this);
			marker_size = 0.05;
			image_pub = it.advertise("result", 1);
			debug_pub = it.advertise("debug", 1);
			pose_pub = nh.advertise<geometry_msgs::PoseStamped>("pose", 100);
			transform_pub = nh.advertise<geometry_msgs::TransformStamped>("transform", 100);
			position_pub = nh.advertise<geometry_msgs::Vector3Stamped>("position", 100);
            if (D.fromFile("/home/filippo/catkin_ws/src/epuck_tracking/data/dizionario.yml") == false)
				ROS_ERROR_STREAM("impossibile aprire dizionario");
			ROS_INFO_STREAM("dictsize "<<D.size());
			if(!HighlyReliableMarkers::loadDictionary(D))
				ROS_INFO("problema con dizionario");
			mDetector.setMakerDetectorFunction(aruco::HighlyReliableMarkers::detect);
			//mDetector.setThresholdMethod(aruco::MarkerDetector::CANNY);
			
	                mDetector.setThresholdParams( 21, 7);
	                mDetector.setCornerRefinementMethod(aruco::MarkerDetector::LINES);
	                mDetector.setWarpSize((D[0].n()+2)*8);
	                mDetector.setMinMaxSize(0.005, 0.5);
			TheBoardConfig.readFromFile("/home/filippo/catkin_ws/src/epuck_tracking/data/board3.yml");
			TheBoardDetector.setYPerperdicular(false);
        		TheBoardDetector.setParams(TheBoardConfig, camParam, marker_size);
        		TheBoardDetector.getMarkerDetector().setThresholdParams(21, 7); // for blue-green markers, the window size has to be larger
        		//TheBoardDetector.getMarkerDetector().getThresholdParams(ThresParam1, ThresParam2);
       			TheBoardDetector.getMarkerDetector().setMakerDetectorFunction(aruco::HighlyReliableMarkers::detect);
			TheBoardDetector.getMarkerDetector().setCornerRefinementMethod(aruco::MarkerDetector::LINES);
        		TheBoardDetector.getMarkerDetector().setWarpSize((D[0].n() + 2) * 8);
      			TheBoardDetector.getMarkerDetector().setMinMaxSize(0.005, 0.5);
			ROS_INFO("setup completo");
			/*
			nh.param<double>("marker_size", marker_size, 0.05);
			nh.param<std::string>("board_config", board_config, "boardConfiguration.yml");
			nh.param<std::string>("board_frame", board_frame, "");
			nh.param<bool>("image_is_rectified", useRectifiedImages, true);
			nh.param<bool>("draw_markers", draw_markers, false);
			nh.param<bool>("draw_markers_cube", draw_markers_cube, false);
			nh.param<bool>("draw_markers_axis", draw_markers_axis, false);
			git test
			the_board_config.readFromFile(board_config.c_str());*/

			ROS_INFO("Epuck_tracking node started with marker size of %f m and board configuration: %s",
					 marker_size, "board3");
		}

		void image_callback(const sensor_msgs::ImageConstPtr& msg)
		{
			if(!cam_info_received) return;
			static tf::TransformBroadcaster br;
			cv_bridge::CvImagePtr cv_ptr;
			try
			{
				cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::RGB8);
				inImage = cv_ptr->image;
				resultImg = cv_ptr->image.clone();

				//detection results will go into "markers"
				markers.clear();
				//Ok, let's detect
				mDetector.detect(inImage, markers, camParam, marker_size);
				float probDetect = 0.;
				probDetect=TheBoardDetector.detect(markers, TheBoardConfig,TheBoardDetected, camParam, marker_size);
				
			              if (probDetect > 0.2) {
                   			 CvDrawingUtils::draw3dAxis(resultImg, TheBoardDetected, camParam);
               			      }
           			
				tf::Transform boardtf = ar_sys::getTf(TheBoardDetected.Rvec,TheBoardDetected.Tvec);
				tf::StampedTransform stampedTransform(boardtf, msg->header.stamp, "world", "board");
				br.sendTransform(stampedTransform);
				//ROS_INFO_STREAM(markers.size());
				for (int marker_index = 0; marker_index < markers.size(); marker_index++)
				{
					if(markers[marker_index].id<8){	
					
						tf::Transform transform = ar_sys::getTf(markers[marker_index].Rvec, markers[marker_index].Tvec);
						
						tf::StampedTransform stampedTransform(transform, msg->header.stamp, "world", boost::to_string(markers[marker_index].id));

						geometry_msgs::PoseStamped poseMsg;
						tf::poseTFToMsg(transform, poseMsg.pose);
						poseMsg.header.frame_id = msg->header.frame_id;
						poseMsg.header.stamp = msg->header.stamp;
						pose_pub.publish(poseMsg);

						geometry_msgs::TransformStamped transformMsg;
						tf::transformStampedTFToMsg(stampedTransform, transformMsg);
						transform_pub.publish(transformMsg);
						br.sendTransform(stampedTransform);
						geometry_msgs::Vector3Stamped positionMsg;
						positionMsg.header = transformMsg.header;
						positionMsg.vector = transformMsg.transform.translation;
						position_pub.publish(positionMsg);

						}
					
				}
				
				//for each marker, draw info and its boundaries in the image
				/*for(size_t i=0; draw_markers && i < markers.size(); ++i)
				{
					markers[i].draw(resultImg,cv::Scalar(0,0,255),2);
				}

*/
				if(camParam.isValid() && marker_size != -1)
				{
					//draw a 3d cube in each marker if there is 3d info
					for(size_t i=0; i<markers.size(); ++i)
					{

						if (draw_markers_cube && markers[i].id<8) CvDrawingUtils::draw3dCube(resultImg, markers[i], camParam);
						if (draw_markers_axis=true && markers[i].id<8) CvDrawingUtils::draw3dAxis(resultImg, markers[i], camParam);
					}
					//draw board axis
					//if (probDetect > 0.0) CvDrawingUtils::draw3dAxis(resultImg, the_board_detected, camParam);
				}

				if(image_pub.getNumSubscribers() > 0)
				{
					//show input with augmented information
					cv_bridge::CvImage out_msg;
					out_msg.header.frame_id = msg->header.frame_id;
					out_msg.header.stamp = msg->header.stamp;
					out_msg.encoding = sensor_msgs::image_encodings::RGB8;
					out_msg.image = resultImg;
					image_pub.publish(out_msg.toImageMsg());
				}

				if(debug_pub.getNumSubscribers() > 0)
				{
					//show also the internal image resulting from the threshold operation
					cv_bridge::CvImage debug_msg;
					debug_msg.header.frame_id = msg->header.frame_id;
					debug_msg.header.stamp = msg->header.stamp;
					debug_msg.encoding = sensor_msgs::image_encodings::MONO8;
					debug_msg.image = mDetector.getThresholdedImage();
					debug_pub.publish(debug_msg.toImageMsg());
				}
			}
			catch (cv_bridge::Exception& e)
			{
				ROS_ERROR("cv_bridge exception: %s", e.what());
				return;
			}
		}

		// wait for one camerainfo, then shut down that subscriber
		void cam_info_callback(const sensor_msgs::CameraInfo &msg)
		{
			camParam = ar_sys::getCamParams(msg, useRectifiedImages);
			cam_info_received = true;
			cam_info_sub.shutdown();
		}
};


int main(int argc,char **argv)
{
	ros::init(argc, argv, "epuck_tracking");

	EPuckTracking node;

	ros::spin();
}