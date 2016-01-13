// AvatarER1.h : main header file for the AvatarER1 DLL
//

#pragma once


#define AvatarER1_MESSAGE_QUEUE_SIZE	100
#define AvatarER1_MAX_MESSAGE_LENGTH	256
#define AvatarER1_MESSAGE_TIMEOUT		2000 // ms

#define AvatarER1_ACTIONPROGRESSPERIOD	200  // ms
#define AvatarER1_MOVE_FINISH_DELAY		5000 // ms

class AvatarER1 : public AvatarBase {

private:
	
	// Instance Specific Parameters
	char TYPE[64];					// type string
	char ADDRESS[64];				// address string
	char PORT[32];					// port string
	float INNER_RADIUS;				// definately inside the avatar
	float OUTER_RADIUS;				// definately outside the avatar

	float LINEAR_VELOCITY;			// max linear velocity m/s rounded to nearest 0.01
	float LINEAR_ACCELERATION;		// linear acceleration m/s^2
	float LINEAR_DECELERATION;		// linear deceleration m/s^2
	int   ANGULAR_VELOCITY;			// max angular velocity deg/s
	float ANGULAR_ACCELERATION;		// angular acceleration m/s^2
	float ANGULAR_DECELERATION;		// angular deceleration m/s^2

	enum {
		MOTION_FORWARD = 0,
		MOTION_BACKWARD,
		MOTION_CW,
		MOTION_CCW
	};

	enum {
		RET_OK = 0,
		RET_EVENTS,
		RET_POSITION,
	};

public:
	AvatarER1( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AvatarER1();

	virtual int configure();	// initial configuration
	virtual int setInstance( char instance ); // instance specific parameters
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

private:
	sAddressPort avatarAP;
	spConnection avatarCon;

	int setAvatarState( bool ready ); // set avatar ready

protected:
	
	int updatePos( int action, unsigned short millis, _timeb *tb );

	// actions
	int queueAction( UUID *director, UUID *thread, int action, void *data, int len );
	int nextAction();
	bool   actionProgressStatus; // 1 == in progress, 0 == waiting to timeout
	_timeb actionProgressLast; // last time a posUpdate happened
	_timeb actionProgressEnd; // action finished
	int motionDirection;	// store the direction of the current motion
	int motionMillis;		// store total milliseconds of the current motion
	bool firstAvatarPos;		  // are we waiting for the first avatar pos?
	float avatarPosX, avatarPosY; // store where the avatar thinks it is
	float latestPosX, latestPosY; // store the last returned position values

	bool motorCommandUpdate( int action, _timeb *tb, bool finished = false );
	int motorCommandFinished();

	virtual int	 conProcessRaw( spConnection con );

	bool			listeningToEvents; // are we already listening to events?
	UUID			avatarMessageTimeout;
	int				avatarMessageTimeouts; // count consecutive timeouts
	char			messageQueue[AvatarER1_MESSAGE_QUEUE_SIZE][AvatarER1_MAX_MESSAGE_LENGTH];
	char			messageReturn[AvatarER1_MESSAGE_QUEUE_SIZE]; // expected return values
	int				messageQueueBegin, messageQueueEnd;
	int				avatarLatencyTicket;
	
	int avatarResetConnection();
	int avatarProcessMessage( spConnection con, char *message );
	int avatarQueueMessage( char *message, char expectedReturn = RET_OK );
	int avatarNextMessage();

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AvatarER1 )

	// Enumerate callback references
	enum CallbackRef {
		AvatarER1_CBR_cbWatchAvatarConnection = AvatarBase_CBR_HIGH,
		AvatarER1_CBR_cbActionStep,
		AvatarER1_CBR_cbMessageTimeout,
		AvatarER1_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool cbWatchAvatarConnection( int evt, void *vpcon );
	bool cbActionStep( void *vpdata );
	bool cbMessageTimeout( void *NA );


};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );

// CAvatarER1DLL: Override ExitInstance for debugging memory leaks
class CAvatarER1DLL : public CWinApp {
public:
	CAvatarER1DLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};