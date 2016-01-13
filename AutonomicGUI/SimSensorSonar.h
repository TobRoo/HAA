
#define SimSensorSonar_REPEAT_RPFINFO_PERIOD	250 // ms

#ifdef PERFECT_RUN
	#define SimSensorSonar_OBS_DENSITY_RATIO	0.3f  // absolute:density
	#define SimSensorSonar_OBS_DENSITY_AMBIENT 500.8f
#else
	#define SimSensorSonar_OBS_DENSITY_RATIO	0.3f  // absolute:density
	#define SimSensorSonar_OBS_DENSITY_AMBIENT 500.8f
#endif

#define SimSensorSonar_MAP_UPDATE_STRENGTH	0.08f
#define SimSensorSonar_MAP_UPDATE_RATIO	0.5f // absolute:density

#define SimSensorSonar_TEMPLATE_SIZE 30
#define SimSensorSonar_TEMPLATE_SIZE_DIV2 (SimSensorSonar_TEMPLATE_SIZE/2)

class SimSensorSonar : public SimAgentBase {

	typedef std::map<UUID,SonarPose,UUIDless>	 mapPose;
	typedef std::map<UUID,UUID,UUIDless>		 mapAvatar;

	struct PROCESS_READING {
		int id;
		UUID sensor;
		UUID avatar;
		_timeb time;
		int pfNum; // number of particles
		int pfStateSize; // state size
		float *pfState;
		float *pfWeight;
		int retries;
	};

	typedef std::map<int,PROCESS_READING>	mapProcessReading;

public:
	SimSensorSonar( Simulation *sim, Visualize *vis, RandomGenerator *randGen );
	~SimSensorSonar();

	virtual int configure();
	virtual int start();
	virtual int stop();

	int setMap( UUID *id );

	int addReading( UUID *sensor, _timeb *tb );

protected:
	mapPose		sensorPose; // map of sensor poses by id
	mapAvatar	sensorAvatar; // map of sensor avatar (PF) by id

	int nextReadingId;
	mapProcessReading readings;

	UUID mapId;
	float mapResolution;
	int mapHeight;
	int mapDataSize;
	float *mapData;

	// computation arrays
	int	  sharedArraySize;
	float *absoluteOd; // arrays used when processing readings
	float *densityOd;
	float *obsDensity;

	// fImages
	FIMAGE *densityT;  // density template
	FIMAGE *absoluteT; // absolute template
	FIMAGE *integralTarget; // temporary integral rotation buf
	FIMAGE *densityR[ROTATION_DIVISIONS];  // density rotated
	FIMAGE *absoluteR[ROTATION_DIVISIONS]; // absolute rotated
	FIMAGE *mapUpdate; // map update

	int processReading( PROCESS_READING *pr );
	int finishReading( PROCESS_READING *pr, bool success );

	int generateConicTemplates( float d, float max, float sig, float a, float b, FIMAGE *densityT, FIMAGE *absoluteT, float *scale );


public:
	
	DECLARE_CALLBACK_CLASS( SimSensorSonar )

	bool cbRequestPFInfo( void *vpReadingId );
	
};