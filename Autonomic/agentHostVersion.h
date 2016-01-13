#define AgentHost_MAJOR 0
#define AgentHost_MINOR 1
#define AgentHost_BUILDNO 0
#define AgentHost_EXTEND 0
#define AgentHost_UUID "14728396-ca2c-4220-ad4d-c2ea18c72ddc"
#define AgentHost_PROCESS_COST 0.5, 0.5
#define AgentHost_TRANSFER_PENALTY 1
#define AgentHost_RESOURCE_REQUIREMENTS ""

namespace AgentHost_MSGS {

	enum {
		MSG_FIRST = MSG_COMMON  // extend message ids from here (use enum here because #define causes problems)
	};

	// !! NOTE !! Make sure the order matches betwen MSGS and MSG_SIZE

	enum MSGS {
		MSG_RSUPERVISOR_REFRESH = MSG_FIRST, // request refresh from supervisor
		MSG_HOST_INTRODUCE, // introduce this connection as a host [UUID uuid]
		MSG_HOST_SHUTDOWN,  // this host is shutting down
		MSG_HOST_LABEL,		// send host lable [UUID uuid, U32 stub age, U32 state age]
		MSG_RHOST_LABEL,	// request host label [UUID uuid]
		MSG_HOST_STUB,		// send host stub [UUID uuid, U32 stub age, ... stub data]
		MSG_RHOST_STUB,		// request host stub [UUID uuid]
		MSG_HOST_STATE,		// send host state [UUID uuid, U32 state age, ... state data]
		MSG_RHOST_STATE,	// request host state [UUID uuid]
		MSG_OTHER_STATUS,	// confirm that another host is active [UUID uuid]
		MSG_ROTHER_STATUS,	// query a host about the status of another [UUID uuid]
		MSG_AGENTSPAWNPROPOSAL,			// submit an agent spawn proposal [UUID ticket, float favourable]
		MSG_RAGENTSPAWNPROPOSAL,		// request an agent spawn proposal from host [UUID ticket, UUID typeId, char instance]
		MSG_ACCEPTAGENTSPAWNPROPOSAL,	// accept an agent spawn proposal [UUID ticket]
		MSG_AGENTSPAWNSUCCEEDED,		// agent spawn succeeding [UUID ticket, UUID agentId]
		MSG_AGENTSPAWNFAILED,			// agent spawn failed [UUID ticket]
		MSG_AGENT_KILL,		// kill agent [UUID agentId]
		MSG_AGENT_ACK_STATUS, // acknowledge status change [UUID from, UUID agent, UUID ticket]
		MSG_CLOSE_DUPLICATE, // request close duplicate connection

		MSG_GM_LEAVE,		// request to leave group [UUID q]
		MSG_GM_APPLY,		// apply for group membership [UUID host, sAddressPort ap, int64 key1, int64 key2]
		MSG_GM_INTRODUCE,	// introduce new applicant to group members and applicants [UUID q, UUID a, sAddressPort ap]
		MSG_GM_SPONSOR,		// introduce self to new sponsee [UUID q]
		MSG_GM_CONNECTIONS,	// connection update for sponsor [UUID q, char globalStateSynced, int numConnections, <connection list>]
		MSG_GM_ACKLOCK,		// acknowledge lock [UUID q, UUID key]
		MSG_GM_REJECT,		// reject application
		MSG_GM_GLOBALSTATEEND, // confirm end of global state [UUID q]

		MSG_MIRROR_REGISTER, // register a new DDB mirror [UUID agent, AgentType agentType]

		MSG_DDB_PFRESAMPLE_CHECK, // tell the leader to check the pf state [UUID agent] 

		MSG_DISTRIBUTE_ADD_AGENT, // new agent added [UUID agent, AgentType agentType]
		MSG_DISTRIBUTE_REM_AGENT, // agent removed [UUID host, UUID agent]
		MSG_DISTRIBUTE_ADD_UNIQUE, // add unique agent [AgentType agentType]
		MSG_DISTRIBUTE_AGENT_UPDATE_PROCESS_COST, // update agent process cost [UUID agent, float weightedCost]
		MSG_DISTRIBUTE_AGENT_UPDATE_AFFINITY, // update agent affinity [UUID agent, UUID affinity, unsigned int size]
		MSG_DISTRIBUTE_AGENT_LOST, // agent lost [UUID agent, int reason]

		MSG_DISTRIBUTE_ADD_MIRROR, // add a new DDB mirror [UUID agent]
		MSG_DISTRIBUTE_REM_MIRROR, // remove a DDB mirror [UUID agent]

		MSG_RGROUP_MERGE,			// request group merge [UUID group, bool true, UUID agent, _timeb joinTime, ..., bool false]
		MSG_DISTRIBUTE_GROUP_MERGE, // add merged group members [UUID group, bool true, UUID agent, _timeb joinTime, ..., bool false]
		MSG_DISTRIBUTE_GROUP_JOIN,  // add new group member [UUID group, UUID agent, _timeb joinTime]
		MSG_DISTRIBUTE_GROUP_LEAVE, // remove group member [UUID group, UUID agent]

		MSG_DDB_ENUMERATE,	// enumerate DDB type [int type, UUID id, data..., repeat until you encounter type = DDB_INVALID]
							// see Enumerate* functions in DDBStore for data format of each type

		MSG_LAST			// last message id
	};
	
	static const unsigned int MSG_SIZE[] = { // array of message size by MSGID - MSG_FIRST
		0,						// MSG_SUPERVISOR_REQUEST_REFRESH
		sizeof(UUID),			// MSG_HOST_INTRODUCE
		0,						// MSG_HOST_SHUTDOWN
		sizeof(UUID) + 4 + 4,	// MSG_HOST_LABEL
		sizeof(UUID),			// MSG_RHOST_LABEL
		-2,						// MSG_HOST_STUB
		sizeof(UUID),			// MSG_RHOST_STUB
		-2,						// MSG_HOST_STATE
		sizeof(UUID),			// MSG_RHOST_STATE
		sizeof(UUID),			// MSG_OTHER_STATUS
		sizeof(UUID),			// MSG_ROTHER_STATUS
		sizeof(UUID) + 4,		// MSG_AGENTSPAWNPROPOSAL
		sizeof(UUID) + sizeof(UUID) + 1, // MSG_RAGENTSPAWNPROPOSAL
		sizeof(UUID),			// MSG_ACCEPTAGENTSPAWNPROPOSAL
		sizeof(UUID) + sizeof(UUID), // MSG_AGENTSPAWNSUCCEEDED
		sizeof(UUID),			// MSG_AGENTSPAWNFAILED
		sizeof(UUID),			// MSG_AGENT_KILL
		sizeof(UUID)*3,			// MSG_AGENT_ACK_STATUS
		0,						// MSG_CLOSE_DUPLICATE

		sizeof(UUID),			// MSG_GM_LEAVE
		sizeof(UUID) + sizeof(sAddressPort) + sizeof(__int64)*2,  // MSG_GM_APPLY
		sizeof(UUID)*2 + sizeof(sAddressPort),  // MSG_GM_INTRODUCE
		sizeof(UUID),			// MSG_GM_SPONSOR
		-2,						// MSG_GM_CONNECTIONS
		sizeof(UUID)*2,			// MSG_GM_ACKLOCK
		0,						// MSG_GM_REJECT
		sizeof(UUID),			// MSG_GM_GLOBALSTATEEND

		sizeof(UUID) + sizeof(AgentType), // MSG_MIRROR_REGISTER

		sizeof(UUID),			// MSG_DDB_PFRESAMPLE_CHECK

		sizeof(UUID) + sizeof(AgentType), // MSG_DISTRIBUTE_ADD_AGENT
		sizeof(UUID) + sizeof(UUID), // MSG_DISTRIBUTE_REM_AGENT
		sizeof(AgentType),		// MSG_DISTRIBUTE_ADD_UNIQUE
		
		sizeof(UUID) + 4,		// MSG_DISTRIBUTE_AGENT_UPDATE_PROCESS_COST
		sizeof(UUID)*2 + 4,		// MSG_DISTRIBUTE_AGENT_UPDATE_AFFINITY
		sizeof(UUID) + 4,		// MSG_DISTRIBUTE_AGENT_LOST

		sizeof(UUID),			// MSG_DISTRIBUTE_ADD_MIRROR
		sizeof(UUID),			// MSG_DISTRIBUTE_REM_MIRROR

		-2,						// MSG_RGROUP_MERGE
		-2,						// MSG_DISTRIBUTE_GROUP_MERGE
		sizeof(UUID)*2 + sizeof(_timeb), // MSG_DISTRIBUTE_GROUP_JOIN
		sizeof(UUID)*2,			// MSG_DISTRIBUTE_GROUP_LEAVE

		-2,						// MSG_DDB_ENUMERATE

	};

}