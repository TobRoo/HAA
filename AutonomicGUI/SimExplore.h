
#define SimExplore_MAX_SECTIONS 255 // because we store section and avatar ids in chars

#define SimExplore_THRESHHOLD_UNOCCUPIED	0.75f
#define SimExplore_THRESHHOLD_OCCUPIED		0.25f
#define SimExplore_THRESHHOLD_ACTIVITY		5.0f

#define SimExplore_UPDATE_RATE		4000	// ms
#define SimExplore_UPDATE_RATE_MIN	1000	// ms

#define SimExplore_MAX_SIMULTANEOUS_UPDATES	255 

class SimAvatar;

class SimExplore : public SimAgentBase {

	typedef std::map<UUID,DDBRegion,UUIDless>	 mapRegions;

	struct CELLCOORD {
		float x, y;
	};

	struct CELL {
		char type;
		float occupancy;
		float activity;
		unsigned char section; // current section id
		unsigned char partition;  // current partition id
		bool dirty;
	};

	enum CELL_TYPE {
		CT_OUTOFBOUNDS = -3,
		CT_UNREACHABLE = -2,
		CT_UNKNOWN = -1,
		CT_UNOCCUPIED = 0,
		CT_OCCUPIED
	};

	enum CELL_EXIT {
		CE_UP =		0x01 << 0,
		CE_DOWN =	0x01 << 1,
		CE_LEFT =	0x01 << 2,
		CE_RIGHT =	0x01 << 3
	};

	typedef std::map<int, CELL *> mapTile;

	struct AVATAR {
		char type[64];
		float pfState[3];
		unsigned char section; // section id
		unsigned char partition; // partition id
		bool searchDirty; // search cell needs updating
		CELLCOORD search; // current search cell
		std::list<CELLCOORD> cells; // currently assigned cells

		SimAvatar *agent;
		int thread; // used to identify avatars in callbacks

		CELLCOORD target;
		float objR, objS; // used for dynamic object
		int targetObj, searchObj; // object ids

	};

	typedef std::map<UUID, AVATAR, UUIDless> mapAvatar;
	
	typedef std::map<int, AVATAR*> mapAvatarThread;

public:
	SimExplore( Simulation *sim, Visualize *vis, RandomGenerator *randGen );
	~SimExplore();

	virtual int configure();
	virtual int start();
	virtual int stop();

	int addRegion( UUID *id );
	int addMap( UUID *id );
	int addAvatar( UUID *id, char *agentType, SimAvatar *agent );	// register avatar
	int notifyMap( UUID *item, char evt, char *data, int len );
	
protected:
	mapRegions regions;  // list of regions to explore
	int _addRegion( UUID *id, DDBRegion *region );
	int initializeRegion( UUID *id );

	mapTile tiles; // map of cell tiles
	std::list<CELLCOORD> activeCells; // list of active cells
	int nextSectionId;
	int totalCells; // total cell count for all regions NOTE regions should not overlap!
	std::list<CELLCOORD> *sections[SimExplore_MAX_SECTIONS]; // list of cells by section
	bool sectionDirty[SimExplore_MAX_SECTIONS]; //  section probably needs to be repartitioned
	std::list<int> *sectionIds; // list of current section ids
	CELL * getCell( float x, float y, bool useRound = true );
	int getNewSectionId(); // get new section id
	int floodSection( float x, float y, std::list<CELLCOORD> *unlisted );
	int _floodSectionRecurse( float x, float y, int id, std::list<CELLCOORD> *section, std::list<CELLCOORD> *unlisted );

	bool mapReady;
	UUID mapId;
	float mapTileSize;
	float mapResolution;
	int mapStride;
	std::list<DDBRegion> dirtyMapRegions; // list of dirty map regions

	int updateMap( float x, float y, float w, float h, float *data );

	
	mapAvatar	avatars; // list of assigned avatars
	int nextAvatarThread;
	mapAvatarThread avatarThreads; // map of avatars by thread
	int nextSearchCell( UUID *uuid );

	_timeb lastPartitionUpdate; // records last update time
	int	partitionUpdateTimer; // id for the partition update timer

	int	   sharedArrayGaussSize; // shared arrays for partitioning sections
	float *sharedArrayGauss;
	int	   sharedArrayPixSize;
	float *sharedArrayPix;

	int avatarUpdateSection( UUID *id, float x, float y ); // update avatar section with new pos

	int updatePartitions( bool force = 0 ); // update sections and partitions
	int _updatePartitions(); // data is gathered, do the update
	int calculatePartitions( unsigned char sectionId ); // divide sections into partitions

	int dirtyRegion( DDBRegion *region );
	int dirtyMap( DDBRegion *region );
	int _dirtyMap( DDBRegion *region, std::list<DDBRegion>::iterator iter, std::list<DDBRegion>::iterator );

	std::list<int> cellVisualization; // list of cell objects
	int cellPath; 
	int visualizeSections();
	
public:
	
	DECLARE_CALLBACK_CLASS( SimExplore )

	bool cbPartitionUpdateTimeout( void *NA );
	bool cbTargetPos( void *pDS );
	
};