#define SupervisorSLAM_MAJOR 0
#define SupervisorSLAM_MINOR 1
#define SupervisorSLAM_BUILDNO 49
#define SupervisorSLAM_EXTEND 0
#define SupervisorSLAM_UUID "f2a7be61-7c84-4142-9a26-c9950258c88c"
#define SupervisorSLAM_PROCESS_COST 0.05, 0.05
#define SupervisorSLAM_TRANSFER_PENALTY 0.1
#define SupervisorSLAM_RESOURCE_REQUIREMENTS ""

namespace SupervisorSLAM_MSGS {

	enum {
		MSG_FIRST = MSG_COMMON  // extend message ids from here (use enum here because #define causes problems)
	};

	// !! NOTE !! Make sure the order matches betwen MSGS and MSG_SIZE

	enum MSGS {
		MSG_ADD_MAP = MSG_FIRST,	// add map [UUID uuid]

		MSG_LAST			// last message id
	};
	
	static const unsigned int MSG_SIZE[] = { // array of message size by MSGID - MSG_FIRST
		sizeof(UUID),					// MSG_ADD_MAP
	};

}
