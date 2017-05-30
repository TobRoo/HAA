// ExecutiveSimulation.h : main header file for the ExecutiveSimulation DLL
//

#pragma once


#define MAX_PATHS	256
#define MAX_OBJECTS	256

#define SIMULATION_STEP 10 // ms

struct SimLandmark {
	UUID id;
	UUID owner;
	unsigned char code;
	float x, y;
	float wx, wy;

	float height, elevation;

	bool collected; // has the landmark been picked up
};

struct SimAvatarState {
	_timeb t;
	float x, y, r;
	float vL, vR;
};

struct SimSonar {
	int index;
	//UUID id;
	_timeb nextT;
	int period; // ms
	SonarPose pose;

	// visualize
	int objId;
	float x, y, r, s;
	float color[3];
};

struct SimCamera {
	int index;
	//UUID id;
	_timeb nextT;
	int period; // ms
	CameraPose pose;
	float max; // max range
	float alpha; // half field of view

	// visualize
	int objId[10];
	float x, y, r[10], s[10];
	float color[3];
	int ffobjId[10];
	float ffr[10], ffs[10];
	float ffcolor[3];
};

#define SimCamera_FLOORFINDERMAX 2.0f // max distance the floorfinder will look

struct AVATAR_HOLD {
	UUID uuid;
	UUID owner;
	float x, y, r;
	char avatarFile[MAX_PATH];
	int landmarkCodeOffset;
};

class ExecutiveSimulation;

#define SimAvatar_SONAR_SIGMA		0.02f  // m
//#define SimAvatar_CAMERA_R_SIGMA	0.02f  // percentage
//#define SimAvatar_CAMERA_A_SIGMA	0.02f  // radians
#define SimAvatar_CAMERA_R_SIGMA	0.015f  // percentage
#define SimAvatar_CAMERA_A_SIGMA	0.017f  // radians
//#define SimAvatar_CAMERA_R_SIGMA	0.04f  // percentage
//#define SimAvatar_CAMERA_A_SIGMA	0.03f  // radians

class SimAvatar {

public:
	SimAvatar( UUID *uuid, UUID *owner, float x, float y, float r,  _timeb *t, char *avatarFile, int landmarkCodeOffset, ExecutiveSimulation *sim, Logger *logger, AgentPlayback *apb );
	~SimAvatar();

	int SimPreStep( _timeb *simTime, int dt );
	int SimStep( _timeb *simTime, int dt );

	int appendOutput( DataStream *ds );

	int crash();

	int doLinearMove( float L, char moveId );
	int doAngularMove( float A, char moveId );
	int doStop( char moveId );

	UUID ownerId; // owner id

	int doCamera( int cameraInd );
	int doCollectLandmark ( unsigned char code, float x, float y, UUID *thread );
	int doDepositLandmark (unsigned char code, float x, float y, UUID *thread, UUID *initiator);

	SimAvatarState *getState() { return &this->state; };

private:
	UUID uuid;

	bool crashed; 

	DataStream ds; // temporary ds
	DataStream output; // output stream

	ExecutiveSimulation *sim;
	Logger *Log;
	AgentPlayback *apb;

	SimAvatarState state;
	SimAvatarState stateEst; // estimated state to simulate odometry error
	SimAvatarState lastReportedStateEst; 
	float odometrySigma[2]; // linear, rotational

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
	char moveId; // current move id
	float accCmdL;		// current acceleration command
	float accCmdR;

	int finishMove();

	int landmarkCodeOffset; // add this to parsed landmark codes to allow multiple avatars of the same type
	std::list<SimLandmark> landmarks;
	std::list<SimSonar>	sonars;
	std::list<SimCamera> cameras;

	int parseConfigFile( char *avatarFile );
	int parseBase( FILE *fp );
	int parseParticleFilter( FILE *fp );
	int parseLandmark( FILE *fp );
	int parseSonar( FILE *fp );
	int parseCamera( FILE *fp );

	// sensors
	int doSonar( SimSonar *sonar );
	int doCamera( SimCamera *camera );


	// vis
	int sinceLastVisUpdate; // millis since last update
	bool extraVisible; // display extras?
	int objIdTrue;
	int objIdEst;

};

typedef std::map<UUID,SimAvatar *,UUIDless> mapSimAvatar;

class ExecutiveSimulation : public AgentBase {
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

public:
	ExecutiveSimulation( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~ExecutiveSimulation();

	virtual int configure();	// initial configuration
	
	virtual int parseMF_HandleLandmarkFile( char *fileName );
	virtual int parseMF_HandlePathFile( char *fileName );

	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

private:
	
	int nextPathId;
	PATH paths[MAX_PATHS];

	int nextObjectId;
	int highObjectId;
	OBJECT objects[MAX_OBJECTS];

	std::list<AVATAR_HOLD> heldAvatars; // hold avatars if we haven't started yet

public:
	int newPath( int count, float *x, float *y );
	int deletePath( int id );
	int loadPathFile( char *fileN );		// load path

	int newStaticObject( float x, float y, float r, float s, int path, float colour[3], float lineWidth, bool solid = false, char *name = NULL )
		{ return newStaticObject( x, y, r, s, 1, &path, &colour, &lineWidth, solid, name ); };
	int newStaticObject( float x, float y, float r, float s, int count, int *paths, float *colours[3], float *lineWidths, bool solid = false, char *name = NULL );
	int createObject( float *x, float *y, float *r, float *s, int count, int *paths, float *colours[3], float *lineWidths, bool dynamic, bool solid, char *name );
	int deleteObject( int id );
	int updateStaticObject( int id, float x, float y, float r, float s );

	int showObject( int id );
	int hideObject( int id );
	
	int addAvatar( UUID *uuid, UUID *owner, float x, float y, float r, char *avatarFile, int landmarkCodeOffset );
	int removeAvatar( UUID *uuid, char retireMode );

	int getLinePathId() { return this->pathIdLine; };
	int getXPathId() { return this->pathIdX; };
	int getRobotPathId() { return this->pathIdRobot; };
	int getSonarPathId( float alpha );

	int addLandmark( SimLandmark *landmark );
	int removeLandmark( SimLandmark *landmark );

	// should be used only for read only purposes!
	std::list<SimLandmark*> * getLandmarkList() { return &this->landmarks; };
	int getNextObject( OBJECT **object, int after = -1 );
	int getPath( int id, PATH **path );

private:
	// basic paths
	int pathIdLine;
	int pathIdX;
	int pathIdRobot;
	int pathIdParticle;
	std::map<float,int> pathIdSonar; // sonar paths indexed by alpha

	mapSimAvatar	avatars; // list of avatars by id
	std::map<UUID,UUID,UUIDless> avatarOwners; // map of owners by avatar
	std::list<SimLandmark*> landmarks; // list of all landmarks

	_timeb simTime;
	unsigned long long totalSimSteps;


	int parseLandmarkFile( char *filename );
	int uploadSimSteps();

protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( ExecutiveSimulation )

	// Enumerate callback references
	enum CallbackRef {
		ExecutiveSimulation_CBR_cbSimStep = AgentBase_CBR_HIGH,
		ExecutiveSimulation_CBR_cbLogPose,
		ExecutiveSimulation_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool cbSimStep( void *NA );
	bool cbLogPose( void *NA );

};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );


// CExecutiveSimulationDLL: Override ExitInstance for debugging memory leaks
class CExecutiveSimulationDLL : public CWinApp {
public:
	CExecutiveSimulationDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};