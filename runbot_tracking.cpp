/***************************************************************************
 *   Copyright (C) 2013 by Graeme Hattan                                   *
 *   graemeh.dev@googlemail.com                                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include <cassert>
#include <iostream>
#include <iomanip>
#include <algorithm>

#include <opencv2/opencv.hpp>

#include <dirent.h>
#include <errno.h>

#include "matfiledump.h"

using namespace std;
using namespace cv;


/**--------------------------- Tunable Parameters ----------------------------*/

//#define UNDISTORT_LENS
#define INPUT_IS_FIELDS   /* define if input images are separated video fields rather than full frames */ 
//#define WRITE_MAT_FILE
//#define WRITE_IMAGES    /* good for creating a video of the result */

static const int num_track_regions = 4;

static const int start_file = 0;

static const int video_wait = 20;

// opencv sub-pixel rendering uses fixed point arithmetic, this is the shift used
static const int shift = 10;
static const int shift_mult = 1<<shift;

// not exactly a parameter, but needs to be tuned anyway
inline bool is_tracking_spot(uchar * const &pixel)
{
  // loaded image format is BGR, so pixel[0] is blue, pixel[1] is green and pixel[2] is red
  return pixel[1] < 210 && pixel[2] > (int)pixel[0]-5 && pixel[2] > (int)pixel[1]+10;
}


/**---------------------------------------------------------------------------*/


// Comparison function for sorting points by their y-value
bool highest_point( Point2d p1, Point2d p2 ) { return p1.y < p2.y; }



/** Class to load the images and correct the lens distortion */
class ImageLoader
{
  private:
    string &dir;

    Mat image_orig;

#ifdef UNDISTORT_LENS
    Mat image_undist;
    Mat map1, map2;
#endif

  public:
    ImageLoader(string &dir) :
      dir(dir)
    {
#ifdef UNDISTORT_LENS
      FileStorage calib("calib.xml", FileStorage::READ);

      if( !calib.isOpened() )
      {
        cerr << "Error: Failed opening calib.xml" << endl;
        exit(-1);
      }

      Mat cam_matrix, dist_coeff;
      calib["Camera_Matrix"]           >> cam_matrix;
      calib["Distortion_Coefficients"] >> dist_coeff;

      initUndistortRectifyMap(
          cam_matrix,
          dist_coeff,
          Mat(),
          getOptimalNewCameraMatrix(cam_matrix, dist_coeff, Point(512, 384), 1, Point(512, 384), 0),
          Point(512, 384),
          CV_16SC2,
          map1,
          map2
          );
#endif
    }

    static inline string file_num_to_name(int file_num)
    {
      ostringstream num_convert;
      num_convert << setw(8) << setfill('0') << file_num << ".ppm";
      return num_convert.str();
    }

    /** Count xxxxxxxx.ppm files in directory */
    static int count_ppm(const char *path)
    {
      int file_count = 0;
      DIR *dir;
      struct dirent *entry;

      if( (dir = opendir(path)) == NULL )
        return -1;

      errno = 0;

      while( (entry = readdir(dir)) != NULL )
      {
        if( entry->d_type == DT_REG                 // regular file
            && strlen(entry->d_name) == 12          // files are 8 digits plus .ppm extension
            && strcmp(entry->d_name+8, ".ppm") == 0 )
        {
          ++file_count;
        }
      }

      // check the end of the directory was actually reached without error
      if( errno != 0 )
        file_count = -1;

      closedir(dir);

      return file_count;
    }

    Mat &load_image(int file_num)
    {
      string file_name = dir + file_num_to_name(file_num);

      // load the image or exit
      image_orig = imread( file_name );
      if( image_orig.data == 0 )
      {
        cerr << "Error: Couldn't find " << file_name << endl;
        exit(-1);
      }

#ifdef UNDISTORT_LENS
      remap(image_orig, image_undist, map1, map2, INTER_LINEAR);  // correct lens distortion

      return image_undist;
#else
      return image_orig;
#endif
    }
};


/** Class to keep track of the regions found when doing connected component labelling **/
class Region
{
  public:
    Region() {}                     // stops initialisation of the full array of regions
    Region(int dud) : count_(0) {}  // for empty region

    Region(int x, int y) : count_(1), x_total(x), y_total(y), lowest_equivalence(65535) {}

    // keep a total of all the x and y values of pixels in a region - the centre is the average of these points
    void add_point( int x, int y ) { ++count_; x_total += x; y_total += y; }

    // only need to keep track of the lowest equivalent for a region
    void set_equivalence( int region ) { if( region < lowest_equivalence ) lowest_equivalence = region; }

    // some 'getters'
    const int &equivalence() const { return lowest_equivalence; }
    const int &count() const { return count_; }

    // the centre is the average of the x and y values of all the pixels in the region
    Point2d centre() const { return Point2d( (double)x_total/count_, (double)y_total/count_ ); }

    // operator overload for combining regions
    Region& operator+=(Region& other)
    {
      count_ += other.count_;
      x_total += other.x_total;
      y_total += other.y_total;
      return *this;
    }

  private:
    int count_;
    int x_total;
    int y_total;

    int lowest_equivalence;
};



/** @function main */
int main( int argc, char** argv )
{
  // set up video directory related stuff
  char *c_str;

  if( argc >= 2 )
    c_str = realpath(argv[1], NULL);
  else
    c_str = realpath("./", NULL);

  if( c_str == NULL )
  {
    cerr << "Error: Failed looking up input directory" << endl;
    return -1;
  }

  string in_dir(c_str);

  if( *in_dir.rbegin() != '/')   // don't trust realpath to be consistent with trailing slash
    in_dir += '/';

  free(c_str);

  // count the image files
  int file_count = ImageLoader::count_ppm( in_dir.c_str() );
  if( file_count < 0 )
  {
    cerr << "Error: Failed counting .ppm files" << endl;
    return -1;
  }
  else if( file_count == 0 )
  {
    cerr << "Error: No .ppm files found in " << in_dir << endl;
    return -1;
  }

  ImageLoader image_loader(in_dir);

  Mat image = image_loader.load_image(start_file); // use parameters from the first image to initialise the masks
  Mat display_mask(image.size(), image.type());    // mask to display
  Mat region_mask(image.size(), CV_16UC1);         // mask to keep track of connected component regions

#ifdef WRITE_MAT_FILE
  // create the output .mat file
  size_t base_index = in_dir.rfind( '/', in_dir.length()-2 ) + 1;     // -1 + 1 if not found
  string base( in_dir.substr( base_index, in_dir.length()-(1+base_index) ) );
  string outfile_name( base + "_tracking.mat" );

  QString q_outfile_name = QString::fromStdString(outfile_name); 
  if( QFile::exists(q_outfile_name) )
  {
    cerr << outfile_name << " exists already, skipping" << endl;
    return -1;
  }

  MatFileDump outfile( 6, q_outfile_name );
#endif

  // create main tracking window
  namedWindow(in_dir);
  cvMoveWindow(in_dir.c_str(), 600, 0);

  // window for the mask
  namedWindow("mask");
  cvMoveWindow("mask", 600, 500);

#ifdef WRITE_IMAGES
  cerr << "Warning: Writing tracked images to output directory" << endl;
#endif

  Region regions[65534];   // array for all regions found

  Region *large_regions[num_track_regions];  // pointers for the largest distinct regions
  Region empty_region(0);

  bool pause = false;

  /** Do the tracking - loop over all the image files */
  for( int file_num = start_file, end_file = file_count-start_file; file_num < end_file; )
  {
    int region_count = 0;

    image = image_loader.load_image(file_num); // first image loaded twice


    // loop through the image and do an adaption of 4 connected component labeling -
    // http://en.wikipedia.org/wiki/Connected-component_labeling
    for( int row=0; row < image.rows; ++row )
    {
      // create row pointers into image and mask
      uchar  *image_ptr         = image.ptr(row);
      uchar  *display_mask_ptr  = display_mask.ptr(row);

      ushort *region_mask_ptr   = region_mask.ptr<ushort>(row);

      // loop columns
      for( int col=0; col < image.cols; ++col )
      {
        if( is_tracking_spot(image_ptr) ) // assumes BGR
        {
          // pixel deemed to be part of a tracking spot

          // above and left regions - use 65535 if we are at the edge of the image
          ushort region_above = (row==0) ? (65535) : (region_mask_ptr[ -region_mask.step1() ]);
          ushort region_left  = (col==0) ? (65535) : (region_mask_ptr[-1]);

          ushort min_connected = min( region_above, region_left );

          if( min_connected < 65535 )
          {
            // pixel is connected to a previously found region
            
            *region_mask_ptr = min_connected;


            regions[min_connected].add_point(col, row);

            ushort max_connected = max( region_above, region_left );

            // if pixel is connected to another region, set its equivalence to the one with the lower index
            if( max_connected < 65535 && max_connected != min_connected )
              regions[max_connected].set_equivalence(min_connected);
          }
          else
          {
            // pixel not connected to a previously found region - add a new one to the array

            if( region_count >= 65535 )
            {
              cerr << "Error: Exceeded maximum regions" << endl;
              return -1;
            }

            regions[region_count] = Region(col, row);
            *region_mask_ptr = region_count++;
          }

          // display the colours of pixels deemed to be part of the tracking spot - useful for tuning
          display_mask_ptr[0] = image_ptr[0];
          display_mask_ptr[1] = image_ptr[1];
          display_mask_ptr[2] = image_ptr[2];
        }
        else
        {
          // pixel not deemed to be part of a tracking spot

          *region_mask_ptr = 65535;

          display_mask_ptr[0] = 255;
          display_mask_ptr[1] = 255;
          display_mask_ptr[2] = 255;
        }

        // advance row pointers to the next column
        image_ptr += 3;
        display_mask_ptr += 3;

        ++region_mask_ptr;
      }
    }

    // (re)initialise the large region pointers
    for( int index=0; index<num_track_regions; ++index )
      large_regions[index] = &empty_region;

    int distinct_region_count = 0;

    // Loop backward over the array of regions and add the necessary internal values of each
    // region with an equivalent region (always lower) to the equivalent region. Once equivalent
    // regions have been accounted for, we keep pointers to the largest distinct regions (which
    // should be the ones we are looking for).
    while( region_count-- )
    {
      Region &region = regions[region_count];

      if( region.equivalence() < 65535 )
      {
        // total equivalent regions
        regions[region.equivalence()] += region;
      }
      else
      {
        ++distinct_region_count;
        
        // keep pointers to the largest regions - do a simple sorting adaption
        if( region.count() > large_regions[0]->count() )
        {
          int index=1;
          for( ; index<num_track_regions; ++index )
          {
            if( region.count() > large_regions[index]->count() )
              large_regions[index-1] = large_regions[index];
            else
              break;
          }

          large_regions[index-1] = &region;
        }
      }
    }

    // check we found the number of regions we were looking for
    if( distinct_region_count < num_track_regions )
      cerr << "Warning: Only found " << distinct_region_count << " regions in " << ImageLoader::file_num_to_name(file_num) << endl;

#ifdef INPUT_IS_FIELDS 
    // rescale the video field to full frame size so we can display the tracking points nicely
    resize(image, image, Size(), 1, 2);
#endif

    vector<Point2d> track_points(num_track_regions);
 
    // find the centres of the large regions - these are the track points
    for( int index=0; index<num_track_regions; ++index )
    {
      Point2d &track_point = track_points[index];
      track_point = large_regions[index]->centre();

#ifdef INPUT_IS_FIELDS   
      track_point.y = track_point.y*2 + file_num%2;
#endif
    }

    // sort the track_points by their y-values - this is an easy way to distinguish the points
    sort( track_points.begin(), track_points.end(), highest_point ); 

    // average the top to points to get the centre of the upper part of the leg
    Point2d leg_centre = (track_points[0] + track_points[1]) * 0.5;

#ifdef WRITE_MAT_FILE
    // centre of upper part of leg
    outfile << leg_centre.x;
    outfile << leg_centre.y;

    // joint
    outfile << track_points[2].x;
    outfile << track_points[2].y;

    // lower part of leg
    outfile << track_points[3].x;
    outfile << track_points[3].y;
#endif

    // apply shift multiplier to points for sub-pixel rendering
    leg_centre *= shift_mult;
    track_points[0] *= shift_mult;
    track_points[1] *= shift_mult;
    track_points[2] *= shift_mult;
    track_points[3] *= shift_mult;

    // draw lines on each part of the leg in green, 2 pixels thick
    line(image, leg_centre, track_points[2], CV_RGB(0, 255, 0), 2, CV_AA, shift);
    line(image, track_points[2], track_points[3], CV_RGB(0, 255, 0), 2, CV_AA, shift);

    // centre of top of leg in blue, 2 pixels thick
    circle(image, leg_centre, 5*shift_mult, CV_RGB(0, 0, 255), CV_FILLED, CV_AA, shift);

    // all the track points in black, 5 pixel diameter
    circle(image, track_points[0], 5*shift_mult, CV_RGB(0, 0, 0), CV_FILLED, CV_AA, shift);
    circle(image, track_points[1], 5*shift_mult, CV_RGB(0, 0, 0), CV_FILLED, CV_AA, shift);
    circle(image, track_points[2], 5*shift_mult, CV_RGB(0, 0, 0), CV_FILLED, CV_AA, shift);
    circle(image, track_points[3], 5*shift_mult, CV_RGB(0, 0, 0), CV_FILLED, CV_AA, shift);

    // show image and display_mask
    imshow( in_dir, image );
    imshow( "mask", display_mask );

#ifdef WRITE_IMAGES
    imwrite( "tracked_" + image_loader.file_num_to_name(file_num), image );
#endif

    // opencv need this to update display windows
    char key = waitKey(video_wait);

    // do some simple video player actions with the result
    switch(key)
    {
      // quit
      case 27:   // ESC
      case 'q':
        cerr << "User quit, processed " << file_num << '/' << file_count << " images" << endl;
        return 0;

      // toggle pause
      case ' ':
        pause ^= true;
        break;

      // advance frame (when paused)
      case 'n':
      case '.':
      case '>':
        if( file_num+1 < end_file )
          ++file_num;
        break;

      // back frame (when paused)
      case 'N':
      case ',':
      case '<':
#ifndef WRITE_MAT_FILE
        if( pause && file_num > start_file )
          --file_num;
        break;
#else
        // TODO - implement this properly so back skipping and writing the output do not need
        // to be mutually exclusive
        cerr << "WRITE_MAT_FILE must be undefined to enable backward skipping" << endl;
        // **fall through**
#endif

      default:
        if( !pause )
          ++file_num;
        break;
    }
  }

  return 0;
}
