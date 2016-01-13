/*

#include <list> 

#define SimAvatar_SAVE_PATH

#define SimAvatar_SONAR_SIGMA		0.02f  // m
//#define SimAvatar_CAMERA_R_SIGMA	0.02f  // percentage
//#define SimAvatar_CAMERA_A_SIGMA	0.02f  // radians
#define SimAvatar_CAMERA_R_SIGMA	0.015f  // percentage
#define SimAvatar_CAMERA_A_SIGMA	0.017f  // radians
//#define SimAvatar_CAMERA_R_SIGMA	0.04f  // percentage
//#define SimAvatar_CAMERA_A_SIGMA	0.03f  // radians

enum AvatarActions {
	AA_WAIT = 0,	// data = [float seconds]
	AA_MOVE,		// data = [float meters]
	AA_ROTATE,		// data = [float radians]
	AA_STOP,		// no data
	AA_IMAGE,		// data = [float delay]
	AA_DELAY,		// no data, delay until all sensor readings are processed
	AA_HIGH,
};

enum AvatarActionResults {
	AAR_SUCCESS = 0,	// data = []
	AAR_GAVEUP,			// data = [int reason]
	AAR_CANCELLED,		// data = [int reason]
	AAR_ABORTED,		// data = [int reason]
};

struct ActionInfo {
	int action;
	UUID director;
	_Callback *cb;
	int  thread;
};

#define SimAvatar_MOVE_FINISH_DELAY	1000 // ms
#define SimAvatar_MOVECHECK_PERIOD	100 // ms

enum AVATAR_PFSTATE {
	AVATAR_PFSTATE_X = 0,
	AVATAR_PFSTATE_Y,
	AVATAR_PFSTATE_R,
	AVATAR_PFSTATE_SIZE
};

#define PFUPDATE_PERIOD		200 // ms
#define PFNOCHANGE_PERIOD	300 // ms, this should be larger than PFUPDATE_PERIOD

struct SimAvatarState {
	_timeb t;
	float x, y, r;
	float vL, vR;
};

struct SimSonar {
	UUID id;
	_timeb nextT;
	int period; // ms
	SonarPose pose;

	// visualize
	int objId;
	float x, y, r, s;
	float color[3];
};

struct SimCamera {
	UUID id;
	_timeb nextT;
	int period; // ms
	CameraPose pose;
	float max; // max range
	float alpha; // half field of view

	// visualize
	int objId[10];
	float x, y, r[10], s[10];
	float color[3];
};

struct SimLandmark;

class SimPathPlanner;

class SimAvatar : public SimAgentBase {
public:
	enum TARGETPOS_FAIL_REASONS {
		TP_NEW_TARGET = 0,
		TP_TARGET_OCCUPIED,
		TP_TARGET_UNREACHABLE,
		TP_MOVE_ERROR,
	};

public:
	SimAvatar( Simulation *sim, Visualize *vis, RandomGenerator *randGen, char *name, float *color );
	~SimAvatar();

	virtual int configure( SimAvatarState *state, char *avatarFile, UUID *mapId, UUID *missionRegionId );
	virtual int start();
	virtual int stop();
	virtual int preStep( int dt );
	virtual int step();

	SimAvatarState * getStatePtr() { return &state; };
	int visualizePath();
	int setVisibility( int avatarVis = -1, int pathVis = -1, int extraVis = -1 );

	int	  setTargetPos( float x, float y, float r, char useRotation, UUID *initiator, _Callback *cb, int thread );
	int	  doneTargetPos( bool success, int reason = 0 );

	int clearActions( int reason = 0, bool aborted = false );
	int abortActions( int reason = 0 );
	int queueAction( UUID *director, _Callback *cb, int thread, int action, void *data = 0, int len = 0 );
	int useDelayActions( bool delay ) { this->useDelayActionsFlag = delay; return 0; };


	int pfResampled( _timeb *startTime, int *parents, float *state ); // particle filter has be resampled so we need to update our internal vars

protected:
	
	char name[256]; // name buf
	float color[3]; // vis color
	int objectId; // avatar vis object id
	int objectEstId; // estimated avatar vis object id
	int visPath; // true path path id
	int visPathObj; // true path object id
	int visPathPF; // pf path path id
	int visPathPFObj; // pf path object id
	bool pathVisible; // stores the visibilty of the paths
	bool extraVisible; // stores the visibility of the "extras" (sonar and cameras so far)

	float wheelBase; // wheel base
	float wheelBaseEst; // wheel base est
	float accelerationL; // left acceleration
	float accelerationR; // right acceleration
	float accelerationEst; // acceleration estimate
	float maxVelocityL;  // max left velocity
	float maxVelocityR;  // max right velocity
	float maxVelocityEst;  // max velocity estimate
	bool moveInProgress;
	float moveCurrent; // current move (linear or angular)
	float moveTargetL; // target linear move
	float moveTargetA; // target angular move

	int pfUpdateTimer; // updates the pf even if we're not moving

	SimAvatarState state;
	SimAvatarState stateEst; // estimated state to simulate odometry error
	SimAvatarState lastStateEst; 
	float odometrySigma[2]; // linear, rotational

	int doLinearMove( float L );
	int doAngularMove( float A );
	int doStop();
	int finishMove();

	int updatePos( float dx, float dy, float dr, _timeb *tb, bool forcePF = true );

	std::list<SimLandmark> landmarks;
	std::list<SimSonar>	sonars;
	std::list<SimCamera> cameras;

#ifdef SimAvatar_SAVE_PATH
	std::list<SimAvatarState> truePath;
#endif

	int parseConfigFile( char *avatarFile );
	int parseBase( FILE *fp );
	int parseParticleFilter( FILE *fp );
	int parseLandmark( FILE *fp );
	int parseSonar( FILE *fp );
	int parseCamera( FILE *fp );

	// target
	UUID mapId;
	UUID missionRegionId;
	SimPathPlanner *pathPlanner;
	bool  haveTarget;
	float targetX;
	float targetY;
	float targetR;
	char  targetUseRotation;
	UUID  targetInitiator; // intiator of target
	_Callback *targetCB;
	int	  targetThread; // thread to reply to

	int	  _goTargetPos();
	
	// actions
	std::list<ActionInfo> actionQueue;
	std::list<void*> actionData;
	bool actionInProgress;
	_timeb actionGiveupTime;

	float maxLinear; // maximum distance per move
	float maxRotation; // maximum distance per rotation

	int nextAction();
	int _stopAction(); // interal use, use clearActions() or abortActions()
	unsigned int actionTimeout; // timeout id for the current action, if any
	
	bool useDelayActionsFlag; // delay until processing queues are empty

	// particle filter
	float	*pfState; // last state sent to DDB
	float	pfInitSigma[3];   // sigma for initializing pf: forwardD, tangentialD, rotationalD
	float	pfUpdateSigma[3]; // sigma for pf updates: forwardV, tangentialV, rotationalV
	int		pfNumParticles;
	int		createParticleFilter( int numParticles, float *sigma ); // create the particle filter
	int		destroyParticleFilter(); // destroy the particle filter
	int		generatePFState( float *sigma ); // generate initial state
	int		updatePFState(); // update state
	
	// sensors
	int doSonar( SimSonar *sonar );
	int doCamera( SimCamera *camera );

public:
	
	DECLARE_CALLBACK_CLASS( SimAvatar )
	
	bool cbActionStep( void *vpdata );
	bool cbActionFinished( void *vpdata );
	bool cbPFUpdateTimer( void *NA );
};

*/