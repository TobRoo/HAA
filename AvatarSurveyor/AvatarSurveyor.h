// AvatarSurveyor.h : main header file for the AvatarSurveyor DLL
//

#pragma once

#define AvatarSurveyor_MOVE_FINISH_DELAY	5000 // ms
#define AvatarSurveyor_ACTIONPROGRESSPERIOD	50 // ms

#define AvatarSurveyor_IMAGE_TIMEOUT		5000 // ms
#define AvatarSurveyor_IMAGECHECKPERIOD		1000 // ms

#define AvatarSurveyor_IMAGE_DELAY			0.6f // seconds, delay this long before taking an image to make sure it is clear

class AvatarSurveyor : public AvatarBase {

private:
	
	// Instance Specific Parameters
	char TYPE[64];					// type string
	char ADDRESS[64];				// address string
	char PORT[32];					// port string
	float INNER_RADIUS;				// definately inside the avatar
	float OUTER_RADIUS;				// definately outside the avatar

	float FORWARD_VELOCITY;			// m/s
	float FORWARD_COAST;			// m
	float BACKWARD_VELOCITY;		// m/s
	float BACKWARD_COAST;			// m
	int LEFT_FORWARD_PWM;			// PWM -127-127
	int RIGHT_FORWARD_PWM;			// PWM -127-127
	int LEFT_BACKWARD_PWM;			// PWM -127-127
	int RIGHT_BACKWARD_PWM;			// PWM -127-127
	float CW_VELOCITY;				// rad/s
	float CCW_VELOCITY;				// rad/s
	float CW_DRAG;					// rad
	float CCW_DRAG;					// rad
	float CW_SMALL_ANGLE;			// rad
	float CCW_SMALL_ANGLE;			// rad
	float CW_SMALL_ANGLE_VELOCITY;	// rad/s
	float CCW_SMALL_ANGLE_VELOCITY; // rad/s
	int LEFT_CW_PWM;				// PWM -127-127  NOTE: for some reason these directions are
	int RIGHT_CW_PWM;				// PWM -127-127  opposite from what you would expect!?
	int LEFT_CCW_PWM;				// PWM -127-127  It seems to happen when you have opposite
	int RIGHT_CCW_PWM;				// PWM -127-127  signs for the two PWMs...
	
	enum {
		MOTION_FORWARD = 0,
		MOTION_BACKWARD,
		MOTION_CW,
		MOTION_CCW
	};

public:
	AvatarSurveyor( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AvatarSurveyor();

	virtual int configure();	// initial configuration
	virtual int setInstance( char instance ); // instance specific parameters
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

private:
	sAddressPort avatarAP;
	spConnection avatarCon;

	int setAvatarState( bool ready ); // set avatar ready

	UUID cameraId;				// UUID of the camera sensor
	unsigned char *imageData;	// buffer to store images
	bool readingImage;			// currently reading an image
	int  imageSize;				// size of the current image
	int  imageRead;				// amount of data already read
	CameraReading cameraReading; // camera reading info

protected:
	
	int updatePos( int action, unsigned short millis, _timeb *tb, bool finished_move = false );

	// actions
	int queueAction( UUID *director, UUID *thread, int action, void *data, int len );
	int nextAction();
	bool   actionProgressStatus; // 1 == in progress, 0 == waiting to timeout
	_timeb actionProgressLast; // last time a posUpdate happened
	_timeb actionProgressEnd; // action finished
	int motionDirection;	// store the direction of the current motion
	int motionMillis;		// store total milliseconds of the current motion
	bool motionSmallAngle;  // store if current motion is a small angle

	bool motorCommandUpdate( int action, _timeb *tb, bool finished = false );
	int motorCommandFinished();
	int _cameraCommandFinishedImgCount; // static count
	int cameraCommandFinished();

	virtual int	 conProcessRaw( spConnection con );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AvatarSurveyor )

	// Enumerate callback references
	enum CallbackRef {
		AvatarSurveyor_CBR_cbWatchAvatarConnection = AvatarBase_CBR_HIGH,
		AvatarSurveyor_CBR_cbActionStep,
		AvatarSurveyor_CBR_cbDebug,
		AvatarSurveyor_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool cbWatchAvatarConnection( int evt, void *vpcon );
	bool cbActionStep( void *vpdata );
	bool cbDebug( void *vpdata );


};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );

// CAvatarSurveyorDLL: Override ExitInstance for debugging memory leaks
class CAvatarSurveyorDLL : public CWinApp {
public:
	CAvatarSurveyorDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};