
// !! NOTE !! Make sure the order matches betwen MSGS and MSG_SIZE
enum MSGS {
	MSG_ACK = 0,	// acknowledge
	MSG_RACK,		// request acknowledgement
	MSG_ACKEX,		// extended acknowledge, includes four byte ID
	MSG_RACKEX,		// request extended acknowledgement, includes four byte ID
	MSG_FORWARD,	// forward a message to another agent [UUID agent, byte return <, UUID return address>, message]
	
	MSG_RESPONSE,   // response to a conversation thread [UUID thread, ... check the trigger message for response format]
	
	MSG_ATOMIC_MSG, // send message that will only be delivered once everyone confirms receipt [UUID sender, UUID id, bool orderedMsg, UUID queue, int targetCount, UUID target, ..., int failureCount, unsigned char msg, unsigned int len, data..., int order]
	MSG_ATOMIC_MSG_REORDER, // propose reorder [UUID id, UUID queue, UUID q, int orderq]
	MSG_ATOMIC_MSG_ESTIMATE, // send estimate in step P1 [UUID id, UUID queue, UUID q, int orderq, int roundq, char estCommitq, int timestampq]
	MSG_ATOMIC_MSG_PROPOSAL, // send proposal in step C1 [UUID id, UUID queue, UUID q, int orderq, int roundq, char estCommitq]
	MSG_ATOMIC_MSG_ACK,	// acknowledge proposal in step P2 [UUID id, UUID queue, UUID q, int orderq, int roundq, char ack]
	MSG_ATOMIC_MSG_DECISION, // decision made in step C2 [UUID id, UUID queue, UUID q, int orderq, int roundq, char commit]
	MSG_ATOMIC_MSG_SUSPECT,	// notify agent to suspect another agent [UUID id, UUID suspect]
	
	MSG_FD_ALIVE,	// failure detector "I'm still alive" message [unsigned int count, unsigned int period, _timeb send time]
	MSG_FD_QOS,		// failure detector set period [unsigned int new period]

	OAC_MISSION_START,	// start mission [string mission file name]

	MSG_MISSION_DONE,	// mission finished [char success]
	OAC_MISSION_DONE,	// mission finished [char success]

	OAC_GM_REMOVE,	// group membership remove transaction [UUID q, UUID key, remove list: <true, UUID remove, ...>, false]
	OAC_GM_MEMBERSHIP,	// group membership membership transaction [UUID q, UUID key, member list: <true, UUID member, ...>, false]
	OAC_GM_FORMATIONFALLBACK,	// group membership formation fallback transaction [UUID q, member list: <true, UUID member, ...>, false]

	OAC_PA_START,   // start PA session [UUID q, int sessionId, ...member list, ...agent info]
	MSG_PA_BID_UPDATE, // PA bid update [UUID q, int sessionId, int update count, UUID process id, UUID winner id, float reward, float support, short round, ...]
	OAC_PA_FINISH,  // finish PA session [UUID q, int sessionId, bool true, UUID process id, UUID winner id, ..., bool false]

	OAC_STATE_TRANSACTION_BUNDLE, // bundle of squential state transactions [bool true, unsigned char msg, unsigned int length, ...data <, more messages...>, bool false]

	OAC_DDB_WATCH_TYPE, // register as a watcher for data type [UUID id, int type]
	OAC_DDB_WATCH_ITEM, // register as a watcher for item [UUID id, UUID item]
	OAC_DDB_STOP_WATCHING_TYPE, // unregister as a watcher for data type [UUID id, int type]
	OAC_DDB_STOP_WATCHING_ITEM, // unregister as a watcher for item [UUID id, UUID item]
	OAC_DDB_CLEAR_WATCHERS, // clear all watchers for item [UUID item]

	OAC_DDB_ADDAGENT,  // add agent to DDB [UUID sender, UUID uuid, UUID parentId, AgentType agentType, UUID spawnThread, float parentAffinity, char priority, float processCost, int activationMode]
	OAC_DDB_REMAGENT,  // remove agent from DDB [UUID sender, UUID uuid]
	OAC_DDB_AGENTSETINFO, // set agent info [UUID sender, UUID uuid, int infoFlags, data...]

	OAC_DDB_VIS_ADDPATH,	// create new path [UUID sender, UUID agent, int pathId, int count, float x[], float y[]]
	OAC_DDB_VIS_REMPATH, // delete path [UUID sender, UUID agent, int pathId]
	OAC_DDB_VIS_EXTENDPATH, // extend path [UUID sender, UUID agent, int pathId, int count, float x[], float y[]]
	OAC_DDB_VIS_UPDATEPATH, // update path [UUID sender, UUID agent, int pathId, int count, int nodes[], float x[], float y[]]
	OAC_DDB_VIS_ADDOBJECT,  // create new object [UUID sender, UUID agent, int objectId, float x, float y, float r, float s, int count, int paths[], float colours[3][], float lineWidths[], bool solid, string name]
	OAC_DDB_VIS_REMOBJECT, // delete object [UUID sender, UUID agent, int objectId]
	OAC_DDB_VIS_UPDATEOBJECT, // update object [UUID sender, UUID agent, int objectId, float x, float y, float r, float s]
	OAC_DDB_VIS_SETOBJECTVISIBLE, // set object visible [UUID sender, UUID agent, int objectId, char visible], 0=off, 1=on, -1=toggle
	OAC_DDB_VIS_CLEAR_ALL, // delete object [UUID sender, UUID agent, char clearPaths]
	
	OAC_DDB_ADDREGION, // add region to DDB [UUID sender, UUID uuid, float x, float y, float w, float h]
	OAC_DDB_REMREGION, // remove region from DDB [UUID sender, UUID uuid]

	OAC_DDB_ADDLANDMARK, // add landmark to DDB [UUID sender, UUID uuid, unsigned char code, UUID owner, float height, float elevation, float x, float y, char estimatedPos, int landmarkType]
	OAC_DDB_REMLANDMARK, // remove landark from DDB [UUID sender, UUID uuid]
	OAC_DDB_LANDMARKSETINFO, // set landmark info [UUID sender, UUID uuid, int infoFlags, data...]
	
	OAC_DDB_ADDPOG,	// add POG to DDB [UUID sender, UUID uuid, float tileSize, float resolution]
	OAC_DDB_REMPOG,  // remove POG from DDB [UUID sender, UUID uuid]
	OAC_DDB_APPLYPOGUPDATE, // apply pog update [UUID sender, UUID uuid, float x, float y, float w, float h, int updateSize, ... update]
	OAC_DDB_POGLOADREGION, // utility function for loading POG from a file [UUID sender, UUID uuid, float x,  float y, float w, float h, string file name]

	OAC_DDB_ADDPARTICLEFILTER, // add particle filter to DDB [UUID sender, UUID uuid, UUID owner, int numParticles, _timeb startTime, int stateSize, ... startState]
	OAC_DDB_REMPARTICLEFILTER, // remove particle filter from DDB [UUID sender, UUID uuid]
	OAC_DDB_INSERTPFPREDICTION, // insert prediction [UUID sender, UUID uuid, _timeb time, char nochange, ... state]
	OAC_DDB_APPLYPFCORRECTION, // apply correction [UUID sender, UUID uuid, int regionAge, _timeb time, ... observation desnity]
	OAC_DDB_APPLYPFRESAMPLE, // apply a resample [UUID pf, int numParticles, int stateSize, ... parents, ... weights]
	
	OAC_DDB_ADDAVATAR, // add avatar to DDB [UUID sender, UUID uuid, string type, int status, UUID agent, UUID pf, float innerRadius, float outerRadius, _timeb startTime, int capacity, int sensorTypes]
	OAC_DDB_REMAVATAR, // remove avatar from DDB [UUID sender, UUID uuid]
	OAC_DDB_AVATARSETINFO, // set controller [UUID sender, UUID uuid, int infoFlags, ...data]

	OAC_DDB_ADDSENSOR, // add sensor to DDB [UUID sender, UUID uuid, int type, UUID avatar, UUID pf, ... pose]
	OAC_DDB_REMSENSOR, // remove sensor from DDB [UUID sender, UUID uuid]
	OAC_DDB_INSERTSENSORREADING, // insert sensor reading to DDB [UUID sender, UUID uuid, _timeb tb, int readingSize, reading, int dataSize <, ... data>]

	OAC_DDB_ADDTASK, // add task to DDB [UUID sender, UUID uuid, UUID landmarkUUID, UUID avatar, bool completed, int type]
	OAC_DDB_REMTASK, // remove task from DDB [UUID sender, UUID uuid]
	OAC_DDB_TASKSETINFO, // set task info [UUID sender, UUID uuid, int infoFlags, data...]

	OAC_DDB_ADDTASKDATA, // add taskdata to DDB [UUID sender, UUID uuid, DDBTaskData data]
	OAC_DDB_REMTASKDATA, // remove taskdata from DDB [UUID sender, UUID uuid]
	OAC_DDB_TASKDATASETINFO, // set taskdata info [UUID sender, UUID uuid, DDBTaskData data]
	OAC_DDB_ADDQLEARNINGDATA, //Upload learning data for next run [UUID typeId, <float>qTable, <unsigned int>expTable]


	MSG_RFREEPORT,  // request a free port [UUID thread]
					// RESPONSE: free port [UUID thread, char success <, int port>]
	MSG_RELEASEPORT, // release a port [int port]

	MSG_AGENT_REGISTER, // agent introduces themself to host [UUID uuid]
	MSG_AGENT_GREET,	// agent introduces themself to other agent [UUID uuid, AgentType agentType, ... custom data], custom data may be included in the greeting message, check recieving agent to find out what data is expected
	MSG_AGENT_PROCESS_COST, // update agent process cost (unweighted usage since last update) [UUID uuid, unsigned int usage]
	MSG_AGENT_SHUTDOWN, // agent is willingly shutting down

	MSG_RUNIQUEID,		// request the agent id of a unique agent (instance = -1 for no instance) [UUID type, char instance, UUID thread]
						// RESPONSE: unique id (response = -1 unknown unique, 0 not spawned, 1 spawned)[UUID thread, char response <, UUID uuid>]

	MSG_RAGENT_SPAWN, // request agent spawn (instance = -1 for no instance) [UUID parent, UUID type, char instance, float affinity, char priority, UUID thread]
					  // RESPONSE: agent spawn result [UUID thread, bool succeeded <, UUID id>]
	MSG_AGENT_SETPARENT, // sets agent parent id [UUID parentId]
	MSG_AGENT_INSTANCE, // set agent instance [char instance]
	MSG_AGENT_START, // start agent [string missionFileName]
	MSG_AGENT_STOP, // stop agent
	MSG_AGENT_LOST, // agent is suspected of failing [UUID agent]
	MSG_AGENT_FREEZE, // freeze agent [UUID ticket]
	MSG_AGENT_STATE,  // send agent state [UUID ticket, _timeb stateTime, unsigned int stateSize, state...]
	MSG_AGENT_THAW,	  // thaw agent [UUID ticket, unsigned int size, state...]
	MSG_AGENT_RESUME_READY, // agent is ready to resume [UUID ticket]
	MSG_AGENT_RESUME, // resume agent [combined message queues...]
	MSG_AGENT_BACKUP, // send agent backup [UUID ticket, _timeb backupTime, unsigned int backupSize, backup...]
	MSG_AGENT_RECOVER,// recover from backup [UUID ticket, unsigned int size, backup...]
	MSG_AGENT_RECOVERED, // report recovery result [UUID ticket, int result]
	MSG_AGENT_RECOVER_FINISH, // resume normal operation

	MSG_AGENT_SIMULATECRASH, // crash the agent

	MSG_RGROUP_JOIN,  // request join group [UUID group, UUID agent]
	MSG_RGROUP_LEAVE, // request leave group [UUID group, UUID agent]
	MSG_GROUP_MSG,	  // send group specific message type [UUID group, char gmsg, message data ...]
	MSG_RGROUP_SIZE,  // request the group size [UUID group, UUID thread]
					  // RESPONSE: group size [UUID thread, int size]
	
	MSG_GROUP_MERGE, // agent was part of a merge operation [UUID group, UUID agent]
	MSG_GROUP_JOIN,  // agent joined group [UUID group, UUID agent]
	MSG_GROUP_LEAVE, // agent left group [UUID group, UUID agent]
	MSG__GROUP_SEND, // package structure for group messages [UUID group, unsigned char message, unsigned int len, string data, unsigned int msgSize] 

	MSG_DDB_WATCH_TYPE, // register as a watcher for data type [UUID id, int type]
	MSG_DDB_WATCH_ITEM, // register as a watcher for item [UUID id, UUID item]
	MSG_DDB_STOP_WATCHING_TYPE, // unregister as a watcher for data type [UUID id, int type]
	MSG_DDB_STOP_WATCHING_ITEM, // unregister as a watcher for item [UUID id, UUID item]
	MSG_DDB_NOTIFY,    // notify watcher of event (additional data may be attached to notifications of specific types, check event definition for format)[UUID key, int type, UUID id, char event <, ... data>]

	MSG_DDB_ADDAGENT,  // add agent to DDB [UUID uuid, UUID parentId, AgentType agentType]
	MSG_DDB_REMAGENT,  // remove agent from DDB [UUID uuid]
	MSG_DDB_RAGENTINFO,   // request agent info [UUID uuid, int infoFlags, UUID thread]

	MSG_DDB_VIS_ADDPATH,	// create new path [UUID agent, int pathId, int count, float x[], float y[]]
	MSG_DDB_VIS_REMPATH, // delete path [UUID agent, int pathId]
	MSG_DDB_VIS_EXTENDPATH, // extend path [UUID agent, int pathId, int count, float x[], float y[]]
	MSG_DDB_VIS_UPDATEPATH, // update path [UUID agent, int pathId, int count, int nodes[], float x[], float y[]]
	MSG_DDB_VIS_ADDOBJECT,  // create new object [UUID agent, int objectId, float x, float y, float r, float s, int count, int paths[], float colours[3][], float lineWidths[], bool solid, string name]
	MSG_DDB_VIS_REMOBJECT, // delete object [UUID agent, int objectId]
	MSG_DDB_VIS_UPDATEOBJECT, // update object [UUID agent, int objectId, float x, float y, float r, float s]
	MSG_DDB_VIS_SETOBJECTVISIBLE, // set object visible [UUID agent, int objectId, char visible], 0=off, 1=on, -1=toggle
	MSG_DDB_VIS_CLEAR_ALL, // clear all objects (and optionally paths) [UUID agent, char clearPaths]

	MSG_DDB_ADDREGION, // add region to DDB [UUID uuid, float x, float y, float w, float h]
	MSG_DDB_REMREGION, // remove region from DDB [UUID uuid]
	MSG_DDB_RREGION,   // request region [UUID uuid, UUID thread]
					   // RESPONSE: send region [UUID thread, char response <, float x, float y, float w, float h>] 

	MSG_DDB_ADDLANDMARK, // add landmark to DDB [UUID uuid, unsigned char code, UUID owner, float height, float elevation, float x, float y, char estimatedPos, int landmarkType]
	MSG_DDB_REMLANDMARK, // remove landmark from DDB [UUID uuid]
	MSG_DDB_LANDMARKSETINFO, // update landmark position estimate [unsigned char code, int infoFlags, data...]
	MSG_DDB_RLANDMARK,   // request landmark [UUID uuid, UUID thread]
					     // RESPONSE: send landmark [UUID thread, char response <, DDBLandmark landmark>] 
	MSG_DDB_RLANDMARKBYID, // request landmark by barcode [unsigned char code, UUID thread]
					     // RESPONSE: send region [UUID thread, char response <, DDBLandmark landmark>] 

	MSG_DDB_ADDPOG,	// add POG to DDB [UUID uuid, float tileSize, float resolution]
	MSG_DDB_REMPOG, // remove POG from DDB [UUID uuid]
	MSG_DDB_RPOGINFO,	// request pog info [UUID uuid, UUID thread]
						// RESPONSE: pog info [UUID thread, char response <, float tileSize, float resolution, int stride>]
	MSG_DDB_APPLYPOGUPDATE, // apply pog update [UUID uuid, float x, float y, float w, float h, ... update]
	MSG_DDB_RPOGREGION, // request pog region [UUID uuid, float x, float y, float w, float h, UUID thread]
						// RESPONSE: send pog region [UUID thread, char response <, float x, float y, float w, float h, float resolution, ... region>]
	MSG__DDB_POGLOADREGION, // utility function for loading POG from a file [UUID uuid, float x,  float y, float w, float h, string file name]
	MSG__DDB_POGDUMPREGION, // utility function to dump POG to a file [UUID uuid, float x,  float y, float w, float h, string file name]
	
	MSG_DDB_ADDPARTICLEFILTER, // add particle filter to DDB [UUID uuid, UUID owner, int numParticles, _timeb startTime, int stateSize, ... startState]
	MSG_DDB_REMPARTICLEFILTER, // remove particle filter from DDB [UUID uuid]
	MSG_DDB_INSERTPFPREDICTION, // insert prediction [UUID uuid, _timeb time, char nochange, ... state]
	MSG_DDB_APPLYPFCORRECTION, // apply correction [UUID uuid, _timeb time, ... observation desnity]
	MSG_DDB_RPFINFO,  // request pf info [UUID uuid, int infoFlags, _timeb time, UUID thread]
					  // RESPONSE: send pf info (infoFlags definition for data format) [UUID thread, char response <, int infoFlags, ... data>]
	MSG_DDB_PFRESAMPLEREQUEST, // ddb is requesting a resample [UUID pf, int num particles, ... weights]
	MSG_DDB_SUBMITPFRESAMPLE, // submit resample results [UUID pf, int numParticles, int stateSize, ... parents, ... weights]
//	MSG__DDB_RESAMPLEPF_LOCK, // lock the particle filter when starting a resample [UUID pf, UUID key, UUID thread, UUID host]
							  // RESPONSE: acknowledge lock [UUID thread, UUID us, UUID key]
//	MSG__DDB_RESAMPLEPF_UNLOCK, // unlock the particle filter and distribute results [UUID pf, UUID key, char result, <_timeb tb, int particleNum, int stateSize, ... parents, ... state>]
	MSG__DDB_PFDUMPINFO, // utility function to dump pf info to a file [UUID uuid, int infoFlags, _timeb startT, _timeb endT, string file name]

	MSG_DDB_ADDAVATAR, // add avatar to DDB [UUID uuid, string type, int status, UUID agent, UUID pf, float innerRadius, float outerRadius, _timeb startTime, int capacity, int sensorTypes]
	MSG_DDB_REMAVATAR, // remove avatar from DDB [UUID uuid]
	MSG_DDB_AVATARSETINFO, // set avatar info [UUID uuid, int infoFlags, ...data]
	MSG_DDB_AVATARGETINFO, // request avatar info [UUID uuid, int infoFlags, UUID thread]
						 // RESPONSE: send avatar info (infoFlags definition for data format) [UUID thread, char response <, int infoFlags, ... data>]
	
	MSG_DDB_ADDSENSOR, // add sensor to DDB [UUID uuid, int type, UUID avatar, UUID pf, ... pose]
	MSG_DDB_REMSENSOR, // remove sensor from DDB [UUID uuid]
	MSG_DDB_INSERTSENSORREADING, // insert sensor reading to DDB [UUID uuid, _timeb tb, int readingSize, reading, int dataSize <, ... data>]
	MSG_DDB_RSENSORINFO, // request sensor info [UUID uuid, int infoFlags, UUID thread]
						 // RESPONSE: send sensor info (infoFlags definition for data format) [UUID thread, char response <, int infoFlags, ... data>]
	MSG_DDB_RSENSORDATA, // request sensor data [UUID uuid, _timeb timeb, UUID thread]
						 // RESPONSE: send sensor data [UUID thread, char response <, int readingSize, ... reading, int dataSize, ... data>]

	MSG_DDB_ADDTASK,		// add task to DDB [UUID uuid, UUID landmarkUUID, UUID agent, UUID avatar, bool completed, int type]
	MSG_DDB_REMTASK,		// remove task from DDB [UUID uuid]
	MSG_DDB_TASKGETINFO,	// request task info [UUID uuid, UUID thread, bool enumTasks]
							// RESPONSE: send task info [UUID uuid, UUID landmarkUUID, UUID agent, UUID avatar, bool completed, int type] (if enum is true, send all tasks)
	MSG_DDB_TASKSETINFO,	// set task info [UUID uuid, int infoFlags, ...data]
	
	MSG_DDB_ADDTASKDATA,	 // add taskdata to DDB [UUID uuid, DDBTaskData data]
	MSG_DDB_REMTASKDATA,	 // remove taskdata from DDB [ UUID uuid]
	MSG_DDB_TASKDATAGETINFO, // request taskdata info [UUID uuid, UUID thread, bool enumTaskData] (if enum is true, send all task data)
	MSG_DDB_TASKDATASETINFO, // set taskdata info [UUID uuid, DDBTaskData data]
	MSG_DDB_QLEARNINGDATA,	 // upload individual learning data for next simulation run [UUID ownerId, UUID agentType.uuid, int table_size, float [table_size]qtable, float [table_size]exptable]

    MSG_DDB_RHOSTGROUPSIZE, // request host group size [UUID thread]
							// RESPONSE: send group size [UUID thread, char response, int size]

    MSG_CBBA_DECIDED,    // cbba decision reached [UUID session, char result <, winner list>]

	MSG__DATADUMP_RAVATARPOSES, // request avatar poses for data dump [UUID sender]
	MSG__DATADUMP_AVATARPOSES,  // avatar poses [bool true, UUID avatarId, _timeb time, float x, float y, float r, ..., bool false]
	MSG__DATADUMP_MANUAL,		// manually trigger a data dump [bool fulldump, bool getPose, bool hasLabel <, string label>]

	MSG_DONE_SENSOR_READING,	// finished processing sensor reading [UUID processingAgent, UUID sensor, _timeb tb, char success]

	MSG_COMMON		// highest shared message id, used as a starting point for agent specific messages
};


static const unsigned int MSG_SIZE[] = { // array of message size by message id, initialized in autonomic.cpp
// -1=char, -2=uint, -3=byte+uint

	0,					// MSG_ACK
	0,					// MSG_RACK
	4,					// MSG_ACKEX
	4,					// MSG_RACKEX
	-3,					// MSG_FORWARD

	-3,					// MSG_RESPONSE

	-3,					// MSG_ATOMIC_MSG
	sizeof(UUID)*3 + 4,	// MSG_ATOMIC_MSG_REORDER
	sizeof(UUID)*3 + 4*3 + 1, // MSG_ATOMIC_MSG_ESTIMATE
	sizeof(UUID)*3 + 4*2 + 1, // MSG_ATOMIC_MSG_PROPOSAL
	sizeof(UUID)*3 + 4*2 + 1, // MSG_ATOMIC_MSG_ACK
	sizeof(UUID)*3 + 4*2 + 1, // MSG_ATOMIC_MSG_DECISION
	sizeof(UUID)*2,		// MSG_ATOMIC_MSG_SUSPECT

	4*2 + sizeof(_timeb), // MSG_FD_ALIVE
	4,					// MSG_FD_QOS

	-3,					// OAC_MISSION_START

	1,					// MSG_MISSION_DONE
	1,					// OAC_MISSION_DONE

	-2,					// OAC_GM_REMOVE
	-2,					// OAC_GM_MEMBERSHIP
	-2,					// OAC_GM_FORMATIONFALLBACK

	-2,					// OAC_PA_START
	-2,					// MSG_PA_BID_UPDATE
	-2,					// OAC_PA_FINISH

	-2,					// OAC_STATE_TRANSACTION_BUNDLE

	sizeof(UUID) + 4,		// OAC_DDB_WATCH_TYPE
	sizeof(UUID) + sizeof(UUID), // OAC_DDB_WATCH_ITEM
	sizeof(UUID) + 4,		// OAC_DDB_STOP_WATCHING_TYPE
	sizeof(UUID) + sizeof(UUID), // OAC_DDB_STOP_WATCHING_ITEM
	sizeof(UUID),			// OAC_DDB_CLEAR_WATCHERS

	sizeof(UUID)*4 + sizeof(AgentType) + 1 + 4*3, // OAC_DDB_ADDAGENT
	sizeof(UUID)*2,		// OAC_DDB_REMAGENT
	-3,					// OAC_DDB_AGENTSETINFO
	
	-3,					// OAC_DDB_VIS_ADDPATH
	sizeof(UUID)*2 + 4,	// OAC_DDB_VIS_REMPATH
	-3,					// OAC_DDB_VIS_EXTENDPATH
	-3,					// OAC_DDB_VIS_UPDATEPATH
	-3,					// OAC_DDB_VIS_ADDOBJECT
	sizeof(UUID)*2 + 4,   // OAC_DDB_VIS_REMOBJECT
	sizeof(UUID)*2 + 4*5, // OAC_DDB_VIS_UPDATEOBJECT
	sizeof(UUID)*2 + 4 + 1, // OAC_DDB_VIS_SETOBJECTVISIBLE
	sizeof(UUID)*2 + 1,		// OAC_DDB_VIS_CLEAR_ALL

	sizeof(UUID)*2 + 4*4,	// OAC_DDB_ADDREGION
	sizeof(UUID)*2,			// OAC_DDB_REMREGION

	sizeof(UUID)*2 + 1 + sizeof(UUID) + 4*4 + 1 + sizeof(ITEM_TYPES), // OAC_DDB_ADDLANDMARK
	sizeof(UUID)*2,			// OAC_DDB_REMLANDMARK
	-2,						// OAC_DDB_LANDMARKSETINFO

	sizeof(UUID)*2 + 4 + 4,	// OAC_DDB_ADDPOG
	sizeof(UUID)*2,			// OAC_DDB_REMPOG
	-2,						// OAC_DDB_APPLYPOGUPDATE
	-2,						// OAC_DDB_POGLOADREGION

	-2,						// OAC_DDB_ADDPARTICLEFILTER
	sizeof(UUID)*2,			// OAC_DDB_REMPARTICLEFILTER
	-2,						// OAC_DDB_INSERTPFPREDICTION
	-2,						// OAC_DDB_APPLYPFCORRECTION
	-2,						// OAC_DDB_APPLYPFRESAMPLE

	-2,						// OAC_DDB_ADDAVATAR
	sizeof(UUID)*2,			// OAC_DDB_REMAVATAR
	-2,						// OAC_DDB_AVATARSETINFO

	-2,						// OAC_DDB_ADDSENSOR
	sizeof(UUID)*2,			// OAC_DDB_REMSENSOR
	-2,						// OAC_DDB_INSERTSENSORREADING

	sizeof(UUID)*5 + 1 + 4, // OAC_DDB_ADDTASK [UUID sender, UUID uuid, UUID landmarkUUID, UUID agent, UUID avatar, bool completed, int type]
	sizeof(UUID)*2,			// OAC_DDB_REMTASK, remove task from DDB [UUID sender, UUID uuid]
	-2,						// OAC_DDB_TASKSETINFO, set task info [UUID sender, UUID uuid, int infoFlags, data...]
	
	-2, //sizeof(UUID) * 2 + sizeof(DDBTaskData),	//OAC_DDB_ADDTASKDATA [UUID sender, UUID uuid, DDBTaskData data]
	sizeof(UUID) * 2,		//OAC_DDB_REMTASKDATA [UUID sender, UUID uuid]
	-2, //sizeof(UUID) * 2 + sizeof(DDBTaskData),  //OAC_DDB_TASKDATASETINFO [UUID sender, UUID uuid, DDBTaskData data]
	-2,	//					OAC_DDB_ADDQLEARNINGDATA

	sizeof(UUID),		// MSG_RFREEPORT
	4,					// MSG_RELEASEPORT

	sizeof(UUID),	    // MSG_AGENT_REGISTER
	-3,					// MSG_AGENT_GREET
	sizeof(UUID) + 4,   // MSG_AGENT_PROCESS_COST
	0,					// MSG_AGENT_SHUTDOWN
					
	sizeof(UUID) + 1 + sizeof(UUID), // MSG_RUNIQUEID

	sizeof(UUID)*2 + 1 + 4 + 1 + sizeof(UUID), // MSG_RAGENT_SPAWN
	sizeof(UUID),		// MSG_AGENT_SETPARENT
	1,					// MSG_AGENT_INSTANCE
	-3,					// MSG_AGENT_START
	0,					// MSG_AGENT_STOP
	sizeof(UUID),		// MSG_AGENT_LOST
	sizeof(UUID),		// MSG_AGENT_FREEZE
	-3,					// MSG_AGENT_STATE
	-3,					// MSG_AGENT_THAW
	sizeof(UUID),		// MSG_AGENT_RESUME_READY
	-3,					// MSG_AGENT_RESUME
	-3,					// MSG_AGENT_BACKUP
	-3,					// MSG_AGENT_RECOVER
	sizeof(UUID) + 4,	// MSG_AGENT_RECOVERED
	0,					// MSG_AGENT_RECOVER_FINISH

	0,					// MSG_AGENT_SIMULATECRASH

	sizeof(UUID)*2,		// MSG_RGROUP_JOIN
	sizeof(UUID)*2,		// MSG_RGROUP_LEAVE
	-3,					// MSG_GROUP_MSG
	sizeof(UUID)*2,		// MSG_RGROUP_SIZE

	sizeof(UUID)*2,		// MSG_GROUP_MERGE
	sizeof(UUID)*2,		// MSG_GROUP_JOIN
	sizeof(UUID)*2,		// MSG_GROUP_LEAVE
	-2,					// MSG__GROUP_SEND

	sizeof(UUID) + 4,	// MSG_DDB_WATCH_TYPE
	sizeof(UUID) * 2,	// MSG_DDB_WATCH_ITEM
	sizeof(UUID) + 4,	// MSG_DDB_STOP_WATCHING_TYPE
	sizeof(UUID) * 2,	// MSG_DDB_STOP_WATCHING_ITEM
	-2,					// MSG_DDB_NOTIFY
	
	sizeof(UUID)*2 + sizeof(AgentType), // MSG_DDB_ADDAGENT
	sizeof(UUID),		// MSG_DDB_REMAGENT
	sizeof(UUID)*2 + 4, // MSG_DDB_RAGENTINFO
	
	-3,					// MSG_DDB_VIS_ADDPATH
	sizeof(UUID) + 4,	// MSG_DDB_VIS_REMPATH
	-3,					// MSG_DDB_VIS_EXTENDPATH
	-3,					// MSG_DDB_VIS_UPDATEPATH
	-3,					// MSG_DDB_VIS_ADDOBJECT
	sizeof(UUID) + 4,   // MSG_DDB_VIS_REMOBJECT
	sizeof(UUID) + 4*5, // MSG_DDB_VIS_UPDATEOBJECT
	sizeof(UUID) + 4 + 1, //MSG_DDB_VIS_SETOBJECTVISIBLE
	sizeof(UUID) + 1 ,	// MSG_DDB_VIS_CLEAR_ALL

	sizeof(UUID) + 4*4,	// MSG_DDB_ADDREGION
	sizeof(UUID),		// MSG_DDB_REMREGION
	sizeof(UUID)*2,		// MSG_DDB_RREGION

	sizeof(UUID) + 1 + sizeof(UUID) + 4*4 + 1 + sizeof(ITEM_TYPES), // MSG_DDB_ADDLANDMARK
	sizeof(UUID),		// MSG_DDB_REMLANDMARK
	-2,					// MSG_DDB_LANDMARKSETINFO
	sizeof(UUID)*2,		// MSG_DDB_RLANDMARK
	1 + sizeof(UUID),	// MSG_DDB_RLANDMARKBYID

	sizeof(UUID) + 4 + 4, //MSG_DDB_ADDPOG
	sizeof(UUID),		// MSG_DDB_REMPOG
	sizeof(UUID)*2,		// MSG_DDB_RPOGINFO
	-2,					// MSG_DDB_APPLYPOGUPDATE
	sizeof(UUID)*2 + 4*4, // MSG_DDB_RPOGREGION
	-2,					// MSG__DDB_POGLOADREGION
	-2,					// MSG__DDB_POGDUMPREGION
	
	-2,					// MSG_DDB_ADDPARTICLEFILTER
	sizeof(UUID),		// MSG_DDB_REMPARTICLEFILTER
	-2,					// MSG_DDB_INSERTPFPREDICTION
	-2,					// MSG_DDB_APPLYPFCORRECTION
	sizeof(UUID) + 4 + sizeof(_timeb) + sizeof(UUID), // MSG_DDB_RPFINFO
	-2,					// MSG_DDB_PFRESAMPLEREQUEST
	-2,					// MSG_DDB_SUBMITPFRESAMPLE
//	sizeof(UUID)*4,		// MSG__DDB_RESAMPLEPF_LOCK
//	-2,					// MSG__DDB_RESAMPLEPF_UNLOCK
	-2,					// MSG__DDB_PFDUMPINFO

	-3,					// MSG_DDB_ADDAVATAR
	sizeof(UUID),		// MSG_DDB_REMAVATAR
	-2,					// MSG_DDB_AVATARSETINFO
	sizeof(UUID) + 4 + sizeof(UUID), // MSG_DDB_AVATARGETINFO

	-1,					// MSG_DDB_ADDSENSOR
	sizeof(UUID),		// MSG_DDB_REMSENSOR
	-2,					// MSG_DDB_INSERTSENSORREADING
	sizeof(UUID) + 4 + sizeof(UUID), // MSG_DDB_RSENSORINFO
	sizeof(UUID) + sizeof(_timeb) + sizeof(UUID), // MSG_DDB_RSENSORDATA

	sizeof(UUID) + sizeof(UUID) + sizeof(UUID) + sizeof(UUID) + 1 + 4, 	//MSG_DDB_ADDTASK
	sizeof(UUID), 											//MSG_DDB_REMTASK
	sizeof(UUID) + sizeof(UUID) + sizeof(bool), 			//MSG_DDB_TASKGETINFO
	-2,														//MSG_DDB_TASKSETINFO

	-2,//sizeof(UUID) + sizeof(DDBTaskData),					//MSG_DDB_ADDTASKDATA
	sizeof(UUID),										//MSG_DDB_REMTASKDATA
	sizeof(UUID) + sizeof(UUID) + sizeof(bool),			//MSG_DDB_TASKDATAGETINFO
	-2,//sizeof(UUID) + sizeof(DDBTaskData),					//MSG_DDB_TASKDATASETINFO
	-2,					// MSG_DDB_QLEARNINGDATA

	sizeof(UUID),		// MSG_DDB_RHOSTGROUPSIZE

	-3,					// MSG_CBBA_DECIDED

	sizeof(UUID),		// MSG__DATADUMP_RAVATARPOSES
	-2,					// MSG__DATADUMP_AVATARPOSES
	-3,					// MSG__DATADUMP_MANUAL
	
	sizeof(UUID)*2 + sizeof(_timeb) + 1, // MSG_DONE_SENSOR_READING
};