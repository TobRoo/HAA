

#define SimSensorLandmark_REPEAT_RPFINFO_PERIOD	250 // ms

#define SimSensorLandmark_MAX_LANDMARKS	64 // max number of landmarks to process in a single image

#define SimSensorLandmark_OBS_DENSITY_AMBIENT		10.0f
#define SimSensorLandmark_OBS_DENSITY_AMBIENT_OTHER 60.0f  // controls how much we want to make adjustments to other particle filters

#define SimSensorLandmark_OBS_SIGMA_X			0.1f // ~1/2 expected error in m
#define SimSensorLandmark_OBS_SIGMA_Y			0.05f // ~1/2 expected error in m at a distance of 1 m

class SimSensorLandmark : public SimAgentBase {
	
	typedef std::map<UUID,CameraPose,UUIDless>	 mapPose;
	typedef std::map<UUID,UUID,UUIDless>		 mapAvatar;
	typedef std::map<char,DDBLandmark>			 mapLandmark;

	struct LANDMARK_HIT {
		char code;
		float D, A; // distance, angle

		DDBLandmark info;
		int pfNum;
		int pfStateSize;
		float *pfState;
		float *pfWeight;
	};

	struct PROCESS_READING {
		int id;
		UUID sensor;
		UUID avatar;
		_timeb time;
		int landmarkCount;
		LANDMARK_HIT landmark[SimSensorLandmark_MAX_LANDMARKS];
		int pfNum; // number of particles
		int pfStateSize; // state size
		float *pfState;
		float *pfWeight;
		int retries;
		int waitingForPF; // waiting on this many pfs (us + landmarks)
	};

	typedef std::map<int,PROCESS_READING>	mapProcessReading;

public:
	SimSensorLandmark( Simulation *sim, Visualize *vis, RandomGenerator *randGen );
	~SimSensorLandmark();

	virtual int configure();
	virtual int start();
	virtual int stop();

	int addReading( UUID *sensor, _timeb *tb );

protected:
	mapPose		sensorPose; // map of sensor poses by id
	mapAvatar	sensorAvatar; // map of sensor avatar (PF) by id
	mapLandmark landmarks; // map of landmarks by code

	int nextReadingId;
	mapProcessReading readings;

	// computation arrays
	int	  obsDensitySize;
	int	  obsDensitySize2;
	float *obsDensity;
	float *obsDensity2;

	int processReading( PROCESS_READING *pr );
	int finishReading( PROCESS_READING *pr, bool success );

public:
	
	DECLARE_CALLBACK_CLASS( SimSensorLandmark )

	bool cbRequestPFInfo( void *vpReadingId );
	
};