#define SupervisorForage_MAJOR 0
#define SupervisorForage_MINOR 1
#define SupervisorForage_BUILDNO 0
#define SupervisorForage_EXTEND 0
#define SupervisorForage_UUID "b18b85cd-fc5f-4b80-a206-16ef174d0542"
#define SupervisorForage_PROCESS_COST 0.05, 0.05
#define SupervisorForage_TRANSFER_PENALTY 0.1
#define SupervisorForage_RESOURCE_REQUIREMENTS ""

namespace SupervisorForage_MSGS {

	enum {
		MSG_FIRST = MSG_COMMON  // extend message ids from here (use enum here because #define causes problems)
	};

	// !! NOTE !! Make sure the order matches betwen MSGS and MSG_SIZE

	enum MSGS {
		MSG_CONFIGURE = MSG_FIRST, // configure parameters [UUID avatarExecId, UUID landmarkId, bool true, UUID regionId, DDBRegion region, ..., bool false]
		
		MSG_LAST			// last message id
	};
	
	static const unsigned int MSG_SIZE[] = { // array of message size by MSGID - MSG_FIRST
		-2,			// MSG_CONFIGURE
	};

}