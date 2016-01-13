// SupervisorForage.h : main header file for the SupervisorForage DLL
//

#pragma once

#include <list>

#define COLLECTION_THRESHOLD 0.15f // m
#define DELIVERY_THRESHOLD 0.15f // m

struct AVATAR_INFO {
	UUID owner; // id of the avatar agent
	UUID pfId;
	UUID controller;
	int controllerIndex;
	bool pfValid; // have we received the state at least once?
	float pfState[3];

	int sensorTypes;
	int capacity;
	int cargoCount;
};

struct AVATAR {
	UUID avatarId; // id of the avatar
	bool pfKnown; // do we know the pfId?
	UUID pfId; 
};

struct GETAVATARPOS_CONVDATA {
	int updateId;
	UUID avatarId;
};

#define SupervisorForage_AVATAR_POS_RETRY  100		// ms

class SupervisorForage : public AgentBase {
	enum VIS_PATHS {
		VIS_PLACEHOLDER = 0,	// replace me with first real path
	};	

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AvatarBase

		// -- state data --
		UUID avatarExecId;
		int waitingOnPF;

		bool requestAvatarInProgress;
		
		bool landmarkCollected;
		bool landmarkDelivered;
		bool forageQueued;

		bool deliveryLocValid;
		float deliveryLoc[2];

		UUID landmarkId;
		DDBLandmark landmark;
	};

protected:
	std::map<UUID,DDBRegion,UUIDless> collectionRegions;

	std::map<UUID,AVATAR_INFO,UUIDless> avatarInfo; // map of known avatars by avatar id
	std::map<UUID,AVATAR,UUIDless>	avatars; // list of assigned avatars by avatar agent
	std::map<UUID,int,UUIDless> agentStatus; // map of status by agent id

//-----------------------------------------------------------------------------
// Non-state member variables	

protected:


//-----------------------------------------------------------------------------
// Functions	

public:
	SupervisorForage( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~SupervisorForage();

	virtual int configure();	// initial configuration
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

private:
	
	int configureParameters( DataStream *ds );

	int avatarUpdateController( UUID *avatarId, UUID *controller, int index ); // update controller
	int addAvatar( UUID *agentId, UUID *avatarId );	// avatar allocated
	int remAvatar( UUID *agentId ); // avatar allocation revoked

	int forage(); // update foraging task
	int queueForage();

	int selectAvatar();
	int collect();
	int deliver();
	int calcDeliveryLoc( bool force = false );

	bool checkReady( UUID *agent );

	virtual int ddbNotification( char *data, int len );

protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( SupervisorForage )

	// Enumerate callback references
	enum CallbackRef {
		SupervisorForage_CBR_convGetAvatarList = AgentBase_CBR_HIGH,
		SupervisorForage_CBR_convGetAvatarInfo,
		SupervisorForage_CBR_convGetAvatarPos,
		SupervisorForage_CBR_convReachedPoint,
		SupervisorForage_CBR_convAgentInfo,
		SupervisorForage_CBR_convLandmarkInfo,
		SupervisorForage_CBR_convRequestAvatar,
		SupervisorForage_CBR_convCollectLandmark,
		SupervisorForage_CBR_cbQueueForage,
		SupervisorForage_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool convGetAvatarList( void *vpConv );
	bool convGetAvatarInfo( void *vpConv );
	bool convGetAvatarPos( void *vpConv );
	bool convReachedPoint( void *vpConv );
	bool convAgentInfo( void *vpConv );
	bool convLandmarkInfo( void *vpConv );
	bool convRequestAvatar( void *vpConv );
	bool convCollectLandmark( void *vpConv );
	bool cbQueueForage( void *NA );

protected:
	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream

	// -- Backup Notes -- 
	// Backup when:
	//  - avatarExecId, landmarkId, landmarkCollected, landmarkDelivered, collectionRegions changes

	// -- Recovery Notes --
	//  - get avatar list
	//  - get landmark
	//  - get avatar executive status

	virtual int	  recoveryFinish();

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );


// CSupervisorForageDLL: Override ExitInstance for debugging memory leaks
class CSupervisorForageDLL : public CWinApp {
public:
	CSupervisorForageDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};