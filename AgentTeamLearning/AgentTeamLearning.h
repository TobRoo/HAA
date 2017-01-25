// AgentTeamLearning.h : main header file for the AgentTeamLearning DLL
//

#pragma once


#include "..\\autonomic\\autonomic.h"
#include "..\\autonomic\\DDB.h"

#include "LAlliance.h"

typedef struct TLAgentDataStruct{
	UUID id;
	UUID parentId;
	bool response;
};

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
		bool receivedAllTeamLearningAgents;
		bool hasReceivedRunNumber;
		int avatarInstance;


		int runNumber;

		int updateId;

		bool isMyDataFound;

		// V2 team learning comms
		int round_number;  // Counter for the current task allocation round
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

	// V2 for team learning comms
	std::map<UUID, TLAgentDataStruct, UUIDless> TLAgentData;
	std::vector<UUID> TLAgents;
	_timeb round_start_time;          // Start time of the current round of learning/task allocation [ms]
	long long round_timout;           // Maximum time for an agent to respond in a round [ms]
	long long delta_learn_time;       // Time between rounds of learning/task allocation
	_timeb last_response_time;        // Time of the last from another agent performing task allocation

	_timeb round_info_receive_time;
	int new_round_number;
	std::vector<UUID> new_round_order;
	bool round_info_set;
	bool last_agent;

	
//-----------------------------------------------------------------------------
// Functions	

public:
	AgentTeamLearning( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AgentTeamLearning();

	virtual int configure();	// initial configuration
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step
	int checkRoundStatus();
	int initiateNextRound();
	int sendRequest(UUID *agentId, int message, UUID *id = NULL);
	void logWrapper(int log_level, char * message);

private:

	int configureParameters( DataStream *ds );
	int finishConfigureParameters();
	int parseLearningData();

	int uploadTask(UUID &task_id, UUID &agent_id, UUID &avatar_id, bool completed);
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
		AgentTeamLearning_CBR_convGetAgentList,
		AgentTeamLearning_CBR_convGetTaskList,
		AgentTeamLearning_CBR_convGetTaskDataList,
		AgentTeamLearning_CBR_convGetTaskDataInfo,
		AgentTeamLearning_CBR_convReqAcquiescence,
		AgentTeamLearning_CBR_convReqMotReset,
		AgentTeamLearning_CBR_convGetRunNumber,
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool convGetAgentList(void *vpConv);
	bool convGetTaskList(void *vpConv);
	bool convGetTaskDataList(void *vpConv);
	bool convGetTaskInfo(void *vpConv);
	bool convGetTaskDataInfo(void *vpConv);
	bool convReqAcquiescence(void *vpConv);
	bool convReqMotReset(void *vpConv);
	bool convGetRunNumber(void *vpConv);

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