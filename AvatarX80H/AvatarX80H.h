// AvatarX80H.h : main header file for the AvatarX80H DLL
//

#pragma once

WCHAR DLL_Directory[512];


#define AvatarX80H_INIT_LINEAR_SIG		0.1f
#define AvatarX80H_INIT_ROTATIONAL_SIG	0.01f
#define AvatarX80H_FORWARDV_SIG_EST		0.03f  // m/s
#define AvatarX80H_TANGENTIALV_SIG_EST	0.006f  // m/s
#define AvatarX80H_ROTATIONALV_SIG_EST	0.03f  // rad/s

#define AvatarX80H_MESSAGE_QUEUE_SIZE	100
#define AvatarX80H_IMAGE_TIMEOUT		5000 // ms

#define AvatarX80H_IMAGE_DELAY			1.0f // seconds, delay this long before taking an image to make sure it is clear


struct SonarReadingPlus {
	int ind;
	_timeb t;
	SonarReading reading;
};

typedef std::map<int,UUID>  mapSonarId;

class CJpegDecode;

class AvatarX80H : public AvatarBase {

private:

	// Instance Specific Parameters
	char TYPE[64];					// type string
	char ADDRESS[64];				// address string
	char PORT[32];					// port string
	float INNER_RADIUS;				// definately inside the avatar
	float OUTER_RADIUS;				// definately outside the avatar

	float WHEEL_CIRCUMFERENCE;		// m
	float WHEEL_BASE;				// m
	int ENCODER_DIVS;
	int COUNTS_PER_REV;
	int ENCODER_THRESHOLD;			// counts

	float CALIBRATE_ROTATION;
	float CALIBRATE_POSITION;

	int MOVE_FINISH_DELAY;			// ms

	float SAFE_VELOCITY;			// m/s
	float SAFE_ROTATION;			// rad/s

	unsigned short SERVO_POSE[6];	// steps

	int PING_WAIT_PERIOD;			// ms
	int MESSAGE_TIMEOUT;			// ms

	int SKIP_SONAR_COUNT;			// only process every Nth reading from each sonar

	int NUM_SONAR;	

public:
	struct State {
		AvatarBase::State state; // inherit state from AvatarBase

		// stub data
		
		// state data
		
	};

	AvatarX80H( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AvatarX80H();

	virtual int configure();	// initial configuration
	virtual int setInstance( char instance ); // instance specific parameters
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

private:
	sAddressPort avatarAP;
	spConnection avatarCon;
	int			 avatarConWatcher;

	int setAvatarState( bool ready ); // set avatar ready

	// TEMP
	FILE *sensorF;
	FILE *encoderF;

protected:

	// encoder counts
	bool posEncodersInitialized;
	unsigned short posEL, posER, posELTarg, posERTarg;
	bool actionMonitorEncoders;

	int updatePos( int el, int er, _timeb *tb );

	// sensors
	int	sonarSkip;		 // tracks when to skip sonar readings
	mapSonarId	sonarId; // sonar ids
	UUID cameraId;				// UUID of the camera sensor
	unsigned char *imageBuf; // buffer for incoming images
	int			  imageBufSize; // size of the buffer
	int			  imageSize;    // size of the current image
	unsigned char imageSeq;     // next image chunk in sequence
	_timeb		  imageTime;	// receive time
	unsigned char decodeRGBBuf[144*176*3]; // rgb buf for decoding
	unsigned char properRGBBuf[144*176*3]; // rgb buf properly oriented
	CJpegDecode	  *imageDecoder; // special jpeg decoding class
	int processSensorData( char *data, _timeb *tb );
	int processImageChunk( unsigned char *data, int len, _timeb *tb );
	int _cameraCommandFinishedImgCount; // static count
	int cameraCommandFinished();
	int cameraCommandFailed();

	// actions
	int queueAction( UUID *director, UUID *thread, int action, void *data, int len );
	int nextAction();
	bool   actionProgressStatus; // 1 == in progress, 0 == waiting to timeout

	bool requestingImage; // are we currently expecting image data

	// networking
	virtual int	 conProcessRaw( spConnection con );

	UUID		 avatarPingWatcher;
	UUID		 avatarMessageTimeout;
	int			 avatarMessageTimeouts; // count consecutive timeouts
	unsigned char messageQueue[AvatarX80H_MESSAGE_QUEUE_SIZE][255+9];
	int			  messageQueueBegin, messageQueueEnd;
	int			 avatarLatencyTicket;
	
	int avatarResetConnection();
	int avatarMessageAcknowledged();
	int avatarProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );
	int avatarQueueMessage( unsigned char message, char *data, unsigned char len, unsigned char target = 1 );
	int avatarNextMessage();
	int avatarPing();

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AvatarX80H )

	// Enumerate callback references
	enum CallbackRef {
		AvatarX80H_CBR_cbWatchAvatarConnection = AvatarBase_CBR_HIGH,
		AvatarX80H_CBR_cbMessageTimeout,
		AvatarX80H_CBR_cbWaitForPing,
		AvatarX80H_CBR_cbActionStep,
		AvatarX80H_CBR_cbRerequestImage,
		AvatarX80H_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool cbWatchAvatarConnection( int evt, void *vpcon );
	bool cbMessageTimeout( void *NA );
	bool cbWaitForPing( void *NA );
	bool cbActionStep( void *vpdata );
	bool cbRerequestImage( void *NA );
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );

// CAvatarX80HDLL: Override ExitInstance for debugging memory leaks
class CAvatarX80HDLL : public CWinApp {
public:
	CAvatarX80HDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};