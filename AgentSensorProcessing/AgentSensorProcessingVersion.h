#define AgentSensorProcessing_MAJOR 0
#define AgentSensorProcessing_MINOR 1
#define AgentSensorProcessing_BUILDNO 0
#define AgentSensorProcessing_EXTEND 0
#define AgentSensorProcessing_UUID "14beae8a-dffa-4758-a9cd-02ebd1a809d6"

namespace AgentSensorProcessing_MSGS {

	enum {
		MSG_FIRST = MSG_COMMON  // extend message ids from here (use enum here because #define causes problems)
	};

	// !! NOTE !! Make sure the order matches betwen MSGS and MSG_SIZE

	enum MSGS {
		MSG_CONFIGURE = MSG_FIRST,	// configure [UUID owner, UUID map]
		MSG_ADD_READING, // add reading to process [UUID uuid, _timeb tb]
		
		MSG_LAST			// last message id
	};
	
	static const unsigned int MSG_SIZE[] = { // array of message size by MSGID - MSG_FIRST
		sizeof(UUID) + sizeof(UUID),	// MSG_CONFIGURE
		sizeof(UUID) + sizeof(_timeb),	// MSG_ADD_READING
	};

}
