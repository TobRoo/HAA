// AgentAdviceExchange.h : main header file for the AgentAdviceExchange DLL

/* 
----------------- TODO -----------------

-???

-Fill in the writeBackup and readBackup sections

----------------------------------------
*/




#pragma once

#include "..\\autonomic\\DDB.h"


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

	UUID avatarId;
	UUID avatarAgentId;
	//std::map<UUID, <perfData TO BE DEFINED>, UUIDless> otherAvatars;


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
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool convGetAgentList(void *vpConv);
	bool convGetAgentInfo(void *vpConv);

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