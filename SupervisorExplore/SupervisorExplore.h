// SupervisorExplore.h : main header file for the SupervisorExplore DLL
//

#pragma once

#include <list>


typedef std::map<UUID,DDBRegion,UUIDless>	 mapRegions;

struct CELLCOORD {
	float x = 0.0;
	float y = 0.0;

};

#ifndef CELLCOORDlessDefined
// CELLCOORDless comparison function
class CELLCOORDless {
public:
	bool operator()(const CELLCOORD& _Left, const CELLCOORD& _Right) const
		{	// apply operator< to operands
			return memcmp( &_Left, &_Right, sizeof(CELLCOORD) ) > 0;
		}
};
#define CELLCOORDlessDefined
#endif

struct CELL {
	float x, y;
	char type;
	float occupancy;
	float activity;
	unsigned char section; // current section id
	unsigned char partition;  // current partition id
	bool dirty;
	UUID flooding; // id of the current flood
	CELL *neighbour[4]; // up, right, left, down
};

enum CELL_TYPE {
	CT_OUTOFBOUNDS = -3,
	CT_UNREACHABLE = -2,
	CT_UNKNOWN = -1,
	CT_UNOCCUPIED = 0,
	CT_OCCUPIED
};

enum CELL_EXIT {
	CE_UP =		0x01 << 0,
	CE_DOWN =	0x01 << 1,
	CE_LEFT =	0x01 << 2,
	CE_RIGHT =	0x01 << 3
};

typedef std::map<int, UUID> mapTile;

struct AVATAR_INFO {
	UUID owner; // id of the avatar agent
	UUID pfId;
	UUID controller;
	int controllerIndex;
};

struct AVATAR {
	UUID avatarId; // id of the avatar
	bool pfKnown; // do we know the pfId?
	bool pfValid; // have we received the state at least once?
	UUID pfId; 
	float pfState[3];
	unsigned char section; // section id
	unsigned char partition; // partition id
	bool searchDirty; // search cell needs updating
	CELLCOORD search; // current search cell
	std::list<CELLCOORD> *cells; // currently assigned cells
};

typedef std::map<UUID, AVATAR, UUIDless> mapAvatar;

struct GETAVATARPOS_CONVDATA {
	UUID updateId;
	UUID avatarId;
};

/*
struct TARGET_POINT {
	float x, y, r;
};
*/
//typedef std::map<UUID,AgentType,UUIDless> mapAgentType;
//typedef std::map<UUID,std::list<TARGET_POINT> *,UUIDless> mapAvatarPoints;

#define MAX_SECTIONS 255 // because we store section and avatar ids in chars

#define THRESHHOLD_UNOCCUPIED	0.73f
#define THRESHHOLD_OCCUPIED		0.27f
#define THRESHHOLD_ACTIVITY		5.0f

#define SEARCH_THRESHOLD		0.05f // percent of accessible unknown cells

#define SupervisorExplore_AVATAR_POS_RETRY  100		// ms

#define SupervisorExplore_UPDATE_RATE		4000	// ms
#define SupervisorExplore_UPDATE_RATE_MIN	1000	// ms

#define SupervisorExplore_MAX_SIMULTANEOUS_UPDATES	255 

struct RegionRequest {
	UUID id;
	bool forbidden;
};

struct DirtyRegion {
	float x, y, w, h;
	UUID key;
};

class SupervisorExplore : public AgentBase {
	enum VIS_PATHS {
		VIS_PATH_CELL = 0,	// path to mark cells of interest
	};	

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AvatarBase

		// -- state data --
		UUID avatarExecId;

		int totalCells; // total cell count for all regions NOTE mission regions should not overlap and forbidden regions should be entirely within a mission region
	
		int nextSectionId;
		bool sectionFree[MAX_SECTIONS]; // section is not currently being used
		bool sectionDirty[MAX_SECTIONS]; //  section probably needs to be repartitioned
	
		bool mapReady;
		UUID mapId;
		float mapTileSize;
		float mapResolution;
		int mapStride;

		int partitionUpdateInProgress; // counts the partition updates in progress
		_timeb lastPartitionUpdate; // records last update time
		UUID partitionUpdateTimer; // id for the partition update timer
		UUID puId; // update id
		bool puWaiting; // waiting, indexed by update id
	};

protected:
	mapRegions regions;  // list of regions to explore
	mapRegions forbiddenRegions; // list of forbidden regions
	mapTile tileRef; // map of cell tiles
	
	std::map<UUID,int,UUIDless> puWaitingOnMap;  // counts how many map updates we're waiting for, indexed by update id
	std::map<UUID,int,UUIDless> puWaitingOnPF;   // counts how many PF updates we're waiting for, indexed by update id

	std::map<CELLCOORD,CELL*,CELLCOORDless> sections[MAX_SECTIONS]; // list of cells by section
	std::list<int> *sectionIds; // list of current section ids

	std::list<DirtyRegion> dirtyMapRegions; // list of dirty map regions
	
	std::map<UUID,AVATAR_INFO,UUIDless> avatarInfo; // map of known avatars by avatar id
	mapAvatar	avatars; // list of assigned avatars
	std::map<UUID,int,UUIDless> agentStatus; // map of status by agent id

	// visualize
	std::list<int> visObjs;

//-----------------------------------------------------------------------------
// Non-state member variables	

protected:
	int	   sharedArrayGaussSize; // shared arrays for partitioning sections
	double *sharedArrayGauss;
	int	   sharedArrayPixSize;
	double *sharedArrayPix;

//-----------------------------------------------------------------------------
// Functions	

public:
	SupervisorExplore( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~SupervisorExplore();

	virtual int configure();	// initial configuration
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

private:

	int addRegion( UUID *id, bool forbidden );
	int _addRegion( UUID *id, DDBRegion *region, bool forbidden );
	int initializeRegion( UUID *id, bool forbidden );
	int resetCellNeighbours();

	CELL * getCell( float x, float y, bool useRound = true );
	int getNewSectionId(); // get new section id
	int floodSection( CELL *seed, std::list<CELL*> *unlisted );
	int _floodSectionRecurse( std::list<CELL*> *flood, UUID *floodId, int id, std::map<CELLCOORD,CELL*,CELLCOORDless> *section, std::list<CELL*> *unlisted );
	int checkCellNeighbours( float x, float y );

	int addMap( UUID *id );
	int updateMap( float x, float y, float w, float h, float *data );

	int nextSearchCell( UUID *uuid );

	int avatarUpdateController( UUID *avatarId, UUID *controller, int index ); // update controller
	int addAvatar( UUID *agentId, UUID *avatarId );	// avatar allocated
	int remAvatar( UUID *agentId ); // avatar allocation revoked
	int avatarUpdateSection( UUID *id, float x, float y ); // update avatar section with new pos

	int updatePartitions( bool force = 0 ); // update sections and partitions
	int _updatePartitions(); // data is gathered, do the update
	int calculatePartitions( unsigned char sectionId ); // divide sections into partitions

	int notifyMap( UUID *item, char evt, char *data, int len );
	int dirtyRegion( DDBRegion *region );
	int dirtyMap( DDBRegion *region );
	int _dirtyMap( DirtyRegion *region, std::list<DirtyRegion>::iterator iter, std::list<DirtyRegion>::iterator );

	virtual int ddbNotification( char *data, int len );

	int visualizeClear();
	int visualizeSection( std::map<CELLCOORD,CELL*,CELLCOORDless> *section );

protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( SupervisorExplore )

	// Enumerate callback references
	enum CallbackRef {
		SupervisorExplore_CBR_cbPartitionUpdateTimeout = AgentBase_CBR_HIGH,
		SupervisorExplore_CBR_convGetRegion,
		SupervisorExplore_CBR_convGetMapInfo,
		SupervisorExplore_CBR_convGetMapData,
		SupervisorExplore_CBR_convGetAvatarList,
		SupervisorExplore_CBR_convGetAvatarInfo,
		SupervisorExplore_CBR_convGetAvatarPos,
		SupervisorExplore_CBR_convReachedPoint,
		SupervisorExplore_CBR_convAgentInfo,
		SupervisorExplore_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool cbPartitionUpdateTimeout( void *NA );

	bool convGetRegion( void *vpConv );
	bool convGetMapInfo( void *vpConv );
	bool convGetMapData( void *vpConv );
	bool convGetAvatarList( void *vpConv );
	bool convGetAvatarInfo( void *vpConv );
	bool convGetAvatarPos( void *vpConv );
	bool convReachedPoint( void *vpConv );
	bool convAgentInfo( void *vpConv );

protected:
	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream

	// -- Backup Notes -- 
	// Backup when:
	//  - avatarExecId, mapReady, mapId, regions changes

	// -- Recovery Notes --
	// - clear visualization
	// - if mapId set
	//		if not mapReady
	//			request map info
	// - for region in regions
	//		redo _addRegion 
	// - get avatar list

	virtual int	  recoveryFinish();

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );


// CSupervisorExploreDLL: Override ExitInstance for debugging memory leaks
class CSupervisorExploreDLL : public CWinApp {
public:
	CSupervisorExploreDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};