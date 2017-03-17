// AvatarBase.h : main header file for the AvatarBase DLL
//

#pragma once

#include <list>
using namespace std;

struct ActionInfo {
	int action;
	UUID director;
	UUID thread;
};

enum AVATAR_PFSTATE {
	AVATAR_PFSTATE_X = 0,
	AVATAR_PFSTATE_Y,
	AVATAR_PFSTATE_R,
	AVATAR_PFSTATE_SIZE
};

struct ParticleFilter_Prediction {
	_timeb tb;
	float dt;
	float forwardD;
	float tangentialD;
	float rotationalD;
};

// Some rough estimates for particle filter constants
#define AVATAR_INIT_LINEAR_SIG		0.1f
#define AVATAR_INIT_ROTATIONAL_SIG	0.01f
//#define AVATAR_FORWARDV_SIG_EST		0.08f  // m/s
//#define AVATAR_TANGENTIALV_SIG_EST	0.01f  // m/s
//#define AVATAR_ROTATIONALV_SIG_EST	0.09f  // rad/s
#define AVATAR_FORWARDV_SIG_EST		0.1f  // m/s
#define AVATAR_TANGENTIALV_SIG_EST	0.02f  // m/s
#define AVATAR_ROTATIONALV_SIG_EST	0.1f  // rad/s


// everything is perfect!
//#define AVATAR_INIT_LINEAR_SIG		0.0000001f
//#define AVATAR_INIT_ROTATIONAL_SIG	0.0000001f
//#define AVATAR_FORWARDV_SIG_EST		0.0000002f  // m/s
//#define AVATAR_TANGENTIALV_SIG_EST	0.0000002f  // m/s
//#define AVATAR_ROTATIONALV_SIG_EST	0.0000002f  // rad/s

#define PFUPDATE_PERIOD		60 // ms
#define PFNOCHANGE_PERIOD	(PFUPDATE_PERIOD+100) // ms, this should be larger than PFUPDATE_PERIOD

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

class AvatarBase : public AgentBase {

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AgentBase
		
		// -- state data --
		UUID	avatarExecutiveUUID; // id for the avatar executve
		UUID	avatarUUID;  // id for the DDB entry
		_timeb  retireTime;   // time started
		char	retireMode;
		bool	retireSet;	 // set to retire
		int		sensorTypes;
		ITEM_TYPES		capacity;	//1 = Light objects, 2  = Heavy objects  (0 = NON_COLLECTABLE, cannot carry items)
	
		// particle filter
		UUID	pfId; // particle filter UUID
		bool	particleFilterInitialized;
		UUID	pfStateRef; // bufRef for last state sent to DDB
		UUID	pfUpdateTimer; // updates the pf even if we're not moving
		float	pfInitSigma[3];   // sigma for initializing pf: forwardD, tangentialD, rotationalD
		float	pfUpdateSigma[3]; // sigma for pf updates: forwardV, tangentialV, rotationalV
		int		pfNumParticles;
				
		// position/rotation
		bool posInitialized;
		float posX;
		float posY;
		float posR;
		_timeb posT;
		float lastX;
		float lastY;
		float lastR;
		_timeb lastT;

		// map and target
		UUID mapId;
		UUID missionRegionId;
		bool  haveTarget;
		float targetX;
		float targetY;
		float targetR;
		char  targetUseRotation;
		UUID  targetInitiator; // intiator of target
		int   targetControllerIndex; // index of the controller
		UUID  targetThread; // thread to reply to

		// motion planner
		UUID agentPathPlanner;
		char agentPathPlannerSpawned;
		int  agentPathPlannerStatus; // agent status
		UUID agentIndividualLearning;
		char agentIndividualLearningSpawned;
		int  agentIndividualLearningStatus; // agent status

		// per-avatar team learning agent

		UUID agentTeamLearning;
		char agentTeamLearningSpawned;
		int  agentTeamLearningStatus; // agent status


		// actions
		float maxLinear;   // maximum distance per move
		float maxRotation; // maximum distance per rotation
		float minLinear;   // minimum distance per move
		float minRotation; // minimum distance per rotation
		UUID actionThread; // thread to reply to
		UUID actionTimeout; // timeout id for the current action, if any
		bool actionInProgress;
		bool canInteract;  // If the avatar can choose to interact with landmarks

		int SLAMmode;
		int SLAMreadingsProcessing; // count readings being processed
		bool SLAMdelayNextAction; // delay next action until readings have been processed
	};

protected:
	
	// actions
	list<ActionInfo> actionQueue;
	list<UUID> actionData; // bufRef

	// SLAM delay mode NOTE: delay mode is not currently implemented to support crashes!
	mapTypeAgents agents; // map of agents by sensor type
	mapAgentSpawning agentsSpawning; // list of currently spawning agents
	std::map<UUID,int,UUIDless> agentsStatus; // map of status by agent id
	std::map<UUID,bool,UUIDless> agentsProcessing; // currently processing, by agent id

	mapTypeQueue typeQueue; // queues of readings by sensor type
	mapAgentQueue agentQueue; // queues of readings by agent

	std::map<UUID, DDBRegion, UUIDless> collectionRegions;

//-----------------------------------------------------------------------------
// Non-state member variables	

protected:
	bool avatarReady; // avatar is ready to receive commands
	bool retiring; // in the process of retiring

	UUID _parseSensorUuid; // temporary storage

	UUID AvatarBase_recoveryLock;

	int motionPlanner;

	enum MotionPlanner {
		AgentPathPlanner,
		AgentIndividualLearning
	};
	

	bool mapRandom;
	bool mapReveal;


//-----------------------------------------------------------------------------
// Functions	

public:
	AvatarBase( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AvatarBase();

	virtual int configure();	// initial configuration
	virtual int parseMF_HandleAvatar( AgentType *agentType, char *fileName, float x, float y, float r, float startTime, float duration, char retireMode );

	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

	virtual int parseMF_HandleMapOptions(bool mapReveal, bool mapRandom);
	virtual int parseMF_HandleOptions( int SLAMmode );
	virtual int parseMF_HandleLearning(bool individualLearning, bool teamLearning);

protected:

	virtual int setAvatarState( bool ready ); // set avatar ready state

	// register avatar
	int	registerAvatar( char *type, float innerRadius, float outerRadius, int capacity, int sensorTypes );
	virtual int retireAvatar();

	int registerLandmark( UUID *id, unsigned char code, UUID *avatar, float x, float y );

	// sensor setup
	UUID * parseSensorSonar( FILE *paraF ); // parse a sonar sensor line from this file
	UUID * parseSensorCamera( FILE *paraF ); // parse a camera sensor line from this file

	int		registerSensor( UUID *id, int type, UUID *avatar, UUID *pf, void *pose, int poseSize );

	int		submitSensorSonar( UUID *sonar, _timeb *t, SonarReading *reading );
	int		submitSensorCamera( UUID *camera, _timeb *t, CameraReading *reading, void *data, unsigned int len );

	// particle filter
	int		createParticleFilter( int numParticles, float *sigma ); // create the particle filter
	int		destroyParticleFilter( bool cleanDDB = true ); // destroy the particle filter
	int		generatePFState( float *sigma ); // generate initial state
	int		updatePFState(); // update state
	int		_updatePFState( _timeb *tb, float dt, float forwardD, float tangentialD, float rotationalD );
	int		_resampleParticleFilter( UUID *id, int pNum, float *weights ); // do resample
//	int		_lockParticleFilter( UUID *id, UUID *key, UUID *thread, UUID *host ); // lock particle filter for resampling
//	int		_unlockParticleFilter( DataStream *ds ); // resampling finished

	// position/rotation
	virtual int setPos( float x, float y, float r, _timeb *tb = NULL );
	virtual int updatePos( float dForward, float dTangential, float dRotational, _timeb *tb );

	int	  setTargetPos( float x, float y, float r, char useRotation, UUID *initiator, int controllerIndex, UUID *thread );

	// actions

	int spawnAgentTeamLearning();

	int getActionsFromAgentPathPlanner();
	int spawnAgentIndividualLearning();
	int spawnAgentPathPlanner();
	int clearActions( int reason = 0, bool aborted = false );
	int abortActions( int reason = 0 );
	virtual int queueAction( UUID *director, UUID *thread, int action, void *data = 0, int len = 0 );
	virtual int nextAction();
	int _stopAction(); // interal use, use clearActions() or abortActions()

	virtual int ddbNotification( char *data, int len );

	// SLAM delay mode NOTE: delay mode is not currently implemented to support crashes!
	int	   getProcessingPhases( int sensor );
	int	   requestAgentSpawn( READING_TYPE *type, char priority );
	UUID * getProcessingAgent( READING_TYPE *type );

	int nextSensorReading( UUID *agent );
	int assignSensorReading( UUID *agent, READING_QUEUE *rq );
	int doneSensorReading( UUID *uuid, UUID *sensor, _timeb *tb, char success );

	int newSensorReading( READING_TYPE *type, UUID *uuid, _timeb *tb, int attempt = 0 );

protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AvatarBase )

	// Enumerate callback references
	enum CallbackRef {
		AvatarBase_CBR_cbRequestExecutiveAvatarId = AgentBase_CBR_HIGH,
		AvatarBase_CBR_convRequestExecutiveAvatarId,
		AvatarBase_CBR_convRequestAgentPathPlanner,
		AvatarBase_CBR_convRequestAgentIndividualLearning,
		AvatarBase_CBR_convRequestAgentTeamLearning,
		AvatarBase_CBR_convPathPlannerSetTarget,
		AvatarBase_CBR_convPFInfo,
		AvatarBase_CBR_convAgentInfo,
		AvatarBase_CBR_cbPFUpdateTimer,
		AvatarBase_CBR_cbRetire,
		AvatarBase_CBR_convRequestAgentSpawn,
		AvatarBase_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool	cbRequestExecutiveAvatarId( void *NA );
	bool	convRequestExecutiveAvatarId( void *vpConv );
	bool	convRequestAgentPathPlanner( void *vpConv );
	bool	convRequestAgentIndividualLearning(void *vpConv);
	bool	convRequestAgentTeamLearning(void *vpConv);
	bool	convPathPlannerSetTarget( void *vpConv );
	bool    convPFInfo( void *vpConv );
	bool    convAgentInfo( void *vpConv );
	bool	cbPFUpdateTimer( void *NA );
	bool	cbRetire( void *NA );

	bool	convRequestAgentSpawn( void *vpConv );

protected:
	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream

	// -- Backup Notes -- 
	// Backup when:
	//  - startTime, retireMode, retireSet, avatarExecutiveUUID, mapId, missionRegionId, posInitialized, particleFilterInitialized, pfInitialSigma, pfUpdateSigma, agentPathPlanner, or target changes

	// -- Recovery Notes --
	// - if mapId or missionRegionId are not set, register with ExecutiveMission
	// - if posInitialized
	//		follows that particleFilterInitialized
	//			lock recovery
	//			get pf state and current position from DDB
	//   else
	//		register with ExecutiveMission
	// - if agentPathPlanner
	//		get agentPathPlannerStatus
	// - if retireSet
	//		reinitialize retire timeout

	virtual int	  recoveryFinish();

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream

};

#ifdef AVATARBASE_DLL
int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
#endif