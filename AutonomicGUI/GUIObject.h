// GUIObjectBase.h : main header file for the GUIObjectBase
//

#pragma once

#include "..\\autonomic\\fImage.h"
#include <map>

enum PATHLIB {
	PATHLIB_AVATAR = 0,
	PATHLIB_PARTICLE,
	PATHLIB_X,
	PATHLIB_LINE,
	PATHLIB_HIGH, // number of paths in the library
};

class AgentMirrorGUI;
class DDBStore;
class DataStream;
class Visualize;

class GUIObjectBase {
public:
	enum MENU_ITEMS {
		MI_VISIBILITY = 1,
		MI_PROPERTIES,
		MI_HIGH
	};

public:
	GUIObjectBase( UUID *id, AgentMirrorGUI *mirror );
	virtual ~GUIObjectBase();

	virtual int getParent( UUID *id );

	virtual int getName( WCHAR *buf, int len );
	virtual int getAbstract( WCHAR *buf, int len, int offset = 0 );

	int getClass() { return this->classType; };
	
	virtual int prepareRClick( CMenu *menu );
	virtual int rClick( UINT result );

	virtual int setVisibility( bool vis );
	virtual int setHighlight( bool highlight );

	bool getVisibility() { return this->visible; };

protected:
	UUID nilUUID; // comes in handy

	UUID id;
	AgentMirrorGUI *mirror;
	DDBStore *dStore;
	Visualize *visualizer;

	int classType;

	bool visible;
	bool highlighted;
	bool hasProperties; // flags whether to show the properties menu option

	int visObj;				// default vis object, don't use this variable if you want to do something fancier
	float visColour[3];     // base colour
	float visHighlight[3];  // highlight

	DataStream *ds; // shared DataStream object, for temporary, single thread, use only!

};

class GODDBAgent : public GUIObjectBase {
public:
	GODDBAgent( UUID *id, AgentMirrorGUI *mirror );
	~GODDBAgent();

	virtual int getParent( UUID *id ) { *id = this->parentId; return 0; };

	virtual int getName( WCHAR *buf, int len );
	virtual int getAbstract( WCHAR *buf, int len, int offset = 0 );

	int _VisAddPath( int id, int count, float *x, float *y );
	int _VisRemovePath( int id );
	int _VisExtendPath( int id, int count, float *x, float *y );
	int _VisUpdatePath( int id, int count, int *nodes, float *x, float *y );
	
	int _VisAddStaticObject( int id, float x, float y, float r, float s, int count, int *paths, float *colours[3], float *lineWidths, bool solid, char *name );
	int _VisRemoveObject( int id );
	int _VisUpdateObject( int id, float x, float y, float r, float s );
	int _VisSetObjectVisible( int id, char visible );

private:
	UUID parentId;

	char typeName[256];
	UUID agentTypeId;
	int  agentInstance;

	std::map<int, int> visPaths; // map of remote->local path ids
	std::map<int, int> visObjects; // map of remote->local object ids
	
};

class GODDBRegion : public GUIObjectBase {
public:
	GODDBRegion( UUID *id, AgentMirrorGUI *mirror );
	~GODDBRegion();

	virtual int getName( WCHAR *buf, int len );
	virtual int getAbstract( WCHAR *buf, int len, int offset = 0 );

private:
	float x, y, w, h;
};

class GODDBLandmark : public GUIObjectBase {
public:
	GODDBLandmark( UUID *id, AgentMirrorGUI *mirror, int *pathLib );
	~GODDBLandmark();

	virtual int getParent( UUID *id );
	virtual int getName( WCHAR *buf, int len );
	virtual int getAbstract( WCHAR *buf, int len, int offset = 0 );

	virtual int setVisibility( bool vis );

	int updatePosEstimation();
	int collected();

	DDBLandmark * getLandmark() { return &this->landmark; };

private:
	DDBLandmark landmark;

	int *pathLib; // list of common paths

	int visTrue; // true position (if initially set)
	int visCov[2]; // covariance bars
};

class GODDBMapPOG : public GUIObjectBase {
public:
	enum MENU_ITEMS_GODDBMapPOG {
		MI_POG = GUIObjectBase::MI_HIGH,
		MI_HIGH
	};

public:
	GODDBMapPOG( UUID *id, AgentMirrorGUI *mirror );
	~GODDBMapPOG();

	virtual int getName( WCHAR *buf, int len );
	virtual int getAbstract( WCHAR *buf, int len, int offset = 0 );

	virtual int prepareRClick( CMenu *menu );
	virtual int rClick( UINT result );

	virtual int setVisibility( bool vis );

	int updateRegion( float x, float y, float w, float h );

private:
	float tileSize;
	float resolution;
	int stride;

	std::map<int,FIMAGE *> tiles;
	std::map<int,int> tilesVis;

	int visualizeTile( int id, FIMAGE *img );
	int hideTiles();
};

struct GODDBAvatar_LandmarkInfo {
	float x, y;
	float ox, oy;
	float s;
	int visObj;
};

class GODDBAvatar : public GUIObjectBase {
public:
	GODDBAvatar( UUID *id, AgentMirrorGUI *mirror, int *pathLib );
	~GODDBAvatar();

	virtual int getName( WCHAR *buf, int len );
	virtual int getAbstract( WCHAR *buf, int len, int offset = 0 );

	virtual int setVisibility( bool vis );

	int addLandmark( UUID *id, float ox, float oy );

private:
	DDBAvatar info;
	float x, y, r; // estimated pose

	int *pathLib; // list of common paths

	std::map<UUID,GODDBAvatar_LandmarkInfo,UUIDless> landmarks; // map of landmark vis objects by id

public:
	DECLARE_CALLBACK_CLASS( GODDBAvatar )

	bool cbDDBWatcher( void *vpds );
};

class GODDBParticleFilter : public GUIObjectBase {
public:
	GODDBParticleFilter( UUID *id, AgentMirrorGUI *mirror, int *pathLib );
	~GODDBParticleFilter();

	virtual int getParent( UUID *id );

	virtual int getName( WCHAR *buf, int len );
	virtual int getAbstract( WCHAR *buf, int len, int offset = 0 );

	virtual int setVisibility( bool vis );

	int updateFilter( int evt, DataStream *ds );

protected:
	int numParticles;
	int stateSize;
	float *state;
	float *weight;

	float effectiveParticleNum;

	int *pathLib; // list of common paths
	int *pObj;
	float *visScale;

};

class GODDBSensor : public GUIObjectBase {
public:
	GODDBSensor( UUID *id, AgentMirrorGUI *mirror );
	~GODDBSensor();

	virtual int getParent( UUID *id ) { *id = this->info.avatar; return 0; };

	virtual int getName( WCHAR *buf, int len );
	virtual int getAbstract( WCHAR *buf, int len, int offset = 0 );

private:
	DDBSensor info;
};