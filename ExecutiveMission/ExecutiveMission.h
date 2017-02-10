// ExecutiveMission.h : main header file for the ExecutiveMission DLL
//

#pragma once

#include <list>

#define CONGREGATE_LANDMARK_ID	254
#define FORAGE_LANDMARK_ID_LOW	200
#define FORAGE_LANDMARK_ID_HIGH 229

enum TASKS {
	TASK_NONE = 0,
	TASK_EXPLORE,
	TASK_CONGREGATE,
	TASK_FORAGE,
};

struct AVATAR_INFO {
	UUID owner; // id of the avatar agent
	UUID pfId; // id of the pf
	UUID controller;
	int controllerIndex;
	int priority;

	bool congregated; // has reached congregate target
	bool retired;
};

typedef std::map<UUID,AgentType,UUIDless> mapAgentType;

class ExecutiveMission : public AgentBase {

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AvatarBase

		// -- state data --
		UUID pogUUID; // UUID of the POG map

		UUID missionRegion; // UUID of the mission region

		int missionStatus;
		int missionPhase;
		int waitingForAgents; // number of agents we are waiting to spawn before we start the mission
		UUID agentSuperSLAM; // UUID of the SupervisorSLAM agent
		UUID agentSuperExplore; // UUID of the SupervisorExplore agent

		float travelTarget[3]; // target for blind traveler
		bool  travelTargetSet; // is the final target currently set
		bool  travelTargetUseRotation;

		int SLAMmode;
	};

protected:

	std::list<UUID> forbiddenRegions; // list of forbidden regions from mission file

	std::map<UUID,UUID,UUIDless> avatars; // map of avatar UUIDs by avatar agent
	mapAgentType	avatarTypes; // map of avatar types by avatar agent
	std::map<UUID,int,UUIDless>  avatarSensors; // map of avatar sensor types by avatar agent

	std::map<UUID,DDBRegion,UUIDless> collectionRegions; // map of collection regions for forage task
	std::map<UUID,UUID,UUIDless> agentSuperForage; // map of landmarks being foraged, indexed by supervisor id
	
	// task congregate stuff
	std::map<UUID,DDBLandmark,UUIDless> landmarks; // map of landmarks by UUID

	std::map<UUID,AVATAR_INFO,UUIDless> avatarInfo; // map of known avatars by avatar id
	std::map<UUID,char,UUIDless>		assignedAvatars; // list of assigned avatars by agent id
	std::map<UUID,int,UUIDless>			agentStatus; // map of status by agent id

//-----------------------------------------------------------------------------
// Non-state member variables	

protected:
	bool mapReveal;			// Flag for starting with an explored (true) or unexplored (false) map
	bool mapRandom;			// Flag for starting with a random (true) map or one loaded from a file (false) 
	bool individualLearning;// Flag showing if individual learning is enabled (true) or disabled (false)
	bool teamLearning;		// Flag showing if team learning is enabled (true) or disabled (false)

//-----------------------------------------------------------------------------
// Functions	

public:
	ExecutiveMission( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~ExecutiveMission();

	virtual int configure();	// initial configuration

	virtual int   parseMF_HandleMissionRegion( DDBRegion *region );
	virtual int   parseMF_HandleForbiddenRegion( DDBRegion *region );
	virtual int   parseMF_HandleCollectionRegion( DDBRegion *region );
	virtual int   parseMF_HandleTravelTarget( float x, float y, float r, bool useRotation );
	virtual int   parseMF_HandleLandmarkFile( char *fileName );
	virtual int	  parseMF_HandleMapOptions(bool mapReveal, bool mapRandom);
	virtual int   parseMF_HandleLearning(bool individualLearning, bool teamLearning) ;
	virtual int   parseMF_HandleTeamLearning(bool teamLearning);

	virtual int	  doMapReveal();

	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

	virtual int parseMF_HandleOptions( int SLAMmode );

private:

	int parseLandmarkFile( char *filename );

	int registerAvatar( UUID *id, UUID *avatarId, AgentType *agentType, int sensorTypes, bool force = false ); // register avatar
	int allocateAvatars();
	int _allocateAvatar( UUID *avAgent, UUID *controller, int priority );
	int avatarResourceRequest( DataStream *ds );
	int avatarResourceRelease( UUID *agent, UUID *avatar );

	int avatarUpdateController( UUID *avatarId, UUID *controller, int index, int priority ); // update controller
	int addAvatarAllocation( UUID *id, UUID *avatarId );
	int remAvatarAllocation( UUID *id );

	int missionDone();

	int taskCongregateStart( DDBLandmark *lm );
	int taskCongregateUpdateTargets();
	int taskCongregateCheckCompletion();
	int taskBlindTravellerStart( UUID *id );
	int taskBlindTravellerWander( UUID *id );
	int taskBlindTravellerFinish();

	int taskForageStart( UUID *id, DDBLandmark *lm );

	virtual int ddbNotification( char *data, int len );

protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( ExecutiveMission )

	// Enumerate callback references
	enum CallbackRef {
		ExecutiveMission_CBR_convRequestSupervisorSLAM = AgentBase_CBR_HIGH,
		ExecutiveMission_CBR_convRequestSupervisorExplore,
		ExecutiveMission_CBR_convRequestSupervisorForage,
		ExecutiveMission_CBR_convLandmarkInfo,
		ExecutiveMission_CBR_convGetAvatarList,
		ExecutiveMission_CBR_convGetAvatarInfo,
		ExecutiveMission_CBR_convGetAvatarPos,
		ExecutiveMission_CBR_convReachedPoint,
		ExecutiveMission_CBR_convAgentInfo,
		ExecutiveMission_CBR_cbAllocateAvatars,
		ExecutiveMission_CBR_convGetTaskList,
		ExecutiveMission_CBR_convGetTaskInfo,
		ExecutiveMission_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool	convRequestSupervisorSLAM( void *vpConv );
	bool	convRequestSupervisorExplore( void *vpConv );
	bool	convRequestSupervisorForage( void *vpConv );
	bool	convLandmarkInfo( void *vpConv );
	bool	convGetAvatarList( void *vpConv );
	bool	convGetAvatarInfo( void *vpConv );
	bool	convGetAvatarPos( void *vpConv );
	bool	convReachedPoint( void *vpConv );
	bool	convAgentInfo( void *vpConv );
	bool	convGetTaskList(void * vpConv);
	bool	convGetTaskInfo(void * vpConv);
	bool	cbAllocateAvatars( void *NA );
	
protected:
	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream

	// -- Backup Notes -- 
	// Backup when:
	//  - waitingForAgents, agentSuperSLAM, agentSuperExplore, missionStatus, missionPhase, landmarks, forbidden regions

	// -- Recovery Notes --
	// - if waitingForAgents == 2
	//		start SupervisorSLAM
	//		start SupervisorExplore
	//	 else if waitingForAgents == 1
	//		if agentSuperSLAM == nilUUID then
	//			start SupervisorSLAM
	//		else
	//			start SupervisorExplore 
	// - request list of avatars (for task congregate)
	// External:
	// - avatars need to watch us and re register if we fail

	virtual int	  recoveryFinish();

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );


// CExecutiveMissionDLL: Override ExitInstance for debugging memory leaks
class CExecutiveMissionDLL : public CWinApp {
public:
	CExecutiveMissionDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};