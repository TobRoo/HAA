// ExecutiveOfflineSLAM.h : main header file for the ExecutiveOfflineSLAM DLL
//

#pragma once

#include <list>

#define MAX_PATHS	256
#define MAX_OBJECTS	256

enum SLAMmode {
	SM_IDEAL = 0,
	SM_DISCARD,
	SM_JCSLAM,
	SM_JCSLAM_FIFO,
	SM_JCSLAM_RANDOM,
	SM_JCSLAM_NOFORWARDPROP,
};



struct READING_TYPE {
	int sensor;
	int phase;
};

struct READING_QUEUE {
	READING_TYPE type;
	UUID uuid;
	_timeb tb;
	int attempt;
};

struct PRE_READING_QUEUE {
	UUID agent;
	READING_QUEUE rq;
};

#ifndef READING_TYPElessDefined
// READING_TYPEless comparison function
class READING_TYPEless {
public:
	bool operator()(const READING_TYPE& _Left, const READING_TYPE& _Right) const
		{	// apply operator< to operands
			return memcmp( &_Left, &_Right, sizeof(READING_TYPE) ) < 0; 
		}
};
#define READING_TYPElessDefined
#endif

typedef std::map<READING_TYPE,std::list<READING_QUEUE>,READING_TYPEless> mapTypeQueue; 
typedef std::map<UUID,std::list<READING_QUEUE>,UUIDless> mapAgentQueue;

typedef std::map<READING_TYPE,std::list<UUID>,READING_TYPEless>	mapTypeAgents;
typedef std::map<READING_TYPE,int,READING_TYPEless>				mapAgentSpawning;



struct OS_READING_INFO {
	unsigned int ind; // reading index
	_timeb readingT; // reading time
	_timeb processT; // processed time
	UUID sensorId;
	READING_TYPE  sensorType;
};

struct OS_EVENT {
	char name[256];			// event name for logging
	char type;				// event type (0-DDB msg, 1-PF update)
	char expectAck;			// expect acknowledgement before the next event
	unsigned _int64 time;	// time in milliseconds
	char msg;				// msg for DDB msg events
	void *data;				
	int dataLen;			
	bool isReading;			// event contains additional info regarding the reading
	OS_READING_INFO readingInfo;
};

struct OS_AVATAR {
	float *pfState;
};

class ExecutiveOfflineSLAM : public AgentBase {
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

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AvatarBase

		// -- state data --
	};

protected:


//-----------------------------------------------------------------------------
// Non-state member variables	

protected:

	bool missionDone;

	UUID pogUUID;

	int nextPathId;
	PATH paths[MAX_PATHS];

	int nextObjectId;
	int highObjectId;
	OBJECT objects[MAX_OBJECTS];

	unsigned _int64	   simStartTime;	// simulation start time
	unsigned _int64    simTime;			// current simulation time
	unsigned _int64    readingTimeHigh; // highest time of reading currently assigned for processing
	std::list<OS_EVENT>	eventQueue; // list of all events

	std::map<UUID,OS_AVATAR,UUIDless> avatars; // map of avatar index by pfId
	std::map<UUID,UUID,UUIDless>	pfByAvatar; // map of pf ids by avatar id
	
	unsigned int readingInd; // highest reading index for debugging
	std::map<UUID,std::list<OS_READING_INFO>,UUIDless> readingById; // map of unprocessed readings by sensor
	std::list<OS_READING_INFO> readingByProcessOrder; // list of readings in the order they were processed
	int readingsProcessedByType[512]; // count of processed readings by sensor type
	int readingsCountByType[512]; // count of known readings by sensor type

	int SLAMmode;
	int particleNum;
	float readingProcessingRate;

	int processingSlots;	 // available processing slots
	std::list<unsigned _int64> processingSlotFinished; // list of times at which a processing slot becomes available

	int waitingForAgents; // waiting for agens to spawn

	int waitingForProcessing; // waiting while readings are processed
	int waitingForAck; // waiting for ack from a message we sent the DDB

	mapTypeAgents agents; // map of agents by sensor type
	mapAgentSpawning agentsSpawning; // list of currently spawning agents
	std::map<UUID,int,UUIDless> agentsStatus; // map of status by agent id
	std::map<UUID,bool,UUIDless> agentsProcessing; // currently processing, by agent id

	mapTypeQueue typeQueue; // queues of readings by sensor type
	mapAgentQueue agentQueue; // queues of readings by agent

//-----------------------------------------------------------------------------
// Functions	

public:
	ExecutiveOfflineSLAM( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~ExecutiveOfflineSLAM();

	virtual int configure();	// initial configuration

	virtual int   parseMF_HandleMissionRegion( DDBRegion *region );
	virtual int   parseMF_HandleForbiddenRegion( DDBRegion *region );
	virtual int   parseMF_HandleCollectionRegion( DDBRegion *region );
	virtual int   parseMF_HandleLandmarkFile( char *fileName );
	virtual int	  parseMF_HandlePathFile( char *fileName );
	virtual int	  parseMF_HandleOfflineSLAM( int SLAMmode, int particleNum, float readingProcessingRate, int processingSlots, char *logPath );

	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step


private:

	int insertEvent( char *name, char type, char expectAck, unsigned _int64 time, void *data, int dataLen, unsigned char msg = -1, bool isReading = false, OS_READING_INFO *readingInfo = NULL );
	
	int readDataBlock( FILE *file, void **data, int *dataLen );
	int parseAvatarLog( char *filename, char *fullname );

	int parseLandmarkFile( char *filename );

	int newPath( int count, float *x, float *y );
	int loadPathFile( char *fileN );		// load path

	int newStaticObject( float x, float y, float r, float s, int path, float colour[3], float lineWidth, bool solid = false, char *name = NULL )
		{ return newStaticObject( x, y, r, s, 1, &path, &colour, &lineWidth, solid, name ); };
	int newStaticObject( float x, float y, float r, float s, int count, int *paths, float *colours[3], float *lineWidths, bool solid = false, char *name = NULL );
	int createObject( float *x, float *y, float *r, float *s, int count, int *paths, float *colours[3], float *lineWidths, bool dynamic, bool solid, char *name );
	
	int initializePF( UUID pfId, int particleNum, float x, float y, float r, float *sigma );
	int _resampleParticleFilter( UUID *id, int pNum, float *weights );

	int	   getProcessingPhases( int sensor );
	int	   requestAgentSpawn( READING_TYPE *type, char priority );
	UUID * getProcessingAgent( READING_TYPE *type );
	
	std::list<std::list<PRE_READING_QUEUE>> preReadingQueue;
	int preAssignSensorReading( UUID *agent, READING_QUEUE *rq ); // used in SM_DISCARD to evenly distribute assigned readings to sonar doesn't get an unfair advantage
	
	int nextSensorReading( UUID *agent );
	int assignSensorReading( UUID *agent, READING_QUEUE *rq );
	int doneSensorReading( UUID *uuid, UUID *sensor, _timeb *tb, char success );
	
	int readingAssigned( READING_QUEUE *rq );

	int newSensorReading( READING_TYPE *type, UUID *uuid, _timeb *tb, int attempt = 0 );

	virtual int ddbNotification( char *data, int len );

protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( ExecutiveOfflineSLAM )

	// Enumerate callback references
	enum CallbackRef {
		ExecutiveOfflineSLAM_CBR_convRequestAgentSpawn= AgentBase_CBR_HIGH,
		ExecutiveOfflineSLAM_CBR_convAgentInfo,
		ExecutiveOfflineSLAM_CBR_convRequestPFInfo,
		ExecutiveOfflineSLAM_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool	convRequestAgentSpawn( void *vpConv );
	bool	convAgentInfo( void *vpConv );
	bool	convRequestPFInfo( void *vpConv );
	
protected:
	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream

	// -- Backup Notes -- 
	// no crash

	virtual int	  recoveryFinish();

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );

// CExecutiveOfflineSLAMDLL: Override ExitInstance for debugging memory leaks
class CExecutiveOfflineSLAMDLL : public CWinApp {
public:
	CExecutiveOfflineSLAMDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};