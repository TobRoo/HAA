// AvatarPioneer.h : main header file for the AvatarPioneer DLL
//

#pragma once

#include "Aria.h"

#define AvatarPioneer_MOVE_FINISH_DELAY 1000 // ms

#define AvatarPioneer_ARIAWAKE_PERIOD	50 // ms

#define AvatarPioneer_MOVECHECK_PERIOD	100 // ms

#define AvatarPioneer_UNSET 999999 // unset flag

#define AvatarPioneer_SONAR_RANGE_MAX 2.5f
#define AvatarPioneer_SONAR_RANGE_MIN 0.0f

#define AvatarPioneer_SONAR_SIG_EST	0.09f
#define AvatarPioneer_SONAR_A_EST		(fM_PI*10/180.0f) // 10 deg
#define AvatarPioneer_SONAR_B_EST		(fM_PI*5/180.0f) // 5 deg

#define AvatarPioneer_SKIP_SONAR_COUNT	5 // only process every Nth reading from each sonar

struct SonarReadingPlus {
	int ind;
	_timeb t;
	SonarReading reading;
};

typedef std::map<int,UUID>  mapSonarId;

class AvatarPioneer : public AvatarBase {
public:
	struct State {
		AvatarBase::State state; // inherit state from AvatarBase

		// stub data
		
		// state data
		
	};

	AvatarPioneer( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AvatarPioneer();

	virtual int configure();	// initial configuration
	int ariaConfigure();		// configure robot

	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

private:
	int setAvatarState( bool ready ); // set avatar ready

	mapSonarId	sonarId; // sonar ids
	int addSonar( int ind, float x, float y, float r ); // register sonar with DDB

	int setPos( float x, float y, float r, _timeb *tb = NULL );
	int updateAriaPos();

	// Aria
	int ariaArgc;
	ArArgumentParser *ariaArgParser;
	ArSimpleConnector *ariaCon;
	ArRobot *robot;

	ArFunctorC<AvatarPioneer> *ariaConnectedCB;
	ArFunctorC<AvatarPioneer> *ariaConnFailCB;
	ArFunctorC<AvatarPioneer> *ariaDisconnectedCB;
	ArFunctorC<AvatarPioneer> *ariaSyncTaskCB;

	UUID AriaWakeTimeout; // check if any AriaEvents have occured
	enum ARIAEVENTS {
		AE_NONE = 0,
		AE_CONNECTED,
		AE_CONNECTIONFAILED,
		AE_DISCONNECTED,
		AE_SYNC
	};

	// !! References to the following objects should be protected by AR_LOCK/AR_UNLOCK mutex !!
	std::list<int> AriaEvent;
	_timeb ariaPoseTb; // last time a pose was recieved
	float ariaFrameRotation; // rotation from aria frame to world frame
	float ariaLastR;
	float ariaAbsoluteR;
	float lastUpdatePosX; // in aria frame
	float lastUpdatePosY; // in aria frame
	float lastUpdatePosR; // in world frame
	std::list<SonarReadingPlus> ariaSonarReadings;


protected:

	int skipReading[64];

	// actions
	int nextAction();
	
	_timeb actionGiveupTime;

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AvatarPioneer )

	// Enumerate callback references
	enum CallbackRef {
		AvatarPioneer_CBR_cbActionStep = AvatarBase_CBR_HIGH,
		AvatarPioneer_CBR_cbAriaWake,
		AvatarPioneer_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool cbActionStep( void *vpdata );

	bool cbAriaWake( void *na );

	// These callbacks get called from the robot thread, 
	// and so the only thing they should safely do is set the 
	// AriaEvent value to be handled cleanly from the cbAriaWake callback
	void cbAriaThreadConnected();
	void cbAriaThreadConnectionFailed();
	void cbAriaThreadDisconnected();
	void cbAriaThreadSyncTask();
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );

// CAvatarPioneerDLL: Override ExitInstance for debugging memory leaks
class CAvatarPioneerDLL : public CWinApp {
public:
	CAvatarPioneerDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};