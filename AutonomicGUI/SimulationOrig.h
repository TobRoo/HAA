
#pragma once
#include "afxwin.h"

#include <gl/gl.h>
#include <gl/glu.h>

#include "..\\autonomic\\fImage.h"

#include <sys/timeb.h>
#include <list>

#include <boost/random/uniform_int.hpp>
#include <boost/random/normal_distribution.hpp>
#include <boost/random/lagged_fibonacci.hpp>
#include <boost/random/variate_generator.hpp>

struct Block {
	float x, y;
	float width, height;
};

#define BLOCK_COUNT 6

struct Robot {
	float x, y, r;
	float v;
	float lastx, lasty, lastr;
	int lastPrediction;
	int nextPrediction;
	int nextSonar;
	int curOrder;
	float curVal;
	char orderType[32];
	float orderVal[32];
};

#define ORD_DONE	-1
#define ORD_MOVE	0
#define ORD_ROTATE	1
#define ORD_WAIT	2

#define ACCEL 2
#define VELMAX 2
#define VELROTATE fM_PI

struct ParticleState_Avatar {
	float x, y, r;
};

#define NUM_PARTICLES 50
#define MIN_EFF_PARTICLES	10
#define OBS_DENSITY_RATIO	0.5f  // absolute:density
#define OBS_DENSITY_AMBIENT 0.8f


#define AVATAR_INIT_LINEAR_SIG		0.001f
#define AVATAR_INIT_ROTATIONAL_SIG	0.001f
#define AVATAR_LINEARV_SIG			0.000005f // m/ms
#define AVATAR_ROTATIONALV_SIG		0.000005f // rad/ms
#define AVATAR_LINEARV_SIG_EST		0.00005f  // m/ms
#define AVATAR_ROTATIONALV_SIG_EST	0.00005f  // rad/ms

#define PREDICTION_PERIOD 100

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


#define TEMPLATE_SIZE 50
#define TEMPLATE_SIZE_DIV2 (TEMPLATE_SIZE/2)

#define MAP_SIZE 20 // meters
#define MAP_DIVISIONS 10 // divs over 1 meter
#define ROTATION_DIVISIONS	60 // divs over 360 degrees
#define ROTATION_RESOLUTION (2*fM_PI/ROTATION_DIVISIONS)

struct SonarOutline {
	float x[6], y[6];
};

#define SONAR_RANGE_MAX 2.5f
#define SONAR_RANGE_MIN 0.05f

#define SONAR_SIG		0.02f
#define SONAR_A			(fM_PI/6)

#define SONAR_SIG_EST	0.4f
#define SONAR_A_EST		(SONAR_A)
#define SONAR_B_EST		(SONAR_A*0.7f)

#define SONAR_HISTORY_SIZE 6

#define SONAR_PERIOD 200

class Simulation {
public:
	Simulation();
	~Simulation();

public:
	int Initialize();
	int Step();
	int TogglePause() { return paused = !paused; };

	int PreDraw();
	int Draw();

private:
	int simTime; // simulation time
	int simStep; // time per step

	bool paused;

	float map[MAP_SIZE*MAP_DIVISIONS][MAP_SIZE*MAP_DIVISIONS]; // map
	float mapLoc[2]; // location of the map
	Block blocks[BLOCK_COUNT]; // blocks
	Robot robots[2]; // robots
	ParticleFilter *particleFilters[2]; // particles
	
	SonarOutline sonarHistory[SONAR_HISTORY_SIZE]; // sonar history
	int sonarHistoryWrite;		   // next write
	int sonarHistoryCount;		   // count
	
	FIMAGE *densityT;  // density template
	FIMAGE *absoluteT; // absolute template
	FIMAGE *integralTarget; // temporary integral rotation buf
	FIMAGE *densityR[ROTATION_DIVISIONS];  // density rotated
	FIMAGE *absoluteR[ROTATION_DIVISIONS]; // absolute rotated
	bool   rotationDivision[ROTATION_DIVISIONS]; // marks whether the rotation division images are up to date
	FIMAGE *mapUpdate; // map update
	float mapUpdateLoc[2]; // location of the mapUpdate origin

	int generateConicTemplates( float d, float max, float var, float a, float b, FIMAGE *densityT, FIMAGE *absoluteT, float *scale ); // generate conic templates
	float takeSonar( float x, float y, float r ); // take sonar reading

	int correctPF_AvatarConicSensor( ParticleFilter *pf, float d, float sensorRot, _timeb *time );
};

void * generateParticleState_Avatar( int index, void *genParams );
void * copyParticleState_Avatar( void *vpstate );
int predictPF_Avatar( ParticleFilter *pf, unsigned int dtime, float forwardV, float tangentialV, float rotationalV );