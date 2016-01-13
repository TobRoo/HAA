// AgentSensorCooccupancy.h : main header file for the AgentSensorCooccupancy DLL
//

#pragma once

#define REPEAT_RPFINFO_PERIOD	250 // ms

typedef std::map<UUID,float,UUIDless>	 mapAvatarRadius;
typedef std::map<UUID,UUID,UUIDless>	 mapPFAvatar;

enum {
	WAITING_ON_RADIUS = 0x0001 << 0,
	WAITING_ON_MAP = 0x0001 << 1,
	WAITING_ON_PF = 0x0001 << 2,
	WAITING_ON_AVATAR = 0x0001 << 3,
};

struct PROCESS_READING {
	int id;
	char waiting;
	UUID pf;
	UUID avatar;
	_timeb time;
	int pfNum; // number of particles
	int pfStateSize; // state size
	UUID pfStateRef;
	UUID pfWeightRef;
	float mapUpdateLoc[2]; // location of the mapUpdate origin
	int   mapUpdateSize[2]; // width,height (cols,rows)
	int mapHeight;
	UUID mapDataRef;
	int retries;
};

typedef std::map<int,PROCESS_READING>	mapReadings;

#define pfStateX( pfState, i ) pfState[i*3]
#define pfStateY( pfState, i ) pfState[i*3+1]
#define pfStateR( pfState, i ) pfState[i*3+2]

#define TEMPLATE_SIZE 64
#define TEMPLATE_SIZE_DIV2 (TEMPLATE_SIZE/2)

#define MAP_UPDATE_STRENGTH	0.0005f
#define OBS_DENSITY_AMBIENT 500.8f

class AgentSensorCooccupancy : public AgentBase {

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AgentBase

		// -- state data --
		UUID ownerId; // owner agent

		UUID mapId;
		float mapResolution;

		int readingNextId; // next id to assign reading

	};

protected:
	
	mapPFAvatar	    pfAvatars; // map of avatars by pf id
	mapAvatarRadius	avatarRadius; // map of avatar radii by id

	mapReadings readings; // queue of reading to process


//-----------------------------------------------------------------------------
// Non-state member variables	

protected:

	// computation arrays
	int	  sharedArraySize;
	float *obsDensity;

	// fImages
	FIMAGE *mapUpdate; // map update

//-----------------------------------------------------------------------------
// Functions	

public:
	AgentSensorCooccupancy( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AgentSensorCooccupancy();

	virtual int configure();	// initial configuration
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

private:

	int configureParameters( DataStream *ds );

	int addAvatarRadius( UUID *avatar, float radius );
	int getAvatarRadius( PROCESS_READING *pr );

	int addReading( UUID *pf, _timeb *time );
	int finishReading( PROCESS_READING *pr, char success );
	int requestMapRegion( PROCESS_READING *pr );
	int processReading( PROCESS_READING *pr );

protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AgentSensorCooccupancy )

	// Enumerate callback references
	enum CallbackRef {
		AgentSensorCooccupancy_CBR_convRequestMapInfo = AgentBase_CBR_HIGH,
		AgentSensorCooccupancy_CBR_convRequestAvatarInfo,
		AgentSensorCooccupancy_CBR_convRequestPFInfo,
		AgentSensorCooccupancy_CBR_cbRepeatRPFInfo,
		AgentSensorCooccupancy_CBR_convRequestMap,
		AgentSensorCooccupancy_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool convRequestMapInfo( void *vpConv );
	bool convRequestAvatarInfo( void *vpConv );
	bool convRequestPFInfo( void *vpConv );
	bool cbRepeatRPFInfo( void *vpid );
	bool convRequestMap( void *vpConv );

protected:
	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream

	// -- Backup Notes -- 
	// Backup when:
	//  - mapId, mapResolution, pfAvatars, avatarRadius change

	// -- Recovery Notes --
	// - if started
	//		if mapResolution == 0
	//			get map resolution

	virtual int	  recoveryFinish();

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );


// CAgentSensorCooccupancyDLL: Override ExitInstance for debugging memory leaks
class CAgentSensorCooccupancyDLL : public CWinApp {
public:
	CAgentSensorCooccupancyDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};