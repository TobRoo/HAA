// AgentSensorLandmark.h : main header file for the AgentSensorLandmark DLL
//

#pragma once

#define REPEAT_RPFINFO_PERIOD	250 // ms

#define MAX_LANDMARKS	64 // max number of landmarks to process in a single image

#define OBS_DENSITY_AMBIENT 1.0f
#define OBS_DENSITY_AMBIENT_OTHER 60.0f  // controls how much we want to make adjustments to other particle filters

#define OBS_SIGMA_X			0.3f // ~1/2 expected error in m
#define OBS_SIGMA_Y			0.15f // ~1/2 expected error in m at a distance of 1 m

#define DEBUG_IMG_DUMP  1 // dump images for debugging

struct HIT_LOC {
	int r, c;
	float w;
};


#define PATTERN_SPACING	4
#define PATTERN_WINDOW	30
#define PATTERN_P		2/3.0f
#define PATTERN_THRESHOLD	0.2f
#define PATTERN_SHAPE	1.3f

#define RECOGNITION_OFFSET	1.0f
#define BARCODE_OFFSET		2

enum {
	WAITING_ON_POSE = 0x0001 << 0,
	WAITING_ON_MAP = 0x0001 << 1,
	WAITING_ON_PF_ID = 0x0001 << 2,
	WAITING_ON_PF = 0x0001 << 3,
	WAITING_ON_DATA = 0x0001 << 4,
	WAITING_ON_HIGH = 0x0001 << 5,
};



struct LANDMARK_HIT {
	unsigned char id;
	
	float r1, c1;
	float r2, c2;
	char waiting;
	float u[3], v[3];
	float px, py; // camera frame

	bool simStream; // flags landmarks what were parsed from simStreams

	DDBLandmark info;
	int pfNum;
	int pfStateSize;
	UUID pfStateRef;
	UUID pfWeightRef;
};

class AgentSensorLandmark : public AgentBase {
	
	typedef std::map<UUID,CameraPose,UUIDless>	 mapPose;
	typedef std::map<UUID,UUID,UUIDless>		 mapAvatar;
	typedef std::map<char,DDBLandmark>			 mapLandmark;
	
	struct PROCESS_READING {
		int id;
		char waiting;
		UUID sensor;
		UUID avatar;
		_timeb time;
		int readingSize;
		UUID readingRef;
		int dataSize;
		UUID dataRef;
		int pfNum; // number of particles
		int pfStateSize; // state size
		UUID pfStateRef;
		UUID pfWeightRef;
		int retries;
		int waitingForPF; // waiting on this many pfs (us + landmarks)
		std::list<LANDMARK_HIT> *landmarks;
	};

	typedef std::map<int,PROCESS_READING>	mapReadings;

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AvatarBase

		// -- state data --
		UUID ownerId; // owner agent

		int nextReadingId; // next id to assign reading
	
	};

protected:
	mapPose		sensorPose; // map of sensor poses by id
	mapAvatar	sensorAvatar; // map of sensor avatar (PF) by id
	mapLandmark landmark; // map of landmark by code
	std::map<UUID,UUID,UUIDless> avatarPF; // map of avatar pf ids by avatar id

	mapReadings readings; // queue of reading to process

//-----------------------------------------------------------------------------
// Non-state member variables	

protected:
	// computation arrays
	int	  obsDensitySize;
	int	  obsDensitySize2;
	float *obsDensity;
	float *obsDensity2;

//-----------------------------------------------------------------------------
// Functions	

public:
	AgentSensorLandmark( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AgentSensorLandmark();

	virtual int configure();	// initial configuration
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

private:

	int configureParameters( DataStream *ds );

	int addPose( UUID *sensor, CameraPose *pose );

	int addReading( UUID *sensor, _timeb *time );

	int readBarcodes( std::list<LANDMARK_HIT> *landmarks, unsigned char *pixels, int width, int height );

	int processReading( PROCESS_READING *pr );
	int finishReading( PROCESS_READING *pr, char success );
	int deleteReading( PROCESS_READING *pr );
	int processLandmark( PROCESS_READING *pr, std::list<LANDMARK_HIT>::iterator landmark );

	int getLandmarkInfo( int readingId, unsigned char landmarkId );
	int getLandmarkPFId( PROCESS_READING *pr, unsigned char landmarkId, UUID *ownerId );
	int getLandmarkPF( PROCESS_READING *pr, unsigned char landmarkId, UUID *pfId );

protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AgentSensorLandmark )

	// Enumerate callback references
	enum CallbackRef {
		AgentSensorLandmark_CBR_convRequestSensorInfo = AgentBase_CBR_HIGH,
		AgentSensorLandmark_CBR_convRequestPFInfo,
		AgentSensorLandmark_CBR_cbRepeatRPFInfo,
		AgentSensorLandmark_CBR_convRequestData,
		AgentSensorLandmark_CBR_convRequestLandmarkInfo,
		AgentSensorLandmark_CBR_convRequestPFId,
		AgentSensorLandmark_CBR_convRequestLandmarkPF,
		AgentSensorLandmark_CBR_cbRepeatRLandmarkPF,
		AgentSensorLandmark_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool convRequestSensorInfo( void *vpConv );
	bool convRequestPFInfo( void *vpConv );
	bool cbRepeatRPFInfo( void *vpid );
	bool convRequestData( void *vpConv );
	bool convRequestLandmarkInfo( void *vpConv );
	bool convRequestPFId( void *vpConv );
	bool convRequestLandmarkPF( void *vpConv );
	bool cbRepeatRLandmarkPF( void *data );

protected:
	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream

	// -- Backup Notes -- 
	// Backup when:
	//  - sensorPose, sensorAvatar, landmark change

	// -- Recovery Notes --
	// - nothing

	virtual int	  recoveryFinish();

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );


// CAgentSensorLandmarkDLL: Override ExitInstance for debugging memory leaks
class CAgentSensorLandmarkDLL : public CWinApp {
public:
	CAgentSensorLandmarkDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};