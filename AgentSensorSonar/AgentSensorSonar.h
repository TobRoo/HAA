// AgentSensorSonar.h : main header file for the AgentSensorSonar DLL
//

#pragma once

#define REPEAT_RPFINFO_PERIOD	250 // ms
#define READING_OBSTRUCTION_THRESHOLD 0.2f // m

typedef std::map<UUID,UUID,UUIDless>	 mapPose;
typedef std::map<UUID,UUID,UUIDless>	 mapAvatar;

enum {
	WAITING_ON_POSE = 0x0001 << 0,
	WAITING_ON_MAP = 0x0001 << 1,
	WAITING_ON_PF = 0x0001 << 2,
	WAITING_ON_DATA = 0x0001 << 3,
};

struct AVATAR_LOC {
	float x, y, r;
};

struct PROCESS_READING {
	int id;
	char waiting;
	UUID sensor;
	_timeb time;
	int readingSize;
	UUID readingRef;
	int dataSize;
	UUID dataRef;
	int pfNum; // number of particles
	int pfStateSize; // state size
	UUID pfStateRef;
	UUID pfWeightRef;
	float mapUpdateLoc[2]; // location of the mapUpdate origin
	int   mapUpdateSize[2]; // width,height (cols,rows)
	int mapHeight;
	UUID mapDataRef;
	int retries;
	std::map<UUID,AVATAR_LOC,UUIDless> *avatarLocs; // location of all known avatars
	int waitingOnAvatars; // waiting for avatar locs
};

typedef std::map<int,PROCESS_READING>	mapReadings;

#define pfStateX( pfState, i ) pfState[i*3]
#define pfStateY( pfState, i ) pfState[i*3+1]
#define pfStateR( pfState, i ) pfState[i*3+2]

#define ROTATION_DIVISIONS	60 // divs over 360 degrees
#define ROTATION_RESOLUTION (2*fM_PI/ROTATION_DIVISIONS)

#define TEMPLATE_SIZE 30
#define TEMPLATE_SIZE_DIV2 (TEMPLATE_SIZE/2)

#define MAP_UPDATE_STRENGTH	0.12f
#define MAP_UPDATE_RATIO	0.5f // absolute:density

#define OBS_DENSITY_RATIO	0.3f  // absolute:density
#define OBS_DENSITY_AMBIENT 500.8f

struct AVATAR_INFO {
	bool ready; // avatar info is ready
	UUID pf;
	_timeb start;
	_timeb end;
	char retired;
	float innerRadius;
	float outerRadius;
};

class AgentSensorSonar : public AgentBase {

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AvatarBase

		// -- state data --
		UUID ownerId; // owner agent
		int readingNextId; // next id to assign reading
	
		UUID mapId;
		float mapResolution;
	};

protected:
	mapPose		sensorPose; // map of sensor poses by id
	mapAvatar	sensorAvatar; // map of sensor avatar (PF) by id

	mapReadings readings; // queue of reading to process

	std::map<UUID,AVATAR_INFO,UUIDless> avatars; // list of active avatars

//-----------------------------------------------------------------------------
// Non-state member variables	

protected:
	
	// computation arrays
	int	  sharedArraySize;
	float *absoluteOd; // arrays used when processing readings
	float *densityOd;
	float *obsDensity;

	// fImages
	FIMAGE *densityT;  // density template
	FIMAGE *absoluteT; // absolute template
	FIMAGE *integralTarget; // temporary integral rotation buf
	FIMAGE *densityR[ROTATION_DIVISIONS];  // density rotated
	FIMAGE *absoluteR[ROTATION_DIVISIONS]; // absolute rotated
	FIMAGE *mapUpdate; // map update

//-----------------------------------------------------------------------------
// Functions	

public:
	AgentSensorSonar( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AgentSensorSonar();

	virtual int configure();	// initial configuration
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

private:

	int configureParameters( DataStream *ds );

	int addPose( UUID *sensor, UUID *poseRef );

	int addReading( UUID *sensor, _timeb *time );
	int finishReading( PROCESS_READING *pr, char success );
	int requestMapRegion( PROCESS_READING *pr );
	int processReading( PROCESS_READING *pr );

	float templateScale( float d, float max, float sig );
	int generateConicTemplates( float d, float max, float sig, float a, float b, FIMAGE *densityT, FIMAGE *absoluteT );

	virtual int ddbNotification( char *data, int len );

protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AgentSensorSonar )

	// Enumerate callback references
	enum CallbackRef {
		AgentSensorSonar_CBR_convRequestMapInfo = AgentBase_CBR_HIGH,
		AgentSensorSonar_CBR_convRequestSensorInfo,
		AgentSensorSonar_CBR_convRequestPFInfo,
		AgentSensorSonar_CBR_cbRepeatRPFInfo,
		AgentSensorSonar_CBR_convRequestAvatarLoc,
		AgentSensorSonar_CBR_cbRepeatRAvatarLoc,
		AgentSensorSonar_CBR_convGetAvatarList,
		AgentSensorSonar_CBR_convGetAvatarInfo,
		AgentSensorSonar_CBR_convRequestMap,
		AgentSensorSonar_CBR_convRequestData,
		AgentSensorSonar_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool convRequestMapInfo( void *vpConv );
	bool convRequestSensorInfo( void *vpConv );
	bool convRequestPFInfo( void *vpConv );
	bool cbRepeatRPFInfo( void *vpid );
	bool convRequestAvatarLoc( void *vpConv );
	bool cbRepeatRAvatarLoc( void *vpid );
	bool convGetAvatarList( void *vpConv );
	bool convGetAvatarInfo( void *vpConv );
	bool convRequestMap( void *vpConv );
	bool convRequestData( void *vpConv );

protected:
	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream
	
	// -- Backup Notes -- 
	// Backup when:
	//  - mapId, mapResolution, sensorPose, sensorAvatar change

	// -- Recovery Notes --
	// - if started
	//		if mapResolution == 0
	//			get map resolution

	virtual int	  recoveryFinish();

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );


// CAgentSensorSonarDLL: Override ExitInstance for debugging memory leaks
class CAgentSensorSonarDLL : public CWinApp {
public:
	CAgentSensorSonarDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};