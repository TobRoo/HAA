
#pragma once
#include "afxwin.h"

#include <gl/gl.h>
#include <gl/glu.h>

#include "..\RoboRealmAPI\RR_API.h"

#include <sys/timeb.h>
#include <list>
#include <map>

//#define PERFECT_RUN		 // used to do things like perfect particle filter estimates and not update weights

#define fM_PI (float)M_PI
#define fM_PI_2 (float)M_PI_2
#define fM_PI_4 (float)M_PI_4

struct FIMAGE;

//#define NUM_PARTICLES 100
//#define MIN_EFF_PARTICLES	20
//#define OBS_DENSITY_RATIO	0.3f  // absolute:density
//#define OBS_DENSITY_AMBIENT 15.8f

#define PREDICTION_PERIOD 500
#define SENSOR_DELAY	500

struct Particle {
	float weight;
	float obsDensity;
	float obsDensityForward;
	float absoluteOd;
	float densityOd;
	std::list<Particle*>::iterator parent;
};

struct ParticleRegion {
	_timeb startTime;
	unsigned int index;
	std::list<Particle*> *particles;
	std::list<_timeb*> *times;
	std::list<std::list<void*>*> *states;
};

struct ParticleFilter {
	unsigned int regionCount;
	unsigned int particleNum;
	std::list<ParticleRegion*> *regions;
	unsigned int forwardMarker;
};

ParticleFilter * CreateFilter( int particleNum, _timeb *startTime, void * (*genState)(int index, void *genParams), void *genParams );
void FreeFilter( ParticleFilter *pf );
int resampleParticleFilter( ParticleFilter *pf, void * (*copyState)( void *vpstate ) );

struct ParticleState_Avatar {
	float x, y, r;
};

void * generateParticleState_Avatar( int index, void *genParams );
void * copyParticleState_Avatar( void *vpstate );
int predictPF_Avatar( ParticleFilter *pf, unsigned int dtime, float forwardV, float tangentialV, float rotationalV );

/*
#define IMAGEFORMAT_SIZE	8
struct CameraReading {
	_timeb tb;
	short w, h;
	char format[IMAGEFORMAT_SIZE];
	char *data;
	int size;
};

struct CameraPose {
	float x, y, r;
	float sigma; // uncertainty for camera readings (relative to meters)
	float horizon; // fraction from the bottom of the image to the horizon line
	int frameH; // frame height used when generating the homography transform
	float H[9]; // homography matrix (floor to image)
	float G[9]; // inverse homography (image to floor)
	
	// RoboRealm 
	RR_API *rr_FloorFinder;
	int		rr_FloorFinderPort;
	int		rr_FloorFinderHorizon;
};*/

#define FLOORFINDER_SCALE	0.1f

//int generateCameraTemplate( CameraReading *reading, CameraPose *pose, FIMAGE *cameraT, FIMAGE *cameraContours );

#define SONAR_RANGE_MAX 3.0f
#define SONAR_RANGE_MIN 0.05f

#define SONAR_SIG_EST	0.09f
#define SONAR_A_EST		(fM_PI*10/180.0f) // 10 deg
#define SONAR_B_EST		(fM_PI*5/180.0f) // 5 deg

int generateConicTemplates( float d, float max, float var, float a, float b, FIMAGE *densityT, FIMAGE *absoluteT, float *scale ); // generate conic templates

#define AVATAR_INIT_LINEAR_SIG		0.001f
#define AVATAR_INIT_ROTATIONAL_SIG	0.001f
#define AVATAR_LINEARV_SIG_EST		0.00002f  // m/ms
#define AVATAR_ROTATIONALV_SIG_EST	0.00002f  // rad/ms

#define AVATAR_MAX_CAMERAS	1
#define AVATAR_MAX_SONAR	8

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

struct VisParticleFilter {
	UUID id;
	_timeb tb;
	int particleNum;
	int stateSize;
	float *state;
	float *scale;
	int *objId;
	float avgState[3];
	int avgObjId;
	float trueState[3]; // record the true state at the time of this prediction
	int trueObjId;
};

typedef std::map<UUID,VisParticleFilter,UUIDless> mapVisParticleFilter;

class SimAvatar;
class SimSLAM;
class SimExplore;

typedef std::map<UUID,SimAvatar *,UUIDless> mapSimAvatar;

/*
struct Avatar {
	int object;
	ParticleFilter *filter;
	float t, x, y, r, s;
	float nextT, nextX, nextY, nextR; // next pose read from the file
	int nextPrediction;
	float lastT, lastX, lastY, lastR; // used for particle predictions
	float particleX[NUM_PARTICLES], particleY[NUM_PARTICLES], particleR[NUM_PARTICLES], particleS[NUM_PARTICLES];
	int particleObj[NUM_PARTICLES];
	
	FILE *poseF;
	int cameraCount;
	CameraPose cameraPose[AVATAR_MAX_CAMERAS];
	std::list<CameraReading*> *cameraData[AVATAR_MAX_CAMERAS];
	int sonarCount;
	SonarPose sonarPose[AVATAR_MAX_SONAR];
	std::list<SonarReading*> *sonarData[AVATAR_MAX_SONAR];
};
*/

#define MAP_TILE_SIZE	10.0f // meters
#define MAP_RESOLUTION	0.1f // meters

#define MAP_SIZE 20 // meters
#define MAP_DIVISIONS 10 // divs over 1 meter
#define MAP_X	-8.0f // meters
#define MAP_Y	-6.0f // meters

#define ROTATION_DIVISIONS	60 // divs over 360 degrees
#define ROTATION_RESOLUTION (2*fM_PI/ROTATION_DIVISIONS)

//#define TEMPLATE_SIZE 50
//#define TEMPLATE_SIZE_DIV2 (TEMPLATE_SIZE/2)

#define CAMERA_TEMPLATE_WIDTH	1	 // meters (y-axis, rows)
#define CAMERA_TEMPLATE_LENGTH	1.5f  // meters (x-axis, cols)
#define CAMERA_TEMPLATE_DIVISIONS	20	// divs over 1 meter

#define BASE_PORT	50000


struct TESTCELL {
	int x;
	int y;
	int pathLength;
	float wPathLength;
	char fromDir;
};

struct SimLandmark {
	UUID id;
	UUID owner;
	char code;
	float x, y;

	// visualize
	int objId;
	float wx, wy, r, s;
	float color[3];
};

enum SimCamera_DATA {
	SimCamera_DATA_END = 0,  // end of stream
	SimCamera_DATA_LANDMARK, // data format: float distance, float angle
};

class DataStream;
class RandomGenerator;
class AgentPlayback;
class DDBStore;
class Visualize;

class Simulation {
public:
	Simulation( Visualize *visualizer );
	~Simulation();

public:
	int getFreePort();
	void releasePort( int port );

	int Step();
	int TogglePause() { return paused = !paused; };

	int Initialize();
	int CleanUp();

	int loadConfig( char *configFile );

	int getSimTime() { return simTime; };

private:
	
	// Configure
	int parseConfigFile( char *configFile );
	int parseSLAM( FILE *fp );
	int parseLandmark( FILE *fp );
	int parseAvatar( FILE *fp );
	int parsePath( FILE *fp );

	int dumpNum;
	
	FIMAGE *blurBuf;
	FIMAGE *blur;
	int cellPath;
	int foundX, foundY;
	char foundFrom;
	int updateWeightedPathLength( int cellX, int cellY, char fromDir, float wPathLength, std::map<int,TESTCELL*> *cellListed );
	int nextCell( int cellX, int cellY, char fromDir, float targetX, float targetY, int pathLength, float wPathLength, std::map<int,TESTCELL*> *cellListed, std::multimap<float,TESTCELL*> *frontier );
	float calcWeightedPathLength( int cellX, int cellY, float startX, float startY, float endX, float endY, std::map<int,TESTCELL*> *cellListed );
	int shortestPath( int *baseCellX, int *baseCellY, float *baseX, float *baseY, int destCellX, int destCellY, float destX, float destY, std::map<int,TESTCELL*> *cellListed );
	int planPath( float startX, float startY, float endX, float endY );

	char nextLandmarkCode;

public:
	int loadNextDump();
	int loadPrevDump();
	
	UUID addAvatar( float x, float y, float r, char *avatarFile, char *name = NULL, float *color = NULL );
	int removeAvatar( UUID id, bool ignoreVis = false );
	
	int _setAvatarPos( float x, float y, float r = 99.9f ); // debug function

	int getLinePathId() { return this->pathIdLine; };
	int getXPathId() { return this->pathIdX; };
	int getRobotPathId() { return this->pathIdRobot; };
	int getSonarPathId( float alpha );

	int addLandmark( SimLandmark *landmark );
	int removeLandmark( SimLandmark *landmark );

	// should be used only for read only purposes!
	std::list<SimLandmark*> * getLandmarkList() { return &this->landmarks; };

	int getReadingQueueSize();

	int visualizePaths();

	int setVisibility( int avatarVis = -1, int avatarPathVis = -1, int avatarExtraVis = -1, int landmarkVis = -1, int particleFilterVis = -1, int consoleVis = -1 );

	//Avatar * addAvatar( char *poseFileN );
	//int	removeAvatar( Avatar *avatar, bool ignoreVis = false );

	//int avatarConfigureCameras( Avatar *avatar, int cameraCount, char *dataFileN, char *poseFileN, char *imagePath );
	//int avatarConfigureSonar( Avatar *avatar, int sonarCount, char *sonarFileN, char *poseFileN );

	//int correctPF_AvatarCamera( ParticleFilter *pf, CameraReading *reading, CameraPose *pose );
	//int correctPF_AvatarConicSensor( ParticleFilter *pf, SonarReading *reading, SonarPose *pose );

private:
	bool clean; // true if CleanUp has been called
	
	char usedPorts[50];

	Visualize *visualizer;

	AgentPlayback *apb;
	RandomGenerator *randGen; // random generator

	UUID	mapId; // map id
	UUID	missionRegionId; // mission region id

	SimExplore *supervisorExplore; // Explore supervisor
	SimSLAM *supervisorSLAM; // SLAM supervisor
	mapSimAvatar	avatars; // list of avatars by id
	mapVisParticleFilter particleFilters; // list of particle filters by id
	std::list<SimLandmark*> landmarks; // list of all landmarks

	// DDB
	DataStream *ds; // shared DataStream object, for temporary, single thread, use only!
	DDBStore   *dStore; // stores all the DDB data
	mapDDBWatchers	DDBWatchers; // map of watcher lists by data type
	mapDDBItemWatchers DDBItemWatchers; // map of watcher lists by item
	std::list<UUID> DDBMirrors; // list of all mirrors

	int ddbAddWatcher( UUID *id, int type );
	int ddbAddWatcher( UUID *id, UUID *item );
	int ddbRemWatcher( UUID *id ); // remove agent from all items/types
	int ddbRemWatcher( UUID *id, int type );
	int ddbRemWatcher( UUID *id, UUID *item );
	int ddbClearWatchers( UUID *id );
	int ddbClearWatchers();
	int ddbNotifyWatchers( int type, char evt, UUID *id, void *data = NULL, int len = 0 );

public:

	int ddbAddRegion( UUID *id, float x, float y, float w, float h );
	int ddbRemoveRegion( UUID *id );
	int ddbGetRegion( UUID *id, DataStream *stream, int thread = 0 );

	int ddbAddLandmark( UUID *id, char code, UUID *owner, float height, float elevation, float x, float y );
	int ddbRemoveLandmark( UUID *id );
	int ddbGetLandmark( UUID *id, DataStream *stream, int thread = 0 );
	int ddbGetLandmark( char code, DataStream *stream, int thread = 0 );

	int ddbAddPOG( UUID *id, float tileSize, float resolution );
	int ddbRemovePOG( UUID *id );
	int ddbPOGGetInfo( UUID *id, DataStream *stream, int thread = 0 );
	int ddbApplyPOGUpdate( UUID *id, float x, float y, float w, float h, float *data );
	int ddbPOGGetRegion( UUID *id, float x, float y, float w, float h, DataStream *stream, int thread = 0 );
	int ddbPOGLoadRegion( UUID *id, float x, float y, float w, float h, char *filename );

	int ddbAddParticleFilter( UUID *id, UUID *owner, int numParticles, _timeb *startTime, float *startState, int stateSize );
	int ddbRemoveParticleFilter( UUID *id );
	int ddbResampleParticleFilter( UUID *id );
	int ddbInsertPFPrediction( UUID *id, _timeb *tb, float *state, bool nochange = false );
	int ddbApplyPFCorrection( UUID *id, _timeb *tb, float *obsDensity );
	int ddbPFGetInfo( UUID *id, int infoFlags, _timeb *tb, DataStream *stream, int thread = 0 );

	int ddbAddSensor( UUID *id, int type, UUID *avatar, void *pose, int poseSize );
	int ddbRemoveSensor( UUID *id );
	int ddbInsertSensorReading( UUID *id, _timeb *tb, void *reading, int readingSize, void *data = NULL, int dataSize = 0 );
	int ddbSensorGetInfo( UUID *id, int infoFlags, DataStream *stream, int thread = 0 );
	int ddbSensorGetData( UUID *id, _timeb *tb, DataStream *stream, int thread = 0 );

private:
	// basic paths
	int pathIdLine;
	int pathIdX;
	int pathIdRobot;
	int pathIdParticle;
	std::map<float,int> pathIdSonar; // sonar paths indexed by alpha

	int simTime; // simulation time (ms)
	int simStep; // simulation step size (ms)
	bool paused;

	bool skipCooccupancy; // skip cooccupancy updates

	// fImages
	FIMAGE *map;
	int mapImage;
	FIMAGE *densityT;  // density template
	FIMAGE *absoluteT; // absolute template
	FIMAGE *integralTarget; // temporary integral rotation buf
	FIMAGE *densityR[ROTATION_DIVISIONS];  // density rotated
	FIMAGE *absoluteR[ROTATION_DIVISIONS]; // absolute rotated
	FIMAGE *cameraContours; // vertical line contours
	FIMAGE *cameraT; // camera template
	FIMAGE *cameraR[ROTATION_DIVISIONS]; // camera rotated
	FIMAGE *mapUpdate; // map update
	int mapUpdateImage;
};

