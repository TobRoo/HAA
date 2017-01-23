#ifndef __DDB_H__
#define __DDB_H__

#include <list>
#include <map>
#include <vector>

#include <sys/types.h>
#include <sys/timeb.h>


#ifndef UUIDlessDefined
// UUIDless comparison function
class UUIDless {
public:
	bool operator()(const UUID& _Left, const UUID& _Right) const
		{	// apply operator< to operands
			return memcmp( &_Left, &_Right, sizeof(UUID) ) < 0; 
		}
};
#define UUIDlessDefined
#endif



#define DDB_REQUEST_TIMEOUT		30000 // ms

// Definition of possible task types
enum ITEM_TYPES {
	NON_COLLECTABLE = 0,
	LIGHT_ITEM = 1,
	HEAVY_ITEM = 2,
	TYPE_PENDING = 3,	//Used as a placeholder when the exact type is not yet known
};

enum DATATYPES {
	DDB_INVALID				= 0,
	DDB_AGENT				= 0x0001 << 0,
	DDB_REGION				= 0x0001 << 1,
	DDB_LANDMARK			= 0x0001 << 2,
	DDB_MAP_PROBOCCGRID		= 0x0001 << 3,
	DDB_PARTICLEFILTER		= 0x0001 << 4,
	DDB_AVATAR				= 0x0001 << 5,
	DDB_SENSOR_SONAR		= 0x0001 << 6,
	DDB_SENSOR_CAMERA		= 0x0001 << 7,
	DDB_SENSOR_SIM_CAMERA	= 0x0001 << 8,
	DDB_HOST_GROUP			= 0x0001 << 9,
	DDB_TASK				= 0x0001 << 10,
	DDB_TASKDATA			= 0x0001 << 11
};
#define DDB_SENSORS	( DDB_SENSOR_SONAR | DDB_SENSOR_CAMERA | DDB_SENSOR_SIM_CAMERA )

enum DDBEVENT {
	DDBE_WATCH_TYPE = -2, // confirmation on watcher registration
	DDBE_WATCH_ITEM = -1, // confirmation on watcher registration
	DDBE_ADD = 0,
	DDBE_REM,
	DDBE_UPDATE,
	DDBE_HIGH,
};

enum DDBRESPONSE {
	DDBR_OK = 0,
	DDBR_ACKNOWLEDGE,
	DDBR_ABORT,
	DDBR_INTERNALERROR,
	DDBR_NOTFOUND,
	DDBR_INVALIDARGS,
	DDBR_TOOEARLY,
	DDBR_TOOLATE,
	DDBR_BADRANGE,
};

typedef std::map<UUID, int, UUIDless> mapDataAge;

typedef std::map<int, std::list<UUID> *> mapDDBWatchers;
typedef std::map<UUID, std::list<UUID> *, UUIDless> mapDDBItemWatchers;

static int DDB_TILE_ID( short u, short v) { // convert tile coordinate into id (valid for coords -0x8FFF to 0x8FFF)
	int id;
	unsigned short a, b;
	a = u + 0x8FFF;
	b = v + 0x8FFF;
	memcpy( (void*)&id, (void*)&a, sizeof(short) );
	memcpy( ((byte*)&id) + sizeof(short), (void*)&b, sizeof(short) );
	return id;
}

static void DDB_REVERSE_TILE_ID( int id, short *u, short *v ) { // convert tile id into coordinate (from -0x8FFF to 0x8FFF)
	unsigned short a, b;
	memcpy( (void *)&a, (void *)&id, sizeof(short) );
	memcpy( (void *)&b, (byte *)&id + sizeof(short), sizeof(short) );
	*u = a - 0x8FFF;
	*v = b - 0x8FFF;
}

// -- DDB_AGENT -----------------------------------------

enum DDBEVENT_AGENT {
	DDBE_AGENT_UPDATE = DDBE_HIGH,	// data = [int infoFlags]
	DDBE_VIS_ADDPATH,				// data = [int id]
	DDBE_VIS_REMPATH,				// data = [int id]
	DDBE_VIS_UPDATEPATH,			// data = [int id, char msg], originating message id is included to tell what update happened
	DDBE_VIS_ADDOBJECT,				// data = [int id]
	DDBE_VIS_REMOBJECT,				// data = [int id]
	DDBE_VIS_UPDATEOBJECT			// data = [int id, char msg], originating message id is included to tell what update happened
};

struct DDBVis_NODE {
	float x, y;
};

struct DDBVis_PATH_REFERENCE {
	int id;
	float r, g, b;
	float lineWidth;
	int stipple;
};

struct DDBVis_OBJECT {
	char name[256];
	bool solid; // is the object solid for simulation purposes
	bool visible; // is the object visible
	float x, y, r, s;
	std::list<DDBVis_PATH_REFERENCE> path_refs;
};

struct DDBAgent_MSG {
	unsigned char msg;
	char *data;
	unsigned int len;

	UUID ticket;
};

struct DDBAgent {
	char agentTypeName[256];
	UUID agentTypeId;
	char agentInstance;

	UUID			host;		  // current host
	UUID			hostTicket;   // used to prevent confusion in host change acknowledgement messages

	int			    status;
	UUID			statusTicket; // used to prevent confusion in status acknowledgement messages

	// spawn data
	UUID parent;
	UUID spawnThread; // request spawn conversation
	float spawnAffinity; // affinity to parent for first spawn
	char priority; // allocation priority
	float spawnProcessCost; // process cost for first spawn

	int activationMode;

	// process cost and affinity info
	_timeb spawnTime;
	std::map<UUID, unsigned int, UUIDless> affinityData; // data sent to agent
	float weightedUsage; // used cpu time in millis weighted by capacity

	// State/Transfer
	_timeb			stateTime;
	unsigned int	stateSize;
	void		   *stateData;
	
	_timeb			backupTime;
	unsigned int	backupSize;
	void		   *backupData;

	std::list<DDBAgent_MSG>	msgQueuePrimary;
	std::list<DDBAgent_MSG>	msgQueueSecondary;

	// GUI/Debug
	std::map<int,std::list<DDBVis_NODE> *> *visPaths;
	std::map<int,DDBVis_OBJECT *> *visObjects;
};

typedef std::map<UUID, DDBAgent *, UUIDless> mapDDBAgent;

enum DDBAGENTINFO_GET {
	DDBAGENTINFO_RTYPE = 0x0001 << 0, // data = [String name, UUID agentTypeId, char agentInstance]
	DDBAGENTINFO_RPARENT = 0x0001 << 1, // data = [UUID parentId]
	DDBAGENTINFO_RSPAWNINFO = 0x0001 << 2, // data = [UUID spawnThread, int activationMode, char priority]
	DDBAGENTINFO_RHOST = 0x0001 << 3, // data = [UUID host, UUID hostTicket]
	DDBAGENTINFO_RSTATUS = 0x0001 << 4, // data = [int status, UUID statusTicket]
	DDBAGENTINFO_RAFFINITY = 0x0001 << 5, // data = [bool true, UUID affinity, float strength, ..., bool false]
	DDBAGENTINFO_RPROCESSCOST = 0x0001 << 6, // data = [float processCost]
	DDBAGENTINFO_RSTATE = 0x0001 << 7, // data = [_timeb stateTime, unsigned int stateSize, stateData...]
	DDBAGENTINFO_RMSG_QUEUES = 0x0001 << 8, // data = [<bool 1, unsigned char msg, unsigned int len, data..., ...,> bool 0], primary and secondary queues are packed together
	DDBAGENTINFO_RBACKUP = 0x0001 << 9, // data = [_timeb backupTime, unsigned int backupSize, backupData...]
	DDBAGENTINFO_RLIST	= 0x0001 << 10, // data = [int agentCount, String name, UUID agentTypeId, char agentInstance, UUID parentId <for all agents>]
};

enum DDBAGENTINFO_SET {
	DDBAGENTINFO_HOST = 0x0001 << 0, // data = [UUID host, UUID hostTicket]
	DDBAGENTINFO_STATUS = 0x0001 << 1, // data = [int status, UUID statusTicket]
	DDBAGENTINFO_SPAWN_DATA = 0x0001 << 2, // data = [UUID parent, float spawnAffinity, float spawnProcessCost]
	DDBAGENTINFO_SPAWN_TIME = 0x0001 << 3, // data = [_timeb spawnTime]
	DDBAGENTINFO_AFFINITY_UPDATE = 0x0001 << 4, // data = [UUID affinity, unsigned int size]
	DDBAGENTINFO_PROCESSCOST_UPDATE = 0x0001 << 5, // data = [float weightedUsage]
	DDBAGENTINFO_STATE = 0x0001 << 6, // data = [_timeb stateTime, unsigned int stateSize, stateData...]
	DDBAGENTINFO_STATE_CLEAR = 0x0001 << 7, // clear state, data = []
	DDBAGENTINFO_QUEUE_CLEAR = 0x0001 << 8, // clear message queues, data = []
	DDBAGENTINFO_QUEUE_MERGE = 0x0001 << 9, // merge message queues, data = []
	DDBAGENTINFO_QUEUE_MSG = 0x0001 << 10, // data = [char primary, unsigned char msg, unsigned int len, data...], primary is used to flag which queue to add to
	DDBAGENTINFO_BACKUP = 0x0001 << 11, // data = [_timeb backupTime, unsigned int backupSize, backupData...]
};

enum DDBAGENT_STATUS {
	DDBAGENT_STATUS_ERROR = -1,
	DDBAGENT_STATUS_WAITING_SPAWN = 0,
	DDBAGENT_STATUS_SPAWNING,
	DDBAGENT_STATUS_READY,
	DDBAGENT_STATUS_FREEZING, // freeze process started
	DDBAGENT_STATUS_FROZEN, // frozen for agent transfer
	DDBAGENT_STATUS_THAWING, // thaw process started	
	DDBAGENT_STATUS_FAILED,
	DDBAGENT_STATUS_RECOVERING, // recovery process started
	DDBAGENT_STATUS_ABORT, // agent is no longer wanted
};

enum DDBAGENT_PRIORITY {
	DDBAGENT_PRIORITY_CRITICAL = 0,
	DDBAGENT_PRIORITY_REDUNDANT_HIGH,
	DDBAGENT_PRIORITY_REDUNDANT_LOW,
	DDBAGENT_PRIORITY_COUNT,
};

enum DDBVISOBJECTINFO {
	DDBVISOBJECTINFO_NAME = 0x0001 << 0,  // data = [string name]
	DDBVISOBJECTINFO_POSE = 0x0001 << 1,  // data = [float x, float y, float r, float s]
	DDBVISOBJECTINFO_EXTRA = 0x0001 << 2, // data = [bool solid, bool visible]
	DDBVISOBJECTINFO_PATHS = 0x0001 << 3, // data = [int count<, DDBVis_PATH_REFERENCE pathRef, ...>]
};

// -- DDB_REGION ----------------------------------------------

struct DDBRegion {
	float x;
	float y;
	float w;
	float h;
};

typedef std::map<UUID, DDBRegion *, UUIDless> mapDDBRegion;

// -- DDB_LANDMARK --------------------------------------------

struct DDBLandmark {
	unsigned char code;		// barcode
	UUID owner;		// Avatar Id or NULL
	float height;	// height of the landmark
	float elevation;// elevation from ground
	float x, y;		// offset from avatar or map 0,0
	bool posInitialized;
	bool estimatedPos; // position is being estimated via Kalman filter
	float P;   // error covariance

	bool collected; // simulate collection

	float trueX, trueY; // true position if it is known (only for debugging)

	ITEM_TYPES landmarkType;

};

typedef std::map<UUID, DDBLandmark *, UUIDless> mapDDBLandmark;

enum DDBLANDMARKINFO_SET {
	DDBLANDMARKINFO_POS = 0x0001 << 0,		// data = [int estNum, float R, float estimates..., float weights...]
	DDBLANDMARKINFO_COLLECTED = 0x0001 << 1, // data = N/A
};

// -- DDB_MAP_PROBOCCGRID -------------------------------------

enum DDBEVENT_POG {
	DDBE_POG_UPDATE = DDBE_HIGH, // data = [float x, float y, float w, float h]
	DDBE_POG_LOADREGION,		 // data = [float x, float y, float w, float h]
};

typedef std::map<int, float*> mapDDBPOGTile;

struct DDBProbabilisticOccupancyGrid {
	mapDataAge *age;
	float tileSize;   // size of a tile in m
	float resolution; // resolution in m
	int stride;		  // number of cells on a side
	mapDDBPOGTile *tiles;
	float xlow, xhigh, ylow, yhigh; // current bounds on the map
	// extra layer for debugging
	mapDDBPOGTile *fow; // fog of war mask
	mapDDBPOGTile *blackhole; // layer is writen to but never read (except when dumping)
};

typedef std::map<UUID, DDBProbabilisticOccupancyGrid *, UUIDless> mapDDBPOG;


// -- DDB_PARTICLEFILTER --------------------------------------

enum DDBEVENT_PF {
	DDBE_PF_RESAMPLE = DDBE_HIGH,	// data = [_timeb tb]
	DDBE_PF_PREDICTION,				// data = [_timeb tb, char nochange]
	DDBE_PF_CORRECTION,				// data = [_timeb tb]
};

struct DDBParticle {
	int index;
	float weight;
	float obsDensity;
	float obsDensityForward;
	std::list<DDBParticle*>::iterator parent;
};

struct DDBParticleRegion {
	mapDataAge *age; // age of the particle filter when this region was created
	bool depreciated; // regions are depreciated (i.e. removed from active duty) after a certain amount of time to improve performance for enumeration, they are kept in memory for debugging purposes
	_timeb startTime;
	int index;
	bool weightsDirty; // have our weights been calculated since the last obsDensity update
	std::list<DDBParticle*> *particles;
	std::list<_timeb*> *times;
	std::list<float*> *states;
};

struct DDBParticleFilter_Correction {
	int regionAge; // how many regions existed when this correction was submitted
	_timeb tb;
	float *obsDensity;
};

struct DDBParticleFilter {
	UUID owner;
	mapDataAge *age;
	int regionCount;
	int depreciatedCount; // number of depreciated regions
	unsigned int particleNum;
	unsigned int stateSize; // how many floats per state?
	float *stateCurrent;
	bool currentDirty; // does currentState need to be recalculated?
	bool predictionHeld; // was the latest prediction a "no change?"
	std::list<DDBParticleRegion*> *regions;
	int forwardMarker;

	// Logging
	int obsSinceLastWeightUpdate; // counts the observations applied since the last weight update
	int totalObs; // count all observations applied
};

typedef std::map<UUID, DDBParticleFilter *, UUIDless> mapDDBParticleFilter;

enum DDBPFINFO {
	DDBPFINFO_USECURRENTTIME = 0x0001 << 0, // data = [_timeb time], use if you just want the most recent data
	DDBPFINFO_NUM_PARTICLES	= 0x0001 << 1, // data = [int num particles]
	DDBPFINFO_STATE_SIZE	= 0x0001 << 2, // data = [int state size]
	DDBPFINFO_STATE			= 0x0001 << 3, // data = [... states]
	DDBPFINFO_WEIGHT		= 0x0001 << 4, // data = [... weight]
	DDBPFINFO_CURRENT		= 0x0001 << 5, // data = [_timeb time, ... state]
	DDBPFINFO_MEAN			= 0x0001 << 6, // data = [... state]
	DDBPFINFO_OWNER			= 0x0001 << 7, // data = [UUID owner]
	DDBPFINFO_EFFECTIVE_NUM_PARTICLES = 0x0001 << 8, // data = [float effective num particles]
	DDBPFINFO_DEBUG			= 0x0001 << 9, // special flag used for debugging
};

#define DDBPF_REGIONDEPRECIATIONTIME	(5*60*1000) // ms

// -- DDB_AVATAR --------------------------------------------

enum DDBEVENT_AVATAR {
	DDBE_AVATAR_PLACEHOLDER = DDBE_HIGH,	// placeholder
};

#define DDBAVATAR_MAXCARGO 10

struct DDBAvatar {
	int age;
	char type[64];
	int status;
	UUID agent;
	UUID agentTypeId;
	int  agentTypeInstance;
	UUID pf;
	float innerRadius; // m
	float outerRadius; // m

	int sensorTypes; // installed sensor types

	UUID controller; // agent currently in control of setting targets
	int  controllerIndex; // count how many controller changes have occured
	int  controllerPriority;

	int capacity; // cargo capacity
	unsigned char cargo[DDBAVATAR_MAXCARGO]; // cargo ids, -1 is unset
	int cargoCount; // current cargo count

	char retired; // 0 - active, 1 - retired, 2 - crashed (i.e. still physically present)
	_timeb startTime;
	_timeb endTime;
};

typedef std::map<UUID, DDBAvatar *, UUIDless> mapDDBAvatar;

enum DDBAVATARINFO_GET {
	DDBAVATARINFO_ENUM		= 0x0001 << 0, // data = [int count, UUID avatar, string type, UUID agent, UUID agentType, int instance ...], flag only used by itself
	DDBAVATARINFO_RTYPE		= 0x0001 << 1, // data = [string type]
	DDBAVATARINFO_RSTATUS	= 0x0001 << 2, // data = [int status]
	DDBAVATARINFO_RAGENT		= 0x0001 << 3, // data = [UUID agent]
	DDBAVATARINFO_RPF		= 0x0001 << 4, // data = [UUID pf]
	DDBAVATARINFO_RRADII		= 0x0001 << 5, // data = [float innerRadius, float outerRadius]
	DDBAVATARINFO_RCONTROLLER = 0x0001 << 6, // data = [UUID controller, int index, int priority]
	DDBAVATARINFO_RSENSORTYPES = 0x0001 << 7, // data = [int sensorTypes]
	DDBAVATARINFO_RCAPACITY = 0x0001 << 8,  // data = [int capacity]
	DDBAVATARINFO_RCARGO = 0x0001 << 9,		// data = [int cargoCount, char cargo, ...]
	DDBAVATARINFO_RTIMECARD  = 0x0001 << 10, // data = [_timeb startTime, char retired <, endTime>]
};

enum DDBAVATARINFO_SET {
	DDBAVATARINFO_STATUS	 = 0x0001 << 0, // data = [int status]
	DDBAVATARINFO_CONTROLLER = 0x0001 << 1, // data = [UUID controller, int priority]
	DDBAVATARINFO_CONTROLLER_RELEASE = 0x0001 << 2, // data = [UUID agent]
	DDBAVATARINFO_CARGO		 = 0x0001 << 3, // data = [int count, bool load, unsigned char code, ...]
	DDBAVATARINFO_RETIRE	 = 0x0001 << 4, // data = [char status, _timeb endTime]
};

enum DDBAVATAR_CONTROLLERPRIORITY {
	DDBAVATAR_CP_HIGH = 0,
	DDBAVATAR_CP_MEDIUM,
	DDBAVATAR_CP_LOW,
	DDBAVATAR_CP_IDLE,
	DDBAVATAR_CP_UNSET,
};

// -- DDB_SENSOR_* --------------------------------------------

enum DDBEVENT_SENSOR {
	DDBE_SENSOR_UPDATE = DDBE_HIGH,	// data = [_timeb tb]
};

struct DDBSensorData {
	_timeb t;
	int readingSize;
	void *reading; // sensor specific struct
	int dataSize;
	void *data; // sensor specific extra data
};

struct DDBSensor {
	int age;
	int type;
	UUID avatar;
	UUID pf;
	void *pose;
	int poseSize;
	std::list<DDBSensorData *>	*data;
};

typedef std::map<UUID, DDBSensor *, UUIDless> mapDDBSensor;

enum DDBSENSORINFO {
	DDBSENSORINFO_TYPE		= 0x0001 << 0, // data = [int type]
	DDBSENSORINFO_AVATAR	= 0x0001 << 1, // data = [UUID avatar]
	DDBSENSORINFO_PF		= 0x0001 << 2, // data = [UUID pf]
	DDBSENSORINFO_POSE		= 0x0001 << 3, // data = [int poseSize, ... pose]
	DDBSENSORINFO_READINGCOUNT = 0x0001 << 4, // data = [int readingCount]
};

// -- DDB_SENSOR_SONAR ----------------------------------------
struct SonarReading {
	float value;
};

struct SonarPose {
	float x, y, r;
	float max;		// max range
	float sigma;	// relative to meters
	float alpha, beta; // falloff angles
};

// -- DDB_SENSOR_CAMERA/DDB_SENSOR_SIM_CAMERA -----------------
#define IMAGEFORMAT_SIZE	8
struct CameraReading {
	short w, h;
	char format[IMAGEFORMAT_SIZE];
};

struct CameraPose {
	float x, y, z, r;
	float sigma; // uncertainty for camera readings (relative to meters)
	float horizon; // fraction from the bottom of the image to the horizon line
	int frameH; // frame height used when generating the homography transform
	float H[9]; // homography matrix (floor to image)
	float G[9]; // inverse homography (image to floor)
	float planeD; // distance to the wall plane used for J and I
	float J[9]; // homography matrix (wall to image)
	float I[9]; // inverse homography (image to wall)
};

enum SimCamera_DATA {
	SimCamera_DATA_END = 0,  // end of stream
	SimCamera_DATA_LANDMARK, // data format: unsigned char code, float distance, float angle
	SimCamera_DATA_FLOORFINDER, // data format: float halfFOV, 10x float hitDistance (-1 means miss)
};

// -- DDB_TASKS ---------------------------------------

// Definition of each task
struct DDBTask {
	UUID landmarkUUID;		//The UUID of the landmark to be collected
	UUID agentUUID;			//The UUID of the assigned -team learning- agent
	UUID avatar;			// Assigned avatar
	bool completed;			// Task status
	ITEM_TYPES type;        // Non-collectible (terrain or avatar landmark), light item, or heavy item
};

typedef std::map<UUID, DDBTask *, UUIDless> mapTask;
typedef std::map<UUID, std::pair<DDBTask *, _timeb>, UUIDless> mapDDBTask;
//typedef std::map<UUID, DDBTask *, UUIDless> mapDDBTask;

// Individual task allocation data, to be shared with other avatars

struct DDBTaskData {
	UUID taskId;								   // UUID for the assigned task
	UUID agentId;								   // UUID for the agent (avatarId is the key)
	std::map<UUID, float, UUIDless> tau;           // Average trial time for each task
	std::map<UUID, float, UUIDless> motivation;    // Motivation values for each task
	std::map<UUID, float, UUIDless> impatience;    // Impatience values for each task
	std::map<UUID, int, UUIDless> attempts;        // Number of attempts at each task
	std::map<UUID, float, UUIDless> mean;          // Mean task time
	std::map<UUID, float, UUIDless> stddev;        // Standard deviation of task time
	int psi;                                       // Time on task
	_timeb updateTime;                             // Time of most recent update to DDB
	int round_number;                              // Task allocation round
};

typedef std::pair<char *, size_t> storagePair;

typedef std::map<UUID, storagePair, UUIDless> mapDDBTaskData;

//Individual Q-Learning data (only stored at the end of a run, to be used in the next run)
 
struct QLStorage {
	std::vector<float> qTable;
	std::vector<unsigned int> expTable;
	long long totalActions;
	long long usefulActions;
};

typedef std::map<char, QLStorage> mapDDBQLearningData;

//Advice data (only stored at the end of a run, to be used in the next run)

struct AdviceStorage {
	float cq;
	float bq;
};

typedef std::map<char, AdviceStorage> mapDDBAdviceData;



#endif