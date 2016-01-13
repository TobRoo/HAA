// AgentPathPlanner.h : main header file for the AgentPathPlanner DLL
//

#pragma once

#include "..\\autonomic\\DDB.h"

typedef std::map<int, UUID> mapTile;

struct TESTCELL {
	int x;
	int y;
	int pathLength;
	float wPathLength;
	char fromDir;

	char listed;
};

struct WAYPOINT {
	float x;
	float y;
};

#define SimPathPlanner_REPEAT_RPFINFO_PERIOD	250 // ms

#define CELLWEIGHT_AMBIENT	0.04f // WARNING! if this is set too low then updateWeightedPathLength can take a loooong time (0.04f seems to be a lower bound)
#define CELLWEIGHT(v) (CELLWEIGHT_AMBIENT + (1-(v))*(1-(v)))

#define MAX_CELL_PATH_LENGTH	30  // maximum number of cell steps to take when planning paths, things can get very slow if this is too big

#define NOPATH_GIVEUP_COUNT 3 // give up after n tries
#define NOPATH_DELAY	0.5f // seconds

#define ACTION_HORIZON 3 // send up to n actions at a time
#define ACTION_HORIZON_STUCK 4 // send extra actions if we're stuck

#define VACANT_THRESHOLD 0.55f // default vacency threshold

#define AVATAR_AVOIDANCE_THRESHOLD 0.05f 

struct ActionPair {
	int action;
	float val;
};

#define LINEAR_BOUND		0.1f // m, have to be this close on the X and Y axes
#define ROTATIONAL_BOUND	0.1f // rad, have to be this close on the R axis (if useRotation)

struct DirtyRegion {
	float x, y, w, h;
	UUID key;
};

struct AVATAR_INFO {
	bool ready; // avatar info is ready
	UUID pf;
	_timeb start;
	_timeb end;
	char retired;
	float innerRadius;
	float outerRadius;
	float x, y, r;
	bool locValid; // loc was successfully updated
};

class AgentPathPlanner : public AgentBase {
	enum VIS_PATHS {
		VIS_PATH_TARGET = 0,	// path to mark the target
		VIS_PATH_CURRENT_PATH,			// current path
	};	
	
	enum VIS_OBJECTS {
		VIS_OBJ_TARGET = 0,	// object to mark the target
		VIS_OBJ_CURRENT_PATH,		// current path
	};

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AvatarBase

		// -- state data --
		UUID ownerId;

		bool parametersSet;
		bool startDelayed; // start has been delayed because parameters are not set yet
		int initialSetup; // 0 when setup is complete

		int updateId;

		// region
		UUID regionId;
		DDBRegion missionRegion; 

		// map data
		UUID mapId;
		float mapTileSize;
		float mapResolution;
		int mapStride;
		int mapOffset[2]; // map offset in cells
		int mapWidth;
		int mapHeight;
		UUID mapDataRef;
		UUID mapProcessedRef;
		UUID mapBufRef;
		int waitingOnMap;
		UUID cellsRef;

		// particle filter
		UUID pfId;
		bool pfDirty;
		UUID pfStateRef;
		int waitingOnPF;

		// avatar config
		float maxLinear;   // maximum distance per move
		float maxRotation; // maximum distance per rotation
		float minLinear;   // minimum distance per move
		float minRotation; // minimum distance per rotation

		// target
		bool haveTarget;
		float targetX;
		float targetY;
		float targetR;
		char  useRotation;
		UUID  targetThread;

		// path
		int noPathCount; // how many times did we fail to find a path
		int visPathValid; // our path has been visualized

		// actions
		int actionsSent; // how many actions are we waiting for
		UUID actionConv; // action conversation id
		_timeb actionCompleteTime; // time that the last action was completed

		// other avatars
		int waitingOnAvatars;
	};

protected:

	mapTile tileRef; // map of cell tiles
	
	std::list<DirtyRegion> dirtyMapRegions; // list of dirty map regions
	
	std::map<UUID,AVATAR_INFO,UUIDless> avatars; // list of active avatars

//-----------------------------------------------------------------------------
// Non-state member variables	

	int DEBUGdirtyMapCount;

protected:
	
	int dir[4][2];
	int fromLookUp[4];
	
	// these only get used within a single call to planPath(), so no need to save them
	std::multimap<float,TESTCELL*>  frontier;

	UUID AgentPathPlanner_recoveryLock;

//-----------------------------------------------------------------------------
// Functions	

public:
	AgentPathPlanner( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AgentPathPlanner();

	virtual int configure();	// initial configuration
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

private:

	int configureParameters( DataStream *ds );
	int finishConfigureParameters();
	int	setTarget( float x, float y, float r, char useRotation, UUID *thread );
	int goTarget();
	int checkArrival();
	int giveUp( int reason );

	float getCell( float x, float y, bool useRound = true );
	int processMap();
	int updateWeightedPathLength( TESTCELL *cell, TESTCELL *cells, float *mapPtr );
	int nextCell( TESTCELL *testCell, float targetX, float targetY, TESTCELL *cells, std::multimap<float,TESTCELL*> *frontier, TESTCELL *bestCell, float *bestDistSq, float vacantThreshold, float *mapPtr );
	float calcWeightedPathLength( int cellX, int cellY, float startX, float startY, float endX, float endY, TESTCELL *cells, float vacantThreshold, float *mapPtr );
	int shortestPath( int *baseCellX, int *baseCellY, float *baseX, float *baseY, int destCellX, int destCellY, float destX, float destY, TESTCELL *cells, float vacantThreshold, float *mapPtr );
	int nearbyVacantCell( int startX, int startY, int *endX, int *endY, float vacantThreshold, float *mapPtr );
	int planPath();

	int updateMap( float x, float y, float w, float h, float *data );
	int dirtyMap( DDBRegion *region );
	int _dirtyMap( DirtyRegion *region, std::list<DirtyRegion>::iterator iter, std::list<DirtyRegion>::iterator );

	int notifyMap( UUID *item, char evt, char *data, int len );
	int notifyPF( UUID *item, char evt, char *data, int len );

	virtual int ddbNotification( char *data, int len );

protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AgentPathPlanner )

	// Enumerate callback references
	enum CallbackRef {
		AgentPathPlanner_CBR_convMissionRegion = AgentBase_CBR_HIGH,
		AgentPathPlanner_CBR_convMapInfo,
		AgentPathPlanner_CBR_convMapRegion,
		AgentPathPlanner_CBR_convPFInfo,
		AgentPathPlanner_CBR_convAction,
		AgentPathPlanner_CBR_convRequestAvatarLoc,
		AgentPathPlanner_CBR_convGetAvatarList,
		AgentPathPlanner_CBR_convGetAvatarInfo,
		AgentPathPlanner_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool convMissionRegion( void *vpConv );
	bool convMapInfo( void *vpConv );
	bool convMapRegion( void *vpConv );
	bool convPFInfo( void *vpConv );
	bool convAction( void *vpConv );
	bool convRequestAvatarLoc( void *vpConv );
	bool convGetAvatarList( void *vpConv );
	bool convGetAvatarInfo( void *vpConv );

protected:
	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream

	// -- Backup Notes -- 
	// Backup when:
	//  - initialSetup

	// -- Recovery Notes --
	// - if initialSetup == 3
	//		fail recovery
	//	 else if initialSetup == 2
	//		lock recovery
	//		get region info
	//		get map info
	//	 else if initialSetup == 1
	//      lock recovery
	//		initialSetup = 2
	//		get region info
	//		get map info
	//   else
	//		finishConfigureParameters();
	// - request list of avatars
	// External:
	// - owner needs to watch our status and reset our target if we fail

	virtual int	  recoveryFinish();

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );


// CAgentPathPlannerDLL: Override ExitInstance for debugging memory leaks
class CAgentPathPlannerDLL : public CWinApp {
public:
	CAgentPathPlannerDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};