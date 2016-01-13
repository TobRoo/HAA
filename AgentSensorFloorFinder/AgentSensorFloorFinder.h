// AgentSensorFloorFinder.h : main header file for the AgentSensorFloorFinder DLL
//

#pragma once

class RR_API;

#define REPEAT_RPFINFO_PERIOD	250 // ms
#define READING_OBSTRUCTION_THRESHOLD 0.2f // m

typedef std::map<UUID,UUID,UUIDless>	 mapPose;
typedef std::map<UUID,UUID,UUIDless>	 mapAvatar;

enum {
	WAITING_ON_POSE = 0x0001 << 0,
	WAITING_ON_MAP = 0x0001 << 1,
	WAITING_ON_PF = 0x0001 << 2,
	WAITING_ON_DATA = 0x0001 << 3,
	WAITING_ON_HIGH = 0x0001 << 4,
};

struct AVATAR_LOC {
	float x, y, r;
};

struct AVATAR_INFO {
	bool ready; // avatar info is ready
	UUID pf;
	_timeb start;
	_timeb end;
	char retired;
	float innerRadius;
	float outerRadius;
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
	int mapWidth;
	UUID mapDataRef;
	int retries;
	std::map<UUID,AVATAR_LOC,UUIDless> *avatarLocs; // location of all known avatars
	int waitingOnAvatars; // waiting for avatar locs
};

typedef std::map<int,PROCESS_READING>	mapReadings;

#define pfStateX( pfState, i ) pfState[i*3]
#define pfStateY( pfState, i ) pfState[i*3+1]
#define pfStateR( pfState, i ) pfState[i*3+2]

struct ROBOREALM_INSTANCE {
	UUID	sensor;
	RR_API *api;
	int		port;
	int		horizon;
	bool	ready;
	std::list<int> queue; // queue of readings to process
};

typedef std::map<UUID,ROBOREALM_INSTANCE,UUIDless>	 mapRoboRealm;

#define TEMPLATE_SIZE 50
#define TEMPLATE_SIZE_DIV2 (TEMPLATE_SIZE/2)

#define CAMERA_TEMPLATE_WIDTH	1	 // meters (y-axis, rows)
#define CAMERA_TEMPLATE_LENGTH	1.5f  // meters (x-axis, cols)
#define CAMERA_TEMPLATE_DIVISIONS	20	// divs over 1 meter

#define ROTATION_DIVISIONS	60 // divs over 360 degrees
#define ROTATION_RESOLUTION (2*fM_PI/ROTATION_DIVISIONS)

#define FLOORFINDER_SCALE	0.1f

#define MAP_UPDATE_STRENGTH	0.5f


class AgentSensorFloorFinder : public AgentBase {
	enum VIS_PATHS {
		VIS_PATH_LINE = 0,	// line
	};	

	enum VIS_OBJECTS {
		VIS_FIRST_HIT = 1000, // id of the first hit line
	};

	struct HIT {
		float sx, ex;
		float sy, ey;
	};


//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AvatarBase

		// -- state data --
		UUID ownerId; // owner agent
	
		int readingNextId; // next id to assign reading
	
		// map
		UUID mapId;
		float mapResolution;
	
		// visualize
		int visHitCount;
	};

protected:
	mapPose		sensorPose; // map of sensor poses by id
	mapAvatar	sensorAvatar; // map of sensor avatar (PF) by id
	mapRoboRealm sensorRR; // map of roborealm instances by id

	mapReadings readings; // queue of reading to process

	std::map<UUID,AVATAR_INFO,UUIDless> avatars; // list of active avatars

//-----------------------------------------------------------------------------
// Non-state member variables	

protected:
	// computation arrays
	int	  sharedArraySize;
	float *absoluteOd; // arrays used when processing readings
	float *obsDensity;

	// fImages
	FIMAGE *integralTarget; // temporary integral rotation buf
	FIMAGE *cameraContours; // vertical line contours
	FIMAGE *cameraT; // camera template
	FIMAGE *cameraR[ROTATION_DIVISIONS]; // camera rotated
	FIMAGE *cameraProcessed; // camera template after post processing
	FIMAGE *mapUpdate; // map update

//-----------------------------------------------------------------------------
// Functions	

public:
	AgentSensorFloorFinder( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AgentSensorFloorFinder();

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

	int nextImage( ROBOREALM_INSTANCE *rr );

	int generateCameraTemplate( CameraReading *reading, CameraPose *pose, ROBOREALM_INSTANCE *rr, char *imageData, int imageSize, FIMAGE *cameraT, FIMAGE *cameraContours, std::list<HIT> *hits );

	int initializeRoboRealm( UUID *sensor );

	int releasePort( int port );

	virtual int ddbNotification( char *data, int len );

	// Visualize
	int visualizeClear();
	int visualizeHits( float x, float y, float r, CameraPose *pose, std::list<HIT> *hits );

	
protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AgentSensorFloorFinder )

	// Enumerate callback references
	enum CallbackRef {
		AgentSensorFloorFinder_CBR_convRequestMapInfo = AgentBase_CBR_HIGH,
		AgentSensorFloorFinder_CBR_convRequestSensorInfo,
		AgentSensorFloorFinder_CBR_convRequestPFInfo,
		AgentSensorFloorFinder_CBR_cbRepeatRPFInfo,
		AgentSensorFloorFinder_CBR_convRequestAvatarLoc,
		AgentSensorFloorFinder_CBR_cbRepeatRAvatarLoc,
		AgentSensorFloorFinder_CBR_convGetAvatarList,
		AgentSensorFloorFinder_CBR_convGetAvatarInfo,
		AgentSensorFloorFinder_CBR_convRequestMap,
		AgentSensorFloorFinder_CBR_convRequestData,
		AgentSensorFloorFinder_CBR_convRequestFreePort,
		AgentSensorFloorFinder_CBR_cbQueueNextImage,
		AgentSensorFloorFinder_CBR_HIGH
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

	bool convRequestFreePort( void *vpConv );

	bool cbQueueNextImage( void *vpSensor );

protected:
	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream

	// -- Backup Notes -- 
	// Backup when:
	//  - mapId, mapResolution, sensorPose, sensorAvatar change

	// -- Recovery Notes --
	// - clear visualization
	// - for each pose in sensorPose
	//		initializeRoborealm

	virtual int	  recoveryFinish();

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );


// CAgentSensorFloorFinderDLL: Override ExitInstance for debugging memory leaks
class CAgentSensorFloorFinderDLL : public CWinApp {
public:
	CAgentSensorFloorFinderDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};