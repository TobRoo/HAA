#define AvatarSimulation_MAJOR 0
#define AvatarSimulation_MINOR 1
#define AvatarSimulation_BUILDNO 284
#define AvatarSimulation_EXTEND 0
#define AvatarSimulation_UUID "32cccc66-41f3-4ff6-8170-06e1d2252b99"
#define AvatarSimulation_PROCESS_COST 0.05, 0.05
#define AvatarSimulation_TRANSFER_PENALTY 0.1
#define AvatarSimulation_RESOURCE_REQUIREMENTS ""

namespace AvatarSimulation_MSGS {

	enum {
		MSG_FIRST = AvatarBase_MSGS::MSG_LAST  // extend message ids from here (use enum here because #define causes problems)
	};

	// !! NOTE !! Make sure the order matches betwen MSGS and MSG_SIZE

	enum MSGS {
		MSG_COLLECT_LANDMARK = MSG_FIRST, // simulate collection task [unsigned char code, float x, float y, UUID initiator, UUID thread]
										  // RESPONSE: [UUID thread, char success]
	    MSG_DEPOSIT_LANDMARK,			  // simulate dropping off landmark [unsigned char code]

		MSG_LAST			// last message id
	};
	
	static const unsigned int MSG_SIZE[] = { // array of message size by MSGID - MSG_FIRST
		1 + 4*2 + sizeof(UUID)*2,	// MSG_COLLECT_LANDMARK
		1 + 4*2 + sizeof(UUID)*2,							// MSG_DEPOSIT_LANDMARK
	};

}