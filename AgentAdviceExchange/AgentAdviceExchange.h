// AgentAdviceExchange.h : main header file for the AgentAdviceExchange DLL

/* 
----------------- TODO -----------------

-???

-Fill in the writeBackup and readBackup sections

----------------------------------------
*/




#pragma once

#include "..\\autonomic\\DDB.h"


typedef struct adviceQueryData {
	UUID conv;
	std::vector<float> quality;
	bool response;
};


struct NameToBeDecided_AdvEx{
	AgentType agentType;

};


class AgentAdviceExchange : public AgentBase {

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AvatarBase

		// Agent data --
		UUID ownerId;
		bool parametersSet;
		bool startDelayed; // start has been delayed because parameters are not set yet
		int updateId;
		bool setupComplete;
	};

//-----------------------------------------------------------------------------
// Non-state member variables	

protected:
	UUID AgentAdviceExchange_recoveryLock;
	DataStream ds; // shared DataStream object, for temporary, single thread, use only!


	//std::map<UUID, <perfData TO BE DEFINED>, UUIDless> otherAvatars;

	// Advice data
	std::vector<float> q_vals_in;
	std::vector<unsigned int> state_vector_in;
	unsigned int num_state_vrbls_;              // Number of variables in state vector 
	unsigned int num_actions_;                  // Number of possible actions

	UUID adviceRequestConv;
	std::map<UUID, adviceQueryData, UUIDless> adviceQuery;

	// Random number generator
	RandomGenerator randomGenerator;


	//Metrics 

//-----------------------------------------------------------------------------
// Functions	

public:
	AgentAdviceExchange( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AgentAdviceExchange();

	virtual int configure();	// initial configuration
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

	int askAdvisers();
	int formAdvice();

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