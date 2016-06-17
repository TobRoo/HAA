// AgentIndividualLearning.h : main header file for the AgentIndividualLearning DLL
//

#pragma once

#include "..\\autonomic\\DDB.h"

struct ActionPair {
	int action;
	float val;
};

class AgentIndividualLearning : public AgentBase {

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AvatarBase

		// -- state data --
		UUID ownerId;

		bool parametersSet;
		bool startDelayed; // start has been delayed because parameters are not set yet
		int initialSetup; // 0 when setup is complete

		int updateId;

		// actions
		int actionsSent; // how many actions are we waiting for
		UUID actionConv; // action conversation id
		_timeb actionCompleteTime; // time that the last action was completed

	};

//-----------------------------------------------------------------------------
// Non-state member variables	

protected:
	UUID AgentIndividualLearning_recoveryLock;

//-----------------------------------------------------------------------------
// Functions	

public:
	AgentIndividualLearning( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~AgentIndividualLearning();

	virtual int configure();	// initial configuration
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

private:

	int configureParameters( DataStream *ds );
	int finishConfigureParameters();
	int sendAction();

protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( AgentIndividualLearning )

	// Enumerate callback references
	enum CallbackRef {
		AgentIndividualLearning_CBR_convAction = AgentBase_CBR_HIGH,
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool convAction(void *vpConv);

protected:
	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream

	// -- Backup Notes -- 
	// Backup when:
	//  - initialSetup

	// -- Recovery Notes --
	// - if initialSetup == 3
	//		fail recovery
	//	 else if initialSetup == 2
	//		lock recovery
	//		get region info
	//		get map info
	//	 else if initialSetup == 1
	//      lock recovery
	//		initialSetup = 2
	//		get region info
	//		get map info
	//   else
	//		finishConfigureParameters();
	// - request list of avatars
	// External:
	// - owner needs to watch our status and reset our target if we fail

	virtual int	  recoveryFinish();

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream
};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );


// CAgentIndividualLearningDLL: Override ExitInstance for debugging memory leaks
class CAgentIndividualLearningDLL : public CWinApp {
public:
	CAgentIndividualLearningDLL();
	virtual BOOL ExitInstance();
};