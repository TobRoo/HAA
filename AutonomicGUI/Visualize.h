
#pragma once
#include "afxwin.h"

#include <gl/gl.h>
#include <gl/glu.h>

#include <list>

#define MAX_PATHS	2048
#define MAX_OBJECTS	15000

#define MAX_FIMAGES	32 // keep this relatively small so that adjusting layer order is fast

#define MAX_STRINGS 1024

#define MAX_CONSOLE_LINES	10

class COpenGLControl;
struct FIMAGE;

class Visualize {
public:
	Visualize();
	~Visualize();

public:
	struct NODE {
		float x, y;
	};

	struct PATH {
		int references;
		std::list<NODE *> *nodes;
	};

	struct PATH_REFERENCE {
		int id;
		float r, g, b;
		float lineWidth;
		int stipple;
	};

	struct OBJECT {
		char name[256];
		bool dynamic;
		bool solid; // is the object solid for simulation purposes
		bool visible; // is the object visible
		float *x, *y, *r, *s;
		std::list<PATH_REFERENCE *> *path_refs;
	};

	struct FIMAGE_INFO {
		float x, y, s, pixel_offset;
		char layer; // layers 127-0 draw behind objects, -1--127 draw in front, layer -128 is reserved
		float bg[3]; // bg colour
		float bg_offset[3]; // offset from bg colour to fg colour
	};

	struct STRING {
		bool valid; // valid string
		float x, y, s;
		char str[256];
		float colour[3];
		bool visible;
	};

private:

	COpenGLControl *oglControl;

	int nextPathId;
	PATH paths[MAX_PATHS];

	int nextObjectId;
	int highObjectId;
	OBJECT objects[MAX_OBJECTS];

	int nextfImageId;
	FIMAGE *fimages[MAX_FIMAGES];
	FIMAGE_INFO fimageInfo[MAX_FIMAGES];
	int fimageOrder[MAX_FIMAGES];

	int nextStringId;
	int highStringId;
	STRING strings[MAX_STRINGS];

	bool consoleVisible;
	float defaultStatusColor[3];
	float defaultConsoleColor[3];
	int  statusLine;
	char statusMsgs[MAX_CONSOLE_LINES][512];
	float statusColor[MAX_CONSOLE_LINES][3];
	int  consoleLine;
	char consoleMsgs[MAX_CONSOLE_LINES][512];
	float consoleColor[MAX_CONSOLE_LINES][3];

public:

	int Initialize( COpenGLControl *oglC );

	int ClearAll();

	int PreDraw();
	int Draw();

	int statusClear();
	int statusPrintLn( float *color, const char *fmt, ... );
	int consoleClear();
	int consolePrintLn( float *color, const char *fmt, ... );

	int showConsole() { consoleVisible = true; return 0; };
	int hideConsole() { consoleVisible = true; return 0; };
	int toggleConsole() { return consoleVisible = !consoleVisible; };

	int drawImage( FIMAGE *img, FIMAGE_INFO *img_info );

	int newPath( int count, float *x, float *y );
	int deletePath( int id );
	int extendPath( int id, float x, float y ) { return extendPath( id, 1, &x, &y ); };
	int extendPath( int id, int count, float *x, float *y );
	int updatePath( int id, int node, float x, float y ) { return updatePath( id, 1, &node, &x, &y ); };
	int updatePath( int id, int count, int *nodes, float *x, float *y );
	int loadPathFile( char *fileN );


	int newStaticObject( float x, float y, float r, float s, int path, float colour[3], float lineWidth, bool solid = false, char *name = NULL )
		{ return newStaticObject( x, y, r, s, 1, &path, &colour, &lineWidth, solid, name ); };
	int newStaticObject( float x, float y, float r, float s, int count, int *paths, float *colours[3], float *lineWidths, bool solid = false, char *name = NULL );
	int newDynamicObject( float *x, float *y, float *r, int path, float colour[3], float lineWidth, bool solid = false, char *name = NULL )
		{ return newDynamicObject( x, y, r, NULL, 1, &path, &colour, &lineWidth, solid, name ); };
	int newDynamicObject( float *x, float *y, float *r, float *s, int path, float colour[3], float lineWidth, bool solid = false, char *name = NULL )
		{ return newDynamicObject( x, y, r, s, 1, &path, &colour, &lineWidth, solid, name ); };
	int newDynamicObject( float *x, float *y, float *r, int count, int *paths, float *colours[3], float *lineWidths, bool solid = false, char *name = NULL )
		{ return createObject( x, y, r, NULL, count, paths, colours, lineWidths, true, solid, name ); };
	int newDynamicObject( float *x, float *y, float *r, float *s, int count, int *paths, float *colours[3], float *lineWidths, bool solid = false, char *name = NULL )
		{ return createObject( x, y, r, s, count, paths, colours, lineWidths, true, solid, name ); };
	int deleteObject( int id, bool keepPath = false );
	int updateStaticObject( int id, float x, float y, float r, float s );

	int setObjectStipple( int id, int stipple );
	int setObjectColour( int id, float *colour );

	int showObject( int id ) { return id < MAX_OBJECTS ? !(this->objects[id].visible = true) : 1; };
	int hideObject( int id ) { return id < MAX_OBJECTS ? (this->objects[id].visible = false) : 1; };
	int toggleObject( int id ) { return id < MAX_OBJECTS ? (this->objects[id].visible = !this->objects[id].visible) : 0; };

	// these pointers should probably be used only for read only purposes!
	int getNextObject( OBJECT **object, int after = -1 );
	int getPath( int id, PATH **path );

	int newfImage( float x, float y, float s, float pixel_offset, char layer, FIMAGE *fimage, float *bg = NULL, float *fg = NULL );
	int deletefImage( int id );

	int newString( float x, float y, float s, char *str, float *colour );
	int deleteString( int id );

private:
	int createObject( float *x, float *y, float *r, float *s, int count, int *paths, float *colours[3], float *lineWidths, bool dynamic, bool solid, char *name );

};