// AgentAdviceExchange.h : main header file for the AgentAdviceExchange DLL

/* 
----------------- TODO -----------------

-???

-Fill in the writeBackup and readBackup sections

----------------------------------------
*/




#pragma once

#include "..\\autonomic\\DDB.h"
#include <numeric>


typedef struct adviserDataStruct {
	UUID parentId;              // Id of (parent) Individual Learning agent
	UUID queryConv;             // Thread used to ask this adviser for advice
	std::vector<float> advice;  // vector of advised quality values
	float cq;                   // Current average quality
	float bq;                   // Best average quality
};


class AgentAdviceExchange : public AgentBase {

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AvatarBase

		// Agent data --
		UUID ownerId;
		UUID avatarId;
		bool parametersSet;
		bool startDelayed; // start has been delayed because parameters are not set yet
		int updateId;
		bool setupComplete;
		ITEM_TYPES avatarCapacity; //Carrying capacity of the parent avatar; 1 = light items, 2 = heavy items (0 = NON_COLLECTABLE, cannot carry items)
		int epoch;  // Epoch counter
	};

//-----------------------------------------------------------------------------
// Non-state member variables	

protected:
	UUID AgentAdviceExchange_recoveryLock;
	DataStream ds; // shared DataStream object, for temporary, single thread, use only!

	// Configuration data
	unsigned int num_state_vrbls_;              // Number of variables in state vector 
	unsigned int num_actions_;                  // Number of possible actions
	float alpha;                                // Coefficient for current average quality metric cq
	float beta;                                 // Coefficient for best avrage quality metric bq
	float delta;                                // Coefficient for bq comparison
	float rho;                                  // Coefficient for quality sum comparison

	// Personal data and metrics
	std::vector<float> q_vals_in;               // Parent agent Q values received during advice request
	std::vector<unsigned int> state_vector_in;  // Parent agent state vector received during advice request
	float cq;                                   // Current average quality
	float bq;                                   // Best average quality
	float q_avg_epoch;                          // Average quality of the current epoch

	// Advice data
	UUID adviceRequestConv;                                    // Conversation from parent agent used to request advice
	std::map<UUID, adviserDataStruct, UUIDless> adviserData;   // All advice data for each adviser
	UUID adviser;                                              // Id of the AgentAdviceExchange adviser (i.e. the key for adviserData)
	
	// Random number generator
	RandomGenerator randomGenerator;

//-----------------------------------------------------------------------------
// Functions	

public:
	AgentAdviceExchange( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AgentAdviceExchange();

	virtual int configure();	// initial configuration
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

	int askAdviser ();        // Sends a request for advice to the advisers
	int formAdvice();         // Performs the Advice Exchange algorithm and sends the advice back

	int preEpochUpdate();     // Performs the necessary updates before a new epoch begins
	int postEpochUpdate();   // Performs the necessary updates after an epoch is finished

private:

	int configureParameters( DataStream *ds );
	int finishConfigureParameters();
	
	virtual int ddbNotification(char *data, int len);

protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AgentAdviceExchange )

	// Enumerate callback references
	enum CallbackRef {
		AgentAdviceExchange_CBR_convGetAgentList = AgentBase_CBR_HIGH,
		AgentAdviceExchange_CBR_convGetAgentInfo,
		AgentAdviceExchange_CBR_convAdviceQuery,
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool convGetAgentList(void *vpConv);
	bool convGetAgentInfo(void *vpConv);
	bool convAdviceQuery(void *vpConv);

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


// CAgentAdviceExchangeDLL: Override ExitInstance for debugging memory leaks
class CAgentAdviceExchangeDLL : public CWinApp {
public:
	CAgentAdviceExchangeDLL();
	virtual BOOL ExitInstance();
};