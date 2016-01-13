
#include "StdAfx.h"
#include "Visualize.h"

#include "OpenGLControl.h"

#include "..\\autonomic\\fImage.h"

Visualize::Visualize() {
	int i;

	this->nextPathId = 0;
	for ( i=0; i<MAX_PATHS; i++ ) {
		this->paths[i].nodes = NULL;
	}

	this->nextObjectId = 0;
	this->highObjectId = 0;
	for ( i=0; i<MAX_OBJECTS; i++ ) {
		this->objects[i].path_refs = NULL;
	}

	this->nextfImageId = 0;
	for ( i=0; i<MAX_FIMAGES; i++ ) {
		this->fimages[i] = NULL;
		this->fimageOrder[i] = -128;
	}

	this->nextStringId = 0;
	this->highStringId = 0;
	for ( i=0; i<MAX_STRINGS; i++ ) {
		this->strings[i].valid = false;
		this->strings[i].str[0] = 0;
	}

	this->consoleVisible = true;
	this->defaultStatusColor[0] = 0.0f;
	this->defaultStatusColor[1] = 1.0f;
	this->defaultStatusColor[2] = 0.0f;
	this->defaultConsoleColor[0] = 0;
	this->defaultConsoleColor[1] = 0;
	this->defaultConsoleColor[2] = 1;
	this->statusLine = 0;
	this->consoleLine = 0;
	for ( i=0; i<MAX_CONSOLE_LINES; i++ ) {
		this->statusMsgs[i][0] = '\0';
		this->consoleMsgs[i][0] = '\0';
	}

}

Visualize::~Visualize() {
	int i;

	for ( i=0; i<MAX_OBJECTS; i++ ) {
		if ( this->objects[i].path_refs != NULL )
			this->deleteObject( i );
	}

	for ( i=0; i<MAX_PATHS; i++ ) {
		if ( this->paths[i].nodes != NULL )
			this->deletePath( i );
	}
}

int Visualize::Initialize( COpenGLControl *oglC ) {

	this->oglControl = oglC;

	// TEMP
	//float x[] = { 0, 1, 2, 4 };
	//float y[] = { 0, -1, -2, 4 };
	//float colour[] = { 1, 1, 0 };

	//int id = newPath( 4, x, y );

	//int obj = newStaticObject( 2, 2, 3.1416f, 2, id, colour, 4 );

	//newStaticObject( 0, 0, 0, 1, id, colour, 4 );

	return 0;
}

int Visualize::ClearAll() {
	int i;

	for ( i=0; i<MAX_OBJECTS; i++ ) {
		if ( this->objects[i].path_refs != NULL )
			this->deleteObject( i );
	}

	for ( i=0; i<MAX_PATHS; i++ ) {
		if ( this->paths[i].nodes != NULL )
			this->deletePath( i );
	}

		this->nextfImageId = 0;
	for ( i=0; i<MAX_FIMAGES; i++ ) {
		this->fimages[i] = NULL;
		this->fimageOrder[i] = -128;
	}

	this->nextStringId = 0;
	this->highStringId = 0;
	for ( i=0; i<MAX_STRINGS; i++ ) {
		this->strings[i].valid = false;
		this->strings[i].str[0] = 0;
	}

	return 0;
}

int Visualize::PreDraw() {

	return 0;
}

int Visualize::Draw() {
	int i, j;
	int imgId;
	std::list<PATH_REFERENCE *>::iterator prIter;
	std::list<NODE *>::iterator nIter;

	// draw fImage layers 127-0
	imgId = 0;
	while ( imgId < MAX_FIMAGES && this->fimageOrder[imgId] != -128 && this->fimageInfo[this->fimageOrder[imgId]].layer >= 0 ) {
		this->drawImage( this->fimages[this->fimageOrder[imgId]], &this->fimageInfo[this->fimageOrder[imgId]] );
		imgId++;
	}

	// draw grid
	glPolygonMode(GL_FRONT, GL_LINE); // Wireframe Mode
	
	glColor3f( 0, 0, 0 );
	glLineWidth( 1 );
	glBegin(GL_LINES);
		for ( i = (int)floor(this->oglControl->viewportBottom); i <= ceil(this->oglControl->viewportTop); i++ ) {
		   glVertex3f( this->oglControl->viewportLeft, (float)i, 0 );
		   glVertex3f( this->oglControl->viewportRight, (float)i, 0 );
		}
		for ( i = (int)floor(this->oglControl->viewportLeft); i <= ceil(this->oglControl->viewportRight); i++ ) {
		   glVertex3f( (float)i, this->oglControl->viewportBottom, 0 );
		   glVertex3f( (float)i, this->oglControl->viewportTop, 0 );
		}
	glEnd();

	// draw objects
	for ( i=0; i<=this->highObjectId; i++ ) {

		if ( this->objects[i].path_refs == NULL
		  || !this->objects[i].visible )
			continue;

		glPushMatrix();
		glTranslatef( *this->objects[i].x, *this->objects[i].y, 0 );
		if ( this->objects[i].s != NULL ) // shortcut to allow objects without scale
			glScalef( *this->objects[i].s, *this->objects[i].s, 1 );
		glRotatef( (float)RadiansToDegrees( *this->objects[i].r ), 0, 0, 1 );

		prIter = this->objects[i].path_refs->begin();
		while ( prIter != this->objects[i].path_refs->end() ) {
			glColor3f( (*prIter)->r, (*prIter)->g, (*prIter)->b );			
			glLineWidth( (*prIter)->lineWidth );
			if ( (*prIter)->stipple == 0 ) {
				glDisable(GL_LINE_STIPPLE);
			} else {
				glLineStipple(5, 0xAAAA);
				glEnable(GL_LINE_STIPPLE);
			}

			glBegin( GL_LINE_STRIP );
				nIter = this->paths[(*prIter)->id].nodes->begin();
				while ( nIter != this->paths[(*prIter)->id].nodes->end() ) {
					glVertex3f( (*nIter)->x, (*nIter)->y, 0 );
					nIter++;
				}
			glEnd();

			prIter++;
		}

		glPopMatrix();
	}



	// draw fImage layers -1--127
	while ( imgId < MAX_FIMAGES && this->fimageOrder[imgId] != -128 ) {
		this->drawImage( this->fimages[this->fimageOrder[imgId]], &this->fimageInfo[this->fimageOrder[imgId]] );
		imgId++;
	}

	// draw strings
	glPushMatrix();
	int fheight;
	GLubyte bytes[1] = { 0 };
	float xpos = this->oglControl->viewportLeft + 1;
	float ypos = this->oglControl->viewportBottom + 1;
	float zoomEInv = 1/this->oglControl->getZoomE(); 
	for ( i=0; i<=this->highStringId; i++ ) {
		if ( this->strings[i].valid && this->strings[i].visible ) {
			fheight = (int)((this->strings[i].s*4*12)*zoomEInv);
			if ( fheight > 0
			  && this->strings[i].x > this->oglControl->viewportLeft - 5
			  && this->strings[i].x < this->oglControl->viewportRight + 1
			  && this->strings[i].y > this->oglControl->viewportBottom -1
			  && this->strings[i].y < this->oglControl->viewportTop + 1 ) {
				this->oglControl->setFontHeight( fheight );
				glColor3f( this->strings[i].colour[0], this->strings[i].colour[1], this->strings[i].colour[2] );		
				glRasterPos2f( xpos, ypos );
				glBitmap( 1, 1, 0, 0, (this->strings[i].x-xpos)*230*zoomEInv, (this->strings[i].y-ypos)*230*zoomEInv, bytes ); // trick the rasterpos to be valid even off screen
				this->oglControl->glPrint( this->strings[i].str );
			}
		}
	}
	glPopMatrix();

	// draw messages
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glTranslatef(0.0f,0.0f,-1.0f);

	CRect *rect = this->oglControl->getWindowRect();
	if ( rect->Height() && this->consoleVisible ) {
		this->oglControl->setFontHeight( 12 );

		int fontHeight = this->oglControl->getFontHeight();
		float lineHeight = 2.0f*fontHeight/rect->Height();
		float xOffset = -1.0f + 10.0f/rect->Height();
		int lineCount = 0;


		// draw status
		for ( i=0; i<this->statusLine; i++ ) {
			lineCount++;
			glColor3f( this->statusColor[i][0], this->statusColor[i][1], this->statusColor[i][2] );		
			glRasterPos2f( xOffset, 1 - lineCount*lineHeight );
			this->oglControl->glPrint( this->statusMsgs[i] );
		}

		// draw console
		for ( j=this->consoleLine; j<this->consoleLine + MAX_CONSOLE_LINES; j++ ) {
			i = j % MAX_CONSOLE_LINES;
			lineCount++;
			if ( this->consoleMsgs[i][0] == '\0' )
				continue;
			glColor3f( this->consoleColor[i][0], this->consoleColor[i][1], this->consoleColor[i][2] );		
			glRasterPos2f( xOffset, 1 - lineCount*lineHeight );
			this->oglControl->glPrint( this->consoleMsgs[i] );
		}
	}

	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);

	return 0;
}

int Visualize::statusClear() {
	int i;

	this->statusLine = 0;
	for ( i=0; i<MAX_CONSOLE_LINES; i++ ) {
		this->statusMsgs[i][0] = '\0';
	}

	return 0;
}

int Visualize::statusPrintLn( float *color, const char *fmt, ... ) {
	va_list		ap;					// Pointer To List Of Arguments

	if ( this->statusLine == MAX_CONSOLE_LINES )
		return 1;

	if (fmt == NULL)					// If There's No Text
		return 0;						// Do Nothing

	if ( color )
		memcpy( this->statusColor[this->statusLine], color, 3*sizeof(float) );
	else
		memcpy( this->statusColor[this->statusLine], this->defaultStatusColor, 3*sizeof(float) );


	va_start(ap, fmt);					// Parses The String For Variables
		vsprintf_s( this->statusMsgs[this->statusLine], sizeof(this->statusMsgs[0]), fmt, ap);				// And Converts Symbols To Actual Numbers
	va_end(ap);						// Results Are Stored In Text

	this->statusLine++;

	return 0;
}

int Visualize::consoleClear() {
	int i;
	
	this->consoleLine = 0;
	for ( i=0; i<MAX_CONSOLE_LINES; i++ ) {
		this->consoleMsgs[i][0] = '\0';
	}

	return 0;
}

int Visualize::consolePrintLn( float *color, const char *fmt, ... ) {
	va_list		ap;					// Pointer To List Of Arguments

	if (fmt == NULL)					// If There's No Text
		return 0;						// Do Nothing

	if ( color )
		memcpy( this->consoleColor[this->consoleLine], color, 3*sizeof(float) );
	else
		memcpy( this->consoleColor[this->consoleLine], this->defaultConsoleColor, 3*sizeof(float) );

	va_start(ap, fmt);					// Parses The String For Variables
		vsprintf_s( this->consoleMsgs[this->consoleLine], sizeof(this->consoleMsgs[0]), fmt, ap);				// And Converts Symbols To Actual Numbers
	va_end(ap);						// Results Are Stored In Text

	this->consoleLine = (this->consoleLine + 1) % MAX_CONSOLE_LINES;

	return 0;
}

int Visualize::drawImage( FIMAGE *img, FIMAGE_INFO *img_info ) {
	int i, j;
	float p;

	glPolygonMode(GL_FRONT, GL_FILL); // Filled Mode

	for ( i=0; i<img->rows; i++ ) {
		for ( j=0; j<img->cols; j++ ) {
			p = Px(img,i,j) + img_info->pixel_offset;
			glColor3f( img_info->bg[0]+p*img_info->bg_offset[0],
					   img_info->bg[1]+p*img_info->bg_offset[1],
					   img_info->bg[2]+p*img_info->bg_offset[2] );
			glBegin(GL_QUADS);
				glVertex3f( j*img_info->s + img_info->x, i*img_info->s + img_info->y, 0 );
				glVertex3f( (j+1)*img_info->s + img_info->x, i*img_info->s + img_info->y, 0 );
				glVertex3f( (j+1)*img_info->s + img_info->x, (i+1)*img_info->s + img_info->y, 0 );
				glVertex3f( j*img_info->s + img_info->x, (i+1)*img_info->s + img_info->y, 0 );
			glEnd();
		}
	}

	return 0;
}

int Visualize::newPath( int count, float *x, float *y ) {
	int i;
	NODE *node;

	while ( this->nextPathId < MAX_PATHS && this->paths[this->nextPathId].nodes != NULL )
		this->nextPathId++;

	if ( this->nextPathId == MAX_PATHS )
		return -1; // we ran out of ids!

	this->paths[this->nextPathId].references = 0;
	this->paths[this->nextPathId].nodes = new std::list<NODE *>;
	if ( this->paths[this->nextPathId].nodes == NULL )
		return -1;

	for ( i=0; i<count; i++ ) {
		node = (NODE *)malloc( sizeof(NODE) );
		if ( node == NULL )
			return -1;

		node->x = x[i];
		node->y = y[i];

		this->paths[this->nextPathId].nodes->push_back( node );
	}

	return this->nextPathId++;
}

int Visualize::deletePath( int id ) {

	if ( id < this->nextPathId )
		this->nextPathId = id;

	std::list<NODE *>::iterator iter = this->paths[id].nodes->begin();
	while ( iter != this->paths[id].nodes->end() ) {
		free ( *iter );
		iter++;
	}
	delete this->paths[id].nodes;

	this->paths[id].references = 0;
	this->paths[id].nodes = NULL;

	return 0;
}

int Visualize::extendPath( int id, int count, float *x, float *y ) {

	return 0;
}

int Visualize::updatePath( int id, int count, int *nodes, float *x, float *y ) {

	return 0;
}

int Visualize::loadPathFile( char *fileN ) {
	FILE *file;
	char cBuf[1024];
	int scanned, points, id, obj;
	float x[65], y[65];
	float poseX, poseY, poseR, poseS;
	float width, colour[3];
	int solid;

	
	if ( fopen_s( &file, fileN, "r" ) ) {
		return 1; // failed to open file
	}

	// expecting one or more paths with the format:
	// point=<x float>\t<y float>\n
	// point=<x float>\t<y float>\n
	// ... up to a maximum of 64 points
	// static=<name>\n
	// pose=<x float>\t<y float>\t<r float>\t<s float>\n
	// width=<line width float>\n
	// colour=<r float>\t<g float>\t<b float>\n
	while ( 1 ) {
		points = 0;
		while ( fscanf_s( file, "point=%f\t%f\n", &x[points], &y[points] ) == 2 ) {
			points++;
			if ( points == 65 ) {
				fclose( file );
				return 1; // more than 64 points
			}
		}
		if ( points == 0 ) {
			break; // expected at least 1 point, we must be finished
		}
		
		id = newPath( points, x, y );
		if ( id == -1 ) {
			fclose( file );
			return 1; // failed to create path
		}

		while ( 1 ) { // look for objects
			// read the static object name
			scanned = fscanf_s( file, "static=%s\n", cBuf, 1024 );
			if ( scanned != 1 ) {
				break; // expected name, we must be done objects for this path
			}
			// read pose
			scanned = fscanf_s( file, "pose=%f\t%f\t%f\t%f\n", &poseX, &poseY, &poseR, &poseS );
			if ( scanned != 4 ) {
				fclose( file );
				return 1; // expected pose
			}
			// read line width
			scanned = fscanf_s( file, "width=%f\n", &width );
			if ( scanned != 1 ) {
				fclose( file );
				return 1; // expected line width
			}
			// read colour
			scanned = fscanf_s( file, "colour=%f\t%f\t%f\n", &colour[0], &colour[1], &colour[2] );
			if ( scanned != 3 ) {
				fclose( file );
				return 1; // expected colour
			}
			// read solid
			scanned = fscanf_s( file, "solid=%d\n", &solid );
			if ( scanned != 1 ) {
				fclose( file );
				return 1; // expected solid
			}

			obj = newStaticObject( poseX, poseY, poseR, poseS, id, colour, width, (solid != 0), cBuf );
			if ( obj == -1 ) {
				fclose( file );
				return 1; // failed to create object
			}
		}
	}

	fclose( file );
	return 0;
}

int Visualize::newStaticObject( float x, float y, float r, float s, int count, int *paths, float *colours[3], float *lineWidths, bool solid, char *name ) {
	float *ptr = (float *)malloc( sizeof(float)*4 );
	if ( ptr == NULL )
		return -1;

	ptr[0] = x;
	ptr[1] = y;
	ptr[2] = r;
	ptr[3] = s;

	return createObject( ptr, ptr+1, ptr+2, ptr+3, count, paths, colours, lineWidths, false, solid, name );
}

int Visualize::createObject( float *x, float *y, float *r, float *s, int count, int *paths, float *colours[3], float *lineWidths, bool dynamic, bool solid, char *name ) {
	int i;
	PATH_REFERENCE *path_ref;

	while ( this->nextObjectId < MAX_OBJECTS && this->objects[this->nextObjectId].path_refs != NULL )
		this->nextObjectId++;

	if ( this->nextObjectId == MAX_OBJECTS )
		return -1; // we ran out of ids!

	if ( this->nextObjectId > this->highObjectId )
		this->highObjectId = this->nextObjectId;

	if ( name != NULL )
		strcpy_s( this->objects[this->nextObjectId].name, 256, name );

	this->objects[this->nextObjectId].dynamic = dynamic;
	this->objects[this->nextObjectId].solid = solid;
	this->objects[this->nextObjectId].visible = true;

	this->objects[this->nextObjectId].x = x;
	this->objects[this->nextObjectId].y = y;
	this->objects[this->nextObjectId].r = r;
	this->objects[this->nextObjectId].s = s;

	this->objects[this->nextObjectId].path_refs = new std::list<PATH_REFERENCE *>;

	if ( this->objects[this->nextObjectId].path_refs == NULL )
		return -1;

	for ( i=0; i<count; i++ ) {
		path_ref = (PATH_REFERENCE *)malloc( sizeof(PATH_REFERENCE) );
		if ( path_ref == NULL )
			return -1;

		path_ref->id = paths[i];
		path_ref->r = colours[i][0];
		path_ref->g = colours[i][1];
		path_ref->b = colours[i][2];
		path_ref->lineWidth = lineWidths[i];
		path_ref->stipple = 0;

		this->paths[paths[i]].references++;

		this->objects[this->nextObjectId].path_refs->push_back( path_ref );
	}

	return this->nextObjectId;
}


int Visualize::deleteObject( int id, bool keepPath ) {

	if ( id < this->nextObjectId )
		this->nextObjectId = id;

	if ( id == this->highObjectId ) {
		do {
			this->highObjectId--;
		} while ( this->highObjectId > 0 && this->objects[this->highObjectId].path_refs == NULL );
	}

	if ( !this->objects[id].dynamic ) {
		free( this->objects[id].x );
	}
	
	std::list<PATH_REFERENCE *>::iterator iter = this->objects[id].path_refs->begin();
	while ( iter != this->objects[id].path_refs->end() ) {
		this->paths[(*iter)->id].references--;

		if ( !keepPath && this->paths[(*iter)->id].references == 0 )
			deletePath( (*iter)->id );

		free( *iter );

		iter++;
	}
	delete this->objects[id].path_refs;

	this->objects[id].path_refs = NULL;

	return 0;
}

int Visualize::updateStaticObject( int id, float x, float y, float r, float s ) {

	*this->objects[id].x = x;
	*this->objects[id].y = y;
	*this->objects[id].r = r;
	*this->objects[id].s = s;

	return 0;
}

int Visualize::setObjectStipple( int id, int stipple ) {

	if ( this->objects[id].path_refs == NULL ) 
		return 1;


	std::list<PATH_REFERENCE *>::iterator prIter = this->objects[id].path_refs->begin();
	while ( prIter != this->objects[id].path_refs->end() ) {
		(*prIter)->stipple = stipple;
		prIter++;
	}

	return 0;
}

int Visualize::setObjectColour( int id, float *colour ) {

	if ( this->objects[id].path_refs == NULL ) 
		return 1;

	std::list<PATH_REFERENCE *>::iterator prIter = this->objects[id].path_refs->begin();
	while ( prIter != this->objects[id].path_refs->end() ) {
		(*prIter)->r = colour[0];
		(*prIter)->g = colour[1];
		(*prIter)->b = colour[2];
		prIter++;
	}

	return 0;
}


// pass in id -1 to get the first object
int Visualize::getNextObject( OBJECT **object, int after ) {
	int id = after + 1;

	while ( id <= this->highObjectId && this->objects[id].path_refs == NULL )
		id++;

	if ( id > this->highObjectId )
		return -1; // no objects?

	*object = &this->objects[id];

	return id;
}

int Visualize::getPath( int id, PATH **path ) {
	
	if ( id > MAX_PATHS || this->paths[id].nodes == NULL )
		return 1; // bad id

	*path = &this->paths[id];

	return 0;
}


int Visualize::newfImage( float x, float y, float s, float pixel_offset, char layer, FIMAGE *fimage, float *bg, float *fg ) {
	int i, j;

	while ( this->nextfImageId < MAX_FIMAGES && this->fimages[this->nextfImageId] != NULL )
		this->nextfImageId++;

	if ( this->nextfImageId == MAX_FIMAGES )
		return -1; // we ran out of ids!

	this->fimages[this->nextfImageId] = fimage;

	this->fimageInfo[this->nextfImageId].x = x;
	this->fimageInfo[this->nextfImageId].y = y;
	this->fimageInfo[this->nextfImageId].s = s;
	this->fimageInfo[this->nextfImageId].pixel_offset = pixel_offset;
	this->fimageInfo[this->nextfImageId].layer = layer;

	if ( bg == NULL ) {
		this->fimageInfo[this->nextfImageId].bg[0] = 0;
		this->fimageInfo[this->nextfImageId].bg[1] = 0;
		this->fimageInfo[this->nextfImageId].bg[2] = 0;
	} else {
		this->fimageInfo[this->nextfImageId].bg[0] = bg[0];
		this->fimageInfo[this->nextfImageId].bg[1] = bg[1];
		this->fimageInfo[this->nextfImageId].bg[2] = bg[2];
	}

	if ( fg == NULL ) {
		this->fimageInfo[this->nextfImageId].bg_offset[0] = 1 - this->fimageInfo[this->nextfImageId].bg[0];
		this->fimageInfo[this->nextfImageId].bg_offset[1] = 1 - this->fimageInfo[this->nextfImageId].bg[1];
		this->fimageInfo[this->nextfImageId].bg_offset[2] = 1 - this->fimageInfo[this->nextfImageId].bg[2];
	} else {
		this->fimageInfo[this->nextfImageId].bg_offset[0] = fg[0] - this->fimageInfo[this->nextfImageId].bg[0];
		this->fimageInfo[this->nextfImageId].bg_offset[1] = fg[1] - this->fimageInfo[this->nextfImageId].bg[1];
		this->fimageInfo[this->nextfImageId].bg_offset[2] = fg[2] - this->fimageInfo[this->nextfImageId].bg[2];
	}

	i = 0;
	while ( this->fimageOrder[i] != -128 && layer <= this->fimageInfo[this->fimageOrder[i]].layer ) 
		i++;

	j = i;
	while ( j < MAX_FIMAGES-1 && this->fimageOrder[j] != -128 )
		j++;
	while ( j > i ) {
		this->fimageOrder[j] = this->fimageOrder[j-1];
		j--;
	}

	this->fimageOrder[i] = this->nextfImageId;

	return this->nextfImageId;
}

int Visualize::deletefImage( int id ) {
	int i;

	if ( id < this->nextfImageId )
		this->nextfImageId = id;

	i = 0;
	while ( this->fimageOrder[i] != id )
		i++;

	while ( i < MAX_FIMAGES - 1 ) {
		this->fimageOrder[i] = this->fimageOrder[i+1];
		i++;
	}
	this->fimageOrder[MAX_FIMAGES-1] = -128;

	this->fimages[id] = NULL;

	return 0;
}

int Visualize::newString( float x, float y, float s, char *str, float *colour ) {

	while ( this->nextStringId < MAX_STRINGS && this->strings[this->nextStringId].valid != false )
		this->nextStringId++;

	if ( this->nextStringId == MAX_STRINGS )
		return -1; // we ran out of ids!

	if ( this->nextStringId > this->highStringId )
		this->highStringId = this->nextStringId;

	this->strings[this->nextStringId].valid = true;
	this->strings[this->nextStringId].x = x;
	this->strings[this->nextStringId].y = y;
	this->strings[this->nextStringId].s = s;

	strcpy_s( this->strings[this->nextStringId].str, sizeof(this->strings[this->nextStringId].str), str );
	
	this->strings[this->nextStringId].colour[0] = colour[0];
	this->strings[this->nextStringId].colour[1] = colour[1];
	this->strings[this->nextStringId].colour[2] = colour[2];

	this->strings[this->nextStringId].visible = true;

	return this->nextStringId;
}

int Visualize::deleteString( int id ) {

	if ( id < this->nextStringId )
		this->nextStringId = id;

	if ( id == this->highStringId ) {
		do {
			this->highStringId--;
		} while ( this->highStringId > 0 && this->strings[this->highStringId].valid == false );
	}

	this->strings[id].valid = false;

	return 0;
}