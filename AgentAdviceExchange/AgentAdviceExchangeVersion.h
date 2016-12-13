#define AgentAdviceExchange_MAJOR 0
#define AgentAdviceExchange_MINOR 1
#define AgentAdviceExchange_BUILDNO 19
#define AgentAdviceExchange_EXTEND 0
#define AgentAdviceExchange_UUID "3d67754a-a314-43d6-a773-a1c812c977b3" 
#define AgentAdviceExchange_PROCESS_COST 0.05, 0.05
#define AgentAdviceExchange_TRANSFER_PENALTY 0.1
#define AgentAdviceExchange_RESOURCE_REQUIREMENTS ""

namespace AgentAdviceExchange_MSGS {

	enum {
		MSG_FIRST = MSG_COMMON  // extend message ids from here (use enum here because #define causes problems)
	};

	// !! NOTE !! Make sure the order matches betwen MSGS and MSG_SIZE

	enum MSGS {
		MSG_CONFIGURE = MSG_FIRST, // configure [UUID owner]	//TODO: To be updated with more...
		MSG_REQUEST_ADVICE,        // Requesting advice from AgentAdviceExchange [UUID owner, UUID convo, float q[0], float q[1], ..., unsigned int state[0], unsigned int state[1], ...]
		MSG_REQUEST_Q_VALUES,      // Requesting Q-values from Q-learning [UUID owner, UUID convo, float q[0], float q[1], ..., unsigned int state[0], unsigned int state[1], ...]
		MSG_REQUEST_CAPACITY,	   // Request capacity value from AgentAdviceExchange [TODO............................]
		MSG_LAST			       // last message id
	};
	
	static const unsigned int MSG_SIZE[] = { // array of message size by MSGID - MSG_FIRST
		-2,					// MSG_CONFIGURE
		-2,                 // MSG_REQUEST_ADVICE
		-2,                 // MSG_REQUEST_Q_VALUES
		-2,					//MSG_REQUEST_CAPACITY
	};

}