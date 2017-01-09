#define AgentTeamLearning_MAJOR 0
#define AgentTeamLearning_MINOR 1
#define AgentTeamLearning_BUILDNO 19
#define AgentTeamLearning_EXTEND 0
#define AgentTeamLearning_UUID "5dda9455-7d18-495d-b7af-b30343577114" // Need to generate new one
#define AgentTeamLearning_PROCESS_COST 0.05, 0.05
#define AgentTeamLearning_TRANSFER_PENALTY 0.1
#define AgentTeamLearning_RESOURCE_REQUIREMENTS ""

#define Task_UUID "c741f5de-7b83-402a-a80c-e933c781aece" // Need to generate new one 

namespace AgentTeamLearning_MSGS {

	enum {
		MSG_FIRST = MSG_COMMON  // extend message ids from here (use enum here because #define causes problems)
	};

	// !! NOTE !! Make sure the order matches betwen MSGS and MSG_SIZE

	enum MSGS {
		MSG_CONFIGURE = MSG_FIRST,	   // configure [UUID owner]
		MSG_REQUEST_ACQUIESCENCE,	   // recAcq [UUID thread]
		MSG_REQUEST_MOTRESET,	       // recAcq [UUID task]
		MSG_ROUND_INFO,                // Information for the next round of learning/task allocation [int round_number, _timeb round_start_time, UUID agentId_1, UUID agentId_2, ...]
		MSG_LAST					   // last message id
	};
	
	static const unsigned int MSG_SIZE[] = { // array of message size by MSGID - MSG_FIRST
		sizeof(UUID),//sizeof(UUID),					// MSG_CONFIGURE
		sizeof(UUID) + sizeof(UUID) + sizeof(UUID),		//MSG_REQUEST_ACQUIESCENCE
		sizeof(UUID) + sizeof(UUID) + sizeof(UUID),		//MSG_REQUEST_MOTRESET
		-2,                                             // MSG_ROUND_INFO
	};

}