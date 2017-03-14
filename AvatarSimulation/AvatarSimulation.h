// AvatarSimulation.h : main header file for the AvatarSimulation DLL
//

#pragma once

#include "Aria.h"

#define AvatarSimulation_MOVE_FINISH_DELAY 1000 // ms

#define AvatarSimulation_SIMWAKE_PERIOD	100 // ms

#define AvatarSimulation_MOVECHECK_PERIOD	10 // ms

#define AvatarSimulation_UNSET 999999 // unset flag

#define AvatarSimulation_SONAR_RANGE_MAX 2.5f
#define AvatarSimulation_SONAR_RANGE_MIN 0.0f

#define AvatarSimulation_SONAR_SIG_EST	0.09f
#define AvatarSimulation_SONAR_A_EST		(fM_PI*10/180.0f) // 10 deg
#define AvatarSimulation_SONAR_B_EST		(fM_PI*5/180.0f) // 5 deg

#define AvatarSimulation_SKIP_SONAR_COUNT	5 // only process every Nth reading from each sonar

#define AvatarSimulation_IMAGECHECKPERIOD		1000 // ms
#define AvatarSimulation_IMAGE_DELAY			 0.003f //0.3f // seconds, delay this long before taking an image to make sure it is clear

typedef std::map<int,UUID>  mapSensorId;

struct COLLECTION_TASK_INFO {
	UUID initiator;
	UUID thread;
};

class AvatarSimulation : public AvatarBase {

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AvatarBase::State state; // inherit state from AvatarBase

		// -- state data --
		UUID execSimulationId; // uuid of the Simulation Executive

		// Instance Specific Parameters
		char TYPE[64];					// type string
		char AVATAR_FILE[256];			// avatar file string
		int  LANDMARK_CODE_OFFSET;		// landmark code offset	
			
		float wheelBaseEst; // wheel base est
		float accelerationEst; // acceleration estimate
		float maxVelocityEst;  // max velocity estimate

		int capacity; // cargo capacity
			
		bool simConfigured;
		bool simRegistrationConfirmed;
		UUID simWakeTimeout; // check if any SimEvents have occured
	
		bool  simPoseInitialized;
		float lastUpdatePosX; // in sim frame
		float lastUpdatePosY; // in sim frame
		float lastUpdatePosR; // in world frame
		float lastUpdateVelLin; // linear velocity
		float lastUpdateVelAng; // angular velocity 
		bool moveDone; // is the current move done?
		char moveId; // id of the current move

		float moveStartX; // estimated x pos when we started our move
		float moveStartY; // estimated x pos when we started our move
		float moveStartR; // estimated x pos when we started our move

		bool   actionProgressStatus; // 1 == in progress, 0 == waiting to timeout
		_timeb actionProgressLast; // last time a posUpdate happened
		_timeb actionProgressEnd; // action finished
		
		int skipReading[64];
		int requestedImages;
	
		_timeb actionGiveupTime;
	};

protected:
	
	mapSensorId	sonarId; // sonar ids
	mapSensorId cameraId; // camera ids

	std::list<COLLECTION_TASK_INFO> collectionTask;

//-----------------------------------------------------------------------------
// Non-state member variables	

protected:
	DataStream ds2; // sometimes you need two
	
//-----------------------------------------------------------------------------
// Functions	

public:
	AvatarSimulation( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AvatarSimulation();

	virtual int configure();	// initial configuration
	virtual int setInstance( char instance ); // instance specific parameters
	int simConfigure();		// configure robot
	virtual int parseMF_HandleAvatar( AgentType *agentType, char *fileName, float x, float y, float r, float startTime, float duration, char retireMode );


	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

private:

	int parseAvatarFile( char *avatarFile );
	int parseBase( FILE *fp );
	int parseParticleFilter( FILE *fp );
	int parseLandmark( FILE *fp );
	int parseSonar( FILE *fp );
	int parseCamera( FILE *fp );

	int setAvatarState( bool ready ); // set avatar ready
	virtual int retireAvatar();

	int setPos( float x, float y, float r, _timeb *tb = NULL );
	int updateSimPos( float x, float y, float r, _timeb *tb );

	int parseAvatarOutput( DataStream *ds );

protected:

	// actions
	int queueAction( UUID *director, UUID *thread, int action, void *data, int len );
	int nextAction();

	int cameraCommandFinished();

protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AvatarSimulation )

	// Enumerate callback references
	enum CallbackRef {
		AvatarSimulation_CBR_cbActionStep = AvatarBase_CBR_HIGH,
		AvatarSimulation_CBR_cbSimWake,
		AvatarSimulation_CBR_convRequestExecutiveSimulationId,
		AvatarSimulation_CBR_cbRequestExecutiveSimulationId,
		AvatarSimulation_CBR_convRequestAvatarOutput,
		AvatarSimulation_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool cbActionStep( void *vpdata );

	bool cbSimWake( void *na );
	bool convRequestExecutiveSimulationId( void *vpConv );
	bool cbRequestExecutiveSimulationId( void *NA );
	bool convRequestAvatarOutput( void *vpConv );

protected:
	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream

	// -- Backup Notes -- 
	// Backup when:
	//  - execSimulationId, simConfigured

	// -- Recovery Notes --
	// - on recoveryFinish
	//		- if execSimulationId is unset, identify and greet execSimulation
	//		- if simConfigured, add simWake timeout

	virtual int	  recoveryFinish();

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );

// CAvatarSimulationDLL: Override ExitInstance for debugging memory leaks
class CAvatarSimulationDLL : public CWinApp {
public:
	CAvatarSimulationDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};