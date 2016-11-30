// SupervisorSLAM.h : main header file for the SupervisorSLAM DLL
//

#pragma once

#define BACKUP_READINGS_THRESHOLD 100

#define REDUNDANT_AGENT_QUEUE_THRESHOLD	20 // readings
#define REDUNDANT_AGENT_REQUEST_DELAY	5 // seconds
#define REDUNDANT_AGENT_REJECTED_DELAY	30 // seconds

struct READING_TYPE {
	int sensor;
	int phase;
};

struct READING_QUEUE {
	READING_TYPE type;
	UUID uuid;
	_timeb tb;
	int attempt;
};

#ifndef READING_TYPElessDefined
// READING_TYPEless comparison function
class READING_TYPEless {
public:
	bool operator()(const READING_TYPE& _Left, const READING_TYPE& _Right) const
		{	// apply operator< to operands
			return memcmp( &_Left, &_Right, sizeof(READING_TYPE) ) < 0; 
		}
};
#define READING_TYPElessDefined
#endif

typedef std::map<READING_TYPE,std::list<READING_QUEUE>,READING_TYPEless> mapTypeQueue; 
typedef std::map<UUID,std::list<READING_QUEUE>,UUIDless> mapAgentQueue;

typedef std::map<READING_TYPE,std::list<UUID>,READING_TYPEless>	mapTypeAgents;
typedef std::map<READING_TYPE,int,READING_TYPEless>				mapAgentSpawning;

class SupervisorSLAM : public AgentBase {

//-----------------------------------------------------------------------------
// State member variables	

public:
	struct State {
		AgentBase::State state; // inherit state from AvatarBase

		// -- state data --
		UUID mapId;
		
		int hostGroupSize; // size of the host group
		_timeb lastRedundantSpawnRequest;
		_timeb lastRedundantSpawnRejected;

		int SLAMmode;
	};

protected:
	mapTypeAgents agents; // map of agents by sensor type
	mapAgentSpawning agentsSpawning; // list of currently spawning agents
	std::map<UUID,int,UUIDless> agentsStatus; // map of status by agent id
	std::map<UUID,bool,UUIDless> agentsProcessing; // currently processing, by agent id

	mapTypeQueue typeQueue; // queues of readings by sensor type
	mapAgentQueue agentQueue; // queues of readings by agent

//-----------------------------------------------------------------------------
// Non-state member variables	

protected:
	int		backupReadingsCount; // count down until next time we backup

	bool mapReveal;
	bool mapRandom;

//-----------------------------------------------------------------------------
// Functions	

public:
	SupervisorSLAM( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~SupervisorSLAM();

	virtual int configure();	// initial configuration
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

	virtual int parseMF_HandleMapOptions(bool mapReveal, bool mapRandom);
	virtual int parseMF_HandleOptions( int SLAMmode );

private:
	
	int addMap( UUID *map );

	int	   getProcessingPhases( int sensor );
	int	   requestAgentSpawn( READING_TYPE *type, char priority );
	UUID * getProcessingAgent( READING_TYPE *type );
	int	   removeProcessingAgent( UUID *agent );

	int nextSensorReading( UUID *agent );
	int assignSensorReading( UUID *agent, READING_QUEUE *rq );
	int doneSensorReading( UUID *uuid, UUID *sensor, _timeb *tb, char success );

	int newSensorReading( READING_TYPE *type, UUID *uuid, _timeb *tb, int attempt = 0 );

	virtual int ddbNotification( char *data, int len );

	int updateHostGroup( int groupSize );

protected:
	virtual int conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len );

public:
	// -- Callbacks -----------------------------------------
	DECLARE_CALLBACK_CLASS( SupervisorSLAM )

	// Enumerate callback references
	enum CallbackRef {
		SupervisorSLAM_CBR_convRequestAgentSpawn = AgentBase_CBR_HIGH,
		SupervisorSLAM_CBR_convAgentInfo,
		SupervisorSLAM_CBR_convHostGroupSize,
		SupervisorSLAM_CBR_HIGH
	};

	// Define callback functions (make sure they match CallbackRef above and are added to this->callback during agent creation)
	bool	convRequestAgentSpawn( void *vpConv );
	bool	convAgentInfo( void *vpConv );
	bool	convHostGroupSize( void *vpConv );

protected:
	virtual int	  freeze( UUID *ticket );
	virtual int   thaw( DataStream *ds, bool resumeReady = true );

	virtual int	  writeState( DataStream *ds, bool top = true ); // write state data to stream
	virtual int	  readState( DataStream *ds, bool top = true );  // read state data from stream
	
	// -- Backup Notes -- 
	// Backup when:
	//  - mapId, or agents changes
	//  - every BACKUP_READINGS_THRESHOLD times a sensor reading is added so that if processing falls behind we can't lose too many readings

	// -- Recovery Notes --
	// - for each agent in agentsProcessing
	//		update status
	//		(NOT DOING THIS, BUT MAYBE WE SHOULD?) tell processing agents to give up on current reading

	virtual int	  recoveryFinish();

	virtual int   writeBackup( DataStream *ds ); // write backup data to stream
	virtual int   readBackup( DataStream *ds ); // read backup data from stream

};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );


// CSupervisorSLAMDLL: Override ExitInstance for debugging memory leaks
class CSupervisorSLAMDLL : public CWinApp {
public:
	CSupervisorSLAMDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};