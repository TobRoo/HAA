
#define SimPathPlanner_REPEAT_RPFINFO_PERIOD	250 // ms

#define CELLWEIGHT_AMBIENT	0.04f // WARNING! if this is set too low then updateWeightedPathLength can take a loooong time (0.04f seems to be a lower bound)
#define CELLWEIGHT(v) (CELLWEIGHT_AMBIENT + (1-(v))*(1-(v)))

#define MAX_CELL_PATH_LENGTH	80  // maximum number of cell steps to take when planning paths, things can get very slow if this is too big

#define NOPATH_GIVEUP_COUNT 5 // give up after n tries
#define NOPATH_DELAY	0.5f // seconds

#define ACTION_HORIZON 3 // send up to n actions at a time

#define LINEAR_BOUND		0.1f // m, have to be this close on the X and Y axes
#define ROTATIONAL_BOUND	0.1f // rad, have to be this close on the R axis (if useRotation)

class SimAvatar;

class SimPathPlanner : public SimAgentBase {
	
	struct TESTCELL {
		int x;
		int y;
		int pathLength;
		float wPathLength;
		char fromDir;
	};

	struct WAYPOINT {
		float x;
		float y;
	};

	struct ActionPair {
		int action;
		float val;
	};

public:
	SimPathPlanner( Simulation *sim, Visualize *vis, RandomGenerator *randGen );
	~SimPathPlanner();

	virtual int configure();
	virtual int start();
	virtual int stop();
	virtual int step();

	int configureParameters( DataStream *ds, SimAvatar *avatar );

	int	setTarget( float x, float y, float r, char useRotation );
	
private:

	UUID ownerId;
	UUID mapId;
	UUID regionId;
	UUID pfId;

	SimAvatar *avatar; // avatar object

	float maxLinear;   // maximum distance per move
	float maxRotation; // maximum distance per rotation

	DDBRegion missionRegion; 

	float mapResolution;
	int mapOffset[2]; // map offset in cells
	int mapWidth;
	int mapHeight;
	float *mapData;
	float *mapBuf;

	float *pfState;
	
	bool haveTarget;
	float targetX;
	float targetY;
	float targetR;
	char  useRotation;

	int noPathCount; // how many times did we fail to find a path

	int goTarget();
	int checkArrival();
	int giveUp( int reason );

	std::multimap<float,TESTCELL*>  frontier;
	std::map<int,TESTCELL*>			cellListed;
	int dir[4][2];
	int fromLookUp[4];

	int processMap();
	int updateWeightedPathLength( TESTCELL *cell, std::map<int,TESTCELL*> *cellListed );
	int nextCell( TESTCELL *testCell, float targetX, float targetY, std::map<int,TESTCELL*> *cellListed, std::multimap<float,TESTCELL*> *frontier, TESTCELL *bestCell, float *bestDistSq );
	float calcWeightedPathLength( int cellX, int cellY, float startX, float startY, float endX, float endY, std::map<int,TESTCELL*> *cellListed );
	int shortestPath( int *baseCellX, int *baseCellY, float *baseX, float *baseY, int destCellX, int destCellY, float destX, float destY, std::map<int,TESTCELL*> *cellListed );
	int planPath();

	int actionsSent; // how many actions are we waiting for
	int actionConv; // action conversation id

public:
	
	DECLARE_CALLBACK_CLASS( SimPathPlanner )

	bool cbActionFinished( void *vpDS );
	
};