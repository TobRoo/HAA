

#define _USE_MATH_DEFINES
#include <math.h>

#define DegreesToRadians(a) (a*M_PI/180.0) 
#define RadiansToDegrees(a) (a*180.0/M_PI)

#define Px(img,i,j) img->data[img->offset+(i)+(j)*img->stride]

struct FIMAGE {
	float *data;
	int rows;
	int cols;
	int offset; // initial buffer offset
	int stride; // column stride
	int bufSize;
};


FIMAGE * NewImage( int rows, int cols, float *data = NULL );
FIMAGE * NewImage( FIMAGE *image );
int ReallocImage( FIMAGE *image, int rows, int cols, float *data = NULL );
void FreeImage( FIMAGE *image );

void CopyImageEx( FIMAGE *image, FIMAGE *target );
FIMAGE *CopyImage( FIMAGE *image );

void IntegralRotateEx( FIMAGE *image, int rotations, FIMAGE *target );
FIMAGE *IntegralRotate( FIMAGE *image, int rotations );

void XShearImage( FIMAGE *image, float shift, int rows, int cols, int x_offset, int y_offset, float bgColor = 0.0f );
void YShearImage( FIMAGE *image, float shift, int rows, int cols, int x_offset, int y_offset, float bgColor = 0.0f );

void RotateImageEx( FIMAGE *image, float degrees, FIMAGE *target, FIMAGE *integralTarget, float bgColor = 0.0f );
FIMAGE * RotateImage( FIMAGE *image, float degrees, float bgColor = 0.0f );

void ImageAdd( FIMAGE *image, FIMAGE *target, float x_loc, float y_loc, float scale, float blend, int mode );
