// AgentTeamLearning.h : main header file for the AgentTeamLearning DLL
//

#pragma once


#include "..\\autonomic\\autonomic.h"
#include "..\\autonomic\\DDB.h"

#include "LAlliance.h"

#define TEAMMATE_NOCHANGE_COUNT	5		//How many stable iterations (with no change in teammate numbers) required before task choosing begins 
#define TEAMMATE_CRASH_TIMEOUT	3000		//Time (in ms) to wait before assuming that the preceding teammate in the list has failed

class AgentTeamLearning : public AgentBase {

	friend class LALLIANCE;

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AvatarBase

		// -- state data --
		UUID ownerId;
		bool startDelayed; // start has been delayed because parameters are not set yet
		bool parametersSet;
		bool hasReceivedTaskList;
		bool hasReceivedTaskDataList;
		bool isSetupComplete;

		int updateId;

		bool isMyDataFound;


	};

	// Random number generator
	RandomGenerator randomGenerator;

//-----------------------------------------------------------------------------
// Non-state member variables	

protected:
	LAlliance lAllianceObject;
	UUID AgentTeamLearning_recoveryLock;
	taskList  mTaskList;
	UUID	  previousTaskId;


	int lastTeammateDataSize;
	int itNoTeammateChange;	//Count how many iterations since last teammateData size change
	bool isReadyToBroadcast;
	bool broadCastUnlocked;
	bool broadCastPending;
	bool hasStabilityOccurred;
	UUID avatarToListenFor;	//The avatar that is ahead of us in the task update list
	UUID agentListeningForUs;	//The -agent- that is after us in the task update list	(NOTE: Agent, not avatar)
	_timeb lastTaskUpdateSent;	//last time we sent a task update to the DDB
	long long timeoutPeriod;
	
//-----------------------------------------------------------------------------
// Functions	

public:
	AgentTeamLearning( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AgentTeamLearning();

	virtual int configure();	// initial configuration
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step


	int sendRequest(UUID *agentId, int message, UUID *id = NULL);

	void logWrapper(int log_level, char * message);

	void negotiateTasks();

private:

	int configureParameters( DataStream *ds );
	int finishConfigureParameters();
	int uploadTask(UUID *taskId, DDBTask * task);
	int uploadTaskDataInfo();


	virtual int ddbNotification(char * data, int len);

protected:

	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AgentTeamLearning )

	// Enumerate callback references
	enum CallbackRef {
		AgentTeamLearning_CBR_convGetTaskInfo = AgentBase_CBR_HIGH,
		AgentTeamLearning_CBR_convGetTaskDataInfo,
		AgentTeamLearning_CBR_convGetTaskList,
		AgentTeamLearning_CBR_convGetTaskDataList,
		AgentTeamLearning_CBR_convReqAcquiescence,
		AgentTeamLearning_CBR_convReqMotReset,
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool convGetTaskInfo(void *vpConv);
	bool convGetTaskDataInfo(void *vpConv);
	bool convGetTaskList(void *vpConv);
	bool convGetTaskDataList(void *vpConv);
	bool convReqAcquiescence(void *vpConv);
	bool convReqMotReset(void *vpConv);


protected:
	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream

	virtual int	  recoveryFinish();

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );


// CAgentTeamLearningDLL: Override ExitInstance for debugging memory leaks
class CAgentTeamLearningDLL : public CWinApp {
public:
	CAgentTeamLearningDLL();
	virtual BOOL ExitInstance();
};