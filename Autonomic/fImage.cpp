
#include "stdafx.h"

#include "fImage.h"

#include <malloc.h>

#ifndef min
#define min(a,b) (a <= b ? a : b)
#endif
#ifndef max
#define max(a,b) (a >= b ? a : b)
#endif

FIMAGE * NewImage( int rows, int cols, float *data ) {
	FIMAGE *image = (FIMAGE *)malloc( sizeof(FIMAGE) );
	if ( image == NULL )
		return 0;

	image->rows = rows;
	image->cols = cols;
	image->offset = 0;
	image->stride = rows;
	image->bufSize = rows*cols*sizeof(float);
	image->data = (float *)malloc( image->bufSize );
	if ( image->data == NULL )
		return 0;
	
	if ( data != NULL )
		memcpy( image->data, data, rows*cols*sizeof(float) );

	return image;
}

FIMAGE * NewImage( FIMAGE *image ) {
	return NewImage( image->rows, image->cols, image->data );
}

int ReallocImage( FIMAGE *image, int rows, int cols, float *data ) {
	float *newData = (float *)malloc( rows*cols*sizeof(float) );

	if ( newData == NULL )
		return 1;
	
	free( image->data );

	image->rows = rows;
	image->cols = cols;
	image->offset = 0;
	image->stride = rows;
	image->bufSize = rows*cols*sizeof(float);
	image->data = newData;

	if ( data != NULL )
		memcpy( image->data, data, image->bufSize );

	return 0;
}

void FreeImage( FIMAGE *image ) {
	if ( image != NULL ) {
		if ( image->data != NULL )
			free( image->data );
		free( image );
	}
}

void CopyImageEx( FIMAGE *image, FIMAGE *target ) {
	int j;

	target->rows = image->rows;
	target->cols = image->cols;
	target->offset = 0;
	target->stride = image->rows;

	if ( image->rows == image->stride ) {
		memcpy( target->data, image->data+image->offset, image->rows*image->cols*sizeof(float) );
	} else {
		for ( j=0; j<image->cols; j++ ) {
			memcpy( target->data+j*target->stride, image->offset+image->data+j*image->stride, image->rows*sizeof(float) );
		}
	}	
}

FIMAGE *CopyImage( FIMAGE *image ) {
	FIMAGE *copy_image = NewImage( image->rows, image->cols );
	if ( copy_image == NULL )
		return NULL;

	CopyImageEx( image, copy_image );

	return copy_image;
}

void CropToFitImage( FIMAGE *image, float x_shear, float y_shear, int width, int height, bool rotate ) {
	float extent[4][2];
	float min[2], max[2];

	int i, j, jc, rowsc, colsc, offsetc;

	extent[0][0] = -width/2.0f;
	extent[0][1] = -height/2.0f;
	extent[1][0] = width/2.0f;
	extent[1][1] = -height/2.0f;
	extent[2][0] = -width/2.0f;
	extent[2][1] = height/2.0f;
	extent[3][0] = width/2.0f;
	extent[3][1] = height/2.0f;
	for ( i=0; i<4; i++ ) {
		extent[i][0] += x_shear*extent[i][1];
		extent[i][1] += y_shear*extent[i][0];
		if ( rotate != false )
			extent[i][0] += x_shear*extent[i][1];
		extent[i][0] += image->cols/2.0f;
		extent[i][1] += image->rows/2.0f;
	}
	min[0] = max[0] = extent[0][0];
	min[1] = max[1] = extent[0][1];
	for ( i=1; i<4; i++ ) {
		if (min[0] > extent[i][0])
			min[0] = extent[i][0];
		if (min[1] > extent[i][1])
			min[1] = extent[i][1];
		if (max[0] < extent[i][0])
			max[0] = extent[i][0];
		if (max[1] < extent[i][1])
			max[1] = extent[i][1];
	}

	offsetc = (int)(min[1]+0.5);
	rowsc = min(image->rows, (int)(max[1]+0.5) - (int)(min[1]+0.5));
	colsc = min(image->cols, (int)(max[0]+0.5) - (int)(min[0]+0.5));
	for ( jc=0, j=(int)(min[0]+0.5); jc<colsc; jc++, j++ ) {
		memcpy( image->data+jc*rowsc, image->offset+image->data+j*image->stride+offsetc, rowsc*sizeof(float) );
	}

	image->rows = rowsc;
	image->cols = colsc;
	image->offset = 0;
	image->stride = rowsc;
}

void IntegralRotateEx( FIMAGE *image, int rotations, FIMAGE *target ) {
	int i, j;

	rotations %= 4;

	target->offset = 0;

	switch ( rotations ) {
		case 0:
			target->rows = image->rows;
			target->cols = image->cols;
			target->stride = image->rows;
			if ( image->rows == image->stride ) {
				memcpy( target->data, image->data+image->offset, image->rows*image->cols*sizeof(float) );
			} else {
				for ( j=0; j<image->cols; j++ ) {
					memcpy( target->data+j*target->stride, image->offset+image->data+j*image->stride, image->rows*sizeof(float) );
				}
			}
			break;
		case 1:
			target->rows = image->cols;
			target->cols = image->rows;
			target->stride = image->cols;
			for ( j=0; j<image->cols; j++ ) {
				for ( i=0; i<image->rows; i++ ) {
					target->data[(target->rows-1-j)+i*target->stride] = Px(image,i,j);
				}
			}
			break;
		case 2:
			target->rows = image->rows;
			target->cols = image->cols;
			target->stride = image->rows;
			for ( j=0; j<image->cols; j++ ) {
				for ( i=0; i<image->rows; i++ ) {
					target->data[(target->rows-1-i)+(target->cols-1-j)*target->stride] = Px(image,i,j);
				}
			}
			break;
		case 3:
			target->rows = image->cols;
			target->cols = image->rows;
			target->stride = image->cols;
			for ( j=0; j<image->cols; j++ ) {
				for ( i=0; i<image->rows; i++ ) {
					target->data[j+(target->cols-1-i)*target->stride] = Px(image,i,j);
				}
			}
			break;
	}
}

FIMAGE *IntegralRotate( FIMAGE *image, int rotations ) {
	FIMAGE *rotate_image = NewImage( image->rows, image->cols );
	if ( rotate_image == NULL )
		return NULL;

	IntegralRotateEx( image, rotations, rotate_image );

	return rotate_image;
}

void XShearImage( FIMAGE *image, float shift, int rows, int cols, int x_offset, int y_offset, float bgColor ) {
	int i, j, c;
	int direction, step;
	float displacement, area;

	typedef enum
	{
	LEFT,
	RIGHT
	} ShearDirection;

	for ( i=y_offset; i<rows+y_offset; i++ ) {
		displacement = shift * ( i - y_offset - ((rows-1)/2.0f) ); // should this be (rows-1)/2.0?
		if ( displacement == 0 )
			continue;
		if ( displacement > 0 ) {
			direction = RIGHT;
		} else {
			displacement *= -1;
			direction = LEFT;
		}
		step = (int)floor(displacement);
		area = displacement - step;
		step++;

		switch ( direction ) {
		case LEFT:
			j = x_offset - step;
			// shift pixels left in left to right order
			for ( c=0; c<cols; c++, j++ ) { 
				if ( j < 0)
					continue;
				Px(image,i,j) = Px(image,i,c+x_offset)*area + Px(image,i,c-1+x_offset)*(1-area);
			}
			// shift right most pixel
			Px(image,i,j) = bgColor*area + Px(image,i,x_offset+cols-1)*(1-area);
			j++;
			// fill in empty pixels
			while ( j < x_offset+cols ) {
				Px(image,i,j) = bgColor;
				j++;
			}
			break;
		case RIGHT:
			j = x_offset + cols-1 + step;
			// shift pixels right in right to left order
			for ( c=cols-1; c>=0; c--, j-- ) {
				if ( j >= image->cols)
					continue;
				Px(image,i,j) = Px(image,i,c+x_offset)*area + Px(image,i,c+1+x_offset)*(1-area);
			}
			// shift left most pixel
			Px(image,i,j) = bgColor*area + Px(image,i,x_offset)*(1-area);
			j--;
			// fill in empty pixels
			while ( j >= x_offset ) {
				Px(image,i,j) = bgColor;
				j--;
			}
			break;
		}
	}
}

void YShearImage( FIMAGE *image, float shift, int rows, int cols, int x_offset, int y_offset, float bgColor ) {
	int i, j, r;
	int direction, step;
	float displacement, area;

	typedef enum
	{
	UP,
	DOWN
	} ShearDirection;

	for ( j=x_offset; j<cols+x_offset; j++ ) {
		displacement = shift * ( j - x_offset - ((cols-1)/2.0f) ); // should this be (cols-1)/2.0?
		if ( displacement == 0 )
			continue;
		if ( displacement > 0 ) {
			direction = DOWN;
		} else {
			displacement *= -1;
			direction = UP;
		}
		step = (int)floor(displacement);
		area = displacement - step;
		step++;

		switch ( direction ) {
		case UP:
			i = y_offset - step;
			// shift pixels up in up to down order
			for ( r=0; r<rows; r++, i++ ) { 
				if ( i < 0)
					continue;
				Px(image,i,j) = Px(image,r+y_offset,j)*area + Px(image,r-1+y_offset,j)*(1-area);
			}
			// shift right most pixel
			Px(image,i,j) = bgColor*area + Px(image,y_offset+rows-1,j)*(1-area);
			i++;
			// fill in empty pixels
			while ( i < y_offset+rows ) {
				Px(image,i,j) = bgColor;
				i++;
			}
			break;
		case DOWN:
			i = y_offset + rows-1 + step;
			// shift pixels down in down to up order
			for ( r=rows-1; r>=0; r--, i-- ) {
				if ( i >= image->rows)
					continue;
				Px(image,i,j) = Px(image,r+y_offset,j)*area + Px(image,r+1+y_offset,j)*(1-area);
			}
			// shift left most pixel
			Px(image,i,j) = bgColor*area + Px(image,y_offset,j)*(1-area);
			i--;
			// fill in empty pixels
			while ( i >= y_offset ) {
				Px(image,i,j) = bgColor;
				i--;
			}
			break;
		}
	}
}

// Adapted from the ImageMagick magickcore library
void RotateImageEx( FIMAGE *image, float degrees, FIMAGE *target, FIMAGE *integralTarget, float bgColor ) {
	int i, j;
	float angle;
	int rotations;
	float shearX, shearY;
	int width, height, y_width, x_offset, y_offset;

	// normalize angle and calculate prerotations
	angle = degrees;
	while ( angle < -45 ) 
		angle += 360;

	for ( rotations=0; angle > 45; rotations++ )
		angle -= 90;
	rotations %= 4;

	// prerotate
	IntegralRotateEx( image, rotations, integralTarget );

	shearX = (float)tan( DegreesToRadians(angle)/2.0 );
	shearY = (float)-sin( DegreesToRadians(angle) );
	if ((shearX == 0.0) && (shearY == 0.0)) {
		CopyImageEx( integralTarget, target );
		return;
	}

	// compute image size
	width = integralTarget->cols;
	height = integralTarget->rows;
	y_width = width + 2*((int)(fabs(shearX)*height/2.0) + 1);
	y_offset = (int)(fabs(shearY)*width/2.0) + 1;
	x_offset = 2*(int)(fabs(shearX)*height/2.0) + 1;
	//y_width = width + (int)(fabs(shearX)*height+0.5);
	//x_offset = (int)( ( ( fabs(shearY) * height ) - width ) / 2.0 + 0.5 );
	//y_offset = (int)( ( ( fabs(shearY) * y_width ) - height ) / 2.0 + 0.5 );

	// copy with border
	target->rows = height + 2*y_offset;
	target->cols = width + 2*x_offset;
	target->offset = 0;
	target->stride = target->rows;
	// fill left border
	for ( j=0; j<x_offset; j++ ) { 
		for ( i=0; i<target->rows; i++ ) {
			target->data[i+j*target->stride] = bgColor;
		}
	}
	// fill top and bottom border, and data
	for ( j=x_offset; j<width+x_offset; j++ ) { 
		for ( i=0; i<y_offset; i++ ) {
			target->data[i+j*target->stride] = bgColor;
		}
		for ( i=y_offset; i<height+y_offset; i++ ) {
			target->data[i+j*target->stride] = Px(integralTarget,i-y_offset,j-x_offset);
		}
		for ( i=height+y_offset; i<target->rows; i++ ) {
			target->data[i+j*target->stride] = bgColor;
		}
	}
	// fill right border
	for ( j=width+x_offset; j<target->cols; j++ ) { 
		for ( i=0; i<target->rows; i++ ) {
			target->data[i+j*target->stride] = bgColor;
		}
	}

	//PrintImage( target, "Border Image" );

	// Rotate Image
	XShearImage( target, shearX, height, width, x_offset, (target->rows-height)/2, bgColor );
	//PrintImage( target, "XShear Image" );
	YShearImage( target, shearY, height, y_width, (target->cols-y_width)/2, y_offset, bgColor );
	//PrintImage( target, "YShear Image" );
	XShearImage( target, shearX, target->rows, y_width, (target->cols-y_width)/2, 0, bgColor );
	//PrintImage( target, "XShear Image" );

	CropToFitImage( target, shearX, shearY, width, height, true );

}

// Adapted from the ImageMagick magickcore library
FIMAGE * RotateImage( FIMAGE *image, float degrees, float bgColor ) {
	int i, j;
	FIMAGE *integral_image, *rotate_image;
	float angle;
	int rotations;
	float shearX, shearY;
	int width, height, y_width, x_offset, y_offset;

	// normalize angle and calculate prerotations
	angle = degrees;
	while ( angle < -45 ) 
		angle += 360;

	for ( rotations=0; angle > 45; rotations++ )
		angle -= 90;
	rotations %= 4;

	// prerotate
	integral_image = IntegralRotate( image, rotations );

	shearX = (float)tan( DegreesToRadians(angle)/2.0 );
	shearY = (float)-sin( DegreesToRadians(angle) );
	if ((shearX == 0.0) && (shearY == 0.0))
		return integral_image;

	// compute image size
	width = integral_image->cols;
	height = integral_image->rows;
	y_width = width + 2*((int)(fabs(shearX)*height/2.0) + 1);
	y_offset = (int)(fabs(shearY)*width/2.0) + 1;
	x_offset = 2*(int)(fabs(shearX)*height/2.0) + 1;
	//y_width = width + (int)(fabs(shearX)*height+0.5);
	//x_offset = (int)( ( ( fabs(shearY) * height ) - width ) / 2.0 + 0.5 );
	//y_offset = (int)( ( ( fabs(shearY) * y_width ) - height ) / 2.0 + 0.5 );

	// copy with border
	rotate_image = NewImage( height + 2*y_offset, width + 2*x_offset );
	// fill left border
	for ( j=0; j<x_offset; j++ ) { 
		for ( i=0; i<rotate_image->rows; i++ ) {
			rotate_image->data[i+j*rotate_image->stride] = bgColor;
		}
	}
	// fill top and bottom border, and data
	for ( j=x_offset; j<width+x_offset; j++ ) { 
		for ( i=0; i<y_offset; i++ ) {
			rotate_image->data[i+j*rotate_image->stride] = bgColor;
		}
		for ( i=y_offset; i<height+y_offset; i++ ) {
			rotate_image->data[i+j*rotate_image->stride] = integral_image->data[(i-y_offset)+(j-x_offset)*height];
		}
		for ( i=height+y_offset; i<rotate_image->rows; i++ ) {
			rotate_image->data[i+j*rotate_image->stride] = bgColor;
		}
	}
	// fill right border
	for ( j=width+x_offset; j<rotate_image->cols; j++ ) { 
		for ( i=0; i<rotate_image->rows; i++ ) {
			rotate_image->data[i+j*rotate_image->stride] = bgColor;
		}
	}

	//PrintImage( integral_image, "Integral Image" );
	
	FreeImage( integral_image );

	//PrintImage( rotate_image, "Border Image" );

	// Rotate Image
	XShearImage( rotate_image, shearX, height, width, x_offset, (rotate_image->rows-height)/2, bgColor );
	YShearImage( rotate_image, shearY, height, y_width, (rotate_image->cols-y_width)/2, y_offset, bgColor );
	XShearImage( rotate_image, shearX, rotate_image->rows, y_width, (rotate_image->cols-y_width)/2, 0, bgColor );
	
	CropToFitImage( rotate_image, shearX, shearY, width, height, true );

	return rotate_image;
}

// mode 0 - average, mode 1 - sum
// WARNING! This function doesn't check to make sure the image fits on top the target!
void ImageAdd( FIMAGE *image, FIMAGE *target, float x_loc, float y_loc, float scale, float blend, int mode ) {
	int i, j;
	int r, rr, c, cc;
	float fr, fc;
	float scaleInv = 1/scale;

	if ( mode == 0 )
		blend = scale*scale*blend;

	c = (int)x_loc; // beginning target col
	for ( j=0; j<image->cols; j++ ) { // loop through image cols
		fc = j*scale + x_loc;
		cc = (int)fc;

		r = (int)y_loc; // beginning target row
		if ( c == cc ) { // all in one col
			for ( i=0; i<image->rows; i++ ) { // loop through image rows
				fr = i*scale + y_loc;
				rr = (int)fr;
				if ( rr >= target->rows )
					rr = rr; // we're off the top!
				if ( r == rr ) { // all in one row
					Px(target,r,c) += Px(image,i,j)*blend;
				} else {
					Px(target,r,c) += Px(image,i,j)*blend*(1-(fr-rr)*scaleInv);
					Px(target,rr,c) += Px(image,i,j)*blend*(fr-rr)*scaleInv;
				}
				r = rr;
			}
		} else { // split between two rows
			for ( i=0; i<image->rows; i++ ) { // loop through image rows
				fr = i*scale + y_loc;
				rr = (int)fr;
				if ( rr >= target->rows )
					rr = rr; // we're off the top!
				if ( r == rr ) { // all in one row
					Px(target,r,c) += Px(image,i,j)*blend*(1-(fc-cc)*scaleInv);
					Px(target,r,cc) += Px(image,i,j)*blend*(fc-cc)*scaleInv;
				} else {
					Px(target,r,c) += Px(image,i,j)*blend*(1-(fr-rr)*scaleInv)*(1-(fc-cc)*scaleInv);
					Px(target,rr,c) += Px(image,i,j)*blend*(fr-rr)*scaleInv*(1-(fc-cc)*scaleInv);
					Px(target,r,cc) += Px(image,i,j)*blend*(1-(fr-rr)*scaleInv)*(fc-cc)*scaleInv;
					Px(target,rr,cc) += Px(image,i,j)*blend*(fr-rr)*scaleInv*(fc-cc)*scaleInv;
				}
				r = rr;
			}
		}
		c = cc;
	}
}

// NOT FINISHED? what about mixing the left and right edges?
void ImageStamp( FIMAGE *image, FIMAGE *target, float x_loc, float y_loc, float scale, float blend, int mode ) {
	int i, j, ii, jj, r, c;
	float fi, fii, fj, fjj;
	float mix, mixtop, mixbottom;
	float scalescaleblend;
	
	// prepare target
	mix = 1-blend;
	mixtop = y_loc - (int)y_loc;
	mixtop = mixtop + (1-mixtop)*mix;
	mixbottom = 1 - ( (y_loc + image->rows*scale) - (int)(y_loc + image->rows*scale) );
	mixbottom = mixbottom + (1-mixbottom)*mix;
	// TODO do left edge
	
	// do middle
	for ( j=(int)x_loc + 1; j<(int)(x_loc + image->cols*scale) - 1; j++ ) {
		Px(target,(int)y_loc,j) *= mixtop;
		for ( i=(int)y_loc + 1; i<(int)(y_loc + image->rows*scale) - 1; i++ ) {
			Px(target,i,j) *= mix; 
		}
		Px(target,(int)(y_loc + image->rows*scale)-1,j) *= mixbottom;
	}
	// TODO do right edge

	scalescaleblend = scale*scale*blend;

	// stamp image
	for ( c=0; c<image->cols; c++ ) {
		fj = x_loc + c*scale;
		fjj = fj + scale;
		j = (int)fj;
		jj = (int)fjj;
		if ( j == jj ) { // whole pixel in one column
			for ( r=0; r<image->rows; r++ ) {
				fi = y_loc + r*scale;
				fii = fi + scale;
				i = (int)fi;
				ii = (int)fii;
				if ( i == ii ) { // whole pixel in one row
					target->data[i+j*target->rows] += image->data[r+c*image->rows]*scalescaleblend;
				} else { // pixel split
					target->data[i+j*target->rows] += image->data[r+c*image->rows]*scalescaleblend*(ii-fi);
					target->data[ii+j*target->rows] += image->data[r+c*image->rows]*scalescaleblend*(fii-ii);
				}
			}
		} else { // pixel split
			for ( r=0; r<image->rows; r++ ) {
				fi = y_loc + r*scale;
				fii = fi + scale;
				i = (int)fi;
				ii = (int)fii;
				if ( i == ii ) { // whole pixel in one row
					target->data[i+j*target->rows] += image->data[r+c*image->rows]*scalescaleblend*(jj-fj);
					target->data[i+jj*target->rows] += image->data[r+c*image->rows]*scalescaleblend*(fjj-jj);
				} else { // pixel split
					target->data[i+j*target->rows] += image->data[r+c*image->rows]*scalescaleblend*(ii-fi)*(jj-fj);
					target->data[ii+j*target->rows] += image->data[r+c*image->rows]*scalescaleblend*(fii-ii)*(jj-fj);
					target->data[i+jj*target->rows] += image->data[r+c*image->rows]*scalescaleblend*(ii-fi)*(fjj-jj);
					target->data[ii+jj*target->rows] += image->data[r+c*image->rows]*scalescaleblend*(fii-ii)*(fjj-jj);
				}
			}
		}
	}
	
}