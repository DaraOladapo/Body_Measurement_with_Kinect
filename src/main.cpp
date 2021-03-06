/****************************************************************************
* 
*  Computer Vision, Fall 2011
*  New York University
*
*  Created by Otavio Braga (obraga@cs.nyu.edu)
*
****************************************************************************/

#include <iostream>
#include <stdlib.h>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/glut.h>
#endif

#include <opencv2/core/core.hpp>

#include "CrossSections.h"
#include "FitEllipse.h"
#include "kmeans_color.h"
#include "gmm_color.h"
#include "gmm_segmentation.h"
#include "histogram.h"
#include "KinectInterface.h"
#include "kmeans_segmentation.h"
#include "threshold.h"
#include "mincut_segmentation.h"
#include "PlanePointCloudIntersect.h"
#include "AngularSkeleton.h"
#include "Skeleton.h"

#ifndef M_PI
#define M_PI 3.1415926535
#endif // M_PI

using namespace std;

// Glut menu ids
enum
{
    MENU_ID_MANUAL_THRESHOLDING,
    MENU_ID_KMEANS_THRESHOLDING,
    MENU_ID_GMM_THRESHOLDING,
    
    MENU_ID_SEGMENTED_IMAGE,
    MENU_ID_COLOR_CODED,
    MENU_ID_FULL_IMAGE,
    MENU_ID_COLOR_CLUSTERS,

    MENU_ID_SEGMENTATION_THRESHOLD,
    MENU_ID_SEGMENTATION_MINCUT
};

enum
{
    THRESHOLD_MANUAL,
    THRESHOLD_KMEANS,
    THRESHOLD_GMM
};

enum
{
    SEGMENTATION_THRESHOLD,
    SEGMENTATION_MINCUT
};

int threshold_method = THRESHOLD_KMEANS;
int segmentation_method = SEGMENTATION_THRESHOLD;

// Glut window ids
int mainWindow, histogramWindow;

// The current window size
int windowWidth = 640, windowHeight = 480;

KinectInterface *kinectIface;

// Depth image size
int imageWidth = 480;
int imageHeight = 640;

// The total number of pixels
int npts;

unsigned short *depthImage;     // we keep our own copy of the depth image
const XnUInt8 *rgbImage;        // this is just a pointer to OpenNI's rgb image

// The points of the depth image linearized after the segmentation
std::vector<cv::Vec3d> point_cloud;

// The user sleketon
std::vector<cv::Vec3d> joints;
std::vector<cv::Vec2d> joints_projected;

std::vector<cv::Vec2d> cross_sections[NUM_CS];

#define NUM_CS_ORIENTATIONS 4
bool cs_orientation_filled[NUM_CS_ORIENTATIONS];
   
// The planes defining each body cross section
cv::Vec3d N[NUM_CS], O[NUM_CS], X[NUM_CS], Y[NUM_CS];

int posecount = 0;

struct ellipse
{
    double sx, sy;
    cv::Vec2d center;
    double theta;
};

// The best fitting ellipse of each section
ellipse ellipses[NUM_CS];

// foreground/background segmentation
bool *foreground;
unsigned char *trimap;
float *prob_foreground;

unsigned char *segmentedImage;
unsigned char *segmentationCmap;        // the segmentation color map
unsigned char *clusterCmap;

histogram *hist;

// The color space GMMs
std::vector<cv::Vec3d> mean[2];
std::vector<cv::Matx33d> cov[2];
std::vector<double> pi[2];
std::vector<cv::Matx33d> inv_cov[2];
std::vector<double> det_cov[2];
int n_color_clusters = 4;

unsigned char *cluster;

float threshold = 3000;
int user_gamma = 50;
int user_filter = 1;
int user_epsilon = 5;

// For k-means segmentation, mu1 and mu2 are the cluster centroids.
// For gaussian mixture, mu and sigma are the gaussian distribution mean
// and standard deviation. p is the mixing coefficient.
double mu1, sigma1, mu2, sigma2, p;

enum
{
    TEXTURE_ID_SEGMENTED_IMAGE,
    TEXTURE_ID_COLOR_CODED_IMAGE,
    TEXTURE_ID_FULL_IMAGE,
    TEXTURE_ID_COLOR_CLUSTERS,
};

// Indicates the image we are currently displaying on the left
// pane (either the segmented image, the color coded segmentation, or
// the original rgb image).
int display_image = 0;

// The texture ids for the 4 possible images we display on the left pane
GLuint texture[4];

// Glut registered callbacks
void display();
void keyboard(unsigned char key, int x, int y);
void reshape(const int width, const int height);
void motion(const int x, const int y);
void mouse(int button, int state, int x, int y);
void menuSelected(int id);
void selectDisplay(int id);

void DrawKinectData();
void updateKinectData();

void initGL(void)
{
    glClearColor(0.0, 0.0, 0.0, 1.0);

    glDisable(GL_LIGHTING);
    //glEnable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glPointSize(2.0);
    
    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void initTextures()
{ 
    glEnable(GL_TEXTURE_2D);
    glGenTextures(4, texture);
    
    // Set the texture parameters
    for (int i = 0; i < 4; i++)
    {
        glBindTexture(GL_TEXTURE_2D, texture[i]); 
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}

void initKinect()
{
    kinectIface = new KinectInterface();

    xn::DepthGenerator &depthGenerator = kinectIface->getDepthGenerator();
    xn::DepthMetaData depthMD;
    depthGenerator.GetMetaData(depthMD);
    
    imageWidth = depthMD.XRes();
    imageHeight = depthMD.YRes();
}

void initArrays()
{
    npts = imageWidth*imageHeight;

    depthImage = new unsigned short[npts];
    foreground = new bool[npts];
    prob_foreground = new float[2*npts];
    segmentationCmap = new unsigned char[3*npts];
    segmentedImage = new unsigned char[3*npts];
    
    clusterCmap = new unsigned char[3*npts];
    cluster = new unsigned char[npts];
    trimap = new unsigned char[npts];

    for (int i = 0; i < NUM_CS_ORIENTATIONS; i++)
        cs_orientation_filled[i] = false;
}

void updateSegmentation()
{
    if (threshold_method == THRESHOLD_KMEANS)
    {
        threshold = k_means_segmentation(depthImage,
                        npts, foreground, &mu1, &mu2);
    }
    else if (threshold_method == THRESHOLD_GMM)
    {
		threshold = gaussian_mixture_segmentation(depthImage, npts,
                        prob_foreground, foreground,
                        &mu1, &sigma1, &mu2, &sigma2, &p);
    }
    
   threshold_depth_map(depthImage, npts, threshold, foreground);

   // Build the trimap
   for (int i = 0; i < npts; i++)
       if (depthImage[i] == 0)
           trimap[i] = TRIMAP_U;
       else
           trimap[i] = foreground[i];
   
   // Cluster the foreground and background pixels in color space     
   for (int a = 0; a < 2; a++)
   {
       // Run k-means to initialize the gaussian mixture estimation
       k_means_color((unsigned char *)rgbImage, npts,
               n_color_clusters, mean[a], cluster, trimap, a);

       if (mean[a].size() != cov[a].size())
       {
           cov[a].resize(n_color_clusters);
           inv_cov[a].resize(n_color_clusters);
           det_cov[a].resize(n_color_clusters);
           pi[a].resize(n_color_clusters);
       }

       // Estimate the GMM
       gmm_color((unsigned char *)rgbImage, npts,
               mean[a], cov[a], pi[a], inv_cov[a], det_cov[a],
               cluster, trimap, a);
   }

   // Assign each pixel to a component of the gaussian mixture
   assign_gmm_component((unsigned char *)rgbImage, npts, foreground, 	 				mean, cov, pi, inv_cov, det_cov, cluster);

    // Refine the segmentation by thresholding with mincut
    if (segmentation_method == SEGMENTATION_MINCUT)
    {
        mincut_segmentation((unsigned char *)rgbImage, imageWidth, imageHeight,
                            trimap, foreground, cluster, n_color_clusters,
                            mean, cov, pi, inv_cov, det_cov, user_gamma, user_filter);

        // Update the trimap using the mincut result, since there are no more
        // pixels with undefined depth
        for (int i = 0; i < npts; i++)
            trimap[i] = foreground[i];
    }

    // Create the segmented image by painting in black the pixels with
    // either an invalid depth or in the background
    const XnUInt8 *pImage = rgbImage;
    unsigned char *pSegmented = segmentedImage;
    unsigned char *pTrimap = trimap;
    for (int i = 0; i < npts; i++, pImage+=3, pSegmented+=3, pTrimap++)
    {
        if (*pTrimap == 1)
        {
            pSegmented[0] = pImage[0];
            pSegmented[1] = pImage[1];
            pSegmented[2] = pImage[2];
        }
        else
        {
            pSegmented[0] = 0;
            pSegmented[1] = 0;
            pSegmented[2] = 0;
        }
    }

    // Color code the segmentation
    unsigned char colors[3][3] = {{0, 0, 255},
                                  {255, 0, 0},
                                  {255, 255, 0}};
    unsigned char *pSegCmap = segmentationCmap;
    for (int i = 0; i < npts; i++, pSegCmap += 3)
    {
        for (int j = 0; j < 3; j++)
            pSegCmap[j] = colors[trimap[i]][j];

        // Make the color darker if it came from a depth = 0 region
        if (depthImage[i] == 0)
            for (int j = 0; j < 3; j++)
                pSegCmap[j] *= 3.0/4;
    }
    
    // Create the color clusters image by coloring each cluster with the
    // color of the cluster centroid
    pImage = rgbImage;
    unsigned char *pClusterCmap = clusterCmap;
    for (int i = 0; i < npts; i++, pImage+=3, pClusterCmap+=3)
    {
        // Compute the color of each pixel as the weighted average of
        // cluster centroids, weighted by the probability that it belongs
        // to each cluster
        cv::Vec3d rgb;
        if (trimap[i] == TRIMAP_U)
            rgb = cv::Vec3d(255,255,0);
        else if (mean[0].size() == 0)// || foreground[i] == 0)
            rgb = cv::Vec3d(0,0,0);
        else
            rgb = mean[foreground[i]][cluster[i]];

        pClusterCmap[0] = (unsigned char)rgb(0);
        pClusterCmap[1] = (unsigned char)rgb(1);
        pClusterCmap[2] = (unsigned char)rgb(2);
    }

    // Collect the points selected by the segmentation
    unsigned short *pDepth = depthImage;
    pTrimap = trimap;
    point_cloud.clear();
    xn::DepthGenerator &depthGenerator = kinectIface->getDepthGenerator();
    for (int y = 0; y < imageHeight; y++)
    {
        for (int x = 0; x < imageWidth; x++, pDepth++, pTrimap++)
        {
            if (*pTrimap == TRIMAP_FG && *pDepth)
            {
                XnPoint3D P;
                P.X = x;
                P.Y = y;
                P.Z = *pDepth;
                depthGenerator.ConvertProjectiveToRealWorld(1, &P, &P);
                point_cloud.push_back(cv::Vec3d(P.X, P.Y, P.Z));
            }
        }
    }

    // Upload the new segmented image textures
    glBindTexture(GL_TEXTURE_2D, texture[TEXTURE_ID_COLOR_CODED_IMAGE]);
    glTexImage2D(GL_TEXTURE_2D,  0, GL_RGB, 640, 480, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, segmentationCmap);
    
    glBindTexture(GL_TEXTURE_2D, texture[TEXTURE_ID_SEGMENTED_IMAGE]);
    glTexImage2D(GL_TEXTURE_2D,  0, GL_RGB, 640, 480, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, segmentedImage);
    
    glBindTexture(GL_TEXTURE_2D, texture[TEXTURE_ID_COLOR_CLUSTERS]);
    glTexImage2D(GL_TEXTURE_2D,  0, GL_RGB, 640, 480, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, clusterCmap);
}

bool too_far(cv::Vec2d v)
{
    return v.dot(v) > 500*500;
}

void RotationToEuler(cv::Matx33d &R, double *yaw, double *pitch, double *roll)
{
    *yaw = atan2(R(1,0), R(0,0));
    *pitch = atan2(-R(2,0), sqrt(R(2,1)*R(2,1) + R(2,2)*R(2,2)));
    *roll = atan2(R(2,1), R(2,2));
}

void computeSkeletonAngularRepresentation()
{
   if (joints.size() == 0)
        return;

   // Transform the 19 3D joint positions into 16 angles
   std::vector<double> angles;
   cv::Vec3d axis[3];
   computeSkeletonAngularRepresentation(joints, angles, axis);
   
   // Print the skeleton angular representatioin for debugging
   //printf("Skeleton Angular Representation:\n");
   //printf("center = %.2lf %.2lf %.2lf\n", center[0], center[1], center[2]);
   //printf("axis[0] = %.2lf %.2lf %.2lf\n", axis[0][0], axis[0][1], axis[0][2]);
   //printf("axis[1] = %.2lf %.2lf %.2lf\n", axis[1][0], axis[1][1], axis[1][2]);
   //printf("axis[2] = %.2lf %.2lf %.2lf\n", axis[2][0], axis[2][1], axis[2][2]);
   //for (unsigned int i = 0; i < angles.size(); i++)
   //     printf("%lf\n", angles[i]);
   //printf("---------------------\n");

   PoseID poses[2] = {POSE_HOSTAGE_FORWARD, POSE_HOSTAGE_BACKWARDS};
   const char *poses_names[6] = {"Hostage Forward", "Hostage Backwards"};
   int poses_bin[6] = {0, 2};

   // Checks if a pose matches a reference pose
   bool match = false;
   int bin = 0;
   for (int i = 0; i < 2; i++)
   {
       if (matchAngularPose(angles, poses[i]))
       {
            posecount++;
            printf("%s\n", poses_names[i]);
            bin = poses_bin[i];
            match = true;
            break;
       }
   }
   if (!match)
   {
        posecount = 0;
        return;
   }

   if (posecount < 10 || cs_orientation_filled[bin])
        return;
   
   cs_orientation_filled[bin] = true;

   // Compute the planes where we will "cut" the body for measurement
   ComputeCrossSections(joints, axis, O, N, X, Y);
    
   // Intersect each plane with the point cloud
   for (int i = 0; i < NUM_CS; i++)
   {
        std::vector<cv::Vec2d> new_points;
        PlanePointCloudIntersect(point_cloud, O[i], N[i], X[i], Y[i], new_points, user_epsilon);

        cross_sections[i].insert(cross_sections[i].end(), new_points.begin(), new_points.end());
   }

   // Fit ellipses to the cross sections
   for (int i = 0; i < NUM_CS; i++)
   {
        FitEllipse(cross_sections[i], &ellipses[i].sx, &ellipses[i].sy, &ellipses[i].theta, &ellipses[i].center);
        //printf("ellipse[%d]:\n", i);
        //printf("sx[%d] = %lf\n", i, ellipses[i].sx);
        //printf("sy[%d] = %lf\n", i, ellipses[i].sy);
        //printf("theta[%d] = %lf\n", i, ellipses[i].theta);
        //printf("center[%d] = %lf %lf\n", i, ellipses[i].center[0], ellipses[i].center[1]);
    }
}

void display()
{
    if (!kinectIface)
        return;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glViewport(0, 0, windowWidth/2, windowHeight);

    DrawKinectData();

    glutSwapBuffers();
}

void RenderStrokeFontString(int x, int y, void *font,
                            const unsigned char *string, double scale)
{
    double H = glutStrokeWidth(font, ' ');
    double W = glutStrokeLength(font, string);
    
    glPushMatrix();
    glTranslated(x, y, 0);
    glScaled(scale, scale, scale);
    for (const unsigned char *c = string; *c; c++)
        glutStrokeCharacter(font, *c);
    glPopMatrix();
}

double length(ellipse &e)
{        
    double l = 0;
    double theta = 0;
    cv::Vec2d P1(e.sx*sin(theta), e.sy*cos(theta));
    int n = 100;
    double dtheta = 2*M_PI/n;
    for (int i = 0; i < n; i++, theta += dtheta)
    {
        cv::Vec2d P2(e.sx*sin(theta), e.sy*cos(theta));
        l += cv::norm(P2 - P1);
        P1 = P2;
    }
    return l;
}

void display_cross_sections()
{
    if (!hist)
        return;
            
    glViewport(0, 0, windowWidth/2, windowHeight);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    const char *parts_names[NUM_CS] = {"chest", "hips", "waist", "left biceps", "right biceps",
                                       "left thigh", "right thigh"};

    int w = windowWidth/2;
    int h = windowHeight/2;

    double viewport[NUM_CS][4] = {{    0, 0, w/3, h},
                                  {  w/3, 0, w/3, h},
                                  {2*w/3, 0, w/3, h},
                                  {    0, h, w/4, h},
                                  {  w/4, h, w/4, h},
                                  {2*w/4, h, w/4, h},
                                  {3*w/4, h, w/4, h}}; 

    for (int cs = 0; cs < NUM_CS; cs++)
    {
        glViewport(viewport[cs][0], viewport[cs][1], viewport[cs][2], viewport[cs][3]);

        double ar = viewport[cs][2]/(double)viewport[cs][3];
        double H = 300;
        double W = H*ar;
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluOrtho2D(-W, W, -H, H);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        // Draw a rectangle around the cross section frame
        glColor3f(1.0, 1.0, 1.0);
        glPushMatrix();
        glScalef(W, H, 1.0);
        glBegin(GL_LINE_LOOP);
        glVertex2d(-1, -1);
        glVertex2d(-1, +1);
        glVertex2d(+1, +1);
        glVertex2d(+1, -1);
        glEnd();
        glPopMatrix();

        // Write the cross section name
        //glColor3f(1.0, 1.0, 0.0);
        RenderStrokeFontString(-W*5.0/6, -H*5.0/6, GLUT_STROKE_ROMAN, (const unsigned char *)parts_names[cs], 0.3);

        // Draw the points from the cross section
        glPointSize(0.5);
        glColor3f(1, 0, 0);
        glBegin(GL_POINTS);
        for (unsigned int i = 0; i < cross_sections[cs].size(); i++)
            glVertex2d(cross_sections[cs][i][0], cross_sections[cs][i][1]);
        glEnd();

        // Draw the ellipse fitting the points of the cross section 
        double theta = 0;
        glPushMatrix();
        glTranslatef(ellipses[cs].center[0], ellipses[cs].center[1], 0);
        glRotatef(ellipses[cs].theta, 0, 0, 1); 
        glScalef(ellipses[cs].sx, ellipses[cs].sy, 1.0);
        int n = 100;
        double dtheta = 2*M_PI/n;
        glLineWidth(1.0);
        glColor3f(1,1,0);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < n; i++, theta += dtheta)
            glVertex2d(sin(theta), cos(theta));
        glEnd();
        glPopMatrix();

        // Print the length of the ellipse
        char s[512];
        //sprintf(s, "d = %.0lf cm\n", .2*r);
        //sprintf(s, "dx = %.0lf cm, dy = %0.lf cm\n", .2*sx, .2*sy);
        sprintf(s, "length = %.0lf cm\n", .1*length(ellipses[cs]));
        glColor3f(1.0, 1.0, 1.0);
        RenderStrokeFontString(-W*5.0/6, H*5.0/6, GLUT_STROKE_ROMAN, (const unsigned char *)s, .3);
    }

    glutSwapBuffers();
}

void DrawKinectData()
{
    glEnable(GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, 1, 0, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // Select the texture based on the image mode we are currently on
    glBindTexture(GL_TEXTURE_2D, texture[display_image]);
 
    glColor3f(1,1,1);
    glBegin(GL_QUADS);
    glTexCoord2d(0.0,1.0); glVertex2d(0.0,0.0);
    glTexCoord2d(1.0,1.0); glVertex2d(1.0,0.0);
    glTexCoord2d(1.0,0.0); glVertex2d(1.0,1.0);
    glTexCoord2d(0.0,0.0); glVertex2d(0.0,1.0);
    glEnd();

    // Draw the projected joints
    glDisable(GL_TEXTURE_2D);
    glPointSize(10);

    glPushMatrix();
    glScalef(1.0/imageWidth, 1.0/imageHeight, 1.0);
    glColor3f(1.0, 0.0, 0.0);
    glBegin(GL_POINTS);
    for (unsigned int i = 0; i < joints_projected.size(); i++)
        glVertex2d(joints_projected[i][0], joints_projected[i][1]);
    glEnd();
    glPopMatrix();

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

void reshape(int width, int height)
{
    windowWidth = width;
    windowHeight = height;

    glutSetWindow(histogramWindow);
    glutPositionWindow(windowWidth/2, 0);
    glutReshapeWindow(windowWidth/2, windowHeight);
    glutPostRedisplay();
    
    glutSetWindow(mainWindow);
}

void mouse(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON)
    {
    }
    else if (button == GLUT_RIGHT_BUTTON)
    {
    }
}

void click(int x, int y)
{
}

void keyboard(unsigned char key, int x, int y)
{
	int matrix;
    switch (key)
    {
        case 27: // ESC
        case 'q':
        case 'Q':
            exit(0);
        case '+':
            if (threshold_method == THRESHOLD_MANUAL)
                threshold += hist->get_bin_size(); // mm
            break;
        case '-':
            if (threshold_method == THRESHOLD_MANUAL)
                threshold -= hist->get_bin_size(); // mm
            break;
		case 'G':
			user_gamma++;
			cout << "gamma : " << user_gamma << endl;
			break;
		case 'g':
			user_gamma--;
			cout << "gamma : " << user_gamma << endl;
			break;
		case 'F' :
			user_filter += 2;
			matrix = user_filter * 2 + 1;
			cout << "filter : " << matrix << "x" << matrix << endl;
			break;
		case 'f' :
			if(user_filter>=3)
				user_filter -= 2;
			matrix = user_filter * 2 + 1;
			cout << "filter : " << matrix << "x" << matrix << endl;
			break;
		case 'E' :
			user_epsilon++;
			cout << "epsilon : " << user_epsilon << endl;
			break;
		case 'e' :
			if(user_epsilon>0)
				user_epsilon--;
			cout << "epsilon : " << user_epsilon << endl;
			break;
    }
}

void menuSelected(int id)
{
    switch (id)
    {
        case MENU_ID_MANUAL_THRESHOLDING:
            threshold_method = THRESHOLD_MANUAL;
            break;
        case MENU_ID_KMEANS_THRESHOLDING:
            threshold_method = THRESHOLD_KMEANS;
            break;
        case MENU_ID_GMM_THRESHOLDING:
            threshold_method = THRESHOLD_GMM;
            break;
    }
}

void selectDisplay(int id)
{
   display_image = id - MENU_ID_SEGMENTED_IMAGE;
} 

void selectSegmentation(int id)
{
    segmentation_method = id - MENU_ID_SEGMENTATION_THRESHOLD;
}

void updateKinectData()
{
    kinectIface->updateKinectData();

    xn::DepthGenerator &depthGenerator = kinectIface->getDepthGenerator();
    xn::ImageGenerator &imageGenerator = kinectIface->getImageGenerator();

    joints = kinectIface->getJoints();
    joints_projected = kinectIface->getProjectedJoints();
    for (unsigned int i = 0; i < joints_projected.size(); i++)
        joints_projected[i][1] = imageHeight - joints_projected[i][1];

    xn::DepthMetaData depthMD;
    xn::ImageMetaData imageMD;

    depthGenerator.GetMetaData(depthMD);
    imageGenerator.GetMetaData(imageMD);

    // Get the RGB image 
    rgbImage = imageMD.Data();

    // Copy the depth map form OpenNI to our own image
    unsigned short *pDepthImage = depthImage;
    const XnDepthPixel *pDepth = depthMD.Data();
    for (XnUInt y = 0; y < depthMD.YRes(); ++y)
    {
        for (XnUInt x = 0; x < depthMD.XRes(); ++x, ++pDepth, ++pDepthImage)
            *pDepthImage = *pDepth;
    }

    compute_histogram(depthImage, npts, *hist, true);

    // Upload the new rgb image texture
    glBindTexture(GL_TEXTURE_2D, texture[TEXTURE_ID_FULL_IMAGE]);
    glTexImage2D(GL_TEXTURE_2D,  0, GL_RGB, 640, 480, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, rgbImage);
}

void idle()
{
    if (kinectIface)
    {
        updateKinectData();
        updateSegmentation();
        computeSkeletonAngularRepresentation();
        
        glutSetWindow(histogramWindow);
        glutPostRedisplay();

        glutSetWindow(mainWindow);
        glutPostRedisplay();
    }
}

int main(int argc, char *argv[])
{
    glutInit(&argc, argv);
    glutInitWindowSize(2*windowWidth, windowHeight);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
    mainWindow = glutCreateWindow("Computer Vision - Fall 2011 - Assignment 3 -  Skeleton and Body Measurement");
    glutDisplayFunc(display);  // How draw in display
    glutReshapeFunc(reshape);	// when resize the window, what do?
    glutMouseFunc(mouse);		// How react about mouse input
    glutMotionFunc(click);		// When mouse button click & move, what do?
    glutKeyboardFunc(keyboard);	// How react about keyboard input
    glutIdleFunc(idle);			// what do in idle state
    
    // Create a histogram between 0 and 5 meters, with 100 bins
    hist = new histogram(0, 5000, 100);
    
    initGL();
    initTextures();
    initKinect();
    initArrays();
    updateKinectData();
    
    glutCreateMenu(selectDisplay);
    glutAddMenuEntry("Segmented Image", MENU_ID_SEGMENTED_IMAGE);
    glutAddMenuEntry("Color Coded Segmentation", MENU_ID_COLOR_CODED);
    glutAddMenuEntry("Full Image", MENU_ID_FULL_IMAGE);
    glutAddMenuEntry("Color Clusters", MENU_ID_COLOR_CLUSTERS);
    glutAttachMenu(GLUT_RIGHT_BUTTON);

    histogramWindow = glutCreateSubWindow(mainWindow, windowWidth, 0,
                        windowWidth, windowHeight);
    glutDisplayFunc(display_cross_sections);// How draw in display
    glutKeyboardFunc(keyboard);	// How react about keyboard input
        

    int submenu = glutCreateMenu(selectSegmentation);
    glutAddMenuEntry("Segment by Thresholding",
                     MENU_ID_SEGMENTATION_THRESHOLD);
    glutAddMenuEntry("Min-Cut Segmentation",
                     MENU_ID_SEGMENTATION_MINCUT);
    glutCreateMenu(selectSegmentation);

    glutCreateMenu(menuSelected);
    glutAddMenuEntry("Manual Thresholding", MENU_ID_MANUAL_THRESHOLDING);
    glutAddMenuEntry("K-Means Thresholding", MENU_ID_KMEANS_THRESHOLDING);
    glutAddMenuEntry("Gaussian Mixture Thresholding", MENU_ID_GMM_THRESHOLDING);
    glutAddSubMenu("Segmentation Method", submenu);
    glutAttachMenu(GLUT_RIGHT_BUTTON);

    initGL();

    glutMainLoop();

    return 0;
}
