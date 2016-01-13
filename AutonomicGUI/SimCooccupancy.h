
#define SimCooccupancy_REPEAT_RPFINFO_PERIOD	250 // ms

#define SimCooccupancy_OBS_DENSITY_AMBIENT 500.8f

#define SimCooccupancy_MAP_UPDATE_STRENGTH	0.002f

class SimCooccupancy : public SimAgentBase {

	typedef std::map<UUID,float,UUIDless>	 mapRadius;

	struct PROCESS_READING {
		int id;
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
	SimCooccupancy( Simulation *sim, Visualize *vis, RandomGenerator *randGen );
	~SimCooccupancy();

	virtual int configure();
	virtual int start();
	virtual int stop();

	int setMap( UUID *id );

	int addReading( UUID *avatar, _timeb *tb );

protected:
	mapRadius	avatarRadius; // map of radii by avatar
	
	int nextReadingId;
	mapProcessReading readings;

	UUID mapId;
	float mapResolution;
	int mapHeight;
	int mapDataSize;
	float *mapData;

	// computation arrays
	int	  sharedArraySize;
	float *obsDensity; // arrays used when processing readings

	// fImages
	FIMAGE *mapUpdate; // map update

	int processReading( PROCESS_READING *pr );
	int finishReading( PROCESS_READING *pr, bool success );

public:
	
	DECLARE_CALLBACK_CLASS( SimCooccupancy )

	bool cbRequestPFInfo( void *vpReadingId );
	
};