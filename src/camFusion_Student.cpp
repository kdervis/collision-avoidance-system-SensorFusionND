
#include <iostream>
#include <algorithm>
#include <numeric>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "camFusion.hpp"
#include "dataStructures.h"

using namespace std;


// Create groups of Lidar points whose projection into the camera falls into the same bounding box
void clusterLidarWithROI(std::vector<BoundingBox> &boundingBoxes, std::vector<LidarPoint> &lidarPoints, float shrinkFactor, cv::Mat &P_rect_xx, cv::Mat &R_rect_xx, cv::Mat &RT)
{
    // loop over all Lidar points and associate them to a 2D bounding box
    cv::Mat X(4, 1, cv::DataType<double>::type);
    cv::Mat Y(3, 1, cv::DataType<double>::type);

    for (auto it1 = lidarPoints.begin(); it1 != lidarPoints.end(); ++it1)
    {
        // assemble vector for matrix-vector-multiplication
        X.at<double>(0, 0) = it1->x;
        X.at<double>(1, 0) = it1->y;
        X.at<double>(2, 0) = it1->z;
        X.at<double>(3, 0) = 1;

        // project Lidar point into camera
        Y = P_rect_xx * R_rect_xx * RT * X;
        cv::Point pt;
        pt.x = Y.at<double>(0, 0) / Y.at<double>(0, 2); // pixel coordinates
        pt.y = Y.at<double>(1, 0) / Y.at<double>(0, 2);

        vector<vector<BoundingBox>::iterator> enclosingBoxes; // pointers to all bounding boxes which enclose the current Lidar point
        for (vector<BoundingBox>::iterator it2 = boundingBoxes.begin(); it2 != boundingBoxes.end(); ++it2)
        {
            // shrink current bounding box slightly to avoid having too many outlier points around the edges
            cv::Rect smallerBox;
            smallerBox.x = (*it2).roi.x + shrinkFactor * (*it2).roi.width / 2.0;
            smallerBox.y = (*it2).roi.y + shrinkFactor * (*it2).roi.height / 2.0;
            smallerBox.width = (*it2).roi.width * (1 - shrinkFactor);
            smallerBox.height = (*it2).roi.height * (1 - shrinkFactor);

            // check wether point is within current bounding box
            if (smallerBox.contains(pt))
            {
                enclosingBoxes.push_back(it2);
            }

        } // eof loop over all bounding boxes

        // check wether point has been enclosed by one or by multiple boxes
        if (enclosingBoxes.size() == 1)
        { 
            // add Lidar point to bounding box
            enclosingBoxes[0]->lidarPoints.push_back(*it1);
        }

    } // eof loop over all Lidar points
}


void show3DObjects(std::vector<BoundingBox> &boundingBoxes, cv::Size worldSize, cv::Size imageSize, bool bWait)
{
    // create topview image
    cv::Mat topviewImg(imageSize, CV_8UC3, cv::Scalar(255, 255, 255));

    for(auto it1=boundingBoxes.begin(); it1!=boundingBoxes.end(); ++it1)
    {
        // create randomized color for current 3D object
        cv::RNG rng(it1->boxID);
        cv::Scalar currColor = cv::Scalar(rng.uniform(0,150), rng.uniform(0, 150), rng.uniform(0, 150));

        // plot Lidar points into top view image
        int top=1e8, left=1e8, bottom=0.0, right=0.0; 
        float xwmin=1e8, ywmin=1e8, ywmax=-1e8;
        for (auto it2 = it1->lidarPoints.begin(); it2 != it1->lidarPoints.end(); ++it2)
        {
            // world coordinates
            float xw = (*it2).x; // world position in m with x facing forward from sensor
            float yw = (*it2).y; // world position in m with y facing left from sensor
            xwmin = xwmin<xw ? xwmin : xw;
            ywmin = ywmin<yw ? ywmin : yw;
            ywmax = ywmax>yw ? ywmax : yw;

            // top-view coordinates
            int y = (-xw * imageSize.height / worldSize.height) + imageSize.height;
            int x = (-yw * imageSize.width / worldSize.width) + imageSize.width / 2;

            // find enclosing rectangle
            top = top<y ? top : y;
            left = left<x ? left : x;
            bottom = bottom>y ? bottom : y;
            right = right>x ? right : x;

            // draw individual point
            cv::circle(topviewImg, cv::Point(x, y), 4, currColor, -1);
        }

        // draw enclosing rectangle
        cv::rectangle(topviewImg, cv::Point(left, top), cv::Point(right, bottom),cv::Scalar(0,0,0), 2);

        // augment object with some key data
        char str1[200], str2[200];
        sprintf(str1, "id=%d, #pts=%d", it1->boxID, (int)it1->lidarPoints.size());
        putText(topviewImg, str1, cv::Point2f(left-250, bottom+50), cv::FONT_ITALIC, 2, currColor);
        sprintf(str2, "xmin=%2.2f m, yw=%2.2f m", xwmin, ywmax-ywmin);
        putText(topviewImg, str2, cv::Point2f(left-250, bottom+125), cv::FONT_ITALIC, 2, currColor);  
    }

    // plot distance markers
    float lineSpacing = 2.0; // gap between distance markers
    int nMarkers = floor(worldSize.height / lineSpacing);
    for (size_t i = 0; i < nMarkers; ++i)
    {
        int y = (-(i * lineSpacing) * imageSize.height / worldSize.height) + imageSize.height;
        cv::line(topviewImg, cv::Point(0, y), cv::Point(imageSize.width, y), cv::Scalar(255, 0, 0));
    }

    // display image
    string windowName = "3D Objects";
    cv::namedWindow(windowName, 1);
    cv::imshow(windowName, topviewImg);

    if(bWait)
    {
        cv::waitKey(0); // wait for key to be pressed
    }
}


// associate a given bounding box with the keypoints it contains
void clusterKptMatchesWithROI(BoundingBox &boundingBox, std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, std::vector<cv::DMatch> &kptMatches)
{
    vector<cv::DMatch> belong;
    cv::KeyPoint kpcurr;
  vector<double> distance;
  for(auto it=kptMatches.begin();it!=kptMatches.end();it++){
   if(boundingBox.roi.contains(kptsCurr[(*it).queryIdx].pt)){
    belong.push_back(*it); 
     distance.push_back(cv::norm(kptsCurr[(*it).queryIdx].pt-kptsPrev[(*it).trainIdx].pt));
   }
      
  }
      auto mean = std::accumulate(distance.begin(),distance.end(),0.0)/distance.size();
    double threshold=1.5*mean;
   auto it1=belong.begin();
    for(auto it2=distance.begin();it2!=distance.end();it1++,it2++){
     if((*it2)<threshold){
       boundingBox.kptMatches.push_back((*it1));
       boundingBox.keypoints.push_back(kptsCurr[(*it1).queryIdx]);
     }
    }
      
    
    
}


// Compute time-to-collision (TTC) based on keypoint correspondences in successive images
void computeTTCCamera(std::vector<cv::KeyPoint> &kptsPrev, std::vector<cv::KeyPoint> &kptsCurr, 
                      std::vector<cv::DMatch> kptMatches, double frameRate, double &TTC, cv::Mat *visImg)
{
   vector<double>distanceRatios;
  for(auto it=kptMatches.begin();it!=kptMatches.end()-1;it++){
   cv::KeyPoint kpOuterCurr = kptsCurr.at(it->trainIdx);
        cv::KeyPoint  kpOuterPrev=kptsPrev.at(it->queryIdx);
    for(auto it1=kptMatches.begin()+1;it1!=kptMatches.end();it1++){
      double minDist=90.0;
      cv::KeyPoint kpInnerCurr = kptsCurr.at(it1->trainIdx);
        cv::KeyPoint  kpInnerPrev=kptsPrev.at(it1->queryIdx);
      double distCurr=cv::norm(kpOuterCurr.pt - kpInnerCurr.pt);
      double distPrev=cv::norm(kpOuterPrev.pt - kpInnerPrev.pt);
      if (distPrev > std::numeric_limits<double>::epsilon() && distCurr >= minDist){
        double distanceRatio=distCurr/distPrev;
        distanceRatios.push_back(distanceRatio);
        
      }
      
    }
  }
  if (distanceRatios.size() == 0)
    {
        TTC = NAN;
        return;
    }
  std::sort(distanceRatios.begin(), distanceRatios.end());
    long medIndex = floor(distanceRatios.size() / 2.0);
    double medDistRatio = distanceRatios.size() % 2 == 0 ? (distanceRatios[medIndex - 1] + distanceRatios[medIndex]) / 2.0 : distanceRatios[medIndex];
  
  double dT=1/frameRate;
  TTC = -dT / (1 - medDistRatio);
  
  
}


void computeTTCLidar(std::vector<LidarPoint> &lidarPointsPrev,
                     std::vector<LidarPoint> &lidarPointsCurr, double frameRate, double &TTC)
{
    double dT=1/frameRate;
  double d0,d1;
 double prevX;
  double currX;
  for(auto it=lidarPointsPrev.begin();it!=lidarPointsPrev.end();it++){
    prevX+=(*it).x;
  }
  for(auto it=lidarPointsCurr.begin();it!=lidarPointsCurr.end();it++){
    currX+=(*it).x;
  }
  d0=prevX/lidarPointsPrev.size();
  d1=currX/lidarPointsCurr.size();
  TTC=(d1*dT)/(d0-d1);
  
}


void matchBoundingBoxes(std::vector<cv::DMatch> &matches, std::map<int, int> &bbBestMatches, DataFrame &prevFrame, DataFrame &currFrame)
{ int p=prevFrame.boundingBoxes.size();
 int c=currFrame.boundingBoxes.size();
 int count[p][c]={};
 cv::KeyPoint prevPoint,currPoint;
 vector<int> prevBoxesIds, currBoxesIds;
 for(auto i=matches.begin();i!=matches.end();i++){
  
       
    prevPoint=prevFrame.keypoints[(*i).queryIdx];
    currPoint=currFrame.keypoints[(*i).trainIdx];
   prevBoxesIds.clear();
   currBoxesIds.clear();
   for(auto bb=prevFrame.boundingBoxes.begin();bb!=prevFrame.boundingBoxes.end();bb++){
     if((*bb).roi.contains(prevPoint.pt)){
       prevBoxesIds.push_back((*bb).boxID);
     }
   }
   for(auto bb1=currFrame.boundingBoxes.begin();bb1!=currFrame.boundingBoxes.end();bb1++){
   if((*bb1).roi.contains(currPoint.pt)){
       currBoxesIds.push_back((*bb1).boxID);
     }
   }
   for(auto pId:prevBoxesIds){
     for(auto cId:currBoxesIds){
    count[pId][cId]++;   
     }
   }
   
   
 }
 int maxcnt=0,maxId;
 for(int i=0;i<p;i++){
   maxcnt=0;
  for(int j=0;j<c;j++){
    
    if(count[i][j]>maxcnt){
     maxcnt=count[i][j]; 
      maxId=j;
    }
  }
    bbBestMatches[i] = maxId;
  }
   bool bMsg = true;
    if (bMsg)
        for (int z = 0; z < p; z++)
             cout << "Box " << z << " matches " << bbBestMatches[z]<< " box" << endl;
 }
 
   

