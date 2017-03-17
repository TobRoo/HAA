
#include "stdafx.h"

#include "autonomic.h"
#include "agentHost.h"
#include "agentHostVersion.h"

#include "io.h"

#include <fstream>
#include <iostream>
#include <iomanip>


#include "..\AgentIndividualLearning\AgentIndividualLearningVersion.h"
#include "..\AgentTeamLearning\AgentTeamLearningVersion.h"
#include "..\AgentAdviceExchange\AgentAdviceExchangeVersion.h"
#include "..\ExecutiveSimulation\ExecutiveSimulationVersion.h"

// TEMP
#include "time.h"

#define round(val) floor((val) + 0.5f)

#ifdef _DEBUG
#using <System.dll>
#define TRACE
#endif

int UUIDLock_Throw( UUIDLock *lock, UUID *tumbler, UUID *key ) {
	if ( *key != lock->key )
		return 1;

	lock->tumbler.remove( *tumbler );
	return 0;
}

//*****************************************************************************
// AgentHost

//-----------------------------------------------------------------------------
// Constructor	
AgentHost::AgentHost( char *libraryPath, int logLevel, char *logDirectory, char playbackMode, char *playbackFile, int runNumber ) : AgentBase( NULL, NULL, logLevel, logDirectory, playbackMode, playbackFile ) {

	// allocate state
	ALLOCATE_STATE( AgentHost, AgentBase )
	STATE(AgentHost)->hostStats = new mapConnectionStatistics();
	STATE(AgentHost)->agentTemplateInstances = new mapAgentTemplateInstances();

	this->keyboardBuf[0] = '\0'; // clear buf

	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AgentHost" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AgentHost_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	STATE(AgentBase)->noCrash = false; // ok to crash

	this->offlineSLAMmode = -1; // unset

	this->missionDone = false;

	UuidFromString( (RPC_WSTR)_T(OAC_GLOBAL), &typeId );
	this->oac_GLOBAL = typeId;
	this->globalStateTransactionInProgress = false;

	this->gracefulExitWaitingOnOACs = false;

	// host membership
	this->gmApplyTo = NULL;
	this->gmIntroduceTo = NULL;
	this->gmOACRemoveAndMembershipCount = 0;
	this->gmKey = nilUUID;
	this->gmWaitingUpdateMembership = false;
	this->gmCoreHosts = NULL; 
	this->gmLocked = nilUUID; 
	this->gmUpdatingMembers = false;
	this->gmSponsor = nilUUID; 
	this->gmGroupCorrect = false;

	this->paSession.id = 0;


	UuidFromString( (RPC_WSTR)_T(GROUP_HOSTS), &typeId );
	this->groupHostId = typeId;

	this->supervisorAP = NULL;

	this->serverCon = NULL;
	this->localCon = NULL;

	memset( this->usedPorts, 0, sizeof(char)*MAX_PORTS );

	strcpy_s( this->libraryPath, sizeof(this->libraryPath), libraryPath );

	this->dStore = new DDBStore( this->apb, &this->Log );

	this->gatherData = false;
	Data.setAgentPlayback( this->apb );

	this->cbbaQueued = nilUUID;
	
	STATE(AgentHost)->runNumber = runNumber;

	// Prepare callbacks
	this->callback[AgentHost_CBR_cbCleanExitCheck] = NEW_MEMBER_CB(AgentHost,cbCleanExitCheck);
	this->callback[AgentHost_CBR_cbDelayedAgentSpawn] = NEW_MEMBER_CB(AgentHost,cbDelayedAgentSpawn);
	//this->callback[AgentHost_CBR_cbSupervisorWatcher] = NEW_MEMBER_CB(AgentHost,cbSupervisorWatcher);
	this->callback[AgentHost_CBR_cbWatchHostConnection] = NEW_MEMBER_CB(AgentHost,cbWatchHostConnection);
	this->callback[AgentHost_CBR_cbHostFormationTimeout] = NEW_MEMBER_CB(AgentHost,cbHostFormationTimeout);
	this->callback[AgentHost_CBR_cbHostConCleanDuplicate] = NEW_MEMBER_CB(AgentHost,cbHostConCleanDuplicate);
	this->callback[AgentHost_CBR_cbHostStatusTimeout] = NEW_MEMBER_CB(AgentHost,cbHostStatusTimeout);
	this->callback[AgentHost_CBR_cbDelayGlobalQuery] = NEW_MEMBER_CB(AgentHost,cbDelayGlobalQuery);
	this->callback[AgentHost_CBR_cbWatchAgentConnection] = NEW_MEMBER_CB(AgentHost,cbWatchAgentConnection);
	this->callback[AgentHost_CBR_cbGlobalStateTransaction] = NEW_MEMBER_CB(AgentHost,cbGlobalStateTransaction);
	this->callback[AgentHost_CBR_cbSpawnAgentExpired] = NEW_MEMBER_CB(AgentHost,cbSpawnAgentExpired);
	this->callback[AgentHost_CBR_cbCBBABuildQueued] = NEW_MEMBER_CB(AgentHost,cbCBBABuildQueued);
	this->callback[AgentHost_CBR_cbCBBADistributeQueued] = NEW_MEMBER_CB(AgentHost,cbCBBADistributeQueued);
	this->callback[AgentHost_CBR_cbCBBAStartQueued] = NEW_MEMBER_CB(AgentHost,cbCBBAStartQueued);
	this->callback[AgentHost_CBR_cbAffinityCurBlock] = NEW_MEMBER_CB(AgentHost,cbAffinityCurBlock);
	this->callback[AgentHost_CBR_convRequestUniqueSpawn] = NEW_MEMBER_CB(AgentHost,convRequestUniqueSpawn);
//	this->callback[AgentHost_CBR_convDDBResamplePF_Lock] = NEW_MEMBER_CB(AgentHost,convDDBResamplePF_Lock);
	this->callback[AgentHost_CBR_cbRetire] = NEW_MEMBER_CB(AgentHost,cbRetire);
	this->callback[AgentHost_CBR_cbDataDump] = NEW_MEMBER_CB(AgentHost,cbDataDump);
	this->callback[AgentHost_CBR_cbQueueMission] = NEW_MEMBER_CB(AgentHost,cbQueueMission);
	this->callback[AgentHost_CBR_cbPFResampleTimeout] = NEW_MEMBER_CB(AgentHost,cbPFResampleTimeout);
	this->callback[AgentHost_CBR_cbCBBAQueued] = NEW_MEMBER_CB(AgentHost,cbCBBAQueued);
	this->callback[AgentHost_CBR_cbQueueCloseConnection] = NEW_MEMBER_CB(AgentHost,cbQueueCloseConnection);
	this->callback[AgentHost_CBR_cbMissionDone] = NEW_MEMBER_CB(AgentHost, cbMissionDone);
}

//-----------------------------------------------------------------------------
// Destructor
AgentHost::~AgentHost() {

	#ifdef TRACE
	System::Diagnostics::Trace::WriteLine( "--- ~AgentHost(): 0 ---" );
	#endif

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

	#ifdef TRACE
	System::Diagnostics::Trace::WriteLine( "--- ~AgentHost(): 1 ---" );
	#endif

	spAddressPort ap;
	while ( this->gmApplyTo ) { 
		ap = this->gmApplyTo->next; 
		free( this->gmApplyTo ); 
		this->gmApplyTo = ap; 
	}	
	while ( this->gmIntroduceTo ) { 
		ap = this->gmIntroduceTo->next; 
		free( this->gmIntroduceTo ); 
		this->gmIntroduceTo = ap; 
	}	
	while ( this->gmCoreHosts ) { 
		ap = this->gmCoreHosts->next; 
		free( this->gmCoreHosts ); 
		this->gmCoreHosts = ap; 
	}
		
	#ifdef TRACE
	System::Diagnostics::Trace::WriteLine( "--- ~AgentHost(): 2 ---" );
	#endif

	// clean up agentLibrary
	mapAgentTemplate::iterator iterAT;
	mapAgentTemplateInstances::iterator iterATI;
	for ( iterAT = this->agentLibrary.begin(); iterAT != this->agentLibrary.end(); iterAT++ ) {
		//iterATI = STATE(AgentHost)->agentTemplateInstances->find( iterAT->first );
		//if ( iterATI != STATE(AgentHost)->agentTemplateInstances->end() && iterATI->second != -1 ) {
		//	//TODO: this causes problems so make sure all agents exit before destroying the host
		//	//FreeLibrary( (HINSTANCE)iterAT->second->vp );
		//}
		free( (spAgentTemplate)iterAT->second );
	}
			
	#ifdef TRACE
	System::Diagnostics::Trace::WriteLine( "--- ~AgentHost(): 3 ---" );
	#endif

	delete this->dStore;

	while ( !this->globalStateQueue.empty() ) {
		list<StateTransaction_MSG>::iterator iM = this->globalStateQueue.begin();

		if ( iM->len )
			free( iM->data );

		this->globalStateQueue.pop_front();
	}

	map<UUID,list<DDBAgent_MSG>,UUIDless>::iterator iNQ;
	list<DDBAgent_MSG>::iterator iN;
	for ( iNQ = this->ddbNotificationQueue.begin(); iNQ != this->ddbNotificationQueue.end(); iNQ++ ) {
		for ( iN = iNQ->second.begin(); iN != iNQ->second.end(); iN++ ) {
			if ( iN->len )
				free( iN->data );
		}
	}
	this->ddbNotificationQueue.clear();

	while ( !this->PFHeldCorrections.empty() ) {
		map<UUID,list<DDBParticleFilter_Correction>,UUIDless>::iterator iPFH = this->PFHeldCorrections.begin();
		
		std::list<DDBParticleFilter_Correction>::iterator iterPFC = iPFH->second.begin();
		while ( iterPFC != iPFH->second.end() ) {
			free( (*iterPFC).obsDensity );
			iterPFC++;
		}

		this->PFHeldCorrections.erase( iPFH );
	}
			
	#ifdef TRACE
	System::Diagnostics::Trace::WriteLine( "--- ~AgentHost(): 4 ---" );
	#endif

	this->_ddbClearWatchers();
			
	#ifdef TRACE
	System::Diagnostics::Trace::WriteLine( "--- ~AgentHost(): 5 ---" );
	#endif

	// free state
	delete STATE(AgentHost)->hostStats;
//	delete STATE(AgentHost)->activeAgents;
	delete STATE(AgentHost)->agentTemplateInstances;

	#ifdef TRACE
	System::Diagnostics::Trace::WriteLine( "--- ~AgentHost(): 6 ---" );
	#endif
}


//-----------------------------------------------------------------------------
// Configure

int AgentHost::configure( char *configPath ) {
	FILE *configF = NULL;
	int iResult;
	char buf[1024], *ptr;
	unsigned char val;
	int count = 1;
	int i;

	// set configPath
	if ( configPath == NULL ) {
		sprintf_s( this->configPath, sizeof(this->configPath), "host.cfg" );
	} else {
		sprintf_s( this->configPath, sizeof(this->configPath), "%s", configPath );
	}
	apb->apbString( this->configPath, sizeof(this->configPath) );

	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		char logName[256];
		char timeBuf[64];
		char configStrip[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		strcpy_s( configStrip, sizeof(configStrip), this->configPath );
		char *c = configStrip;
		while ( *c != 0 ) {
			if ( *c == '\\' || *c == '/' ) *c = '_';
			c++;
		}
		sprintf_s( logName, "%s\\AgentHost %s %s.txt", logDirectory, configStrip, timeBuf );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_ALL ); 



		Log.log( 0, "AgentHost %.2d.%.2d.%.2d", AUTONOMIC_MAJOR_VERSION, AUTONOMIC_MINOR_VERSION, AUTONOMIC_SUB_VERSION );
	}

	if ( AgentBase::configure() )
		return 1;
//#ifdef	NO_LOGGING
//	Log.setLogMode(LOG_MODE_OFF);
//	Log.setLogLevel(LOG_LEVEL_NONE);
//#endif
	STATE(AgentBase)->configured = false;
	
	// look for config  file
	if ( _access( this->configPath, 00 ) == -1 ) { // do initial setup routine
		char buf10[10][256];
		char buf10B[10][256];

		printf( "First run, please configure this host.\n\n" );

		//-------------------------------------
		// get server address

		char hostname[256];
		int res = gethostname(hostname, sizeof(hostname));
		if (res != 0) {
		  printf("Error getting hostname: %u\n", apb->apbWSAGetLastError());
		} else {
			hostent* pHostent = gethostbyname(hostname);
			if (pHostent==NULL) {
				printf("Error getting hostent: %u\n", apb->apbWSAGetLastError());
			} else {
				hostent& he = *pHostent;
				sprintf_s( buf10[count], sizeof(buf10[0]), "%s", he.h_name );
				count++;

				sockaddr_in sa;
				for (int nAdapter=0; he.h_addr_list[nAdapter]; nAdapter++) {
					memcpy ( &sa.sin_addr.s_addr, he.h_addr_list[nAdapter],he.h_length);
					sprintf_s( buf10[count], sizeof(buf10[0]), "%s", inet_ntoa(sa.sin_addr) );
					count++;
					if ( count >= sizeof(buf10)/sizeof(buf10[0]) )
						break;
				}
			}
		}

		printf( "Detecting Addresses:\n" );
		printf( "  [0] Custom Address\n" );
		for ( i=1; i<count; i++ ) {
			printf( "  [%d] %s\n", i, buf10[i] );
		}
		
		do {
			printf( "Select an address for the server: " );
			*buf = getchar();
			while( getchar() != '\n' ); // clear until return
		} while ( buf[0] - '0' < 0 || buf[0] - '0' >= count );

		count = buf[0] - '0'; // use count to store the selected address

		if ( buf[0] == '0' ) {
			printf( "Enter custom address (e.g. 192.168.0.10, -1 for local only): " );
			gets_s( STATE(AgentHost)->serverAP.address, sizeof(STATE(AgentHost)->serverAP.address) );
		} else {
			sprintf_s( STATE(AgentHost)->serverAP.address, sizeof(STATE(AgentHost)->serverAP.address), "%s", buf10[count] );
		}
		
		printf( "Server address set to: %s\n\n", STATE(AgentHost)->serverAP.address );

		//-------------------------------------
		// get server port
		printf( "Enter the server port number (leave blank for default = %s): ", DEFAULT_PORT );
		gets_s( STATE(AgentHost)->serverAP.port, sizeof(STATE(AgentHost)->serverAP.port) );

		if ( STATE(AgentHost)->serverAP.port[0] == '\0' ) {
			sprintf_s( STATE(AgentHost)->serverAP.port, sizeof(STATE(AgentHost)->serverAP.port), "%s", DEFAULT_PORT );
		}

		printf( "Server port set to: %s\n\n", STATE(AgentHost)->serverAP.port );
		
		//-------------------------------------
		// get local port
		
		sprintf_s( this->localAP.address, sizeof(localAP.address), "127.0.0.1" );
		
		printf( "Enter the local port number (leave blank for default = %s): ", DEFAULT_PORT );
		gets_s( this->localAP.port, sizeof(this->localAP.port) );

		if ( this->localAP.port[0] == '\0' ) {
			sprintf_s( this->localAP.port, sizeof(this->localAP.port), "%s", DEFAULT_PORT );
		}

		printf( "Local port set to: %s\n\n", this->localAP.port );
		

		//-------------------------------------
		// get cluster supervisors
		printf( "Input Core Hosts:\n" );
		count = 0;

		do {
			printf( "Current Core Hosts:\n" );
			if ( count == 0 ) {
				printf( "  None\n" );
			} else {
				for ( i=0; i<count; i++ ) {
					printf( "  [%d] %s:%s\n", i, buf10[i], buf10B[i] );
				}
			}
			printf( "Enter 'n' to add core host, 'f' to finish, or a number to delete entry: " );

			*buf = getchar();
			while( getchar() != '\n' ); // clear until return

			if ( *buf == 'n' ) {
				if ( count < 10 ) {
					printf( "Enter an address (e.g. aer-dev8.aerospace.utoronto.ca): " );
					gets_s( buf10[count], sizeof(buf10[count]) );
					printf( "Enter a port (leave blank for default = %s): ", DEFAULT_PORT );
					gets_s( buf10B[count], sizeof(buf10B[count]) );

					if ( buf10B[count][0] == '\0' ) {
						sprintf_s( buf10B[count], sizeof(buf10B[count]), "%s", DEFAULT_PORT );
					}
					count++;
				} else {
					printf( "Too many core hosts, edit the config manually to add more\n" );
				}
			} else if ( *buf - '0' >= 0 && *buf - '0' < count ) {
				printf( "Deleting entry %c.\n", *buf );
				for ( i=*buf-'0'; i<count-1; i++ ) {
					memcpy( &buf10[i], &buf10[i+1], sizeof(buf10[0]) );
					memcpy( &buf10B[i], &buf10B[i+1], sizeof(buf10B[0]) );
				}
				count--;
			} else if ( *buf != 'f' ) {
				printf( "Invalid input.\n" );
			}
		} while ( *buf != 'f' );

		this->gmCoreHosts = NULL;
		spAddressPort ap = NULL;
		spAddressPort lastAP = NULL;
		for ( i=0; i<count; i++ ) {
			ap = (spAddressPort)malloc(sizeof(sAddressPort));
			if ( !ap ) {
				Log.log( 0, "AgentHost::configure: Failed to malloc AddressPort" );
				while ( this->gmCoreHosts ) { ap = this->gmCoreHosts->next; free( this->gmCoreHosts ); this->gmCoreHosts = ap; }
				return 1;
			}
			sprintf_s( ap->address, sizeof(ap->address), "%s", buf10[i] );
			sprintf_s( ap->port, sizeof(ap->port), "%s", buf10B[i] );
			if ( i == 0 ) {
				this->gmCoreHosts = ap;
			} else {
				lastAP->next = ap;
			}		
			ap->next = NULL;
			lastAP = ap;
		}

		//-------------------------------------
		// get identification key
		while (1) {
			printf( "Enter group identification key: " );
			gets_s( this->clusterIDStr, sizeof(this->clusterIDStr) );			
			if ( strlen( this->clusterIDStr ) != 32 ) {
				printf( "Bad key: key requires 32 hex characters.\n" );
				continue;
			}
			this->clusterID[0] = 0;
			ptr = this->clusterIDStr;
			for ( i=7; i>=0; i-- ) {
				if ( *ptr >= '0' && *ptr <= '9' ) {
					val = (unsigned char)(*ptr - '0') << 4;
				} else if ( *ptr >= 'A' && *ptr <= 'F' ) {
					val = ( (unsigned char)(*ptr - 'A') + 10 ) << 4;
				} else if ( *ptr >= 'a' && *ptr <= 'f' ) {
					val = ( (unsigned char)(*ptr - 'a') + 10 ) << 4;
				} else {
					break;
				}
				ptr++;
				if ( *ptr >= '0' && *ptr <= '9' ) {
					val += (unsigned char)(*ptr - '0');
				} else if ( *ptr >= 'A' && *ptr <= 'F' ) {
					val += (unsigned char)(*ptr - 'A') + 10;
				} else if ( *ptr >= 'a' && *ptr <= 'f' ) {
					val += (unsigned char)(*ptr - 'a') + 10;
				} else {
					break;
				}
				ptr++;
				memcpy( ((char*)(&this->clusterID[0]))+i, &val, sizeof(char) );
			}
			if ( i != -1 ) {
				printf( "Bad key: use only valid hex characters.\n" );
				continue;
			}
			this->clusterID[1] = 0;
			ptr = &this->clusterIDStr[16];
			for ( i=7; i>=0; i-- ) {
				if ( *ptr >= '0' && *ptr <= '9' ) {
					val = (unsigned char)(*ptr - '0') << 4;
				} else if ( *ptr >= 'A' && *ptr <= 'F' ) {
					val = ( (unsigned char)(*ptr - 'A') + 10 ) << 4;
				} else if ( *ptr >= 'a' && *ptr <= 'f' ) {
					val = ( (unsigned char)(*ptr - 'a') + 10 ) << 4;
				} else {
					break;
				}
				ptr++;
				if ( *ptr >= '0' && *ptr <= '9' ) {
					val += (unsigned char)(*ptr - '0');
				} else if ( *ptr >= 'A' && *ptr <= 'F' ) {
					val += (unsigned char)(*ptr - 'A') + 10;
				} else if ( *ptr >= 'a' && *ptr <= 'f' ) {
					val += (unsigned char)(*ptr - 'a') + 10;
				} else {
					break;
				}
				ptr++;
				memcpy( (char*)(&this->clusterID[1])+i, &val, sizeof(char) );
			}
			if ( i != -1 ) {
				printf( "Bad key: use only valid hex characters.\n" );
				continue;
			}
			break;
		}

		//-------------------------------------
		// get process capacity
		
		this->processCores = 1;
		this->processCapacity = 1;

		do {
			printf( "Enter the number of cores for this host: " );
			gets_s( buf, sizeof(buf) );

			int d;
			if ( 1 == sscanf_s( buf, "%d", &d ) ) {
				this->processCores = d;
				break;
			} else {
				printf( "Invalid entry!\n" );
			}
		} while ( 1 );

		printf( "Process cores set to: %d\n\n", this->processCores );

		do {
			printf( "Enter the process capacity for each core: " );
			gets_s( buf, sizeof(buf) );

			float f;
			if ( 1 == sscanf_s( buf, "%f", &f ) ) {
				this->processCapacity = f;
				break;
			} else {
				printf( "Invalid entry!\n" );
			}
		} while ( 1 );

		printf( "Process capacity set to: %f\n\n", this->processCapacity );

		// default timing and stability
		this->timecardStart = 0;
		this->timecardEnd = -1;

		STATE(AgentBase)->stabilityTimeMin = 999999999.0f;
		STATE(AgentBase)->stabilityTimeMax = 9999999999.0f;


		if ( this->saveConfig() ) {
			return 1;			
		}

	} else { // parse config file

		iResult = fopen_s( &configF, this->configPath, "r" );
		if ( iResult ) {
			Log.log( 0, "AgentHost::configure: Error opening config file %s for reading", this->configPath );
			return 1;
		}

		//-------------------------------------
		// read server address
		fscanf_s( configF, "[Server Address]\n" );
		if ( 1 != fscanf_s( configF, "address=%s\n", STATE(AgentHost)->serverAP.address, sizeof(STATE(AgentHost)->serverAP.address) ) ) {
			Log.log( 0, "AgentHost::configure: Error reading server address" );
			return 1;
		}
		if ( 1 != fscanf_s( configF, "port=%s\n", STATE(AgentHost)->serverAP.port, sizeof(STATE(AgentHost)->serverAP.port) ) ) {
			Log.log( 0, "AgentHost::configure: Error reading server port" );
			return 1;
		}
		sprintf_s( this->localAP.address, sizeof(localAP.address), "127.0.0.1" );
		if ( 1 != fscanf_s( configF, "local_port=%s\n", this->localAP.port, sizeof(this->localAP.port) ) ) {
			Log.log( 0, "AgentHost::configure: Error reading local port" );
			return 1;
		}
	
		
		//-------------------------------------
		// read culster supervisor addresses
		count = 0;
		fscanf_s( configF, "[Core Hosts]\n" );
		spAddressPort ap = NULL;
		spAddressPort lastAP = NULL;
		while (1) {
			iResult = fscanf_s( configF, "s_address%d=%s\n", &i, buf, sizeof(buf) );
			if ( !iResult || iResult == EOF  ) {
				break; // no more supervisors
			}
			if ( 2 != iResult ) {
				Log.log( 0, "AgentHost::configure: Error reading core host address, %d", count );
				return 1;
			}
			if ( count != i ) {
				Log.log( 0, "AgentHost::configure: Out of order core host address, expecting %d, got %d", count, i );
				return 1;
			}
			ap = (spAddressPort)malloc(sizeof(sAddressPort));
			if ( !ap ) {
				Log.log( 0, "AgentHost::configure: Failed to malloc AddressPort\n" );
				return 1;
			}
			sprintf_s( ap->address, sizeof(ap->address), "%s", buf );
			if ( 2 != fscanf_s( configF, "s_port%d=%s\n", &i, ap->port, sizeof(ap->port) ) ) {
				Log.log( 0, "AgentHost::configure: Error reading core host port, %d\n", count );
				return 1;
			}
			if ( count != i ) {
				Log.log( 0, "AgentHost::configure: Out of order core host port, expecting %d, got %d", count, i );
				return 1;
			}
			if ( count == 0 ) {
				this->gmCoreHosts = ap;
			} else { 
				lastAP->next = ap;
			}		
			ap->next = NULL;
			lastAP = ap;
			count++;
		}

		//-------------------------------------
		// read cluster identification key
		fscanf_s( configF, "[Group Key]\n" );
		if ( 1 != fscanf_s( configF, "key=%s\n", this->clusterIDStr, sizeof(this->clusterIDStr) ) ) {
			Log.log( 0, "AgentHost::configure: Error reading cluster identification key" );
			return 1;
		}
		
		this->clusterID[0] = 0;
		ptr = this->clusterIDStr;
		for ( i=7; i>=0; i-- ) {
			if ( *ptr >= '0' && *ptr <= '9' ) {
				val = (unsigned char)(*ptr - '0') << 4;
			} else if ( *ptr >= 'A' && *ptr <= 'F' ) {
				val = ( (unsigned char)(*ptr - 'A') + 10 ) << 4;
			} else if ( *ptr >= 'a' && *ptr <= 'f' ) {
				val = ( (unsigned char)(*ptr - 'a') + 10 ) << 4;
			} else {
				break;
			}
			ptr++;
			if ( *ptr >= '0' && *ptr <= '9' ) {
				val += (unsigned char)(*ptr - '0');
			} else if ( *ptr >= 'A' && *ptr <= 'F' ) {
				val += (unsigned char)(*ptr - 'A') + 10;
			} else if ( *ptr >= 'a' && *ptr <= 'f' ) {
				val += (unsigned char)(*ptr - 'a') + 10;
			} else {
				break;
			}
			ptr++;
			memcpy( ((char*)(&this->clusterID[0]))+i, &val, sizeof(char) );
		}
		if ( i != -1 ) {
			Log.log( 0, "AgentHost::configure: Bad cluster identification key" );
			return 1;
		}
		this->clusterID[1] = 0;
		ptr = &this->clusterIDStr[16];
		for ( i=7; i>=0; i-- ) {
			if ( *ptr >= '0' && *ptr <= '9' ) {
				val = (unsigned char)(*ptr - '0') << 4;
			} else if ( *ptr >= 'A' && *ptr <= 'F' ) {
				val = ( (unsigned char)(*ptr - 'A') + 10 ) << 4;
			} else if ( *ptr >= 'a' && *ptr <= 'f' ) {
				val = ( (unsigned char)(*ptr - 'a') + 10 ) << 4;
			} else {
				break;
			}
			ptr++;
			if ( *ptr >= '0' && *ptr <= '9' ) {
				val += (unsigned char)(*ptr - '0');
			} else if ( *ptr >= 'A' && *ptr <= 'F' ) {
				val += (unsigned char)(*ptr - 'A') + 10;
			} else if ( *ptr >= 'a' && *ptr <= 'f' ) {
				val += (unsigned char)(*ptr - 'a') + 10;
			} else {
				break;
			}
			ptr++;
			memcpy( (char*)(&this->clusterID[1])+i, &val, sizeof(char) );
		}
		if ( i != -1 ) {
			Log.log( 0, "AgentHost::configure: Bad cluster identification key" );
			return 1;
		}

		//-------------------------------------
		// read process capacity
		fscanf_s( configF, "[Process Capacity]\n" );
		if ( 1 != fscanf_s( configF, "process_cores=%d\n", &this->processCores ) ) {
			Log.log( 0, "AgentHost::configure: Error reading process cores" );
			return 1;
		}
		if ( 1 != fscanf_s( configF, "process_capacity=%f\n", &this->processCapacity ) ) {
			Log.log( 0, "AgentHost::configure: Error reading process capacity" );
			return 1;
		}

		//-------------------------------------
		// read timing and stability
		fscanf_s( configF, "[Timing and Stability]\n" );
		if ( 2 != fscanf_s( configF, "timecard=%f %f\n", &this->timecardStart, &this->timecardEnd ) ) {
			Log.log( 0, "AgentHost::configure: Error reading timecard" );
			return 1;
		}
		if ( 2 != fscanf_s( configF, "timeminmax=%f %f\n", &STATE(AgentBase)->stabilityTimeMin, &STATE(AgentBase)->stabilityTimeMax ) ) {
			Log.log( 0, "AgentHost::configure: Error reading timeminmax" );
			return 1;
		}

		//-------------------------------------
		// read hardware resources
		count = 0;
		fscanf_s( configF, "[Hardware Resources]\n" );
		WCHAR idBuf[64];
		UUID hid;
		while (1) {
			iResult = fscanf_s( configF, "h_resource%d=%ws\n", &i, idBuf, 64 );
			if ( !iResult || iResult == EOF  ) {
				break; // no more resources
			}
			if ( 2 != iResult ) {
				Log.log( 0, "AgentHost::configure: Error reading hardware resource, %d", count );
				return 1;
			}
			if ( count != i ) {
				Log.log( 0, "AgentHost::configure: Out of order hardware resource, expecting %d, got %d", count, i );
				return 1;
			}
			UuidFromString( (RPC_WSTR)idBuf, &hid );
			this->hardwareResources.push_back( hid );
			count++;
		}
		
		fclose( configF );
	}

	this->loadLibrary(); // load the agent library

	STATE(AgentBase)->configured = true;

	// handle timecard
	if ( this->timecardEnd != -1 ) {
		Log.log( 0, "AgentHost::configure: scheduling retirement in %d ms", (int)(this->timecardEnd*1000*60) );
		this->addTimeout( (int)(this->timecardEnd*1000*60), AgentHost_CBR_cbRetire );
	}

	// handle stability
	UUID noCrashId;
	UuidFromString( (RPC_WSTR)_T(HWRESOURCE_NO_CRASH), &noCrashId );
	list<UUID>::iterator lRes;
	for ( lRes = this->hardwareResources.begin(); lRes != this->hardwareResources.end(); lRes++ ) {
		if ( *lRes == noCrashId ) {
			STATE(AgentBase)->noCrash = true;
			break;
		}
	}

	// see if we have the data gathering resource
	UUID gatherDataId;
	UuidFromString( (RPC_WSTR)_T(HWRESOURCE_GATHER_DATA), &gatherDataId );
	for ( lRes = this->hardwareResources.begin(); lRes != this->hardwareResources.end(); lRes++ ) {
		if ( *lRes == gatherDataId ) {
			this->gatherData = true;
			break;
		}
	}
	if ( this->gatherData ) {
		char logName[256];
		char timeBuf[64];
		char configStrip[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		strcpy_s( configStrip, sizeof(configStrip), this->configPath );
		char *c = configStrip;
		while ( *c != 0 ) {
			if ( *c == '\\' || *c == '/' ) *c = '_';
			c++;
		}
		sprintf_s( logName, "%s\\xDATA AgentHost %s %s.txt", logDirectory, configStrip, timeBuf );

		Data.setLogMode( LOG_MODE_FILE, logName );
		Data.setLogLevel( LOG_LEVEL_ALL );
		this->addTimeout( DATA_GATHER_PERIOD, AgentHost_CBR_cbDataDump ); // schedule data dumps
	}


	return 0;
}

int AgentHost::saveConfig() {
	FILE *configF = NULL;
	spAddressPort ap;
	int i;
	list<UUID>::iterator iRes;
	WCHAR resStr[64];
	
	if ( fopen_s( &configF, this->configPath, "w" ) ) {
		Log.log( 0, "Failed to open config file %s for writting", this->configPath );
		return 1;
	}

	fprintf( configF, "[Server Address]\n" );
	if ( !strcmp( STATE(AgentHost)->serverAP.address, "127.0.0.1" ) ) 
		fprintf( configF, "address=-1\n" );
	else
		fprintf( configF, "address=%s\n", STATE(AgentHost)->serverAP.address );
	fprintf( configF, "port=%s\n", STATE(AgentHost)->serverAP.port );
	fprintf( configF, "local_port=%s\n", this->localAP.port );
	fprintf( configF, "[Core Hosts]\n" );
		
	ap = this->gmCoreHosts;
	i = 0;
	while ( ap ) {
		fprintf( configF, "s_address%d=%s\n", i, ap->address );
		fprintf( configF, "s_port%d=%s\n", i, ap->port );
		ap = ap->next;
		i++;
	}

	fprintf( configF, "[Group Key]\n" );
	fprintf( configF, "key=%s\n", this->clusterIDStr );
	
	fprintf( configF, "[Process Capacity]\n" );
	fprintf( configF, "process_cores=%d\n", this->processCores );
	fprintf( configF, "process_capacity=%f\n", this->processCapacity );

	fprintf( configF, "[Timing and Stability]\n" );
	fprintf( configF, "timecard=%f %f\n", this->timecardStart, this->timecardEnd );
	fprintf( configF, "timeminmax=%f %f\n", STATE(AgentBase)->stabilityTimeMin, STATE(AgentBase)->stabilityTimeMax );
	
	fprintf( configF, "[Hardware Resources]\n" );
	for ( i = 0, iRes = this->hardwareResources.begin(); iRes != this->hardwareResources.end(); iRes++, i++ ) {
		UuidToString( &*iRes, (RPC_WSTR *)resStr );
		fprintf( configF, "h_resource%d=%ws\n", i, resStr );
	}

	fclose( configF );
			
	return 0;
}

int AgentHost::keyboardInput( char ch ) {
	if ( apb->getPlaybackMode() != PLAYBACKMODE_PLAYBACK ) {
		int l = (int)strlen( this->keyboardBuf );

		if ( l >= sizeof(this->keyboardBuf)-1 )
			return 1; // buffer full

		// insert char
		this->keyboardBuf[l] = ch;
		this->keyboardBuf[l+1] = '\0';
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Start

int AgentHost::start( char *missionFile, char *queueMission ) {

	// handle timecard
	if ( this->timecardStart > 0 ) {
		Log.log( 0, "AgentHost::start: Delayed start, sleeping %d ms", (int)(this->timecardStart*1000*60) );
		float minPerSec = 1/60.0f;
		while ( this->timecardStart > minPerSec ) {
			apb->apbSleep( 1000 );
			this->timecardStart -= minPerSec;
			if ( STATE(AgentBase)->stopFlag )
				return 0; // time to stop
		}
		apb->apbSleep( (int)(this->timecardStart*1000*60) );
		if ( STATE(AgentBase)->stopFlag )
			return 0; // time to stop
	}

	if ( AgentBase::start( missionFile ) ) 
		return 1;

	STATE(AgentBase)->started = false;

	UUID id = this->addTimeout( CLEAN_EXIT_INTERVAL, AgentHost_CBR_cbCleanExitCheck );
	if ( id == nilUUID ) {
		Log.log( 0, "AgentHost::start: addTimeout failed" );
		return 1;
	}
	
	// start listening for connections
	if ( strncmp( "-1", STATE(AgentHost)->serverAP.address, 2 ) ) { // -1 means local only
		this->serverCon = this->openListener( &STATE(AgentHost)->serverAP );
		if ( this->serverCon == NULL ) {
			return 1;
		}
	} else {
		STATE(AgentHost)->serverAP = this->localAP; // copy local AP
	}
	
	// start local listener
	this->localCon = this->openListener( &this->localAP );
	if ( this->localCon == NULL ) {
		return 1;
	}

	/*
	// attempt connections to cluster supervisors
	spAddressPort ap = this->supervisorAP;
	spConnection con;
	while ( ap ) { 
		con = this->openConnection( ap, NULL, 60 );
		if ( con ) {
			this->watchConnection( con, AgentHost_CBR_cbSupervisorWatcher );
		}
		ap = ap->next; 
	}
*/
	STATE(AgentBase)->started = true;

	Log.log( 0, "debug release: A" );

	// join host group
	this->hostGroupJoin();

	Log.log( 0, "debug release: B" );

	// handle queue mission
	char queueBuf[MAX_PATH];
	if ( queueMission ) {
		strcpy_s( queueBuf, sizeof(queueBuf), queueMission );
	} else {
		queueBuf[0] = 0;
	}
	apb->apbString( queueBuf, sizeof(queueBuf) );

	if ( strlen(queueBuf) > 0 ) {
		this->queueMission( queueBuf );
	}

	return 0;
}


//-----------------------------------------------------------------------------
// Stop


int AgentHost::stop() {
	// notify hosts that we're shutting down
	mapAgentHostState::iterator iterHS;
/*	for ( iterHS = this->hostKnown.begin(); iterHS != this->hostKnown.end(); iterHS++ ) {
		if ( iterHS->second->connection != NULL )
			this->sendMessageEx( iterHS->second->connection, MSGEX(AgentHost_MSGS,MSG_HOST_SHUTDOWN) );
	}

	// kill all agents
	mapAgentInfo::iterator iA;
	mapAgentInfo::iterator nextA;
	for ( iA = this->agentInfo.begin(); iA != this->agentInfo.end(); iA = nextA ) {
		nextA = iA;
		nextA++;
		if ( *dStore->AgentGetHost((UUID *)&iA->first) == *this->getUUID() ) {
			this->killAgent( (UUID *)&iA->first );
		}
	}


	STATE(AgentHost)->hostStats->clear();
*/

	// clean up hostKnown
	for ( iterHS = this->hostKnown.begin(); iterHS != this->hostKnown.end(); iterHS++ ) {
		AgentHost_DeleteState( (AgentBase::State *)iterHS->second );
	}
	this->hostKnown.clear();

	// shutdown all connections
	mapConnection::iterator iterCon = this->connection.begin();
	while ( this->connection.size() ) {
		iterCon = this->connection.begin();

		if ( iterCon->second->state == CON_STATE_LISTENING )
			this->closeListener( iterCon->second );
		else {
			this->closeConnection( iterCon->second );
			this->conDelete( iterCon->second );
		}
	}

	// clean up agentLocalMessageQueue
	DDBAgent_MSG msg;
	map<UUID,list<DDBAgent_MSG>,UUIDless>::iterator iQ;
	for ( iQ = this->agentLocalMessageQueue.begin(); iQ != this->agentLocalMessageQueue.end(); iQ++ ) {
		while ( iQ->second.size() ) {
			msg = iQ->second.front();
			if ( msg.len )
				free( msg.data );
			iQ->second.pop_front();
		}
	}
	this->agentLocalMessageQueue.clear();

	this->DumpStatistics();

	// TEMP as a fudge use the data gatherer resource to flag the host that needs to report mission done to remoteStart
	if ( this->missionDone ) {
		/*UUID gatherDataId;
		UuidFromString( (RPC_WSTR)_T(HWRESOURCE_GATHER_DATA), &gatherDataId );
		list<UUID>::iterator lRes;
		for ( lRes = this->hardwareResources.begin(); lRes != this->hardwareResources.end(); lRes++ ) {
			if ( *lRes == gatherDataId ) {
				WCHAR appBuf[MAX_PATH];
				WCHAR argBuf[MAX_PATH];

				swprintf_s( appBuf, MAX_PATH, L"remoteStartD.exe" );
				swprintf_s( argBuf, MAX_PATH, L"-mission_done" );

				HINSTANCE hi = ShellExecute( NULL, NULL, appBuf, argBuf, NULL, SW_SHOW );

				Log.log( 0, "AgentHost::stop: reporting mission_done to remoteStart" );
				break;
			}
		}*/
		if ( this->gatherData ) {
			WCHAR appBuf[MAX_PATH];
			WCHAR argBuf[MAX_PATH];

			swprintf_s( appBuf, MAX_PATH, L"remoteStartD.exe" );
			swprintf_s( argBuf, MAX_PATH, L"-mission_done" );

			HINSTANCE hi = ShellExecute( NULL, NULL, appBuf, argBuf, NULL, SW_SHOW );

			Log.log( 0, "AgentHost::stop: reporting mission_done to remoteStart" );
		}
	}

	return AgentBase::stop();
}

//-----------------------------------------------------------------------------
// Graceful Exit

int AgentHost::gracefulExit() {
	DataStream lds;
	UUID aId;
	int status;

	std::list<UUID>::iterator iE;
	mapAgentInfo::iterator iAI;
	AgentInfo *ai;

	// outline:
	// - stop bidding for agents
	// - stop expecting ownership and clean up shells
	// - handle current agents (e.g. abort any spawning agents, freeze active agents, etc.)
	// - when all agents are frozen:
	//		- notify of our leaving
	//		- stop

	STATE(AgentBase)->gracefulExit = true;
	gracefulExitLock.key = nilUUID;
	gracefulExitLock.tumbler.clear();

	Log.log( 0, "AgentHost::gracefulExit: starting graceful exit" );

	for ( iAI = this->agentInfo.begin(); iAI != this->agentInfo.end(); iAI++ ) {
		if ( iAI->second.activationMode == AM_UNSET || iAI->second.activationMode == AM_EXTERNAL )
			continue; // skip

		aId = iAI->first;
		ai = &iAI->second;

		// check shell status
		if ( ai->shellStatus == DDBAGENT_STATUS_ERROR ) { // no shell
			// nothing to do
		} else if ( ai->shellStatus == DDBAGENT_STATUS_SPAWNING ) { // shell was spawning
			ai->shellStatus = DDBAGENT_STATUS_ABORT; // abort agent when it reports in
		} else if ( ai->shellStatus == DDBAGENT_STATUS_READY ) { // shell was active, kill shell
			this->sendMessage( ai->shellCon, MSG_AGENT_STOP );

			this->stopWatchingConnection( ai->shellCon, ai->shellWatcher );
			this->closeConnection( ai->shellCon );

			ai->shellStatus = DDBAGENT_STATUS_ERROR;
			ai->shellCon = NULL;
			ai->shellWatcher = 0;
		} else {
			Log.log( 0, "AgentHost::gracefulExit: unexpected shell status %d (%s)", ai->shellStatus, Log.formatUUID( 0, &aId ) );
		}

		if ( *this->getUUID() == *dStore->AgentGetHost( &aId ) && iAI->second.expectingStatus.empty() ) { // one of our agents, and we haven't recently submitted a status change
			status = dStore->AgentGetStatus( &aId );
			
			if ( status == DDBAGENT_STATUS_SPAWNING ) { // release ownership (aborting spawn is handled in recvAgentSpawned)
				this->AgentSpawnAbort( &aId );
				this->gracefulExitLock.tumbler.push_back( aId ); // waiting for freeze to finish
				Log.log( 0, "AgentHost::gracefulExit: agent %s, aborting spawn", Log.formatUUID(0,&aId) );
			
			} else if ( status == DDBAGENT_STATUS_READY ) {
				if ( this->agentLibrary[ai->type.uuid]->transferPenalty == 1 ) { // non-transferable, fail agent
					// update status
					lds.reset();
					lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
					lds.packUUID( &nilUUID ); // release ownership
					lds.packUUID( this->getUUID() );
					lds.packInt32( DDBAGENT_STATUS_FAILED );
					lds.packUUID( this->getUUID() );
					lds.rewind();
					this->ddbAgentSetInfo( &aId, &lds );
					lds.unlock();

					// stop agent
					this->sendMessage( iAI->second.con, MSG_AGENT_STOP );
					
					Log.log( 0, "AgentHost::gracefulExit: agent %s, failing non-transferable agent", Log.formatUUID(0,&aId) );
				} else {  // begin freezing
					this->AgentTransferStartFreeze( &aId );
					this->gracefulExitLock.tumbler.push_back( aId ); // waiting for freeze to finish
					Log.log( 0, "AgentHost::gracefulExit: agent %s, freezing", Log.formatUUID(0,&aId) );
				}
			
			} else if ( status == DDBAGENT_STATUS_FREEZING ) { // continue freezing 
				this->gracefulExitLock.tumbler.push_back( aId ); // waiting for freeze to finish
				Log.log( 0, "AgentHost::gracefulExit: agent %s, continuing freeze", Log.formatUUID(0,&aId) );
			
			} else if ( status == DDBAGENT_STATUS_THAWING ) { // abort thaw
				this->AgentTransferAbortThaw( &aId );
				this->gracefulExitLock.tumbler.push_back( aId ); // waiting for re-freeze to finish
				Log.log( 0, "AgentHost::gracefulExit: agent %s, re-freezing", Log.formatUUID(0,&aId) );
			
			} else if ( status == DDBAGENT_STATUS_RECOVERING ) {
				this->AgentRecoveryAbort( &aId );
				this->gracefulExitLock.tumbler.push_back( aId ); // waiting for recovery abort to finish
				Log.log( 0, "AgentHost::gracefulExit: agent %s, aborting recovery", Log.formatUUID(0,&aId) );

			} else { // unexpected status
				Log.log( 0, "AgentHost::gracefulExit: unexpected agent status %d, %s", status, Log.formatUUID(0,&aId) );
			}
		} else if ( !iAI->second.expectingStatus.empty() ) { // we are expecting a status change, we must resolve the exit after the change
			this->gracefulExitLock.tumbler.push_back( aId ); // add this to our lock
			Log.log( 0, "AgentHost::gracefulExit: agent %s, expecting status change", Log.formatUUID(0,&aId) );
			
		}
	}

	this->gracefulExitUpdate(); // check if we're done already

	return 0;
}

int AgentHost::gracefulExitUpdate( UUID *agent ) {

	if ( agent ) {
		UUIDLock_Throw( &this->gracefulExitLock, agent, &nilUUID );
	}

	if ( this->gracefulExitLock.tumbler.empty() ) { // nothing left to wait for
		this->hostGroupLeave();

		Log.log( 0, "AgentHost::gracefulExitUpdate: ready to leave group" );
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Mission start configuration

int AgentHost::parseMF_HandleAgent( AgentType *agentType ) {

	if ( *this->getUUID() == this->gmMemberList.front() ) { // leader

		Log.log( 0, "AgentHost::parseMF_HandleAgent: requesting agent %s-%d", Log.formatUUID(0,&agentType->uuid), agentType->instance );

		UUID thread = this->conversationInitiate( AgentHost_CBR_convRequestUniqueSpawn, REQUESTAGENTSPAWN_TIMEOUT, agentType, sizeof(AgentType) );
		if ( thread == nilUUID ) {
			return 1;
		}
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packUUID( &agentType->uuid );
		this->ds.packChar( agentType->instance );
		this->ds.packFloat32( 0 ); // affinity
		this->ds.packChar( DDBAGENT_PRIORITY_CRITICAL );
		this->ds.packUUID( &thread );
		this->conProcessMessage( NULL, MSG_RAGENT_SPAWN, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

	} else { // not leader
		mapAgentInfo::iterator iA;
		for ( iA = this->agentInfo.begin(); iA != this->agentInfo.end(); iA++ ) {
			if ( iA->second.type.uuid == agentType->uuid && iA->second.type.instance == agentType->instance ) {
				break; // already got one
			}
		}

		if ( iA == this->agentInfo.end() ) {
			this->uniqueNeeded.push_back( *agentType ); // save for later
		}
	}

	return 0;
}

int AgentHost::parseMF_HandleAvatar( AgentType *agentType, char *fileName, float x, float y, float r, float startTime, float duration, char retireMode ) {
	
	if ( startTime == 0 ) { // start right away
		if ( *this->getUUID() == this->gmMemberList.front() ) { // leader

			Log.log( 0, "AgentHost::parseMF_HandleAvatar: requesting agent %s-%d", Log.formatUUID(0,&agentType->uuid), agentType->instance );

			UUID thread = this->conversationInitiate( AgentHost_CBR_convRequestUniqueSpawn, REQUESTAGENTSPAWN_TIMEOUT, agentType, sizeof(AgentType) );
			if ( thread == nilUUID ) {
				return 1;
			}
			this->ds.reset();
			this->ds.packUUID( this->getUUID() );
			this->ds.packUUID( &agentType->uuid );
			this->ds.packChar( agentType->instance );
			this->ds.packFloat32( 0 ); // affinity
			this->ds.packChar( DDBAGENT_PRIORITY_CRITICAL );
			this->ds.packUUID( &thread );
			this->conProcessMessage( NULL, MSG_RAGENT_SPAWN, this->ds.stream(), this->ds.length() );
			this->ds.unlock();

		} else { // not leader
			mapAgentInfo::iterator iA;
			for ( iA = this->agentInfo.begin(); iA != this->agentInfo.end(); iA++ ) {
				if ( iA->second.type.uuid == agentType->uuid && iA->second.type.instance == agentType->instance ) {
					break; // already got one
				}
			}

			if ( iA == this->agentInfo.end() ) {
				this->uniqueNeeded.push_back( *agentType ); // save for later
			}
		}
	} else { // delay start
		this->addTimeout( (int)(startTime*60*1000), AgentHost_CBR_cbDelayedAgentSpawn, agentType, sizeof(AgentType) );
	}
	Log.log(0, "AgentHost::parseMF_HandleAvatar: completed");

	return 0;
}

int AgentHost::parseMF_HandleOfflineSLAM( int SLAMmode, int particleNum, float readingProcessingRate, int processingSlots, char *logPath ) {
	this->offlineSLAMmode = SLAMmode;

	return 0;
}

int AgentHost::queueMission( char *misFile ) {
	int delay = 10000;
	
	Log.log( LOG_LEVEL_VERBOSE, "AgentHost::queueMission: queuing mission %s to start in %.3f seconds", misFile, delay/1000.0f );
	
	this->addTimeout( 10000, AgentHost_CBR_cbQueueMission, misFile, (int)strlen(misFile)+1 );
	
	return 0;
}

int AgentHost::runMission( char *misFile ) {
//	AgentType type;

	DataStream lds;

	lds.reset();
	lds.packString( misFile );
	this->globalStateTransaction( OAC_MISSION_START, lds.stream(), lds.length() );

	Log.log( LOG_LEVEL_VERBOSE, "AgentHost::runMission: starting mission %s", misFile );

/*	switch ( num ) {
	case 1:
		{
			// request mission executive
			UuidFromString( (RPC_WSTR)_T("3ce2a3f1-5b63-4481-9fc8-544857faa381"), &type.uuid );
			type.instance = -1;
			sprintf_s( type.name, sizeof(type.name), "ExecutiveMission" );

			UUID thread = this->conversationInitiate( AgentHost_CBR_convRequestUniqueSpawn, REQUESTAGENTSPAWN_TIMEOUT, &type, sizeof(AgentType) );
			if ( thread == nilUUID ) {
				return 1;
			}
			this->ds.reset();
			this->ds.packUUID( this->getUUID() );
			this->ds.packUUID( &type.uuid );
			this->ds.packChar( type.instance );
			this->ds.packFloat32( 0 ); // affinity
			this->ds.packChar( DDBAGENT_PRIORITY_CRITICAL );
			this->ds.packUUID( &thread );
			this->conProcessMessage( NULL, MSG_RAGENT_SPAWN, this->ds.stream(), this->ds.length() );
			this->ds.unlock();
		}
		break;
	default:
		break;
	};
*/
	return 0;
}

//-----------------------------------------------------------------------------
// Step

int AgentHost::step() {
	int i, l;

	// handle keyboard input
	apb->apbString( this->keyboardBuf, sizeof(this->keyboardBuf) );
	
	l = (int)strlen( this->keyboardBuf );
	for ( i=0; i<l; i++ ) {
		switch ( this->keyboardBuf[i] ) {
		case 27: // escape
			this->prepareStop();
			break;
		case 'x':
			this->gracefulExit();
			break;
		case 'c':
			this->simulateCrash();
			break;
		case '`':
			this->runMission( "data\\missions\\mission.ini" );
			break;
		case '1': 
			this->runMission( "data\\missions\\missionTest1.ini" );
			break;
		case '2': 
			this->runMission( "data\\missions\\missionTest2.ini" );
			break;
		case '3': 
			this->runMission( "data\\missions\\missionTest3.ini" );
			break;
		case '4': 
			this->runMission( "data\\missions\\missionTest4.ini" );
			break;
		case '5': 
			this->runMission( "data\\missions\\missionTest5.ini" );
			break;
		case '6':
			this->cbbaPAStart(); // reallocate agents
			break;
		case '7':
			{	 // crash an agent
				mapAgentInfo::iterator iA;
				for ( iA = this->agentInfo.begin(); iA != this->agentInfo.end(); iA++ ) {
					if ( !strcmp( iA->second.type.name, "AgentSensorLandmark" ) )
						this->sendAgentMessage( (UUID *)&iA->first, MSG_AGENT_SIMULATECRASH );
				}
			}
			break;
		case '8':
			{	 // crash an agent
				mapAgentInfo::iterator iA;
				for ( iA = this->agentInfo.begin(); iA != this->agentInfo.end(); iA++ ) {
					if ( !strcmp( iA->second.type.name, "AgentSensorFloorFinder" ) )
						this->sendAgentMessage( (UUID *)&iA->first, MSG_AGENT_SIMULATECRASH );
				}
			}
			break;
		case '9':
			{	 // crash an agent
				mapAgentInfo::iterator iA;
				for ( iA = this->agentInfo.begin(); iA != this->agentInfo.end(); iA++ ) {
					if ( !strcmp( iA->second.type.name, "AgentSensorCooccupancy" ) )
						this->sendAgentMessage( (UUID *)&iA->first, MSG_AGENT_SIMULATECRASH );
				}
			}
			break;
		case '0':
			{	 // crash an agent
				mapAgentInfo::iterator iA;
				for ( iA = this->agentInfo.begin(); iA != this->agentInfo.end(); iA++ ) {
					if ( !strcmp( iA->second.type.name, "AgentPathPlanner" ) )
						this->sendAgentMessage( (UUID *)&iA->first, MSG_AGENT_SIMULATECRASH );
				}
			}
			break;
		default:
			// ignore
			break;
		}
	}
	this->keyboardBuf[0] = '\0'; // clear buffer

	int ret = apb->apbHostStopFlag( AgentBase::step() );

	if ( ret )
		Log.log( 0, "AgentHost::step: stop flag set!" );

	return ret;
}

//-----------------------------------------------------------------------------
// OAC handling

int AgentHost::atomicMessageEvaluate( UUID *id, unsigned char message, char *data, unsigned int len ) {
	DataStream lds;

	switch ( message ) {
	case OAC_GM_REMOVE:
		{
			this->gmOACRemoveAndMembershipCount++; // track how many remove and membership transactions we have going

			UUID q, ii;
			list<UUID> removalList;
			list<UUID>::iterator iM;
			list<UUID>::iterator iI;
			list<UUID>::iterator iJ;
			lds.setData( data, len );
			lds.unpackUUID( &q );
			lds.unpackUUID( &ii ); // ignore key
			while ( lds.unpackBool() ) {
				lds.unpackUUID( &ii );
				removalList.push_back( ii );
			}
			lds.unlock();

			// who do we think is the leader?
			for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); iM++ ) {
				for ( iI = this->gmRemoveList.begin(); iI != this->gmRemoveList.end(); iI++ ) {
					if ( *iM == *iI )
						break;
				}
				if ( iI == this->gmRemoveList.end() ) // iM isn't on the removeList
					break;
			}

			if ( iM != this->gmMemberList.end() && *iM == q ) { // we accept q as the leader
				// check removal list
				for ( iJ = removalList.begin(); iJ != removalList.end(); iJ++ ) {
					for ( iI = this->gmRemoveList.begin(); iI != this->gmRemoveList.end(); iI++ ) {
						if ( *iJ == *iI )
							break;
					}
					if ( iI == this->gmRemoveList.end() ) // iJ isn't on the removeList
						return 0; // vote no: don't agree with removal list
				}
				return 1; // vote yes
			} else {
				return 0; // vote no: not accepted leader
			}

		}
		break;
	case OAC_GM_MEMBERSHIP:
		this->gmOACRemoveAndMembershipCount++; // track how many remove and membership transactions we have going
		break;
	case OAC_GM_FORMATIONFALLBACK:
		{
			UUID q, ii;
			list<UUID> newMemberList;
			list<UUID> propMemberList;
			mapAgentHostState::iterator iH;
			list<UUID>::iterator iI;
			list<UUID>::iterator iJ;
			lds.setData( data, len );
			lds.unpackUUID( &q );
			while ( lds.unpackBool() ) {
				lds.unpackUUID( &ii );
				newMemberList.push_back( ii );
			}
			lds.unlock();

			if ( this->gmMemberList.size() != 0 ) { // already have a group
				return 0; // vote no
			} else {
				// build our proposed member list
				spAddressPort ap = this->gmCoreHosts;
				int i = 0;
				while ( ap != NULL && i < this->gmCoreHead ) {
					if ( apCmp( ap, &STATE(AgentHost)->serverAP ) == 0 ) { // it's us
						propMemberList.push_back( *this->getUUID() );
					} else {
						for ( iH = this->hostKnown.begin(); iH != this->hostKnown.end(); iH++ ) {
							if ( apCmp( &iH->second->serverAP, ap ) == 0 ) { // found them
								if ( this->connectionStatus( iH->first ) == CS_TRUSTED ) // trust them
									propMemberList.push_back( iH->first ); 
								break;
							}
						}
					}
					ap = ap->next;
					i++;
				}

				// compare lists
				if ( newMemberList.size() != propMemberList.size() ) {
					return 0; // vote no: lists don't match
				}
				for ( iI = newMemberList.begin(), iJ = propMemberList.begin(); iI != newMemberList.end(); iI++, iJ++ ) {
					if ( *iI != *iJ )
						return 0; // vote no: lists don't match
				}

				return 1; // vote yes
			}

		}
		break;
	default:
		return AgentBase::atomicMessageEvaluate( id, message, data, len );
	};

	return 1; // vote yes
}

int AgentHost::atomicMessageDecided( char commit, UUID *id ) {
	DataStream lds;
	list<UUID>::iterator iI;
	AtomicMessage *am = &this->atomicMsgs[*id];

	// statistics
	this->statisticsAtomicMsg[*id].initiator = am->initiator;
	this->statisticsAtomicMsg[*id].participantCount = (int)am->targets.size();
	this->statisticsAtomicMsg[*id].order = am->order;
	this->statisticsAtomicMsg[*id].round = am->round;
	this->statisticsAtomicMsg[*id].msgsSent = am->msgsSent;
	this->statisticsAtomicMsg[*id].dataSent = am->dataSent;
	this->statisticsAtomicMsg[*id].orderChanges = am->orderChanges;
	this->statisticsAtomicMsg[*id].delivered = (am->delivered == 1);
	this->statisticsAtomicMsg[*id].startT = am->startT;
	this->statisticsAtomicMsg[*id].decidedT = am->decidedT;
	this->statisticsAtomicMsg[*id].deliveredT = am->deliveredT;

	bool def = false;
	switch ( am->msg ) {
	case OAC_GM_REMOVE:
		if ( !commit ) { // aborted
			this->gmOACRemoveAndMembershipCount--; // track how many remove and membership transactions we have going
			this->hostGroupUpdateMembership();		

			UUID q;
			lds.setData( (char *)this->getDynamicBuffer( am->dataRef ), sizeof(UUID) );
			lds.unpackUUID( &q );
			lds.unlock();

			if ( q == *this->getUUID() ) 
				this->_hostGroupUpdateMembership2(); // OAC_GM_REMOVE aborted
		}
		break;
	case OAC_GM_MEMBERSHIP:
		if ( !commit ) { // aborted
			this->gmOACRemoveAndMembershipCount--; // track how many remove and membership transactions we have going

			UUID q, key;
			lds.setData( (char *)this->getDynamicBuffer( am->dataRef ), sizeof(UUID)*2 );
			lds.unpackUUID( &q );
			lds.unpackUUID( &key );
			lds.unlock();
			if ( key == this->gmLocked ) {
				this->hostGroupUnlock();
				this->gmUpdatingMembers = false;
				this->hostGroupUpdateMembership();
			}
		}
		break;
	case OAC_GM_FORMATIONFALLBACK:
		if ( !commit ) {
			UUID q;
			lds.setData( (char *)this->getDynamicBuffer( am->dataRef ), sizeof(UUID) );
			lds.unpackUUID( &q );
			lds.unlock();

			if ( q == *this->getUUID() ) 
				this->hostGroupStartFallback();
		}
		break;
	case OAC_PA_START:
		if ( !commit ) {
			UUID q;
			int sesId;
			lds.setData( (char *)this->getDynamicBuffer( am->dataRef ), sizeof(UUID) + sizeof(int) );
			lds.unpackUUID( &q );
			sesId = lds.unpackInt32();
			lds.unlock();
			
			if ( q == *this->getUUID() && sesId == this->paSession.id ) {
				// make sure group and agents are still the same
				if ( this->paSession.group.size() == this->gmMemberList.size() ) { // group size matches
					list<UUID>::iterator iG;
					list<UUID>::iterator iH;
					for ( iG = this->paSession.group.begin(), iH = this->gmMemberList.begin();
						  iG != this->paSession.group.end(); iG++, iH++ ) {
						if ( *iG != *iH )
							break;
					}
					if ( iG == this->paSession.group.end() ) { // group matches
						mapAgentInfo::iterator iAI;
						map<UUID, PA_ProcessInfo, UUIDless>::iterator iP;
						iP = this->paSession.p.begin();
						for ( iAI = this->agentInfo.begin(); iAI != this->agentInfo.end(); iAI++ ) {
							if ( iAI->second.activationMode == AM_UNSET || iAI->second.activationMode == AM_EXTERNAL )
								continue; // skip
							if ( iP == this->paSession.p.end() )
								break;
							if ( iP->first != iAI->first ) 
								break;
							iP++;
						}
						if ( iAI == this->agentInfo.end() && iP == this->paSession.p.end() ) { // agents match
							// repeat message
							this->atomicMessageOrdered( &this->oac_GLOBAL, &this->paSession.group, OAC_PA_START, (char *)this->getDynamicBuffer( am->dataRef ), am->len );
						}
					}
				}
			}	
		}
		break;
	case OAC_PA_FINISH:
		if ( !commit ) {
			UUID q;
			int sesId;
			lds.setData( (char *)this->getDynamicBuffer( am->dataRef ), sizeof(UUID) + sizeof(int) );
			lds.unpackUUID( &q );
			sesId = lds.unpackInt32();
			lds.unlock();
			
			if ( q == *this->getUUID() && sesId == this->paSession.id ) {
				// make sure group and agents are still the same
				if ( this->paSession.group.size() == this->gmMemberList.size() ) { // group size matches
					list<UUID>::iterator iG;
					list<UUID>::iterator iH;
					for ( iG = this->paSession.group.begin(), iH = this->gmMemberList.begin();
						  iG != this->paSession.group.end(); iG++, iH++ ) {
						if ( *iG != *iH )
							break;
					}
					if ( iG == this->paSession.group.end() ) { // group matches
						mapAgentInfo::iterator iAI;
						map<UUID, PA_ProcessInfo, UUIDless>::iterator iP;
						iP = this->paSession.p.begin();
						for ( iAI = this->agentInfo.begin(); iAI != this->agentInfo.end(); iAI++ ) {
							if ( iAI->second.activationMode == AM_UNSET || iAI->second.activationMode == AM_EXTERNAL )
								continue; // skip
							if ( iP == this->paSession.p.end() )
								break;
							if ( iP->first != iAI->first ) 
								break;
							iP++;
						}
						if ( iAI == this->agentInfo.end() && iP == this->paSession.p.end() ) { // agents match
							// repeat message
							this->atomicMessageOrdered( &this->oac_GLOBAL, &this->paSession.group, OAC_PA_FINISH, (char *)this->getDynamicBuffer( am->dataRef ), am->len );
						}
					}
				}
			}	
		}
		break;
	default:
		def = true;
	};

	
	if ( this->gmWaitingUpdateMembership ) { // we need to clear our active OAC list
		for ( iI = this->gmActiveOACList.begin(); iI != this->gmActiveOACList.end(); iI++ ) {
			if ( *iI == *id ) {
				this->gmActiveOACList.erase( iI );
				this->_hostGroupUpdateMembershipCheckWait();
				break;
			}
		}
	}

	if ( this->gracefulExitWaitingOnOACs ) { // we need to clear our active OAC list
		for ( iI = this->gmLeaveActiveOACList.begin(); iI != this->gmLeaveActiveOACList.end(); iI++ ) {
			if ( *iI == *id ) {
				this->gmLeaveActiveOACList.erase( iI );

				if ( this->gmLeaveActiveOACList.empty() ) {
					this->gracefulExitWaitingOnOACs = false;
					// graceful exit is already prepared, so stop
					STATE(AgentBase)->stopFlag = true; 
				}
				break;
			}
		}
	}

	map<UUID,list<UUID>,UUIDless>::iterator iL;
	for ( iL = this->agentTransferActiveOACList.begin(); iL != this->agentTransferActiveOACList.end(); ) {
		for ( iI = iL->second.begin(); iI != iL->second.end(); iI++ ) {
			if ( *iI == *id ) {
				iL->second.erase( iI );
				this->AgentTransferUpdate( &nilUUID, (UUID *)&iL->first, &nilUUID );
				break;
			}
		}
		if ( iL->second.empty() ) {
			UUID agent = iL->first;
			iL++;
			this->agentTransferActiveOACList.erase( agent );
		} else {
			iL++;
		}
	}

	if ( def )
		return AgentBase::atomicMessageDecided( commit, id );
	else
		return 0;
}

int AgentHost::_atomicMessageSendOrdered( UUID *queue ) {
	DataStream lds;
	AtomicMessage *amq;
	std::list<UUID>::iterator iT;

	if ( this->atomicMsgsOrderedQueue[*queue].empty() ) 
		return 0; // nothing to send

	amq = &this->atomicMsgsOrderedQueue[*queue].front();

	if ( amq->msg == AgentHost_MSGS::MSG_GM_ACKLOCK ) { // handle specially

		// send lock ack
		if ( amq->targets.front() == *this->getUUID() ) {
			UUID lockedId;
			lds.setData( (char *)this->getDynamicBuffer( amq->dataRef ), amq->len );
			lds.unpackData( sizeof(UUID) ); // us
			lds.unpackUUID( &lockedId );
			lds.unlock();
			this->hostGroupAckLock( *this->getUUID(), lockedId );
		} else {
			this->sendMessageEx( this->hostKnown[amq->targets.front()]->connection, MSGEX(AgentHost_MSGS,MSG_GM_ACKLOCK), (char *)this->getDynamicBuffer( amq->dataRef ), amq->len );
		}


		if ( amq->len ) {
			this->freeDynamicBuffer( amq->dataRef );
		}

		this->atomicMsgs.erase( amq->id ); // clean!

		this->atomicMsgsOrderedQueue[*queue].pop_front();


		return this->_atomicMessageSendOrdered( queue );
	} else {
		return AgentBase::_atomicMessageSendOrdered( queue );
	}
	
	return 0;
}

int AgentHost::connectionStatusChanged( spConnection con, int status ) {
	DataStream lds;

	if ( con->uuid != nilUUID ) {
		mapAgentHostState::iterator iH = this->hostKnown.find( con->uuid );
		
		// set QOS parameters
		if ( status == CS_TRUSTED ) {
			if ( iH != this->hostKnown.end() ) { // host
				this->setConnectionfailureDetectionQOS( con, FD_HOST_TDu, FD_HOST_TMRL, FD_HOST_TMU );
			} else if ( this->offlineSLAMmode != -1 ) { // offline mode, no need for failure detection
				this->setConnectionfailureDetectionQOS( con, 2*60*60*1000, FD_AGENT_TMRL, FD_AGENT_TMU );
			} else { // agent
				this->setConnectionfailureDetectionQOS( con, FD_AGENT_TDu, FD_AGENT_TMRL, FD_AGENT_TMU );
			}
		}

		if ( iH != this->hostKnown.end() && iH->second->connection == con ) { // is a host and this is it's active connection
			// host group
			if ( status == CS_TRUSTED ) {
				this->hostGroupTrust( con->uuid );
			} else if ( status == CS_SUSPECT ) {
				this->hostGroupSuspect( con->uuid );
			} else {
				this->hostGroupSuspect( con->uuid );
				this->hostGroupPermanentlySuspect( con->uuid );
			}

			// check atomic messages
			mapAtomicMessage::iterator iAM;
			AtomicMessage *am;
			std::list<UUID>::iterator iTrg;
			for ( iAM = this->atomicMsgs.begin(); iAM != this->atomicMsgs.end(); ) {
				for ( iTrg = iAM->second.targets.begin(); iTrg != iAM->second.targets.end(); iTrg++ ) { // check if this agent was a target
					if ( *iTrg == con->uuid )
						break;
				}
				UUID id = iAM->first;
				am = &iAM->second;
				iAM++;

				if ( iTrg != am->targets.end() ) { // they were
					if ( status == CS_TRUSTED ) {
						this->_atomicMessageTrust( &id, &con->uuid );
					} else if ( status == CS_SUSPECT ) {
						this->_atomicMessageSuspect( &id, &con->uuid );
					} else {
						this->_atomicMessagePermanentlySuspect( &id, &con->uuid );
					}
				}
			}

			// check ordered atomic message queues
			std::map<UUID, std::list<AtomicMessage>, UUIDless>::iterator iQ;
			std::list<AtomicMessage>::iterator iMq;
			for ( iQ = this->atomicMsgsOrderedQueue.begin(); iQ != this->atomicMsgsOrderedQueue.end(); iQ++ ) {
				for ( iMq = iQ->second.begin(); iMq != iQ->second.end(); iMq++ ) {
					for ( iTrg = iMq->targets.begin(); iTrg != iMq->targets.end(); iTrg++ ) { // check if this agent was a target
						if ( *iTrg == con->uuid )
							break;
					}
					if ( iTrg != iMq->targets.end() ) { // they were
						// nothing to do?
					}
				}		
			}

			if ( status == CS_NO_CONNECTION ) {
				iH->second->connection = NULL; // no connection to the host anymore!
			}
		}

		mapAgentInfo::iterator iA = this->agentInfo.find( con->uuid );
		if ( iA != this->agentInfo.end() && *dStore->AgentGetHost( &con->uuid ) == *this->getUUID() && iA->second.con == con ) { // it's an agent, and it still belongs to us
			if ( status != CS_TRUSTED ) {
				Log.log( 0, "AgentHost::connectionStatusChanged: agent suspect (%s)", Log.formatUUID( 0, &con->uuid ) );

				// fail agent
				UUID tId;
				apb->apbUuidCreate( &tId );
				lds.reset();
				lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
				lds.packUUID( &nilUUID ); 
				lds.packUUID( &tId );
				lds.packInt32( DDBAGENT_STATUS_FAILED );
				lds.packUUID( &tId );
				lds.rewind();
				this->ddbAgentSetInfo( &con->uuid, &lds );
				lds.unlock();

				// tell the agent to stop, just in case
				this->sendMessage( con, MSG_AGENT_STOP ); // stop agent
				
				// clean up connection
				spConnection oldCon = iA->second.con;
				this->stopWatchingConnection( iA->second.con, iA->second.watcher );
				iA->second.con = NULL;
				iA->second.watcher = 0;
				this->closeConnection( oldCon );

			}
		}
	}

	return AgentBase::connectionStatusChanged( con, status );
}


//-----------------------------------------------------------------------------
// Host Functions

int AgentHost::hostGroupJoin() { 
	int i;

	
	Log.log( 0, "debug release: A1" );

	// decided max core failures
	int coreSize = 0;
	spAddressPort ap = this->gmCoreHosts;
	while ( ap != NULL ) {
		coreSize++;
		ap = ap->next;
	}
	this->gmMaxCoreFailures = min( 10, coreSize/2 );
	if ( this->gmMaxCoreFailures == (float)coreSize/2.0f )
		this->gmMaxCoreFailures--; // strictly less than

	Log.log( 0, "debug release: A2" );

	// decide core head size
	this->gmCoreHead = this->gmMaxCoreFailures + 1;

	Log.log( 0, "debug release: A3" );

	// connect to all core members
	ap = this->gmCoreHosts;
	spAddressPort newAP;
	spConnection con;
	while ( ap ) { 
		
		Log.log( 0, "debug release: A4 %s:%s %s:%s %d %d", ap->address, ap->port, STATE(AgentHost)->serverAP.address, STATE(AgentHost)->serverAP.port, (int)sizeof(sAddressPort), (int)sizeof(spAddressPort) );
		if ( apCmp( ap, &STATE(AgentHost)->serverAP ) != 0 ) { // not us
			
			// get ready to apply
			newAP = (spAddressPort)malloc( sizeof(sAddressPort) );
			*newAP = *ap;
			newAP->next = this->gmApplyTo;
			this->gmApplyTo = newAP;

			Log.log( 0, "debug release: A4.1" );

			con = this->openConnection( ap, NULL, 60 );
			Log.log( 0, "debug release: A4.2" );
			if ( con ) {
				this->watchConnection( con, AgentHost_CBR_cbWatchHostConnection );
				Log.log( 0, "debug release: A4.3" );

			}
			Log.log( 0, "debug release: A4.4" );

		}
		ap = ap->next; 
	}

	
	Log.log( 0, "debug release: A5" );

	if ( apCmp( this->gmCoreHosts, &STATE(AgentHost)->serverAP ) == 0 ) { // we are the initial leader
		list<UUID> newMemberList;
		apb->apbUuidCreate( &this->gmLocked );
		
		Log.log( 0, "debug release: A6" );
		newMemberList.push_back( *this->getUUID() );
		this->gmOACRemoveAndMembershipCount++;
		this->hostGroupMembership( *this->getUUID(), this->gmLocked, &newMemberList );
	} else { 
		int coreMember = 0;
		i = 0;
		ap = this->gmCoreHosts;
		while ( i < this->gmCoreHead ) {
			if ( apCmp( ap, &STATE(AgentHost)->serverAP ) == 0 ) { // we are a member of coreHead
				coreMember = 1;		
				this->addTimeout( GM_FORMATION_TIMEOUT, AgentHost_CBR_cbHostFormationTimeout, &coreMember, sizeof(int) );
				break;
			}
			ap = ap->next;
			i++;
		}
		
		if ( i == this->gmCoreHead ) { // we are a regular member
			this->addTimeout( GM_FORMATION_TIMEOUT*2, AgentHost_CBR_cbHostFormationTimeout, &coreMember, sizeof(int) ); // wait twice as long so that the core members have a chance to create a fallback group
		}
	}

	
	Log.log( 0, "debug release: AF" );

	return 0;
}

int AgentHost::hostGroupLeave() {
	DataStream lds;
	list<UUID>::iterator iM;

	// send leave to all members
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); iM++ ) {
		if ( *iM == *this->getUUID() ) {
			this->hostGroupLeaveRequest( *this->getUUID() );
		} else {
			this->sendMessageEx( this->hostKnown[*iM]->connection, MSGEX(AgentHost_MSGS,MSG_GM_LEAVE), lds.stream(), lds.length() ); // introduce ourselves
		}
	}
	lds.unlock();
	
	return 0;
}

int AgentHost::_hostGroupLeaveSuccessful() {
	mapAtomicMessage::iterator iA;

	// finish all active OACs
	this->gmLeaveActiveOACList.clear();
	for ( iA = this->atomicMsgs.begin(); iA != this->atomicMsgs.end(); iA++ ) {
		if ( iA->second.orderedMsg && !iA->second.delivered && iA->second.queue == this->oac_GLOBAL ) 
			this->gmLeaveActiveOACList.push_back( iA->first );
	}

	this->gracefulExitWaitingOnOACs = true;

	if ( this->gmLeaveActiveOACList.empty() ) {
		this->gracefulExitWaitingOnOACs = false;
		// graceful exit is already prepared, so stop
		STATE(AgentBase)->stopFlag = true; 
	}

	return 0;
}

int AgentHost::hostGroupLeaveRequest( UUID q ) {
	list<UUID>::iterator iI;

	Log.log( 0, "AgentHost::hostGroupLeaveRequest: leave request from %s", Log.formatUUID(0,&q) );

	// check member list
	for ( iI = this->gmMemberList.begin(); iI != this->gmMemberList.end(); iI++ ) {
		if ( *iI == q ) {
			this->gmLeaveList.push_back( q );
			this->hostGroupUpdateMembership();
			return 0;
		}
	}

	// check join list
	for ( iI = this->gmJoinList.begin(); iI != this->gmJoinList.end(); iI++ ) {
		if ( *iI == q ) {
			this->gmLeaveList.push_back( q );
			this->hostGroupUpdateMembership();
			return 0;
		}
	}

	return 0;
}

int AgentHost::hostGroupSuspect( UUID q ) {
	list<UUID>::iterator iI;

	// check remove list
	for ( iI = this->gmRemoveList.begin(); iI != this->gmRemoveList.end(); iI++ ) {
		if ( *iI == q ) {
			return 0; // already on our list
		}
	}

	// check member list
	for ( iI = this->gmMemberList.begin(); iI != this->gmMemberList.end(); iI++ ) {
		if ( *iI == q ) {
			this->gmRemoveList.push_back( q );
			this->hostGroupUpdateMembership();
			break;
		}
	}

	if ( this->gmWaitingUpdateMembership ) { // we might be finished waiting
		this->_hostGroupUpdateMembershipCheckWait();
	}

	return 0;
}

int AgentHost::hostGroupPermanentlySuspect( UUID q ) {
	list<UUID>::iterator iI;

	// check join list
	for ( iI = this->gmJoinList.begin(); iI != this->gmJoinList.end(); iI++ ) {
		if ( *iI == q ) {
			this->gmJoinList.erase( iI );	
			return 0;
		}
	}

	return 0;
}

int AgentHost::hostGroupTrust( UUID q ) {
	list<UUID>::iterator iI;

	// check join list
	for ( iI = this->gmJoinList.begin(); iI != this->gmJoinList.end(); iI++ ) {
		if ( *iI == q ) {
			this->hostGroupUpdateMembership();
			return 0;
		}
	}

	
	// check remove list
	for ( iI = this->gmRemoveList.begin(); iI != this->gmRemoveList.end(); iI++ ) {
		if ( *iI == q ) {
			this->gmRemoveList.erase( iI );
			this->hostGroupUpdateMembership();
			return 0;
		}
	}

	return 0;
}

int AgentHost::hostGroupApply( UUID q, spAddressPort qAP, __int64 *key ) {
	DataStream lds;

	if ( key[0] != this->clusterID[0] || key[1] != this->clusterID[1] ) { // key does not match, reject them!
		Log.log( 0, "AgentHost::hostGroupApply: key doesn't match, rejecting %s", Log.formatUUID(0,&q) );
		this->sendMessageEx( this->hostKnown[q]->connection, MSGEX(AgentHost_MSGS,MSG_GM_REJECT) ); 
		return 0;
	}

	memcpy( &this->hostKnown[q]->serverAP, qAP, sizeof(sAddressPort) );

	// introduce ourselves
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packData( &STATE(AgentHost)->serverAP, sizeof(sAddressPort) );
	this->sendMessageEx( this->hostKnown[q]->connection, MSGEX(AgentHost_MSGS,MSG_GM_INTRODUCE), lds.stream(), lds.length() ); // introduce ourselves
	lds.unlock();

	// introduce q to current group and all applicants
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packUUID( &q );
	lds.packData( &this->hostKnown[q]->serverAP, sizeof(sAddressPort) );
	
	list<UUID>::iterator iH;
	for ( iH = this->gmMemberList.begin(); iH != this->gmMemberList.end(); iH++ ) {
		if ( *iH != *this->getUUID() )
			this->sendMessageEx( this->hostKnown[*iH]->connection, MSGEX(AgentHost_MSGS,MSG_GM_INTRODUCE), lds.stream(), lds.length() ); 
	}
	for ( iH = this->gmJoinList.begin(); iH != this->gmJoinList.end(); iH++ ) {
		this->sendMessageEx( this->hostKnown[*iH]->connection, MSGEX(AgentHost_MSGS,MSG_GM_INTRODUCE), lds.stream(), lds.length() );
	}
	lds.unlock();

	// add to join list	
	this->gmJoinList.push_back( q );

	if ( this->gmMemberList.size() && *this->getUUID() == this->gmMemberList.front() ) { // we're the undisputed leader
		this->hostGroupNewSponsee( q ); // sponsor q
	}

	return 0;
}

int AgentHost::hostGroupIntroduce( UUID q, UUID a, spAddressPort aAP ) {
	DataStream lds;

	if ( a == *this->getUUID() )
		return 0; // we already know ourselves!

	list<UUID>::iterator iI;
	for ( iI = this->gmIntroducedTo.begin(); iI != this->gmIntroducedTo.end(); iI++ ) {
		if ( *iI == a )
			break;
	}
	if ( iI == this->gmIntroducedTo.end() ) { // we haven't tried introduce ourselves to a yet
		this->gmIntroducedTo.push_back( a );
		
		for ( iI = this->gmConnection.begin(); iI != this->gmConnection.end(); iI++ ) {
			if ( *iI == a )
				break;
		}
		if ( iI == this->gmConnection.end() ) { // we haven't got a connection to a
			
			// get ready to introduce
			spConnection con;
			spAddressPort newAP = (spAddressPort)malloc( sizeof(sAddressPort) );
			*newAP = *aAP;
			newAP->next = this->gmIntroduceTo;
			this->gmIntroduceTo = newAP;

			// open connection
			con = this->openConnection( aAP, NULL, 60 );
			if ( con ) {
				this->watchConnection( con, AgentHost_CBR_cbWatchHostConnection );
			}

		} else  {
		
			// introduce ourselves
			lds.reset();
			lds.packUUID( &STATE(AgentBase)->uuid );
			lds.packUUID( &STATE(AgentBase)->uuid );
			lds.packData( &STATE(AgentHost)->serverAP, sizeof(sAddressPort) );
			this->sendMessageEx( this->hostKnown[q]->connection, MSGEX(AgentHost_MSGS,MSG_GM_INTRODUCE), lds.stream(), lds.length() ); // introduce ourselves
			lds.unlock();

		}
	}

	for ( iI = this->gmMemberList.begin(); iI != this->gmMemberList.end(); iI++ ) {
		if ( *iI == *this->getUUID() )
			break;
	}
	if ( iI == this->gmMemberList.end() && this->gmSponsor != nilUUID ) { // we aren't a member and have a sponsor
		// send connection update
		lds.reset();
		lds.packUUID( &STATE(AgentBase)->uuid );
		lds.packChar( this->gmGlobalStateSynced );
		lds.packInt32( (int)this->gmConnection.size() );
		for ( iI = this->gmConnection.begin(); iI != this->gmConnection.end(); iI++ ) {
			lds.packUUID( &*iI );
		}
		this->sendMessageEx( this->hostKnown[this->gmSponsor]->connection, MSGEX(AgentHost_MSGS,MSG_GM_CONNECTIONS), lds.stream(), lds.length() ); // introduce ourselves
		lds.unlock();
	}

	return 0;
}

int AgentHost::hostGroupSponsor( UUID q ) {
	DataStream lds;

	this->gmSponsor = q;

	// clear global state
	this->gmGlobalStateSynced = 0;
	this->_ddbClearWatchers();
	this->dStore->Clean();

	// send connection update
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packChar( this->gmGlobalStateSynced );
	lds.packInt32( (int)this->gmConnection.size() );
	list<UUID>::iterator iI;
	for ( iI = this->gmConnection.begin(); iI != this->gmConnection.end(); iI++ ) {
		lds.packUUID( &*iI );
	}
	this->sendMessageEx( this->hostKnown[this->gmSponsor]->connection, MSGEX(AgentHost_MSGS,MSG_GM_CONNECTIONS), lds.stream(), lds.length() ); // introduce ourselves
	lds.unlock();

	return 0;
}

int AgentHost::hostGroupGlobalStateEnd( UUID q ) {
	DataStream lds;

	if ( q != this->gmSponsor )
		return 0; // not our sponsor!?

	this->gmGlobalStateSynced = 1;

	// send connection update
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	lds.packChar( this->gmGlobalStateSynced );
	lds.packInt32( (int)this->gmConnection.size() );
	list<UUID>::iterator iI;
	for ( iI = this->gmConnection.begin(); iI != this->gmConnection.end(); iI++ ) {
		lds.packUUID( &*iI );
	}
	this->sendMessageEx( this->hostKnown[this->gmSponsor]->connection, MSGEX(AgentHost_MSGS,MSG_GM_CONNECTIONS), lds.stream(), lds.length() ); // introduce ourselves
	lds.unlock();

	return 0;
}

int AgentHost::hostGroupNewSponsee( UUID q ) {
	DataStream lds;

	this->gmSponsee[q].clear();
	this->gmSponseeGlobalStateSync.erase( q );
		
	lds.reset();
	lds.packUUID( &STATE(AgentBase)->uuid );
	this->sendMessageEx( this->hostKnown[q]->connection, MSGEX(AgentHost_MSGS,MSG_GM_SPONSOR), lds.stream(), lds.length() );
	lds.unlock();

	// send global state to q
	mapDDBWatchers::iterator watchers;
	mapDDBItemWatchers::iterator iwatchers;
	std::list<UUID>::iterator iI;

	for ( watchers = this->DDBWatchers.begin(); watchers != this->DDBWatchers.end(); watchers++ ) {
		for ( iI = watchers->second->begin(); iI != watchers->second->end(); iI++ ) {
			lds.reset();
			lds.packUUID( &*iI );
			lds.packInt32( watchers->first );
			this->sendMessage( this->hostKnown[q]->connection, OAC_DDB_WATCH_TYPE, lds.stream(), lds.length() );
			lds.unlock();
		}
	}

	for ( iwatchers = this->DDBItemWatchers.begin(); iwatchers != this->DDBItemWatchers.end(); iwatchers++ ) {
		for ( iI = iwatchers->second->begin(); iI != iwatchers->second->end(); iI++ ) {
			lds.reset();
			lds.packUUID( &*iI );
			lds.packUUID( (UUID *)&iwatchers->first );
			this->sendMessage( this->hostKnown[q]->connection, OAC_DDB_WATCH_ITEM, lds.stream(), lds.length() );
			lds.unlock();
		}
	}

	this->ddbEnumerate( this->hostKnown[q]->connection, 0 );

	// confirm global state sent
	lds.reset();
	lds.packUUID( this->getUUID() );
	this->sendMessageEx( this->hostKnown[q]->connection, MSGEX(AgentHost_MSGS,MSG_GM_GLOBALSTATEEND), lds.stream(), lds.length() );
	lds.unlock();

	return 0;
}

int AgentHost::hostGroupSponseeUpdate( DataStream *ds ) {
	int i, count;
	UUID q;
	UUID c;
	list<UUID>::iterator iI;

	ds->unpackUUID( &q );

	if ( this->gmSponsee.find( q ) == this->gmSponsee.end() ) {
		return 1; // not found
	}

	char sync = ds->unpackChar();
	if ( sync )
		this->gmSponseeGlobalStateSync[q] = 1; // synced!

	// merge list
	count = ds->unpackInt32();
	for ( i = 0; i < count; i++ ) {
		ds->unpackUUID( &c );

		for ( iI = this->gmSponsee[q].begin(); iI != this->gmSponsee[q].end(); iI++ ) {
			if ( *iI == c )
				break;
		}
		if ( iI == this->gmSponsee[q].end() )
			this->gmSponsee[q].push_back( c );
	}

	// update membership
	this->hostGroupUpdateMembership();

	return 0;
}

int AgentHost::hostGroupUpdateMembership() {
	DataStream lds;
	list<UUID>::iterator iM;
	list<UUID>::iterator iA;
	list<UUID>::iterator iI;
	list<UUID>::iterator iJ;

	if ( this->gmUpdatingMembers || this->gmOACRemoveAndMembershipCount != 0 ) {
		return 0; // already in progress
	}

	// who do we think is the leader?
	for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); iM++ ) {
		for ( iI = this->gmRemoveList.begin(); iI != this->gmRemoveList.end(); iI++ ) {
			if ( *iM == *iI )
				break;
		}
		if ( iI == this->gmRemoveList.end() ) // iM isn't on the removeList
			break;
	}

	if ( iM != this->gmMemberList.end() && *iM == *this->getUUID() ) { // we think we are the leader
		list<UUID> potentialList;
		list<UUID> curMemberList;
		
		// current members
		this->gmActiveList.clear();
		this->gmLockList.clear();
		for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); iM++ ) {
			for ( iI = this->gmRemoveList.begin(); iI != this->gmRemoveList.end(); iI++ ) {
				if ( *iM == *iI )
					break;
			}
			if ( iI == this->gmRemoveList.end() ) { // iM isn't on the removeList
				this->gmActiveList.push_back( *iM );
				this->gmLockList.push_back( *iM );

				for ( iI = this->gmLeaveList.begin(); iI != this->gmLeaveList.end(); iI++ ) {
					if ( *iM == *iI )
						break;
				}
				if ( iI == this->gmLeaveList.end() ) { // iM isn't on the leaveList
					curMemberList.push_back( *iM );
				}
			}
		}

		// prune applicants
		for ( iA = this->gmJoinList.begin(); iA != this->gmJoinList.end(); iA++ ) {
			if ( this->connectionStatus( *iA ) != CS_TRUSTED ) 
				continue; // not trusted
			map<UUID,list<UUID>,UUIDless>::iterator iS = this->gmSponsee.find( *iA );
			if ( iS == this->gmSponsee.end() ) 
				continue; // not a sponsee
			map<UUID,char,UUIDless>::iterator iSGSS = this->gmSponseeGlobalStateSync.find( *iA );
			if ( iSGSS == this->gmSponseeGlobalStateSync.end() ) 
				continue; // global state not synced yet
			if ( this->gmGroupCorrect == false ) {
				spAddressPort ap;
				ap = this->gmCoreHosts;
				while ( ap != NULL ) {
					if ( apCmp( ap, &this->hostKnown[*iA]->serverAP ) == 0 )
						break;
					ap = ap->next;
				}
				if ( ap == NULL )
					continue; // not a correct group and not a core host
			}

			// make sure a is connected to all current members
			for ( iI = curMemberList.begin(); iI != curMemberList.end(); iI++ ) {
				for ( iJ = this->gmSponsee[*iA].begin(); iJ != this->gmSponsee[*iA].end(); iJ++ ) {
					if ( *iI == *iJ )
						break;
				}
				if ( iJ == this->gmSponsee[*iA].end() )
					break; // not connected to iI
			}
			if ( iI == curMemberList.end() ) { // a is a potential successful applicant
				potentialList.push_back( *iA );
			}
		}

		// build accept list
		this->gmAcceptList.clear();
		for ( iA = potentialList.begin(); iA != potentialList.end(); iA++ ) {
			// make sure a is connected to everyone already accepted
			for ( iI = this->gmAcceptList.begin(); iI != this->gmAcceptList.end(); iI++ ) {
				for ( iJ = this->gmSponsee[*iA].begin(); iJ != this->gmSponsee[*iA].end(); iJ++ ) {
					if ( *iI == *iJ )
						break;
				}
				if ( iJ == this->gmSponsee[*iA].end() )
					break; // not connected to iI
			}
			if ( iI == this->gmAcceptList.end() ) { // a is a successful applicant
				this->gmAcceptList.push_back( *iA );
			}
		}
		if ( this->gmGroupCorrect == false && (int)this->gmAcceptList.size() < this->gmMaxCoreFailures ) { // we need more members to ensure a correct group
			this->gmAcceptList.clear();
		}

		if ( this->gmAcceptList.size() + this->gmRemoveList.size() + this->gmLeaveList.size() > 0 ) { // we have something to update
			this->gmUpdatingMembers = true;
			
			if ( this->gmAcceptList.size() + this->gmLeaveList.size() == 0 ) {
				this->gmKey = nilUUID; // no need to lock
			} else {
				apb->apbUuidCreate( &this->gmKey );
			}

			this->gmWaitingUpdateMembership = true;
			
			// send remove request
			lds.reset();
			lds.packUUID( this->getUUID() );
			lds.packUUID( &this->gmKey );
			for ( iI = this->gmRemoveList.begin(); iI != this->gmRemoveList.end(); iI++ ) {
				lds.packBool( 1 );
				lds.packUUID( &*iI );
			}
			lds.packBool( 0 );
			this->atomicMessageOrdered( &this->oac_GLOBAL, &this->gmActiveList, OAC_GM_REMOVE, lds.stream(), lds.length() );
			lds.unlock();
		}
	}

	return 0;
}

int AgentHost::_hostGroupUpdateMembershipCheckWait() {
	list<UUID>::iterator iI;

	if ( this->gmWaitingUpdateMembership == true && this->gmActiveOACList.empty() ) {
		for ( iI = this->gmLockList.begin(); iI != this->gmLockList.end(); iI++ ) {
			if ( this->connectionStatus( *iI ) == CS_TRUSTED )
				break;
		}
		if ( iI == this->gmLockList.end() ) // we suspect everyone we are waiting for a lock from
			this->_hostGroupUpdateMembership2();	
	}

	return 0;
}

int AgentHost::_hostGroupUpdateMembership2() {
	DataStream lds;
	list<UUID>::iterator iM;
	list<UUID>::iterator iL;

	this->gmWaitingUpdateMembership = false;

	if ( this->gmKey != nilUUID && this->gmLockList.empty() ) {
		list<UUID> targets = this->gmActiveList;
	
		list<UUID> newMemberList;
		for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); iM++ ) {
			for ( iL = this->gmLeaveList.begin(); iL != this->gmLeaveList.end(); iL++ ) {
				if ( *iM == *iL )
					break;
			}
			if ( iL == this->gmLeaveList.end() ) { // not on the leave list
				newMemberList.push_back( *iM ); // old member
			}
		}
		for ( iM = this->gmAcceptList.begin(); iM != this->gmAcceptList.end(); iM++ ) {
			targets.push_back( *iM );
			newMemberList.push_back( *iM ); // new member
		}

		// send membership request
		lds.reset();
		lds.packUUID( this->getUUID() );
		lds.packUUID( &this->gmKey );
		for ( iM = newMemberList.begin(); iM != newMemberList.end(); iM++ ) {
			lds.packBool( 1 );
			lds.packUUID( &*iM );
		}
		lds.packBool( 0 );
		this->atomicMessageOrdered( &this->oac_GLOBAL, &targets, OAC_GM_MEMBERSHIP, lds.stream(), lds.length() );
		lds.unlock();

	} else {
		this->gmUpdatingMembers = false;
		this->hostGroupUpdateMembership();
	}

	return 0;
}

int AgentHost::hostGroupRemove( UUID q, UUID key, list<UUID> *removalList ) {
	list<UUID>::iterator iM;
	list<UUID>::iterator iI;
	map<UUID,list<UUID>,UUIDless>::iterator iS;

	this->gmOACRemoveAndMembershipCount--; // track how many remove and membership transactions we have going

	Log.log( LOG_LEVEL_NORMAL, "AgentHost::hostGroupRemove: removing members:" );
	for ( iI = removalList->begin(); iI != removalList->end(); iI++ ) {
		Log.log( LOG_LEVEL_NORMAL, "%s", Log.formatUUID( LOG_LEVEL_NORMAL, &*iI ) );	
	}

	list<UUID> oldMembers = this->gmMemberList;
	
	// remove removalList from members
	for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); ) {
		for ( iI = removalList->begin(); iI != removalList->end(); iI++ ) {
			if ( *iM == *iI )
				break;
		}
		if ( iI != removalList->end() ) { // remove
			iI = iM++;
			this->gmMemberList.erase( iI );
		} else {
			iM++;
		}
	}

	// reset remove list
	this->gmRemoveList.clear();
	for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); iM++ ) {
		if ( this->connectionStatus( *iM ) != CS_TRUSTED )
			this->gmRemoveList.push_back( *iM ); // suspected
	}

	// reset leave list
	for ( iI = this->gmLeaveList.begin(); iI != this->gmLeaveList.end(); ) {
		for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); iM++ ) {
			if ( *iM == *iI )
				break;
		}
		if ( iM == this->gmMemberList.end() ) { // no longer a member
			iM = iI++;
			this->gmLeaveList.erase( iM );
		} else {
			iI++;
		}
	}

	if ( this->gmMemberList.front() == *this->getUUID() ) { // we are the undisputed leader
		// take responsibility for applicants
		for ( iI = this->gmJoinList.begin(); iI != this->gmJoinList.end(); iI++ ) {
			for ( iS = this->gmSponsee.begin(); iS != this->gmSponsee.end(); iS++ ) {
				if ( *iI == iS->first ) 
					break;
			}
			if ( iS == this->gmSponsee.end() ) { // not sponsoring them yet
				this->hostGroupNewSponsee( *iI );
			}
		}
	}

	// lock if necessary
	this->hostGroupLock( q, key );

	this->hostGroupUpdateMembership();

	if ( q == *this->getUUID() ) {
		if ( this->gmLocked == nilUUID ) {
			this->_hostGroupUpdateMembership2(); // OAC(remove, NULL, p, ... ) committed

		} else {
			mapAtomicMessage::iterator iA;

			// build active OAC list
			this->gmActiveOACList.clear();
			for ( iA = this->atomicMsgs.begin(); iA != this->atomicMsgs.end(); iA++ ) {
				if ( iA->second.orderedMsg && !iA->second.delivered && iA->second.queue == this->oac_GLOBAL ) 
					this->gmActiveOACList.push_back( iA->first );
			}
		}
	}
	
	if ( this->gmLocked == nilUUID ) {	
		// check if the group has changed
		if ( oldMembers.size() != this->gmMemberList.size() ) {
			this->hostGroupChanged( &oldMembers );
		} else {
			for ( iM = this->gmMemberList.begin(), iI = oldMembers.begin(); iM != this->gmMemberList.end(); iM++, iI++ ) {
				if ( *iM != *iI )
					break;
			}
			if ( iM != this->gmMemberList.end() )
				this->hostGroupChanged( &oldMembers );
		}
	}

	return 0;
}

int AgentHost::hostGroupMembership( UUID q, UUID key, list<UUID> *newMemberList ) {
	list<UUID>::iterator iM;
	list<UUID>::iterator iI;
	map<UUID,list<UUID>,UUIDless>::iterator iS;

	this->gmOACRemoveAndMembershipCount--; // track how many remove and membership transactions we have going

	Log.log( LOG_LEVEL_NORMAL, "AgentHost::hostGroupMembership: new member list:" );
	for ( iI = newMemberList->begin(); iI != newMemberList->end(); iI++ ) {
		Log.log( LOG_LEVEL_NORMAL, "%s", Log.formatUUID( LOG_LEVEL_NORMAL, &*iI ) );	
	}

	if ( q == *this->getUUID() ) { // we sent this
		// update sponsee list
		for ( iS = this->gmSponsee.begin(); iS != this->gmSponsee.end(); ) {
			for ( iI = newMemberList->begin(); iI != newMemberList->end(); iI++ ) {
				if ( iS->first == *iI ) 
					break; // sponsee is now a member
			}
			if ( iI != newMemberList->end() ) {
				iS++;
				this->gmSponsee.erase( *iI ); // not sponsoring
				this->gmSponseeGlobalStateSync.erase( *iI );
			} else {
				iS++;
			}
		}
	}

	// accept memberlist
	list<UUID> oldMembers = this->gmMemberList;
	this->gmMemberList.clear();
	for ( iM = newMemberList->begin(); iM != newMemberList->end(); iM++ )
		this->gmMemberList.push_back( *iM );

	// update join list
	for ( iI = this->gmJoinList.begin(); iI != this->gmJoinList.end(); ) {
		for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); iM++ ) {
			if ( *iM == *iI )
				break;
		}
		if ( iM != this->gmMemberList.end() ) { // joined!
			iM = iI++;
			this->gmJoinList.erase( iM );
		} else {
			iI++;
		}
	}

	// reset remove list
	this->gmRemoveList.clear();
	for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); iM++ ) {
		if ( this->connectionStatus( *iM ) != CS_TRUSTED )
			this->gmRemoveList.push_back( *iM ); // suspected
	}

	// reset leave list
	for ( iI = this->gmLeaveList.begin(); iI != this->gmLeaveList.end(); ) {
		for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); iM++ ) {
			if ( *iM == *iI )
				break;
		}
		if ( iM == this->gmMemberList.end() ) { // no longer a member
			UUID id = *iI;
			iM = iI++;
			this->gmLeaveList.erase( iM );

			if ( id == *this->getUUID() ) 
				this->_hostGroupLeaveSuccessful(); // we were removed from the group
		} else {
			iI++;
		}
	}

	// unlock
	this->hostGroupUnlock();

	// check if we have a correct group
	if ( this->gmGroupCorrect == false ) {
		if ( (int)this->gmMemberList.size() > this->gmMaxCoreFailures ) {
			this->gmGroupCorrect = true;
			Log.log( 0, "AgentHost::hostGroupMembership: correct group formed!" );
		}
	}

	// check if the group has changed
	if ( oldMembers.size() != this->gmMemberList.size() ) {
		this->hostGroupChanged( &oldMembers );
	} else {
		for ( iM = this->gmMemberList.begin(), iI = oldMembers.begin(); iM != this->gmMemberList.end(); iM++, iI++ ) {
			if ( *iM != *iI )
				break;
		}
		if ( iM != this->gmMemberList.end() )
			this->hostGroupChanged( &oldMembers );
	}

	this->gmUpdatingMembers = false;
	this->hostGroupUpdateMembership();

	return 0;
}

int AgentHost::hostGroupStartFallback() {
	DataStream lds;
	list<UUID>::iterator iM;
	mapAgentHostState::iterator iH;
	
	for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); iM++ ) {
		if ( *iM == *this->getUUID() )
			break;
	}
	if ( iM == this->gmMemberList.end() ) { // not a member of the group
		list<UUID> propMemberList;

		// build our proposed member list
		spAddressPort ap = this->gmCoreHosts;
		int i = 0;
		while ( ap != NULL && i < this->gmCoreHead ) {
			if ( apCmp( ap, &STATE(AgentHost)->serverAP ) == 0 ) { // it's us
				propMemberList.push_back( *this->getUUID() );
			} else {
				for ( iH = this->hostKnown.begin(); iH != this->hostKnown.end(); iH++ ) {
					if ( apCmp( &iH->second->serverAP, ap ) == 0 ) { // found them
						if ( this->connectionStatus( iH->first ) == CS_TRUSTED ) // trust them
							propMemberList.push_back( iH->first ); 
						break;
					}
				}
			}
			ap = ap->next;
			i++;
		}

		// send membership list
		lds.reset();
		lds.packUUID( this->getUUID() );
		for ( iM = propMemberList.begin(); iM != propMemberList.end(); iM++ ) {
			lds.packBool( 1 );
			lds.packUUID( &*iM );
		}
		lds.packBool( 0 );
		this->atomicMessageOrdered( &this->oac_GLOBAL, &propMemberList, OAC_GM_FORMATIONFALLBACK, lds.stream(), lds.length() );
		lds.unlock();

		return 0;
	} else { // already in group
		return 1;
	}
}

int AgentHost::hostGroupFormationFallback( UUID q, list<UUID> *newMemberList ) {
	list<UUID>::iterator iM;
	list<UUID>::iterator iI;
	map<UUID,list<UUID>,UUIDless>::iterator iS;

	if ( this->gmMemberList.size() == 0 ) { 
		// accept memberlist
		this->gmMemberList.clear();
		for ( iM = newMemberList->begin(); iM != newMemberList->end(); iM++ )
			this->gmMemberList.push_back( *iM );

		// update join list
		for ( iI = this->gmJoinList.begin(); iI != this->gmJoinList.end(); ) {
			for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); iM++ ) {
				if ( *iM == *iI )
					break;
			}
			if ( iM != this->gmMemberList.end() ) { // joined!
				iM = iI++;
				this->gmJoinList.erase( iM );
			} else {
				iI++;
			}
		}

		// reset remove list
		this->gmRemoveList.clear();
		for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); iM++ ) {
			if ( this->connectionStatus( *iM ) != CS_TRUSTED )
				this->gmRemoveList.push_back( *iM ); // suspected
		}

		// reset leave list
		for ( iI = this->gmLeaveList.begin(); iI != this->gmLeaveList.end(); ) {
			for ( iM = this->gmMemberList.begin(); iM != this->gmMemberList.end(); iM++ ) {
				if ( *iM == *iI )
					break;
			}
			if ( iM == this->gmMemberList.end() ) { // no longer a member
				iM = iI++;
				this->gmLeaveList.erase( iM );
			} else {
				iI++;
			}
		}

		if ( this->gmMemberList.front() == *this->getUUID() ) { // we are the undisputed leader
			// take responsibility for applicants
			for ( iI = this->gmJoinList.begin(); iI != this->gmJoinList.end(); iI++ ) {
				for ( iS = this->gmSponsee.begin(); iS != this->gmSponsee.end(); iS++ ) {
					if ( *iI == iS->first ) 
						break;
				}
				if ( iS == this->gmSponsee.end() ) { // not sponsoring them yet
					this->hostGroupNewSponsee( *iI );
				}
			}
		}

		this->gmGroupCorrect = true;
		this->hostGroupUpdateMembership();

	}

	return 0;
}

int AgentHost::hostGroupAckLock( UUID q, UUID key ) {
	list<UUID>::iterator iI;

	if ( this->gmWaitingUpdateMembership && this->gmLocked == key ) {
		for ( iI = this->gmLockList.begin(); iI != this->gmLockList.end(); iI++ ) {
			if ( *iI == q ) {
				this->gmLockList.erase( iI );
				break;
			}
		}

		this->_hostGroupUpdateMembershipCheckWait();
	}

	return 0;
}

int AgentHost::hostGroupLock( UUID q, UUID key ) {
	DataStream lds;

	this->gmLocked = key;
	if ( this->gmLocked != nilUUID ) { // acknowledge lock, but make sure we do it in order
		list<UUID> ql;
		ql.push_back( q );
		lds.reset();
		lds.packUUID( this->getUUID() );
		lds.packUUID( &this->gmLocked );
		this->atomicMessageOrdered( &this->oac_GLOBAL, &ql, AgentHost_MSGS::MSG_GM_ACKLOCK, lds.stream(), lds.length() );
		lds.unlock();
	} else {
		this->hostGroupUnlock();
	}

	return 0;
}

int AgentHost::hostGroupUnlock() {
	this->gmLocked = nilUUID;

	// send held global state updates
	this->_globalStateTransactionSend();

	return 0;
}

int AgentHost::hostGroupChanged( list<UUID> *oldMembers ) {
	DataStream lds;
	list<UUID> added;
	list<UUID> removed;
	list<UUID>::iterator iI;
	list<UUID>::iterator iJ;
	mapAgentInfo::iterator iA;

	iJ = this->gmMemberList.begin();
	for ( iI = oldMembers->begin(); iI != oldMembers->end(); iI++ ) {
		if ( iJ == this->gmMemberList.end() || *iI != *iJ )
			removed.push_back( *iI );
		else
			iJ++;
	}
	for ( ; iJ != this->gmMemberList.end(); iJ++ ) {
		added.push_back( *iJ );
	}

	// Data dump
	for ( iI = added.begin(); iI != added.end(); iI++ ) {
		Data.log( 0, "HOST_ADDED %s", Data.formatUUID(0,&*iI) );
	}
	for ( iI = removed.begin(); iI != removed.end(); iI++ ) {
		Data.log( 0, "HOST_REMOVED %s", Data.formatUUID(0,&*iI) );
	}

	// notify watchers
	int memberCount = (int)this->gmMemberList.size();
	this->_ddbNotifyWatchers( this->getUUID(), DDB_HOST_GROUP, DDBE_UPDATE, &nilUUID, &memberCount, sizeof(int) ); // pack member count

	// check freezing/thawing locks
	if ( !removed.empty() ) {
		for ( iA = this->agentInfo.begin(); iA != this->agentInfo.end(); iA++ ) {
			mapLock::iterator iL;
			iL = this->locks.find( iA->first );
			if ( iL == this->locks.end() ) 
				continue;

			for ( iI = removed.begin(); iI != removed.end(); iI++ )
				this->AgentTransferUpdate( &*iI, (UUID *)&iA->first, &iL->second.key );
		}
	}

	// immediately flag any agents belonging to removed hosts as failed (or waiting to spawn if they haven't finished spawning)!
	UUID curHost;
	int infoFlags;
	for ( iA = this->agentInfo.begin(); iA != this->agentInfo.end(); iA++ ) {
		curHost = *dStore->AgentGetHost( (UUID *)&iA->first );
		for ( iI = removed.begin(); iI != removed.end(); iI++ ) {
			if ( *iI == curHost ) {
				infoFlags = DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS;
				lds.reset();
				lds.packUUID( &nilUUID );
				lds.packUUID( &nilUUID );
				if ( this->dStore->AgentGetStatus( (UUID *)&iA->first ) == DDBAGENT_STATUS_SPAWNING )
					lds.packInt32( DDBAGENT_STATUS_WAITING_SPAWN );
				else 
					lds.packInt32( DDBAGENT_STATUS_FAILED );
				lds.packUUID( &nilUUID );
				lds.rewind();
				infoFlags = this->dStore->AgentSetInfo( (UUID *)&iA->first, infoFlags, &lds );
				lds.unlock();
				if ( infoFlags ) {
					// notify watchers
					this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_AGENT_UPDATE, (UUID *)&iA->first, (void *)&infoFlags, sizeof(int) );
				}
				
				// pack OAC_DDB_AGENTSETINFO message
				lds.reset();
				lds.packUUID( this->getUUID() );
				lds.packUUID( (UUID *)&iA->first );
				lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS ); // infoFlags
				lds.packUUID( &nilUUID );
				lds.packUUID( &nilUUID );
				lds.packInt32( DDBAGENT_STATUS_FAILED );
				lds.packUUID( &nilUUID );
				this->globalStateChangeForward( OAC_DDB_AGENTSETINFO, lds.stream(), lds.length() ); // forward to mirrors and sponsees
				lds.unlock();

				this->AgentTransferInfoChanged( (UUID *)&iA->first, infoFlags );

				break;
			}
		}
	}

	if ( oldMembers->size() && *this->getUUID() != oldMembers->front() && *this->getUUID() == this->gmMemberList.front() ) { // wasn't the leader, but now we are
		// spawn all uniques
		this->startUniques();
	}

	// start PA session
	if ( !this->gmMemberList.empty() )
		this->cbbaPAStart();

	return 0;
}

int AgentHost::hostConCleanDuplicate( spConnection con ) {
	DataStream lds;

	Log.log( LOG_LEVEL_VERBOSE, "AgentHost::hostConCleanDuplicate: clean duplicate connection with %s", Log.formatUUID( LOG_LEVEL_VERBOSE, &con->uuid ) );

	this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_CLOSE_DUPLICATE) );

	this->addTimeout( 11000, AgentHost_CBR_cbHostConCleanDuplicate, &con->index, sizeof(UUID) );

	return 0;
}



int AgentHost::setHostKnownStatus( AgentHost::State *hState, int status ) {
	int time;

	if ( hState->statusTimeout != nilUUID ) {
		this->removeTimeout( &hState->statusTimeout );
		hState->statusTimeout = nilUUID;
	}

	hState->status = status;
	hState->statusActivity = 0;

	Log.log( 4, "AgentHost::setHostKnownStatus: host %s, status %d", 
		Log.formatUUID(4,&((AgentBase::State *)hState)->uuid), hState->status );

	switch ( status ) {
	case STATUS_ACTIVE:
		time = 1000*60;
		break;
	case STATUS_ALIVE:
		time = 1000*60*3; // note this should be at least a couple times larger than the time for STATUS_ACTIVE
		break;
	case STATUS_QUERY:
		this->sendMessage( hState->connection, MSG_RACK );
		time = 1000*20;
		break;
	case STATUS_GLOBAL_QUERY:
		{
			mapAgentHostState::iterator iterHS;
			mapConnectionStatistics::iterator iterCS;
			this->ds.reset();
			this->ds.packUUID( &((AgentBase::State *)hState)->uuid );
			for ( iterHS = this->hostKnown.begin(); iterHS != this->hostKnown.end(); iterHS++ ) {
				if ( iterHS->second->connection == NULL )
					continue; // we don't have a connection to this host
				iterCS = iterHS->second->hostStats->find( ((AgentBase::State *)hState)->uuid );
				if ( iterCS != iterHS->second->hostStats->end() ) { // this host has a potential connection
					if ( iterCS->second->connected ) { // this host is connected
						this->sendMessageEx( iterHS->second->connection, MSGEX(AgentHost_MSGS,MSG_ROTHER_STATUS), this->ds.stream(), this->ds.length() );
					}					
				}
			}
			this->ds.unlock();
		}
		time = 1000*20;
		break;
	case STATUS_DEAD:
		time = 1000*60*5;
		break;
	case STATUS_SHUTDOWN:
		time = 1000*60*5;
		break;
	default:
		Log.log( 0, "AgentHost::setHostKnownStatus: unknown status %d", status );
		return 1;
	};

	this->addTimeout( time, AgentHost_CBR_cbHostStatusTimeout, &((AgentBase::State *)hState)->uuid, sizeof(UUID) );

	return 0;
}

int AgentHost::recvHostIntroduce( spConnection con, UUID *uuid ) {
	AgentHost::State *hState;
	DataStream lds;
	RPC_STATUS Status;

	Log.log( 0, "AgentHost::recvHostIntroduce: introduction from %s", Log.formatUUID( 0, uuid ) );

	// label connection
	con->uuid = *uuid;

	mapAgentHostState::iterator iterHS;
	iterHS = this->hostKnown.find( *uuid );
	if ( iterHS == this->hostKnown.end() ) { // we don't know about this host yet
		hState = (AgentHost::State *)AgentHost_CreateState( uuid, sizeof(AgentHost::State) );
		if ( !hState ) 
			return 1;

		memset( hState, 0, sizeof(AgentHost::State) );

		//this->setHostKnownStatus( hState, STATUS_ACTIVE );

		hState->connection = con;

		this->hostKnown[*uuid] = hState;
	}

	if ( apCmp( &con->ap, &STATE(AgentHost)->serverAP ) == 0 ) { // connection is from them to us, introduce ourselves
		lds.reset();
		lds.packUUID( &STATE(AgentBase)->uuid );
		this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_HOST_INTRODUCE), lds.stream(), lds.length() ); // introduce ourselves
		lds.unlock();	
	} else {
		this->hostKnown[*uuid]->serverAP = con->ap; // save address
	}

	// insert into connection list (if new)
	list<UUID>::iterator iHC;
	for ( iHC = this->gmConnection.begin(); iHC != this->gmConnection.end(); iHC++ ) {
		if ( *iHC == *uuid )
			break;
	}
	if ( iHC == this->gmConnection.end() )
		this->gmConnection.push_back( *uuid ); // we have a connection to them

	if ( strlen( this->hostKnown[*uuid]->serverAP.address ) != 0 ) { // we have an address for this host
		// check if we want to apply
		spAddressPort ap = this->gmApplyTo;
		spAddressPort lastAP = NULL;
		while ( ap != NULL ) {
			if ( apCmp( ap, &this->hostKnown[*uuid]->serverAP ) == 0 ) {
				// send apply message
				lds.reset();
				lds.packUUID( &STATE(AgentBase)->uuid );
				lds.packData( &STATE(AgentHost)->serverAP, sizeof(sAddressPort) );
				lds.packInt64( this->clusterID[0] );
				lds.packInt64( this->clusterID[1] );
				this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_GM_APPLY), lds.stream(), lds.length() ); // introduce ourselves
				lds.unlock();

				// remove from list
				if ( lastAP ) lastAP->next = ap->next;
				else this->gmApplyTo = ap->next;
				free( ap );

				break;
			}
			lastAP = ap;
			ap = ap->next;
		}

		// check if we want to introduce ourselves
		ap = this->gmIntroduceTo;
		lastAP = NULL;
		while ( ap != NULL ) {
			if ( apCmp( ap, &this->hostKnown[*uuid]->serverAP ) == 0 ) {
				// send introduction message
				lds.reset();
				lds.packUUID( &STATE(AgentBase)->uuid );
				lds.packUUID( &STATE(AgentBase)->uuid );
				lds.packData( &STATE(AgentHost)->serverAP, sizeof(sAddressPort) );
				this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_GM_INTRODUCE), lds.stream(), lds.length() ); // introduce ourselves
				lds.unlock();

				// remove from list
				if ( lastAP ) lastAP->next = ap->next;
				else this->gmIntroduceTo = ap->next;
				free( ap );

				break;
			}
			lastAP = ap;
			ap = ap->next;
		}
	}

	// check if we already have a connection to this host
	mapConnection::iterator iC;
	for ( iC = this->connection.begin(); iC != this->connection.end(); iC++ ) {
		if ( iC->second != con && iC->second->uuid == *uuid )
			break;
	}
	if ( iC != this->connection.end() ) { // already have one, start trying to clean one up
		int cmp = UuidCompare( this->getUUID(), uuid, &Status );

		if ( cmp < 0 ) { // we have lower id, so we in charge of getting rid of connections (try to keep one that is from us to them)
			int iCToUs = !apCmp( &iC->second->ap, &STATE(AgentHost)->serverAP );
			int conToUs = !apCmp( &con->ap, &STATE(AgentHost)->serverAP );


			if ( iCToUs && conToUs ) { // both connections are to us! potentially get rid of both
				// close con, it can't possibly be our active connection
				// if iC is our active connection then we're keeping it for now
				// if iC is not our active connection then we must already be trying to close it
				this->hostConCleanDuplicate( con );

			} else if ( iCToUs ) { // iC is to us (and con is from us)
				// close iC
				// if iC was not our active connection, close our active connection
				// replace our active connection with con
				if ( iC->second != this->hostKnown[*uuid]->connection )
					this->hostConCleanDuplicate( this->hostKnown[*uuid]->connection );
				this->hostConCleanDuplicate( iC->second );
				this->hostKnown[*uuid]->connection = con;

			} else if ( conToUs ) { // con is to us (and iC is from us)
				// close con, it can't possibly be our active connection
				// if iC is our active connection then we're keeping it for now
				// if iC is not our active connection then we must already be trying to close it
				this->hostConCleanDuplicate( con );

			} else { // both are from us
				// close con, it can't possibly be our active connection
				// if iC is our active connection then we're keeping it for now
				// if iC is not our active connection then we must already be trying to close it
				this->hostConCleanDuplicate( con );

			}
		}
/*
		if ( apCmp( &iC->second->ap, &STATE(AgentHost)->serverAP ) == 0 ) { // iC is from them to us
			usTothem = con;
			themTous = iC->second;
		} else { // con is from them to us
			usTothem = iC->second;
			themTous = con;
		}

		if ( cmp < 0 ) { // we have lower id, so try to get rid of themTous
			this->hostKnown[*uuid]->connection = usTothem;
			this->hostConCleanDuplicate( themTous );
		} else { // they have lower id, so try to get rid of usTothem
			this->hostKnown[*uuid]->connection = themTous;
			//this->hostConCleanDuplicate( usTothem ); // let them ask us
		} */
	} 


	// start failure detection
	this->initializeConnectionFailureDetection( con );

/*	
	mapAgentHostState::iterator iterHS;
	iterHS = this->hostKnown.find( *uuid );
	if ( iterHS == this->hostKnown.end() ) { // we don't know about this host yet
		hState = (AgentHost::State *)AgentHost_CreateState( uuid, sizeof(AgentHost::State) );
		if ( !hState ) 
			return 1;

		memset( hState, 0, sizeof(AgentHost::State) );

		this->setHostKnownStatus( hState, STATUS_ACTIVE );

		this->hostKnown[*uuid] = hState;

		this->ds.reset();
		this->ds.packUUID( uuid );
		
		if ( this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_RHOST_STUB), this->ds.stream(), this->ds.length() ) ) {
			this->ds.unlock();
			return 1;
		}
		
		this->ds.unlock();

	} else { // we already know about this host
		// TODO: maybe we would like to update our info on it?

		hState = iterHS->second;
	}

	if ( hState->connection == NULL ) { // we haven't registered a connection to this host yet
		if ( this->watchConnection( con, AgentHost_CBR_cbWatchHostConnection ) == -1 )
			return 1;
		con->uuid = *uuid;
		hState->connection = con;
		(*STATE(AgentHost)->hostStats)[*uuid] = &con->statistics;

		this->setConnectionfailureDetectionQOS( con, FD_HOST_TDu, FD_HOST_TMRL, FD_HOST_TMU );
		
		this->groupMergeSend( con ); // merge our groups now
	}
*/
	return 0;
}


int AgentHost::recvHostShutdown( spConnection con ) {
	RPC_STATUS Status;

	if ( !UuidIsNil( &con->uuid, &Status ) ) {
		this->hostKnown[con->uuid]->connection = NULL;
		STATE(AgentHost)->hostStats->erase( con->uuid );

		//this->setHostKnownStatus( this->hostKnown[con->uuid], STATUS_SHUTDOWN );

		UuidCreateNil( &con->uuid );
	}

	return 0;
}

int AgentHost::sendHostLabelAll( spConnection con ) {
	RPC_STATUS Status;

	// send our label
	if ( this->sendHostLabel( con, &STATE(AgentBase)->uuid ) )
		return 1;

	// send all known host labels
	mapAgentHostState::iterator iterHS;
	for ( iterHS = this->hostKnown.begin(); iterHS != this->hostKnown.end(); iterHS++ ) {
		if ( UuidCompare( &con->uuid, (UUID *)&iterHS->first, &Status ) )
			this->sendHostLabel( con, (UUID *)&iterHS->first );
	}

	return 0;
}

int AgentHost::sendHostLabel( spConnection con, UUID *which ) {
	
	this->ds.reset();

	this->ds.packUUID( which );

	if ( *which == STATE(AgentBase)->uuid ) { // this is our data, which is always up to date
		this->ds.packUInt32( 0 ); // stub age is 0 ms
		this->ds.packUInt32( 0 ); // state age is 0 ms
	} else {
		mapAgentHostState::iterator iter;
		_timeb *lastStub, *lastState, curTime;

		iter = this->hostKnown.find( *which );
		if ( iter == this->hostKnown.end() ) {
			Log.log( 0, "AgentHost::sendHostLabel: couldn't find UUID" );
			return 1;
		}
		lastStub = &((AgentBase::State *)iter->second)->lastStub;
		lastState = &((AgentBase::State *)iter->second)->lastState;
		apb->apb_ftime_s( &curTime );

		this->ds.packUInt32( (unsigned int)((curTime.time - lastStub->time)*1000 + (curTime.millitm - lastStub->millitm)) );
		this->ds.packUInt32( (unsigned int)((curTime.time - lastState->time)*1000 + (curTime.millitm - lastState->millitm)) );
	}

	int ret = this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_HOST_LABEL), this->ds.stream(), this->ds.length() );
	
	this->ds.unlock();
	
	return ret;
}

int AgentHost::recvHostLabel( spConnection con, DataStream *ds ) {
	UUID uuid;
	unsigned int stubAge, stateAge;

	ds->unpackUUID( &uuid );
	stubAge = ds->unpackUInt32() + con->statistics.latency;
	stateAge = ds->unpackUInt32() + con->statistics.latency;

	if ( uuid == STATE(AgentBase)->uuid ) { // this is our uuid, ignore
		return 0;
	}

	mapAgentHostState::iterator iter;

	iter = this->hostKnown.find( uuid );
	if ( iter == this->hostKnown.end() ) { // we don't know about this host yet
		AgentHost::State *hState = (AgentHost::State *)AgentHost_CreateState( &uuid, sizeof(AgentHost::State) );
		if ( !hState ) 
			return 1;

		memset( hState, 0, sizeof(AgentHost::State) );
		
		//this->setHostKnownStatus( hState, STATUS_ALIVE );

		this->hostKnown[uuid] = hState;

		this->ds.reset();
		this->ds.packUUID( &uuid );
		
		int ret = this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_RHOST_STUB), this->ds.stream(), this->ds.length() );

		this->ds.unlock();

		return ret;

	} else { // we already know about this host
		// TODO: maybe we would like to update our info on it?
	}
	
	return 0;
}

int AgentHost::sendHostStub( spConnection con, UUID *which ) {
	AgentHost::State *hState;

	this->ds.reset();

	this->ds.packUUID( which );

	if ( *which == STATE(AgentBase)->uuid ) { // this is our data, which is always up to date
		this->ds.packUInt32( 0 ); // stub age is 0 ms
		hState = (AgentHost::State *)this->state;
	} else {
		mapAgentHostState::iterator iter;
		_timeb *lastStub, curTime;

		iter = this->hostKnown.find( *which );
		if ( iter == this->hostKnown.end() ) {
			Log.log( 0, "AgentHost::sendHostLabel: couldn't find UUID" );
			this->ds.unlock();
			return 1;
		}
		lastStub = &((AgentBase::State *)iter->second)->lastStub;
		apb->apb_ftime_s( &curTime );

		this->ds.packUInt32( (unsigned int)((curTime.time - lastStub->time)*1000 + (curTime.millitm - lastStub->millitm)) );

		hState = iter->second;
	}

	if ( AgentHost_writeStub( hState, &this->ds ) ) {
		this->ds.unlock();
		return 1;
	}

	int ret = this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_HOST_STUB), this->ds.stream(), this->ds.length() );

	this->ds.unlock();

	return ret;
}

int AgentHost::recvHostStub( spConnection con, DataStream *ds ) {
	UUID uuid;
	unsigned int stubAge;
	AgentHost::State *hState;

	ds->unpackUUID( &uuid );
	stubAge = ds->unpackUInt32() + con->statistics.latency;

	if ( uuid == STATE(AgentBase)->uuid ) { // this is our uuid, ignore
		return 0;
	}

	mapAgentHostState::iterator iterHS;
	iterHS = this->hostKnown.find( uuid );
	if ( iterHS == this->hostKnown.end() ) { // we don't know about this host yet
		hState = (AgentHost::State *)AgentHost_CreateState( &uuid );
		if ( !hState ) 
			return 1;
		
		//this->setHostKnownStatus( hState, STATUS_ALIVE );
		this->hostKnown[uuid] = hState;
	} else { // we already know about this host
		hState = iterHS->second;
	}

	// TODO only update data if stubAge is younger?!
	if ( AgentHost_readStub( hState, ds, stubAge ) ) {
		AgentHost_DeleteState( (AgentBase::State *)hState );
		this->hostKnown.erase( uuid );
		return 1;
	}

	if ( hState->connection == NULL ) { // we haven't got a connection to this host yet
		spConnection hCon;

		if ( !strncmp( "-1", hState->serverAP.address, 2 ) ) { // -1 means local host, can only happen during local testing, otherwise this host could not be part of the cluster
			sprintf_s( hState->serverAP.address, sizeof(hState->serverAP.address), "127.0.0.1" );
		}

		// get a connection to this AP if it already exists, otherwise create a new one
		if ( (hCon = this->getConnection( &hState->serverAP )) == NULL ) {
			hCon = this->openConnection( &hState->serverAP, NULL, 60 );
			if ( !hCon ) {
				AgentHost_DeleteState( (AgentBase::State *)hState );
				this->hostKnown.erase( uuid );
				return 1;
			}
		} else if ( hCon->state == CON_STATE_CONNECTED ) { // introduce ourselves if we're already connected
			this->ds.reset();
			this->ds.packUUID( &STATE(AgentBase)->uuid );
			this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_HOST_INTRODUCE), this->ds.stream(), this->ds.length() ); // introduce ourselves
			this->ds.unlock();
			this->groupMergeSend( con ); // merge our groups now
		}
		if ( this->watchConnection( hCon, AgentHost_CBR_cbWatchHostConnection ) == -1 )
			return 1;
		hCon->uuid = uuid;
		hState->connection = hCon;
		(*STATE(AgentHost)->hostStats)[uuid] = &hCon->statistics;

		//this->setHostKnownStatus( hState, STATUS_ACTIVE );
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Agent Library

int AgentHost::loadLibrary() {
	int i, j, ind, count;
	WIN32_FIND_DATA ffd;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	DWORD dwError=0;
	WCHAR Wbuf[MAX_PATH];

	FILE *iniF = NULL;
	char buf[MAX_PATH];
	sAgentTemplate AT;

	wsprintf( Wbuf, _T("%hs*"), this->libraryPath );
	hFind = FindFirstFile( Wbuf, &ffd);
	
	if ( hFind == INVALID_HANDLE_VALUE ) {
		dwError = GetLastError();
		if ( dwError != ERROR_FILE_NOT_FOUND ) {
			return 1; // some error!
		}
		return 0; // no folders
	}

	do {
		if ( ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) { // found a directory, see if it's an agent template
			if ( ffd.cFileName[0] == '.' ) // skip . and .. directories
				continue;
			sprintf_s( buf, MAX_PATH, "%s%ls\\%ls.ini", this->libraryPath, ffd.cFileName, ffd.cFileName );
			
			if ( fopen_s( &iniF, buf, "r" ) ) // no ini file exists, guess this isn't an agent template
				continue;

			sprintf_s( AT.name, sizeof(AT.name), "%ls", ffd.cFileName );
			
			// read version
			if ( 4 != fscanf_s( iniF, "version=%d.%d.%d.%d\n", &AT.verMajor, &AT.verMinor, &AT.verBuildNo, &AT.verExtend ) ) {
				Log.log( 0, "Badly formated template file (version): %ls.ini\n", ffd.cFileName );
				continue;
			}
			// read uuid
			if ( 1 != fscanf_s( iniF, "uuid=%s\n", buf, MAX_PATH ) ) {
				Log.log( 0, "Badly formated template file (uuid): %ls.ini\n", ffd.cFileName );
				continue;
			}
			if ( strlen(buf) != 36 ) {
				Log.log( 0, "Badly formated template file (uuid): %ls.ini\n", ffd.cFileName );
				continue;
			}
			wsprintf( Wbuf, _T("%hs"), buf );
			UuidFromString( (RPC_WSTR)Wbuf, &AT.uuid );
			// read type
			if ( 1 != fscanf_s( iniF, "type=%s\n", buf, MAX_PATH ) ) {
				Log.log( 0, "Badly formated template file (type): %ls.ini\n", ffd.cFileName );
				continue;
			}
			if ( !strcmp( buf, "DLL" ) ) {
				AT.type = AGENTTYPE_DLL;
			} else if ( !strcmp( buf, "EXE" ) ) {
				AT.type = AGENTTYPE_EXE;
			} else {
				Log.log( 0, "Badly formated template file (type): %ls.ini\n", ffd.cFileName );
				continue;
			}
			// read object
			if ( 1 != fscanf_s( iniF, "object=%s\n", AT.object, sizeof(AT.object) ) ) {
				Log.log( 0, "Badly formated template file (object): %ls.ini\n", ffd.cFileName );
				continue;
			}
			// read object
			if ( 1 != fscanf_s( iniF, "debugmode=%s\n", AT.debugmode, sizeof(AT.debugmode) ) ) {
				Log.log( 0, "Badly formated template file (debugmode): %ls.ini\n", ffd.cFileName );
				continue;
			}
			// read process cost
			if ( 1 != fscanf_s( iniF, "processcost=%f\n", &AT.processCost ) ) {
				Log.log( 0, "Badly formated template file (processcost): %ls.ini\n", ffd.cFileName );
				continue;
			}
			// read transfer penalty
			if ( 1 != fscanf_s( iniF, "transferpenalty=%f\n", &AT.transferPenalty ) ) {
				Log.log( 0, "Badly formated template file (transferpenalty): %ls.ini\n", ffd.cFileName );
				continue;
			}
			// read resource requirements
			if ( 1 != fscanf_s( iniF, "resourcerequirements=%s\n", buf, MAX_PATH ) ) {
				Log.log( 0, "Badly formated template file (resourcerequirements): %ls.ini\n", ffd.cFileName );
				continue;
			}
			count = (int)strlen(buf);
			ind = 0;
			i = j = 1;
			while ( ind < AGENTTEMPLATE_MAX_RESOURCE_REQUIREMENTS ) {
				while ( j < count ) {
					if ( buf[j] == ',' || buf[j] == '"' ) {
						buf[j] = '\0';
						break;
					}
					j++;
				}
				if ( j - i == 36 ) {
					wsprintf( Wbuf, _T("%hs"), buf + i );
					UuidFromString( (RPC_WSTR)Wbuf, &AT.resourceRequirements[ind] );
					ind++;
				} else {
					break;
				}
				i = j + 1;
			}
			if ( ind < AGENTTEMPLATE_MAX_RESOURCE_REQUIREMENTS )
				UuidCreateNil( &AT.resourceRequirements[ind] );

			fclose( iniF );

			this->addAgentTemplate( &AT );
		}
	} while (FindNextFile(hFind, &ffd) != 0);

	
	dwError = GetLastError();
	if ( dwError != ERROR_NO_MORE_FILES ) {
		return 1; // some error!
	}

	FindClose( hFind );

	return 0;
}

int AgentHost::addAgentTemplate( spAgentTemplate AT ) {
	mapAgentTemplate::iterator iterAT;
	iterAT = this->agentLibrary.find( AT->uuid );
	if ( iterAT != this->agentLibrary.end() ) { // we already have an agent by this UUID!
		Log.log( 0, "AgentHost::addAgentTemplate: AgentTemplate already exists (%s)\n", Log.formatUUID( 0, &AT->uuid ) );
		return 1;
	}
	
	spAgentTemplate newAT;
	newAT = (spAgentTemplate)malloc(sizeof(sAgentTemplate));
	if ( !newAT ) {
		Log.log( 0, "AgentHost::addAgentTemplate: Failed to malloc AgentTemplate\n" );
		return 1;
	}

	memcpy( newAT, AT, sizeof(sAgentTemplate) );

	this->agentLibrary[AT->uuid] = newAT;

	(*STATE(AgentHost)->agentTemplateInstances)[AT->uuid] = -1;

	Log.log( 3, "AgentHost::addAgentTemplate: Adding agent %s, %.2d.%.2d.%.5d.%.2d, %s\n", AT->name, AT->verMajor, AT->verMinor, AT->verBuildNo, AT->verExtend, Log.formatUUID( 3, &AT->uuid ) );

	return 0;
}


//-----------------------------------------------------------------------------
// Agent Management
/*
int AgentHost::requestAgentSpawnProposals( spConnection con, UUID *thread, UUID *ticket, AgentType *type, DataStream *dsState ) {
	
	// initialize proposal evaluator
	spAgentSpawnProposalEvaluator aspe = (spAgentSpawnProposalEvaluator)malloc( sizeof(sAgentSpawnProposalEvaluator) );
	if ( !aspe ) {
		Log.log( 0, "AgentHost::requestAgentSpawnProposals: Failed to malloc AgentSpawnProposalEvaluator\n" );
		return 1;
	}

	aspe->ticket = *ticket;
	aspe->con = con;
	aspe->thread = thread;
	aspe->type.uuid = type->uuid;
	aspe->type.instance = type->instance;
	aspe->openRFPs = 0;
	aspe->proposals = new list<spAgentSpawnProposal>();
	aspe->accepting = (spConnection)-1; // not accepting
	aspe->requestTimeout = NULL;
	aspe->evaluateTimeout = NULL;
	aspe->acceptTimeout = NULL;

	this->agentSPE[*ticket] = aspe;

	// set timeout
	aspe->requestTimeout = this->addTimeout( TIMEOUT_REQUESTAGENTSPAWNPROPOSALS, NEW_MEMBER_CB(AgentHost,cbRequestAgentSpawnProposalsExpired), ticket, sizeof(UUID) );
	
	// send out RFPs to appropriate hosts
	mapAgentHostState::iterator iterHS;
	this->ds.reset();
	this->ds.packUUID( ticket );
	this->ds.packUUID( &type->uuid );
	this->ds.packChar( type->instance );
	for ( iterHS = this->hostKnown.begin(); iterHS != this->hostKnown.end(); iterHS++ ) {
		if ( iterHS->second->connection == NULL )
			continue; // we don't have a connection to this host
				
		if ( !this->sendMessageEx( iterHS->second->connection, MSGEX(AgentHost_MSGS,MSG_RAGENTSPAWNPROPOSAL), this->ds.stream(), this->ds.length() ) )
			aspe->openRFPs++;
	}

	this->ds.unlock();

	aspe->openRFPs++;
	if ( this->generateAgentSpawnProposal( NULL, ticket, type ) ) // generate a proposal of our own
		aspe->openRFPs--;


	return 0;
}

int AgentHost::generateAgentSpawnProposal( spConnection con, UUID *ticket,  AgentType *type  ) {
	float favourable = 0;
	bool haveTemplate = false;
	bool decline = true;

	// evaluate requirements
	mapAgentTemplate::iterator iterAT;
	iterAT = this->agentLibrary.find( type->uuid );
	if ( iterAT != this->agentLibrary.end() ) { 
		haveTemplate = true;
	} else {// we don't have an agent template by this UUID
		// TODO: consider downloading the template?
	}

	// generate proposal
	// TEMP
	if ( haveTemplate ) {
		favourable = 0.2f + 0.8f*(float)apb->apbUniform01();
		decline = false;
	} else {
		favourable = 0;
	}
	
	if ( !decline ) {
		// add to open proposal list
		spAgentSpawnRequest asr = NewAgentSpawnRequest( con, ticket, type );
		this->openASR[*ticket] = asr;

		// add timeout to expire proposal
		asr->timeout = this->addTimeout( TIMEOUT_AGENTSPAWNPROPOSAL, NEW_MEMBER_CB(AgentHost,cbGenerateAgentSpawnProposalExpired), ticket, sizeof(UUID) );
	}

	// send proposal
	if ( con ) { // remote response
		this->ds.reset();
		this->ds.packUUID( ticket );
		this->ds.packFloat32( favourable );

		if ( !this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_AGENTSPAWNPROPOSAL), this->ds.stream(), this->ds.length() ) ) {
			this->ds.unlock();
			return 1;
		}
		
		this->ds.unlock();

	} else { // local response
		spAgentSpawnProposal asp = NewAgentSpawnProposal( NULL, favourable );
		if ( !asp )
			return 1;
		this->evaluateAgentSpawnProposal( ticket, asp );
	}

	return 0;
}

int AgentHost::evaluateAgentSpawnProposal( UUID *ticket, spAgentSpawnProposal asp ) {

	mapAgentSpawnProposalEvaluator::iterator iterASPE = this->agentSPE.find( *ticket );
	if ( iterASPE == this->agentSPE.end() ) // evaluator not found, hopefully because RFP has already ended
		return 0;

	spAgentSpawnProposalEvaluator aspe = iterASPE->second;
	
	// increment response counter
	aspe->openRFPs--;

	// evaluate proposal
	// TODO


	// add to proposal list if request is >= boarderline
	if ( asp->favourable > 0.2 ) {
		list<spAgentSpawnProposal>::iterator iterASP = aspe->proposals->begin();
		while ( iterASP != aspe->proposals->end() ) {
			if ( asp->favourable > (*iterASP)->favourable )
				break;
			iterASP++;
		}
		aspe->proposals->insert( iterASP, asp );
	}

	// if not already accepting proposal
	if ( aspe->accepting == (spConnection)-1 ) {
		// if proposal is ideal or there are no more incoming proposals then accept
		if ( aspe->openRFPs == 0 || asp->favourable > 0.9 ) {
			if ( aspe->evaluateTimeout ) {
				this->removeTimeout( aspe->evaluateTimeout );
				aspe->evaluateTimeout = NULL;
			}
			this->acceptAgentSpawnProposal( aspe );
		}
		// if proposal is good then wait TIMEOUT_EVALUATEAGENTSPAWNPROPOSAL_GOOD ms for more proposals before accepting
		else if ( asp->favourable > 0.6 ) {
			if ( !aspe->evaluateTimeout ) {
				aspe->evaluateTimeout = this->addTimeout( TIMEOUT_EVALUATEAGENTSPAWNPROPOSAL_GOOD, NEW_MEMBER_CB(AgentHost,cbEvaluateAgentSpawnProposalExpired), &aspe->ticket, sizeof(UUID) );
			} else if ( this->readTimeout( aspe->evaluateTimeout ) > TIMEOUT_EVALUATEAGENTSPAWNPROPOSAL_GOOD ) {
				this->removeTimeout( aspe->evaluateTimeout );
				aspe->evaluateTimeout = this->addTimeout( TIMEOUT_EVALUATEAGENTSPAWNPROPOSAL_GOOD, NEW_MEMBER_CB(AgentHost,cbEvaluateAgentSpawnProposalExpired), &aspe->ticket, sizeof(UUID) );
			}
		}
		// if proposal is acceptable then wait TIMEOUT_EVALUATEAGENTSPAWNPROPOSAL_ACCEPTABLE ms for more proposals before accepting
		else if ( asp->favourable > 0.4 ) {
			if ( !aspe->evaluateTimeout ) {
				aspe->evaluateTimeout = this->addTimeout( TIMEOUT_EVALUATEAGENTSPAWNPROPOSAL_ACCEPTABLE, NEW_MEMBER_CB(AgentHost,cbEvaluateAgentSpawnProposalExpired), &aspe->ticket, sizeof(UUID) );
			}
		}
	}

	return 0;
}

int AgentHost::acceptAgentSpawnProposal( spAgentSpawnProposalEvaluator aspe ) {

	// make sure we have at least one proposal
	if ( !aspe->proposals->size() ) { // no proposals
		if ( !aspe->openRFPs || !aspe->requestTimeout ) { // no chance of getting more proposals
			this->finishAgentSpawnProposal( aspe, NULL );
		}
		return 0;
	}

	// initiate spawn based on top proposal
	spAgentSpawnProposal asp = aspe->proposals->front();
	aspe->proposals->pop_front();

	if ( asp->con == NULL ) { // local host
		if ( this->spawnAgent( &aspe->ticket, &aspe->type, NULL ) ) {
			free( asp );
			this->acceptAgentSpawnProposal( aspe );
			return 0;
		}
		aspe->accepting = NULL; // local host
	} else {
		this->ds.reset();
		this->ds.packUUID( &aspe->ticket );
		if ( this->sendMessageEx( asp->con, MSGEX(AgentHost_MSGS,MSG_ACCEPTAGENTSPAWNPROPOSAL), this->ds.stream(), this->ds.length() ) ) { 
			this->ds.unlock();
			// send failed
			free( asp );
			this->acceptAgentSpawnProposal( aspe );
			return 0;
		}
		this->ds.unlock();
		aspe->accepting = asp->con;
	}

	free( asp );

	// add timeout for spawn to complete
	aspe->acceptTimeout = this->addTimeout( TIMEOUT_ACCEPTAGENTSPAWNPROPOSAL, NEW_MEMBER_CB(AgentHost,cbAcceptAgentSpawnProposalExpired), &aspe->ticket, sizeof(UUID) );

	return 0;
}

int AgentHost::finishAgentSpawnProposal( spAgentSpawnProposalEvaluator aspe, UUID *agentId ) {

	// remove timeouts
	if ( aspe->requestTimeout ) 
		this->removeTimeout( aspe->requestTimeout );
	if ( aspe->acceptTimeout )
		this->removeTimeout( aspe->acceptTimeout );

	// notify initiator of results
	if ( aspe->con == NULL ) { // we were the initiator
		this->spawnFinished( aspe->accepting, &aspe->ticket, &aspe->type, agentId );
	} else {
		if ( agentId ) { // spawn succeeded
			this->ds.reset();
			this->ds.packInt32( aspe->thread );
			this->ds.packBool( true );
			this->ds.packUUID( agentId );
			this->sendMessage( aspe->con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
			this->ds.unlock();
		} else {
			this->ds.reset();
			this->ds.packInt32( aspe->thread );
			this->ds.packBool( false );
			this->sendMessage( aspe->con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
			this->ds.unlock();
		}
	}

	delete aspe->proposals;

	// free proposal evaluator
	this->agentSPE.erase( aspe->ticket );
	free( aspe );

	return 0;
}*/

int AgentHost::recvRequestAgentSpawn( spConnection con, DataStream *ds ) {
	UUID parent;
	AgentType type;
	float affinity;
	char priority;
	UUID thread;

	ds->unpackUUID( &parent );
	ds->unpackUUID( &type.uuid );
	type.instance = ds->unpackChar();
	affinity = ds->unpackFloat32();
	priority = ds->unpackChar();
	ds->unpackUUID( &thread );

	this->newAgent( &type, &parent, affinity, priority, &thread );

	return 0;
}
/*
int AgentHost::recvAgentSpawnProposal( spConnection con, DataStream *ds ) {
	UUID ticket;
	float favourable;
	ds->unpackUUID( &ticket );
	favourable = ds->unpackFloat32();

	spAgentSpawnProposal asp = NewAgentSpawnProposal( con, favourable );
	if ( !asp )
		return 1;

	this->evaluateAgentSpawnProposal( &ticket, asp );

	return 0;
}

int AgentHost::recvAcceptAgentSpawnProposal( spConnection con, DataStream *ds ) {
	UUID ticket;
	ds->unpackUUID( &ticket );
	
	mapAgentSpawnRequest::iterator iterASR = this->openASR.find( ticket );
	if ( iterASR == this->openASR.end() ) { // request not found
		this->ds.reset();
		this->ds.packUUID( &ticket );
		this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_AGENTSPAWNFAILED), this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		return 0;
	}

	spAgentSpawnRequest asr = iterASR->second;

	this->removeTimeout( asr->timeout );

	if ( this->spawnAgent( &asr->ticket, &asr->type, NULL ) ) {
		this->ds.reset();
		this->ds.packUUID( &ticket );
		this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_AGENTSPAWNFAILED), this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		return 0;
	}

	asr->timeout = this->addTimeout( TIMEOUT_SPAWNAGENT, NEW_MEMBER_CB(AgentHost,cbSpawnAgentExpired), &asr->ticket, sizeof(UUID) );

	return 0;
}

int AgentHost::recvAgentSpawnSucceeded( spConnection con, DataStream *ds ) {
	UUID ticket;
	UUID agentId;
	ds->unpackUUID( &ticket );
	ds->unpackUUID( &agentId );

	mapAgentSpawnProposalEvaluator::iterator iterASPE = this->agentSPE.find( ticket );
	if ( iterASPE == this->agentSPE.end() ) { // evaluator not found, probably because RFP has already ended
		if ( con == (spConnection)-1 ) // local host 
			return 1;
		this->ds.reset();
		this->ds.packUUID( &agentId );
		this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_AGENT_KILL), this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		return 0;
	}

	spAgentSpawnProposalEvaluator aspe = iterASPE->second;
	
	if ( aspe->accepting != con ) { // this was not the host we currently wanted to spawn the agent
		if ( con == (spConnection)-1 ) // local host 
			return 1;
		this->ds.reset();
		this->ds.packUUID( &agentId );
		this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_AGENT_KILL), this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		return 0;
	}

	this->finishAgentSpawnProposal( aspe, &agentId );

	return 0;
}

int AgentHost::recvAgentSpawnFailed( spConnection con, DataStream *ds ) {
	UUID ticket;
	ds->unpackUUID( &ticket );

	mapAgentSpawnProposalEvaluator::iterator iterASPE = this->agentSPE.find( ticket );
	if ( iterASPE == this->agentSPE.end() ) // evaluator not found, hopefully because RFP has already ended
		return 0;

	spAgentSpawnProposalEvaluator aspe = iterASPE->second;

	if ( aspe->accepting != con ) { // we aren't trying to accept a spawn from this host, must be expired so ignore
		return 0;
	}

	if ( aspe->acceptTimeout ) {
		this->removeTimeout( aspe->acceptTimeout );
		aspe->acceptTimeout = NULL;
	}

	this->acceptAgentSpawnProposal( aspe );

	return 0;
}*/
/*
int AgentHost::loadUniques() {
	char nameBuf[1024], *writeHead;
	WCHAR uuidBuf[64];
	AgentType type;
	FILE *uniF; 


	if ( fopen_s( &uniF, "StartupAgents.ini", "r" ) ) {
		Log.log( 6, "AgentHost::loadUniques: StartupAgents.ini not found, no uniques loaded" );
		return 0;
	}

	while ( 1 ) {
		type.instance = -1;

		writeHead = nameBuf;
		while ( EOF != (*writeHead = fgetc(uniF)) 
				&& '=' != *writeHead 
				&& '\n' != *writeHead
				&& ';' != *writeHead
				&& '/' != *writeHead ) 
			writeHead++;
		if ( *writeHead == ';' ) {
			while ( EOF != (*writeHead = fgetc(uniF)) 
					&& '\n' != *writeHead ) 
				writeHead++;
		}
		if ( *writeHead == EOF )
			break;
		if ( *writeHead == '\n' )
			continue;
		if ( *writeHead == '/' ) { // scan instance num
			int instanceInt;
			fscanf_s( uniF, "%d=", &instanceInt );
			type.instance = (char)instanceInt;
		}

		*writeHead = '\0';
		
		*uuidBuf = _T('\0');
		fscanf_s( uniF, "%ls", uuidBuf, 64 );
		if ( UuidFromString( (RPC_WSTR)uuidBuf, &type.uuid ) != RPC_S_OK ) {
			Log.log( 0, "AgentHost::loadUniques: bad uuid for %s", nameBuf );
		}
		this->uniqueNeeded.push_back( type );
	}

	fclose( uniF );

	return 0;
} */


int AgentHost::startUniques() {
	UUID thread;
	list<AgentType>::iterator iterUni;

	Log.log( LOG_LEVEL_VERBOSE, "AgentHost::startUniques: starting uniques (%d)", (int)this->uniqueNeeded.size() );

	for ( iterUni = this->uniqueNeeded.begin(); iterUni != this->uniqueNeeded.end(); iterUni++ ) {

		thread = this->conversationInitiate( AgentHost_CBR_convRequestUniqueSpawn, REQUESTAGENTSPAWN_TIMEOUT, &(*iterUni), sizeof(AgentType) );
		if ( thread == nilUUID ) {
			return 1;
		}
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packUUID( &iterUni->uuid );
		this->ds.packChar( iterUni->instance );
		this->ds.packFloat32( 0 ); // affinity
		this->ds.packChar( DDBAGENT_PRIORITY_CRITICAL );
		this->ds.packUUID( &thread );
		this->conProcessMessage( NULL, MSG_RAGENT_SPAWN, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

	}

	this->uniqueNeeded.clear();

	return 0;
}

/*
int AgentHost::spawnUnique( AgentType *type ) {
	UUID ticket;

	apb->apbUuidCreate( &ticket );
	this->spawnTickets[ticket] = SPAWN_UNIQUE;

	return this->requestAgentSpawnProposals( NULL, 0, &ticket, type );
}*/

int AgentHost::newAgent( AgentType *type, UUID *parent, float affinity, char priority, UUID *spawnThread ) {
	DataStream lds;	
	UUID agent, nil;
	_timeb nilTB;

	// make sure we have a template
	mapAgentTemplate::iterator iAT;
	if ( (iAT = this->agentLibrary.find( type->uuid )) == this->agentLibrary.end() ) {
		Log.log( 0, "AgentHost::newAgent: unknown type %s", Log.formatUUID(0, &type->uuid) );
		return 1; // unknown type!
	}
	
	// set agent type name
	strcpy_s( type->name, sizeof(type->name), iAT->second->name );

	apb->apbUuidCreate( &agent );
	UuidCreateNil( &nil );
	nilTB.time = 0;

	Log.log( LOG_LEVEL_NORMAL, "AgentHost::newAgent: %s, type %s instance %d", Log.formatUUID(LOG_LEVEL_NORMAL,&agent), type->name, type->instance );

	if ( 1 ) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T("268f7000-de18-40c1-a57a-f1008e877977"), &breakId );

		if ( breakId == agent )
			int i=0;
	}

	// add to DDB
	this->ddbAddAgent( &agent, parent, type, spawnThread, affinity, priority, this->agentLibrary[type->uuid]->processCost, AM_NORMAL );

	return 0;
}

int AgentHost::spawnAgent( UUID *agentId, AgentType *type, DataStream *dsState, bool agentShell ) {
	DataStream lds;
	AgentInfo *ai;

	mapAgentTemplate::iterator iterAT;
	iterAT = this->agentLibrary.find( type->uuid );
	if ( iterAT == this->agentLibrary.end() ) { // we don't have a template for this agent
		Log.log( 0, "AgentHost::spawnAgent: missing agent template (%s)\n", Log.formatUUID( 0, &type->uuid ) );
		return 1;
	}

	spAgentTemplate AT = (spAgentTemplate)iterAT->second;
	switch ( AT->type ) {
	case AGENTTYPE_DLL:
		{
			mapAgentTemplateInstances::iterator iterATI;
			iterATI = STATE(AgentHost)->agentTemplateInstances->find( type->uuid );
			if ( iterATI == STATE(AgentHost)->agentTemplateInstances->end() ) { // we don't have an instance count for this agent
				Log.log( 0, "AgentHost::spawnAgent: missing agent template instances (%s)\n", Log.formatUUID( 0, &type->uuid ) );
				return 1;
			}

			short *ATI = (short*)&iterATI->second;
			HINSTANCE library = NULL;
			if ( *ATI == -1 ) { // we need to load this library first
				WCHAR baseDirectory[512];
				WCHAR buf[512];
				// set the current directory to the library path in case they need their own dlls
				GetCurrentDirectory( 512, baseDirectory );
				wsprintf( buf, _T(".\\%hs%hs\\"), this->libraryPath, AT->name );
				SetCurrentDirectory( buf );
				// load the library
				wsprintf( buf, _T("%hs"), AT->object );
				library = LoadLibrary( buf );
				// set the directory back
				SetCurrentDirectory( baseDirectory );
				if ( library == NULL ) {
					Log.log( 0, "AgentHost::spawnAgent: LoadLibrary failed for %ls (%s) Error %d\n", buf, Log.formatUUID( 0, &type->uuid ), GetLastError() );
					return 1;
				}
				*ATI = 0;
				AT->vp = library;
			} else {
				library = (HINSTANCE)AT->vp;
			}

			mapAgentInfo::iterator iAI = this->agentInfo.find(*agentId);
			if ( iAI == this->agentInfo.end() ) {
				Log.log( 0, "AgentHost::spawnAgent: agent info not found %s", Log.formatUUID( 0, agentId ) );
				return 1;
			} 
			ai = &iAI->second;

			if ( !agentShell ) { // this is a normal agent spawn
				_timeb spawnTime;
				
				apb->apb_ftime_s( &spawnTime );

				// update DDB
				lds.reset();
				lds.packInt32( DDBAGENTINFO_SPAWN_TIME );
				lds.packData( &spawnTime, sizeof(_timeb) );
				lds.rewind();
				this->ddbAgentSetInfo( agentId, &lds );
				lds.unlock();

			} else { // this is a shell
				ai->shellStatus = DDBAGENT_STATUS_SPAWNING;				
			}

			SpawnFuncPtr sfp;
			sfp = (SpawnFuncPtr)GetProcAddress( library, "Spawn" );

			if ( apb->apbHostSpawnAgent( sfp, &this->localAP, agentId, logLevel, logDirectory, PLAYBACKMODE_DEFAULT, NULL ) ) {
				Log.log( 0, "AgentHost::spawnAgent: failed to spawn agent (%s)\n", Log.formatUUID( 0, &type->uuid ) );
				return 1;
			}

			// start timeout
			ai->spawnTimeout = this->addTimeout( TIMEOUT_SPAWNAGENT, AgentHost_CBR_cbSpawnAgentExpired, agentId, sizeof(UUID) );

		}
		break;
	default:
		Log.log( 0, "AgentHost::spawnAgent: unknown agent type %d for template %s\n", AT->type, Log.formatUUID( 0, &type->uuid ) );
		return 1;
	};
	
	return 0;
}


int AgentHost::spawnFinished( spConnection con, UUID *agentId, int spawnType ) {
	DataStream lds;

/*	mapSpawnType::iterator iterST;
	iterST = this->spawnTickets.find( *ticket );
	if ( iterST == this->spawnTickets.end() ) { // we don't have an open ticket for this spawn
		Log.log( 0, "AgentHost::spawnFinished: no open ticket" );
		if ( agentId ) this->killAgent( agentId );
		return 1;
	}

	char spawnType = iterST->second;
	this->spawnTickets.erase( iterST );
*/

	mapAgentInfo::iterator iA = this->agentInfo.find( *agentId );
	if ( iA == this->agentInfo.end() ) { 
		return 1; // agent not found
	}

	switch ( spawnType ) {
	case SPAWN_UNIQUE:
		{

/*			list<AgentType>::iterator iterUN;
			for ( iterUN = this->uniqueNeeded.begin(); iterUN != this->uniqueNeeded.end(); iterUN++ ) {
				if ( iterUN->uuid == type->uuid && iterUN->instance == type->instance )
						break;
			}
			if ( iterUN == this->uniqueNeeded.end() ) { // we didn't need this unique agent!
				Log.log( 0, "AgentHost::spawnFinished: unique not needed!?" );
				this->killAgent( agentId );
				return 1;
			}

			this->_addUnique( &iA->second.type );

			// distribute
			this->ds.reset();
			this->ds.packData( &iA->second.type, sizeof(AgentType) );
			this->_distribute( AgentHost_MSGS::MSG_DISTRIBUTE_ADD_UNIQUE, this->ds.stream(), this->ds.length() );
			this->ds.unlock();
*/
			
			// start the agent
			lds.reset();
			lds.packString( STATE(AgentBase)->missionFile );
			this->sendAgentMessage( (UUID *)&iA->first, MSG_AGENT_START, lds.stream(), lds.length() );
			lds.unlock();

		/*	if ( *dStore->AgentGetHost((UUID *)&iA->first) == *this->getUUID() ) { // local host
				this->sendMessage( iA->second.con, MSG_AGENT_START );
			} else {
				this->sendMessage( con, MSG_AGENT_START, agentId );
			}*/
		}
		break;
	default:
		Log.log( 0, "AgentHost::spawnFinished: unknown spawn type %d", spawnType );
		//if ( agentId ) this->killAgent( agentId );
		return 1;
	};

	return 0;
}

/*
int AgentHost::spawnFinished( spConnection con, UUID *ticket, AgentType *type, UUID *agentId ) {
	mapSpawnType::iterator iterST;
	iterST = this->spawnTickets.find( *ticket );
	if ( iterST == this->spawnTickets.end() ) { // we don't have an open ticket for this spawn
		Log.log( 0, "AgentHost::spawnFinished: no open ticket" );
		if ( agentId ) this->killAgent( agentId );
		return 1;
	}

	char spawnType = iterST->second;
	this->spawnTickets.erase( iterST );

	switch ( spawnType ) {
	case SPAWN_UNIQUE:
		{
			if ( agentId ) { // spawn succeeded
				
				list<AgentType>::iterator iterUN;
				for ( iterUN = this->uniqueNeeded.begin(); iterUN != this->uniqueNeeded.end(); iterUN++ ) {
					if ( iterUN->uuid == type->uuid && iterUN->instance == type->instance )
							break;
				}
				if ( iterUN == this->uniqueNeeded.end() ) { // we didn't need this unique agent!
					Log.log( 0, "AgentHost::spawnFinished: unique not needed!?" );
					this->killAgent( agentId );
					return 1;
				}
	
				this->_addUnique( type );

				// distribute
				this->ds.reset();
				this->ds.packData( type, sizeof(AgentType) );
				this->_distribute( AgentHost_MSGS::MSG_DISTRIBUTE_ADD_UNIQUE, this->ds.stream(), this->ds.length() );
				this->ds.unlock();

				// start the agent
				if ( con == NULL ) { // local host
					mapAgentInfo::iterator iterAI = this->agentInfo.find(*agentId);
					if ( iterAI == this->agentInfo.end() ) { // don't have a connection
						Log.log( 0, "AgentHost::finishAgentSpawnProposal: no connection to agent!" );
						this->killAgent( agentId );
						return 1;
					}
					this->sendMessage( iterAI->second.con, MSG_AGENT_START );
				} else {
					this->sendMessage( con, MSG_AGENT_START, agentId );
				}
			} else { // spawn failed
				UUID ticket;
				apb->apbUuidCreate( &ticket );
				this->requestAgentSpawnProposals( NULL, 0, &ticket, type ); // try again
			}
		}
		break;
	default:
		Log.log( 0, "AgentHost::spawnFinished: unknown spawn type %d", type );
		if ( agentId ) this->killAgent( agentId );
		return 1;
	};

	return 0;
}
*/

int AgentHost::killAgent( UUID *agentId ) {
	list<UUID>::iterator iterId;
	mapAgentInfo::iterator iA;

	iA = this->agentInfo.find( *agentId );
	if ( iA == this->agentInfo.end() ) { // agent not found
		Log.log( 0, "AgentHost::killAgent: agent not found! (activeAgents)" );
		return 1;
	}
	
	Log.log( 0, "AgentHost::killAgent: killing agent %s", Log.formatUUID( 0, agentId ) );

	// if this is a mirror clean it up
	for ( iterId = this->DDBMirrors.begin(); iterId != this->DDBMirrors.end(); iterId++ ) {
		if ( *iterId == *agentId ) {
			this->ddbRemMirror( agentId );
			break;
		}
	}

	if ( iA->second.con ) {
		this->stopWatchingConnection( iA->second.con, iA->second.watcher );
	}

	this->ddbRemoveAgent( agentId );

	return 0;
}

int AgentHost::_killAgent( UUID *agentId ) {
	DataStream lds;

	mapAgentInfo::iterator iA = this->agentInfo.find( *agentId );
	if ( iA == this->agentInfo.end() ) {
		Log.log( 0, "AgentHost::_killAgent: agent not found %s", Log.formatUUID( 0, agentId ) );
		return 1;
	}

	Log.log( 0, "AgentHost::_killAgent: killing agent %s", Log.formatUUID( 0, agentId ) );
		
	// notify
	this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_REM, agentId );
	
	// handle mirrors/sponsees
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packUUID( agentId );
	this->globalStateChangeForward( OAC_DDB_REMAGENT, lds.stream(), lds.length() );
	lds.unlock();

	// clean up agent
	if ( *dStore->AgentGetHost( agentId ) == *this->getUUID() ) {
		int status = dStore->AgentGetStatus( agentId );

		switch ( status ) {
		case DDBAGENT_STATUS_SPAWNING:
			{
				if ( iA->second.con ) // tell the agent to stop if we already have a connection
					this->sendMessage( iA->second.con, MSG_AGENT_STOP );
				// otherwise the agent will automatically be stopped when it reports in

				// notify parent		
				lds.reset();
				lds.packUUID( &iA->second.spawnThread );
				lds.packBool( false ); // failed
				this->sendAgentMessage( dStore->AgentGetParent( agentId ), MSG_RESPONSE, lds.stream(), lds.length() );
				lds.unlock();
			}
			break;
		case DDBAGENT_STATUS_READY:
			this->sendMessage( iA->second.con, MSG_AGENT_STOP ); // stop the agent
			break;
		case DDBAGENT_STATUS_FREEZING:
			this->sendMessage( iA->second.con, MSG_AGENT_STOP ); // stop the agent
			break;
		case DDBAGENT_STATUS_THAWING:
			if ( iA->second.shellStatus == DDBAGENT_STATUS_SPAWNING ) {
				iA->second.shellStatus = DDBAGENT_STATUS_ABORT; // abort agent when it reports in
			} else if ( iA->second.shellStatus == DDBAGENT_STATUS_READY
				|| iA->second.shellStatus == DDBAGENT_STATUS_THAWING ) { // shell was active, kill shell
				this->sendMessage( iA->second.shellCon, MSG_AGENT_STOP );

				this->stopWatchingConnection( iA->second.shellCon, iA->second.shellWatcher );
				this->closeConnection( iA->second.shellCon );

				iA->second.shellStatus = DDBAGENT_STATUS_ERROR;
				iA->second.shellCon = NULL;
				iA->second.shellWatcher = 0;
			}
			break;
		case DDBAGENT_STATUS_RECOVERING:
			if ( iA->second.shellStatus == DDBAGENT_STATUS_SPAWNING ) {
				iA->second.shellStatus = DDBAGENT_STATUS_ABORT; // abort agent when it reports in
			} else if ( iA->second.shellStatus == DDBAGENT_STATUS_READY
				|| iA->second.shellStatus == DDBAGENT_STATUS_RECOVERING ) { // shell was active, kill shell
				this->sendMessage( iA->second.shellCon, MSG_AGENT_STOP );

				this->stopWatchingConnection( iA->second.shellCon, iA->second.shellWatcher );
				this->closeConnection( iA->second.shellCon );

				iA->second.shellStatus = DDBAGENT_STATUS_ERROR;
				iA->second.shellCon = NULL;
				iA->second.shellWatcher = 0;
			}
			break;
		default:
			Log.log( 0, "AgentHost::_killAgent: unexpected agent status %d", status );
		};
	}

	// clean up DDB
	dStore->RemoveAgent( agentId );

	// clean up agentInfo
	this->_remAgent2( agentId );

	// check graceful exit
	if ( STATE(AgentBase)->gracefulExit )
		this->gracefulExitUpdate( agentId );

	return 0;
}

int AgentHost::agentLost( UUID *agent, int reason ) {

	// distribute
	this->ds.reset();
	this->ds.packUUID( agent );
	this->ds.packInt32( reason );
	this->_distribute( AgentHost_MSGS::MSG_DISTRIBUTE_AGENT_LOST, this->ds.stream(), this->ds.length() );
	this->ds.unlock();	

	// apply locally
	this->_agentLost( agent, reason );

	// notify all agents so they can check their atomic messages
	mapAgentInfo::iterator iA;
	for ( iA = this->agentInfo.begin(); iA != this->agentInfo.end(); iA++ ) {
		this->sendAgentMessage( (UUID *)&iA->first, MSG_AGENT_LOST, (char *)agent, sizeof(UUID) );
	}

	return 0;
}

int AgentHost::_agentLost( UUID *agent, int reason ) {
	DataStream lds;
	mapAgentInfo::iterator iterAI;

	// gather relevant data
	bool wasHost = false;
	bool wasLocal = false;
	int status;

	mapAgentHostState::iterator iterAHS = this->hostKnown.find( *agent );
	if ( iterAHS != this->hostKnown.end() ) {
		wasHost = true;
	} else {
		// check if it is for an agent
		iterAI = this->agentInfo.find(*agent);
		if (  iterAI == this->agentInfo.end() ) { // agent not found!
			Log.log( 0, "AgentHost::agentLost: agent not found %s, lost reason %d", Log.formatUUID( 0, agent ), reason );
			return 1; // unknown agent
		} 

		if ( *dStore->AgentGetHost( agent ) == *this->getUUID() )
			wasLocal = true;
	
		status = dStore->AgentGetStatus( agent );
	}

	Log.log( LOG_LEVEL_NORMAL, "AgentHost::agentLost: %s, reason %d, wasHost %d, wasLocal %d, status %d", Log.formatUUID( LOG_LEVEL_NORMAL, agent ), reason, (int)wasHost, (int)wasLocal, status );

	// check groups
	mapGroupMembers::iterator iGrp;
	std::list<UUID>::iterator iMbr;
	for ( iGrp = this->groupMembers.begin(); iGrp != this->groupMembers.end(); iGrp++ ) {
		for ( iMbr = iGrp->second.byJoin.begin(); iMbr != iGrp->second.byJoin.end(); iMbr++ ) {
			if ( *iMbr == *agent ) {
				Log.log( LOG_LEVEL_VERBOSE, "AgentHost::agentLost: removing from group %s", Log.formatUUID( LOG_LEVEL_VERBOSE, (UUID *)&iGrp->first ) );
				this->groupLeave( iGrp->first, *agent );
				break;
			}
		}
	}

	// check atomic messages
	mapAtomicMessage::iterator iAM;
	std::list<UUID>::iterator iTrg;
	for ( iAM = this->atomicMsgs.begin(); iAM != this->atomicMsgs.end(); iAM++ ) {
		for ( iTrg = iAM->second.targets.begin(); iTrg != iAM->second.targets.end(); iTrg++ ) { // check if this agent was a target
			if ( *iTrg == *agent )
				break;
		}
		if ( iTrg != iAM->second.targets.end() ) { // they were
//			iAM->second.targetsSuspect = true;

			// suspect locally
			this->_atomicMessageSuspect( (UUID *)&iAM->first, agent );
		}
	}

	// check ordered atomic message queues
	std::map<UUID, std::list<AtomicMessage>, UUIDless>::iterator iQ;
	std::list<AtomicMessage>::iterator iMq;
	for ( iQ = this->atomicMsgsOrderedQueue.begin(); iQ != this->atomicMsgsOrderedQueue.end(); iQ++ ) {
		for ( iMq = iQ->second.begin(); iMq != iQ->second.end(); iMq++ ) {
			for ( iTrg = iMq->targets.begin(); iTrg != iMq->targets.end(); iTrg++ ) { // check if this agent was a target
				if ( *iTrg == *agent )
					break;
			}
			if ( iTrg != iMq->targets.end() ) { // they were
//				iMq->targetsSuspect = true;
			}
		}		
	}

	// check pfLocks
/*	mapLock::iterator iPFL;
	for ( iPFL = this->PFLocks.begin(); iPFL != this->PFLocks.end(); iPFL++ ) {
		if ( this->PFLockingHost[iPFL->first] == *agent ) { // if resampling host is suspected abort L
			this->_ddbResampleParticleFilter_AbortLock( (UUID *)&iPFL->first, &iPFL->second.key );
		} else if ( this->PFLockingHost[iPFL->first] == *this->getUUID() && wasHost ) { // if we are the locking host and other host is suspected treat them as locked
			Log.log( LOG_LEVEL_VERBOSE, "AgentHost::agentLost: removing from lock %s", Log.formatUUID( LOG_LEVEL_VERBOSE, (UUID *)&iPFL->first ) );
			UUIDLock_Throw( &iPFL->second, agent, &iPFL->second.key );
		} else if ( !wasHost ) { // if avatar is suspected suspected abort L
			// get the avatar of the pf
			UUID owner;
			_timeb tb;
			float effPN;
			dStore->PFGetInfo( (UUID *)&iPFL->first, DDBPFINFO_OWNER, &tb, &lds, &nilUUID, &effPN );
			lds.rewind();
			lds.unpackData( sizeof(UUID) ); // thread
			if ( lds.unpackChar() == DDBR_OK ) {
				lds.unpackInt32();
				lds.unpackUUID( &owner );

				if ( owner == *agent ) {
					this->_ddbResampleParticleFilter_AbortLock( (UUID *)&iPFL->first, &iPFL->second.key );
				}
			} else {
				Log.log( 0, "AgentHost::agentLost: error getting pf owner %s", Log.formatUUID( 0, (UUID *)&iPFL->first ) );
			}
		}
	}

	// check cbba session
	if ( this->paSession != NULL ) {
		if ( wasHost )
			this->cbbaAbortSession( PA_ABORT_HOST_CHANGE );
		else
			this->cbbaAbortSession( PA_ABORT_AGENT_CHANGE );
	}
*/
/*	// clean up
	if ( wasHost ) {
		// remove from hosts
		AgentHost_DeleteState( (AgentBase::State *)this->hostKnown[*agent] );
		this->hostKnown.erase( *agent );

		// deal with agents belonging to that host
		UUID aId;
		mapAgentInfo::iterator iAI;
		AgentInfo *ai;
		int aStatus;

		for ( iAI = this->agentInfo.begin(); iAI != this->agentInfo.end(); iAI++ ) {
			if ( iAI->second.activationMode == AM_UNSET || iAI->second.activationMode == AM_EXTERNAL )
				continue; // skip

			aId = iAI->first;
			ai = &iAI->second;

			if ( *this->getUUID() == *dStore->AgentGetHost( agent ) ) { // one of their agents
				aStatus = dStore->AgentGetStatus( &aId );
				
				if ( aStatus == DDBAGENT_STATUS_SPAWNING ) { // reset to waiting
					Log.log( LOG_LEVEL_NORMAL, "AgentHost::agentLost: cleaning host: reset to waiting (%s, status %d)", Log.formatUUID( LOG_LEVEL_NORMAL, &aId ), aStatus );
					lds.reset();
					lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
					lds.packUUID( &nilUUID ); // free ownership
					lds.packUUID( this->getUUID() );
					lds.packInt32( DDBAGENT_STATUS_WAITING_SPAWN );
					lds.packUUID( this->getUUID() );
					lds.rewind();
					this->ddbAgentSetInfo( agent, &lds );
					lds.unlock();

				} else if ( aStatus == DDBAGENT_STATUS_READY
					     || aStatus == DDBAGENT_STATUS_FREEZING
					     || aStatus == DDBAGENT_STATUS_THAWING ) { // fail agent
					Log.log( LOG_LEVEL_NORMAL, "AgentHost::agentLost: cleaning host: fail agent (%s, status %d)", Log.formatUUID( LOG_LEVEL_NORMAL, &aId ), aStatus );
					lds.reset();
					lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
					lds.packUUID( &nilUUID ); // free ownership
					lds.packUUID( this->getUUID() );
					lds.packInt32( DDBAGENT_STATUS_FAILED );
					lds.packUUID( this->getUUID() );
					lds.rewind();
					this->ddbAgentSetInfo( agent, &lds );
					lds.unlock();

				} else { // unexpected status
					Log.log( 0, "AgentHost::agentLost: cleaning host: unexpected agent status %d, %s", aStatus, Log.formatUUID(0,&aId) );
				}
			}
		}

		// queue PA session
		this->cbbaQueuePAStart();

	} else if ( wasLocal ) {
		if ( status == DDBAGENT_STATUS_READY ) { // fail
			Log.log( LOG_LEVEL_NORMAL, "AgentHost::agentLost: failing" );
			lds.reset();
			lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
			lds.packUUID( &nilUUID ); // release ownership
			lds.packUUID( this->getUUID() );
			lds.packInt32( DDBAGENT_STATUS_FAILED );
			lds.packUUID( this->getUUID() );
			lds.rewind();
			this->ddbAgentSetInfo( agent, &lds );
			lds.unlock();

		} else if ( status == DDBAGENT_STATUS_FREEZING ) { // check if we have the state already or we have to fail the agent
			mapLock::iterator iL;

			iL = this->locks.find( *agent );

			if ( iL == this->locks.end() ) {
				Log.log( LOG_LEVEL_NORMAL, "AgentHost::agentLost: lock not found? failing" );
				lds.reset();
				lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
				lds.packUUID( &nilUUID ); // release ownership
				lds.packUUID( this->getUUID() );
				lds.packInt32( DDBAGENT_STATUS_FAILED );
				lds.packUUID( this->getUUID() );
				lds.rewind();
				this->ddbAgentSetInfo( agent, &lds );
				lds.unlock();

			} else {
				std::list<UUID>::iterator iT;
				for ( iT = iL->second.tumbler.begin(); iT != iL->second.tumbler.end(); iT++ ) {
					if ( *iT == *agent )
						break;
				}
				if ( iT == iL->second.tumbler.end() ) {
					Log.log( LOG_LEVEL_NORMAL, "AgentHost::agentLost: we have the state, let the freeze process finish" );
					// do nothing

				} else {
					Log.log( LOG_LEVEL_NORMAL, "AgentHost::agentLost: no state yet, failing" );
					lds.reset();
					lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
					lds.packUUID( &nilUUID ); // release ownership
					lds.packUUID( this->getUUID() );
					lds.packInt32( DDBAGENT_STATUS_FAILED );
					lds.packUUID( this->getUUID() );
					lds.rewind();
					this->ddbAgentSetInfo( agent, &lds );
					lds.unlock();


				}
			}
		} else if ( status == DDBAGENT_STATUS_THAWING ) { // set back to frozen
			Log.log( LOG_LEVEL_NORMAL, "AgentHost::agentLost: re-freezing" );
			this->AgentTransferAbortThaw( agent );

		} else {
			Log.log( 0, "AgentHost::agentLost: unexpected agent status %d", status );
		}
	}
*/
	return 0;
}

int AgentHost::_addAgent2( UUID *agent, AgentType *agentType, UUID *spawnThread, int activationMode, char priority, spConnection con, int watcher ) {
	AgentInfo *ai;

	if ( this->agentInfo.find( *agent ) == this->agentInfo.end() ) {
		ai = &this->agentInfo[ *agent ];

		ai->type = *agentType;
		ai->spawnThread = *spawnThread;
		ai->activationMode = activationMode;
		ai->priority = priority;
		ai->con = con;
		ai->watcher = watcher;

		ai->shellStatus = DDBAGENT_STATUS_ERROR; // no shell
		ai->shellCon = NULL;
		ai->shellWatcher = 0;

		// check if this was a unique
		list<AgentType>::iterator iA;
		for ( iA = this->uniqueNeeded.begin(); iA != this->uniqueNeeded.end(); iA++ ) {
			if ( iA->uuid == agentType->uuid && iA->instance == agentType->instance ) {
				this->uniqueNeeded.erase( iA ); // got it
				break;
			}
		}
	}

	return 0;
}

int AgentHost::_remAgent2( UUID *agent ) {

	mapAgentInfo::iterator iA = this->agentInfo.find( *agent );
	if ( iA == this->agentInfo.end() ) {
		return 1;
	}

	// clean up watcher
	this->ddbRemWatcher( agent, true );

	// clean up affinity
	map<UUID, AgentAffinityBlock, UUIDless>::iterator iAB;
	for ( iAB = iA->second.curAffinityBlock.begin(); iAB != iA->second.curAffinityBlock.end(); iAB++ ) {
		this->removeTimeout( &iAB->second.timeout );
	}

	this->locks.erase( *agent );

	// remove from agentInfo
	this->agentInfo.erase( *agent );

	return 0;
}

int AgentHost::_addUnique( AgentType *type ) {
	
	list<AgentType>::iterator iterUN;
	for ( iterUN = this->uniqueNeeded.begin(); iterUN != this->uniqueNeeded.end(); iterUN++ ) {
		if ( iterUN->uuid == type->uuid && iterUN->instance == type->instance )
			break;
	}
	if ( iterUN == this->uniqueNeeded.end() ) { // we didn't need this unique agent!
		return 1;
	}
	this->uniqueNeeded.erase( iterUN );
	this->uniqueSpawned.push_back( *type );

	return 0;
}

int AgentHost::recvAgentSpawned( spConnection con, DataStream *ds ) {
	DataStream lds;
	UUID agentId;
	mapAgentInfo::iterator iA;
	mapAgentInfo::iterator iP;

	ds->unpackUUID( &agentId );

	
	Log.log( LOG_LEVEL_NORMAL, "AgentHost::recvAgentSpawned: %s", Log.formatUUID( LOG_LEVEL_NORMAL, &agentId ) );

	// find the agent
	iA = this->agentInfo.find( agentId );
	if ( iA == this->agentInfo.end() ) {
		Log.log( 0, "AgentHost::recvAgentSpawned: unknown agent %s", Log.formatUUID( 0, &agentId ) );
		this->sendMessage( con, MSG_AGENT_STOP ); // stop agent
		return 1; // unknown agent
	}

	// clear spawn timeout
	if ( iA->second.spawnTimeout != nilUUID ) {
		this->removeTimeout( &iA->second.spawnTimeout );
		iA->second.spawnTimeout = nilUUID;
	}

	if ( STATE(AgentBase)->gracefulExit ) { // we're trying to exit, not interested in any more agents
		Log.log( 0, "AgentHost::recvAgentSpawned: graceful exit, aborting agent %s", Log.formatUUID( 0, &agentId ) );
		this->sendMessage( con, MSG_AGENT_STOP ); // stop agent
		return 0;
	}

	int watcher = this->watchConnection( con, AgentHost_CBR_cbWatchAgentConnection );
	if ( watcher == -1 )
		return 1;

	con->uuid = agentId;
	
	// start connection failure detection
	this->initializeConnectionFailureDetection( con );

	Log.log( LOG_LEVEL_VERBOSE, "AgentHost::recvAgentSpawned: shellStatus %d", iA->second.shellStatus );

	if ( iA->second.shellStatus == DDBAGENT_STATUS_SPAWNING ) { // this is a shell
		iA->second.shellStatus = DDBAGENT_STATUS_READY; // shell is ready
		iA->second.shellCon = con;
		iA->second.shellWatcher = watcher;

		this->AgentTransferUpdate( &nilUUID, &agentId, &nilUUID ); // check if we already have a lock (probably not)
		return 0; // finished
	} else if ( iA->second.shellStatus == DDBAGENT_STATUS_ABORT ) { // we don't want this agent anymore

		this->sendMessage( con, MSG_AGENT_STOP );

		this->stopWatchingConnection( con, watcher );
		this->closeConnection( con );

		iA->second.shellStatus = DDBAGENT_STATUS_ERROR;
		iA->second.shellCon = NULL;
		iA->second.shellWatcher = 0;

		return 0; // finished
	}
	
	iA->second.con = con;
	iA->second.watcher = watcher;

	// set the instance if there is one
	if ( iA->second.type.instance != -1 ) 
		this->sendMessage( con, MSG_AGENT_INSTANCE, &iA->second.type.instance, 1 );

	// update DDB
	UUID ticket;
	apb->apbUuidCreate( &ticket );
	lds.reset();
	lds.packInt32( DDBAGENTINFO_STATUS );
	lds.packInt32( DDBAGENT_STATUS_READY );
	lds.packUUID( &ticket );
	lds.rewind();
	this->ddbAgentSetInfo( &agentId, &lds );
	lds.unlock();

	this->agentInfo[agentId].expectingStatus.push_back( ticket );

	return 0;
}

int AgentHost::recvRequestUniqueId( spConnection con, DataStream *ds ) {
	UUID type;
	char instance;
	UUID thread;
	mapAgentInfo::iterator iterAI;
	list<AgentType>::iterator iterAT;

	ds->unpackUUID( &type );
	instance = ds->unpackChar();
	ds->unpackUUID( &thread );

	this->ds.reset();
	this->ds.packUUID( &thread );

	// try to find it in our agent list
	iterAI = this->agentInfo.begin();
	while ( iterAI != this->agentInfo.end() ) {
		if ( iterAI->second.type.uuid == type && iterAI->second.type.instance == instance ) 
			break;
		iterAI++;
	}

	if ( iterAI != this->agentInfo.end() ) {
		this->ds.packChar( 1 ); // spawned
		this->ds.packUUID( (UUID *)&iterAI->first );
		this->sendAgentMessage( &con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
//		this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		return 0;
	}

	// try to find it in our needed list
	iterAT = this->uniqueNeeded.begin();
	while ( iterAT != this->uniqueNeeded.end() ) {
		if ( (*iterAT).uuid == type && (*iterAT).instance == instance ) 
			break;
		iterAT++;
	}

	if ( iterAT != this->uniqueNeeded.end() ) {
		this->ds.packChar( 0 ); // not spawned
		this->sendAgentMessage( &con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
//		this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		return 0;
	}

	// couldn't find it
	this->ds.packChar( -1 ); // unknown unique
	this->sendAgentMessage( &con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
	//this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int AgentHost::recvAgentMirrorRegister( spConnection con, DataStream *ds ) {
	DataStream lds;
	UUID agentId;
	AgentType agentType;
	_timeb nilTB;
	nilTB.time = 0;

	ds->unpackUUID( &agentId );
	agentType = *(AgentType *)ds->unpackData( sizeof(AgentType) );

	Log.log( LOG_LEVEL_NORMAL, "AgentHost::recvAgentMirrorRegister: mirror agent registered" );

	int watcher = this->watchConnection( con, AgentHost_CBR_cbWatchAgentConnection );
	if ( watcher == -1 )
		return 1;

	con->uuid = agentId;

	this->_addAgent2( &agentId, &agentType, &nilUUID, AM_EXTERNAL, 0, con, watcher );

	// add to DDB
	this->ddbAddAgent( &agentId, &nilUUID, &agentType, &nilUUID, 0, 0, -1, AM_EXTERNAL );

	// add to mirrors
	this->ddbAddMirror( &agentId );

	return 0;
}


//-----------------------------------------------------------------------------
// _distribute


int AgentHost::globalStateChangeForward( unsigned char msg, char *data, unsigned int len ) {
	map<UUID,list<UUID>,UUIDless>::iterator iS;

	// sponsees
	for ( iS = this->gmSponsee.begin(); iS != this->gmSponsee.end(); iS++ ) {
		this->sendMessage( this->hostKnown[iS->first]->connection, msg, data, len );		
	}


	// mirrors
	UUID agentId;
	std::list<UUID>::iterator iterId;
	mapAgentInfo::iterator iterAI;
	mapAgentHostState::iterator iterHS;

	for ( iterId = this->DDBMirrors.begin(); iterId != this->DDBMirrors.end();  ) {
		agentId = *iterId;
		iterAI = this->agentInfo.find(agentId);
		iterId++; // increment here since it is possible DDBMirrors will change during the loop (i.e. a mirror is removed)
		if (  iterAI == this->agentInfo.end() ) { // agent not found!
			continue;
		} else if ( iterAI->second.con != NULL ) { // this is one of our agents
			this->sendMessage( iterAI->second.con, msg, data, len );
		} else { // forward this message to the appropriate host
/* should never happen			iterHS = this->hostKnown.find( *dStore->AgentGetHost((UUID *)&iterAI->first) );
			if ( iterHS == this->hostKnown.end() ) {
				continue; // host not found
			}

			// TODO check connection graph if we don't have connection to this host

			this->sendMessage( iterHS->second->connection, msg, data, len, &agentId );
*/		}
	}

	return 0;
}

int AgentHost::globalStateTransaction( unsigned char msg, char *data, unsigned int len, bool retrying, bool bundleable ) {

	if ( this->gmLocked == nilUUID && !this->globalStateTransactionInProgress ) { 
		this->globalStateTransactionInProgress = true;
		if ( nilUUID == this->atomicMessageOrdered( &this->oac_GLOBAL, &this->gmMemberList, msg, data, len, NULL, AgentHost_CBR_cbGlobalStateTransaction ) ) {
			this->globalStateTransactionInProgress = false;
			Log.log( 0, "AgentHost::globalStateTransaction: atomicMessageOrdered returned error!" );
			return 1; // message failed to send
		}
	} else {
		StateTransaction_MSG m;
		m.msg = msg;
		if ( len ) {
			m.data = (char *)malloc(len);
			memcpy( m.data, data, len );
		}
		m.len = len;
		m.bundleable = bundleable;
		
		if ( retrying ) {
			this->globalStateQueue.push_front( m );
		} else {
			this->globalStateQueue.push_back( m );
		}

		if ( this->globalStateQueue.size() > 1000 ) {
			Log.log( 0, "AgentHost::globalStateTransaction: globalStateQueue build up, %d items", (int)this->globalStateQueue.size() );
		}
	}

	return 0;
}

int AgentHost::globalStateAck( UUID *host, char *data, unsigned int len ) {

	if ( this->gmLocked == nilUUID && !this->globalStateTransactionInProgress ) { 
		
		// send right away
		if ( *host == *this->getUUID() )
			this->conProcessMessage( NULL, AgentHost_MSGS::MSG_AGENT_ACK_STATUS, data, len );
		else
			this->sendMessageEx( this->hostKnown[*host]->connection, MSGEX(AgentHost_MSGS,MSG_AGENT_ACK_STATUS), data, len );

	} else {
		StateTransaction_MSG m;
		m.msg = AgentHost_MSGS::MSG_AGENT_ACK_STATUS;
		if ( len ) {
			m.data = (char *)malloc(len);
			memcpy( m.data, data, len );
		}
		m.len = len;
		m.ticket = *host; // store host in ticket

		m.bundleable = false;
		
		this->globalStateQueue.push_back( m );
	}

	return 0;
}

int AgentHost::_globalStateTransactionSend() {
	DataStream lds;

	if ( this->gmLocked == nilUUID  && !this->globalStateTransactionInProgress && !this->globalStateQueue.empty() ) { // try the next message
		
		StateTransaction_MSG *m = &this->globalStateQueue.front();
		StateTransaction_MSG *m2;
		if ( this->globalStateQueue.size() > 1 ) {
			m2 = &*(++this->globalStateQueue.begin()); // second message
		}
		if ( m->msg == AgentHost_MSGS::MSG_AGENT_ACK_STATUS ) { // handle specially
			this->globalStateTransactionInProgress = true;

			if ( m->ticket == *this->getUUID() )
				this->conProcessMessage( NULL, AgentHost_MSGS::MSG_AGENT_ACK_STATUS, m->data, m->len );
			else
				this->sendMessageEx( this->hostKnown[m->ticket]->connection, MSGEX(AgentHost_MSGS,MSG_AGENT_ACK_STATUS), m->data, m->len );
			
			if ( m->len )
				free( m->data );
			this->globalStateQueue.pop_front();

			this->globalStateTransactionInProgress = false;

			this->_globalStateTransactionSend(); // next
		} else if ( !m->bundleable || this->globalStateQueue.size() == 1 || !m2->bundleable ) { // send single
			this->globalStateTransactionInProgress = true;
			this->atomicMessageOrdered( &this->oac_GLOBAL, &this->gmMemberList, m->msg, m->data, m->len, NULL, AgentHost_CBR_cbGlobalStateTransaction );
			if ( m->len )
				free( m->data );
			this->globalStateQueue.pop_front();
		} else { // build bundle
			this->globalStateTransactionInProgress = true;
			
			lds.reset();
			do {
				lds.packBool( 1 );
				lds.packUChar( m->msg );
				lds.packUInt32( m->len );
				if ( m->len ) {
					lds.packData( m->data, m->len );
					free( m->data );
				}
				this->globalStateQueue.pop_front();

				if ( this->globalStateQueue.empty() )
					break;

				m = &this->globalStateQueue.front();
			} while ( m->bundleable && lds.length() < 200*1024 );
			lds.packBool( 0 );

			this->atomicMessageOrdered( &this->oac_GLOBAL, &this->gmMemberList, OAC_STATE_TRANSACTION_BUNDLE, lds.stream(), lds.length(), NULL, AgentHost_CBR_cbGlobalStateTransaction );
		}
	}

	return 0;
}

int AgentHost::_distribute( unsigned char msg, char *data, unsigned int len ) {
	UUID agentId;
	std::list<UUID>::iterator iterId;
	mapAgentInfo::iterator iterAI;
	mapAgentHostState::iterator iterHS;

	// distribute to mirrors
	for ( iterId = this->DDBMirrors.begin(); iterId != this->DDBMirrors.end();  ) {
		agentId = *iterId;
		iterAI = this->agentInfo.find(agentId);
		iterId++; // increment here since it is possible DDBMirrors will change during the loop (i.e. a mirror is removed)
		if (  iterAI == this->agentInfo.end() ) { // agent not found!
			continue;
		} else if ( iterAI->second.con != NULL ) { // this is one of our agents
			this->sendMessageEx( iterAI->second.con, msg, (unsigned int *)AgentHost_MSGS::MSG_SIZE, AgentHost_MSGS::MSG_FIRST, AgentHost_MSGS::MSG_LAST, data, len );
		} else { // forward this message to the appropriate host
			iterHS = this->hostKnown.find( *dStore->AgentGetHost((UUID *)&iterAI->first) );
			if ( iterHS == this->hostKnown.end() ) {
				continue; // host not found
			}

			// TODO check connection graph if we don't have connection to this host

			this->sendMessageEx( iterHS->second->connection, msg, (unsigned int *)AgentHost_MSGS::MSG_SIZE, AgentHost_MSGS::MSG_FIRST, AgentHost_MSGS::MSG_LAST, data, len, &agentId );
		}
	}

	// distribute to hosts
	for ( iterHS = this->hostKnown.begin(); iterHS != this->hostKnown.end(); iterHS++ ) {
		if ( iterHS->second->connection == NULL )
			continue; // we don't have a connection to this host
				
		this->sendMessageEx( iterHS->second->connection, msg, (unsigned int *)AgentHost_MSGS::MSG_SIZE, AgentHost_MSGS::MSG_FIRST, AgentHost_MSGS::MSG_LAST, data, len );
	}

	return 0;
}


int AgentHost::cbbaPATestStart() {
	DataStream lds;
/*
	// get group size
	int groupSize = this->groupSize( &this->groupHostId );

	// TEMP load session data
	UUID sessionId;
	apb->apbUuidCreate( &sessionId );
	lds.reset();
	lds.packUUID( &this->groupHostId );
	lds.packUChar( GMSG_HOSTS_PA_START_SESSION );
	lds.packUUID( &sessionId );
	lds.packUUID( this->getUUID() );
	lds.packInt32( groupSize );
	{
		FILE *fp;
		WCHAR idbuf[64];
		UUID id;
		float cost;
		float affinity;
		int count;

		fopen_s( &fp, "CBBA_PA_test.txt", "r" );

		fscanf_s( fp, "count=%d\n", &count );

		lds.packInt32( count );

		while ( fscanf_s( fp, "id=%ws\n", idbuf, 64 ) == 1 ) {
			// read cost
			fscanf_s( fp, "cost=%f\n", &cost );

			UuidFromString( (RPC_WSTR)idbuf, &id );

			lds.packUUID( &id );
			lds.packFloat32( cost );

			while ( fscanf_s( fp, "resource=%ws\n", idbuf, 64 ) == 1 ) {
				lds.packBool( 1 );

				UuidFromString( (RPC_WSTR)idbuf, &id );
				lds.packUUID( &id );
			}
			lds.packBool( 0 );

			while ( fscanf_s( fp, "affinity=%f,%ws\n", &affinity, idbuf, 64 ) == 2 ) {
				lds.packBool( 1 );

				UuidFromString( (RPC_WSTR)idbuf, &id );
				lds.packUUID( &id );
				lds.packFloat32( affinity );
			}
			lds.packBool( 0 );
			
		}

		fclose( fp );
	}

	// send start message
	this->sendMessage( &this->groupHostId, MSG_GROUP_MSG, lds.stream(), lds.length() );

	lds.unlock();
*/
	return 0;
}

int AgentHost::cbbaPAStart() {
	DataStream lds;
	list<UUID>::iterator iH;
	list<UUID>::iterator iI;
	mapAgentInfo::iterator iAI;
	map<UUID, PA_ProcessInfo, UUIDless>::iterator iP;
	int i;
	RPC_STATUS Status;
	map<UUID, float, UUIDless> processCost;
	map<UUID, map<UUID, float, UUIDless>, UUIDless> affinityTable;
	map<UUID, float, UUIDless> *affinityUs;
	map<UUID, float, UUIDless> *affinityThem;
	map<UUID, float, UUIDless>::iterator iAff;
	map<UUID, unsigned int, UUIDless>::iterator iD;
	UUID affTo;
	float affinity;

	if ( !this->gmMemberList.size() || this->gmMemberList.front() != *this->getUUID() )
		return 0; // not leader

	this->paSession.id++;

	this->paSession.group = this->gmMemberList;
	this->paSession.groupSize = (int)this->paSession.group.size();
	this->paSession.p.clear();
	// prep agent data
	int agentCount = 0;
	for ( iAI = this->agentInfo.begin(); iAI != this->agentInfo.end(); iAI++ ) {
		if ( iAI->second.activationMode == AM_UNSET || iAI->second.activationMode == AM_EXTERNAL )
			continue; // skip

		agentCount++;

		processCost[iAI->first] = dStore->AgentGetProcessCost( (UUID *)&iAI->first );

		affinityUs = &affinityTable[iAI->first];
		
		// add all affinities
		dStore->AgentGetInfo( (UUID *)&iAI->first, DDBAGENTINFO_RAFFINITY, &lds, &nilUUID );
		lds.rewind();
		lds.unpackData( sizeof(UUID) ); // discard thread
		if ( lds.unpackChar() == DDBR_OK ) {
			lds.unpackInt32(); // infoFlags
			while ( lds.unpackBool() ) {
				lds.unpackUUID( &affTo );
				affinity = lds.unpackFloat32();
				if ( affinity ) {
					affinityThem = &affinityTable[affTo];
					
					affinity /= 2; // take average of us->them and them->us

					iAff = affinityUs->find( affTo );
					if(  iAff == affinityUs->end() ) (*affinityUs)[affTo] = affinity;
					else iAff->second += affinity;

					iAff = affinityThem->find( iAI->first );
					if(  iAff == affinityThem->end() ) (*affinityThem)[iAI->first] = affinity;
					else iAff->second += affinity;
				}
			}
		} else {
			// what happened?
		}
		lds.unlock();

	}

	if ( agentCount == 0 ) {
		return 0; // nothing to do
	}

	if ( 1 ) { // DEBUG dump affinity
		FILE *fp;
		mapAgentInfo::iterator jAI;
		char fname[MAX_PATH];
		sprintf_s( fname, MAX_PATH, "%s\\dump\\affinity.txt", logDirectory );
		if ( !fopen_s( &fp, fname, "w" ) ) {

			// title row
			fprintf( fp, "Us vs Them\t" );
			for ( iAI = this->agentInfo.begin(); iAI != this->agentInfo.end(); iAI++ ) {
				fprintf( fp, "%s\t", Log.formatUUID(0,(UUID *)&iAI->first) );
			}
			fprintf( fp, "\n" );

			for ( iAI = this->agentInfo.begin(); iAI != this->agentInfo.end(); iAI++ ) {
				fprintf( fp, "%s\t", Log.formatUUID(0,(UUID *)&iAI->first) );
			
				if ( affinityTable.find(iAI->first) != affinityTable.end() ) {
					affinityUs = &affinityTable[iAI->first];
					
					for ( jAI = this->agentInfo.begin(); jAI != this->agentInfo.end(); jAI++ ) {
						if ( affinityUs->find( jAI->first ) == affinityUs->end() ) { // no affinity
							fprintf( fp, "0\t" );
						} else {
							fprintf( fp, "%f\t", ((*affinityUs)[jAI->first]) );
						}
					}
				}
				fprintf( fp, "\n" );
			}

			fclose( fp );
		}
	}

	for ( iAI = this->agentInfo.begin(); iAI != this->agentInfo.end(); iAI++ ) {
		if ( iAI->second.activationMode == AM_UNSET || iAI->second.activationMode == AM_EXTERNAL )
			continue; // skip

		this->paSession.p[iAI->first].cost = processCost[iAI->first];
		i = 0;
		// resource requirements
		while ( i < AGENTTEMPLATE_MAX_RESOURCE_REQUIREMENTS && !UuidIsNil( &this->agentLibrary[iAI->second.type.uuid]->resourceRequirements[i], &Status ) ) {
			this->paSession.p[iAI->first].resourceRequirement.push_back( this->agentLibrary[iAI->second.type.uuid]->resourceRequirements[i] );
			i++;
		}
		// affinities
		affinityUs = &affinityTable[iAI->first];
		for ( iAff = affinityUs->begin(); iAff != affinityUs->end(); iAff++ ) {
			if ( iAff->second >= AGENTAFFINITY_THRESHOLD ) {
				this->paSession.p[iAI->first].agentAffinity[iAff->first] = iAff->second;
			}
		}
		lds.packInt32( dStore->AgentGetStatus( (UUID *)&iAI->first ) );
	}

	// reset session data
	this->paSession.allBids.clear();
	this->paSession.outbox.clear();
	this->paSession.buildQueued = nilUUID;
	this->paSession.distributeQueued = nilUUID;
	this->paSession.decided = false;
	this->paSession.sessionReady = false;
	this->paSession.round = 0;
	this->paSession.lastBuildRound = -1;
	this->paSession.capacity = this->processCapacity*this->processCores;
	this->paSession.usage = this->getCpuUsage()*this->processCapacity; // account for host usage
	UuidCreateNil( &this->paSession.consensusLowAgent );

	// statistics
	this->statisticsAgentAllocation[this->paSession.id].initiator = *this->getUUID();
	this->statisticsAgentAllocation[this->paSession.id].participantCount = (int)this->gmMemberList.size();
	this->statisticsAgentAllocation[this->paSession.id].agentCount = (int)this->paSession.p.size();
	this->statisticsAgentAllocation[this->paSession.id].round = 0;
	this->statisticsAgentAllocation[this->paSession.id].msgsSent = 0;
	this->statisticsAgentAllocation[this->paSession.id].dataSent = 0;
	this->statisticsAgentAllocation[this->paSession.id].clustersFound = 0;
	this->statisticsAgentAllocation[this->paSession.id].biggestCluster = 0;
	this->statisticsAgentAllocation[this->paSession.id].decided = false;
	apb->apb_ftime_s( &this->statisticsAgentAllocation[this->paSession.id].startT );

	// send start transaction
	Log.log( LOG_LEVEL_VERBOSE, "AgentHost::cbbaPAStart: proposing session %d", this->paSession.id );
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packInt32( this->paSession.id );
	for ( iH = this->paSession.group.begin(); iH != this->paSession.group.end(); iH++ ) {
		lds.packBool( true );
		lds.packUUID( &*iH );
	}
	lds.packBool( false );
	for ( iP = this->paSession.p.begin(); iP != this->paSession.p.end(); iP++ ) {
		lds.packBool( true );
		lds.packUUID( (UUID *)&iP->first );
		lds.packFloat32( iP->second.cost );
		for ( iI = iP->second.resourceRequirement.begin(); iI != iP->second.resourceRequirement.end(); iI++ ) {
			lds.packBool( true );
			lds.packUUID( &*iI );
		}
		lds.packBool( false );
		for ( iAff = iP->second.agentAffinity.begin(); iAff != iP->second.agentAffinity.end(); iAff++ ) {
			lds.packBool( true );
			lds.packUUID( (UUID *)&iAff->first );
			lds.packFloat32( iAff->second );
		}
		lds.packBool( false );
	}
	lds.packBool( false );
	this->atomicMessageOrdered( &this->oac_GLOBAL, &this->paSession.group, OAC_PA_START, lds.stream(), lds.length() );
	this->statisticsAgentAllocation[this->paSession.id].msgsSent += (int)this->paSession.group.size(); // statistics
	this->statisticsAgentAllocation[this->paSession.id].dataSent += (int)this->paSession.group.size() * lds.length();
	lds.unlock();

	// DEBUG
	if ( this->paSession.id == 98 )
		i = i;

	return 0;
}

int AgentHost::_cbbaPAStart( DataStream *ds ) {
	DataStream lds;
	UUID uuid, uuid2;
	list<UUID>::iterator iRes;
	list<UUID>::iterator lRes;
	list<UUID>::iterator lAgent;
	
	PA_Bid nilBid;

	UuidCreateNil( &nilBid.winner );
	nilBid.reward = 0;
	nilBid.support = -9999;
	nilBid.round = 0;

	UUID q;
	int sesIdq;
	ds->unpackUUID( &q );
	sesIdq = ds->unpackInt32();
	
	if ( q != this->gmMemberList.front() )
		return 0; // not leader
	
	if ( sesIdq < this->paSession.id )
		return 0; // old session

	// unpack group and make sure it matches current group
	list<UUID> groupq;
	while ( ds->unpackBool() ) {
		ds->unpackUUID( &uuid );
		groupq.push_back( uuid );
	}
	if ( groupq.size() != this->gmMemberList.size() )
		return 0; // group doesn't match

	list<UUID>::iterator iG;
	list<UUID>::iterator iH;
	for ( iG = groupq.begin(), iH = this->gmMemberList.begin();
		  iG != groupq.end(); iG++, iH++ ) {
		if ( *iG != *iH )
			break;
	}
	if ( iG != groupq.end() )
		return 0; // group doesn't match

	// unpack agents and check
	map<UUID, PA_ProcessInfo, UUIDless> pq;
	while ( ds->unpackBool() ) {
		ds->unpackUUID( &uuid );
		pq[uuid].cost = ds->unpackFloat32();
		while ( ds->unpackBool() ) {
			ds->unpackUUID( &uuid2 );
			pq[uuid].resourceRequirement.push_back( uuid2 );
		}
		while ( ds->unpackBool() ) {
			ds->unpackUUID( &uuid2 );
			pq[uuid].agentAffinity[uuid2] = ds->unpackFloat32();
		}
	}

	mapAgentInfo::iterator iAI;
	map<UUID, PA_ProcessInfo, UUIDless>::iterator iP;
	iP = pq.begin();
	for ( iAI = this->agentInfo.begin(); iAI != this->agentInfo.end(); iAI++ ) {
		if ( iAI->second.activationMode == AM_UNSET || iAI->second.activationMode == AM_EXTERNAL )
			continue; // skip
		if ( iP == pq.end() )
			break;
		if ( iP->first != iAI->first ) 
			break;
		iP->second.priority = iAI->second.priority; // retrieve priority
		iP->second.type = iAI->second.type; // retrieve type
		iP++;
	}
	if ( iAI != this->agentInfo.end() || iP != pq.end() )
		return 0; // agents don't match

	// session is valid, proceed
	if ( sesIdq > this->paSession.id ) { // join session
		this->paSession.id = sesIdq;
		// reset session data
		this->paSession.allBids.clear();
		this->paSession.outbox.clear();
		this->paSession.buildQueued = nilUUID;
		this->paSession.distributeQueued = nilUUID;
		this->paSession.decided = false;
		this->paSession.sessionReady = false;
		this->paSession.round = 0;
		this->paSession.lastBuildRound = -1;
		this->paSession.capacity = this->processCapacity*this->processCores;
		this->paSession.usage = this->getCpuUsage()*this->processCapacity; // account for host usage
		UuidCreateNil( &this->paSession.consensusLowAgent );

		// statistics
		this->statisticsAgentAllocation[sesIdq].initiator = q;
		this->statisticsAgentAllocation[sesIdq].participantCount = (int)this->gmMemberList.size();
		this->statisticsAgentAllocation[sesIdq].round = 0;
		this->statisticsAgentAllocation[sesIdq].msgsSent = 0;
		this->statisticsAgentAllocation[sesIdq].dataSent = 0;
		this->statisticsAgentAllocation[sesIdq].clustersFound = 0;
		this->statisticsAgentAllocation[sesIdq].biggestCluster = 0;
		this->statisticsAgentAllocation[sesIdq].decided = false;
		apb->apb_ftime_s( &this->statisticsAgentAllocation[sesIdq].startT );
	}
	
	this->statisticsAgentAllocation[sesIdq].agentCount = (int)pq.size(); // statistics

	Log.log( LOG_LEVEL_VERBOSE, "AgentHost::_cbbaPAStart: starting session %d", this->paSession.id );

	// DEBUG
	if ( this->paSession.id == 101 )
		q=q;

	this->paSession.group = groupq;
	this->paSession.p = pq;

	this->paSession.sessionReady = true;

	// check if we have the HWRESOURCE_EXCLUSIVE
	bool exclusiveOnly = false;
	UUID exclusiveId;
	UuidFromString( (RPC_WSTR)_T(HWRESOURCE_EXCLUSIVE), &exclusiveId );
	for ( lRes = this->hardwareResources.begin(); lRes != this->hardwareResources.end(); lRes++ ) {
		if ( *lRes == exclusiveId ) {
			exclusiveOnly = true;
			break;
		}
	}

	// pre bundle building
	char pri;
	for ( pri = DDBAGENT_PRIORITY_CRITICAL; pri < DDBAGENT_PRIORITY_COUNT; pri++ ) 
		this->paSession.b[pri].clear();
	this->paSession.ub.clear();
	int nextAgentId = 0;
	for ( iP = this->paSession.p.begin(); iP != this->paSession.p.end(); iP++ ) {
		iP->second.agentId = nextAgentId++;

		// set transfer penalty
		if ( dStore->AgentGetStatus( (UUID *)&iP->first ) == DDBAGENT_STATUS_WAITING_SPAWN ) {
			iP->second.transferPenalty = 0; // no penalty for unspawned agents
		} else {
			iP->second.transferPenalty = this->agentLibrary[this->agentInfo[iP->first].type.uuid]->transferPenalty;
		}

		// set local flag
		if ( *dStore->AgentGetHost( (UUID *)&iP->first ) == *this->getUUID() )
			iP->second.local = true;
		else
			iP->second.local = false;

		//Log.log( LOG_LEVEL_VERBOSE, "AgentHost::_cbbaPAStart: %s, local %d, cost %.2f, transfer penalty %.2f, resource requirements %d, affinities %d", Log.formatUUID( LOG_LEVEL_VERBOSE, &agent ), newSession->p[agent].local, newSession->p[agent].cost, newSession->p[agent].transferPenalty, newSession->p[agent].resourceRequirement.size(), newSession->p[agent].agentAffinity.size()  );

		// flag every agent as no winner for now for the initial update
		this->paSession.outbox[iP->first] = nilBid;

		this->paSession.allBids[iP->first][*this->getUUID()] = nilBid; // all agents are currently available

		// update consensus
		this->cbbaUpdateConsensus( &this->paSession, (UUID *)&iP->first, this->getUUID(), &nilBid );

		// check to see if this agent is transferable
		if ( !iP->second.local && iP->second.transferPenalty == 1 )
			continue; // not a local agent and cannot be transfered

		// check to see if we meet their resource requirements
		for ( iRes = iP->second.resourceRequirement.begin(); iRes != iP->second.resourceRequirement.end(); iRes++ ) {
			for ( lRes = this->hardwareResources.begin(); lRes != this->hardwareResources.end(); lRes++ ) {
				if ( *lRes == *iRes )
					break;
			}
			if ( lRes == this->hardwareResources.end() )
				break;
		}
		if ( iRes != iP->second.resourceRequirement.end() )
			continue; // we don't have the resource(s) for this agent

		// SPECIAL: if we have HWRESOURCE_EXCLUSIVE and they don't, we won't bundle them
		if ( exclusiveOnly ) {
			for ( iRes = iP->second.resourceRequirement.begin(); iRes != iP->second.resourceRequirement.end(); iRes++ ) {
				if ( *iRes == exclusiveId )
					break;
			}
			if ( iRes == iP->second.resourceRequirement.end() )
				continue; // they aren't exclusive and we are
		}

		// add to unbundled pool
		this->paSession.ub.push_back( iP->first );
	}

	if ( STATE(AgentBase)->gracefulExit ) { // we're not going to be making any bids
		// compute our first bid
		this->cbbaBuildBundle();

	} else {
		// bundle any local non transferable agents
		list<UUID>::iterator iA;
		float x;
		PA_Bid highBid;
		UuidCreateNil( &highBid.winner );
		highBid.reward = 0;
		highBid.support = -9999;
		highBid.round = 0;
		highBid.clusterHead = 0;

		// bid on any local non-transferable agents first, so we don't give false impressions about our support
		// to keep things easy we also say that anyone in the middle of spawning or thawing is also non-transferable
		for ( iA = this->paSession.ub.begin(); iA != this->paSession.ub.end(); ) {
			if ( this->paSession.p[*iA].local && 
				( this->paSession.p[*iA].transferPenalty == 1 || dStore->AgentGetStatus(&*iA) == DDBAGENT_STATUS_SPAWNING || dStore->AgentGetStatus(&*iA) == DDBAGENT_STATUS_THAWING ) ) { // found one, we'll take it
				highBid.winner = *this->getUUID();
				highBid.reward = 9999; // nobody else can possibly bid
				x = this->paSession.usage + this->paSession.p[*iA].cost;
				highBid.support = (this->paSession.capacity - x)/this->paSession.capacity;
				
				// add to bundle
				this->paSession.b[this->paSession.p[*iA].priority].push_back( *iA );
				this->paSession.usage += this->paSession.p[*iA].cost;
				
				this->paSession.allBids[*iA][*this->getUUID()] = highBid;

				// add to outbox
				this->paSession.outbox[*iA] = highBid;

				// update consensus
				this->cbbaUpdateConsensus( &this->paSession, &(*iA), this->getUUID(), &highBid );

				// remove from list
				lAgent = iA;
				iA++;
				this->paSession.ub.erase( lAgent );

			} else {
				iA++;
			}
		}

		// compute our first bid
		this->cbbaBuildBundle();
	}

	

	return 0;
}

int AgentHost::cbbaDecided( DataStream *ds ) {
	DataStream lds;
	UUID q;
	int sesIdq;

	ds->unpackUUID( &q );
	sesIdq = ds->unpackInt32();

	Log.log( LOG_LEVEL_VERBOSE, "AgentHost::cbbaDecided: decided %d", sesIdq );

	if ( 1 ) { // debug
		if ( this->paSession.id == 12 )
			int i = 0;
	}

	if ( sesIdq == this->paSession.id ) {
		this->paSession.decided = true;

		// statistics
		this->statisticsAgentAllocation[this->paSession.id].decided = true;
		apb->apb_ftime_s( &this->statisticsAgentAllocation[this->paSession.id].decidedT );
	}
	
	// accept allocation
	UUID agent;
	UUID winner;
	UUID curHost;
	AgentInfo *ai;
	list<UUID>::iterator iI;
	int status;

	map<UUID,UUID,UUIDless> oldAllocation = this->agentAllocation;
	this->agentAllocation.clear();

	while ( ds->unpackBool() ) {
		ds->unpackUUID( &agent );
		ds->unpackUUID( &winner );

		this->agentAllocation[agent] = winner;

		curHost = *dStore->AgentGetHost( &agent );

		mapAgentInfo::iterator iAI = this->agentInfo.find(agent);
		if ( iAI == this->agentInfo.end() ) {
			Log.log( 0, "AgentHost::cbbaDecided: agent info not found %s", Log.formatUUID( 0, &agent ) );
			return 1;
		} 
		ai = &iAI->second;
		status = dStore->AgentGetStatus( &agent );

		Log.log( LOG_LEVEL_VERBOSE, "agent %s %s winner %s curHost %s status %d shellStatus %d", ai->type.name,
			Log.formatUUID( LOG_LEVEL_VERBOSE, &agent ), Log.formatUUID( LOG_LEVEL_VERBOSE, &winner ), Log.formatUUID( LOG_LEVEL_VERBOSE, &curHost ),
			status, ai->shellStatus );

		if ( STATE(AgentBase)->gracefulExit == true )
			continue; // we're no longer participating in agent alloction

		if ( winner == *this->getUUID() ) { // we win
			if ( curHost == *this->getUUID() ) { // we already have this agent
			/*	if ( status == DDBAGENT_STATUS_FREEZING ) { // we are in the process of freezing, get ready to take it back
					// check if we were already expecting ownership of this agent
					if ( oldAllocation[agent] != *this->getUUID() ) { // start expecting
						//
						if ( ai->shellStatus == DDBAGENT_STATUS_ERROR ) { // no shell
							// start agent shell
							this->spawnAgent( &agent, &ai->type, NULL, true );
						} else if ( ai->shellStatus == DDBAGENT_STATUS_ABORT ) { // old shell must be in the middle of spawning
							// accept shell
							ai->shellStatus = DDBAGENT_STATUS_SPAWNING;
						} else {
							Log.log( 0, "AgentHost::cbbaDecided: unexpected shell status %d, win (%s)", ai->shellStatus, Log.formatUUID( 0, &agent ) );
						}
					}
				} else {
					// nothing to do
				}*/
			} else if ( status != DDBAGENT_STATUS_WAITING_SPAWN ) { // this agent used to belong to someone else
			/*	// check if we were already expecting ownership of this agent
				if ( oldAllocation[agent] != *this->getUUID() ) { // start expecting
					if ( ai->shellStatus == DDBAGENT_STATUS_ERROR ) { // no shell
						// start agent shell
						this->spawnAgent( &agent, &ai->type, NULL, true );
					} else if ( ai->shellStatus == DDBAGENT_STATUS_ABORT ) { // old shell must be in the middle of spawning
						// accept shell
						ai->shellStatus = DDBAGENT_STATUS_SPAWNING;
					} else {
						Log.log( 0, "AgentHost::cbbaDecided: unexpected shell status2 %d, win (%s)", ai->shellStatus, Log.formatUUID( 0, &agent ) );
					}
				}*/
				if ( status == DDBAGENT_STATUS_FROZEN ) { // agent is frozen and ready for the taking
					this->AgentTransferAvailable( &agent );
				} else if ( status == DDBAGENT_STATUS_FAILED ) { // agent is ready for the taking
					this->AgentRecoveryAvailable( &agent );
				}
			} else { // new agent
				// check if we were already expecting ownership of this agent
				if ( oldAllocation[agent] != *this->getUUID() ) {  // start expecting
					// claim agent
					UUID ticket;
					apb->apbUuidCreate( &ticket );
					lds.reset();
					lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
					lds.packUUID( this->getUUID() ); 
					lds.packUUID( &ticket ); 
					lds.packInt32( DDBAGENT_STATUS_SPAWNING );
					lds.packUUID( &ticket );
					lds.rewind();
					this->ddbAgentSetInfo( &agent, &lds );
					lds.unlock();

					ai->expectingStatus.push_back( ticket );
				}
			}
		} else if ( winner != nilUUID ) { // we lose, but someone else one
			if ( curHost == *this->getUUID() ) { // this used to be our agent
				if ( status == DDBAGENT_STATUS_READY ) {
					this->AgentTransferStartFreeze( &agent );
				}
			} else {

				// check if we were expecting ownership of this agent
				if ( oldAllocation[agent] == *this->getUUID() ) { // we were expecting but now lost, clean up

					if ( STATE(AgentBase)->gracefulExit ) { 
						UUIDLock_Throw( &this->gracefulExitLock, &agent, &nilUUID ); // don't need to wait on this anymore
					}
					
					if ( ai->shellStatus == DDBAGENT_STATUS_SPAWNING ) { // shell was spawning
						ai->shellStatus = DDBAGENT_STATUS_ABORT; // abort agent when it reports in
					} else if ( ai->shellStatus == DDBAGENT_STATUS_READY ) { // shell was active, kill shell
						this->sendMessage( ai->shellCon, MSG_AGENT_STOP );

						this->stopWatchingConnection( ai->shellCon, ai->shellWatcher );
						this->closeConnection( ai->shellCon );

						ai->shellStatus = DDBAGENT_STATUS_ERROR;
						ai->shellCon = NULL;
						ai->shellWatcher = 0;
					} else {
						Log.log( 0, "AgentHost::cbbaDecided: unexpected shell status %d, lose (%s)", ai->shellStatus, Log.formatUUID( 0, &agent ) );
					}
				}
			}
		} else { // no one wins
			this->_killAgent( &agent );
		}
	}

	// clean up
	if ( this->paSession.distributeQueued != nilUUID ) {
		this->removeTimeout( &this->paSession.distributeQueued );
		this->paSession.distributeQueued = nilUUID;
	}
	if ( this->paSession.buildQueued != nilUUID ) {
		this->removeTimeout( &this->paSession.buildQueued );
		this->paSession.buildQueued = nilUUID;
	}

	if ( *this->getUUID() == this->gmMemberList.front() ) { // we are the group leader
		if ( this->cbbaQueued != nilUUID )
			this->resetTimeout( &this->cbbaQueued );
		else 
			this->cbbaQueued = this->addTimeout( CBBA_ALLOCATION_INTERVAL, AgentHost_CBR_cbCBBAQueued );
	}

	return 0;
}
/*
int AgentHost::cbbaAbortSession( int reason ) {

	this->ds.reset();
	this->ds.packUUID( &this->groupHostId );
	this->ds.packUChar( GMSG_HOSTS_PA_ABORT_SESSION );
	this->ds.packUUID( &this->paSession.id );
	this->ds.packInt32( reason ); 
	this->sendMessage( &this->groupHostId, MSG_GROUP_MSG, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	// abort locally so that session is clean before we move on
	this->_cbbaAbortSession( this->paSession.id, reason );

	return 0;
}

int AgentHost::_cbbaAbortSession( UUID sessionId, int reason ) {

	Log.log( LOG_LEVEL_VERBOSE, "AgentHost::_cbbaAbortSession: session %s, reason %d", Log.formatUUID( LOG_LEVEL_VERBOSE, &sessionId ), reason );
			
	if ( this->paSession && this->paSession.id == sessionId ) {
		if ( this->paSession.distributeQueued != nilUUID )
			this->removeTimeout( &this->paSession.distributeQueued );
		if ( this->paSession.buildQueued != nilUUID )
			this->removeTimeout( &this->paSession.buildQueued );

		delete this->paSession;
		this->paSession = NULL;

		// rebroadcast abort
		this->ds.reset();
		this->ds.packUUID( &this->groupHostId );
		this->ds.packUChar( GMSG_HOSTS_PA_ABORT_SESSION );
		this->ds.packUUID( &sessionId );
		this->ds.packInt32( reason ); 
		this->sendMessage( &this->groupHostId, MSG_GROUP_MSG, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

		if ( this->paSessionInitiator ) { // queue new paStart
			this->cbbaQueuePAStart();
		}
	}

	return 0;
}
*/
int AgentHost::cbbaBuildBundle() {
	DataStream lds;
	PA_ProcessInfo *pp;
	
	list<UUID>::iterator iA;
	list<UUID>::iterator iBun;
	map<UUID,float,UUIDless>::iterator iAff;
	char pri;

	if ( this->paSession.buildQueued != nilUUID ) {
		this->removeTimeout( &this->paSession.buildQueued );
		this->paSession.buildQueued = nilUUID;
	}

	if ( STATE(AgentBase)->gracefulExit ) { // we're not going to be making any bids
		// do nothing

	} else {

		// increment round if necessary
		if ( this->paSession.round == this->paSession.lastBuildRound )
			this->paSession.round++; 
		this->paSession.lastBuildRound = this->paSession.round;

		// DEBUG
		if ( this->paSession.id == 28 && this->paSession.round == 6 )
			int brk=1;

		this->statisticsAgentAllocation[this->paSession.id].round = this->paSession.round; // statistics
		this->statisticsAgentAllocation[this->paSession.id].bundleBuilds++;

		// initial values and reward calculation
		for ( iA = this->paSession.ub.begin(); iA != this->paSession.ub.end(); iA++ ) { 
			pp = &this->paSession.p[*iA];
			pp->ancestorAffinity = 0;

			for ( iAff = pp->agentAffinity.begin(); iAff != pp->agentAffinity.end(); iAff++ ) {
				for ( pri = DDBAGENT_PRIORITY_CRITICAL; pri < DDBAGENT_PRIORITY_COUNT; pri++ ) {
					for ( iBun = this->paSession.b[pri].begin(); iBun != this->paSession.b[pri].end(); iBun++ ) {
						if ( iAff->first == *iBun ) { // we have this in our bundle so we get the affinity bonus
							pp->ancestorAffinity += iAff->second;
							break;
						}
					}
					if ( iBun != this->paSession.b[pri].end() )
						break;
				}
			}

			pp->reward = (pp->cost + pp->ancestorAffinity + pp->resourceRequirement.size()*10) * (pp->local ? 1 : 1 - pp->transferPenalty);
		}

		// generate affinity clusters
		this->paSession.nextClusterId = 1; // current cluster
		this->paSession.cluster.clear();
		memset( this->paSession.agentCluster, 0, sizeof(int)*CBBA_PA_MAX_AGENTS );
		char clusterSkip[CBBA_PA_MAX_AGENTS];
		memset( clusterSkip, 0, sizeof(char)*CBBA_PA_MAX_AGENTS );

		for ( iA = this->paSession.ub.begin(); iA != this->paSession.ub.end(); iA++ ) {
			if ( this->paSession.agentCluster[this->paSession.p[*iA].agentId] != 0 ) 
				continue; // already in a cluster
			if ( this->paSession.p[*iA].priority >= DDBAGENT_PRIORITY_REDUNDANT_HIGH ) 
				continue; // only non-redundant agents can be in clusters

			this->cbbaAffinityClusterRecurse( *iA, this->paSession.nextClusterId, clusterSkip, this->paSession.agentCluster, &this->paSession.cluster[this->paSession.nextClusterId] );

			if ( this->paSession.cluster[this->paSession.nextClusterId].size() > 1 ) {
				this->paSession.nextClusterId++; // a cluster was formed, move to the next id
			} else {
				this->paSession.agentCluster[this->paSession.p[*iA].agentId] = 0;
				this->paSession.cluster.erase( this->paSession.nextClusterId ); // clear empty cluster
			}
		}

		// dump cluster info
		map<int,list<UUID>>::iterator iCl;
		Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaBuildBundle: cluster info, count %d", this->paSession.cluster.size() );
		for ( iCl = this->paSession.cluster.begin(); iCl != this->paSession.cluster.end(); iCl++ ) {
			Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaBuildBundle: cluster %d, size %d", iCl->first, iCl->second.size() );
			for ( iA = iCl->second.begin(); iA != iCl->second.end(); iA++ ) {
				Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaBuildBundle: %s", Log.formatUUID( LOG_LEVEL_ALL, &*iA ) );
			}

			// statistics
			if ( (int)iCl->second.size() > this->statisticsAgentAllocation[this->paSession.id].biggestCluster )
				this->statisticsAgentAllocation[this->paSession.id].biggestCluster = (int)iCl->second.size();
		}

		this->statisticsAgentAllocation[this->paSession.id].clustersFound += (int)this->paSession.cluster.size(); // statistics

		// iteratively build bundle
		char priority;
		for ( priority = DDBAGENT_PRIORITY_CRITICAL; priority < DDBAGENT_PRIORITY_COUNT; priority++ ) 
			while ( this->cbbaBuildBundleIterate( priority ) );

	}

	// dump bundle info
	Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaBuildBundle: bundle created, round %d, usage %f", this->paSession.round, this->paSession.usage );
	for ( pri = DDBAGENT_PRIORITY_CRITICAL; pri < DDBAGENT_PRIORITY_COUNT; pri++ ) {
		for ( iA = this->paSession.b[pri].begin(); iA != this->paSession.b[pri].end(); iA++ ) {
			Log.log( 0, "%s, cost %.3f, reward %.3f, support %.3f, clusterHead %d, priority %d, round %d", Log.formatUUID(LOG_LEVEL_ALL,&(*iA)), this->paSession.p[*iA].cost, this->paSession.allBids[*iA][*this->getUUID()].reward, this->paSession.allBids[*iA][*this->getUUID()].support, this->paSession.allBids[*iA][*this->getUUID()].clusterHead, pri, this->paSession.allBids[*iA][*this->getUUID()].round );	
		}
	}

	// prepare update message
	if ( this->paSession.distributeQueued != nilUUID ) {
		this->removeTimeout( &this->paSession.distributeQueued );
		this->paSession.distributeQueued = nilUUID;
	}

	if ( this->paSession.outbox.size() > 0 ) {
		map<UUID, PA_Bid, UUIDless>::iterator iB;
		
		lds.reset();
		lds.packUUID( this->getUUID() );
		lds.packInt32( this->paSession.id );
		lds.packInt32( (int)this->paSession.outbox.size() );

		for ( iB = this->paSession.outbox.begin(); iB != this->paSession.outbox.end(); iB++ ) {
			lds.packUUID( (UUID *)&iB->first );
			lds.packUUID( &iB->second.winner );
			lds.packFloat32( iB->second.reward );
			lds.packFloat32( iB->second.support );
			lds.packInt16( iB->second.round );
		}
		this->paSession.outbox.clear();

		// send update message
		list<UUID>::iterator iH;
		for ( iH = this->paSession.group.begin(); iH != this->paSession.group.end(); iH++ )
			if ( *iH != *this->getUUID() )
				this->sendAgentMessage( &*iH, MSG_PA_BID_UPDATE, lds.stream(), lds.length() );
		this->statisticsAgentAllocation[this->paSession.id].msgsSent += (int)this->paSession.group.size() - 1; // statistics
		this->statisticsAgentAllocation[this->paSession.id].dataSent += (int)(this->paSession.group.size() - 1) * lds.length();
		lds.unlock();
	}

	// check to see if we have consensus
	this->cbbaCheckConsensus();

	return 0;
}

int AgentHost::cbbaAffinityClusterRecurse( UUID id, int clusterId, char *clusterSkip, int *agentCluster, list<UUID> *cluster ) {

	list<UUID>::iterator iA;
	map<UUID,float,UUIDless>::iterator iAff;
	PA_ProcessInfo *pp;
	
	pp = &this->paSession.p[id];

	if ( clusterSkip[pp->agentId] )
		return 0;

	agentCluster[pp->agentId] = clusterId;
	if ( cluster )
		cluster->push_back( id );

	for ( iAff = pp->agentAffinity.begin(); iAff != pp->agentAffinity.end(); iAff++ ) {
		if ( this->paSession.p[iAff->first].priority >= DDBAGENT_PRIORITY_REDUNDANT_HIGH ) 
			continue; // only non-redundant agents can be clustered
		if ( agentCluster[this->paSession.p[iAff->first].agentId] != 0 ) {
			if ( agentCluster[this->paSession.p[iAff->first].agentId] != clusterId )
				return 1; // somehow is part of another cluster!
			continue; // already part of this cluster
		}
		for ( iA = this->paSession.ub.begin(); iA != this->paSession.ub.end(); iA++ ) { 
			if ( iAff->first == *iA ) { // found and available for clustering
				this->cbbaAffinityClusterRecurse( *iA, clusterId, clusterSkip, agentCluster, cluster );
				break;
			}
		}
	}
		
	return 0;
}

int AgentHost::cbbaBuildBundleIterate( char priority ) {
	int cmp;
	RPC_STATUS Status;
	PA_Bid curBid;
	PA_Bid highBid;
	UUID highAgent; 
	float x;
	list<UUID>::iterator iA;
	list<UUID>::iterator iB;
	list<UUID>::iterator highiA;
	map<int,list<UUID>>::iterator iC;
	list<UUID> *cluster;
	list<UUID> highCluster;
	char clusterSkip[CBBA_PA_MAX_AGENTS];
	char pri;

	curBid.round = this->paSession.round;

	// -- compute reward for unbundled agents and see if we can win the bidding --
	UuidCreateNil( &highBid.winner );
	highBid.reward = -1;
	highBid.support = -9999;
	
	// check clusters
	if ( priority < DDBAGENT_PRIORITY_REDUNDANT_HIGH ) {
		map<int,list<list<UUID>>> exploredClusters;
		if ( this->paSession.capacity - this->paSession.usage > 0 ) {
			for ( iC = this->paSession.cluster.begin(); iC != this->paSession.cluster.end(); iC++ ) {
				cluster = &iC->second;
				exploredClusters.clear();
				memset( clusterSkip, 0, sizeof(char)*CBBA_PA_MAX_AGENTS );
				this->cbbaTestClusterRecurse( cluster, (int)cluster->size(), clusterSkip, cluster->begin(), &exploredClusters, &highBid, &highCluster );
			}
		}
	}
	// check unclustered agents (even treating clustered agents as individuals)
	for ( iA = this->paSession.ub.begin(); iA != this->paSession.ub.end(); iA++ ) {
		if ( this->paSession.p[*iA].priority != priority )
			continue; // priority doesn't match current cycle

		if ( priority >= DDBAGENT_PRIORITY_REDUNDANT_HIGH ) { // make sure we don't already have this type of agent, because redundant agents should be one per host
			for ( pri = DDBAGENT_PRIORITY_CRITICAL; pri <= priority; pri++ ) {
				for ( iB = this->paSession.b[pri].begin(); iB != this->paSession.b[pri].end(); iB++ ) {
					if ( this->paSession.p[*iB].type.uuid == this->paSession.p[*iA].type.uuid
						&& this->paSession.p[*iB].type.instance == this->paSession.p[*iA].type.instance )
						break; // got one already
				}
				if ( iB != this->paSession.b[pri].end() )
					break; // got one
			}
			if ( pri-1 != priority )
				continue; // don't want
		}

		// compute reward
		curBid.reward = this->paSession.p[*iA].reward;

		// see if we can win bidding
		x = this->paSession.usage + this->paSession.p[*iA].cost;
		curBid.support = (this->paSession.capacity - x)/this->paSession.capacity;

		if ( !UuidIsNil( &this->paSession.allBids[*iA][*this->getUUID()].winner, &Status ) ) { // someone owns this already
			cmp = this->cbbaCompareBid( &this->paSession.allBids[*iA][*this->getUUID()], &curBid, false, false );
			if ( cmp <= 0 )
				continue; // lost or tied
		}

		// see if this is our best option
		cmp = this->cbbaCompareBid( &highBid, &curBid, false, false );
		if ( cmp > 0 ) {
			curBid.winner = *iA; // record the winning agent here for now
			highBid = curBid;
			highiA = iA;
		}
	}

	if ( highBid.reward >= 0 ) { // we got something
		highBid.clusterHead = 0;
		highBid.round = this->paSession.round;

		if ( UuidIsNil( &highBid.winner, &Status ) ) { // cluster bid
			highBid.winner = *this->getUUID();
			for ( iA = highCluster.begin(); iA != highCluster.end(); iA++ ) {
				// add to bundle
				this->paSession.b[priority].push_back( *iA );
				this->paSession.ub.remove( *iA );
				this->paSession.usage += this->paSession.p[*iA].cost;
				
				this->paSession.allBids[*iA][*this->getUUID()] = highBid;

				// add to outbox
				this->paSession.outbox[*iA] = highBid;

				// update consensus
				this->cbbaUpdateConsensus( &this->paSession, &*iA, this->getUUID(), &highBid );

				highBid.clusterHead++;
			}
			
			// clear lower priority bundles
			this->cbbaTrimBundle( &nilUUID, priority+1 );

			// recalculate cluster related stuff in case they changed
			Log.log( 0, "AgentHost::cbbaBuildBundleIterate: cluster agents added, update cluster info" );
			
			// initial values and reward calculation for unbundled agents
			PA_ProcessInfo *pp;
			list<UUID>::iterator iBun;
			map<UUID,float,UUIDless>::iterator iAff;
			char pri;
			for ( iA = this->paSession.ub.begin(); iA != this->paSession.ub.end(); iA++ ) { 
				pp = &this->paSession.p[*iA];
				pp->ancestorAffinity = 0;

				for ( iAff = pp->agentAffinity.begin(); iAff != pp->agentAffinity.end(); iAff++ ) {
					for ( pri = DDBAGENT_PRIORITY_CRITICAL; pri < DDBAGENT_PRIORITY_COUNT; pri++ ) {
						for ( iBun = this->paSession.b[pri].begin(); iBun != this->paSession.b[pri].end(); iBun++ ) {
							if ( iAff->first == *iBun ) { // we have this in our bundle so we get the affinity bonus
								pp->ancestorAffinity += iAff->second;
								break;
							}
						}
						if ( iBun != this->paSession.b[pri].end() )
							break;
					}
				}

				pp->reward = (pp->cost + pp->ancestorAffinity) * (pp->local ? 1 : 1 - pp->transferPenalty);
			}

			// generate affinity clusters
			this->paSession.nextClusterId = 1; // current cluster
			this->paSession.cluster.clear();
			memset( this->paSession.agentCluster, 0, sizeof(int)*CBBA_PA_MAX_AGENTS );
			memset( clusterSkip, 0, sizeof(char)*CBBA_PA_MAX_AGENTS );

			for ( iA = this->paSession.ub.begin(); iA != this->paSession.ub.end(); iA++ ) {
				if ( this->paSession.agentCluster[this->paSession.p[*iA].agentId] != 0 ) 
					continue; // already in a cluster
				if ( this->paSession.p[*iA].priority >= DDBAGENT_PRIORITY_REDUNDANT_HIGH ) 
					continue; // only non-redundant agents can be in clusters

				this->cbbaAffinityClusterRecurse( *iA, this->paSession.nextClusterId, clusterSkip, this->paSession.agentCluster, &this->paSession.cluster[this->paSession.nextClusterId] );

				if ( this->paSession.cluster[this->paSession.nextClusterId].size() > 1 ) {
					this->paSession.nextClusterId++; // a cluster was formed, move to the next id
				} else {
					this->paSession.agentCluster[this->paSession.p[*iA].agentId] = 0;
					this->paSession.cluster.erase( this->paSession.nextClusterId ); // clear empty cluster
				}
			}

			// dump cluster info
			map<int,list<UUID>>::iterator iCl;
			Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaBuildBundleIterate: cluster info, count %d", this->paSession.cluster.size() );
			for ( iCl = this->paSession.cluster.begin(); iCl != this->paSession.cluster.end(); iCl++ ) {
				Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaBuildBundleIterate: cluster %d, size %d", iCl->first, iCl->second.size() );
				for ( iA = iCl->second.begin(); iA != iCl->second.end(); iA++ ) {
					Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaBuildBundleIterate: %s", Log.formatUUID( LOG_LEVEL_ALL, &*iA ) );
				}
			}
		} else {
			highAgent = highBid.winner;
			highBid.winner = *this->getUUID();

			// add to bundle
			this->paSession.b[priority].push_back( highAgent );
			this->paSession.ub.erase( highiA );
			this->paSession.usage += this->paSession.p[highAgent].cost;
			
			this->paSession.allBids[highAgent][*this->getUUID()] = highBid;

			// clear lower priority bundles
			this->cbbaTrimBundle( &nilUUID, priority+1 );

			// add to outbox
			this->paSession.outbox[highAgent] = highBid;

			// update consensus
			this->cbbaUpdateConsensus( &this->paSession, &highAgent, this->getUUID(), &highBid );
		}

		return 1; // keep going
	} else {
		return 0; // we're done
	}
}

int AgentHost::cbbaTestClusterRecurse( list<UUID> *cluster, int activeCount, char *oldClusterSkip, list<UUID>::iterator start, map<int,list<list<UUID>>> *exploredClusters, PA_Bid *highBid, list<UUID> *highCluster, bool possibleClusterSplit ) {
	int i;
	list<UUID>::iterator it;
	list<UUID>::iterator itA;
	list<list<UUID>>::iterator iEx;
	list<UUID>::iterator iExA;
	
	map<UUID,float,UUIDless>::iterator iAff;

	float clusterCost = 0, clusterReward = 0;
	PA_Bid clusterBid;
	float x;
	int cmp;

	int nextClusterId;

	int tempClusterId[CBBA_PA_MAX_AGENTS];
	char clusterSkip[CBBA_PA_MAX_AGENTS];

	bool clusterValid;

	memcpy( clusterSkip, oldClusterSkip, sizeof(char)*CBBA_PA_MAX_AGENTS );

	// make sure we haven't done this sub cluster already
	for ( iEx = (*exploredClusters)[activeCount].begin(); iEx != (*exploredClusters)[activeCount].end(); iEx++ ) {
		iExA = iEx->begin();
		for ( it = cluster->begin(); it != cluster->end(); it++ ) {
			if ( clusterSkip[this->paSession.p[*it].agentId] )
				continue;
			if ( *iExA != *it ) {
				break; // cluster doesn't match
			}
			iExA++;
		}
		if ( it == cluster->end() ) {
			return 0; // cluster matches
		}
	}

	// add us as an explored cluster
	list<UUID> tmpList;
	list<UUID> *activeCluster;
	(*exploredClusters)[activeCount].push_back( tmpList );
	activeCluster = &(*exploredClusters)[activeCount].back();
	for ( it = cluster->begin(); it != cluster->end(); it++ ) {
		if ( clusterSkip[this->paSession.p[*it].agentId] )
			continue;
		activeCluster->push_back( *it );
	}

	if ( possibleClusterSplit ) {
		// make sure we are a single cluster
		memset( tempClusterId, 0, CBBA_PA_MAX_AGENTS*sizeof(int) );
		nextClusterId = 1;
		
		for ( itA = cluster->begin(); itA != cluster->end(); itA++ ) {
			if ( clusterSkip[this->paSession.p[*itA].agentId] )
				continue; // not active
			if ( tempClusterId[this->paSession.p[*itA].agentId] != 0 ) 
				continue; // already in a cluster

			this->cbbaAffinityClusterRecurse( *itA, nextClusterId, clusterSkip, tempClusterId, NULL );

			nextClusterId++;
		}

		if ( nextClusterId > 2 ) { // we have more than one cluster! break into separate clusters and process separately
			list<UUID> subcluster;
			for ( i = 1; i < nextClusterId; i++ ) {
				subcluster.clear();
				for ( itA = cluster->begin(); itA != cluster->end(); itA++ ) {
					if ( clusterSkip[this->paSession.p[*itA].agentId] )
						continue; // not active
					if ( tempClusterId[this->paSession.p[*itA].agentId] == i ) {
						subcluster.push_back( *itA );
					}
				}
				
				this->cbbaTestClusterRecurse( &subcluster, (int)subcluster.size(), clusterSkip, subcluster.begin(), exploredClusters, highBid, highCluster );
			}

			return 0;
		}
	}

	// -- see if we're a winning cluster --
	// calculate cost and reward
	int oldActiveCount = activeCount;
	activeCount = this->cbbaCalculateClusterReward( cluster, clusterSkip, &clusterCost, &clusterReward );

	if ( oldActiveCount != activeCount ) { // some new agents were skipped, start processing again
		this->cbbaTestClusterRecurse( cluster, activeCount, clusterSkip, start, exploredClusters, highBid, highCluster, true );
		return 0;
	}

	clusterValid = false;
	if ( activeCount == 1 ||									// this is an individual
		 activeCount > 1 && this->paSession.usage + clusterCost <= this->paSession.capacity ) {  // all clusters have to be within capacity
		
	    clusterValid = true; // so far so good

		// make sure everyone is winning
		clusterBid.reward = clusterReward;
		x = this->paSession.usage + clusterCost;
		clusterBid.support = (this->paSession.capacity - x)/this->paSession.capacity;
		UuidCreateNil( &clusterBid.winner );

		for ( itA = cluster->begin(); itA != cluster->end(); itA++ ) {
			if ( clusterSkip[this->paSession.p[*itA].agentId] )
				continue; // not active
		
			cmp = this->cbbaCompareBid( &clusterBid, &this->paSession.allBids[*itA][*this->getUUID()], false, false );
			if ( cmp >= 0) { // they win or tie, can't use this cluster
				clusterValid = false;
				break;
			}
		}
	}

	if ( clusterValid ) {
		// see if we're the best option
		clusterBid.reward = clusterReward;
		x = this->paSession.usage + clusterCost;
		clusterBid.support = (this->paSession.capacity - x)/this->paSession.capacity;
		UuidCreateNil( &clusterBid.winner );

		cmp = this->cbbaCompareBid( &clusterBid, highBid, false, false );
		if ( cmp < 0) { // we're the best!
			*highBid = clusterBid;
			highCluster->clear();
			for ( it = cluster->begin(); it != cluster->end(); it++ ) {
				if ( !clusterSkip[this->paSession.p[*it].agentId] )
					highCluster->push_back( *it );
			}
			return 0; // no need to go further on this branch
		}
	}

	if ( activeCount == 1 ) {
		return 0; // nothing left to turn off
	}

	// -- we didn't win so start turning off agents and see if sub clusters can win --
	for ( it = start; it != cluster->end(); it++ ) {
		if ( clusterSkip[this->paSession.p[*it].agentId] )
			continue; // already inactive

		// flag off
		clusterSkip[this->paSession.p[*it].agentId] = true;

		itA = it;
		itA++;
		if ( this->paSession.p[*it].agentAffinity.size() > 1 ) { // possible cluster split
			this->cbbaTestClusterRecurse( cluster, activeCount - 1, clusterSkip, itA, exploredClusters, highBid, highCluster, true );
		} else {
			this->cbbaTestClusterRecurse( cluster, activeCount - 1, clusterSkip, itA, exploredClusters, highBid, highCluster );
		}

		// flag back on
		clusterSkip[this->paSession.p[*it].agentId] = false;
	}

	return 0;
}

int AgentHost::cbbaCalculateClusterReward( list<UUID> *cluster, char *clusterSkip, float *clusterCost, float *clusterReward ) {
	bool loop = true;
	list<UUID>::iterator it;
	map<UUID,float,UUIDless>::iterator iAff;
	PA_ProcessInfo *pp;
	RPC_STATUS Status;
	int clusterSize;

	PA_Bid bestBid;
	float x;
	int cmp;

	while ( loop ) {
		loop = false;

		clusterSize = 0;

		// calculate cost and reward
		*clusterCost = 0;
		*clusterReward = 0;
		for ( it = cluster->begin(); it != cluster->end(); it++ ) {
			pp = &this->paSession.p[*it];
			if ( clusterSkip[pp->agentId] )
				continue;

			if ( this->paSession.usage + pp->cost > this->paSession.capacity ) {
				clusterSkip[pp->agentId] = true; // can't possibly be used
				loop = true;
				continue;
			}
			
			*clusterCost += pp->cost;
			*clusterReward += pp->reward;
			clusterSize++;

			for ( iAff = pp->agentAffinity.begin(); iAff != pp->agentAffinity.end(); iAff++ ) {
				if ( !clusterSkip[this->paSession.p[iAff->first].agentId] )
					*clusterReward += iAff->second*0.5f; // only add half because we get the other half from the other direction
			}
		}

		if ( loop )
			continue;

		// check to see if any individual agents can be eliminated from consideration
		bestBid.reward = *clusterReward;
		bestBid.round = this->paSession.round;
		for ( it = cluster->begin(); it != cluster->end(); it++ ) {
			if ( clusterSkip[this->paSession.p[*it].agentId] )
				continue;
			if ( UuidIsNil( &this->paSession.allBids[*it][*this->getUUID()].winner, &Status ) )
				continue; // no owner
			// test best case
			x = this->paSession.usage + this->paSession.p[*it].cost;
			bestBid.support = (this->paSession.capacity - x)/this->paSession.capacity;
			cmp = this->cbbaCompareBid( &bestBid, &this->paSession.allBids[*it][*this->getUUID()], false, false ); 
			if ( cmp >= 0 ) { // they win or tie so we can't possibly use this agent
				clusterSkip[this->paSession.p[*it].agentId] = true;
				loop = true; // recalculate reward
			}
		}
	}

	return clusterSize;
}

int AgentHost::cbbaTrimBundle( UUID *agent, char priority ) {
	list<UUID>::iterator it;
	list<UUID>::iterator iClusterHead;
	float preClusterUsage;
	int preClusterCount;
	
	float oldUsage = this->paSession.usage;

	int count;
	this->paSession.usage = this->getCpuUsage()*this->processCapacity; // account for host usage

	// find the agent and recalculate usage
	char pri;
	for ( pri = DDBAGENT_PRIORITY_CRITICAL; pri < DDBAGENT_PRIORITY_COUNT; pri++ ) {
		if ( pri == priority )
			break; // priority hit

		if ( *agent != nilUUID ) {
			count = 0;
			for ( it = this->paSession.b[pri].begin(); it != this->paSession.b[pri].end(); it++ ) {
				if ( this->paSession.allBids[*it][*this->getUUID()].clusterHead == 0 ) {
					iClusterHead = it;
					preClusterUsage = this->paSession.usage;
					preClusterCount = count;
				}

				if ( *it == *agent ) {
					break; // found it
				}

				this->paSession.usage += this->paSession.p[*it].cost;
				count++;
			}
			if ( it != this->paSession.b[pri].end() ) {
				break; // found it
			}
		} else {
			for ( it = this->paSession.b[pri].begin(); it != this->paSession.b[pri].end(); it++ ) {
				this->paSession.usage += this->paSession.p[*it].cost;
			}
		}
	}

	if ( pri < priority ) { // agent hit,
		this->paSession.usage = preClusterUsage;
		count = preClusterCount;

		// clear bids on the remaining agents at this priority
		for ( it = iClusterHead; it != this->paSession.b[pri].end(); it++ ) {
			// clear bid
			UuidCreateNil( &this->paSession.allBids[*it][*this->getUUID()].winner );
			this->paSession.allBids[*it][*this->getUUID()].reward = 0;
			this->paSession.allBids[*it][*this->getUUID()].support = 0;
			this->paSession.allBids[*it][*this->getUUID()].round = this->paSession.round;

			// add to outbox
			this->paSession.outbox[*it] = this->paSession.allBids[*it][*this->getUUID()];

			// add to the unbundled list
			this->paSession.ub.push_back( *it );
		}

		// trim the bid list
		this->paSession.b[pri].resize( count );
		
		pri++;
	}

	// clear all lower priority bids
	for ( ; pri < DDBAGENT_PRIORITY_COUNT; pri++ ) {
		for ( it = this->paSession.b[pri].begin(); it != this->paSession.b[pri].end(); it++ ) {
			// clear bid
			UuidCreateNil( &this->paSession.allBids[*it][*this->getUUID()].winner );
			this->paSession.allBids[*it][*this->getUUID()].reward = 0;
			this->paSession.allBids[*it][*this->getUUID()].support = 0;
			this->paSession.allBids[*it][*this->getUUID()].round = this->paSession.round;

			// add to outbox
			this->paSession.outbox[*it] = this->paSession.allBids[*it][*this->getUUID()];

			// add to the unbundled list
			this->paSession.ub.push_back( *it );
		}

		this->paSession.b[pri].clear();
	}

	// dump bundle info
//	if ( oldUsage != this->paSession.usage ) 
//		Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaTrimBundle: bundle trimmed, round %d, usage %f", this->paSession.round, this->paSession.usage );
//	else
//		Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaTrimBundle: no trimming necessary, round %d, usage %f", this->paSession.round, this->paSession.usage );
//	for ( pri = DDBAGENT_PRIORITY_CRITICAL; pri < DDBAGENT_PRIORITY_COUNT; pri++ ) {
//		for ( it = this->paSession.b[pri].begin(); it != this->paSession.b[pri].end(); it++ ) {
//			Log.log( 0, "%s, cost %.2f, reward %.2f, support %.2f, clusterHead %d, priority %d, round %d", Log.formatUUID(LOG_LEVEL_ALL,&(*it)), this->paSession.p[*it].cost, this->paSession.allBids[*it][*this->getUUID()].reward, this->paSession.allBids[*it][*this->getUUID()].support, this->paSession.allBids[*it][*this->getUUID()].clusterHead, pri, this->paSession.allBids[*it][*this->getUUID()].round );	
//		}
//	}


	return 0;
}

int AgentHost::cbbaParseBidUpdate( DataStream *ds ) {
	RPC_STATUS Status;
	int i, count, cmp;
	int sesIdq;
	UUID q;
	UUID agentId;
	PA_Bid newBid;
	PA_Bid *localBid;

	// get session id
	ds->unpackUUID( &q );
	sesIdq = ds->unpackInt32();

	if ( sesIdq < this->paSession.id ) { // must be an old message
		return 0;
	} else if ( sesIdq > this->paSession.id ) { // join session
		this->paSession.id = sesIdq;
		// reset session data
		this->paSession.allBids.clear();
		this->paSession.outbox.clear();
		this->paSession.buildQueued = nilUUID;
		this->paSession.distributeQueued = nilUUID;
		this->paSession.decided = false;
		this->paSession.sessionReady = false;
		this->paSession.round = 0;
		this->paSession.lastBuildRound = -1;
		this->paSession.capacity = this->processCapacity*this->processCores;
		this->paSession.usage = this->getCpuUsage()*this->processCapacity; // account for host usage
		UuidCreateNil( &this->paSession.consensusLowAgent );

		// statistics
		this->statisticsAgentAllocation[sesIdq].initiator = q;
		this->statisticsAgentAllocation[sesIdq].participantCount = (int)this->gmMemberList.size();
		this->statisticsAgentAllocation[sesIdq].round = 0;
		this->statisticsAgentAllocation[sesIdq].msgsSent = 0;
		this->statisticsAgentAllocation[sesIdq].dataSent = 0;
		this->statisticsAgentAllocation[sesIdq].clustersFound = 0;
		this->statisticsAgentAllocation[sesIdq].biggestCluster = 0;
		this->statisticsAgentAllocation[sesIdq].decided = false;
		apb->apb_ftime_s( &this->statisticsAgentAllocation[sesIdq].startT );
	}

	if ( this->paSession.decided ) { // we've already reached a decision
		return 0;
	}
	
	// parse the update data
	count = ds->unpackInt32();
	Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: bid update from %s, %d updates", Log.formatUUID(LOG_LEVEL_ALL,&q), count );
	for ( i=0; i<count; i++ ) {
		ds->unpackUUID( &agentId );
		ds->unpackUUID( &newBid.winner );
		newBid.reward = ds->unpackFloat32();
		newBid.support = ds->unpackFloat32();
		newBid.round = ds->unpackInt16();

		this->cbbaUpdateConsensus( &this->paSession, &agentId, &q, &newBid );
	
		// update our local round value if necessary
		if ( newBid.round >= this->paSession.round )
			this->paSession.round = newBid.round + 1;

		localBid = &this->paSession.allBids[agentId][*this->getUUID()];
	
		//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: - new - agent %s, winner %s, reward %.2f, support %.2f, round %d", Log.formatUUID(LOG_LEVEL_ALL,&agentId), Log.formatUUID(LOG_LEVEL_ALL,&newBid.winner), newBid.reward, newBid.support, newBid.round );
		//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: - old - agent %s, winner %s, reward %.2f, support %.2f, round %d", Log.formatUUID(LOG_LEVEL_ALL,&agentId), Log.formatUUID(LOG_LEVEL_ALL,&localBid->winner), localBid->reward, localBid->support, localBid->round );

		// resolve update based on rules in Table 1, "Improving the Efficiency of a decentralized Tasking Algorithm for UAV Teams with Asynchronous Communications" Johnson et al.
		if ( !UuidCompare( &q, &newBid.winner, &Status ) ) { // sender thinks they are the winner
			if ( !UuidCompare( this->getUUID(), &localBid->winner, &Status ) ) { // we think we are the winner
				cmp = this->cbbaCompareBid( &newBid, localBid, true );
				if ( cmp < 0 ) { // they win (ll. 1 and 2)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender them, us us, they win (ll. 1 and 2), trim, update local and rebroadcast sender, queue bundle" );

					// trim bundle 
					this->cbbaTrimBundle( &agentId );

					// update local and rebroadcast sender bid
					*localBid = newBid;
					this->paSession.outbox[agentId] = newBid;

					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), &newBid );
					
					// queue bundle build
					this->cbbaQueueBuildBundle();

				} else if ( cmp == 0 ) { // tie (only happens if we are the sender! i.e. never in normal operation)
					// do nothing
				} else if ( cmp > 0 ) { // we win (l. 3)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender them, us us, we win (l. 3), update round and rebroadcast" );

					// update round on our bid and rebroadcast local bid
					if ( localBid->round == this->paSession.round )
						this->paSession.round++; // make sure we send out a higher round than last time
					localBid->round = this->paSession.round;
					this->paSession.outbox[agentId] = *localBid;

					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), localBid );

				}
			} else if ( !UuidCompare( &q, &localBid->winner, &Status ) ) { // we think the sender is the winner
				if ( newBid.round > localBid->round ) { // more recent info (l. 4)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender them, us them, more recent info (l. 4), update local and rebroadcast sender" );

					cmp = this->cbbaCompareBid( &newBid, localBid );
					if ( cmp > 0 ) { // they made their bid worse?!
						// queue bundle build
						this->cbbaQueueBuildBundle();
					}
					
					// update with new info and rebroadcast sender bid
					*localBid = newBid;
					this->paSession.outbox[agentId] = newBid;

					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), &newBid );

				} else if ( newBid.round <= localBid->round ) { // same or older info (ll. 5 and 6)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender them, us them, same or older info (ll. 5 and 6), do nothing" );
					// do nothing
				}
			} else if ( !UuidIsNil( &localBid->winner, &Status ) ) { // we think some other host is the winner
				cmp = this->cbbaCompareBid( &newBid, localBid );
				if ( cmp < 0 && newBid.round >= localBid->round ) { // sender wins (l. 7)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender them, us m, sender wins (l. 7), update and rebroadcast" );
					// update with new info and rebroadcast sender bid
					*localBid = newBid;
					this->paSession.outbox[agentId] = newBid;
					
					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), &newBid );

				} else if ( cmp > 0 && newBid.round <= localBid->round ) { // m wins (l. 8)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender them, us m, m wins (l. 8), leave and rebroadcast" );
					// leave and rebroadcast local bid
					this->paSession.outbox[agentId] = *localBid;

				} else if ( cmp == 0 ) { // tied so side with m (l. 9)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender them, us m, tied so side with m (l. 9), leave and rebroadcast" );
					// leave and rebroadcast local bid
					this->paSession.outbox[agentId] = *localBid;

				} else if ( cmp < 0 && newBid.round > localBid->round ) { // unsure (l. 10)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender them, us m, unsure (l. 10), reset and rebroadcast sender, queue bundle" );
					// reset and rebroadcast sender bid
					UuidCreateNil( &localBid->winner );
					localBid->reward = 0;
					localBid->support = 0;
					localBid->round = this->paSession.round;
					this->paSession.outbox[agentId] = newBid;
					
					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), localBid );

					// queue new bundle build
					this->cbbaQueueBuildBundle();
				} else if ( cmp > 0 && newBid.round < localBid->round ) { // unsure (l. 11)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender them, us m, unsure (l. 11), reset and rebroadcast sender, queue bundle" );
					// reset and rebroadcast sender bid
					UuidCreateNil( &localBid->winner );
					localBid->reward = 0;
					localBid->support = 0;
					localBid->round = this->paSession.round;
					this->paSession.outbox[agentId] = newBid;

					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), localBid );

					// queue new bundle build
					this->cbbaQueueBuildBundle();
				} else { // (default)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender them, us m, (default), leave and rebroadcast" );
					// leave and rebroadcast local bid
					this->paSession.outbox[agentId] = *localBid;

				}
			} else { // we think there is no winner
				// sender wins (l. 12)
				//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender them, us no one, sender wins (l. 12), update and rebroadcast" );
				// update with new info and rebroadcast sender bid
				*localBid = newBid;
				this->paSession.outbox[agentId] = newBid;
				
				// update consensus
				this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), &newBid );
				
			}

		} else if ( !UuidCompare( this->getUUID(), &newBid.winner, &Status ) ) { // sender thinks we are the winner
			if ( !UuidCompare( this->getUUID(), &localBid->winner, &Status ) ) { // we think we are the winner
				if ( newBid.round == localBid->round ) { // same info (l. 13)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender us, us us, same info (l. 13), do nothing" );
					// do nothing
				} else { // old message (default)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender us, us us, old message (default), leave and rebroadcast" );
					// leave and rebroadcast local bid
					this->paSession.outbox[agentId] = *localBid;

				}
			} else if ( !UuidCompare( &q, &localBid->winner, &Status ) ) { // we think the sender is the winner 
				// so confused (l. 14)
				//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender us, us sender, so confused (l. 14), reset and rebroadcast sender, queue bundle" );
				// reset and rebroadcast sender bid
				UuidCreateNil( &localBid->winner );
				localBid->reward = 0;
				localBid->support = 0;
				localBid->round = this->paSession.round;
				this->paSession.outbox[agentId] = newBid;

				// update consensus
				this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), localBid );

				// queue build bundle
				this->cbbaQueueBuildBundle();

			} else if ( !UuidIsNil( &localBid->winner, &Status ) ) { // we think some other host is the winner
				// m wins (l. 15)
				//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender us, us m, m wins (l. 15), leave and rebroadcast" );
				// leave and rebroadcast local bid
				this->paSession.outbox[agentId] = *localBid;

			} else { // we think there is no winner
				// we're probably right (l. 16)
				//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender us, us no one, we're probably right (l. 16), leave and rebroadcast" );
				// leave and rebroadcast local bid
				this->paSession.outbox[agentId] = *localBid;

			}

		} else if ( !UuidIsNil( &newBid.winner, &Status ) ) { // sender thinks some other host m is the winner
			if ( !UuidCompare( this->getUUID(), &localBid->winner, &Status ) ) { // we think we are the winner
				cmp = this->cbbaCompareBid( &newBid, localBid, true );
				if ( cmp < 0 ) { // m wins (ll. 17 and 18)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender m, us us, m wins (ll. 17 and 18), trim, update and rebroadcast, queue bundle" );
					// trim bundle
					this->cbbaTrimBundle( &agentId );

					// update local and rebroadcast sender bid
					*localBid = newBid;
					this->paSession.outbox[agentId] = newBid;
					
					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), &newBid );
					
					// queue bundle build
					this->cbbaQueueBuildBundle();

				} else if ( cmp == 0 ) { // tie (only happens if we are m! i.e. never in normal operation)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender m, us us, tie, should never happen!" );
					// do nothing
				} else if ( cmp > 0 ) { // we win (l. 19)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender m, us us, we win (l. 19), update round and rebroadcast" );
					// update round on our bid and rebroadcast local bid
					if ( localBid->round == this->paSession.round )
						this->paSession.round++; // make sure we send out a higher round than last time
					localBid->round = this->paSession.round;
					this->paSession.outbox[agentId] = *localBid;

					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), localBid );

				}
			} else if ( !UuidCompare( &q, &localBid->winner, &Status ) ) { // we think the sender is the winner
				if ( newBid.round >= localBid->round ) { // m wins (l. 20)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender m, us sender, m wins (l. 20), update and rebroadcast" );
					cmp = this->cbbaCompareBid( &newBid, localBid );
					if ( cmp > 0 ) { // they made their bid worse?!
						// queue bundle build
						this->cbbaQueueBuildBundle();
					}

					// update and rebroadcast sender bid
					*localBid = newBid;
					this->paSession.outbox[agentId] = newBid;
					
					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), &newBid );

				} else if ( newBid.round < localBid->round ) { // possibly old info (l. 21)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender m, us sender, possibly old info (l. 21), reset and rebroadcast sender, queue bundle" );
					// reset and rebroadcast sender bid
					UuidCreateNil( &localBid->winner );
					localBid->reward = 0;
					localBid->support = 0;
					localBid->round = this->paSession.round;
					this->paSession.outbox[agentId] = newBid;
					
					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), localBid );

					// queue bundle build
					this->cbbaQueueBuildBundle();

				}
			} else if ( !UuidCompare( &localBid->winner, &newBid.winner, &Status ) ) { // we think the same host m is the winner
				if ( newBid.round > localBid->round ) { // new info (l. 22)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender m, us m, new info (l. 22), update and no rebroadcast" );
					cmp = this->cbbaCompareBid( &newBid, localBid );
					if ( cmp > 0 ) { // they made their bid worse?!
						// queue bundle build
						this->cbbaQueueBuildBundle();
					}
					
					// update AND rebroadcast (NOTE: the original specification says not to rebroadcast but we need to to reach consensus)
					*localBid = newBid;
					this->paSession.outbox[agentId] = newBid;
					
					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), &newBid );

				} else if ( newBid.round <= localBid->round ) { // same or old info (ll. 23 and 24)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender m, us m, same or old info (ll. 23 and 24), do nothing" );
					// do nothing
				}
			} else if ( !UuidIsNil( &localBid->winner, &Status ) ) { // we think some other host n is the winner
				cmp = this->cbbaCompareBid( &newBid, localBid );
				if ( cmp < 0 && newBid.round >= localBid->round ) { // m wins (l. 25)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender m, us n, m wins (l. 25), update and rebroadcast" );
					// update with new info and rebroadcast sender bid
					*localBid = newBid;
					this->paSession.outbox[agentId] = newBid;
					
					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), &newBid );

				} else if ( cmp > 0 && newBid.round <= localBid->round ) { // n wins (l. 26)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender m, us n, n wins (l. 26), leave and rebroadcast" );
					// leave and rebroadcast local bid
					this->paSession.outbox[agentId] = *localBid;

				} else if ( cmp == 0 ) { // tied so side with n (l. 27)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender m, us n, tied so side with n (l. 27), leave and rebroadcast" );
					// leave and rebroadcast local bid
					this->paSession.outbox[agentId] = *localBid;

				} else if ( cmp < 0 && newBid.round > localBid->round ) { // unsure (l. 28)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender m, us n, unsure (l. 28), reset and rebroadcast sender, queue bundle" );
					// reset and rebroadcast sender bid
					UuidCreateNil( &localBid->winner );
					localBid->reward = 0;
					localBid->support = 0;
					localBid->round = this->paSession.round;
					this->paSession.outbox[agentId] = newBid;
					
					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), localBid );

					// queue new bundle build
					this->cbbaQueueBuildBundle();

				} else if ( cmp > 0 && newBid.round < localBid->round ) { // unsure (l. 29)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender m, us n, unsure (l. 29), reset and rebroadcast sender, queue bundle" );
					// reset and rebroadcast sender bid
					UuidCreateNil( &localBid->winner );
					localBid->reward = 0;
					localBid->support = 0;
					localBid->round = this->paSession.round;
					this->paSession.outbox[agentId] = newBid;
					
					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), localBid );

					// queue new bundle build
					this->cbbaQueueBuildBundle();

				} else { // (default)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender m, us n, (default), leave and rebroadcast" );
					// leave and rebroadcast local bid
					this->paSession.outbox[agentId] = *localBid;

				}
			} else { // we think there is no winner
				// m wins (l. 30)
				//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender m, us n, m wins (l. 30), update and rebroadcast" );
				// update with new info and rebroadcast sender bid
				*localBid = newBid;
				this->paSession.outbox[agentId] = newBid;
				
				// update consensus
				this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), &newBid );

			}

		} else { // sender thinks there is no winner
			if ( !UuidCompare( this->getUUID(), &localBid->winner, &Status ) ) { // we think we are the winner
				// we win (l. 31)
				//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender no one, us us, we win (l. 31), leave and rebroadcast" );
				// leave and rebroadcast local bid
				this->paSession.outbox[agentId] = *localBid;

			} else if ( !UuidCompare( &q, &localBid->winner, &Status ) ) { // we think the sender is the winner
				// "no one" wins (l. 32)
				//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender no one, us sender, no one wins (l.32), update and rebroadcast, queue bundle" );
				// update with new info and rebroadcast sender bid
				*localBid = newBid;
				this->paSession.outbox[agentId] = newBid;
				
				// update consensus
				this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), &newBid );
					
				// queue bundle build
				this->cbbaQueueBuildBundle();

			} else if ( !UuidIsNil( &localBid->winner, &Status ) ) { // we think some other host is the winner
				if ( newBid.round > localBid->round ) { // "no one" wins (l. 33)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender no one, us m, no one wins (l.33), update and rebroadcast, queue bundle" );
					// update with new info and rebroadcast sender bid
					*localBid = newBid;
					this->paSession.outbox[agentId] = newBid;
					
					// update consensus
					this->cbbaUpdateConsensus( &this->paSession, &agentId, this->getUUID(), &newBid );

					// queue bundle build
					this->cbbaQueueBuildBundle();

				} else { // (default)
					//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender no one, us m, (default), leave and rebroadcast" );
					// leave and rebroadcast local bid
					this->paSession.outbox[agentId] = *localBid;

				}
			} else { // we think there is no winner
				// same info (l. 34)
				//Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaParseBidUpdate: sender no one, us no one, same info (l. 34), do nothing" );
				// do nothing
			}			
		}
	}

	// if we have updates in our outbox but there is no bundle build queue then queue distribute
	if ( this->paSession.outbox.size() > 0 && this->paSession.buildQueued == nilUUID && this->paSession.distributeQueued == nilUUID ) {
		// queue distribute
		this->paSession.distributeQueued = this->addTimeout( 0, AgentHost_CBR_cbCBBADistributeQueued, &this->paSession.id, sizeof(UUID) );
	}

	if ( this->paSession.buildQueued == nilUUID && this->paSession.sessionReady ) {
		// check to see if we have consensus
		this->cbbaCheckConsensus();
	}

	return 0;
}

int AgentHost::cbbaQueueBuildBundle() {

	if ( this->paSession.buildQueued == nilUUID ) {
		this->paSession.buildQueued = this->addTimeout( 0, AgentHost_CBR_cbCBBABuildQueued, &this->paSession.id, sizeof(UUID) );
	}

	if ( this->paSession.distributeQueued != nilUUID ) { // clear the queued distribute
		this->removeTimeout( &this->paSession.distributeQueued );
		this->paSession.distributeQueued = nilUUID;
	}

	return 0;
}
/*
int AgentHost::cbbaQueuePAStart() {

	if ( this->paSession != NULL )
		return 0; // already in progress

	if ( this->paSessionQueued == nilUUID ) {
		this->paSessionQueued = this->addTimeout( 0, AgentHost_CBR_cbCBBAStartQueued );
	} else {
		this->resetTimeout( &this->paSessionQueued ); // move to back of queue
	}

	return 0;
}
*/
int AgentHost::cbbaCompareBid( PA_Bid *left, PA_Bid *right, bool breakTiesWithId, bool nilIdLoses ) {
	RPC_STATUS Status;

	if ( nilIdLoses ) {
		if ( UuidIsNil( &left->winner, &Status ) ) { // no owner always loses
			return 1; // right wins
		} else if ( UuidIsNil( &right->winner, &Status ) ) {
			return -1;
		}
	}

	if ( left->support < 0 ) { // left is over capacity
		if ( right->support < 0 ) { // right is over capacity
			if ( right->support > left->support ) { // right is less burdened
				return 1; // right wins
			} else if ( right->support == left->support ) { // support is the same
				if ( right->reward > left->reward ) { // right has a better reward
					return 1; // right wins
				} else if ( right->reward == left->reward ) { // reward is the same
					goto tie;
				} else { // left has a better reward
					return -1; // left wins
				}
			} else { // left has better support
				return -1; // left wins
			}
		} else { // right is within capacity
			return 1; // right wins
		}
	} else { // left is within capacity
		if ( right->support < 0 ) { // right is over capacity
			return -1; // left wins
		} else { // right is within capacity
			if ( right->reward*right->support > left->reward*left->support ) { // right has better reward, try penalizing with support to encourage balancing
				return 1; // right wins
			} else if ( right->reward == left->reward ) { // same reward
				if ( right->support > left->support ) { // right has better support
					return 1; // right wins
				} else if ( right->support == left->support ) { // same support
					goto tie;
				} else { // left has better support
					return -1;
				}
			} else { // left has better reward
				return -1; // left wins
			}
		}
	}
	
tie:
	if ( breakTiesWithId ) {
		return UuidCompare( &left->winner, &right->winner, &Status );
	} else {
		return 0; // tie
	}
}

int AgentHost::cbbaUpdateConsensus( CBBA_PA_SESSION *paSession, UUID *agent, UUID *from, PA_Bid *bid ) {
	RPC_STATUS Status;

	paSession->allBids[*agent][*from] = *bid;

	if ( UuidIsNil( &paSession->consensusLowAgent, &Status ) || UuidCompare( agent, &paSession->consensusLowAgent, &Status ) < 0 )
		paSession->consensusLowAgent = *agent;

	return 0;
}

int AgentHost::cbbaCheckConsensus() {
	RPC_STATUS Status;
	map<UUID, map<UUID, PA_Bid, UUIDless>, UUIDless>::iterator iA;
	map<UUID, PA_Bid, UUIDless>::iterator iH;
	map<UUID, PA_ProcessInfo, UUIDless>::iterator iP;

	PA_Bid baseBid;

	if ( this->gmMemberList.front() != *this->getUUID() || this->paSession.decided )
		return 0; // not leader

	// check the low agent
	if ( this->paSession.consensusLowAgent == nilUUID )
		return 0;

	iA = this->paSession.allBids.find( this->paSession.consensusLowAgent );
	
	if ( iA->second.size() != this->paSession.groupSize ) { // fail
		Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaCheckConsensus: consensus not reached, agent %s", Log.formatUUID(LOG_LEVEL_ALL,(UUID*)&iA->first) );
		return 0;
	}

	iH = iA->second.begin();
	baseBid = iH->second;
	iH++;
	for ( ; iH != iA->second.end(); iH++ ) {
		if ( UuidCompare( &baseBid.winner, &iH->second.winner, &Status )
			|| baseBid.round != iH->second.round ) { // fail
			Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaCheckConsensus: consensus not reached, agent %s", Log.formatUUID(LOG_LEVEL_ALL,(UUID*)&iA->first) );
			return 0;
		} 
	}

	// check the remaining agents
	iA++;
	for ( ; iA != this->paSession.allBids.end(); iA++ ) {
		
		if ( iA->second.size() != this->paSession.groupSize ) { // fail
			this->paSession.consensusLowAgent = iA->first;
			Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaCheckConsensus: consensus not reached, agent %s", Log.formatUUID(LOG_LEVEL_ALL,(UUID*)&iA->first) );
			return 0;
		}

		iH = iA->second.begin();
		baseBid = iH->second;
		iH++;
		for ( ; iH != iA->second.end(); iH++ ) {
			if ( UuidCompare( &baseBid.winner, &iH->second.winner, &Status )
				|| baseBid.round != iH->second.round ) { // fail
				this->paSession.consensusLowAgent = iA->first;
				Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaCheckConsensus: consensus not reached, agent %s", Log.formatUUID(LOG_LEVEL_ALL,(UUID*)&iA->first) );
				return 0;
			} 
		}
	}

	if ( 1 ) { // debug
		if ( this->paSession.id == 10 )
			int i = 0;
	}

	// success!
	Log.log( LOG_LEVEL_ALL, "AgentHost::cbbaCheckConsensus: consensus reached" );
	for ( iA = this->paSession.allBids.begin(); iA != this->paSession.allBids.end(); iA++ ) {
		iH = iA->second.begin();
		Log.log( LOG_LEVEL_ALL, "agent %s, winner %s, reward %.3f, support %.3f, round %d", Log.formatUUID(LOG_LEVEL_ALL,(UUID*)&iA->first), Log.formatUUID(LOG_LEVEL_ALL,&iH->second.winner), iH->second.reward, iH->second.support, iH->second.round );
	}

	this->paSession.decided = true;

	// clear timeouts
	if ( this->paSession.buildQueued != nilUUID ) {
		this->removeTimeout( &this->paSession.buildQueued );
		this->paSession.buildQueued = nilUUID;
	}
	if ( this->paSession.distributeQueued != nilUUID ) {
		this->removeTimeout( &this->paSession.distributeQueued );
		this->paSession.distributeQueued = nilUUID;
	}

	// send start transaction
	DataStream lds;
	map<UUID, map<UUID, PA_Bid, UUIDless>, UUIDless>::iterator iB;
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packInt32( this->paSession.id );
	for ( iB = this->paSession.allBids.begin(); iB != this->paSession.allBids.end(); iB++ ) {
		lds.packBool( true );
		lds.packUUID( (UUID *)&iB->first );
		lds.packUUID( &iB->second[*this->getUUID()].winner );
	}
	lds.packBool( false );
	this->atomicMessageOrdered( &this->oac_GLOBAL, &this->paSession.group, OAC_PA_FINISH, lds.stream(), lds.length() );
	this->statisticsAgentAllocation[this->paSession.id].msgsSent += (int)this->paSession.group.size(); // statistics
	this->statisticsAgentAllocation[this->paSession.id].dataSent += (int)this->paSession.group.size() * lds.length();
	lds.unlock();
		
	return 0;
}


//-----------------------------------------------------------------------------
// Agent Spawn

int AgentHost::AgentSpawnAbort( UUID *agent ) {
	DataStream lds;

	// -- release the agent and reset status --
	Log.log( 0, "AgentHost::AgentSpawnAbort: aborting spawn (%s)", Log.formatUUID(0,agent) );
		
	UUID ticket;
	apb->apbUuidCreate( &ticket );

	// update DDB
	lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
	lds.packUUID( &nilUUID ); // release ownership
	lds.packUUID( &ticket );
	lds.packInt32( DDBAGENT_STATUS_WAITING_SPAWN );
	lds.packUUID( &ticket );
	lds.rewind();
	this->ddbAgentSetInfo( agent, &lds );
	lds.unlock();

	this->agentInfo[*agent].expectingStatus.push_back( ticket );

	return 0;
}


//-----------------------------------------------------------------------------
// Agent Transfer

int AgentHost::recvAgentState( UUID *agent, DataStream *ds ) {
	DataStream lds;
	UUID ticket;
	_timeb stateT;
	unsigned int stateSize;

	Log.log( LOG_LEVEL_NORMAL, "AgentHost::recvAgentState: got state from %s", Log.formatUUID(LOG_LEVEL_NORMAL,agent) );

	if (1) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T("1689f437-07ae-4d50-a4cb-4b081547183e"), &breakId );

		if ( breakId == *agent )
			int i=0;
	}

	// check if this a freeze
	if ( dStore->AgentGetStatus( agent ) == DDBAGENT_STATUS_FREEZING ) {
		ds->unpackUUID( &ticket );

		// make sure this is the right ticket
		mapLock::iterator iL = this->locks.find( *agent );
		if ( iL == this->locks.end() || iL->second.key != ticket )
			return 1; // lock doesn't match

		stateT = *(_timeb *)ds->unpackData( sizeof(_timeb) );
		stateSize = ds->unpackUInt32();

		// update DDB
		lds.reset();
		lds.packInt32( DDBAGENTINFO_STATE );
		lds.packData( &stateT, sizeof(_timeb) );
		lds.packUInt32( stateSize );
		lds.packData( ds->unpackData( stateSize ), stateSize );
		lds.rewind();
		this->ddbAgentSetInfo( agent, &lds );
		lds.unlock();

		// cleanup connection
		mapAgentInfo::iterator iA = this->agentInfo.find( *agent );
		spConnection oldCon = iA->second.con;
		this->stopWatchingConnection( iA->second.con, iA->second.watcher );
		iA->second.con = NULL;
		iA->second.watcher = 0;
		this->closeConnection( oldCon );

		// update lock
		this->AgentTransferUpdate( agent, agent, &ticket );

	}

	return 0;
}

int AgentHost::recvAgentResumeReady( UUID *agent, UUID *ticket ) {

	Log.log( LOG_LEVEL_NORMAL, "AgentHost::recvAgentResumeReady: %s", Log.formatUUID(LOG_LEVEL_NORMAL,agent) );

	mapAgentInfo::iterator iA;

	iA = this->agentInfo.find( *agent );
	if ( iA == this->agentInfo.end() ) {
		Log.log( 0, "AgentHost::recvAgentResumeReady: agent not found %s", Log.formatUUID(0,agent) );
		return 1; // what happened here
	}

	iA->second.shellStatus = DDBAGENT_STATUS_THAWING;

	this->AgentTransferUpdate( agent, agent, ticket );

	return 0;
}

int AgentHost::AgentTransferInfoChanged( UUID *agent, int infoFlags ) {
	DataStream lds, sds;
	UUID ticket, host;
	AgentInfo *ai;

	if ( infoFlags & DDBAGENTINFO_STATUS ) { // we sometimes need to acknowledge status changes
		int status;
		this->dStore->AgentGetInfo( agent, DDBAGENTINFO_RHOST | DDBAGENTINFO_RSTATUS, &lds, &nilUUID );
		lds.rewind();
		lds.unpackUUID( &ticket ); // ignore thread
		lds.unpackChar(); // response, has to be ok because we just modified it
		lds.unpackInt32(); // infoFlags
		lds.unpackUUID( &host );
		lds.unpackUUID( &ticket );
		status = lds.unpackInt32();
		lds.unpackUUID( &ticket );
		lds.unlock();

		
		Log.log( LOG_LEVEL_VERBOSE, "AgentHost::AgentTransferInfoChanged: status changed %d (%s)", status, Log.formatUUID( LOG_LEVEL_VERBOSE, agent ) );

		if (1) { // debug
			UUID breakId;
			UuidFromString( (RPC_WSTR)_T("7fc0efb1-fc72-49c0-8c93-0961da1c8e7e"), &breakId );

			if ( breakId == *agent )
				int i=0;
		}
		
		mapAgentInfo::iterator iAI = this->agentInfo.find(*agent);
		if ( iAI == this->agentInfo.end() ) {
			Log.log( 0, "AgentHost::AgentTransferInfoChanged: agent info not found %s", Log.formatUUID( 0, agent ) );
			return 1;
		} 
		ai = &iAI->second;

		if ( !ai->expectingStatus.empty() ) {
			if ( ai->expectingStatus.front() == ticket ) {
				switch ( status ) {
				case DDBAGENT_STATUS_SPAWNING:
					// check graceful exit
					if ( STATE(AgentBase)->gracefulExit ) {
						this->AgentSpawnAbort( agent );
						Log.log( 0, "AgentHost::AgentTransferInfoChanged: gracefulExit agent %s, aborting spawn", Log.formatUUID(0,agent) );
						break;
					}

					// we got ownership. spawn agent
					this->spawnAgent( agent, &ai->type, NULL ); 
					break;
				case DDBAGENT_STATUS_READY:
					// check graceful exit
					if ( STATE(AgentBase)->gracefulExit ) {
						if ( this->agentLibrary[ai->type.uuid]->transferPenalty == 1 ) { // non-transferable, fail agent
							// update status
							lds.reset();
							lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
							lds.packUUID( &nilUUID ); // release ownership
							lds.packUUID( this->getUUID() );
							lds.packInt32( DDBAGENT_STATUS_FAILED );
							lds.packUUID( this->getUUID() );
							lds.rewind();
							this->ddbAgentSetInfo( agent, &lds );
							lds.unlock();

							// stop agent
							this->sendMessage( iAI->second.con, MSG_AGENT_STOP );

							this->gracefulExitUpdate( agent );
							Log.log( 0, "AgentHost::AgentTransferInfoChanged: gracefulExit agent %s, failing non-transferable agent", Log.formatUUID(0,agent) );
						} else {  // begin freezing
							this->AgentTransferStartFreeze( agent );
							Log.log( 0, "AgentHost::AgentTransferInfoChanged: gracefulExit agent %s, freezing", Log.formatUUID(0,agent) );
						}
						break;
					}

					if ( ai->shellStatus == DDBAGENT_STATUS_THAWING ) { // thawing
						mapAgentInfo::iterator iA;

						iA = this->agentInfo.find( *agent );
						if ( iA == this->agentInfo.end() ) {
							return 1; // what happened here
						}

						// get message queues from DDB
						DDBAgent_MSG msg;
						this->dStore->AgentGetInfo( agent, DDBAGENTINFO_RMSG_QUEUES, &sds, &nilUUID );
						sds.rewind();
						sds.unpackData( sizeof(UUID) ); // ignore thread
						sds.unpackChar(); // response, has to be ok because we just modified it
						sds.unpackInt32(); // infoFlags

						// pack messages
						lds.reset();
						while ( sds.unpackBool() ) { // primary and secondary queues
							msg.msg = sds.unpackUChar();
							msg.len = sds.unpackUInt32();
							msg.data = (char *)sds.unpackData( msg.len );
							
							lds.packBool( 1 );
							lds.packUChar( msg.msg );
							lds.packUInt32( msg.len );
							if ( msg.len )
								lds.packData( msg.data, msg.len );
						}
						// local queue
						map<UUID,list<DDBAgent_MSG>,UUIDless>::iterator iQ;
						iQ = this->agentLocalMessageQueue.find( *agent );
						if ( iQ != this->agentLocalMessageQueue.end() ) {
							list<DDBAgent_MSG>::iterator iM;
							for ( iM = iQ->second.begin(); iM != iQ->second.end(); iM++ ) {
								lds.packBool( 1 );
								lds.packUChar( iM->msg );
								lds.packUInt32( iM->len );
								if ( iM->len ) {
									lds.packData( iM->data, iM->len );
									free( iM->data );
								}
							}
							this->agentLocalMessageQueue.erase( iQ );
						}
						lds.packBool( 0 ); // done messages
						
						// promote shell
						iA->second.con = iA->second.shellCon;
						iA->second.watcher = iA->second.shellWatcher;
						iA->second.shellStatus = DDBAGENT_STATUS_ERROR;
						iA->second.shellCon = NULL;
						iA->second.shellWatcher = 0;

						// start resume
						this->sendMessage( iA->second.con, MSG_AGENT_RESUME, lds.stream(), lds.length() );
						lds.unlock();			
						sds.unlock();

						Log.log( 0, "AgentHost::AgentTransferInfoChanged: agent resuming (%s)", Log.formatUUID(0,agent) );
					
					} else if ( ai->shellStatus == DDBAGENT_STATUS_RECOVERING ) { // recovering
						mapAgentInfo::iterator iA;

						iA = this->agentInfo.find( *agent );
						if ( iA == this->agentInfo.end() ) {
							return 1; // what happened here
						}
						
						// promote shell
						iA->second.con = iA->second.shellCon;
						iA->second.watcher = iA->second.shellWatcher;
						iA->second.shellStatus = DDBAGENT_STATUS_ERROR;
						iA->second.shellCon = NULL;
						iA->second.shellWatcher = 0;

						this->sendMessage( iA->second.con, MSG_AGENT_RECOVER_FINISH );

						Log.log( 0, "AgentHost::AgentTransferInfoChanged: agent finish recovery (%s)", Log.formatUUID(0,agent) );
					
					} else { // must be spawning
						// notify parent		
						lds.reset();
						lds.packUUID( &ai->spawnThread );
						lds.packBool( true );
						lds.packUUID( agent );
						this->sendAgentMessage( dStore->AgentGetParent( agent ), MSG_RESPONSE, lds.stream(), lds.length() );
						lds.unlock();
					}
					break;
				case DDBAGENT_STATUS_FREEZING:
					// check gracefulExit -> either way just freeze the agent
					
					// tell agent to freeze
					this->sendMessage( ai->con, MSG_AGENT_FREEZE, (char *)&this->locks[*agent].key, sizeof(UUID) );
					break;
				case DDBAGENT_STATUS_THAWING:
				case DDBAGENT_STATUS_RECOVERING:
					// check graceful exit
					if ( STATE(AgentBase)->gracefulExit ) {
						if ( status == DDBAGENT_STATUS_THAWING ) {
							this->AgentTransferAbortThaw( agent );
							Log.log( 0, "AgentHost::AgentTransferInfoChanged: gracefulExit agent %s, re-freezing", Log.formatUUID(0,agent) );
						} else {
							this->AgentRecoveryAbort( agent );
							Log.log( 0, "AgentHost::AgentTransferInfoChanged: gracefulExit agent %s, aborting recovery", Log.formatUUID(0,agent) );
						}
						break;
					}

					// get the shell ready
					if ( ai->shellStatus == DDBAGENT_STATUS_ERROR ) { // no shell
						// start agent shell
						this->spawnAgent( agent, &ai->type, NULL, true );
					} else if ( ai->shellStatus == DDBAGENT_STATUS_ABORT ) { // old shell must be in the middle of spawning
						// accept shell
						ai->shellStatus = DDBAGENT_STATUS_SPAWNING;
					} else if ( ai->shellStatus == DDBAGENT_STATUS_READY ) {
						this->AgentTransferUpdate( &nilUUID, agent, &nilUUID );
					} else {
						Log.log( 0, "AgentHost::AgentTransferInfoChanged: unexpected shell status %d, win (%s)", ai->shellStatus, Log.formatUUID( 0, agent ) );
					}
					break;
				case DDBAGENT_STATUS_WAITING_SPAWN:
				case DDBAGENT_STATUS_FROZEN:
				case DDBAGENT_STATUS_FAILED:
					// check graceful exit
					if ( STATE(AgentBase)->gracefulExit ) {
						this->gracefulExitUpdate( agent );
					}
					break;
				default:
					Log.log( 0, "AgentHost::AgentTransferInfoChanged: unhandled status %d", status );
				};

				ai->expectingStatus.pop_front();
			}
		}

		if ( status == DDBAGENT_STATUS_FAILED ) {
			this->agentAllocation[*agent] = nilUUID; // clear allocation
			this->cbbaPAStart(); // reallocate
		}

		if ( !STATE(AgentBase)->gracefulExit ) { // don't want to do this if we're already exiting
			if ( status == DDBAGENT_STATUS_RECOVERING && host == *this->getUUID() ) 
				this->AgentTransferUpdate( &nilUUID, agent, &nilUUID );

			if ( status == DDBAGENT_STATUS_THAWING && host == *this->getUUID() ) { // we're the thawing host, so wait for all active OAC's to decide before proceeding
				mapAtomicMessage::iterator iA;

				this->agentTransferActiveOACList[*agent].clear();
				for ( iA = this->atomicMsgs.begin(); iA != this->atomicMsgs.end(); iA++ ) {
					if ( iA->second.orderedMsg && !iA->second.delivered && iA->second.queue == this->oac_GLOBAL ) 
						this->agentTransferActiveOACList[*agent].push_back( iA->first );
				}

				this->AgentTransferUpdate( &nilUUID, agent, &nilUUID );
			}
		}

		if ( status == DDBAGENT_STATUS_FREEZING || status == DDBAGENT_STATUS_THAWING ) { // acknowledge, but do it in order!
			if ( STATE(AgentBase)->gracefulExit && status == DDBAGENT_STATUS_THAWING && host == *this->getUUID() ) {
				// don't acknowledge
			} else {
				lds.reset();
				lds.packUUID( this->getUUID() );
				lds.packUUID( agent );
				lds.packUUID( &ticket );
				this->globalStateAck( &host, lds.stream(), lds.length() );
				lds.unlock();
			}
		}


		if ( !STATE(AgentBase)->gracefulExit && status == DDBAGENT_STATUS_THAWING && this->ddbNotificationQueue.find(*agent) != this->ddbNotificationQueue.end() ) { // handle notification queue
			if ( host == *this->getUUID() ) { // we're the host, deliver notifications
				list<DDBAgent_MSG>::iterator iN;
				for ( iN = this->ddbNotificationQueue[*agent].begin(); iN != this->ddbNotificationQueue[*agent].end(); iN++ ) {
					this->sendAgentMessage( agent, iN->msg, iN->data, iN->len );
					if ( iN->len )
						free( iN->data );
				}
				this->ddbNotificationQueue.erase( *agent );
			} else { // not us, clear notifications
				list<DDBAgent_MSG>::iterator iN;
				for ( iN = this->ddbNotificationQueue[*agent].begin(); iN != this->ddbNotificationQueue[*agent].end(); iN++ ) {
					if ( iN->len )
						free( iN->data );
				}
				this->ddbNotificationQueue.erase( *agent );
			}
		}
	}

	if ( infoFlags & DDBAGENTINFO_HOST ) { // we sometimes need to intercept this
		UUID host;
		int status;
		this->dStore->AgentGetInfo( agent, DDBAGENTINFO_RHOST | DDBAGENTINFO_RSTATUS, &lds, &nilUUID );
		lds.rewind();
		lds.unpackUUID( &ticket ); // ignore thread
		lds.unpackChar(); // response, has to be ok because we just modified it
		lds.unpackInt32(); // infoFlags
		lds.unpackUUID( &host );
		lds.unpackUUID( &ticket );
		status = lds.unpackInt32();
		lds.unpackUUID( &ticket );
		lds.unlock();

		Log.log( LOG_LEVEL_VERBOSE, "AgentHost::AgentTransferInfoChanged: host changed %s (%s)", Log.formatUUID( LOG_LEVEL_VERBOSE, &host ), Log.formatUUID( LOG_LEVEL_VERBOSE, agent ) );

		if ( status == DDBAGENT_STATUS_FROZEN && host == nilUUID ) 
			this->AgentTransferAvailable( agent );
		if ( status == DDBAGENT_STATUS_FAILED && host == nilUUID ) 
			this->AgentRecoveryAvailable( agent );
	}

	return 0;
}

int AgentHost::AgentTransferAvailable( UUID *agent ) {
	DataStream lds;
	list<UUID>::iterator it;

	if ( this->agentAllocation[*agent] != *this->getUUID() || STATE(AgentBase)->gracefulExit ) {
		return 0; // don't want it
	}

	mapAgentInfo::iterator iAI = this->agentInfo.find(*agent);
	if ( iAI == this->agentInfo.end() ) {
		Log.log( 0, "AgentHost::AgentTransferAvailable: agent info not found %s", Log.formatUUID( 0, agent ) );
		return 1;
	} 

	// -- claim the agent and begin the thaw process --
	Log.log( 0, "AgentHost::AgentTransferAvailable: claiming ownership of agent and thawing (%s)", Log.formatUUID(0,agent) );
		
	

	// set up lock
	UUIDLock *lock = &this->locks[*agent];

	lock->tumbler.clear();
	apb->apbUuidCreate( &lock->key );
	lock->tumbler = this->gmMemberList;
	
	if ( 1 ) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T("df9b1d56-3364-4872-aa6b-54ec7073ec56"), &breakId );

		if ( breakId == *agent )
			int i=0;
	}
	
	// update DDB
	lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
	lds.packUUID( this->getUUID() ); // claim ownership
	lds.packUUID( &lock->key );
	lds.packInt32( DDBAGENT_STATUS_THAWING );
	lds.packUUID( &lock->key );
	lds.rewind();
	this->ddbAgentSetInfo( agent, &lds );
	lds.unlock();

	this->agentInfo[*agent].expectingStatus.push_back( lock->key );

	this->AgentTransferUpdate( &nilUUID, agent, &nilUUID ); // check if we already have a lock (maybe if there are no other hosts)
	
	return 0;
}

int AgentHost::AgentTransferUpdate( UUID *from, UUID *agent, UUID *ticket ) {
	DataStream lds, sds;
	mapLock::iterator iL;
	mapAgentInfo::iterator iA;

	iL = this->locks.find( *agent );

	if ( iL != this->locks.end() && *from != nilUUID ) {
		// throw tumbler
		UUIDLock_Throw( &iL->second, from, ticket );
	}

	iA = this->agentInfo.find( *agent );
	if ( iA == this->agentInfo.end() ) {
		return 1; // what happened here
	}

	if ( *dStore->AgentGetHost( agent ) != *this->getUUID() ) {
		return 0; // we don't know the agent?
	}

	int status = dStore->AgentGetStatus( agent );
	if ( status == DDBAGENT_STATUS_FREEZING ) { 

		// check if we're finished
		if ( iL->second.tumbler.size() == 0 ) { // nothing left to wait for
			Log.log( 0, "AgentHost::AgentTransferUpdate: agent freeze complete, releasing ownership (%s)", Log.formatUUID(0,agent) );
		
			// clear lock
			this->locks.erase( iL );
				
			// update DDB
			lds.reset();
			lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
			lds.packUUID( &nilUUID ); // release ownership
			lds.packUUID( ticket );
			lds.packInt32( DDBAGENT_STATUS_FROZEN );
			lds.packUUID( ticket );
			lds.rewind();
			this->ddbAgentSetInfo( agent, &lds );
			lds.unlock();

			if ( STATE(AgentBase)->gracefulExit ) 
				iA->second.expectingStatus.push_back( *ticket );
				
		}
	
	} else if ( status == DDBAGENT_STATUS_THAWING ) { // finish thaw

		bool noActiveOACs = true;
		if ( this->agentTransferActiveOACList.find( *agent ) != this->agentTransferActiveOACList.end()
			&& !this->agentTransferActiveOACList[*agent].empty() )
			noActiveOACs = false;

		// check if we're finished
		if ( iL->second.tumbler.size() == 0 && noActiveOACs ) {
			if ( iA->second.shellStatus == DDBAGENT_STATUS_READY ) { // ready to send state
				// get state and message queues from DDB
				unsigned int stateSize;
				void *stateData;
				this->dStore->AgentGetInfo( agent, DDBAGENTINFO_RSTATE, &sds, &nilUUID );
				sds.rewind();
				sds.unpackData( sizeof(UUID) ); // ignore thread
				sds.unpackChar(); // response, has to be ok because we just modified it
				sds.unpackInt32(); // infoFlags
				sds.unpackData( sizeof(_timeb) ); // stateTime
				stateSize = sds.unpackUInt32();
				stateData = sds.unpackData( stateSize );
				sds.unlock();

				// start thaw
				lds.reset();
				lds.packUUID( ticket ); // thaw ticket
				lds.packUInt32( stateSize ); // statesize
				lds.packData( stateData, stateSize );
				this->sendMessage( iA->second.shellCon, MSG_AGENT_THAW, lds.stream(), lds.length() );
				lds.unlock();

				Log.log( 0, "AgentHost::AgentTransferUpdate: agent sent state (%s)", Log.formatUUID(0,agent) );

			} else if ( iA->second.shellStatus == DDBAGENT_STATUS_THAWING ) { // ready to send message queues and resume
				
				// clear lock
				this->locks.erase( iL );

				// update agent status
				UUID rticket;
				apb->apbUuidCreate( &rticket );
				lds.reset();
				lds.packInt32( DDBAGENTINFO_STATUS | DDBAGENTINFO_STATE_CLEAR );
				lds.packInt32( DDBAGENT_STATUS_READY );
				lds.packUUID( &rticket );
				lds.rewind();
				this->ddbAgentSetInfo( agent, &lds );
				lds.unlock();

				this->agentInfo[*agent].expectingStatus.push_back( rticket );
				
				Log.log( 0, "AgentHost::AgentTransferUpdate: agent ready to resume (%s)", Log.formatUUID(0,agent) );
			}
		}
	} else if ( status == DDBAGENT_STATUS_RECOVERING ) { // finish recovery

		bool noActiveOACs = true;
		if ( this->agentTransferActiveOACList.find( *agent ) != this->agentTransferActiveOACList.end()
			&& !this->agentTransferActiveOACList[*agent].empty() )
			noActiveOACs = false;

		if ( noActiveOACs && iA->second.shellStatus == DDBAGENT_STATUS_READY ) { // ready to recover
			DataStream sds;
			UUID ticket;
			int backupSize;
			dStore->AgentGetInfo( agent, DDBAGENTINFO_RBACKUP, &sds, &nilUUID );
			sds.rewind();
			sds.unpackData( sizeof(UUID) ); // thread
			sds.unpackChar(); // OK
			sds.unpackInt32(); // infoFlags
			sds.unpackUUID( &ticket );
			backupSize = sds.unpackInt32();

			RPC_STATUS Status;
			UUID noCrashId;
			bool noCrash = false;
			int i = 0;
			UuidFromString( (RPC_WSTR)_T(HWRESOURCE_NO_CRASH), &noCrashId );
			while ( i < AGENTTEMPLATE_MAX_RESOURCE_REQUIREMENTS && !UuidIsNil( &this->agentLibrary[iA->second.type.uuid]->resourceRequirements[i], &Status ) ) {
				if ( noCrashId == this->agentLibrary[iA->second.type.uuid]->resourceRequirements[i] ) {
					noCrash = true;
					break;
				}
				i++;
			}

			if ( backupSize == 0 || noCrash ) {
				Log.log( 0, "AgentHost::AgentTransferUpdate: agent cannot be recovered (%s, backupSize=%d, noCrash=%d)", Log.formatUUID(0,agent), backupSize, noCrash );
				Log.log( 0, "AgentHost::AgentTransferUpdate: GAME OVER" );
				assert( 0 );
			} else {
				
				iA->second.shellStatus = DDBAGENT_STATUS_RECOVERING;

				// start recovery
				lds.reset();
				lds.packUUID( &ticket );
				lds.packInt32( backupSize );
				if ( backupSize )
					lds.packData( sds.unpackData( backupSize ), backupSize );
				this->sendMessage( iA->second.shellCon, MSG_AGENT_RECOVER, lds.stream(), lds.length() );
				lds.unlock();
				sds.unlock();

				Log.log( 0, "AgentHost::AgentTransferUpdate: agent ready to recover (%s)", Log.formatUUID(0,agent) );
			}
		}
	}

	return 0;
}

int AgentHost::AgentTransferStartFreeze( UUID *agent ) {
	DataStream lds;

	mapAgentInfo::iterator iAI = this->agentInfo.find(*agent);
	if ( iAI == this->agentInfo.end() ) {
		Log.log( 0, "AgentHost::AgentTransferStartFreeze: agent info not found %s", Log.formatUUID( 0, agent ) );
		return 1;
	} 

	// find out who we need locks from
	UUIDLock *lock = &this->locks[*agent];

	lock->tumbler.clear();
	apb->apbUuidCreate( &lock->key );
	lock->tumbler = this->gmMemberList;
	lock->tumbler.push_back( *agent ); // add agent because we're waiting for a state update

	// update agent state
	lds.reset();
	lds.packInt32( DDBAGENTINFO_STATUS | DDBAGENTINFO_QUEUE_CLEAR );
	lds.packInt32( DDBAGENT_STATUS_FREEZING );
	lds.packUUID( &lock->key );
	lds.rewind();
	this->ddbAgentSetInfo( agent, &lds );
	lds.unlock();

	this->agentInfo[*agent].expectingStatus.push_back( lock->key );

	return 0;
}

int AgentHost::AgentTransferAbortThaw( UUID *agent ) {
	DataStream lds;

	// merge primary and secondary queues
	lds.reset();
	lds.packInt32( DDBAGENTINFO_QUEUE_MERGE ); 
	lds.rewind();
	this->ddbAgentSetInfo( agent, &lds );
	lds.unlock();

	// append local queue to primary queue
	map<UUID,list<DDBAgent_MSG>,UUIDless>::iterator iQ;
	iQ = this->agentLocalMessageQueue.find( *agent );
	if ( iQ != this->agentLocalMessageQueue.end() ) {
		list<DDBAgent_MSG>::iterator iM;
		for ( iM = iQ->second.begin(); iM != iQ->second.end(); iM++ ) {
			lds.reset();
			lds.packInt32( DDBAGENTINFO_QUEUE_MSG ); 
			lds.packChar( 1 ); // primary
			lds.packUChar( iM->msg );
			lds.packUInt32( iM->len );
			if ( iM->len ) {
				lds.packData( iM->data, iM->len );
				free( iM->data );
			}
			lds.rewind();
			this->ddbAgentSetInfo( agent, &lds );
			lds.unlock();
		}
		this->agentLocalMessageQueue.erase( iQ );
	}

	// find out who we need locks from
	UUIDLock *lock = &this->locks[*agent];

	lock->tumbler.clear();
	apb->apbUuidCreate( &lock->key );
	this->groupList( &this->groupHostId, &lock->tumbler );
	lock->tumbler.remove( *this->getUUID() ); // remove us because we don't need a lock

	// update status
	lds.reset();
	lds.packInt32( DDBAGENTINFO_STATUS );
	lds.packInt32( DDBAGENT_STATUS_FREEZING );
	lds.packUUID( &lock->key );
	lds.rewind();
	this->ddbAgentSetInfo( agent, &lds );
	lds.unlock();

	this->agentInfo[*agent].expectingStatus.push_back( lock->key );

	return 0;
}

//-----------------------------------------------------------------------------
// Agent Recovery

int AgentHost::recvAgentBackup( UUID *agent, DataStream *ds ) {
	DataStream lds;
	UUID ticket;
	_timeb backupT;
	unsigned int backupSize;

	Log.log( LOG_LEVEL_NORMAL, "AgentHost::recvAgentBackup: got backup from %s", Log.formatUUID(LOG_LEVEL_NORMAL,agent) );

	// confirm status
	if ( this->agentInfo[*agent].shellStatus != DDBAGENT_STATUS_ERROR ) {
		Log.log( LOG_LEVEL_NORMAL, "AgentHost::recvAgentBackup: from shell, ignore" );
		return 0;
	}

	ds->unpackUUID( &ticket );

	backupT = *(_timeb *)ds->unpackData( sizeof(_timeb) );
	backupSize = ds->unpackUInt32();

	// update DDB
	lds.reset();
	lds.packInt32( DDBAGENTINFO_BACKUP );
	lds.packData( &backupT, sizeof(_timeb) );
	lds.packUInt32( backupSize );
	lds.packData( ds->unpackData( backupSize ), backupSize );
	lds.rewind();
	this->ddbAgentSetInfo( agent, &lds );
	lds.unlock();

	return 0;
}

int AgentHost::recvAgentRecovered( UUID *agent, UUID *ticket, int result ) {
	DataStream lds;

	mapAgentInfo::iterator iAI = this->agentInfo.find(*agent);
	if ( iAI == this->agentInfo.end() ) {
		Log.log( 0, "AgentHost::recvAgentRecovered: agent info not found %s", Log.formatUUID( 0, agent ) );
		return 1;
	} 

	if ( !result ) { // success
		UUID tId;
		apb->apbUuidCreate( &tId );

		// set agent status
		lds.packInt32( DDBAGENTINFO_STATUS );
		lds.packInt32( DDBAGENT_STATUS_READY );
		lds.packUUID( &tId );
		lds.rewind();
		this->ddbAgentSetInfo( agent, &lds );
		lds.unlock();

		this->agentInfo[*agent].expectingStatus.push_back( tId );

	} else {
		// we're screwed!
		Log.log( 0, "AgentHost::recvAgentRecovered: recovery failed (%s)", Log.formatUUID(0,agent) );
		assert( 0 );
	}

	return 0;
}

int AgentHost::AgentRecoveryAvailable( UUID *agent ) {
	DataStream lds;
	list<UUID>::iterator it;

	if ( this->agentAllocation[*agent] != *this->getUUID() || STATE(AgentBase)->gracefulExit ) {
		return 0; // don't want it
	}

	mapAgentInfo::iterator iAI = this->agentInfo.find(*agent);
	if ( iAI == this->agentInfo.end() ) {
		Log.log( 0, "AgentHost::AgentRecoveryAvailable: agent info not found %s", Log.formatUUID( 0, agent ) );
		return 1;
	} 

	// -- claim the agent and begin the recovery process --
	Log.log( 0, "AgentHost::AgentRecoveryAvailable: claiming ownership of agent and recovering (%s)", Log.formatUUID(0,agent) );
		
	UUID ticket;
	apb->apbUuidCreate( &ticket );

	// update DDB
	lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
	lds.packUUID( this->getUUID() ); // claim ownership
	lds.packUUID( &ticket );
	lds.packInt32( DDBAGENT_STATUS_RECOVERING );
	lds.packUUID( &ticket );
	lds.rewind();
	this->ddbAgentSetInfo( agent, &lds );
	lds.unlock();

	this->agentInfo[*agent].expectingStatus.push_back( ticket );

	mapAtomicMessage::iterator iA;
	this->agentTransferActiveOACList[*agent].clear();
	for ( iA = this->atomicMsgs.begin(); iA != this->atomicMsgs.end(); iA++ ) {
		if ( iA->second.orderedMsg && !iA->second.delivered && iA->second.queue == this->oac_GLOBAL ) 
			this->agentTransferActiveOACList[*agent].push_back( iA->first );
	}

	this->AgentTransferUpdate( &nilUUID, agent, &nilUUID ); // check if we already have a lock (maybe if there are no other hosts)
	
	return 0;
}

int AgentHost::AgentRecoveryAbort( UUID *agent ) {
	DataStream lds;

	// -- release the agent and reset status --
	Log.log( 0, "AgentHost::AgentRecoveryAbort: aborting recovery (%s)", Log.formatUUID(0,agent) );
		
	UUID ticket;
	apb->apbUuidCreate( &ticket );

	// update DDB
	lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
	lds.packUUID( &nilUUID ); // release ownership
	lds.packUUID( &ticket );
	lds.packInt32( DDBAGENT_STATUS_FAILED );
	lds.packUUID( &ticket );
	lds.rewind();
	this->ddbAgentSetInfo( agent, &lds );
	lds.unlock();

	this->agentInfo[*agent].expectingStatus.push_back( ticket );

	return 0;
}

//-----------------------------------------------------------------------------
// Group membership service

int AgentHost::groupJoin( UUID group, UUID agent ) {
	mapAgentHostState::iterator iterHS;
	_timeb tb;

	apb->apb_ftime_s( &tb );

	// distribute
	this->ds.reset();
	this->ds.packUUID( &group );
	this->ds.packUUID( &agent );
	this->ds.packData( &tb, sizeof(_timeb) );

	for ( iterHS = this->hostKnown.begin(); iterHS != this->hostKnown.end(); iterHS++ ) {
		if ( iterHS->second->connection == NULL )
			continue; // we don't have a connection to this host
				
		this->sendMessageEx( iterHS->second->connection, MSGEX(AgentHost_MSGS,MSG_DISTRIBUTE_GROUP_JOIN), this->ds.stream(), this->ds.length() );
	}
	this->ds.unlock();

	// add locally
	this->_groupJoin( group, agent, tb );

	// broadcast join message
	this->ds.reset();
	this->ds.packUUID( &group );
	this->ds.packUUID( &agent );
	this->sendMessage( &group, MSG_GROUP_JOIN, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int AgentHost::_groupJoin( UUID group, UUID agent, _timeb joinTime ) {
	RPC_STATUS Status;
	GroupData *GD;
	list<UUID>::iterator it;
	list<_timeb>::iterator iT;

	// find the group
	GD = &this->groupMembers[group];

	// make sure we don't already have the agent
	for ( it = GD->byJoin.begin(); it != GD->byJoin.end(); it++ ) {
		if ( *it == agent ) {
			break;
		}
	}

	if ( it == GD->byJoin.end() ) { // insert
		// insert into byJoin based on joinTime
		for ( iT = GD->joinTime.begin(), it = GD->byJoin.begin(); iT != GD->joinTime.end(); iT++, it++ ) {
			if ( iT->time > joinTime.time 
			  || ( iT->time == joinTime.time && iT->millitm > joinTime.millitm )
			  || ( iT->time == joinTime.time && iT->millitm == joinTime.millitm && UuidCompare( &*it, &agent, &Status ) > 0 ) )
				break;
		}
		GD->byJoin.insert( it, agent );
		GD->joinTime.insert( iT, joinTime );
	}

	return 0;
}

int AgentHost::groupLeave( UUID group, UUID agent ) {
	mapAgentHostState::iterator iterHS;

	// distribute
	this->ds.reset();
	this->ds.packUUID( &group );
	this->ds.packUUID( &agent );

	for ( iterHS = this->hostKnown.begin(); iterHS != this->hostKnown.end(); iterHS++ ) {
		if ( iterHS->second->connection == NULL )
			continue; // we don't have a connection to this host
				
		this->sendMessageEx( iterHS->second->connection, MSGEX(AgentHost_MSGS,MSG_DISTRIBUTE_GROUP_LEAVE), this->ds.stream(), this->ds.length() );
	}

	// leave locally
	this->_groupLeave( group, agent );

	// broadcast leave message
	this->sendMessage( &group, MSG_GROUP_LEAVE, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int AgentHost::_groupLeave( UUID group, UUID agent ) {
	mapGroupMembers::iterator iGM;
	list<UUID>::iterator it;
	list<_timeb>::iterator iT;

	// find the group
	iGM = this->groupMembers.find( group );

	if ( iGM == this->groupMembers.end() ) { // group not found
		return 0;
	}

	// find agent
	for ( iT = iGM->second.joinTime.begin(), it = iGM->second.byJoin.begin(); iT != iGM->second.joinTime.end(); iT++, it++ ) {
		if ( *it == agent )
			break;
	}

	if ( it != iGM->second.byJoin.end() ) { // found it
		iGM->second.byJoin.erase( it );
		iGM->second.joinTime.erase( iT );
	}

	return 0; 
}

int AgentHost::groupSize( UUID *group ) {
	mapGroupMembers::iterator iG;
	
	iG = this->groupMembers.find( *group );

	if ( iG == this->groupMembers.end() ) { // group not found
		return -1;
	} else {
		return (int)iG->second.byJoin.size();
	}
}

int AgentHost::groupList( UUID *group, list<UUID> *list ) {
	mapGroupMembers::iterator iG;
	std::list<UUID>::iterator it;
	
	iG = this->groupMembers.find( *group );

	if ( iG == this->groupMembers.end() ) { // group not found
		return 1;
	} else {
		for ( it = iG->second.byJoin.begin(); it != iG->second.byJoin.end(); it++ )
			list->push_back( *it );
	}

	return 0;
}

UUID * AgentHost::groupLeader( UUID *group ) {
	mapGroupMembers::iterator iG;
	
	iG = this->groupMembers.find( *group );

	if ( iG == this->groupMembers.end() ) { // group not found
		return &nilUUID;
	} else {
		if ( iG->second.byJoin.size() == 0 )
			return &nilUUID; // no members
		else
			return &iG->second.byJoin.front();
	}
}

int AgentHost::groupMergeSend( spConnection con ) {
	mapGroupMembers::iterator iGM;
	list<UUID>::iterator it;
	list<_timeb>::iterator iT;

	// iterate through groups
	for ( iGM = this->groupMembers.begin(); iGM != this->groupMembers.end(); iGM++ ) {
		this->ds.reset();
		this->ds.packUUID( (UUID*)&iGM->first );
		// iterate through agents
		for ( it = iGM->second.byJoin.begin(), iT = iGM->second.joinTime.begin(); it != iGM->second.byJoin.end(); it++, iT++ ) {
			this->ds.packBool( 1 );
			this->ds.packUUID( &*it );
			this->ds.packData( &*iT, sizeof(_timeb) );
		}	
		this->ds.packBool( 0 ); // end of agents

		// send request
		this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_RGROUP_MERGE), this->ds.stream(), this->ds.length() );

		this->ds.unlock();
	}

	return 0;
};

int AgentHost::groupMergeRequest( DataStream *ds ) {
	mapAgentHostState::iterator iterHS;

	UUID group;
	UUID agent;
	_timeb joinTime;

	GroupData *GD;
	list<UUID>::iterator it;
	list<_timeb>::iterator iT;
	map<UUID, _timeb, UUIDless> joinTimes;

	list<UUID> mergeList;

	ds->unpackUUID( &group );	
	
	GD = &this->groupMembers[group];

	// add local members

	for ( it = GD->byJoin.begin(), iT = GD->joinTime.begin(); it != GD->byJoin.end(); it++, iT++ ) {
		mergeList.push_back( *it );
		joinTimes[*it] = *iT;
	}

	// unpack and repack new merging members
	while ( ds->unpackBool() ) {
		ds->unpackUUID( &agent );
		joinTime = *(_timeb *)ds->unpackData( sizeof(_timeb) );

		// iterate through agents
		for ( it = mergeList.begin(); it != mergeList.end(); it++ ) {
			if ( *it == agent )
				break; 
		}	
		if ( it == mergeList.end() ) {
			// this agent is new
			mergeList.push_back( agent );
			joinTimes[agent] = joinTime;
		} else {
			// this agent appears on both sides
			mergeList.erase( it );
		}
	}

	if ( mergeList.size() == 0 ) {
		return 0; // no changes
	}

	// distribute
	this->ds.reset();
	this->ds.packUUID( &group );
	// iterate through agents
	for ( it = GD->byJoin.begin(); it != GD->byJoin.end(); it++ ) {
		this->ds.packBool( 1 );
		this->ds.packUUID( &*it );
		this->ds.packData( &joinTimes[*it], sizeof(_timeb) );
	}	
	// end of agents
	this->ds.packBool( 0 );

	for ( iterHS = this->hostKnown.begin(); iterHS != this->hostKnown.end(); iterHS++ ) {
		if ( iterHS->second->connection == NULL )
			continue; // we don't have a connection to this host
				
		this->sendMessageEx( iterHS->second->connection, MSGEX(AgentHost_MSGS,MSG_DISTRIBUTE_GROUP_MERGE), this->ds.stream(), this->ds.length() );
	}

	// add locally
	this->ds.rewind();
	this->_groupMerge( &this->ds );
	this->ds.unlock();

	// broadcast merge messages
	for ( it = mergeList.begin(); it != mergeList.end(); it++ ) {
		this->ds.reset();
		this->ds.packUUID( &group );
		this->ds.packUUID( &*it );
		this->sendMessage( &group, MSG_GROUP_MERGE, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
	}	
	
	return 0;
}

int AgentHost::_groupMerge( DataStream *ds ) {
	RPC_STATUS Status;
	UUID group;
	UUID agent;
	_timeb joinTime;

	GroupData *GD;
	list<UUID>::iterator it;
	list<_timeb>::iterator iT;

	ds->unpackUUID( &group );

	GD = &this->groupMembers[group];

	// unpack and repack new merging members
	while ( ds->unpackBool() ) {
		ds->unpackUUID( &agent );
		joinTime = *(_timeb *)ds->unpackData( sizeof(_timeb) );

		// iterate through agents
		for ( it = GD->byJoin.begin(); it != GD->byJoin.end(); it++ ) {
			if ( *it == agent ) {
				break; // we have this agent already
			}
		}	
		if ( it == GD->byJoin.end() ) {
			// insert into byJoin based on joinTime
			for ( iT = GD->joinTime.begin(), it = GD->byJoin.begin(); iT != GD->joinTime.end(); iT++, it++ ) {
				if ( iT->time > joinTime.time 
				  || ( iT->time == joinTime.time && iT->millitm > joinTime.millitm )
				  || ( iT->time == joinTime.time && iT->millitm == joinTime.millitm && UuidCompare( &*it, &agent, &Status ) > 0 ) )
					break;
			}
			GD->byJoin.insert( it, agent );
			GD->joinTime.insert( iT, joinTime );
		}
	}

	return 0;
}

int AgentHost::sendGroupMessage( UUID *group, unsigned char message, char *data, unsigned int len, unsigned int msgSize ) {
	mapGroupMembers::iterator iGM;
	std::list<UUID>::iterator iA;
	//mapGroupMembersByHost::iterator iH;

	//mapAgentHostState::iterator iHS;

	// find the group
	iGM = this->groupMembers.find( *group );

	if ( iGM == this->groupMembers.end() ) { // group not found
		return 0;
	}

	// send to members
	for ( iA = iGM->second.byJoin.begin(); iA != iGM->second.byJoin.end(); iA++ ) {
		this->sendAgentMessage( (UUID *)&*iA, message, data, len, msgSize );
	}

	return 0;
}

int AgentHost::sendAgentMessage( UUID *agent, unsigned char message, char *data, unsigned int len, unsigned int msgSize ) {
	DataStream lds, hds;
	int status;
	bool fromLocal = false; // message is from one of our local agents
	bool toLocal = false; // message is to one of our local agents
	spConnection fwdCon = NULL;
	int queueMsg = 0; // 1 - primary, 2 - secondary, 3 - local

	if ( msgSize == -4 ) {
		if ( message >= MSG_COMMON ) {
			Log.log( 0, "AgentHost::sendAgentMessage: message out of range (%d >= MSG_COMMON), perhaps you should be using sendMessageEx?", message );
			return 1;
		}

		msgSize = MSG_SIZE[message];

		if ( msgSize < (unsigned int)-3 && msgSize != len ) {
			Log.log( 0, "AgentHost::sendAgentMessage: message wrong length, [%d: %d != %d]", message, msgSize, len );
			return 1;
		}
	}

	// check if it's us
	if ( *agent == *this->getUUID() ) {
		this->_queueLocalMessage( message, data, len ); 
		return 0;
	}

	// check if it is for a host
	mapAgentHostState::iterator iterAHS = this->hostKnown.find( *agent );
	if ( iterAHS != this->hostKnown.end() ) {
		this->conSendMessage( iterAHS->second->connection, message, data, len, msgSize );
		return 0; // message handled
	}
	
	// check if it is for an agent
	mapAgentInfo::iterator iterAI = this->agentInfo.find(*agent);
	if (  iterAI == this->agentInfo.end() ) { // agent not found!
		return 1; // unknown agent
	} 

	if ( *dStore->AgentGetHost( agent ) == *this->getUUID() )
		toLocal = true;
	
	// check agent status to handle message appropriately
	status = dStore->AgentGetStatus( agent );
	if ( status == DDBAGENT_STATUS_READY || status == DDBAGENT_STATUS_SPAWNING ) {
		if ( toLocal ) { // send it on
			fwdCon = iterAI->second.con;
		} else { // forward to appropriate host
			mapAgentHostState::iterator iterAHS = this->hostKnown.find( *dStore->AgentGetHost( agent ) );
			if ( iterAHS == this->hostKnown.end() ) {
				Log.log( 0, "AgentHost::sendAgentMessage: received message to agent (%s) but no connection to host (%s)", Log.formatUUID(0, agent), Log.formatUUID(0,dStore->AgentGetHost( agent )) );
				return 0; // message handled
			}

			// TODO check connection graph if we don't have connection to this host
			fwdCon = iterAHS->second->connection;
		}
	} else if ( status == DDBAGENT_STATUS_FREEZING ) {
		queueMsg = 2; // forward to secondary queue
	} else if ( status == DDBAGENT_STATUS_FROZEN ) {
		queueMsg = 2; // forward to secondary queue
	} else if ( status == DDBAGENT_STATUS_THAWING ) {
		if ( toLocal ) { // add to local queue
			queueMsg = 3;
		} else { // forward to appropriate host
			mapAgentHostState::iterator iterAHS = this->hostKnown.find( *dStore->AgentGetHost( (UUID *)&iterAI->first ) );
			if ( iterAHS == this->hostKnown.end() ) {
				Log.log( 0, "AgentHost::sendAgentMessage: received message to agent (%s) but no connection to host (%s)", Log.formatUUID(0,(UUID *)&iterAI->first), Log.formatUUID(0,dStore->AgentGetHost( (UUID *)&iterAI->first )) );
				return 0; // message handled
			}

			// TODO check connection graph if we don't have connection to this host
			fwdCon = iterAHS->second->connection;
		}
	} else if ( status == DDBAGENT_STATUS_FAILED ) {
		// discard
		return 0; // message handled
	} else if ( status == DDBAGENT_STATUS_RECOVERING ) {
		if ( toLocal ) { // we can send messages to our shell
			fwdCon = iterAI->second.shellCon;
		}
	} else {
		Log.log( 0, "AgentHost::sendAgentMessage: received message to agent with unacceptable status %d (%s)", status, Log.formatUUID(0,(UUID *)&iterAI->first) );
		return 0; // message handled
	}

/*	// TEMP
	Log.log( 0, "AgentHost::sendAgentMessage: message to %s (msg %d, len %d]", Log.formatUUID( 0, agent ), message, len );
	Log.log( 0, "curHost %s status %d toLocal %d queueMsg %d", Log.formatUUID( 0, dStore->AgentGetHost( agent ) ), status, toLocal, queueMsg );
	if ( fwdCon ) {
		Log.log( 0, "fwdCon con %s uuid %s sendBuf %d, sendBufLen %d", Log.formatUUID( 0, &fwdCon->index ), Log.formatUUID( 0, &fwdCon->uuid ), (fwdCon->sendBuf == NULL ? 0:1 ), fwdCon->sendBufLen );	
	}
*/
	// forward/queue message
	if ( fwdCon ) {
		if ( toLocal ) {
			this->conSendMessage( fwdCon, message, data, len, msgSize );
		} else {
			// construct forward header
			char header[64], *headerPtr = header;
			unsigned int headerLen = 0;
			unsigned int combinedLen;

			// start off headerLen as just the addresses, then add msg id and msg size later
			headerLen = sizeof(UUID) + 1;

			if ( msgSize == -1 ) {
				combinedLen = headerLen + 1 + 1 + len;
			} else if ( msgSize == -2 ) {
				combinedLen = headerLen + 1 + 4 + len;
			} else if ( msgSize == -3 ) {
				if ( len < 0xFF ) combinedLen = headerLen + 1 + 1 + len;
				else combinedLen = headerLen + 1 + 5 + len;
			} else {
				combinedLen = headerLen + 1 + len;
			}

			// msg id
			*headerPtr = MSG_FORWARD;
			headerPtr++;
			headerLen++;
			
			// msg size -3 mode
			if ( combinedLen < 0xFF ) {
				*headerPtr = combinedLen;
				headerPtr++;
				headerLen++;
			} else {
				*headerPtr = (unsigned char)0xFF;
				headerPtr++;
				memcpy( headerPtr, (void *)&combinedLen, 4 );
				headerPtr += 4;
				headerLen += 5;
			}

			// msg data
			memcpy( headerPtr, agent, sizeof(UUID) );
			headerPtr += sizeof(UUID);
			*headerPtr = false;

			this->conSendMessage( fwdCon, message, data, len, msgSize, header, headerLen );
		}
		return 0; // message handled
	}

	// parse message to queue
	if ( queueMsg == 1 ) { // primary queue
		lds.reset();
		lds.packInt32( DDBAGENTINFO_QUEUE_MSG ); 
		lds.packChar( 1 ); // primary
		lds.packUChar( message );
		lds.packUInt32( len );
		lds.packData( data, len );
		lds.rewind();
		this->ddbAgentSetInfo( (UUID *)&iterAI->first, &lds );
		lds.unlock();
	} else if ( queueMsg == 2 ) { // secondary queue
		lds.reset();
		lds.packInt32( DDBAGENTINFO_QUEUE_MSG ); 
		lds.packChar( 0 ); // secondary
		lds.packUChar( message );
		lds.packUInt32( len );
		lds.packData( data, len );
		lds.rewind();
		this->ddbAgentSetInfo( (UUID *)&iterAI->first, &lds );
		lds.unlock();
	} else if ( queueMsg == 3 ) { // local queue
		DDBAgent_MSG msgInfo;
		msgInfo.msg = message;
		msgInfo.len = len;
		msgInfo.data = (char *)malloc(len);
		if ( !msgInfo.data ) {
			Log.log( 0, "AgentHost::sendAgentMessage: malloc error adding to local queue of agent %s", Log.formatUUID(0,(UUID *)&iterAI->first) );
			return 0; // message handled
		}
		memcpy( msgInfo.data, data, len );
		this->agentLocalMessageQueue[iterAI->first].push_back( msgInfo );
	}

	return 0;
}

int AgentHost::conFDSuspect( spConnection con ) {

	//this->agentLost( &con->index, AGENT_LOST_SUSPECTED );

	return 0;
}

//-----------------------------------------------------------------------------
// DDB

int AgentHost::ddbAddWatcher( UUID *id, int type ) {

	// send global state OAC
	this->ds.reset();
	this->ds.packUUID( id );
	this->ds.packInt32( type );
	this->globalStateTransaction( OAC_DDB_WATCH_TYPE, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int AgentHost::_ddbAddWatcher( UUID *id, int type ) {
	DataStream lds;
	int i, flag;
	mapDDBWatchers::iterator watchers;
	std::list<UUID> *watcherList;

	for ( i=0; i<4*8; i++ ) {
		flag = 0x0001 << i;
		if ( type & flag ) {
			watchers = this->DDBWatchers.find( flag );
			if ( watchers == this->DDBWatchers.end() ) {
				watcherList = new std::list<UUID>;
				this->DDBWatchers[flag] = watcherList;
			} else {
				watcherList = watchers->second;
			}
			watcherList->push_back( *id );
		}
	}

	// notify watcher that they were registered
	mapAgentInfo::iterator iterAI = this->agentInfo.find(*id);
	if ( iterAI == this->agentInfo.end() ) { // unknown agent
		return 1;
	}
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packInt32( type );
	lds.packUUID( &nilUUID );
	lds.packChar( DDBE_WATCH_TYPE );
	if ( *dStore->AgentGetHost( id ) == *this->getUUID() ) {
		this->sendAgentMessage( id, MSG_DDB_NOTIFY, lds.stream(), lds.length() );
	} else if ( *dStore->AgentGetHost( id ) == nilUUID && dStore->AgentGetStatus( id ) == DDBAGENT_STATUS_FROZEN ) { // store notification
		DDBAgent_MSG m;
		m.msg = MSG_DDB_NOTIFY;
		m.data = NULL;
		m.len = lds.length();
		if ( m.len ) {
			m.data = (char *)malloc(m.len);
			memcpy( m.data, lds.stream(), m.len );
		}
		this->ddbNotificationQueue[*id].push_back( m );
	}

	return 0;
}

int AgentHost::ddbAddWatcher( UUID *id, UUID *item ) {

	// distribute
	this->ds.reset();
	this->ds.packUUID( id );
	this->ds.packUUID( item );
	this->globalStateTransaction( OAC_DDB_WATCH_ITEM, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int AgentHost::_ddbAddWatcher( UUID *id, UUID *item ) {
	DataStream lds;
	mapDDBItemWatchers::iterator watchers = this->DDBItemWatchers.find( *item );
	std::list<UUID> *watcherList;

	if ( watchers == this->DDBItemWatchers.end() ) {
		watcherList = new std::list<UUID>;
		this->DDBItemWatchers[*item] = watcherList;
	} else {
		watcherList = watchers->second;
	}
	watcherList->push_back( *id );

	// notify watcher that they were registered
	mapAgentInfo::iterator iterAI = this->agentInfo.find(*id);
	if ( iterAI == this->agentInfo.end() ) { // unknown agent
		return 1;
	}
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packInt32( 0 );
	lds.packUUID( item );
	lds.packChar( DDBE_WATCH_ITEM );
	if ( *dStore->AgentGetHost( id ) == *this->getUUID() ) {
		this->sendAgentMessage( id, MSG_DDB_NOTIFY, lds.stream(), lds.length() );
	} else if ( *dStore->AgentGetHost( id ) == nilUUID && dStore->AgentGetStatus( id ) == DDBAGENT_STATUS_FROZEN ) { // store notification
		DDBAgent_MSG m;
		m.msg = MSG_DDB_NOTIFY;
		m.data = NULL;
		m.len = lds.length();
		if ( m.len ) {
			m.data = (char *)malloc(m.len);
			memcpy( m.data, lds.stream(), m.len );
		}
		this->ddbNotificationQueue[*id].push_back( m );
	}

	return 0;
}

int AgentHost::ddbRemWatcher( UUID *id, bool immediate ) {
	int i, flag, type;
	mapDDBWatchers::iterator watchers;
	mapDDBItemWatchers::iterator itemWatchers;
	std::list<UUID>::iterator iter;
	
	type = 0;

	// check our lists by type
	for ( i=0; i<4*8; i++ ) {
		flag = 0x0001 << i;

		watchers = this->DDBWatchers.find( flag );

		if ( watchers == this->DDBWatchers.end() ) {
			continue; // no watchers for this type!
		}

		iter = watchers->second->begin();
		while ( iter != watchers->second->end() ) {
			if ( (*iter) == *id ) {
				type |= flag;
				break;
			}
			iter++;
		}
	}

	if ( type ) { // remove the watcher
		if ( immediate ) // we're already in a global state transaction
			this->_ddbRemWatcher( id, type );
		else
			this->ddbRemWatcher( id, type );
	}

	// check our lists by item
	itemWatchers = this->DDBItemWatchers.begin();
	while ( itemWatchers != this->DDBItemWatchers.end() ) {
		iter = itemWatchers->second->begin();
		while ( iter != itemWatchers->second->end() ) {
			if ( (*iter) == *id ) {
				if ( immediate ) // we're already in a global state transaction
					this->_ddbRemWatcher( id, (UUID *)&itemWatchers->first );
				else	
					this->ddbRemWatcher( id, (UUID *)&itemWatchers->first );
				break;
			}
			iter++;
		}
		itemWatchers++;
	}

	return 0;
}

int AgentHost::ddbRemWatcher( UUID *id, int type ) {

	// distribute
	this->ds.reset();
	this->ds.packUUID( id );
	this->ds.packInt32( type );
	this->globalStateTransaction( OAC_DDB_STOP_WATCHING_TYPE, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int AgentHost::_ddbRemWatcher( UUID *id, int type ) {
	int i, flag;
	mapDDBWatchers::iterator watchers;
	std::list<UUID>::iterator iter;
	
	for ( i=0; i<4*8; i++ ) {
		flag = 0x0001 << i;
		if ( type & flag ) {
			watchers = this->DDBWatchers.find( flag );

			if ( watchers == this->DDBWatchers.end() ) {
				continue; // no watchers for this type!
			}

			iter = watchers->second->begin();
			while ( iter != watchers->second->end() ) {
				if ( (*iter) == *id ) {
					watchers->second->erase( iter );
					break;
				}
				iter++;
			}
		}
	}

	return 0; // watcher not found!
}

int AgentHost::ddbRemWatcher( UUID *id, UUID *item ) {

	// distribute
	this->ds.reset();
	this->ds.packUUID( id );
	this->ds.packUUID( item );
	this->globalStateTransaction( OAC_DDB_STOP_WATCHING_ITEM, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}
	
int AgentHost::_ddbRemWatcher( UUID *id, UUID *item ) {
	mapDDBItemWatchers::iterator watchers = this->DDBItemWatchers.find( *item );
	std::list<UUID>::iterator iter;

	if ( watchers == this->DDBItemWatchers.end() ) {
		return 1; // no watchers for this item!
	}

	iter = watchers->second->begin();
	while ( iter != watchers->second->end() ) {
		if ( (*iter) == *id ) {
			watchers->second->erase( iter );
			return 0;
		}
		iter++;
	} 

	return 1; // watcher not found!
}
/*
int AgentHost::ddbClearWatchers( UUID *id ) {
	mapDDBItemWatchers::iterator watchers = this->DDBItemWatchers.find( *id );
	std::list<UUID>::iterator iter;

	if ( watchers == this->DDBItemWatchers.end() ) {
		return 0; // no watchers for this item
	}

	// distribute
	this->ds.reset();
	this->ds.packUUID( id );
	this->globalStateTransaction( OAC_DDB_CLEAR_WATCHERS, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}
*/
int AgentHost::_ddbClearWatchers( UUID *id ) {
	mapDDBItemWatchers::iterator watchers = this->DDBItemWatchers.find( *id );
	std::list<UUID>::iterator iter;

	if ( watchers == this->DDBItemWatchers.end() ) {
		return 0; // no watchers for this item
	}

	watchers->second->clear();
	
	delete watchers->second;

	this->DDBItemWatchers.erase( watchers );

	return 0;
}

int AgentHost::_ddbClearWatchers() {
	mapDDBWatchers::iterator watchers;
	mapDDBItemWatchers::iterator iwatchers;
	std::list<UUID>::iterator iter;

	while ( !this->DDBWatchers.empty() ) {
		watchers = this->DDBWatchers.begin();
		watchers->second->clear();
		delete watchers->second;
		this->DDBWatchers.erase( watchers );
	}

	while ( !this->DDBItemWatchers.empty() ) {
		iwatchers = this->DDBItemWatchers.begin();
		iwatchers->second->clear();
		delete iwatchers->second;
		this->DDBItemWatchers.erase( iwatchers );
	}

	return 0;
}

int AgentHost::_ddbNotifyWatchers( UUID *key, int type, char evt, UUID *id, void *data, int len ) {
	std::list<UUID>::iterator iter;
	mapAgentInfo::iterator iterAI;
	
	// by type
	mapDDBWatchers::iterator watchers = this->DDBWatchers.find( type );
	if ( watchers != this->DDBWatchers.end() && !watchers->second->empty() ) {
		this->ds.reset();
		this->ds.packUUID( key );
		this->ds.packInt32( type );
		this->ds.packUUID( id );
		this->ds.packChar( evt );
		if ( len )
			this->ds.packData( data, len );
		iter = watchers->second->begin();
		while ( iter != watchers->second->end() ) {
			mapAgentInfo::iterator iterAI = this->agentInfo.find(*iter);
			if ( iterAI == this->agentInfo.end() ) { // unknown agent
				this->ds.unlock();
				return 1;
			}
			if ( *dStore->AgentGetHost( &*iter ) == *this->getUUID() ) {
				this->sendAgentMessage( &*iter, MSG_DDB_NOTIFY, this->ds.stream(), this->ds.length() );
			} else if ( *dStore->AgentGetHost( &*iter ) == nilUUID && dStore->AgentGetStatus( &*iter ) == DDBAGENT_STATUS_FROZEN ) { // store notification
				DDBAgent_MSG m;
				m.msg = MSG_DDB_NOTIFY;
				m.data = NULL;
				m.len = this->ds.length();
				if ( m.len ) {
					m.data = (char *)malloc(m.len);
					memcpy( m.data, this->ds.stream(), m.len );
				}
				this->ddbNotificationQueue[*iter].push_back( m );
			}

			iter++;
		}
		this->ds.unlock();
	}


	// by item
	if ( *id != nilUUID ) {
		mapDDBItemWatchers::iterator iwatchers = this->DDBItemWatchers.find( *id );
		if ( iwatchers != this->DDBItemWatchers.end() && !iwatchers->second->empty() ) {
			if ( watchers == this->DDBWatchers.end() || watchers->second->empty() ) { // data hasn't been packed yet
				this->ds.reset();
				this->ds.packUUID( key );
				this->ds.packInt32( type );
				this->ds.packUUID( id );
				this->ds.packChar( evt );
				if ( len )
					this->ds.packData( data, len );
			}
			iter = iwatchers->second->begin();
			while ( iter != iwatchers->second->end() ) {
				mapAgentInfo::iterator iterAI = this->agentInfo.find(*iter);
				if ( iterAI == this->agentInfo.end() ) { // unknown agent
					this->ds.unlock();
					return 1;
				}
				if ( *dStore->AgentGetHost( &*iter ) == *this->getUUID() ) {
					this->sendAgentMessage( &*iter, MSG_DDB_NOTIFY, this->ds.stream(), this->ds.length() );
				} else if ( *dStore->AgentGetHost( &*iter ) == nilUUID && dStore->AgentGetStatus( &*iter ) == DDBAGENT_STATUS_FROZEN ) { // store notification
					DDBAgent_MSG m;
					m.msg = MSG_DDB_NOTIFY;
					m.data = NULL;
					m.len = this->ds.length();
					if ( m.len ) {
						m.data = (char *)malloc(m.len);
						memcpy( m.data, this->ds.stream(), m.len );
					}
					this->ddbNotificationQueue[*iter].push_back( m );
				}

				iter++;
			}
			this->ds.unlock();
		}
	}

	return 0;
}


int AgentHost::ddbAddMirror( UUID *id ) {

	// distribute
/*	this->ds.reset();
	this->ds.packUUID( id );
	this->_distribute( AgentHost_MSGS::MSG_DISTRIBUTE_ADD_MIRROR, this->ds.stream(), this->ds.length() );
	this->ds.unlock();
*/
	this->_ddbAddMirror( id ); // add locally

	return 0;
}

int AgentHost::_ddbAddMirror( UUID *id ) {
	spConnection con;
	UUID *forward;

	// find connection
	mapAgentInfo::iterator iterAI = this->agentInfo.find(*id);
	if ( iterAI == this->agentInfo.end() ) { // unknown agent
		return 1;
	} else if ( iterAI->second.con != NULL ) { // this is one of our agents
		con = iterAI->second.con;
		forward = NULL;
	} else {  // forward this message to the appropriate host
/* should never happen		mapAgentHostState::iterator iterAHS = this->hostKnown.find( *dStore->AgentGetHost( (UUID *)&iterAI->first ) );
		if ( iterAHS == this->hostKnown.end() ) {
			return 1; // unknown host
		}

		// TODO check connection graph if we don't have connection to this host
		con = iterAHS->second->connection;
		forward = id;
*/	}

	// enumerate
	this->ddbEnumerate( con, forward );
	
	this->DDBMirrors.push_back( *id );

	return 0;
}

int AgentHost::ddbRemMirror( UUID *id ) {

	// distribute
/*	this->ds.reset();
	this->ds.packUUID( id );
	this->_distribute( AgentHost_MSGS::MSG_DISTRIBUTE_REM_MIRROR, this->ds.stream(), this->ds.length() );
	this->ds.unlock();
*/
	this->_ddbRemMirror( id ); // remove locally

	return 0;
}

int AgentHost::_ddbRemMirror( UUID *id ) {
	std::list<UUID>::iterator iter;

	iter = this->DDBMirrors.begin();
	while ( iter != this->DDBMirrors.end() ) {
		if ( *iter == *id ) 
			break;
		iter++;
	}

	if ( iter != this->DDBMirrors.end() ) {
		this->DDBMirrors.erase( iter );
	}
	
	return 0;
}

int AgentHost::ddbEnumerate( spConnection con, UUID *forward ) {

	if ( this->ddbEnumerateAgents( con, forward ) )
		return 1;

	if ( this->ddbEnumerateRegions( con, forward ) )
		return 1;
	
	if ( this->ddbEnumerateLandmarks( con, forward ) )
		return 1;
	
	if ( this->ddbEnumeratePOGs( con, forward ) )
		return 1;
	
	if ( this->ddbEnumerateParticleFilters( con, forward ) )
		return 1;
	
	if ( this->ddbEnumerateAvatars( con, forward ) )
		return 1;

	if ( this->ddbEnumerateSensors( con, forward ) )
		return 1;

	return 0;
}

int AgentHost::_ddbParseEnumerate( DataStream *ds ) {
	DataStream lds;
	int type;
	UUID parsedId;

	while ( (type = ds->unpackInt32()) != DDB_INVALID ) {
		
		switch ( type ) {
		case DDB_AGENT:
			{
				AgentType agentType;
				UUID spawnThread;
				int activationMode;
				char priority;
				if ( this->dStore->ParseAgent( ds, &parsedId ) > 0 )
					return 1;
				this->dStore->AgentGetInfo( &parsedId, DDBAGENTINFO_RTYPE | DDBAGENTINFO_RSPAWNINFO, &lds, &nilUUID );
				lds.rewind();
				lds.unpackData(sizeof(UUID)); // thread
				lds.unpackChar(); // success
				lds.unpackInt32(); // infoFlags
				strcpy_s( agentType.name, sizeof(agentType.name), lds.unpackString() );
				lds.unpackUUID( &agentType.uuid );
				agentType.instance = lds.unpackChar();
				lds.unpackUUID( &spawnThread );
				activationMode = lds.unpackInt32();
				priority = lds.unpackChar();
				lds.unlock();
				this->_addAgent2( &parsedId, &agentType, &spawnThread, activationMode, priority );
			}
			break;
		case DDB_REGION:
			if ( this->dStore->ParseRegion( ds, &parsedId ) > 0 )
				return 1;
			break;
		case DDB_LANDMARK:
			if ( this->dStore->ParseLandmark( ds, &parsedId ) > 0 )
				return 1;
			break;
		case DDB_MAP_PROBOCCGRID:
			if ( this->dStore->ParsePOG( ds, &parsedId ) > 0 )
				return 1;
			break;
		case DDB_PARTICLEFILTER:
			if ( this->dStore->ParseParticleFilter( ds, &parsedId ) > 0 )
				return 1;
			break;
		case DDB_AVATAR:
			if ( this->dStore->ParseAvatar( ds, &parsedId ) > 0 )
				return 1;
			break;
		case DDB_SENSOR_SONAR:
		case DDB_SENSOR_CAMERA:
		case DDB_SENSOR_SIM_CAMERA:
			if ( this->dStore->ParseSensor( ds, &parsedId ) > 0 )
				return 1;
			break;
		default:
			// Unhandled type?!
			return 1;
		}
	}

	return 0;
}

int AgentHost::ddbEnumerateAgents( spConnection con, UUID *forward ) {
	int count;

	this->ds.reset();

	_timeb packStart, packEnd;
	apb->apb_ftime_s( &packStart );

	count = this->dStore->EnumerateAgents( &this->ds ); 

	apb->apb_ftime_s( &packEnd );
	unsigned int dt = (unsigned int)(1000*(packEnd.time - packStart.time) + packEnd.millitm - packStart.millitm);
	Log.log( LOG_LEVEL_NORMAL, "AgentHost::ddbEnumerateAgents: enumation took %d ms, size %d", dt, this->ds.length() );


	if ( count > 0 ) {
		// pack invalid to close the stream
		this->ds.packInt32( DDB_INVALID );
		// send
		this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_DDB_ENUMERATE), this->ds.stream(), this->ds.length(), forward );
	}

	this->ds.unlock();

	return 0;
}

int AgentHost::ddbAddAgent( UUID *id, UUID *parentId, AgentType *agentType, UUID *spawnThread, float parentAffinity, char priority, float processCost, int activationMode ) {

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( id );
	this->ds2.packUUID( parentId );
	this->ds2.packData( agentType, sizeof(AgentType) );
	this->ds2.packUUID( spawnThread );
	this->ds2.packFloat32( parentAffinity );
	this->ds2.packChar( priority );
	this->ds2.packFloat32( processCost );
	this->ds2.packInt32( activationMode );
	this->globalStateTransaction( OAC_DDB_ADDAGENT, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

	//this->dStore->AddAgent( id, parentId, agentType->name, &agentType->uuid, agentType->instance ); // add locally

	// notify watchers
	//this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_ADD, id );

	return 0;
}

int AgentHost::ddbRemoveAgent( UUID *id ) {

	// distribute
	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( id );
	this->globalStateTransaction( OAC_DDB_REMAGENT, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	//this->dStore->RemoveAgent( id ); // remove locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_REM, id );
//	this->ddbClearWatchers( id );

	return 0;
}

int AgentHost::ddbAgentGetInfo( UUID *id, int infoFlags, spConnection con, UUID *thread ) {
	DataStream lds;

	this->dStore->AgentGetInfo( id, infoFlags, &lds, thread );

	this->sendAgentMessage( &con->uuid, MSG_RESPONSE, lds.stream(), lds.length() );

	lds.unlock();
	
	return 0;
}

int AgentHost::ddbAgentSetInfo( UUID *id, DataStream *ds ) {
	DataStream lds;

	// distribute
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packUUID( id );
	lds.packData( ds->stream(), ds->length() );
	this->globalStateTransaction( OAC_DDB_AGENTSETINFO, lds.stream(), lds.length() );
	lds.unlock();

	return 0;
}

int AgentHost::ddbVisAddPath( UUID *agentId, int id, int count, float *x, float *y ) {

	if (1) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T("a4b983ad-ae53-4a36-b674-1e4de6951986"), &breakId );

		if ( breakId == *agentId )
			int i=0;
	}

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( agentId );
	this->ds2.packInt32( id );
	this->ds2.packInt32( count );
	this->ds2.packData( x, sizeof(float)*count );
	this->ds2.packData( y, sizeof(float)*count );
	this->globalStateTransaction( OAC_DDB_VIS_ADDPATH, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

//	this->dStore->VisAddPath( agentId, id, count, x, y ); // add locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_VIS_ADDPATH, agentId, &id, sizeof(int) );

	return 0;
}

int AgentHost::ddbVisRemovePath( UUID *agentId, int id ) {

	if (1) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T("a4b983ad-ae53-4a36-b674-1e4de6951986"), &breakId );

		if ( breakId == *agentId )
			int i=0;
	}

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( agentId );
	this->ds2.packInt32( id );
	this->globalStateTransaction( OAC_DDB_VIS_REMPATH, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

//	this->dStore->VisRemovePath( agentId, id ); // remove locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_VIS_REMPATH, agentId, &id, sizeof(int) );

	return 0;
}

int AgentHost::ddbVisExtendPath( UUID *agentId, int id, int count, float *x, float *y ) {
	
	return 0;
}

int AgentHost::ddbVisUpdatePath( UUID *agentId, int id, int count, int *nodes, float *x, float *y ) {
	
	return 0;
}

int AgentHost::ddbVisAddStaticObject( UUID *agentId, int id, float x, float y, float r, float s, int count, int *paths, float *colours[3], float *lineWidths, bool solid, char *name ) {
	int i;

	if (1) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T("a4b983ad-ae53-4a36-b674-1e4de6951986"), &breakId );

		if ( breakId == *agentId )
			int i=0;
	}

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( agentId );
	this->ds2.packInt32( id );
	this->ds2.packFloat32( x );
	this->ds2.packFloat32( y );
	this->ds2.packFloat32( r );
	this->ds2.packFloat32( s );
	this->ds2.packInt32( count );
	this->ds2.packData( paths, sizeof(int)*count );
	for ( i=0; i<count; i++ ) {
		this->ds2.packData( colours[i], sizeof(float)*3);
	}
	this->ds2.packData( lineWidths, count*sizeof(float));
	this->ds2.packBool( solid );
	this->ds2.packString( name );
	this->globalStateTransaction( OAC_DDB_VIS_ADDOBJECT, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

	//this->dStore->VisAddStaticObject( agentId, id, x, y, r, s, count, paths, colours, lineWidths, solid, name ); // add locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_VIS_ADDOBJECT, agentId, &id, sizeof(int) );

	return 0;
}


int AgentHost::ddbVisRemoveObject( UUID *agentId, int id ) {

	if (1) { // debug
		UUID breakId;
		UuidFromString( (RPC_WSTR)_T("a4b983ad-ae53-4a36-b674-1e4de6951986"), &breakId );

		if ( breakId == *agentId )
			int i=0;
	}

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( agentId );
	this->ds2.packInt32( id );
	this->globalStateTransaction( OAC_DDB_VIS_REMOBJECT, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

//	this->dStore->VisRemoveObject( agentId, id ); // remove locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_VIS_REMOBJECT, agentId, &id, sizeof(int) );

	return 0;
}


int AgentHost::ddbVisUpdateObject( UUID *agentId, int id, float x, float y, float r, float s ) {

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( agentId );
	this->ds2.packInt32( id );
	this->ds2.packFloat32( x );
	this->ds2.packFloat32( y );
	this->ds2.packFloat32( r );
	this->ds2.packFloat32( s );
	this->globalStateTransaction( OAC_DDB_VIS_UPDATEOBJECT, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

//	this->dStore->VisUpdateObject( agentId, id, x, y, r, s ); // update locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_VIS_UPDATEOBJECT, agentId, &id, sizeof(int) );

	return 0;
}

int AgentHost::ddbVisSetObjectVisible( UUID *agentId, int id, char visible )  {

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( agentId );
	this->ds2.packInt32( id );
	this->ds2.packChar( visible );
	this->globalStateTransaction( OAC_DDB_VIS_SETOBJECTVISIBLE, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

//	this->dStore->VisSetObjectVisible( agentId, id, visible ); // set locally

	// notify watchers
/*	this->ds2.reset();
	this->ds2.packInt32( id );
	this->ds2.packChar( MSG_DDB_VIS_SETOBJECTVISIBLE );
	this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_VIS_UPDATEOBJECT, agentId, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();
*/
	return 0;
}

int AgentHost::ddbVisClearAll( UUID *agentId, char clearPaths ) {
	DataStream lds;

	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packUUID( agentId );
	lds.packChar( clearPaths );
	this->globalStateTransaction( OAC_DDB_VIS_CLEAR_ALL, lds.stream(), lds.length() );
	lds.unlock();

	return 0;
}

int AgentHost::ddbEnumerateRegions( spConnection con, UUID *forward ) {
	int count;

	this->ds.reset();

	_timeb packStart, packEnd;
	apb->apb_ftime_s( &packStart );

	count = this->dStore->EnumerateRegions( &this->ds );

	apb->apb_ftime_s( &packEnd );
	unsigned int dt = (unsigned int)(1000*(packEnd.time - packStart.time) + packEnd.millitm - packStart.millitm);
	Log.log( LOG_LEVEL_NORMAL, "AgentHost::ddbEnumerateRegions: enumation took %d ms, size %d", dt, this->ds.length() );


	if ( count > 0 ) {
		// pack invalid to close the stream
		this->ds.packInt32( DDB_INVALID );
		// send
		this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_DDB_ENUMERATE), this->ds.stream(), this->ds.length(), forward );
	}
	this->ds.unlock();

	return 0;
}

int AgentHost::ddbAddRegion( UUID *id, float x, float y, float w, float h ) {

	// distribute
	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( id );
	this->ds.packFloat32( x );
	this->ds.packFloat32( y );
	this->ds.packFloat32( w );
	this->ds.packFloat32( h );
	this->globalStateTransaction( OAC_DDB_ADDREGION, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

//	this->dStore->AddRegion( id, x, y, w, h ); // add locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_REGION, DDBE_ADD, id );

	return 0;
}

int AgentHost::ddbRemoveRegion( UUID *id ) {

	// distribute
	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( id );
	this->globalStateTransaction( OAC_DDB_REMREGION, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

//	this->dStore->RemoveRegion( id ); // remove locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_REGION, DDBE_REM, id );
//	this->ddbClearWatchers( id );

	return 0;
}

int AgentHost::ddbGetRegion( UUID *id, spConnection con, UUID *thread ) {

	this->dStore->GetRegion( id, &this->ds, thread );

	this->sendAgentMessage( &con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
	//this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );

	this->ds.unlock();
	
	return 0;
}

int AgentHost::ddbEnumerateLandmarks( spConnection con, UUID *forward ) {
	int count;

	this->ds.reset();

	_timeb packStart, packEnd;
	apb->apb_ftime_s( &packStart );

	count = this->dStore->EnumerateLandmarks( &this->ds );

	apb->apb_ftime_s( &packEnd );
	unsigned int dt = (unsigned int)(1000*(packEnd.time - packStart.time) + packEnd.millitm - packStart.millitm);
	Log.log( LOG_LEVEL_NORMAL, "AgentHost::ddbEnumerateLandmarks: enumation took %d ms, size %d", dt, this->ds.length() );

	if ( count > 0 ) {
		// pack invalid to close the stream
		this->ds.packInt32( DDB_INVALID );
		// send
		this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_DDB_ENUMERATE), this->ds.stream(), this->ds.length(), forward );
	}
	this->ds.unlock();

	return 0;
}

int AgentHost::ddbAddLandmark( UUID *id, unsigned char code, UUID *owner, float height, float elevation, float x, float y, char estimatedPos, ITEM_TYPES landmarkType ) {

	// distribute
	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( id );
	this->ds.packUChar( code );
	this->ds.packUUID( owner );
	this->ds.packFloat32( height );
	this->ds.packFloat32( elevation );
	this->ds.packFloat32( x );
	this->ds.packFloat32( y );
	this->ds.packChar( estimatedPos );
	this->ds.packInt32(landmarkType);
	this->globalStateTransaction( OAC_DDB_ADDLANDMARK, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

//	this->dStore->AddLandmark( id, code, owner, height, elevation, x, y ); // add locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_LANDMARK, DDBE_ADD, id );

	return 0;
}

int AgentHost::ddbRemoveLandmark( UUID *id ) {

	// distribute
	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( id );
	this->globalStateTransaction( OAC_DDB_REMLANDMARK, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

//	this->dStore->RemoveLandmark( id ); // remove locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_LANDMARK, DDBE_REM, id );
//	this->ddbClearWatchers( id );

	return 0;
}

int AgentHost::ddbLandmarkSetInfo( char *data, unsigned int len ) {
	DataStream lds;
	unsigned char code;
	UUID id;

	lds.setData( data, 1 + 4 );
	code = lds.unpackUChar();
	int infoFlags = lds.unpackInt32();
	lds.unlock();

	id = dStore->GetLandmarkId( code );

	if ( id == nilUUID )
		return 1; // not found

	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packUUID( &id );
	lds.packData( data + 1, len - 1 ); // trim code from front of data
	this->globalStateTransaction( OAC_DDB_LANDMARKSETINFO, lds.stream(), lds.length() );
	lds.unlock();

	return 0;
}

int AgentHost::ddbGetLandmark( UUID *id, spConnection con, UUID *thread, bool enumLandmarks ) {

	this->dStore->GetLandmark( id, &this->ds, thread, enumLandmarks );

	this->sendAgentMessage( &con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
	//this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );

	this->ds.unlock();
	
	return 0;
}

int AgentHost::ddbGetLandmark( unsigned char code, spConnection con, UUID *thread ) {

	this->dStore->GetLandmark( code, &this->ds, thread );

	this->sendAgentMessage( &con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
	//this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );

	this->ds.unlock();
	
	return 0;
}

int AgentHost::ddbEnumeratePOGs( spConnection con, UUID *forward ) {
	int count;

	this->ds.reset();

	_timeb packStart, packEnd;
	apb->apb_ftime_s( &packStart );

	count = this->dStore->EnumeratePOGs( &this->ds );

	apb->apb_ftime_s( &packEnd );
	unsigned int dt = (unsigned int)(1000*(packEnd.time - packStart.time) + packEnd.millitm - packStart.millitm);
	Log.log( LOG_LEVEL_NORMAL, "AgentHost::ddbEnumeratePOGs: enumation took %d ms, size %d", dt, this->ds.length() );

	if ( count > 0 ) {
		// pack invalid to close the stream
		this->ds.packInt32( DDB_INVALID );
		// send
		this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_DDB_ENUMERATE), this->ds.stream(), this->ds.length(), forward );
	}
	this->ds.unlock();

	return 0;
}

int AgentHost::ddbAddPOG( UUID *id, float tileSize, float resolution ) {
	//Log.log(0, "AgentHost::ddbAddpog Adding POG with UUID %s", id);
	// verify that tileSize and resolution are valid
	if ( (float)floor(tileSize/resolution) != (float)(tileSize/resolution) )
		return 1; // tileSize must be an integer multiple of resolution

	// distribute
	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( id );
	this->ds.packFloat32( tileSize );
	this->ds.packFloat32( resolution );
	this->globalStateTransaction( OAC_DDB_ADDPOG, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

//	this->dStore->AddPOG( id, tileSize, resolution ); // add locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_MAP_PROBOCCGRID, DDBE_ADD, id );

	return 0;
}

int AgentHost::ddbRemovePOG( UUID *id ) {

	// distribute
	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( id );
	this->globalStateTransaction( OAC_DDB_REMPOG, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

//	this->dStore->RemovePOG( id ); // remove locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_MAP_PROBOCCGRID, DDBE_REM, id );
//	this->ddbClearWatchers( id );

	return 0;
}

int AgentHost::ddbPOGGetInfo( UUID *id, spConnection con, UUID *thread ) {

	this->dStore->POGGetInfo( id, &this->ds, thread );

	this->sendAgentMessage( &con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
	//this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );

	this->ds.unlock();
	
	return 0;
}

int AgentHost::ddbApplyPOGUpdate( UUID *id, float x, float y, float w, float h, float *data ) {
	int updateSize;

	// verify that the cooridates are valid
	updateSize = this->dStore->POGVerifyRegion( id, x, y, w, h );
	if ( !updateSize ) {
		return 1; // all coordinates must be integer multiples of resolution
	}

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( id );
	this->ds2.packFloat32( x );
	this->ds2.packFloat32( y );
	this->ds2.packFloat32( w );
	this->ds2.packFloat32( h );
	this->ds2.packInt32( updateSize );
	this->ds2.packData( data, updateSize );
	this->globalStateTransaction( OAC_DDB_APPLYPOGUPDATE, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

	//Log.log( LOG_LEVEL_ALL, "AgentHost::ddbApplyPOGUpdate: updating region %f %f %f %f", x, y, w, h );

//	this->dStore->ApplyPOGUpdate( id, x, y, w, h, data ); // add locally

	// notify watchers (by type and POG uuid)
/*	this->ds2.reset();
	this->ds2.packFloat32( x );
	this->ds2.packFloat32( y );
	this->ds2.packFloat32( w );
	this->ds2.packFloat32( h );
	this->_ddbNotifyWatchers( this->getUUID(), DDB_MAP_PROBOCCGRID, DDBE_POG_UPDATE, id, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

	// TEMP dump the map
	if ( 0 ) {
		char filename[512];
		static int count = 0;
		sprintf_s( filename, 512, "%s\\dump\\ddbApplyPOGUpdate%d.txt", logDirectory, count );
		count++;
		this->dStore->POGDumpRegion( id, -1, -1, 5, 7, filename );
	}
*/
	return 0;
}

int AgentHost::ddbPOGGetRegion( UUID *id, float x, float y, float w, float h, spConnection con, UUID *thread ) {

	this->dStore->POGGetRegion( id, x, y, w, h, &this->ds, thread );

	this->sendAgentMessage( &con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
	//this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );

	this->ds.unlock();
	
	return 0;
}

int AgentHost::ddbPOGLoadRegion( UUID *id, float x, float y, float w, float h, char *filename ) {
	FILE *fp;

	// verify that the cooridates are valid
	if ( !this->dStore->POGVerifyRegion( id, x, y, w, h ) ) {
		return 1; // all coordinates must be integer multiples of resolution
	}

	// make sure the file exists
	if ( fopen_s( &fp, filename, "r" ) ) {
		return 1; 
	}
	fclose( fp );

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( id );
	this->ds2.packFloat32( x );
	this->ds2.packFloat32( y );
	this->ds2.packFloat32( w );
	this->ds2.packFloat32( h );
	this->ds2.packString( filename );
	this->globalStateTransaction( OAC_DDB_POGLOADREGION, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

//	this->dStore->POGLoadRegion( id, x, y, w, h, filename ); // add locally

	// notify watchers (by type and POG uuid)
/*	this->ds2.reset();
	this->ds2.packFloat32( x );
	this->ds2.packFloat32( y );
	this->ds2.packFloat32( w );
	this->ds2.packFloat32( h );
	this->_ddbNotifyWatchers( this->getUUID(), DDB_MAP_PROBOCCGRID, DDBE_POG_LOADREGION, id, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();
*/
	return 0;
}

int AgentHost::ddbEnumerateParticleFilters( spConnection con, UUID *forward ) {
	int count;

	this->ds.reset();

	_timeb packStart, packEnd;
	apb->apb_ftime_s( &packStart );

	count = this->dStore->EnumerateParticleFilters( &this->ds );

	apb->apb_ftime_s( &packEnd );
	unsigned int dt = (unsigned int)(1000*(packEnd.time - packStart.time) + packEnd.millitm - packStart.millitm);
	Log.log( LOG_LEVEL_NORMAL, "AgentHost::ddbEnumerateParticleFilters: enumation took %d ms, size %d", dt, this->ds.length() );

	if ( count > 0 ) {
		// pack invalid to close the stream
		this->ds.packInt32( DDB_INVALID );
		// send
		this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_DDB_ENUMERATE), this->ds.stream(), this->ds.length(), forward );
	}
	this->ds.unlock();

	return 0;
}

int AgentHost::ddbAddParticleFilter( UUID *id, UUID *owner, int numParticles, _timeb *startTime, float *startState, int stateSize ) {

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( id );
	this->ds2.packUUID( owner );
	this->ds2.packInt32( numParticles );
	this->ds2.packData( startTime, sizeof(_timeb) );
	this->ds2.packInt32( stateSize );
	this->ds2.packData( startState, numParticles*stateSize*sizeof(float) );
	this->globalStateTransaction( OAC_DDB_ADDPARTICLEFILTER, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();
	
//	this->dStore->AddParticleFilter( id, owner, numParticles, startTime, startState, stateSize ); // add locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_PARTICLEFILTER, DDBE_ADD, id );

	return 0;
}

int AgentHost::ddbRemoveParticleFilter( UUID *id ) {

	// distribute
	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( id );
	this->globalStateTransaction( OAC_DDB_REMPARTICLEFILTER, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

//	this->_ddbRemoveParticleFilter( id ); // remove locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_PARTICLEFILTER, DDBE_REM, id );
//	this->ddbClearWatchers( id );

	return 0;
}

int AgentHost::_ddbRemoveParticleFilter( UUID *id ) {

	// if it is locked clear it
/*	mapLock::iterator iterLock = this->PFLocks.find( *id );
	if ( iterLock != this->PFLocks.end() ) { 
		this->PFLocks.erase( iterLock );
	}
*/
	map<UUID,list<DDBParticleFilter_Correction>,UUIDless>::iterator iPFH = this->PFHeldCorrections.find( *id );
	if ( iPFH != this->PFHeldCorrections.end() ) {
		std::list<DDBParticleFilter_Correction>::iterator iterPFC = iPFH->second.begin();
		while ( iterPFC != iPFH->second.end() ) {
			free( (*iterPFC).obsDensity );
			iterPFC++;
		}

		this->PFHeldCorrections.erase( iPFH );
	}

	this->dStore->RemoveParticleFilter( id );

	return 0;	
}

int AgentHost::ddbResampleParticleFilter( UUID *id ) {
	DataStream lds;

	// see if we're the group leader
	if ( this->gmMemberList.front() != *this->getUUID() ) {
		// we're not the leader, so ask the leader to do it
		this->sendMessageEx( this->hostKnown[this->gmMemberList.front()]->connection, MSGEX(AgentHost_MSGS,MSG_DDB_PFRESAMPLE_CHECK), (char *)id, sizeof(UUID) );	
		return 0; // done
	}

	// check to make sure we aren't resampling already
	list<UUID>::iterator iI;
	for ( iI = this->PFResampleInProgress.begin(); iI != this->PFResampleInProgress.end(); iI++ ) {
		if ( *iI == *id )
			return 0; // in progress
	}

	// get avatar
	UUID *ownerId = this->dStore->PFGetOwner( id );
	if ( !ownerId ) {
		Log.log( 0, "AgentHost::ddbResampleParticleFilter: Particle filter no owner?\n" );
		return 1; // not found?
	}

	// get avatar agent
	UUID agentId;
	this->dStore->AvatarGetInfo( ownerId, DDBAVATARINFO_RAGENT, &lds, &nilUUID );
	lds.rewind();
	lds.unpackData(sizeof(UUID)); // discard thread
	if ( lds.unpackChar() != DDBR_OK ) {
		Log.log( 0, "AgentHost::ddbResampleParticleFilter: avatar agent not found?\n" );
		lds.unlock();
		return 1; // not found?
	}
	lds.unpackInt32(); // info flags
	lds.unpackUUID( &agentId );
	lds.unlock();

	float effectiveParticleNum;
	dStore->ResamplePF_Prepare( id, &lds, &effectiveParticleNum );
	if ( effectiveParticleNum != -1 && effectiveParticleNum < DDB_EFFECTIVEPARTICLENUM_THRESHOLD ) { 
		// starting
		this->PFResampleInProgress.push_back( *id );

		// tell avatar agent to resample
		this->sendAgentMessage( &agentId, MSG_DDB_PFRESAMPLEREQUEST, lds.stream(), lds.length() );

		// set timeout to clear request
		this->addTimeout( PFRESAMPLE_TIMEOUT, AgentHost_CBR_cbPFResampleTimeout, id, sizeof(UUID) );
	}
	lds.unlock();

	return 0;
}

int AgentHost::ddbApplyPFResample( DataStream *ds ) {
	
	// distribute
	this->globalStateTransaction( OAC_DDB_APPLYPFRESAMPLE, ds->stream(), ds->length() );

	return 0;
}

int AgentHost::_ddbApplyPFResample( DataStream *ds ) {
	UUID id;
	int particleNum, stateSize;
	int *parents;
	float *state;

	ds->unpackUUID( &id );
	particleNum = ds->unpackInt32();
	stateSize = ds->unpackInt32();
	parents = (int *)ds->unpackData( sizeof(int)*particleNum );
	state = (float *)ds->unpackData( sizeof(float)*stateSize*particleNum );

	this->dStore->ResamplePF_Apply( &id, parents, state );

	// if we asked for this clear the in progress flag
	list<UUID>::iterator iI;
	for ( iI = this->PFResampleInProgress.begin(); iI != this->PFResampleInProgress.end(); iI++ ) {
		if ( *iI == id ) {
			this->PFResampleInProgress.erase( iI );		
			break;
		}
	}

	return 0;
}

/*	
int AgentHost::_ddbResampleParticleFilter_Lock( UUID *id, spConnection con, UUID *key, UUID *thread, UUID *host ) {

	mapLock::iterator iterLock = this->PFLocks.find( *id );
	if ( iterLock != this->PFLocks.end() ) { // it is already locked, so a resample must be in progress
		this->_ddbResampleParticleFilter_AbortLock( id, key ); // abort
		
		return 0;
	}

	// set lock
	UUIDLock *L = &this->PFLocks[*id];
	L->key = *key;
	this->PFLockingHost[*id] = *host;

	// TODO send all outstanding notifications

	// respond
	this->ds.reset();
	this->ds.packUUID( thread );
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( key );
	this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}

int AgentHost::_ddbResampleParticleFilter_Do( UUID *id ) {
	DataStream lds, ldsAbort;

	// get lock key
	UUIDLock *L = &this->PFLocks[*id];

	// prepare message
	lds.reset();
	lds.packUUID( id );
	lds.packUUID( &L->key );
	lds.packChar( DDBR_OK );
	if ( this->dStore->ResamplePF_Generate( id, &lds ) ) { // this function will pack the data stream that we send out below
		return 1; // not found?
	}

	// get avatar
	UUID *ownerId = this->dStore->PFGetOwner( id );
	if ( !ownerId ) {
		return 1; // not found?
	}

	// get avatar agent
	UUID agentId;
	this->dStore->AvatarGetInfo( ownerId, DDBAVATARINFO_RAGENT, &this->ds2, &nilUUID );
	this->ds2.rewind();
	this->ds2.unpackData(sizeof(UUID)); // discard thread
	if ( this->ds2.unpackChar() != DDBR_OK ) {
		Log.log( 0, "AgentHost::_ddbResampleParticleFilter_Do: avatar agent not found?\n" );
		this->ds2.unlock();
		return 1; // not found?
	}
	this->ds2.unpackInt32(); // info flags
	this->ds2.unpackUUID( &agentId );
	this->ds2.unlock();

	// start atomic commit
	list<UUID> targets;
	this->groupList( &this->groupHostId, &targets );
	targets.push_back( agentId );

	ldsAbort.reset();
	ldsAbort.packUUID( id );
	ldsAbort.packUUID( &L->key );
	ldsAbort.packChar( DDBR_ABORT );

	this->atomicMessage( &targets, MSG__DDB_RESAMPLEPF_UNLOCK, lds.stream(), lds.length(), &L->key, CBR_INVALID, MSG__DDB_RESAMPLEPF_UNLOCK, ldsAbort.stream(), ldsAbort.length() );
	lds.unlock();

	Log.log( LOG_LEVEL_VERBOSE, "AgentHost::_ddbResampleParticleFilter_Do: starting atomic commit of resample (pf %s)", Log.formatUUID( LOG_LEVEL_VERBOSE, id ) );

	return 0;
}

int AgentHost::_ddbResampleParticleFilter_Unlock( DataStream *ds ) {
	UUID id, key;
	char result;
	UUIDLock *L;

	ds->unpackUUID( &id );
	ds->unpackUUID( &key );
	result = ds->unpackChar();

	if ( this->PFLocks.find( id ) == this->PFLocks.end() ) {
		return 1; // not locked?
	}

	L = &this->PFLocks[id];

	if ( L->key != key ) {
		return 1; // wrong key
	}

	Log.log( LOG_LEVEL_VERBOSE, "AgentHost::_ddbResampleParticleFilter_Unlock: result %d (pf %s)", result, Log.formatUUID( LOG_LEVEL_VERBOSE, &id ) );

	if ( result == DDBR_ABORT ) {
		// rebroadcast abort
		this->_ddbResampleParticleFilter_AbortLock( &id, &key );

		// unlock
		this->PFLocks.erase( id );

		// apply held corrections
		map<UUID,list<DDBParticleFilter_Correction>,UUIDless>::iterator iCL;
		list<DDBParticleFilter_Correction>::iterator iC;
		iCL = this->PFHeldCorrections.find( id );
		if ( iCL != this->PFHeldCorrections.end() ) {
			for ( iC = iCL->second.begin(); iC != iCL->second.end(); iC++ ) {
				this->_ddbProcessPFCorrection( &id, iC->regionAge, &iC->tb, iC->obsDensity );

				free( iC->obsDensity );
			}
			this->PFHeldCorrections.erase( iCL );
		}		

	} else { // DDBR_OK
		_timeb startTime;
		int particleNum, stateSize;
		int *parents;
		float *state;

		startTime = *(_timeb *)ds->unpackData( sizeof(_timeb) );
		particleNum = ds->unpackInt32();
		stateSize = ds->unpackInt32();
		parents = (int *)ds->unpackData( sizeof(int)*particleNum );
		state = (float *)ds->unpackData( sizeof(float)*stateSize*particleNum );

		this->dStore->ResamplePF_Apply( &id, &startTime, parents, state );
	
		// unlock
		this->PFLocks.erase( id );

		// apply held corrections
		map<UUID,list<DDBParticleFilter_Correction>,UUIDless>::iterator iCL;
		list<DDBParticleFilter_Correction>::iterator iC;
		iCL = this->PFHeldCorrections.find( id );
		if ( iCL != this->PFHeldCorrections.end() ) {
			for ( iC = iCL->second.begin(); iC != iCL->second.end(); iC++ ) {
				this->_ddbProcessPFCorrection( &id, iC->regionAge, &iC->tb, iC->obsDensity );

				free( iC->obsDensity );
			}
			this->PFHeldCorrections.erase( iCL );
		}

		// distribute to local mirrors
		list<UUID>::iterator iterId;
		mapAgentInfo::iterator iterAI;
		for ( iterId = this->DDBMirrors.begin(); iterId != this->DDBMirrors.end();  ) {
			iterAI = this->agentInfo.find(*iterId);
			iterId++; // increment here since it is possible DDBMirrors will change during the loop (i.e. a mirror is removed)
			if (  iterAI == this->agentInfo.end() ) { // agent not found!
				continue;
			} else if ( iterAI->second.con != NULL ) { // this is one of our agents
				this->sendMessage( iterAI->second.con, MSG__DDB_RESAMPLEPF_UNLOCK, ds->stream(), ds->length() );
			}
		}

		// notify local watchers
		this->_ddbNotifyWatchers( this->getUUID(), DDB_PARTICLEFILTER, DDBE_PF_RESAMPLE, &id, (char *)&startTime, sizeof(_timeb) );
	}

	return 0;
}

int AgentHost::_ddbResampleParticleFilter_AbortLock( UUID *id, UUID *key ) {

	// get avatar
	UUID *ownerId = this->dStore->PFGetOwner( id );
	if ( !ownerId ) {
		Log.log( 0, "AgentHost::_ddbResampleParticleFilter_AbortLock: Particle filter no owner?\n" );
		return 1; // not found?
	}

	// get avatar agent
	UUID agentId;
	this->dStore->AvatarGetInfo( ownerId, DDBAVATARINFO_RAGENT, &this->ds, &nilUUID );
	this->ds.rewind();
	this->ds.unpackData(sizeof(UUID)); // discard thread
	if ( this->ds.unpackChar() != DDBR_OK ) {
		Log.log( 0, "AgentHost::_ddbResampleParticleFilter_AbortLock: avatar agent not found?\n" );
		this->ds.unlock();
		return 1; // not found?
	}
	this->ds.unpackInt32(); // info flags
	this->ds.unpackUUID( &agentId );
	this->ds.unlock();


	Log.log( LOG_LEVEL_VERBOSE, "AgentHost::_ddbResampleParticleFilter_AbortLock: aborting lock %s", Log.formatUUID( LOG_LEVEL_VERBOSE, id ) );
				
	// send abort
	this->ds.reset();
	this->ds.packUUID( id );
	this->ds.packUUID( key );
	this->ds.packChar( DDBR_ABORT );

	this->sendAgentMessage( (UUID *)&agentId, MSG__DDB_RESAMPLEPF_UNLOCK, this->ds.stream(), this->ds.length() );

	mapAgentHostState::iterator iterHS;
	for ( iterHS = this->hostKnown.begin(); iterHS != this->hostKnown.end(); iterHS++ ) {
		if ( iterHS->second->connection == NULL )
			continue; // we don't have a connection to this host
				
		this->sendMessage( iterHS->second->connection, MSG__DDB_RESAMPLEPF_UNLOCK, this->ds.stream(), this->ds.length() );
	}

	this->ds.unlock();

	return 0;
}
*/
int AgentHost::ddbInsertPFPrediction( UUID *id, _timeb *tb, float *state, bool nochange ) {
	int stateArraySize = this->dStore->PFGetStateArraySize( id );

	if ( !stateArraySize ) {
		return 1;
	}

	// TODO increment age

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( id );
	this->ds2.packData( tb, sizeof(_timeb) );
	this->ds2.packChar( nochange );
	if ( !nochange )
		this->ds2.packData( state, stateArraySize );
	this->globalStateTransaction( OAC_DDB_INSERTPFPREDICTION, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

//	this->dStore->InsertPFPrediction( id, tb, state, nochange ); // add locally

	// notify watchers (by type and pf uuid)
/*	this->ds2.reset();
	this->ds2.packData( tb, sizeof(_timeb) );
	this->ds2.packChar( (char)nochange );
	this->_ddbNotifyWatchers( this->getUUID(), DDB_PARTICLEFILTER, DDBE_PF_PREDICTION, id, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();
*/
	return 0;
}

int AgentHost::ddbApplyPFCorrection( UUID *id, _timeb *tb, float *obsDensity ) {
	int particleNum = this->dStore->PFGetParticleNum( id );
	int regionAge = this->dStore->PFGetRegionCount( id ) - 1;
	
	if ( !particleNum ) {
		return 1;
	}

/*	mapLock::iterator iterLock = this->PFLocks.find( *id );
	if ( iterLock != this->PFLocks.end() ) { // hold correction until resample is finished
		DDBParticleFilter_Correction corr;
		
		corr.regionAge = regionAge;
		corr.tb = *tb;
		corr.obsDensity = (float *)malloc( sizeof(float)*particleNum );
		if ( corr.obsDensity == NULL ) {
			return 1; // malloc failed
		}
		memcpy( corr.obsDensity, obsDensity, sizeof(float)*particleNum );

		this->PFHeldCorrections[*id].push_back( corr );
	} else { // process now
		this->_ddbProcessPFCorrection( id, regionAge, tb, obsDensity );
	}
*/

	this->_ddbProcessPFCorrection( id, regionAge, tb, obsDensity );

	return 0;
}

int AgentHost::_ddbProcessPFCorrection( UUID *id, int regionAge, _timeb *tb, float *obsDensity ) {
	int particleNum = this->dStore->PFGetParticleNum( id );
	
	if ( !particleNum ) {
		return 1;
	}

	// TODO increment age

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( id );
	this->ds2.packInt32( regionAge );
	this->ds2.packData( tb, sizeof(_timeb) );
	this->ds2.packData( obsDensity, sizeof(float)*particleNum );
	this->globalStateTransaction( OAC_DDB_APPLYPFCORRECTION, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

//	this->dStore->ApplyPFCorrection( id, regionAge, tb, obsDensity ); // add locally

	// notify watchers (by type and pf uuid)
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_PARTICLEFILTER, DDBE_PF_CORRECTION, id, (char *)tb, sizeof(_timeb) );

	return 0;
}

int AgentHost::ddbPFGetInfo( UUID *id, int infoFlags, _timeb *tb, spConnection con, UUID *thread ) {
	DataStream lds;
	float effectiveParticleNum;

	this->dStore->PFGetInfo( id, infoFlags, tb, &lds, thread, &effectiveParticleNum );

	this->sendAgentMessage( &con->uuid, MSG_RESPONSE, lds.stream(), lds.length() );
	//this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );

	lds.unlock();

	if ( effectiveParticleNum != -1 && effectiveParticleNum < DDB_EFFECTIVEPARTICLENUM_THRESHOLD ) {
		// resample
		this->ddbResampleParticleFilter( id );
	}

	return 0;
}

int AgentHost::ddbEnumerateAvatars( spConnection con, UUID *forward ) {
	int count;

	this->ds.reset();

	_timeb packStart, packEnd;
	apb->apb_ftime_s( &packStart );

	count = this->dStore->EnumerateAvatars( &this->ds );

	apb->apb_ftime_s( &packEnd );
	unsigned int dt = (unsigned int)(1000*(packEnd.time - packStart.time) + packEnd.millitm - packStart.millitm);
	Log.log( LOG_LEVEL_NORMAL, "AgentHost::ddbEnumerateAvatars: enumation took %d ms, size %d", dt, this->ds.length() );

	if ( count > 0 ) {
		// pack invalid to close the stream
		this->ds.packInt32( DDB_INVALID );
		// send
		this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_DDB_ENUMERATE), this->ds.stream(), this->ds.length(), forward );
	}
	this->ds.unlock();

	return 0;
}

int AgentHost::ddbAddAvatar( UUID *id, char *type, int status, UUID *agent, UUID *pf, float innerRadius, float outerRadius, _timeb *startTime, int capacity, int sensorTypes ) {

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( id );
	this->ds2.packString( type );
	this->ds2.packInt32( status );
	this->ds2.packUUID( agent );
	this->ds2.packUUID( pf );
	this->ds2.packFloat32( innerRadius );
	this->ds2.packFloat32( outerRadius );
	this->ds2.packData( startTime, sizeof(_timeb) );
	this->ds2.packInt32( capacity );
	this->ds2.packInt32( sensorTypes );
	this->globalStateTransaction( OAC_DDB_ADDAVATAR, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

//	AgentType agentType = this->agentInfo[*agent].type;
//	this->dStore->AddAvatar( id, type, &agentType, status, agent, pf, innerRadius, outerRadius ); // add locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_AVATAR, DDBE_ADD, id );

	return 0;
}

int AgentHost::ddbRemoveAvatar( UUID *id ) {

	// distribute
	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( id );
	this->globalStateTransaction( OAC_DDB_REMAVATAR, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

//	this->dStore->RemoveAvatar( id ); // remove locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), DDB_AVATAR, DDBE_REM, id );
//	this->ddbClearWatchers( id );

	return 0;
}

int AgentHost::ddbAvatarGetInfo( UUID *id, int infoFlags, spConnection con, UUID *thread ) {

	this->dStore->AvatarGetInfo( id, infoFlags, &this->ds, thread );

	this->sendAgentMessage( &con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
	//this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
	
	this->ds.unlock();

	return 0;
}

int AgentHost::ddbAvatarSetInfo( char *data, unsigned int len ) {
	DataStream lds;

	// distribute
	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packData( data, len );
	this->globalStateTransaction( OAC_DDB_AVATARSETINFO, lds.stream(), lds.length() );
	lds.unlock();

	return 0;
}

int AgentHost::ddbEnumerateSensors( spConnection con, UUID *forward ) {
	int count;

	this->ds.reset();

	_timeb packStart, packEnd;
	apb->apb_ftime_s( &packStart );

	count = this->dStore->EnumerateSensors( &this->ds );

	apb->apb_ftime_s( &packEnd );
	unsigned int dt = (unsigned int)(1000*(packEnd.time - packStart.time) + packEnd.millitm - packStart.millitm);
	Log.log( LOG_LEVEL_NORMAL, "AgentHost::ddbEnumerateSensors: enumation took %d ms, size %d", dt, this->ds.length() );

	if ( count > 0 ) {
		// pack invalid to close the stream
		this->ds.packInt32( DDB_INVALID );
		// send
		this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_DDB_ENUMERATE), this->ds.stream(), this->ds.length(), forward );
	}
	this->ds.unlock();

	return 0;
}

int AgentHost::ddbAddSensor( UUID *id, int type, UUID *avatar, UUID *pf, void *pose, int poseSize ) {

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( id );
	this->ds2.packInt32( type );
	this->ds2.packUUID( avatar );
	this->ds2.packUUID( pf );
	this->ds2.packData( pose, poseSize );
	this->globalStateTransaction( OAC_DDB_ADDSENSOR, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

//	this->dStore->AddSensor( id, type, avatar, pf, pose, poseSize ); // add locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), type, DDBE_ADD, id );

	return 0;
}

int AgentHost::ddbRemoveSensor( UUID *id ) {
/*	int type = this->dStore->GetSensorType( id );

	if ( !type )
		return 1;
*/
	// distribute
	this->ds.reset();
	this->ds.packUUID( this->getUUID() );
	this->ds.packUUID( id );
	this->globalStateTransaction( OAC_DDB_REMSENSOR, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

//	this->dStore->RemoveSensor( id ); // remove locally

	// notify watchers
//	this->_ddbNotifyWatchers( this->getUUID(), type, DDBE_REM, id );
//	this->ddbClearWatchers( id );

	return 0;
}

int AgentHost::ddbInsertSensorReading( UUID *id, _timeb *tb, void *reading, int readingSize, void *data, int dataSize ) {
	
	int type = this->dStore->GetSensorType( id );

	if ( !type )
		return 1;

	// TODO increment age

	// distribute
	this->ds2.reset();
	this->ds2.packUUID( this->getUUID() );
	this->ds2.packUUID( id );
	this->ds2.packData( tb, sizeof(_timeb) );
	this->ds2.packInt32( readingSize );
	this->ds2.packData( reading, readingSize );
	this->ds2.packInt32( dataSize );
	if ( dataSize )
		this->ds2.packData( data, dataSize );
	this->globalStateTransaction( OAC_DDB_INSERTSENSORREADING, this->ds2.stream(), this->ds2.length() );
	this->ds2.unlock();

//	this->dStore->InsertSensorReading( id, tb, reading, readingSize, data, dataSize ); // add locally

	// notify watchers (by type and sensor uuid)
//	this->_ddbNotifyWatchers( this->getUUID(), type, DDBE_SENSOR_UPDATE, id, (void *)tb, sizeof(_timeb) );

	return 0;
}

int AgentHost::ddbSensorGetInfo( UUID *id, int infoFlags, spConnection con, UUID *thread ) {

	this->dStore->SensorGetInfo( id, infoFlags, &this->ds, thread );

	this->sendAgentMessage( &con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
	//this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );

	this->ds.unlock();
	
	return 0;
}

int AgentHost::ddbSensorGetData( UUID *id, _timeb *tb, spConnection con, UUID *thread ) {

	this->dStore->SensorGetData( id, tb, &this->ds, thread );

	this->sendAgentMessage( &con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
	//this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );

	this->ds.unlock();
	
	return 0;
}

int AgentHost::ddbAddTask(UUID *id, UUID *landmarkUUID, UUID *agent, UUID *avatar, bool completed, ITEM_TYPES TYPE) {


	
Log.log(0, "AgentHost::ddbAddTask: task contents: landmark UUID: %s, avatar UUID: %s, completed: %s, ITEM_TYPES: %d", Log.formatUUID(LOG_LEVEL_NORMAL, landmarkUUID), Log.formatUUID(LOG_LEVEL_NORMAL, avatar), completed ? "true" : "false", TYPE);




	// distribute
	this->ds.reset();
	this->ds.packUUID(this->getUUID());
	this->ds.packUUID(id);
	this->ds.packUUID(landmarkUUID);
	this->ds.packUUID(agent);
	this->ds.packUUID(avatar);
	this->ds.packBool(completed);
	this->ds.packInt32(TYPE);
	this->globalStateTransaction(OAC_DDB_ADDTASK, this->ds.stream(), this->ds.length());
	this->ds.unlock();

	return 0;
}

int AgentHost::ddbRemoveTask(UUID *id) {

	// distribute
	this->ds.reset();
	this->ds.packUUID(this->getUUID());
	this->ds.packUUID(id);
	this->globalStateTransaction(OAC_DDB_REMTASK, this->ds.stream(), this->ds.length());
	this->ds.unlock();

	return 0;
}

int AgentHost::ddbTaskSetInfo(UUID *id, UUID *agent, UUID *avatar, bool completed) {

	DataStream lds;
	bool isIdNotFound;
	UUID foundId;

	isIdNotFound = dStore->GetTaskId(id, &foundId);
//	Log.log(0, "AgentHost::ddbTaskSetInfo: Is the task found? ");
	if (*id != nilUUID) {
		if (isIdNotFound == true)
			return 1; // not found
	}
//	Log.log(0, "AgentHost::ddbTaskSetInfo: Yes it is! And the agent is: %s", Log.formatUUID(0,agent));
	// distribute
	lds.reset();
	lds.packUUID(this->getUUID());
	lds.packUUID(id);
	lds.packUUID(agent);
	lds.packUUID(avatar);
	lds.packBool(completed);
	this->globalStateTransaction(OAC_DDB_TASKSETINFO, lds.stream(), lds.length());
	lds.unlock();

	return 0;
}

int AgentHost::ddbGetTask(UUID *id, spConnection con, UUID *thread, bool enumTasks) {

	this->dStore->GetTask(id, &this->ds, thread, enumTasks);

	DataStream lds;
	lds.setData(this->ds.stream(), this->ds.length());
	lds.unpackChar();
	lds.unpackBool();
	lds.unpackData(sizeof(UUID));
	DDBTask *inspectTask;
	inspectTask = (DDBTask *)lds.unpackData(sizeof(DDBTask));
//	Log.log(0, "AgentHost::ddbTaskGetInfo: Task id %s has agent id: %s", Log.formatUUID(0,id), Log.formatUUID(0, &inspectTask->agentUUID));


	this->sendAgentMessage(&con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length());

	this->ds.unlock();

	return 0;
}

int AgentHost::ddbAddTaskData(UUID *avatarId, DDBTaskData *taskData) {
	// distribute


	this->ds.reset();
	this->ds.packUUID(this->getUUID());
	this->ds.packUUID(avatarId);
	this->ds.packTaskData(taskData);
	this->globalStateTransaction(OAC_DDB_ADDTASKDATA, this->ds.stream(), this->ds.length());
	this->ds.unlock();

	return 0;
}

int AgentHost::ddbRemoveTaskData(UUID *avatarid) {

	// distribute
	this->ds.reset();
	this->ds.packUUID(this->getUUID());
	this->ds.packUUID(avatarid);
	this->globalStateTransaction(OAC_DDB_REMTASKDATA, this->ds.stream(), this->ds.length());
	this->ds.unlock();

	return 0;
}

int AgentHost::ddbTaskDataSetInfo(UUID *avatarId, DDBTaskData *taskData) {

	DataStream lds;
	UUID foundId;

	foundId = dStore->GetTaskDataId(avatarId);

	if (foundId == nilUUID) {
		Log.log(0, "AgentHost::ddbTaskDataSetInfo: id not found.");
		return 1; // not found
	}
				  // distribute
	lds.reset();
	lds.packUUID(this->getUUID());
	lds.packUUID(avatarId);
	lds.packTaskData(taskData);
	this->globalStateTransaction(OAC_DDB_TASKDATASETINFO, lds.stream(), lds.length());
	lds.unlock();
	return 0;
}

int AgentHost::ddbGetTaskData(UUID *id, spConnection con, UUID *thread, bool enumTaskData) {

	this->dStore->GetTaskData(id, &this->ds, thread, enumTaskData);

	this->sendAgentMessage(&con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length());

	this->ds.unlock();

	return 0;
}

int AgentHost::ddbTLRoundSetInfo(DataStream *lds) {

	this->globalStateTransaction(OAC_DDB_TL_ROUND_INFO, lds->stream(), lds->length());

	return 0;
}

int AgentHost::ddbTLRoundGetInfo(spConnection con, UUID *thread) {

	this->dStore->GetTLRoundInfo(&this->ds, thread);

	this->sendAgentMessage(&con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length());

	this->ds.unlock();

	return 0;
}



int AgentHost::ddbAddQLearningData(bool onlyActions, char typeId, long long totalActions, long long usefulActions, int tableSize, std::vector<float> qTable, std::vector<unsigned int> expTable)
{
	this->ds.reset();
	this->ds.packUUID(this->getUUID());
	this->ds.packChar(typeId);
	this->ds.packBool(onlyActions);
	this->ds.packInt64(totalActions);
	this->ds.packInt64(usefulActions);


	if (!onlyActions) {
		this->ds.packInt32(tableSize);		//Size of value tables to store

		for (auto qIter : qTable) {
			this->ds.packFloat32(qIter);						//Pack all values in q-table
			//if (qIter > 0.0f)
				//Log.log(LOG_LEVEL_NORMAL, "AgentHost::ddbAddQLearningData:packing qVal: %f", qIter);
		}
		for (auto expIter : expTable) {
			this->ds.packUInt32(expIter);						//Pack all values in exp-table
		}
		Log.log(LOG_LEVEL_NORMAL, "AgentHost::ddbAddQLearningData: stream length is %d", this->ds.length());
	}
	this->globalStateTransaction(OAC_DDB_ADDQLEARNINGDATA, this->ds.stream(), this->ds.length());

	this->ds.unlock();
	return 0;
}

int AgentHost::ddbAddAdviceData(char instance, float cq, float bq)
{
	this->ds.reset();
	this->ds.packUUID(this->getUUID());
	this->ds.packChar(instance);
	this->ds.packFloat32(cq);
	this->ds.packFloat32(bq);

	this->globalStateTransaction(OAC_DDB_ADDADVICEDATA, this->ds.stream(), this->ds.length());
	this->ds.unlock();
	return 0;
}

int AgentHost::ddbAddSimSteps(unsigned long long totalSimSteps)
{
	this->ds.reset();
	this->ds.packUInt64(totalSimSteps);
	this->globalStateTransaction(OAC_DDB_ADDSIMSTEPS, this->ds.stream(), this->ds.length());
	this->ds.unlock();
	return 0;
}


int AgentHost::DataDump( bool fulldump, bool getPose, char *label ) {

	if ( !this->gatherData )
		return 0;
	
	if ( label ) {
		Log.log( LOG_LEVEL_NORMAL, "AgentHost::DataDump: dumping data (%s)", label );
		Data.log( 0, "DUMPING_DATA (%s)", label );
	} else {	
		Log.log( LOG_LEVEL_NORMAL, "AgentHost::DataDump: dumping data" );
		Data.log( 0, "DUMPING_DATA" );
	}
	
	// dump DDB statistics, landmarks, maps, and particle filters
	dStore->DataDump( &Data, fulldump, logDirectory );

	//this->LearningDataDump();	//ONLY FOR TESTING; REMOVE WHEN DONE

	if ( getPose ) {
		// ask ExecutiveSimulation for true avatar poses
		mapAgentInfo::iterator iterAI;
		UUID execSimulationId;
		UuidFromString( (RPC_WSTR)_T("b2e39474-2c56-42ac-a40e-5c14f9a38ef7"), &execSimulationId );
		iterAI = this->agentInfo.begin();
		while ( iterAI != this->agentInfo.end() ) {
			if ( iterAI->second.type.uuid == execSimulationId ) { // found ExecutiveSimulation
				this->sendAgentMessage( (UUID *)&iterAI->first, MSG__DATADUMP_RAVATARPOSES, (char *)this->getUUID(), sizeof(UUID) );
				break;
			}
			iterAI++;
		}
	}

	Log.log( LOG_LEVEL_NORMAL, "AgentHost::DataDump: dump finished" );

	return 0;
}

int AgentHost::DataDump_AvatarPose( DataStream *ds ) {
	DataStream lds;
	UUID avatar, pf;
	_timeb tb;
	float x, y, r;
	float effectiveParticleNum;
	int stateSize;
	float *state;

	float errX, errY, errR, errD, errAvg, errAvgR;
	int errCount;
	errAvg = 0;
	errAvgR = 0;
	errCount = 0;
	while ( ds->unpackBool() ) {
		ds->unpackUUID( &avatar );
		tb = *(_timeb *)ds->unpackData( sizeof(_timeb) );
		x = ds->unpackFloat32();
		y = ds->unpackFloat32();
		r = ds->unpackFloat32();

		// find pf
		dStore->AvatarGetInfo( &avatar, DDBAVATARINFO_RPF, &lds, &nilUUID );
		lds.rewind();
		lds.unpackData(sizeof(UUID)); // thread
		if ( lds.unpackChar() == DDBR_OK ) {
			lds.unpackInt32(); // infoFlags
			lds.unpackUUID( &pf );
			lds.unlock();
		} else {
			lds.unlock();
			continue; // not found?	
		}

		// get pose estimate
		dStore->PFGetInfo( &pf, DDBPFINFO_STATE_SIZE | DDBPFINFO_MEAN, &tb, &lds, &nilUUID, &effectiveParticleNum );
		lds.rewind();
		lds.unpackData(sizeof(UUID)); // thread;
		if ( lds.unpackChar() == DDBR_OK ) {
			lds.unpackInt32(); // infoFlags
			stateSize = lds.unpackInt32();
			state = (float *)lds.unpackData(sizeof(float)*stateSize);
			
			errX = state[0] - x;
			errY = state[1] - y;
			errD = sqrt( errX*errX + errY*errY );
			errR = state[2] - r;
			errAvg += errD;
			errAvgR += errR;
			errCount++;
			Data.log( 0, "AVATAR_POSE %s x %f y %f r %f trueX %f trueY %f trueR %f errX %f errY %f errR %f errD %f", 
				Data.formatUUID(0,&avatar),
				state[0], state[1], state[2],
				x, y, r,
				errX, errY, errR, errD );	
		} 
		lds.unlock();
	}

	if ( errCount ) {
		Data.log( 0, "AVATAR_ESTIMATION count %d errAvg %f errAvgR %f", errCount, errAvg/errCount, errAvgR/errCount );
	}

	return 0;
}

int AgentHost::LearningDataDump()
{
	Log.log(LOG_LEVEL_NORMAL, "AgentHost::LearningDataDump: 1");



	Log.log(LOG_LEVEL_NORMAL, "AgentHost::LearningDataDump: 1.3");
	DataStream taskDataDS;
	DataStream taskDS;


	this->dStore->GetTaskData(&nilUUID, &taskDataDS, &nilUUID, true);
	Log.log(LOG_LEVEL_NORMAL, "AgentHost::LearningDataDump: 2");
	taskDataDS.rewind();
	taskDataDS.unpackData(sizeof(UUID)); // discard thread
	char taskDataOk = taskDataDS.unpackChar();	//DDBR_OK
	bool enumTaskData = taskDataDS.unpackBool();	//EnumTaskData
	int numTaskDatas = taskDataDS.unpackInt32();
	Log.log(LOG_LEVEL_NORMAL, "AgentHost::LearningDataDump: taskDataOk is %d, enumTaskData is %d, numTaskDatas is %d", taskDataOk, enumTaskData, numTaskDatas);

	this->dStore->GetTask(&nilUUID, &taskDS, &nilUUID, true);
	taskDS.rewind();
	taskDS.unpackData(sizeof(UUID)); // discard thread
	char taskOk = taskDS.unpackChar();	//DDBR_OK
	bool enumTask = taskDS.unpackBool();	//EnumTasks
	int numTasks = taskDS.unpackInt32();
	Log.log(LOG_LEVEL_NORMAL, "AgentHost::LearningDataDump: 3");
	Log.log(LOG_LEVEL_NORMAL, "AgentHost::LearningDataDump: taskOk is %d, enumTask is %d, numTasks is %d, ", taskOk, enumTask, numTasks);

	mapDDBQLearningData QLData = this->dStore->GetQLearningData();
	Log.log(LOG_LEVEL_NORMAL, "AgentHost::LearningDataDump: size of QLData is %d, ", QLData.size());
	for (auto& qlIter : QLData) {
		Log.log(LOG_LEVEL_NORMAL, "AgentHost::LearningDataDump: Instance: %d, Total actions: %d, Useful actions: %d, table size: %d", qlIter.first, qlIter.second.totalActions, qlIter.second.usefulActions, qlIter.second.qTable.size());

//		for (auto qTIter : qlIter.second.qTable)
//			;
////			Log.log(LOG_LEVEL_NORMAL, "AgentHost::LearningDataDump: instance %d, qTable entry: %f", qlIter.first-'0', qTIter);
	}

	mapDDBAdviceData adviceData = this->dStore->GetAdviceData();
	Log.log(LOG_LEVEL_NORMAL, "AgentHost::LearningDataDump: size of adviceData is %d, ", adviceData.size());
	for (auto& advIter : adviceData) {
		Log.log(LOG_LEVEL_NORMAL, "AgentHost::LearningDataDump: Instance: %d, cq: %f, bq: %f", advIter.first, advIter.second.cq, advIter.second.bq);

		//		for (auto qTIter : qlIter.second.qTable)
		//			;
		////			Log.log(LOG_LEVEL_NORMAL, "AgentHost::LearningDataDump: instance %d, qTable entry: %f", qlIter.first-'0', qTIter);
	}

	Log.log(LOG_LEVEL_NORMAL, "AgentHost::LearningDataDump:about to write learning data...");
	taskDataDS.rewind();
	taskDS.rewind();




	WritePerformanceData(&QLData);


	WCHAR tempLearningDataFile[512];
	wsprintf(tempLearningDataFile, _T("learningData%d.tmp"), STATE(AgentHost)->runNumber);
	std::ifstream    tempLearningData(tempLearningDataFile);
	bool fileExists = tempLearningData.good();
	if (fileExists) {		//Only dump learning data if there is no file with the current run number in the bin folder
		return 0;
	}
	tempLearningData.close();
	WriteLearningData(&taskDataDS, &taskDS, &QLData, &adviceData);

	taskDataDS.unlock();
	taskDS.unlock();

	return 0;
}

int AgentHost::WriteLearningData(DataStream *taskDataDS, DataStream *taskDS, mapDDBQLearningData *QLData, mapDDBAdviceData *adviceData) {
	Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 1");

	taskDataDS->unpackData(sizeof(UUID)); // discard thread
	char taskDataOk = taskDataDS->unpackChar();	//DDBR_OK
	taskDataDS->unpackBool();	//EnumTaskData
	int numTaskDatas = taskDataDS->unpackInt32();

	taskDS->unpackData(sizeof(UUID)); // discard thread
	char taskOk = taskDS->unpackChar();	//DDBR_OK
	taskDS->unpackBool();	//EnumTasks
	int numTasks = taskDS->unpackInt32();
	Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: numTasks is %d, numTaskDatas is %d", numTasks, numTaskDatas);
	if (taskDataOk != DDBR_OK || taskOk != DDBR_OK) {
		Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: DDBR_OK is false, aborting dump: taskDataOk is %c, taskOk is %c", taskDataOk, taskOk);
		return 1;
	}

	Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 2");

	if (QLData->size() == 0 && numTaskDatas == 0)
		return 0;	//No data to write

	Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 3");
	mapTask allTasks;	//A map of all tasks, used for extracting landmark type info
	UUID taskUUID;
	Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 4");
	for (int i = 0; i < numTasks; i++) {
		Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 4.1");
		DDBTask *task = (DDBTask *)malloc(sizeof(DDBTask));
		taskDS->unpackUUID(&taskUUID);
		Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 4.2");
		*task = *(DDBTask*)taskDS->unpackData(sizeof(DDBTask));
		Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 4.3");
		allTasks[taskUUID] = task;
	}
	Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 5");

	UUID avatarId;
	DDBTaskData taskData;
	int instance;	//Used for identifying avatars (specified in mission file)


	//Store learning data in .tmp file in bin dir for next run

	/*WCHAR learningArchiveFile[512];
	wsprintf(learningArchiveFile, _T("%s\\learningData.csv"), logDirectory);
	Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 6");*/
	WCHAR tempLearningDataFile[512];
	wsprintf(tempLearningDataFile, _T("learningData%d.tmp"), STATE(AgentHost)->runNumber);


	std::ofstream    tempLearningData(tempLearningDataFile);
	//std::ofstream    archiveLearningData(learningArchiveFile);


	RPC_WSTR stringUUID;
	int landmark_type;
	Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 7");
	for (int i = 0; i < numTaskDatas; i++) {
		 taskDataDS->unpackUUID(&avatarId);
		 taskDataDS->unpackTaskData(&taskData);
		 Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 7: avatarId id %s", Log.formatUUID(0,&avatarId));
		 //mapDDBAvatar::iterator iterAvatar = this->dStore->DDBAvatars.find(avatarId);

		 mapDDBAgent::iterator iterAvatar = this->dStore->DDBAgents.find(avatarId);
		 //if (iterAvatar == this->dStore->DDBAvatars.end())
			// Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 7: reached end of DDBAvatars");
		 ////UuidToString(&iterAvatar->second->agentTypeId, &stringUUID);
		 //instance = iterAvatar->second->agentTypeInstance;

		 if (iterAvatar == this->dStore->DDBAgents.end())
			 Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 7: reached end of DDBAgents");
		 //UuidToString(&iterAvatar->second->agentTypeId, &stringUUID);
		 instance = iterAvatar->second->agentInstance;
		 Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 7: Instance is %d", instance);
		 tempLearningData << "[TLData]\n";
		 tempLearningData << "id=" << instance << "\n";
		 for (auto& tauIter : taskData.tau) {
			 Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 7: landmark_type is %d", (int)allTasks[tauIter.first]->type);
			 Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 7: tau is %f", tauIter.second);
			 tempLearningData << "landmark_type=" << (int)allTasks[tauIter.first]->type << "\n";
			 tempLearningData << "tau=" << tauIter.second << "\n";
		 }
		 Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 7: end of tau loop");
		 tempLearningData << "\n";
		 //taskData.taskId
	}
	Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData:8");

	//Store all advice data

	for (auto& advIter : *adviceData) {
		Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData:8.1");
		tempLearningData << "[AdviserData]\n";
		instance = advIter.first;
		tempLearningData << "id=" << instance << "\n";
		tempLearningData << "cq=" << advIter.second.cq << "\n";
		Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData:8.2");
		tempLearningData << "bq=" << advIter.second.bq << "\n";
		tempLearningData << "\n";
		Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData:8.3");
	}

	Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 9");

	//Store all individual Q-learning data

	for (auto& QLIter : *QLData) {
		Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData:9.1");
		tempLearningData << "[QLData]\n";
		instance = QLIter.first;
		tempLearningData << "id=" << instance <<"\n";
		tempLearningData << "qTable=" << "\n";
		Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData:9.2");
		for (auto qtIter : QLIter.second.qTable) {
			if (qtIter > 0.0f)
				Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: Saving qVal %f", qtIter);
			tempLearningData << qtIter << "\n";
		}
		tempLearningData << "\nexpTable=" << "\n";
		for (auto exptIter : QLIter.second.expTable) {
			if (exptIter > 0)
				Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: Saving expVal %f", exptIter);
			tempLearningData << exptIter << "\n";
		}
		tempLearningData << "\n";
		Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData:9.3");
	}

	tempLearningData.close();

	Log.log(LOG_LEVEL_NORMAL, "AgentHost::WriteLearningData: 10");


	return 0;
}

int AgentHost::WritePerformanceData(mapDDBQLearningData * QLData)
{
	if (this->gatherData) {		//Collect performance data and save in .csv file

		long long totalActions = 0;
		long long usefulActions = 0;
		time_t timeNow = time(0);		//Get current time (for timestamping data)
		tm* utcTime = gmtime(&timeNow);

		for (auto& qlIter : *QLData) {	//Gather all action data
			totalActions = totalActions + qlIter.second.totalActions;
			usefulActions = usefulActions + qlIter.second.usefulActions;
		}

		std::ifstream testIfExists;
		std::ofstream outputData;

		testIfExists.open("performanceData.csv");
		if (!testIfExists.good()) {  //File does not exist, write headers
			Log.log(LOG_LEVEL_NORMAL, "AgentHost::WritePerformanceData: no performance records found, writing headers...");
			testIfExists.close();
			outputData.open("performanceData.csv", std::ios::app);
			outputData << "Time," << "runNumber," << " totalActions," << "usefulActions," << "totalSimSteps" << "\n";
			outputData.close();
		}
		else {					 //File exists, do not write headers
			testIfExists.close();
		}

		Log.log(LOG_LEVEL_NORMAL, "AgentHost::WritePerformanceData: appending data...");

		outputData.open("performanceData.csv", std::ios::app);
		//outputData << asctime(utcTime) << "," << STATE(AgentHost)->runNumber << "," << totalActions << "," << usefulActions << "\n";
		outputData << utcTime->tm_year + 1900 << "-" << setw(2) << setfill('0') << utcTime->tm_mon + 1 << "-" << utcTime->tm_mday;
		outputData << " " << utcTime->tm_hour << ":" << utcTime->tm_min << ":" << utcTime->tm_sec << ",";
		outputData << STATE(AgentHost)->runNumber << "," << totalActions << "," << usefulActions << "," << this->dStore->GetSimSteps() << "\n";
		outputData.close();
	}


	return 0;
}





int AgentHost::DumpStatistics() {

	Log.log( 0, "AgentHost::DumpStatistics:" );

	// turn off cout logging to speed things up
	int mode = Log.getLogMode();
	if ( mode & LOG_MODE_COUT ) {
		Log.unsetLogMode( LOG_MODE_COUT );
	}

	// final pf statistics
	this->dStore->PFDumpStatistics();

	// agent messages
	map<UUID,STATISTICS_MSGS,UUIDless>::iterator iM;
	for ( iM = this->statisticsMsgs.begin(); iM != this->statisticsMsgs.end(); iM++ ) {
		Log.log( 0, "AGENT_MESSAGES %s msgCount %d totalSize %d", Log.formatUUID(0,(UUID *)&iM->first), iM->second.msgCount, iM->second.dataSize );
	}

	// atomic messages
	{
		unsigned int decidedAvg, deliveredAvg;
		int decidedCount, deliveredCount;
		int decidedD, deliveredD;
		decidedAvg = 0;
		deliveredAvg = 0;
		decidedCount = 0;
		deliveredCount = 0;
		map<UUID,STATISTICS_ATOMIC_MSG,UUIDless>::iterator iAM;
		for ( iAM = this->statisticsAtomicMsg.begin(); iAM != this->statisticsAtomicMsg.end(); iAM++ ) {
			decidedD = (int)( (iAM->second.decidedT.time - iAM->second.startT.time)*1000 + (iAM->second.decidedT.millitm - iAM->second.startT.millitm) );
			decidedAvg += decidedD;
			decidedCount++;
			if ( iAM->second.delivered ) {
				deliveredD = (int)( (iAM->second.deliveredT.time - iAM->second.startT.time)*1000 + (iAM->second.deliveredT.millitm - iAM->second.startT.millitm) );
				deliveredAvg += deliveredD;
				deliveredCount++;
			} else {
				deliveredD = -1;
			}

			Log.log( 0, "ATOMIC_MESSAGE %s initiator %s participantCount %d order %d round %d msgsSent %d dataSent %d orderChanges %d delivered %d startT %d.%d decidedT %d.%d deliveredT %d.%d decidedD %d deliveredD %d", 
				Log.formatUUID(0,(UUID *)&iAM->first), Log.formatUUID( 0, &iAM->second.initiator ),
				iAM->second.participantCount, iAM->second.order, iAM->second.round, iAM->second.msgsSent, iAM->second.dataSent, iAM->second.orderChanges, iAM->second.delivered,
				(int)iAM->second.startT.time, (int)iAM->second.startT.millitm, 
				(int)iAM->second.decidedT.time, (int)iAM->second.decidedT.millitm, 
				(int)iAM->second.deliveredT.time, (int)iAM->second.deliveredT.millitm,
				decidedD, deliveredD );
		}
		if ( decidedCount && deliveredCount ) {
			Log.log( 0, "ATOMIC_MESSAGES decidedCount %d decidedAvg %f deliveredCount %d deliveredAvg %f", decidedCount, decidedAvg/(float)decidedCount, deliveredCount, deliveredAvg/(float)deliveredCount );
		}
	}

	// agent allocation
	{
		unsigned int decidedAvg;
		int decidedCount;
		int decidedD;
		decidedAvg = 0;
		decidedCount = 0;
		map<int,STATISTICS_AGENT_ALLOCATION>::iterator iAA;
		for ( iAA = this->statisticsAgentAllocation.begin(); iAA != this->statisticsAgentAllocation.end(); iAA++ ) {
			decidedD = (int)( (iAA->second.decidedT.time - iAA->second.startT.time)*1000 + (iAA->second.decidedT.millitm - iAA->second.startT.millitm) );
			if ( iAA->second.decided ) {
				decidedAvg += decidedD;
				decidedCount++;
			}

			Log.log( 0, "AGENT_ALLOCATION %d initiator %s participantCount %d agentCount %d round %d bundleBuilds %d msgsSent %d dataSent %d clustersFound %d biggestCluster %d startT %d.%d decided %d decidedT %d.%d decidedD %d", 
				iAA->first, Log.formatUUID( 0, &iAA->second.initiator ),
				iAA->second.participantCount, iAA->second.agentCount, iAA->second.round, iAA->second.bundleBuilds, 
				iAA->second.msgsSent, iAA->second.dataSent, iAA->second.clustersFound, iAA->second.biggestCluster,
				(int)iAA->second.startT.time, (int)iAA->second.startT.millitm, 
				iAA->second.decided, (int)iAA->second.decidedT.time, (int)iAA->second.decidedT.millitm, 
				decidedD );
		}
		if ( decidedCount ) {
			Log.log( 0, "AGENT_ALLOCATIONS decidedCount %d decidedAvg %f", decidedCount, decidedAvg/(float)decidedCount );
		}
	}

	// turn cout back on
	if ( mode & LOG_MODE_COUT ) {
		Log.setLogMode( LOG_MODE_COUT );
	}

	return 0;
}

int AgentHost::getFreePort( spConnection con, UUID *thread ) {
	int base_port;
	int i;
	int port = -1;
	for ( i=0; i<MAX_PORTS; i++ ) {
		if ( usedPorts[i] == 0 ) {
			usedPorts[i] = 1;
			sscanf_s( this->localAP.port, "%d", &base_port );
			port = base_port + 1 + i;
			break;
		}
	}

	this->ds.reset();
	this->ds.packUUID( thread );

	if ( port == -1 ) {
		this->ds.packChar( 0 );
		this->sendAgentMessage( &con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
		//this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
	} else { 
		this->ds.packChar( 1 );
		this->ds.packInt32( port );
		this->sendAgentMessage( &con->uuid, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
		//this->sendMessage( con, MSG_RESPONSE, this->ds.stream(), this->ds.length() );
	}

	this->ds.unlock();

	return 0;
}

int AgentHost::releasePort( int port ) {
	int base_port;
	sscanf_s( this->localAP.port, "%d", &base_port );
	this->usedPorts[ port - 1 - base_port ] = 0;
	return 0;
}


//-----------------------------------------------------------------------------
// Agent Info
/*
int AgentHost::updateAgentInfo( UUID *agent, AgentType *agentType, int activationMode, UUID *spawnThread ) {

	mapAgentInfo::iterator iA = this->agentInfo.find( *agent );
	if ( iA == this->agentInfo.end() ) {
		this->_addAgent( agent, agentType ); // we don't know this agent yet, initialize
	}

	AgentInfo *ai = &this->agentInfo[*agent];

	ai->type = *agentType;
	ai->activationMode = activationMode;
	ai->spawnThread = *spawnThread;
	
	return 0;
}
*/
int AgentHost::processCostUpdate( UUID *agent, unsigned int usage ) {
	DataStream lds;

	// update DDB
	lds.reset();
	lds.packInt32( DDBAGENTINFO_PROCESSCOST_UPDATE );
	lds.packFloat32( usage*this->processCapacity );
	lds.rewind();
	this->ddbAgentSetInfo( agent, &lds );
	lds.unlock();

	return 0;
}

int AgentHost::affinityUpdate( UUID *agent, AgentInfo *agentInfo, UUID *affinity, unsigned int size ) {
	map<UUID, AgentAffinityBlock, UUIDless>::iterator iAB = agentInfo->curAffinityBlock.find( *affinity );
	
	if ( iAB == agentInfo->curAffinityBlock.end() ) { // new block
		AgentAffinityBlock block;
		AgentAffinityCBData cbD;

		cbD.agent = *agent;
		cbD.affinity = *affinity;

		block.size = size;
		block.timeout = this->addTimeout( AGENTAFFINITY_CURBLOCK_TIMEOUT, AgentHost_CBR_cbAffinityCurBlock, (void *)&cbD, sizeof(AgentAffinityCBData) );
		if ( block.timeout == nilUUID ) {
			return 1;
		}
		agentInfo->curAffinityBlock[*affinity] = block;
	} else { // update block
		iAB->second.size += size;
	}

	return 0;
}

int AgentHost::_affinityUpdate( UUID *agent, UUID *affinity, unsigned int size ) {
	DataStream lds;

	// update DDB
	lds.reset();
	lds.packInt32( DDBAGENTINFO_AFFINITY_UPDATE );
	lds.packUUID( affinity );
	lds.packUInt32( size );
	lds.rewind();
	this->ddbAgentSetInfo( agent, &lds );
	lds.unlock();

	// calculate average affinity
	float dataRate = dStore->AgentGetAffinity( agent, affinity );

//	Log.log( LOG_LEVEL_VERBOSE, "AgentHost::cbAffinityCurBlock: agent data rate %s -> %s = %f",
//		Log.formatUUID( LOG_LEVEL_VERBOSE, agent ), Log.formatUUID( LOG_LEVEL_VERBOSE, affinity ), dataRate );

	return 0;
}


//-----------------------------------------------------------------------------
// Process Message

int AgentHost::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	RPC_STATUS Status;
	DataStream lds;
	UUID uuid;


	std::map<UUID, float, UUIDless>::const_iterator tauIter;
	std::map<UUID, float, UUIDless>::const_iterator motIter;
	std::map<UUID, float, UUIDless>::const_iterator impIter;
	std::map<UUID, int, UUIDless>::const_iterator attIter;			//REMOVEALLTHESE AFTER DEBUGGING!!!


/*	if ( con && !UuidIsNil( &con->uuid, &Status ) ) { // increment networkActivity if this is a host connection
		mapAgentHostState::iterator iterHS;
		iterHS = this->hostKnown.find( con->uuid );
		if ( iterHS != this->hostKnown.end() ) {
			if ( iterHS->second->status == STATUS_ACTIVE )
				iterHS->second->statusActivity++;
			else
				this->setHostKnownStatus( iterHS->second, STATUS_ACTIVE );
		}
	} */

	if ( con && !UuidIsNil( &con->uuid, &Status ) ) { // network statistics
		this->statisticsMsgs[con->uuid].msgCount++;
		this->statisticsMsgs[con->uuid].dataSize += len;
	}

	// we are a host so we can forward messages, but make sure it's not for us first!
	if ( message == MSG_FORWARD && UuidCompare( (UUID *)data, &STATE(AgentBase)->uuid, &Status ) ) { 
		bool fromLocal = false; // message is from one of our local agents
		bool toLocal = false; // message is to one of our local agents
		int status;
		spConnection fwdCon = NULL; // connection to forward on
		int queueMsg = 0; // 1 - primary, 2 - secondary, 3 - local

		// check if it is for a host
		mapAgentHostState::iterator iterAHS = this->hostKnown.find( *(UUID *)data );
		if ( iterAHS != this->hostKnown.end() ) {
			this->conSendMessage( iterAHS->second->connection, MSG_FORWARD, data, len, MSG_SIZE[MSG_FORWARD] );
			return 0; // message handled
		}
		
		// check if it is for an agent
		mapAgentInfo::iterator iterAI = this->agentInfo.find(*(UUID *)data);
		if (  iterAI == this->agentInfo.end() ) { // agent not found!
			return 0; // message handled
		} 

		mapAgentInfo::iterator jAI = this->agentInfo.find(con->uuid);
		if ( jAI != this->agentInfo.end() ) // we can only have connections to local agents
			fromLocal = true;
		if ( *dStore->AgentGetHost( (UUID *)&iterAI->first ) == *this->getUUID() )
			toLocal = true;

		// if this is from a local agent then record message size for affinity
		if ( fromLocal ) {
			this->affinityUpdate( (UUID *)&jAI->first, &jAI->second, (UUID *)data, len );
		}
		
		// check agent status to handle message appropriately
		status = dStore->AgentGetStatus( (UUID *)&iterAI->first );

		// debugging
		if ( 1 ) {
			static int fwdCount = 0;
			int fwdC;
			UUID returnId, thrd, to, from;
			unsigned char fwdMsg;
			UuidCreateNil( &returnId );
			UuidCreateNil( &thrd );
			to = iterAI->first;
			from = con->uuid;
			fwdC = ++fwdCount;
			lds.setData( data, len );
			if ( lds.unpackChar() ) // return present
				lds.unpackUUID( &returnId ); // return address
			fwdMsg = lds.unpackChar();
			if ( fwdMsg == MSG_RESPONSE ) {
				unsigned char sz = lds.unpackChar();
				if ( sz == 0xff ) 
					lds.unpackInt32(); // size
				lds.unpackUUID( &thrd );
			}
			lds.unlock();
			if ( fwdC == 151 )
				fwdC = fwdC;
		}

		if ( status == DDBAGENT_STATUS_READY || status == DDBAGENT_STATUS_SPAWNING ) {
			if ( toLocal ) { // send it on
				fwdCon = iterAI->second.con;
			} else { // forward to appropriate host
				mapAgentHostState::iterator iterAHS = this->hostKnown.find( *dStore->AgentGetHost( (UUID *)&iterAI->first ) );
				if ( iterAHS == this->hostKnown.end() ) {
					Log.log( 0, "AgentHost::conProcessMessage: received message to agent (%s) but no connection to host (%s)", Log.formatUUID(0,(UUID *)&iterAI->first), Log.formatUUID(0,dStore->AgentGetHost( (UUID *)&iterAI->first )) );
					return 0; // message handled
				}

				// TODO check connection graph if we don't have connection to this host
				fwdCon = iterAHS->second->connection;
			}
		} else if ( status == DDBAGENT_STATUS_FREEZING ) {
			if ( fromLocal ) { // forward to secondary queue
				queueMsg = 2; 
			} else { // from host, must have been sent before acknowledging our status change, forward to primary queue
				queueMsg = 1;
			}
		} else if ( status == DDBAGENT_STATUS_FROZEN ) {
			if ( fromLocal ) { // forward to secondary queue
				queueMsg = 2;
			} else { // from host, how did this happen?
				Log.log( 0, "AgentHost::conProcessMessage: received message to frozen agent from a host, should never happen! (%s)", Log.formatUUID(0,(UUID *)&iterAI->first) );
				return 0; // message handled
			}
		} else if ( status == DDBAGENT_STATUS_THAWING ) {
			if ( toLocal ) { // add to local queue
				queueMsg = 3;
			} else { // forward to appropriate host
				mapAgentHostState::iterator iterAHS = this->hostKnown.find( *dStore->AgentGetHost( (UUID *)&iterAI->first ) );
				if ( iterAHS == this->hostKnown.end() ) {
					Log.log( 0, "AgentHost::conProcessMessage: received message to agent (%s) but no connection to host (%s)", Log.formatUUID(0,(UUID *)&iterAI->first), Log.formatUUID(0,dStore->AgentGetHost( (UUID *)&iterAI->first )) );
					return 0; // message handled
				}

				// TODO check connection graph if we don't have connection to this host
				fwdCon = iterAHS->second->connection;
			}
		} else {
			Log.log( 0, "AgentHost::conProcessMessage: received message to agent with unacceptable status %d (%s)", status, Log.formatUUID(0,(UUID *)&iterAI->first) );
			return 0; // message handled
		}

		// forward/queue message
		if ( fwdCon ) {
			this->conSendMessage( fwdCon, MSG_FORWARD, data, len, MSG_SIZE[MSG_FORWARD] );
		} else if ( queueMsg == 1 ) { // primary queue
			lds.reset();
			lds.packInt32( DDBAGENTINFO_QUEUE_MSG ); 
			lds.packChar( 1 ); // primary
			lds.packUChar( message );
			lds.packUInt32( len );
			lds.packData( data, len );
			lds.rewind();
			this->ddbAgentSetInfo( (UUID *)&iterAI->first, &lds );
			lds.unlock();
		} else if ( queueMsg == 2 ) { // secondary queue
			lds.reset();
			lds.packInt32( DDBAGENTINFO_QUEUE_MSG ); 
			lds.packChar( 0 ); // secondary
			lds.packUChar( message );
			lds.packUInt32( len );
			lds.packData( data, len );
			lds.rewind();
			this->ddbAgentSetInfo( (UUID *)&iterAI->first, &lds );
			lds.unlock();
		} else if ( queueMsg == 3 ) { // local queue
			DDBAgent_MSG msgInfo;
			msgInfo.msg = message;
			msgInfo.len = len;
			msgInfo.data = (char *)malloc(len);
			if ( !msgInfo.data ) {
				Log.log( 0, "AgentHost::conProcessMessage: malloc error adding to local queue of agent %s", Log.formatUUID(0,(UUID *)&iterAI->first) );
				return 0; // message handled
			}
			memcpy( msgInfo.data, data, len );
			this->agentLocalMessageQueue[iterAI->first].push_back( msgInfo );
		}
		
		return 0; // message handled
	}

	if ( !AgentBase::conProcessMessage( con, message, data, len ) ) // message handled
		return 0;

	switch (message) {
	case OAC_MISSION_START:
	{
		char *misFile;
		lds.setData(data, len);
		misFile = lds.unpackString();
		strcpy_s(STATE(AgentBase)->missionFile, sizeof(STATE(AgentBase)->missionFile), misFile);
		this->parseMissionFile(misFile);
		lds.unlock();

		Data.log(0, "MISSION_START %s", misFile);

	}
	break;
	case MSG_MISSION_DONE:
	{
		UUID aTLTypeId;
		UuidFromString((RPC_WSTR)_T(AgentTeamLearning_UUID), &aTLTypeId);
		UUID aITTypeId;
		UuidFromString((RPC_WSTR)_T(AgentIndividualLearning_UUID), &aITTypeId);
		UUID aAETypeId;
		UuidFromString((RPC_WSTR)_T(AgentAdviceExchange_UUID), &aAETypeId);
		UUID eSTypeId;
		UuidFromString((RPC_WSTR)_T(ExecutiveSimulation_UUID), &eSTypeId);

		DataStream sds;

		//Go through all agents and find the learning agents and advice agents
		for (auto& iterAgent : this->dStore->DDBAgents) {

			//Check if learning or advice agent id matches the agent id
			if (iterAgent.second->agentTypeId == aITTypeId || iterAgent.second->agentTypeId == aTLTypeId || iterAgent.second->agentTypeId == aAETypeId || iterAgent.second->agentTypeId == eSTypeId) {
				// Order agent to upload learning or advice data for next run

				UUID agentUUID = iterAgent.first;
				Log.log(0, "AgentHost::conProcessMessage: MSG_MISSION_DONE, messaging agent %s", Log.formatUUID(0, &agentUUID));
				sds.reset();
				sds.packChar(1); // success
				//this->sendMessage(con, MSG_MISSION_DONE, sds.stream(),sds.length(),&agentUUID);
				sendAgentMessage(&agentUUID, MSG_MISSION_DONE, sds.stream(), sds.length());
				sds.unlock();
			}
		}

		UUID id = this->addTimeout(3000, AgentHost_CBR_cbMissionDone);
		if (id == nilUUID) {
			Log.log(0, "AgentHost::conProcessMessage: MSG_MISSION_DONE: addTimeout failed");
			return 1;
		}
	}
		break;
	case OAC_MISSION_DONE:
		Log.log( 0, "AgentHost::conProcessMessage: OAC_MISSION_DONE, mission done!" );
		this->prepareStop();
		this->DataDump( true );
		//this->LearningDataDump();
		Data.log( 0, "MISSION_DONE" );
		this->missionDone = true;
		break;
	case OAC_GM_REMOVE:
		{
			UUID key;
			UUID r;
			list<UUID> removalList;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &key );
			while ( lds.unpackBool() ) {
				lds.unpackUUID( &r );
				removalList.push_back( r );
			}
			this->hostGroupRemove( uuid, key, &removalList );
			lds.unlock();
		}
		break;
	case OAC_GM_MEMBERSHIP:
		{
			UUID key;
			UUID m;
			list<UUID> newMemberList;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &key );
			while ( lds.unpackBool() ) {
				lds.unpackUUID( &m );
				newMemberList.push_back( m );
			}
			this->hostGroupMembership( uuid, key, &newMemberList );
			lds.unlock();
		}
		break;
	case OAC_GM_FORMATIONFALLBACK:
		{
			UUID m;
			list<UUID> newMemberList;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			while ( lds.unpackBool() ) {
				lds.unpackUUID( &m );
				newMemberList.push_back( m );
			}
			this->hostGroupFormationFallback( uuid, &newMemberList );
			lds.unlock();
		}
		break;
	case OAC_PA_START:
		lds.setData( data, len );
		this->_cbbaPAStart( &lds );
		lds.unlock();
		break;
	case MSG_PA_BID_UPDATE:
		lds.setData( data, len );
		this->cbbaParseBidUpdate( &lds );
		lds.unlock();
		break;
	case OAC_PA_FINISH:
		lds.setData( data, len );
		this->cbbaDecided( &lds );
		lds.unlock();
		break;
	case MSG_RFREEPORT:
		this->getFreePort( con, (UUID *)data );
		break;
	case MSG_RELEASEPORT:
		this->releasePort( *(int *)data );
		break;
	case MSG_AGENT_REGISTER:
		lds.setData( data, len );
		this->recvAgentSpawned( con, &lds );
		lds.unlock();
		break;
	case MSG_AGENT_PROCESS_COST:
		lds.setData( data, len );
		lds.unpackUUID( &uuid );
		this->processCostUpdate( &uuid, lds.unpackUInt32() );
		lds.unlock();
		break;
	case MSG_AGENT_SHUTDOWN:
		this->killAgent( &con->uuid );
		break;
	case MSG_RUNIQUEID:
		lds.setData( data, len );
		this->recvRequestUniqueId( con, &lds );
		lds.unlock();
		break;
	case MSG_RAGENT_SPAWN:
		lds.setData( data, len );
		this->recvRequestAgentSpawn( con, &lds );
		lds.unlock();
		break;
	case MSG_AGENT_STATE:
		lds.setData( data, len );
		this->recvAgentState( &con->uuid, &lds );
		lds.unlock();
		break;
	case MSG_AGENT_RESUME_READY:
		this->recvAgentResumeReady( &con->uuid, (UUID *)data );
		break;
	case MSG_AGENT_BACKUP:
		lds.setData( data, len );
		this->recvAgentBackup( &con->uuid, &lds );
		lds.unlock();
		break;
	case MSG_AGENT_RECOVERED:
		{
			int result;
			lds.setData( data, len );
			lds.unpackUUID( &uuid ); // ticket
			result = lds.unpackInt32();
			lds.unlock();
			this->recvAgentRecovered( &con->uuid, &uuid, result );
		}
		break;
	case MSG_RGROUP_JOIN:
		{
			UUID group;
			lds.setData( data, len );
			lds.unpackUUID( &group );
			lds.unpackUUID( &uuid );
			this->groupJoin( group, uuid );
			lds.unlock();
		}
		break;
	case MSG_RGROUP_LEAVE:
		{
			UUID group;
			lds.setData( data, len );
			lds.unpackUUID( &group );
			lds.unpackUUID( &uuid );
			this->groupLeave( group, uuid );
			lds.unlock();
		}
		break;
	case MSG_GROUP_MSG:
		{
			UUID group;
			unsigned char gmsg;
			lds.setData( data, len );
			lds.unpackUUID( &group );
			gmsg = lds.unpackUChar();
			
		/*	if ( group == this->groupHostId ) {
				switch ( gmsg ) {
				default:
					break;
				}
			}
		*/	lds.unlock();
		}
		break;
	case MSG_RGROUP_SIZE:
		{
			UUID group;
			UUID thread;
			int groupSize;
			
			lds.setData( data, len );
			lds.unpackUUID( &group );
			lds.unpackUUID( &thread );
			lds.unlock();

			groupSize = this->groupSize( &group );

			lds.reset();
			lds.packUUID( &thread );
			lds.packInt32( groupSize );
			this->sendAgentMessage( &con->uuid, MSG_RESPONSE, lds.stream(), lds.length() );
			//this->sendMessage( con, MSG_RESPONSE, lds.stream(), lds.length() );
			lds.unlock();
		}
		break;
	case MSG__GROUP_SEND:
		{
			UUID group;
			unsigned char gmessage;
			char *gdata;
			unsigned int glen;
			unsigned int gmsgSize;

			lds.setData( data, len );
			lds.unpackUUID( &group );
			gmessage = lds.unpackUChar();
			glen = lds.unpackUInt32();
			gdata = (char *)lds.unpackData( glen );
			gmsgSize = lds.unpackUInt32();

			this->sendGroupMessage( &group, gmessage, gdata, glen, gmsgSize );
			lds.unlock();
		}
		break;

	// DDB messages
	case MSG_DDB_WATCH_TYPE:
		{
			int type;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			type = lds.unpackInt32();
			lds.unlock();
			this->ddbAddWatcher( &uuid, type );
		}
		break;
	case MSG_DDB_WATCH_ITEM:
		{
			UUID item;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &item );
			lds.unlock();
			this->ddbAddWatcher( &uuid, &item );
		}
		break;
	case MSG_DDB_STOP_WATCHING_TYPE:
		{
			int type;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			type = lds.unpackInt32();
			lds.unlock();
			this->ddbRemWatcher( &uuid, type );
		}
		break;
	case MSG_DDB_STOP_WATCHING_ITEM:
		{
			UUID item;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &item );
			lds.unlock();
			this->ddbRemWatcher( &uuid, &item );
		}
		break;
	case MSG_DDB_ADDAGENT:
		{
/*			UUID parentId;
			AgentType *agentType;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &parentId );
			agentType = (AgentType *)lds.unpackData( sizeof(agentType) );
			this->ddbAddAgent( &uuid, &parentId, agentType );
			lds.unlock();
*/		}
		break;
	case MSG_DDB_REMAGENT:
		memcpy( &uuid, data, sizeof(UUID) );
		this->ddbRemoveAgent( &uuid );
		break;	
	case MSG_DDB_RAGENTINFO:
		{
			int infoFlags;
			UUID thread;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			infoFlags = lds.unpackInt32();
			lds.unpackUUID( &thread );
			lds.unlock();
			this->ddbAgentGetInfo( &uuid, infoFlags, con, &thread );
		}
		break;
	case MSG_DDB_VIS_ADDPATH:
		{
			UUID agentId;
			int id, count;
			float *x, *y;
			lds.setData( data, len );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			count = lds.unpackInt32();
			x = (float *)lds.unpackData(count*sizeof(float));
			y = (float *)lds.unpackData(count*sizeof(float));
			this->ddbVisAddPath( &agentId, id, count, x, y );
			lds.unlock();
		}
		break;
	case MSG_DDB_VIS_REMPATH:
		{
			UUID agentId;
			int id;
			lds.setData( data, len );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			lds.unlock();
			this->ddbVisRemovePath( &agentId, id );
		}
		break;
	case MSG_DDB_VIS_EXTENDPATH:
	case MSG_DDB_VIS_UPDATEPATH:
		break; // TODO
	case MSG_DDB_VIS_ADDOBJECT:
		{
			int i;
			UUID agentId;
			int id;
			float x, y, r, s;
			int count;
			int *paths;
			float **colours, *lineWidths;
			bool solid;
			char *name;
			lds.setData( data, len );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			r = lds.unpackFloat32();
			s = lds.unpackFloat32();
			count = lds.unpackInt32();
			paths = (int *)lds.unpackData(count*sizeof(int));
			colours = (float **)malloc(count*sizeof(float*));
			if ( !colours ) {// malloc failed!
				lds.unlock();
				return 0;
			}
			for ( i=0; i<count; i++ ) {
				colours[i] = (float *)lds.unpackData(sizeof(float)*3);
			}
			lineWidths = (float *)lds.unpackData(count*sizeof(float));
			solid = lds.unpackBool();
			name = lds.unpackString();
			this->ddbVisAddStaticObject( &agentId, id, x, y, r, s, count, paths, colours, lineWidths, solid, name );
			lds.unlock();
			free( colours );
		}
		break;
	case MSG_DDB_VIS_REMOBJECT:
		{
			UUID agentId;
			int id;
			lds.setData( data, len );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			lds.unlock();
			this->ddbVisRemoveObject( &agentId, id );
		}
		break;
	case MSG_DDB_VIS_UPDATEOBJECT:
		{
			UUID agentId;
			int id;
			float x, y, r, s;
			lds.setData( data, len );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			r = lds.unpackFloat32();
			s = lds.unpackFloat32();
			lds.unlock();
			this->ddbVisUpdateObject( &agentId, id, x, y, r, s );
		}
		break;
	case MSG_DDB_VIS_SETOBJECTVISIBLE:
		{
			int objectId;
			char visible;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			objectId = lds.unpackInt32();
			visible = lds.unpackChar();
			lds.unlock();
			this->ddbVisSetObjectVisible( &uuid, objectId, visible );
		}
		break;
	case MSG_DDB_VIS_CLEAR_ALL:
		{
			char clearPaths;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			clearPaths = lds.unpackChar();
			lds.unlock();
			this->ddbVisClearAll( &uuid, clearPaths );
		}
		break;

	case MSG_DDB_ADDREGION:
		{
			float x, y, w, h;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			w = lds.unpackFloat32();
			h = lds.unpackFloat32();
			lds.unlock();
			this->ddbAddRegion( &uuid, x, y, w, h );
		}
		break;
	case MSG_DDB_REMREGION:
		memcpy( &uuid, data, sizeof(UUID) );
		this->ddbRemoveRegion( &uuid );
		break;
	case MSG_DDB_RREGION:
		{
			UUID thread;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &thread );
			lds.unlock();
			this->ddbGetRegion( &uuid, con, &thread );
		}
		break;
	case MSG_DDB_ADDLANDMARK:
		{
			unsigned char code;
			UUID owner;
			float height, elevation, x, y;
			char estimatedPos;
			ITEM_TYPES landmarkType;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			code = lds.unpackUChar();
			lds.unpackUUID( &owner );
			height = lds.unpackFloat32();
			elevation = lds.unpackFloat32();
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			estimatedPos = lds.unpackChar();
			landmarkType = (ITEM_TYPES)lds.unpackInt32();
			lds.unlock();
			this->ddbAddLandmark( &uuid, code, &owner, height, elevation, x, y, estimatedPos, landmarkType );
		}
		break;
	case MSG_DDB_REMLANDMARK:
		memcpy( &uuid, data, sizeof(UUID) );
		this->ddbRemoveLandmark( &uuid );
		break;
	case MSG_DDB_LANDMARKSETINFO:
		this->ddbLandmarkSetInfo( data, len );
		break;
	case MSG_DDB_RLANDMARK:
		{
			UUID thread;
			bool enumLandmarks;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &thread );
			enumLandmarks = lds.unpackBool();
			lds.unlock();
			/*if(enumLandmarks)
				Log.log(0, "Received request for landmark list.");*/
			this->ddbGetLandmark( &uuid, con, &thread, enumLandmarks );
		}
		break;
	case MSG_DDB_RLANDMARKBYID:
		{
			unsigned char code;
			UUID thread;
			lds.setData( data, len );
			code = lds.unpackUChar();
			lds.unpackUUID( &thread );
			lds.unlock();
			this->ddbGetLandmark( code, con, &thread );
		}
		break;
	case MSG_DDB_ADDPOG:
		{
			float tileSize, resolution;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			tileSize = lds.unpackFloat32();
			resolution = lds.unpackFloat32();
			lds.unlock();
			this->ddbAddPOG( &uuid, tileSize, resolution );
		}
		break;
	case MSG_DDB_REMPOG:
		memcpy( &uuid, data, sizeof(UUID) );
		this->ddbRemovePOG( &uuid );
		break;
	case MSG_DDB_RPOGINFO:
		{
			UUID thread;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &thread );	
			lds.unlock();
			this->ddbPOGGetInfo( &uuid, con, &thread );
		}
		break;
	case MSG_DDB_APPLYPOGUPDATE:
		{
			int offset;
			float x, y, w, h;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			w = lds.unpackFloat32();
			h = lds.unpackFloat32();
			offset = sizeof(UUID) + 4*4;
			lds.unlock();
			this->ddbApplyPOGUpdate( &uuid, x, y, w, h, (float *)(data + offset) );
		}
		break;
	case MSG_DDB_RPOGREGION:
		{
			float x, y, w, h;
			UUID thread;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			w = lds.unpackFloat32();
			h = lds.unpackFloat32();
			lds.unpackUUID( &thread );
			lds.unlock();
			this->ddbPOGGetRegion( &uuid, x, y, w, h, con, &thread );
		}
		break;
	case MSG__DDB_POGLOADREGION:
		{
			Log.log(0, "agentHost::POGLOAD for mapReveal");
			float x, y, w, h;
			char *filename;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			w = lds.unpackFloat32();
			h = lds.unpackFloat32();
			filename = lds.unpackString();
			Log.log(0, "agentHost::POGLOAD filename is %s", filename);
			int hasFailed;
			hasFailed = this->ddbPOGLoadRegion( &uuid, x, y, w, h, filename );
			if (hasFailed)
				Log.log(0, "agentHost::POGLOAD for mapReveal returned with a 1");
			lds.unlock();
		}
		break;
	case MSG__DDB_POGDUMPREGION:
		{
			float x, y, w, h;
			char *filename;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			w = lds.unpackFloat32();
			h = lds.unpackFloat32();
			filename = lds.unpackString();
			this->dStore->POGDumpRegion( &uuid, x, y, w, h, filename );		
			lds.unlock();
		}
		break;
	case MSG_DDB_ADDPARTICLEFILTER:
		{
			UUID owner;
			int numParticles, stateSize, offset;
			_timeb *startTime;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &owner );
			numParticles = lds.unpackInt32();
			startTime = (_timeb *)lds.unpackData( sizeof(_timeb) );
			stateSize = lds.unpackInt32();
			offset = sizeof(UUID)*2 + 4 + sizeof(_timeb) + 4;
			this->ddbAddParticleFilter( &uuid, &owner, numParticles, startTime, (float *)(data + offset), stateSize );
			lds.unlock();
		}
		break;
	case MSG_DDB_REMPARTICLEFILTER:
		memcpy( &uuid, data, sizeof(UUID) );
		this->ddbRemoveParticleFilter( &uuid );
		break;
	case MSG_DDB_INSERTPFPREDICTION:
		{
			int offset;
			_timeb *tb;
			bool nochange;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			tb = (_timeb *)lds.unpackData( sizeof(_timeb) );
			nochange = lds.unpackChar() ? 1 : 0;
			offset = sizeof(UUID) + sizeof(_timeb) + 1;
			this->ddbInsertPFPrediction( &uuid, tb, (float *)(data + offset), nochange );
			lds.unlock();
		}
		break;
	case MSG_DDB_APPLYPFCORRECTION:
		{
			int offset;
			_timeb *tb;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			tb = (_timeb *)lds.unpackData( sizeof(_timeb) );
			offset = sizeof(UUID) + sizeof(_timeb);
			this->ddbApplyPFCorrection( &uuid, tb, (float *)(data + offset) );
			lds.unlock();
		}
		break;
	case MSG_DDB_RPFINFO:
		{
			_timeb tb;
			int infoFlags;
			UUID thread;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			infoFlags = lds.unpackInt32();
			tb = *(_timeb*)lds.unpackData( sizeof(_timeb) );
			lds.unpackUUID( &thread );
			this->ddbPFGetInfo( &uuid, infoFlags, &tb, con, &thread );
			lds.unlock();
		}
		break;
	case MSG_DDB_SUBMITPFRESAMPLE:
		lds.setData( data, len );
		this->ddbApplyPFResample( &lds );
		lds.unlock();
		break;
/*	case MSG__DDB_RESAMPLEPF_LOCK:
		{
			UUID key, thread, host;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &key );
			lds.unpackUUID( &thread );
			lds.unpackUUID( &host );
			lds.unlock();
			this->_ddbResampleParticleFilter_Lock( &uuid, con, &key, &thread, &host );
		}
		break;
	case MSG__DDB_RESAMPLEPF_UNLOCK:
		{
			lds.setData( data, len );
			this->_ddbResampleParticleFilter_Unlock( &lds );
			lds.unlock();
		}
		break;*/
	case MSG__DDB_PFDUMPINFO:
		{
			int infoFlags;
			_timeb startT, endT;
			char *filename;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			infoFlags = lds.unpackInt32();
			startT = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			endT = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			filename = lds.unpackString();
			this->dStore->PFDump( &uuid, infoFlags, &startT, &endT, filename );
			lds.unlock();
		}
		break;
	case MSG_DDB_ADDAVATAR:
		{
			char type[64];
			int status;
			UUID agent, pf;
			float innerRadius, outerRadius;
			_timeb startTime;
			int capacity;
			int sensorTypes;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			strncpy_s( type, sizeof(type), lds.unpackString(), sizeof(type) );
			status = lds.unpackInt32();
			lds.unpackUUID( &agent );
			lds.unpackUUID( &pf );
			innerRadius = lds.unpackFloat32();
			outerRadius = lds.unpackFloat32();
			startTime = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			capacity = lds.unpackInt32();
			sensorTypes = lds.unpackInt32();
			lds.unlock();
			this->ddbAddAvatar( &uuid, type, status, &agent, &pf, innerRadius, outerRadius, &startTime, capacity, sensorTypes );
		}
		break;
	case MSG_DDB_REMAVATAR:
		memcpy( &uuid, data, sizeof(UUID) );
		this->ddbRemoveAvatar( &uuid );
		break;
	case MSG_DDB_AVATARSETINFO:
		this->ddbAvatarSetInfo( data, len );
		break;
	case MSG_DDB_AVATARGETINFO:
		{
			int infoFlags;
			UUID thread;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			infoFlags = lds.unpackInt32();
			lds.unpackUUID( &thread );
			lds.unlock();
			this->ddbAvatarGetInfo( &uuid, infoFlags, con, &thread );
		}
		break;
	case MSG_DDB_ADDSENSOR:
		{
			UUID avatar, pf;
			int type, offset;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			type = lds.unpackInt32();
			lds.unpackUUID( &avatar );
			lds.unpackUUID( &pf );
			offset = sizeof(UUID) + 4 + sizeof(UUID)*2;
			lds.unlock();
			this->ddbAddSensor( &uuid, type, &avatar, &pf, data + offset, len - offset );
		}
		break;
	case MSG_DDB_REMSENSOR:
		memcpy( &uuid, data, sizeof(UUID) );
		this->ddbRemoveSensor( &uuid );
		break;
	case MSG_DDB_INSERTSENSORREADING:
		{
			int readingSize, dataSize;
			void *reading, *rdata;
			_timeb tb;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			readingSize = lds.unpackInt32();
			reading = lds.unpackData( readingSize );
			dataSize = lds.unpackInt32();
			if ( dataSize ) rdata = lds.unpackData( dataSize );
			else rdata = NULL;
			this->ddbInsertSensorReading( &uuid, &tb, reading, readingSize, rdata, dataSize );
			lds.unlock();
		}
		break;
	case MSG_DDB_RSENSORINFO:
		{
			int infoFlags;
			UUID thread;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			infoFlags = lds.unpackInt32();
			lds.unpackUUID( &thread );
			lds.unlock();
			this->ddbSensorGetInfo( &uuid, infoFlags, con, &thread );
		}
		break;
	case MSG_DDB_RSENSORDATA:
		{
			_timeb tb;
			UUID thread;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			tb = *(_timeb*)lds.unpackData( sizeof(_timeb) );
			lds.unpackUUID( &thread );
			this->ddbSensorGetData( &uuid, &tb, con, &thread );
			lds.unlock();
		}
		break;
	case MSG_DDB_ADDTASK:
		{
			UUID landmarkUUID;
			UUID agentUUID;
			UUID avatarUUID;
			bool completed;
			ITEM_TYPES TYPE;

			lds.setData(data, len);
			lds.unpackUUID(&uuid);

			lds.unpackUUID(&landmarkUUID);
			lds.unpackUUID(&agentUUID);
			lds.unpackUUID(&avatarUUID);
			completed = lds.unpackBool();
			TYPE = (ITEM_TYPES)lds.unpackInt32();
			
			lds.unlock();
			this->ddbAddTask(&uuid, &landmarkUUID, &agentUUID, &avatarUUID, completed, TYPE);
		}
		break;
	case MSG_DDB_REMTASK:
	{
		memcpy(&uuid, data, sizeof(UUID));
		this->ddbRemoveTask(&uuid);
	}
		break;
	case MSG_DDB_TASKSETINFO:
	{
		UUID agentUUID;
		UUID avatarUUID;
		bool completed;
		//Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_TASKSETINFO ");
		lds.setData(data, len);
		lds.unpackUUID(&uuid);
		lds.unpackUUID(&agentUUID);
		lds.unpackUUID(&avatarUUID);
		completed = lds.unpackBool();
		lds.unlock();
		this->ddbTaskSetInfo(&uuid, &agentUUID, &avatarUUID, completed);
	}
		break;
	case MSG_DDB_TASKGETINFO:
	{
		UUID thread;
		bool enumTasks;
		//Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_TASKGETINFO ");
		lds.setData(data, len);
		lds.unpackUUID(&uuid);
		lds.unpackUUID(&thread);
		enumTasks = lds.unpackBool();
		lds.unlock();
		this->ddbGetTask(&uuid, con, &thread, enumTasks);
	}
		break;
	case MSG_DDB_ADDTASKDATA:
	{
	//	Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_ADDTASKDATA ");
		DDBTaskData	taskData;

		lds.setData(data, len);
		lds.unpackUUID(&uuid);		//Avatar id (owner of the taskdata set)
		lds.unpackTaskData(&taskData);
		lds.unlock();

	/*	Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_ADDTASKDATA avatarId: %s", Log.formatUUID(0, &uuid));
		Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_ADDTASKDATA taskId: %s", Log.formatUUID(0, &taskData.taskId));

		for (tauIter = taskData.tau.begin(); tauIter != taskData.tau.end(); tauIter++) {
			Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_ADDTASKDATA tauIter: %f", tauIter->second);
		}

		for (motIter = taskData.motivation.begin(); motIter != taskData.motivation.end(); motIter++) {
			Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_ADDTASKDATA motIter: %f", motIter->second);
		}

		for (impIter = taskData.impatience.begin(); impIter != taskData.impatience.end(); impIter++) {
			Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_ADDTASKDATA impIter: %f", impIter->second);
		}

		for (attIter = taskData.attempts.begin(); attIter != taskData.attempts.end(); attIter++) {
			Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_ADDTASKDATA attIter: %d", attIter->second);
		}
		Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_ADDTASKDATA psi: %d", taskData.psi);
		Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_ADDTASKDATA tauStdDev: %f", taskData.tauStdDev);
*/
		this->ddbAddTaskData(&uuid, &taskData);

	}
		break;
	case MSG_DDB_REMTASKDATA:
	{
		memcpy(&uuid, data, sizeof(UUID));
		this->ddbRemoveTaskData(&uuid);
	}
		break;
	case MSG_DDB_TASKDATASETINFO:
	{
	//	Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_TASKDATASETINFO");
		DDBTaskData	taskData;
		UUID avatarUUID;

		lds.setData(data, len);
		lds.unpackUUID(&avatarUUID); //Avatar id (owner of the taskdata set)
	//	Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_TASKDATASETINFO avatarId: %s", Log.formatUUID(0, &avatarUUID));
		lds.unpackTaskData(&taskData);
		lds.unlock();
		

	/*	Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_TASKDATASETINFO taskId: %s", Log.formatUUID(0, &taskData.taskId));

		for (tauIter = taskData.tau.begin(); tauIter != taskData.tau.end(); tauIter++) {
			Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_TASKDATASETINFO tauIter: %f", tauIter->second);
		}

		for (motIter = taskData.motivation.begin(); motIter != taskData.motivation.end(); motIter++) {
			Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_TASKDATASETINFO motIter: %f", motIter->second);
		}

		for (impIter = taskData.impatience.begin(); impIter != taskData.impatience.end(); impIter++) {
			Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_TASKDATASETINFO impIter: %f", impIter->second);
		}
		for (attIter = taskData.attempts.begin(); attIter != taskData.attempts.end(); attIter++) {
			Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_TASKDATASETINFO attIter: %d", attIter->second);
		}
		Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_TASKDATASETINFO psi: %d", taskData.psi);
		Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_TASKDATASETINFO tauStdDev: %f", taskData.tauStdDev);


*/
		this->ddbTaskDataSetInfo(&avatarUUID, &taskData);
	}
		break;
	case MSG_DDB_TASKDATAGETINFO:
	{
	//	Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_TASKDATAGETINFO ");
		UUID thread;
		bool enumTaskData;
		lds.setData(data, len);
		lds.unpackUUID(&uuid);
		lds.unpackUUID(&thread);
		enumTaskData = lds.unpackBool();
		lds.unlock();
	//	Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_TASKDATAGETINFO: enumTaskData is %s", enumTaskData ? "true":"false");

		this->ddbGetTaskData(&uuid, con, &thread, enumTaskData);
	}
	break;
	case MSG_DDB_TL_ROUND_INFO:
	{

		lds.setData(data, len);
		this->ddbTLRoundSetInfo(&lds);
		lds.unlock();
	}
	break;
	case MSG_DDB_TL_GET_ROUND_INFO:
	{

		UUID thread;
		lds.setData(data, len);
		lds.unpackUUID(&thread);
		lds.unlock();

		this->ddbTLRoundGetInfo(con, &thread);
	}
	break;
	case MSG_DDB_QLEARNINGDATA:
	{
		Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_QLEARNINGDATA ");

		UUID ownerId;
		char instance;	//Type of avatar, stored in the mission file
		long long totalActions;
		long long usefulActions;
		int tableSize;
		bool onlyActions;

		std::vector<float> qTable;             // Vector of all Q-values
		std::vector<unsigned int> expTable;    // Vector of all experience values

		lds.setData(data, len);
		lds.unpackUUID(&ownerId);		//Avatar id (owner of the data set)
		instance = lds.unpackChar();
		onlyActions = lds.unpackBool();
		totalActions = lds.unpackInt64();
		usefulActions = lds.unpackInt64();
		if (!onlyActions) {
			tableSize = lds.unpackInt32();

			/*qTable.resize(tableSize, 0);
			expTable.resize(tableSize, 0);*/

			qTable.clear();
			expTable.clear();

			for (int i = 0; i < tableSize; i++) {
				qTable.push_back(lds.unpackFloat32());						//Pack all values in q-table
				//if (qTable.back() > 0.0f)
				//	Log.log(LOG_LEVEL_NORMAL, "AgentHost::conProcessMessage::MSG_DDB_QLEARNINGDATA:received qVal: %f", qTable.back());
			}
			for (int i = 0; i < tableSize; i++) {
				expTable.push_back(lds.unpackUInt32());						//Pack all values in exp-table
				//if (expTable.back() > 0)
				//	Log.log(LOG_LEVEL_NORMAL, "AgentHost::conProcessMessage::MSG_DDB_QLEARNINGDATA:received expVal: %d", expTable.back());
			}
		}
		lds.unlock();
		this->ddbAddQLearningData(onlyActions, instance, totalActions, usefulActions, tableSize, qTable, expTable);
	}
	break;
	case MSG_DDB_ADVICEDATA:
	{
		//Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_ADVICEDATA ");

		UUID avatarId;
		char instance;	//Type of avatar, stored in the mission file
		float cq;
		float bq;

		lds.setData(data, len);
		lds.unpackUUID(&avatarId);		//Avatar id (owner of the data set)
		instance = lds.unpackChar();
		cq = lds.unpackFloat32();
		bq = lds.unpackFloat32();
		lds.unlock();
		this->ddbAddAdviceData(instance, cq, bq);
	}
	break;
	case MSG_DDB_SIMSTEPS:
	{
		Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_SIMSTEPS ");

		unsigned long long totalSimSteps;

		lds.setData(data, len);
		totalSimSteps = lds.unpackUInt64();
		Log.log(0, "AgentHost::conProcessMessage: MSG_DDB_SIMSTEPS: simsteps is %llu ", totalSimSteps);
		lds.unlock();
		this->ddbAddSimSteps(totalSimSteps);
	}
	break;


	case MSG_DDB_RHOSTGROUPSIZE:
	{
		lds.setData(data, len);
		lds.unpackUUID(&uuid); // thread
		lds.unlock();
		lds.reset();
		lds.packUUID(&uuid); // thread
		lds.packChar(DDBR_OK);
		lds.packInt32((int)this->gmMemberList.size());
		this->sendAgentMessage(&con->uuid, MSG_RESPONSE, lds.stream(), lds.length());
		lds.unlock();
	}
		break;
	case MSG_CBBA_DECIDED:
	{
		lds.setData(data, len);
		this->cbbaDecided(&lds);
		lds.unlock();
	}
		break;
	case MSG__DATADUMP_AVATARPOSES:
	{
		lds.setData(data, len);
		this->DataDump_AvatarPose(&lds);
		lds.unlock();
	}
		break;
	case MSG__DATADUMP_MANUAL:
		{
			bool fulldump, getPose;
			char *label;
			lds.setData( data, len );
			fulldump = lds.unpackBool();
			getPose = lds.unpackBool();
			if ( lds.unpackBool() ) // has label
				label = lds.unpackString();
			if ( !this->gatherData ) { // we need to prep the file
				char logName[256];
				char timeBuf[64];
				char configStrip[64];
				time_t t_t;
				struct tm stm;
				apb->apbtime( &t_t );
				localtime_s( &stm, &t_t );
				strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
				strcpy_s( configStrip, sizeof(configStrip), this->configPath );
				char *c = configStrip;
				while ( *c != 0 ) {
					if ( *c == '\\' || *c == '/' ) *c = '_';
					c++;
				}
				sprintf_s( logName, "%s\\xDATA AgentHost %s %s.txt", logDirectory, configStrip, timeBuf );

				Data.setLogMode( LOG_MODE_FILE, logName );
				Data.setLogLevel( LOG_LEVEL_ALL );

				this->gatherData = true;
			}
			this->DataDump( fulldump, getPose, label );
			lds.unlock();
		}
		break;

	case MSG_RRUNNUMBER:
		{
			Log.log(0, "AgentHost::conProcessMessage: MSG_RRUNNUMBER ");
			UUID thread;
			UUID sender;
			lds.setData(data, len);
			lds.unpackUUID(&thread);
			//lds.unpackUUID(&sender);
			lds.unlock();

			//Send reply with run number to requesting agent
			lds.reset();
			lds.packUUID(&thread); // thread
			lds.packInt32(STATE(AgentHost)->runNumber);
			this->sendAgentMessage(&con->uuid, MSG_RESPONSE, lds.stream(), lds.length());
			lds.unlock();

		}
		break;
	// AgentHost messages
	case AgentHost_MSGS::MSG_RSUPERVISOR_REFRESH:
		this->sendHostLabelAll( con );
		break;
	case AgentHost_MSGS::MSG_HOST_INTRODUCE:
		memcpy( &uuid, data, sizeof(UUID) );
		this->recvHostIntroduce( con, &uuid );
		break;
	case AgentHost_MSGS::MSG_HOST_SHUTDOWN:
		//this->agentLost( &con->uuid, AGENT_LOST_WILLING );
		break;
	case AgentHost_MSGS::MSG_HOST_LABEL:
		lds.setData( data, len );
		this->recvHostLabel( con, &lds );
		lds.unlock();
		break;
	case AgentHost_MSGS::MSG_RHOST_LABEL:
		memcpy( &uuid, data, sizeof(UUID) );
		this->sendHostLabel( con, &uuid );
		break;
	case AgentHost_MSGS::MSG_HOST_STUB:
		lds.setData( data, len );
		this->recvHostStub( con, &lds );
		lds.unlock();
		break;
	case AgentHost_MSGS::MSG_RHOST_STUB:
		memcpy( &uuid, data, sizeof(UUID) );
		this->sendHostStub( con, &uuid );
		break;
	case AgentHost_MSGS::MSG_OTHER_STATUS:
		{
	/*		memcpy( &uuid, data, sizeof(UUID) );
			mapAgentHostState::iterator iterHS;
			iterHS = this->hostKnown.find( uuid );
			if ( iterHS != this->hostKnown.end() ) {
				if ( iterHS->second->status != STATUS_ALIVE )
					this->setHostKnownStatus( iterHS->second, STATUS_ALIVE ); // mark the host as alive
			}
	*/	}
		break;
	case AgentHost_MSGS::MSG_ROTHER_STATUS:
		{
			memcpy( &uuid, data, sizeof(UUID) );
			mapAgentHostState::iterator iterHS;
			iterHS = this->hostKnown.find( uuid );
			if ( iterHS != this->hostKnown.end() 
			  && iterHS->second->status == STATUS_ACTIVE ) {
				this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_OTHER_STATUS), data, len ); // tell them we know the host is active
			}	
		}
		break;
/*	case AgentHost_MSGS::MSG_AGENTSPAWNPROPOSAL:
		lds.setData( data, len );
		this->recvAgentSpawnProposal( con, &lds );
		lds.unlock();
		break;
	case AgentHost_MSGS::MSG_RAGENTSPAWNPROPOSAL:
		{
			UUID ticket;
			AgentType type;
			lds.setData( data, len );
			lds.unpackUUID( &ticket );
			lds.unpackUUID( &type.uuid );
			type.instance = lds.unpackChar();
			lds.unlock();
			this->generateAgentSpawnProposal( con, &ticket, &type );
		}
		break;
	case AgentHost_MSGS::MSG_ACCEPTAGENTSPAWNPROPOSAL:
		lds.setData( data, len );
		this->recvAcceptAgentSpawnProposal( con, &lds );
		lds.unlock();
		break;
	case AgentHost_MSGS::MSG_AGENTSPAWNSUCCEEDED:
		lds.setData( data, len );
		this->recvAgentSpawnSucceeded( con, &lds );
		lds.unlock();
		break;
	case AgentHost_MSGS::MSG_AGENTSPAWNFAILED:
		lds.setData( data, len );
		this->recvAgentSpawnFailed( con, &lds );
		lds.unlock();
		break;
	case AgentHost_MSGS::MSG_AGENT_KILL:
		this->killAgent( (UUID *)data );
		break;
*/	case AgentHost_MSGS::MSG_AGENT_ACK_STATUS:
		{
			UUID from, ticket;
			lds.setData( data, len );
			lds.unpackUUID( &from );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &ticket );
			lds.unlock();
			this->AgentTransferUpdate( &from, &uuid, &ticket ); // status
		}
		break;
	case AgentHost_MSGS::MSG_CLOSE_DUPLICATE:
		{
			// see if we know who this is
			if ( con->uuid != nilUUID ) {
				// see if we have a valid replacement connection
				mapConnection::iterator iC;
				for ( iC = this->connection.begin(); iC != this->connection.end(); iC++ ) {
					if ( iC->second != con && iC->second->uuid == con->uuid ) { // potential connection, but it must be them to us
						if ( apCmp( &iC->second->ap, &STATE(AgentHost)->serverAP ) == 0 )
							break; // ok
					}
				}
				if ( iC != this->connection.end() ) { // we do have one, replace the active connection if necessary and close this connection
					mapAgentHostState::iterator iH = this->hostKnown.find( con->uuid );
					if ( iH != this->hostKnown.end() ) {
						if ( iH->second->connection == con ) {
							// replace connection and refresh status
							iH->second->connection = iC->second;
							this->connectionStatusChanged( iC->second, this->connectionStatus( iC->second->uuid ) );
						}
					}

					Log.log( 0, "AgentHost::conProcessMessage: MSG_CLOSE_DUPLICATE queuing close duplicate connection (%s)", Log.formatUUID(0,&con->uuid) );
					
					this->addTimeout( 10000, AgentHost_CBR_cbQueueCloseConnection, &con->index, sizeof(UUID) );
				} 
			}
		}
		break;
	case AgentHost_MSGS::MSG_GM_LEAVE:
		this->hostGroupLeaveRequest( *(UUID *)data );
		break;
	case AgentHost_MSGS::MSG_GM_APPLY:
		{
			spAddressPort ap;
			__int64 key[2];
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			ap = (spAddressPort)lds.unpackData( sizeof(sAddressPort) );
			key[0] = lds.unpackInt64();
			key[1] = lds.unpackInt64();
			this->hostGroupApply( uuid, ap, key );
			lds.unlock();
		}
		break;
	case AgentHost_MSGS::MSG_GM_INTRODUCE:
		{
			UUID a;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &a );
			this->hostGroupIntroduce( uuid, a, (spAddressPort)lds.unpackData( sizeof(sAddressPort) ) );
			lds.unlock();
		}
		break;
	case AgentHost_MSGS::MSG_GM_SPONSOR:
		this->hostGroupSponsor( *(UUID *)data );
		break;
	case AgentHost_MSGS::MSG_GM_CONNECTIONS:
		{
			lds.setData( data, len );
			this->hostGroupSponseeUpdate( &lds );
			lds.unlock();
		}
		break;
	case AgentHost_MSGS::MSG_GM_ACKLOCK:
		{
			UUID key;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &key );
			lds.unlock();
			this->hostGroupAckLock( uuid, key );
		}
		break;
	case AgentHost_MSGS::MSG_GM_REJECT:
		Log.log( 0, "AgentHost::conProcessMessage: MSG_GM_REJECT rejected from host group, stopping" );
		this->prepareStop(); // rejected
		break;
	case AgentHost_MSGS::MSG_GM_GLOBALSTATEEND:
		lds.setData( data, len );
		lds.unpackUUID( &uuid );
		lds.unlock();
		this->hostGroupGlobalStateEnd( uuid );
		break;
	case AgentHost_MSGS::MSG_MIRROR_REGISTER:
		lds.setData( data, len );
		this->recvAgentMirrorRegister( con, &lds );
		lds.unlock();
		break;
	case AgentHost_MSGS::MSG_DDB_PFRESAMPLE_CHECK:
		lds.setData( data, len );
		lds.unpackUUID( &uuid );
		lds.unlock();
		this->ddbResampleParticleFilter( &uuid );
		break;

	// Distribute messages
/*	case AgentHost_MSGS::MSG_DISTRIBUTE_ADD_AGENT:
		{
			UUID agent;
			AgentType agentType;
			lds.setData( data, len );
			lds.unpackUUID( &agent );
			agentType = *(AgentType *)lds.unpackData( sizeof(AgentType) );
			lds.unlock();
			this->_addAgent( &agent, &agentType );
		}
		break;
	case AgentHost_MSGS::MSG_DISTRIBUTE_REM_AGENT:
		{
			UUID agent;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &agent );
			lds.unlock();
			this->_remAgent( &uuid, &agent );
		}
		break;
*/	case AgentHost_MSGS::MSG_DISTRIBUTE_ADD_UNIQUE:
		this->_addUnique( (AgentType *)data );
		break;
	case AgentHost_MSGS::MSG_DISTRIBUTE_AGENT_LOST:
		{
			int reason;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			reason = lds.unpackInt32();
			lds.unlock();
			this->_agentLost( &uuid, reason );
		}
		break;
	case AgentHost_MSGS::MSG_DISTRIBUTE_ADD_MIRROR:
		lds.setData( data, len );
		lds.unpackUUID( &uuid );
		lds.unlock();
		this->_ddbAddMirror( &uuid );
		break;
	case AgentHost_MSGS::MSG_DISTRIBUTE_REM_MIRROR:
		lds.setData( data, len );
		lds.unpackUUID( &uuid );
		lds.unlock();
		this->_ddbRemMirror( &uuid );
		break;
	case AgentHost_MSGS::MSG_RGROUP_MERGE:
		lds.setData( data, len );
		this->groupMergeRequest( &lds );
		lds.unlock();
		break;
	case AgentHost_MSGS::MSG_DISTRIBUTE_GROUP_MERGE:
		lds.setData( data, len );
		this->_groupMerge( &lds );
		lds.unlock();
		break;
	case AgentHost_MSGS::MSG_DISTRIBUTE_GROUP_JOIN:
		{
			UUID group;
			_timeb tb;
			lds.setData( data, len );
			lds.unpackUUID( &group );
			lds.unpackUUID( &uuid );
			tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			lds.unlock();
			
			this->_groupJoin( group, uuid, tb );
		}
		break;
	case AgentHost_MSGS::MSG_DISTRIBUTE_GROUP_LEAVE:
		{
			UUID group;
			lds.setData( data, len );
			lds.unpackUUID( &group );
			lds.unpackUUID( &uuid );
			lds.unlock();
			
			this->_groupLeave( group, uuid );
		}
		break;
	case OAC_STATE_TRANSACTION_BUNDLE:
		{
			unsigned char bmsg;
			unsigned int blen;
			char *bdata;
			lds.setData( data, len );
			while ( lds.unpackBool() ) {
				bmsg = lds.unpackUChar();
				blen = lds.unpackUInt32();
				if ( blen ) bdata = (char *)lds.unpackData( blen );
				else bdata = NULL;
				this->conProcessMessage( con, bmsg, bdata, blen );
			}
			lds.unlock();
		}
		break;
	case OAC_DDB_WATCH_TYPE:
		lds.setData( data, len );
		lds.unpackUUID( &uuid );
		this->_ddbAddWatcher( &uuid, lds.unpackInt32() );
		lds.unlock();
		this->globalStateChangeForward( message, data, len ); // forward to sponsees

		break;
	case OAC_DDB_WATCH_ITEM:
		{
			UUID item;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &item );
			lds.unlock();
			this->_ddbAddWatcher( &uuid, &item );
			this->globalStateChangeForward( message, data, len ); // forward to sponsees
		}
		break;
	case OAC_DDB_STOP_WATCHING_TYPE:
		lds.setData( data, len );
		lds.unpackUUID( &uuid );
		this->_ddbRemWatcher( &uuid, lds.unpackInt32() );
		lds.unlock();
		this->globalStateChangeForward( message, data, len ); // forward to sponsees
		break;
	case OAC_DDB_STOP_WATCHING_ITEM:
		{
			UUID item;
			lds.setData( data, len );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &item );
			lds.unlock();
			this->_ddbRemWatcher( &uuid, &item );
			this->globalStateChangeForward( message, data, len ); // forward to sponsees
		}
		break;
	case OAC_DDB_CLEAR_WATCHERS:
		lds.setData( data, len );
		lds.unpackUUID( &uuid );
		lds.unlock();
		this->_ddbClearWatchers( &uuid );
		this->globalStateChangeForward( message, data, len ); // forward to sponsees
		break;
	case AgentHost_MSGS::MSG_DDB_ENUMERATE:
		lds.setData( data, len );
		this->_ddbParseEnumerate( &lds );
		lds.unlock();
		break;
	case OAC_DDB_ADDAGENT:
		{
			UUID parentId;
			AgentType *agentType;
			UUID sender, spawnThread;
			float parentAffinity, processCost;
			char priority;
			int activationMode;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &parentId );
			agentType = (AgentType *)lds.unpackData( sizeof(AgentType) );
			lds.unpackUUID( &spawnThread );
			parentAffinity = lds.unpackFloat32();
			priority = lds.unpackChar();
			processCost = lds.unpackFloat32();
			activationMode = lds.unpackInt32();
			this->dStore->AddAgent( &uuid, &parentId, agentType->name, &agentType->uuid, agentType->instance, &spawnThread, parentAffinity, priority, processCost, activationMode );
			this->_ddbNotifyWatchers( &this->atomicMessageDeliveryId(), DDB_AGENT, DDBE_ADD, &uuid );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees

			Data.log( 0, "AGENT_ADD %s parent %s type %s instance %d (%s)", Data.formatUUID(0,&uuid), Data.formatUUID(0,&parentId), Data.formatUUID(0,&agentType->uuid), agentType->instance, agentType->name );

			this->_addAgent2( &uuid, agentType, &spawnThread, activationMode, priority );
			lds.unlock();
			if ( activationMode == AM_NORMAL )
				this->cbbaPAStart(); // start PA session
		}
		break;
	case OAC_DDB_REMAGENT:
		{
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unlock();
			this->dStore->RemoveAgent( &uuid );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_REM, &uuid );
			this->_ddbClearWatchers( &uuid );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees

			Data.log( 0, "AGENT_REM %s", Data.formatUUID(0,&uuid) );

			this->_remAgent2( &uuid );
			this->cbbaPAStart(); // start PA session

		}
		break;
	case OAC_DDB_AGENTSETINFO:
		{
			int infoFlags;
			UUID sender, owner;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			infoFlags = lds.unpackInt32();

			if (1) { // debug
				UUID breakId;
				UuidFromString( (RPC_WSTR)_T("1689f437-07ae-4d50-a4cb-4b081547183e"), &breakId );

				if ( breakId == uuid )
					int i=0;
			}

			// only the owner (or winner if owner is nil) can modify agent host
			owner = *dStore->AgentGetHost( &uuid );
			if ( !(infoFlags & DDBAGENTINFO_HOST) || (sender == owner || (owner == nilUUID && sender == this->agentAllocation[uuid])) ) { // ok
				lds.rewind();
				lds.unpackData( 2*sizeof(UUID) ); // run to AgentSetInfo data start
				infoFlags = lds.unpackInt32();
				infoFlags = this->dStore->AgentSetInfo( &uuid, infoFlags, &lds );
				if ( infoFlags ) {
					// notify watchers
					this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_AGENT_UPDATE, &uuid, (void *)&infoFlags, sizeof(int) );
				}
				
				this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
				this->AgentTransferInfoChanged( &uuid, infoFlags );

				// Data dump
				if ( infoFlags & DDBAGENTINFO_HOST ) {
					Data.log( 0, "AGENT_HOST_CHANGED %s newhost %s", Data.formatUUID(0,&uuid), Data.formatUUID(0,dStore->AgentGetHost( &uuid )) );
				}
				if ( infoFlags & DDBAGENTINFO_STATUS ) {
					Data.log( 0, "AGENT_STATUS_CHANGED %s status %d", Data.formatUUID(0,&uuid), dStore->AgentGetStatus( &uuid ) );
				}

			} else {
				Log.log( LOG_LEVEL_NORMAL, "AgentHost::conProcessMessage: OAC_DDB_AGENTSETINFO disregarding HOST/STATUS transaction on %s from %s, not owner/winner", Log.formatUUID(LOG_LEVEL_NORMAL,&uuid), Log.formatUUID(LOG_LEVEL_NORMAL,&sender) );
				// if we were expecting this status clear the flag
				// make sure this matches the unpacking of AgentSetInfo!
				UUID host, ticket;
				if ( infoFlags & DDBAGENTINFO_HOST ) {
					lds.unpackUUID( &host );
					lds.unpackUUID( &ticket );
				}

				if ( infoFlags & DDBAGENTINFO_STATUS ) {
					lds.unpackInt32(); // status
					lds.unpackUUID( &ticket );
				}

				mapAgentInfo::iterator iAI = this->agentInfo.find(uuid);
				if ( iAI == this->agentInfo.end() ) {
					Log.log( 0, "AgentHost::conProcessMessage: OAC_DDB_AGENTSETINFO agent info not found %s", Log.formatUUID( 0, &uuid ) );
					return 0;
				} 

				if ( !this->agentInfo[uuid].expectingStatus.empty() && this->agentInfo[uuid].expectingStatus.front() == ticket ) {
					Log.log( LOG_LEVEL_NORMAL, "AgentHost::conProcessMessage: OAC_DDB_AGENTSETINFO expectingStatus, ticket removed", Log.formatUUID(LOG_LEVEL_NORMAL,&ticket) );
					this->agentInfo[uuid].expectingStatus.pop_front();
				}
			}
			lds.unlock();
		}
		break;
	case OAC_DDB_VIS_ADDPATH:
		{
			UUID agentId;
			int id, count;
			float *x, *y;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			count = lds.unpackInt32();
			x = (float *)lds.unpackData(count*sizeof(float));
			y = (float *)lds.unpackData(count*sizeof(float));
		
			//Log.log( LOG_LEVEL_VERBOSE, "AgentHost::conProcessMessage: %s DDBE_VIS_ADDPATH pathId %d, %d nodes", Log.formatUUID(LOG_LEVEL_VERBOSE,&agentId), id, count);
		
			this->dStore->VisAddPath( &agentId, id, count, x, y );
			lds.unlock();
			this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_VIS_ADDPATH, &agentId, &id, sizeof(int) );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_VIS_REMPATH:
		{
			UUID agentId;
			int id;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			lds.unlock();

			//Log.log( LOG_LEVEL_VERBOSE, "AgentHost::conProcessMessage: %s DDBE_VIS_REMPATH pathId %d", Log.formatUUID(LOG_LEVEL_VERBOSE,&agentId), id );
		
			this->dStore->VisRemovePath( &agentId, id );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_VIS_REMPATH, &agentId, &id, sizeof(int) );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_VIS_EXTENDPATH:
	case OAC_DDB_VIS_UPDATEPATH:
		break; // TODO
	case OAC_DDB_VIS_ADDOBJECT:
		{
			int i;
			UUID agentId;
			int id;
			float x, y, r, s;
			int count;
			int *paths;
			float **colours, *lineWidths;
			bool solid;
			char *name;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			r = lds.unpackFloat32();
			s = lds.unpackFloat32();
			count = lds.unpackInt32();
			paths = (int *)lds.unpackData(count*sizeof(int));
			colours = (float **)malloc(count*sizeof(float*));
			if ( !colours ) { // malloc failed!
				lds.unlock();
				return 0;
			}
			for ( i=0; i<count; i++ ) {
				colours[i] = (float *)lds.unpackData(sizeof(float)*3);
			}
			lineWidths = (float *)lds.unpackData(count*sizeof(float));
			solid = lds.unpackBool();
			name = lds.unpackString();
	
			//Log.log( LOG_LEVEL_VERBOSE, "AgentHost::conProcessMessage: %s DDBE_VIS_ADDOBJECT objectId %d, name %s x %f y %f r %f", Log.formatUUID(LOG_LEVEL_VERBOSE,&agentId), id, name, x, y, r );
		
			this->dStore->VisAddStaticObject( &agentId, id, x, y, r, s, count, paths, colours, lineWidths, solid, name );
			free( colours );
			lds.unlock();
			this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_VIS_ADDOBJECT, &agentId, &id, sizeof(int) );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_VIS_REMOBJECT:
		{
			UUID agentId;
			int id;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			lds.unlock();
			
			//Log.log( LOG_LEVEL_VERBOSE, "AgentHost::conProcessMessage: %s DDBE_VIS_REMOBJECT objectId %d", Log.formatUUID(LOG_LEVEL_VERBOSE,&agentId), id );

			this->dStore->VisRemoveObject( &agentId, id );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_VIS_REMOBJECT, &agentId, &id, sizeof(int) );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_VIS_UPDATEOBJECT:
		{
			UUID agentId;
			int id;
			float x, y, r, s;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &agentId );
			id = lds.unpackInt32();
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			r = lds.unpackFloat32();
			s = lds.unpackFloat32();
			lds.unlock();
			this->dStore->VisUpdateObject( &agentId, id, x, y, r, s );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_VIS_UPDATEOBJECT, &agentId, &id, sizeof(int) );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_VIS_SETOBJECTVISIBLE:
		{
			int objectId;
			char visible;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			objectId = lds.unpackInt32();
			visible = lds.unpackChar();
			lds.unlock();
			this->dStore->VisSetObjectVisible( &uuid, objectId, visible );
			lds.reset();
			lds.packInt32( objectId );
			lds.packChar( MSG_DDB_VIS_SETOBJECTVISIBLE );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_VIS_UPDATEOBJECT, &uuid, lds.stream(), lds.length() );
			lds.unlock();
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_VIS_CLEAR_ALL:
		{
			UUID sender;
			char clearPaths;
			list<int> objects;
			list<int> paths;
			list<int>::iterator iI;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			clearPaths = lds.unpackChar();
			lds.unlock();
			this->dStore->VisListObjects( &uuid, &objects );
			if ( clearPaths )
				this->dStore->VisListPaths( &uuid, &paths );
			this->dStore->VisClearAll( &uuid, clearPaths );
			for ( iI = objects.begin(); iI != objects.end(); iI++ ) {
				this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_VIS_REMOBJECT, &uuid, &*iI, sizeof(int) );
			}
			if ( clearPaths ) {
				for ( iI = paths.begin(); iI != paths.end(); iI++ ) {
					this->_ddbNotifyWatchers( this->getUUID(), DDB_AGENT, DDBE_VIS_REMPATH, &uuid, &*iI, sizeof(int) );
				}
			}
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_ADDREGION:
		{
			float x, y, w, h;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			w = lds.unpackFloat32();
			h = lds.unpackFloat32();
			this->dStore->AddRegion( &uuid, x, y, w, h );
			lds.unlock();
			this->_ddbNotifyWatchers( this->getUUID(), DDB_REGION, DDBE_ADD, &uuid );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_REMREGION:
		{	
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unlock();
			this->dStore->RemoveRegion( &uuid );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_REGION, DDBE_REM, &uuid );
			this->_ddbClearWatchers( &uuid );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_ADDLANDMARK:
		{
			unsigned char code;
			UUID owner;
			float height, elevation, x, y;	
			char estimatedPos;
			UUID sender;
			ITEM_TYPES landmarkType;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			code = lds.unpackChar();
			lds.unpackUUID( &owner );
			height = lds.unpackFloat32();
			elevation = lds.unpackFloat32();
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			estimatedPos = lds.unpackChar();
			landmarkType = (ITEM_TYPES) lds.unpackInt32();
			this->dStore->AddLandmark( &uuid, code, &owner, height, elevation, x, y, (estimatedPos ? true : false), landmarkType );
			lds.unlock();
			this->_ddbNotifyWatchers( this->getUUID(), DDB_LANDMARK, DDBE_ADD, &uuid );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_REMLANDMARK:
		{
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unlock();
			this->dStore->RemoveLandmark( &uuid );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_LANDMARK, DDBE_REM, &uuid );
			this->_ddbClearWatchers( &uuid );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_LANDMARKSETINFO:
		{
			UUID sender;
			int infoFlags;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			infoFlags = lds.unpackInt32();
			infoFlags = this->dStore->LandmarkSetInfo( &uuid, infoFlags, &lds );
			lds.unlock();
			if ( infoFlags ) {
				this->_ddbNotifyWatchers( this->getUUID(), DDB_LANDMARK, DDBE_UPDATE, &uuid, &infoFlags, sizeof(int) );
			}
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees	
		}
		break;
	case OAC_DDB_ADDPOG:
		{
			float tileSize, resolution;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			tileSize = lds.unpackFloat32();
			resolution = lds.unpackFloat32();
			this->dStore->AddPOG( &uuid, tileSize, resolution );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_MAP_PROBOCCGRID, DDBE_ADD, &uuid );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
			lds.unlock();
		}
		break;
	case OAC_DDB_REMPOG:
		{
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unlock();
			this->dStore->RemovePOG( &uuid );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_MAP_PROBOCCGRID, DDBE_REM, &uuid );
			this->_ddbClearWatchers( &uuid );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_APPLYPOGUPDATE:
		{
			float x, y, w, h;
			int updateSize;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			w = lds.unpackFloat32();
			h = lds.unpackFloat32();
			updateSize = lds.unpackInt32();
			this->dStore->ApplyPOGUpdate( &uuid, x, y, w, h, (float *)lds.unpackData( updateSize ) );
			lds.unlock();
			
			lds.reset();
			lds.packFloat32( x );
			lds.packFloat32( y );
			lds.packFloat32( w );
			lds.packFloat32( h );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_MAP_PROBOCCGRID, DDBE_POG_UPDATE, &uuid, lds.stream(), lds.length() );
			lds.unlock();

			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_POGLOADREGION:
		{
			float x, y, w, h;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			x = lds.unpackFloat32();
			y = lds.unpackFloat32();
			w = lds.unpackFloat32();
			h = lds.unpackFloat32();
			this->dStore->POGLoadRegion( &uuid, x, y, w, h, lds.unpackString() );
			lds.unlock();

			lds.reset();
			lds.packFloat32( x );
			lds.packFloat32( y );
			lds.packFloat32( w );
			lds.packFloat32( h );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_MAP_PROBOCCGRID, DDBE_POG_LOADREGION, &uuid, lds.stream(), lds.length() );
			lds.unlock();

			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_ADDPARTICLEFILTER:
		{
			UUID owner;
			int numParticles, stateSize;
			_timeb tb;
			float *startState;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unpackUUID( &owner );
			numParticles = lds.unpackInt32();
			tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			stateSize = lds.unpackInt32();
			startState = (float *)lds.unpackData( sizeof(float)*numParticles*stateSize );
			this->dStore->AddParticleFilter( &uuid, &owner, numParticles, &tb, startState, stateSize );
			lds.unlock();
			this->_ddbNotifyWatchers( this->getUUID(), DDB_PARTICLEFILTER, DDBE_ADD, &uuid );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_REMPARTICLEFILTER:
		{
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			this->_ddbRemoveParticleFilter( &uuid );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_PARTICLEFILTER, DDBE_REM, &uuid );
			this->_ddbClearWatchers( &uuid );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_INSERTPFPREDICTION:
		{
			int offset;
			_timeb tb;
			bool nochange;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			nochange = lds.unpackChar() ? 1 : 0;
			offset = sizeof(UUID)*2 + sizeof(_timeb) + 1;
			this->dStore->InsertPFPrediction( &uuid, &tb, (float *)(data + offset), nochange );
			lds.unlock();

			lds.reset();
			lds.packData( &tb, sizeof(_timeb) );
			lds.packChar( (char)nochange );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_PARTICLEFILTER, DDBE_PF_PREDICTION, &uuid, lds.stream(), lds.length() );
			lds.unlock();

			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_APPLYPFCORRECTION:
		{
			int regionAge, offset;
			_timeb tb;
			offset = sizeof(UUID)*2 + 4 + sizeof(_timeb);
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			regionAge = lds.unpackInt32();
			tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			lds.unlock();
			if ( this->offlineSLAMmode == 0 || this->offlineSLAMmode == 1 || this->offlineSLAMmode == 5 ) // ideal, discard, no forward prop
				this->dStore->ApplyPFCorrection( &uuid, regionAge, &tb, (float *)(data + offset), true );
			else
				this->dStore->ApplyPFCorrection( &uuid, regionAge, &tb, (float *)(data + offset) );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_PARTICLEFILTER, DDBE_PF_CORRECTION, &uuid, (char *)&tb, sizeof(_timeb) );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_APPLYPFRESAMPLE:
		lds.setData( data, len );
		lds.unpackUUID( &uuid );
		lds.rewind();
		this->_ddbApplyPFResample( &lds );
		lds.unlock();

		float effectiveParticleNum;
		_timeb *tb;
		dStore->PFGetInfo( &uuid, DDBPFINFO_USECURRENTTIME, NULL, &lds, &nilUUID, &effectiveParticleNum );

		lds.rewind();
		lds.unpackData(sizeof(UUID)); // discard thread
		if ( lds.unpackChar() == DDBR_OK ) {
			tb = (_timeb *)lds.unpackData(sizeof(_timeb));
			this->_ddbNotifyWatchers( this->getUUID(), DDB_PARTICLEFILTER, DDBE_PF_RESAMPLE, &uuid, (char *)tb, sizeof(_timeb) );
		}
		lds.unlock();

		this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		break;
	case OAC_DDB_ADDAVATAR:
		{
			char type[64];
			int status;
			UUID agent;
			UUID pf;
			float innerRadius, outerRadius;
			_timeb startTime;
			int capacity;
			int sensorTypes;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			strncpy_s( type, sizeof(type), lds.unpackString(), sizeof(type) );
			status = lds.unpackInt32();
			lds.unpackUUID( &agent );
			lds.unpackUUID( &pf );
			innerRadius = lds.unpackFloat32();
			outerRadius = lds.unpackFloat32();
			startTime = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			capacity = lds.unpackInt32();
			sensorTypes = lds.unpackInt32();
			lds.unlock();

			mapAgentInfo::iterator iAI = this->agentInfo.find(agent);
			if ( iAI == this->agentInfo.end() ) {
				Log.log( 0, "AgentHost::conProcessMessage: OAC_DDB_ADDAVATAR agent info not found %s", Log.formatUUID( 0, &agent ) );
				return 0;
			} 

			AgentType agentType = this->agentInfo[agent].type;
			this->dStore->AddAvatar( &uuid, type, &agentType, status, &agent, &pf, innerRadius, outerRadius, &startTime, capacity, sensorTypes );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_AVATAR, DDBE_ADD, &uuid );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees

			// Data dump
			Data.log( 0, "AVATAR_ADDED %s agent %s type %s", Data.formatUUID(0,&uuid), Data.formatUUID(0,&agent), type );
		}
		break;
	case OAC_DDB_REMAVATAR:
		{
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );		
			lds.unlock();
			this->dStore->RemoveAvatar( &uuid );
			this->_ddbNotifyWatchers( this->getUUID(), DDB_AVATAR, DDBE_REM, &uuid );
			this->_ddbClearWatchers( &uuid );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_AVATARSETINFO:
		{
			UUID sender;
			int infoFlags;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			infoFlags = lds.unpackInt32();
			lds.unlock();
			infoFlags = this->dStore->AvatarSetInfo( &uuid, infoFlags, &lds );
			if ( infoFlags )
				this->_ddbNotifyWatchers( this->getUUID(), DDB_AVATAR, DDBE_UPDATE, &uuid, &infoFlags, sizeof(int) );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees

			// data dump
			if ( infoFlags & DDBAVATARINFO_RETIRE ) {
				dStore->AvatarGetInfo( &uuid, DDBAVATARINFO_RTIMECARD, &lds, &nilUUID );
				lds.rewind();
				lds.unpackData(sizeof(UUID)); // thread
				lds.unpackChar(); // OK
				lds.unpackInt32(); // infoFlags
				lds.unpackData(sizeof(_timeb)); // startTime
				char retired = lds.unpackChar();
				_timeb endTime = *(_timeb *)lds.unpackData(sizeof(_timeb)); // endTime
				lds.unlock();
				Data.log( 0, "AVATAR_RETIRE %s %d %d.%d", Data.formatUUID(0,&uuid), retired, (int)endTime.time, (int)endTime.millitm );
			}
			if ( infoFlags & DDBAVATARINFO_CARGO ) {
				dStore->AvatarGetInfo( &uuid, DDBAVATARINFO_RCARGO, &lds, &nilUUID );
				lds.rewind();
				lds.unpackData(sizeof(UUID)); // thread
				lds.unpackChar(); // OK
				lds.unpackInt32(); // infoFlags
				int cargoCount = lds.unpackInt32();
				lds.unlock();
				Data.log( 0, "AVATAR_CARGO %s %d", Data.formatUUID(0,&uuid), cargoCount );
			}
			if ( infoFlags & DDBAVATARINFO_CONTROLLER ) {
				UUID controller;
				dStore->AvatarGetInfo( &uuid, DDBAVATARINFO_RCONTROLLER, &lds, &nilUUID );
				lds.rewind();
				lds.unpackData(sizeof(UUID)); // thread
				lds.unpackChar(); // OK
				lds.unpackInt32(); // infoFlags
				lds.unpackUUID( &controller );
				int index = lds.unpackInt32();
				int priority = lds.unpackInt32();
				lds.unlock();
				Data.log( 0, "AVATAR_CONTROLLER %s %s %d %d", Data.formatUUID(0,&uuid), Data.formatUUID(0,&controller), index, priority );
			}

		}
		break;
	case OAC_DDB_ADDSENSOR:
		{
			UUID avatar, pf;
			int type, offset;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			type = lds.unpackInt32();
			lds.unpackUUID( &avatar );
			lds.unpackUUID( &pf );
			offset = sizeof(UUID)*2 + 4 + sizeof(UUID) + sizeof(UUID);
			lds.unlock();
			this->dStore->AddSensor( &uuid, type, &avatar, &pf, data + offset, len - offset );
			this->_ddbNotifyWatchers( this->getUUID(), type, DDBE_ADD, &uuid );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_REMSENSOR:
		{
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			lds.unlock();
			int type = this->dStore->GetSensorType( &uuid );
			this->dStore->RemoveSensor( &uuid );
			this->_ddbNotifyWatchers( this->getUUID(), type, DDBE_REM, &uuid );
			this->_ddbClearWatchers( &uuid );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_INSERTSENSORREADING:
		{
			_timeb tb;
			void *reading, *rdata;
			int readingSize, dataSize;
			UUID sender;
			lds.setData( data, len );
			lds.unpackUUID( &sender );
			lds.unpackUUID( &uuid );
			tb = *(_timeb *)lds.unpackData( sizeof(_timeb) );
			readingSize = lds.unpackInt32();
			reading = lds.unpackData( readingSize );
			dataSize = lds.unpackInt32();
			if ( dataSize ) rdata = lds.unpackData( dataSize );
			else rdata = NULL;
			this->dStore->InsertSensorReading( &uuid, &tb, reading, readingSize, rdata, dataSize );
			lds.unlock();
			int type = this->dStore->GetSensorType( &uuid );
			this->_ddbNotifyWatchers( this->getUUID(), type, DDBE_SENSOR_UPDATE, &uuid, (void *)&tb, sizeof(_timeb) );
			this->globalStateChangeForward( message, data, len ); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_ADDTASK:
	{
		UUID sender;
		UUID landmarkUUID;
		UUID agentUUID;
		UUID avatarUUID;
		bool completed;
		ITEM_TYPES TYPE;
		
		lds.setData(data, len);
		lds.unpackUUID(&sender);
		lds.unpackUUID(&uuid);
		lds.unpackUUID(&landmarkUUID);
		lds.unpackUUID(&agentUUID);
		lds.unpackUUID(&avatarUUID);
		completed = lds.unpackBool();
		TYPE = (ITEM_TYPES)lds.unpackInt32();

		this->dStore->AddTask(&uuid, &landmarkUUID, &agentUUID, &avatarUUID, completed, TYPE);
		lds.unlock();
		this->_ddbNotifyWatchers(this->getUUID(), DDB_TASK, DDBE_ADD, &uuid);
		this->globalStateChangeForward(message, data, len); // forward to mirrors and sponsees
	}
		break;
	case OAC_DDB_REMTASK:
	{
		UUID sender;
		lds.setData(data, len);
		lds.unpackUUID(&sender);
		lds.unpackUUID(&uuid);
		lds.unlock();
		this->dStore->RemoveTask(&uuid);
		this->_ddbNotifyWatchers(this->getUUID(), DDB_TASK, DDBE_REM, &uuid);
		this->_ddbClearWatchers(&uuid);
		this->globalStateChangeForward(message, data, len); // forward to mirrors and sponsees
	}
	break;
	case OAC_DDB_TASKSETINFO:
	{
		Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_TASKSETINFO ");
		UUID sender;
		UUID agentUUID;
		UUID avatar;
		bool completed;
		int isTaskSet;

		lds.setData(data, len);
		lds.unpackUUID(&sender);
		lds.unpackUUID(&uuid);
		lds.unpackUUID(&agentUUID);
		lds.unpackUUID(&avatar);
		completed = lds.unpackBool();
		isTaskSet = this->dStore->TaskSetInfo(&uuid, &agentUUID, &avatar, completed);	//isTaskSet is 1 if id not found or within update threshold
	//	Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_TASKSETINFO: isTaskSet is %s ", isTaskSet ? "true":"false");
		lds.unlock();
		if (isTaskSet == 0 || isTaskSet == 2) {
			this->_ddbNotifyWatchers(this->getUUID(), DDB_TASK, DDBE_UPDATE, &uuid);
		}
		if(isTaskSet == 2)
			Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_TASKSETINFO within AVATAR CHANGE THRESHOLD");
		this->globalStateChangeForward(message, data, len); // forward to mirrors and sponsees	
	}
	break;
	case OAC_DDB_ADDTASKDATA:
	{
		UUID sender;
		UUID avatarUUID;
		DDBTaskData taskData;


		lds.setData(data, len);
		lds.unpackUUID(&sender);
		lds.unpackUUID(&avatarUUID);
		lds.unpackTaskData(&taskData);

		//Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_ADDTASKDATA avatarId: %s", Log.formatUUID(0, &avatarUUID));
		//Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_ADDTASKDATA taskId: %s", Log.formatUUID(0, &taskData.taskId));

		for (tauIter = taskData.tau.begin(); tauIter != taskData.tau.end(); tauIter++) {
//			Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_ADDTASKDATA tauIter: %f", tauIter->second);
		}

		for (motIter = taskData.motivation.begin(); motIter != taskData.motivation.end(); motIter++) {
//			Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_ADDTASKDATA motIter: %f", motIter->second);
		}

		for (impIter = taskData.impatience.begin(); impIter != taskData.impatience.end(); impIter++) {
//			Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_ADDTASKDATA impIter: %f", impIter->second);
		}

		for (attIter = taskData.attempts.begin(); attIter != taskData.attempts.end(); attIter++) {
//			Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_ADDTASKDATA attIter: %d", attIter->second);
		}
//		Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_ADDTASKDATA psi: %d", taskData.psi);
//		Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_ADDTASKDATA tauStdDev: %f", taskData.tauStdDev);


		lds.unlock();
		this->dStore->AddTaskData(&avatarUUID, &taskData);

		this->_ddbNotifyWatchers(this->getUUID(), DDB_TASKDATA, DDBE_ADD, &avatarUUID);
		this->globalStateChangeForward(message, data, len); // forward to mirrors and sponsees
	}
		break;
	case OAC_DDB_REMTASKDATA:			//TODO: Investigate - ddbClearWatchers would clear avatarUUID from ALL lists? Might have to go with separate keys for taskdata
	{
		UUID sender;
		UUID avatarUUID;
		lds.setData(data, len);
		lds.unpackUUID(&sender);
		lds.unpackUUID(&avatarUUID);
		lds.unlock();
		this->dStore->RemoveTaskData(&avatarUUID);
		this->_ddbNotifyWatchers(this->getUUID(), DDB_TASKDATA, DDBE_REM, &avatarUUID);
		this->_ddbClearWatchers(&avatarUUID);
		this->globalStateChangeForward(message, data, len); // forward to mirrors and sponsees
	}
		break;
	case OAC_DDB_TASKDATASETINFO:
	{
		
		UUID sender;
		UUID avatarUUID;
		DDBTaskData taskData;
		bool idNotFound;

		lds.setData(data, len);
		lds.unpackUUID(&sender);
		lds.unpackUUID(&avatarUUID);
		lds.unpackTaskData(&taskData);
		idNotFound = this->dStore->TaskDataSetInfo(&avatarUUID, &taskData);
		lds.unlock();
		if (!idNotFound) {
			this->_ddbNotifyWatchers(this->getUUID(), DDB_TASKDATA, DDBE_UPDATE, &avatarUUID);
		}
		this->globalStateChangeForward(message, data, len); // forward to mirrors and sponsees	
	}
		break;
	case OAC_DDB_TL_ROUND_INFO:
		{
		Log.log(0, "YEAH!");
		UUID sender;
		RoundInfoStruct newRoundInfo;

		lds.setData(data, len);
		lds.unpackUUID(&sender);

		newRoundInfo.roundNumber = lds.unpackInt32();  // Current round number
		newRoundInfo.newRoundNumber = lds.unpackInt32();  // Next round number
		newRoundInfo.startTime = *(_timeb *)lds.unpackData(sizeof(_timeb)); // Next round start time
																			
		int numberOfAgents = lds.unpackInt32();
		UUID newAgentId;								// Unpack the new randomized list of agents
		for (int i = 0; i < numberOfAgents; i++) {
			lds.unpackUUID(&newAgentId);
			newRoundInfo.TLAgents.push_back(newAgentId);
			Log.log(0, "New round info: Agent %s has position %d",Log.formatUUID(0, &newAgentId), i+1);

		}

		lds.unlock();
		this->dStore->SetTLRoundInfo(&newRoundInfo);
		Log.log(0, "Sending new round info: Current round number %d, new round number %d.", newRoundInfo.roundNumber, newRoundInfo.newRoundNumber);

		this->_ddbNotifyWatchers(this->getUUID(), DDB_TL_ROUND_INFO, DDBE_UPDATE, &nilUUID);
		this->globalStateChangeForward(message, data, len); // forward to mirrors and sponsees
		}
		break;
	case OAC_DDB_ADDQLEARNINGDATA:
	{


		int qCount, expCount = 0;
		UUID sender;
		char instance;
		long long totalActions;
		long long usefulActions;
		bool onlyActions;

		int tableSize;
		std::vector<float> qTable;
		std::vector<unsigned int> expTable;


		lds.setData(data, len);
		lds.unpackUUID(&sender);
		instance = lds.unpackChar();
		onlyActions = lds.unpackBool();
		totalActions = lds.unpackInt64();
		usefulActions = lds.unpackInt64();

		if (!onlyActions) {

			Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_ADDQLEARNINGDATA");
			Log.log(LOG_LEVEL_NORMAL, "AgentHost::conProcessMessage: OAC_DDB_ADDQLEARNINGDATA: stream length is %d", lds.length());
			tableSize = lds.unpackInt32();
			Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_ADDQLEARNINGDATA:: tableSize is %d", tableSize);
		/*	qTable.resize(tableSize, 0);
			expTable.resize(tableSize, 0);*/
			qTable.clear();
			expTable.clear();

			for (int i = 0; i < tableSize; i++) {
				qTable.push_back(lds.unpackFloat32());						//Pack all values in q-table
				//if (qTable.back() > 0.0f)
				//	Log.log(LOG_LEVEL_NORMAL, "AgentHost::conProcessMessage::MSG_DDB_QLEARNINGDATA:received qVal: %f", qTable.back());
				qCount++;
			}
			for (int i = 0; i < tableSize; i++) {
				expTable.push_back(lds.unpackUInt32());						//Pack all values in exp-table
				//if (expTable.back() > 0)
				//	Log.log(LOG_LEVEL_NORMAL, "AgentHost::conProcessMessage::MSG_DDB_QLEARNINGDATA:received expVal: %d", expTable.back());
				expCount++;
			}
			Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_ADDQLEARNINGDATA:: qCount is %d, expCount is %d", qCount, expCount);
		}
		
		this->dStore->AddQLearningData(onlyActions, instance, totalActions, usefulActions, tableSize, qTable, expTable);

		lds.unlock();
	}
		break;
	case OAC_DDB_ADDADVICEDATA:
	{

		//Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_ADDADVICEDATA");

		UUID sender;
		char instance;
		float cq;
		float bq;


		lds.setData(data, len);
		lds.unpackUUID(&sender);
		instance = lds.unpackChar();
		cq = lds.unpackFloat32();
		bq = lds.unpackFloat32();

		this->dStore->AddAdviceData(instance, cq, bq);

		lds.unlock();
	}
	break;
	case OAC_DDB_ADDSIMSTEPS:
	{

		Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_ADDSIMSTEPS");

		unsigned long long totalSimSteps;


		lds.setData(data, len);
		totalSimSteps = lds.unpackUInt64();
		Log.log(0, "AgentHost::conProcessMessage: OAC_DDB_ADDSIMSTEPS: simsteps is %llu", totalSimSteps);
		this->dStore->AddSimSteps(totalSimSteps);

		lds.unlock();
	}
	break;
	default:
		return 1; // unhandled message
	}

	return 0;
}

//-----------------------------------------------------------------------------
// TESTING

int AgentHost::testAgentBidding() {
	Log.log( 0, "AgentHost::testAgentBidding: starting bid test" );
		
	this->cbbaPAStart();

	return 0;
}


//-----------------------------------------------------------------------------
// Callbacks

bool AgentHost::cbDelayedAgentSpawn( void *vpAgentType ) {
	AgentType *agentType = (AgentType *)vpAgentType;
	
	if ( *this->getUUID() == this->gmMemberList.front() ) { // leader

		Log.log( 0, "AgentHost::cbDelayedAgentSpawn: requesting agent %s-%d", Log.formatUUID(0,&agentType->uuid), agentType->instance );

		UUID thread = this->conversationInitiate( AgentHost_CBR_convRequestUniqueSpawn, REQUESTAGENTSPAWN_TIMEOUT, agentType, sizeof(AgentType) );
		if ( thread == nilUUID ) {
			return 1;
		}
		this->ds.reset();
		this->ds.packUUID( this->getUUID() );
		this->ds.packUUID( &agentType->uuid );
		this->ds.packChar( agentType->instance );
		this->ds.packFloat32( 0 ); // affinity
		this->ds.packChar( DDBAGENT_PRIORITY_CRITICAL );
		this->ds.packUUID( &thread );
		this->conProcessMessage( NULL, MSG_RAGENT_SPAWN, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

	} else { // not leader
		mapAgentInfo::iterator iA;
		for ( iA = this->agentInfo.begin(); iA != this->agentInfo.end(); iA++ ) {
			if ( iA->second.type.uuid == agentType->uuid && iA->second.type.instance == agentType->instance ) {
				break; // already got one
			}
		}

		if ( iA == this->agentInfo.end() ) {
			this->uniqueNeeded.push_back( *agentType ); // save for later
		}
	}

	return 0;
}

/*
bool AgentHost::cbSupervisorWatcher( int evt, void *vpcon ) {
	spConnection con = (spConnection)vpcon;
	bool wasEmpty = this->supervisorCon.empty();

	if ( evt == CON_EVT_STATE ) {
		list<spConnection>::iterator iter;
		if ( con->state == CON_STATE_CONNECTED ) { // add to the supervisorCon list
			// start failure detection
			this->initializeConnectionFailureDetection( con );

			for ( iter = this->supervisorCon.begin(); iter != this->supervisorCon.end(); iter++ ) {
				if ( *iter == con ) // already in the list
					break;
			}
			if ( this->supervisorCon.empty() || (iter != this->supervisorCon.end() && *iter != con) ) // add to the list
				this->supervisorCon.push_back( con );
			
			this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_RSUPERVISOR_REFRESH) ); // request a refresh from the supervisor

		} else { // remove from the supervisorCon list
			this->supervisorCon.remove( con );
			if ( wasEmpty && con->state == CON_STATE_DISCONNECTED ) { // retries must have run out, but there's still nobody connected so reset
				this->resetConnectionRetries( con );
			}
		}
	} else { // evt == CON_EVT_CLOSED or CON_EVT_REMOVED: remove from the supervisorCon list
		this->supervisorCon.remove( con );
	}

	if ( !wasEmpty && this->supervisorCon.empty() ) {
		// restart connections
		spAddressPort ap = this->supervisorAP;
		spConnection con;
		while ( ap ) { 
			if ( !this->getConnection( ap ) ) { // connection doesn't exist
				con = this->openConnection( ap, NULL, 60 );
				if ( con ) {
					this->watchConnection( con, AgentHost_CBR_cbSupervisorWatcher );
				}
			}
			ap = ap->next; 
		}
	}

	return 0;
}*/

bool AgentHost::cbWatchHostConnection( int evt, void *vpcon ) {
	spConnection con = (spConnection)vpcon;
	RPC_STATUS Status;

	if ( evt == CON_EVT_STATE ) {
		if ( con->state == CON_STATE_CONNECTED ) { 
			this->ds.reset();
			this->ds.packUUID( &STATE(AgentBase)->uuid );
			this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_HOST_INTRODUCE), this->ds.stream(), this->ds.length() ); // introduce ourselves
			this->ds.unlock();

			// start failure detection
			this->initializeConnectionFailureDetection( con );
		}
	} else if ( evt == CON_EVT_CLOSED ) {
		if ( !UuidIsNil( &con->uuid, &Status ) ) {
			mapAgentHostState::iterator iterAHS = this->hostKnown.find(con->uuid);
			if ( iterAHS != this->hostKnown.end() ) {
				if ( con == iterAHS->second->connection ) { // this was our active connection to host
					// see if we have another connection
					mapConnection::iterator iC;
					for ( iC = this->connection.begin(); iC != this->connection.end(); iC++ ) {
						if ( iC->second != con && iC->second->uuid == con->uuid ) 
							break;
					}
					if ( iC != this->connection.end() ) { // found one, use this connection from now on
						iterAHS->second->connection = iC->second;
					} else { // that was our only connection
						// let the connectionStatusChanged handler take care of it iterAHS->second->connection = NULL;
					}
				}
			}
		}
	}

	return 0;
}

/*bool AgentHost::cbWatchHostConnection( int evt, void *vpcon ) {
	spConnection con = (spConnection)vpcon;
	RPC_STATUS Status;

	if ( evt == CON_EVT_STATE ) {
		if ( con->state == CON_STATE_CONNECTED ) { // connected to a host, introduce ourselves
			this->ds.reset();
			this->ds.packUUID( &STATE(AgentBase)->uuid );
			this->sendMessageEx( con, MSGEX(AgentHost_MSGS,MSG_HOST_INTRODUCE), this->ds.stream(), this->ds.length() ); // introduce ourselves
			this->ds.unlock();
			this->groupMergeSend( con ); // merge our groups now
		}
	} else if ( evt == CON_EVT_CLOSED ) {
		if ( !UuidIsNil( &con->uuid, &Status ) ) {
			mapAgentHostState::iterator iterAHS = this->hostKnown.find(con->uuid);
			if ( iterAHS != this->hostKnown.end() ) {
				iterAHS->second->connection = NULL;
				this->addTimeout( 1000*5, AgentHost_CBR_cbDelayGlobalQuery, &con->uuid, sizeof(UUID) );
			}
			STATE(AgentHost)->hostStats->erase( con->uuid );
		}
	}

	return 0;
}
*/

bool AgentHost::cbHostFormationTimeout( void *vpCoreMember ) {
	if ( *(int *)vpCoreMember ) { // we're a core member
		if ( !this->hostGroupStartFallback() )
			Log.log( 0, "AgentHost::cbHostFormationTimeout: formation timed out, core member entering fallback mode" );
	} else { // we're a regular member
		if ( this->gmMemberList.empty() ) { // we're not in a group
			Log.log( 0, "AgentHost::cbHostFormationTimeout: formation timed out, regular member preparing to stop" );
			this->prepareStop();
		}
	}
	return 0;
}


bool AgentHost::cbHostConCleanDuplicate( void *vpuuid ) {
	UUID conid = *(UUID *)vpuuid;

	mapConnection::iterator iC = this->connection.find( conid );
	if ( iC == this->connection.end() ) { // not found, must have been closed
		return 0; // done
	} else {
		Log.log( LOG_LEVEL_VERBOSE, "AgentHost::cbHostConCleanDuplicate: connection with %s still active", Log.formatUUID( LOG_LEVEL_VERBOSE, &iC->second->uuid ) );
		this->sendMessageEx( iC->second, MSGEX(AgentHost_MSGS,MSG_CLOSE_DUPLICATE) );
		return 1; // repeat
	}
}

bool AgentHost::cbHostStatusTimeout( void *vpuuid ) {
	UUID *uuid = (UUID *)vpuuid;

	mapAgentHostState::iterator iterHS;
	iterHS = this->hostKnown.find( *uuid );
	if ( iterHS == this->hostKnown.end() ) {
		Log.log( 0, "AgentHost::cbHostStatusTimeout: unknown host %s", Log.formatUUID(0,uuid) );
		return 0;
	}

	AgentHost::State *hState = iterHS->second;

	hState->statusTimeout = nilUUID;

	Log.log( 6, "AgentHost::cbHostStatusTimeout: host %s, status %d, statusActivity %d, have connection %d", 
		Log.formatUUID(6,uuid), hState->status, hState->statusActivity, hState->connection != NULL );

	switch ( hState->status ) {
	case STATUS_ACTIVE:
		if ( hState->statusActivity ) {
			hState->statusActivity = 0;
			return 1; // connection is still alive, wait again
		}

		if ( hState->connection ) {
			this->setHostKnownStatus( hState, STATUS_QUERY );
		} else {
			this->setHostKnownStatus( hState, STATUS_GLOBAL_QUERY );
		}
		break;
	case STATUS_ALIVE:
		if ( hState->connection ) {
			this->setHostKnownStatus( hState, STATUS_QUERY );
		} else {
			this->setHostKnownStatus( hState, STATUS_GLOBAL_QUERY );
		}
		break;
	case STATUS_QUERY:
		this->setHostKnownStatus( hState, STATUS_GLOBAL_QUERY );
		break;
	case STATUS_GLOBAL_QUERY:
		this->setHostKnownStatus( hState, STATUS_DEAD );
		break;
	case STATUS_DEAD:
	case STATUS_SHUTDOWN:
		AgentHost_DeleteState( (AgentBase::State *)hState );
		this->hostKnown.erase( *uuid );
		break;
	default:
		Log.log( 0, "AgentHost::cbHostStatusTimeout: unknown status %d", hState->status );
		break;
	};

	return 0;
}

bool AgentHost::cbDelayGlobalQuery( void *vpuuid ) {
	mapAgentHostState::iterator iterAHS = this->hostKnown.find(*(UUID *)vpuuid);
	if ( iterAHS == this->hostKnown.end() ) {
		return 0; // not found
	}

	//this->setHostKnownStatus( (AgentHost::State *)iterAHS->second, STATUS_GLOBAL_QUERY );

	return 0;
}

bool AgentHost::cbWatchAgentConnection( int evt, void *vpcon ) {
	spConnection con = (spConnection)vpcon;
	RPC_STATUS Status;

	if ( evt == CON_EVT_CLOSED ) {
		if ( !UuidIsNil( &con->uuid, &Status ) ) {
			mapAgentInfo::iterator iAI = this->agentInfo.find(con->uuid);
			if ( iAI != this->agentInfo.end() )
				this->agentInfo[con->uuid].con = NULL; // remove the connection
		}
	}

	return 0;
}

bool AgentHost::cbGlobalStateTransaction( int commit, void *vpuuid ) {

	this->globalStateTransactionInProgress = false;

	if ( !commit ) { // message failed, try again
		AtomicMessage *am = &this->atomicMsgs[*(UUID *)vpuuid];
		this->globalStateTransaction( am->msg, (am->len ? (char *)this->getDynamicBuffer( am->dataRef ) : NULL), am->len, true );
	} else { // try the next message
		this->_globalStateTransactionSend();
	}

	return 0;
}

bool AgentHost::cbSpawnAgentExpired( void *vpAgentId ) {

	DataStream lds;
	UUID *agent = (UUID *)vpAgentId;
	mapAgentInfo::iterator iA;

	// find agent
	iA = this->agentInfo.find( *agent );
	if ( iA == this->agentInfo.end() ) {
		return 0; // not found?
	}

	if ( iA->second.shellStatus == DDBAGENT_STATUS_SPAWNING ) { // this was a shell
		Log.log( 0, "AgentHost::cbSpawnAgentExpired: spawn shell failed! (%s)", Log.formatUUID(0,agent) );
		iA->second.shellStatus = DDBAGENT_STATUS_ERROR;

		if ( dStore->AgentGetStatus( agent ) == DDBAGENT_STATUS_THAWING ) { // we were already thawing, reset to frozen
			this->AgentTransferAbortThaw( agent );
		} else if ( dStore->AgentGetStatus( agent ) == DDBAGENT_STATUS_RECOVERING ) { // we were already thawing, reset to frozen
			// abandon ownership
			lds.reset();
			lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
			lds.packUUID( &nilUUID ); // release ownership
			lds.packUUID( this->getUUID() );
			lds.packInt32( DDBAGENT_STATUS_WAITING_SPAWN );
			lds.packUUID( this->getUUID() );
			lds.rewind();
			this->ddbAgentSetInfo( agent, &lds );
			lds.unlock();
		}
	} else { // normal failure

		// abandon ownership
		lds.reset();
		lds.packInt32( DDBAGENTINFO_HOST | DDBAGENTINFO_STATUS );
		lds.packUUID( &nilUUID ); // release ownership
		lds.packUUID( this->getUUID() );
		lds.packInt32( DDBAGENT_STATUS_WAITING_SPAWN );
		lds.packUUID( this->getUUID() );
		lds.rewind();
		this->ddbAgentSetInfo( agent, &lds );
		lds.unlock();
	}

	// TODO trigger PA session

	return 0;
}

bool AgentHost::cbCBBABuildQueued( void *vpSessionId ) {
	int sessionId = *(int *)vpSessionId;
	
	if ( sessionId != this->paSession.id ) {
		return 0; // wrong session?
	}

	this->paSession.buildQueued = nilUUID;

	// do build
	if ( this->paSession.sessionReady )
		this->cbbaBuildBundle();

	return 0;
}

bool AgentHost::cbCBBADistributeQueued( void *vpSessionId ) {
	DataStream lds;

	int sessionId = *(int *)vpSessionId;
	
	if ( sessionId != this->paSession.id ) {
		return 0; // wrong session?
	}

	this->paSession.distributeQueued = nilUUID;

	if ( this->paSession.outbox.size() == 0 ) {
		return 0; // nothing to distribute
	}

	// prepare update message
	map<UUID, PA_Bid, UUIDless>::iterator iB;

	lds.reset();
	lds.packUUID( this->getUUID() );
	lds.packInt32( this->paSession.id );
	lds.packInt32( (int)this->paSession.outbox.size() );

	for ( iB = this->paSession.outbox.begin(); iB != this->paSession.outbox.end(); iB++ ) {
		lds.packUUID( (UUID *)&iB->first );
		lds.packUUID( &iB->second.winner );
		lds.packFloat32( iB->second.reward );
		lds.packFloat32( iB->second.support );
		lds.packInt16( iB->second.round );
	}
	this->paSession.outbox.clear();

	// send update message
	list<UUID>::iterator iH;
	for ( iH = this->paSession.group.begin(); iH != this->paSession.group.end(); iH++ )
		if ( *iH != *this->getUUID() )
			this->sendAgentMessage( &*iH, MSG_PA_BID_UPDATE, lds.stream(), lds.length() );
	this->statisticsAgentAllocation[this->paSession.id].msgsSent += (int)this->paSession.group.size() - 1; // statistics
	this->statisticsAgentAllocation[this->paSession.id].dataSent += (int)(this->paSession.group.size() - 1) * lds.length();
	lds.unlock();

	return 0;
}

bool AgentHost::cbCBBAStartQueued( void *NA ) {
	
//	this->paSessionQueued = nilUUID;

	// do session
//	this->cbbaPAStart();

	return 0;
}

bool AgentHost::cbAffinityCurBlock( void *vpAgentAffinityCBData ) {
	AgentAffinityCBData cbD = *(AgentAffinityCBData *)vpAgentAffinityCBData;
	mapAgentInfo::iterator iA;
	AgentInfo *agentInfo;
	map<UUID, unsigned int, UUIDless>::iterator iD;
	AgentAffinityBlock block;
	map<UUID, AgentAffinityBlock, UUIDless>::iterator iAB;

	// get agent info
	iA = this->agentInfo.find( cbD.agent );
	if ( iA == this->agentInfo.end() ) {
		return 0; // agent not found
	}
	
	agentInfo = &iA->second;

	// move curblock to stack
	iAB = agentInfo->curAffinityBlock.find( cbD.affinity );
	if ( iAB == agentInfo->curAffinityBlock.end() ) {
		return 0; // block not found?
	}

	block = iAB->second;
	agentInfo->curAffinityBlock.erase( iAB );

	this->_affinityUpdate( &cbD.agent, &cbD.affinity, block.size );
	
	return 0;
}

bool AgentHost::convRequestUniqueSpawn( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	if ( conv->response == NULL ) { // spawn timed out
		Log.log( 0, "AgentHost::convRequestUniqueSpawn: request spawn timed out" );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	if ( this->ds.unpackBool() ) { // succeeded
		UUID agentId;
		this->ds.unpackUUID( &agentId );
		this->ds.unlock();

		this->spawnFinished( conv->con, &agentId, SPAWN_UNIQUE );
		
	} else { // failed
		this->ds.unlock();

		// TODO try again?
	}

	return 0;
}

/*
bool AgentHost::convDDBResamplePF_Lock( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;

	UUID id, them, key;

	id = *(UUID *)conv->data;
	mapLock::iterator iL = this->PFLocks.find( id );
	if ( iL == this->PFLocks.end() ) {
		Log.log( 0, "AgentHost::convDDBResamplePF_Lock: particle filter lock not found" );
		return 0;
	}
	UUIDLock *L = &iL->second;	


	if ( conv->response == NULL ) { // timed out
		Log.log( 0, "AgentHost::convDDBResamplePF_Lock: timed out" );

		// abort lock
		this->_ddbResampleParticleFilter_AbortLock( &id, &L->key );
		
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	this->ds.unpackUUID( &them );
	this->ds.unpackUUID( &key );
	this->ds.unlock();
	
	UUIDLock_Throw( L, &them, &key );

	Log.log( LOG_LEVEL_VERBOSE, "AgentHost::convDDBResamplePF_Lock: throwing tumbler %s, %d tumblers remaining (pf %s)", Log.formatUUID( LOG_LEVEL_VERBOSE, &them ), (int)L->tumbler.size(), Log.formatUUID( LOG_LEVEL_VERBOSE, &id ) );

	if ( L->tumbler.size() == 0 /* && TODO wait for updates * ) {
		this->_ddbResampleParticleFilter_Do( &id );
		return 0; // end conversation
	}

	if ( L->tumbler.size() != 0 )
		return 1; // waiting for more responses
	else
		return 0; // end conversation
}*/

bool AgentHost::cbRetire( void *NA ) {
	this->gracefulExit();
	return 0;
}

bool AgentHost::cbPFResampleTimeout( void *vpUUID ) {
	UUID id = *(UUID *)vpUUID;

	// clear flag so we can try again
	list<UUID>::iterator iI;
	for ( iI = this->PFResampleInProgress.begin(); iI != this->PFResampleInProgress.end(); iI++ ) {
		if ( *iI == id ) {
			this->PFResampleInProgress.erase( iI );		
			break;
		}
	}

	return 0;
}

bool AgentHost::cbCBBAQueued( void *NA ) {
	Log.log( 0, "AgentHost::cbCBBAQueued: starting interval update" );

	this->cbbaQueued = nilUUID;

	this->cbbaPAStart();

	return 0;
}

bool AgentHost::cbQueueCloseConnection( void *vpConId ) {
	mapConnection::iterator iC = this->connection.find( *(UUID *)vpConId );

	if ( iC == this->connection.end() ) {
		Log.log( 0, "AgentHost::cbQueueCloseConnection: connection not found (id %s)", Log.formatUUID(0,(UUID *)vpConId) );
		return 0;
	}

	spConnection con = iC->second;

	Log.log( 0, "AgentHost::cbQueueCloseConnection: closing connection (agent %s)", Log.formatUUID(0,&con->uuid) );

	// close duplicate
	this->closeConnection( con );

	return 0;
}

bool AgentHost::cbMissionDone(void * vpConId)
{
	this->globalStateTransaction(OAC_MISSION_DONE, 0, 0);


	return false;
}

//-----------------------------------------------------------------------------
// State functions

int AgentHost_writeStub( AgentHost::State *state, DataStream * ds, bool climb ) {
	if ( climb && AgentBase_writeStub( (AgentBase::State *)state, ds ) )
		return 1;

	ds->packString( state->serverAP.address );
	ds->packInt32( atoi(state->serverAP.port) );

	return 0;
}

int AgentHost_readStub( AgentHost::State *state, DataStream * ds, unsigned int age, bool climb ) {
	if ( climb && AgentBase_readStub( (AgentBase::State *)state, ds, age ) )
		return 1;

	strcpy_s( state->serverAP.address, sizeof(state->serverAP.address), ds->unpackString() );
	sprintf_s( state->serverAP.port, sizeof(state->serverAP.port), "%d", ds->unpackInt32() );	

	return 0;
}

int AgentHost_writeState( AgentHost::State *state, DataStream * ds ) {
	if ( AgentBase_writeState( (AgentBase::State *)state, ds ) )
		return 1;

	AgentHost_writeStub( state, ds, false );

	return 0;
}

int AgentHost_readState( AgentHost::State *state, DataStream * ds, unsigned int age ) {
	if ( AgentBase_readState( (AgentBase::State *)state, ds, age ) )
		return 1;

	AgentHost_readStub( state, ds, false );

	return 0;
}

AgentBase::State * AgentHost_CreateState( UUID *uuid, int size ) {
	AgentBase::State *newState = AgentBase_CreateState( uuid, size );
	if ( !newState ) {
		return NULL;
	}
/*	
	((AgentHost::State *)newState)->hostStats = new mapConnectionStatistics();
//	((AgentHost::State *)newState)->activeAgents = new list<UUID>();	

	((AgentHost::State *)newState)->status = AgentHost::STATUS_UNSET;
	((AgentHost::State *)newState)->statusTimeout = nilUUID;
	((AgentHost::State *)newState)->statusActivity =  0;

	((AgentHost::State *)newState)->connection = NULL;
*/
	return newState;
}

void AgentHost_DeleteState( AgentBase::State *state, AgentHost *owner ) {
	
/*	mapConnectionStatistics::iterator iterCS;
	for ( iterCS = ((AgentHost::State *)state)->hostStats->begin(); iterCS != ((AgentHost::State *)state)->hostStats->end(); iterCS++ ) {
		free( iterCS->second );
	}

	delete ((AgentHost::State *)state)->hostStats;
//	delete ((AgentHost::State *)state)->activeAgents;

	if ( ((AgentHost::State *)state)->statusTimeout != nilUUID ) {
		owner->removeTimeout( &((AgentHost::State *)state)->statusTimeout );
	}

	if ( ((AgentHost::State *)state)->connection != NULL ) {
		UuidCreateNil( &((AgentHost::State *)state)->connection->uuid );
	}
*/
	AgentBase_DeleteState( state );
}

spAgentSpawnProposal NewAgentSpawnProposal( spConnection con, float favourable ) {
	spAgentSpawnProposal asp = (spAgentSpawnProposal)malloc( sizeof(sAgentSpawnProposal) );
	if ( !asp ) {
		printf( "NewAgentSpawnProposal: Failed to malloc AgentSpawnProposal" );
		return NULL;
	}

	asp->con = con;

	// TEMP
	asp->favourable = favourable;

	return asp;
}

spAgentSpawnRequest NewAgentSpawnRequest( spConnection con, UUID *ticket, AgentType *type ) {
	spAgentSpawnRequest asr = (spAgentSpawnRequest)malloc( sizeof(sAgentSpawnRequest) );
	if ( !asr ) {
		printf( "NewAgentSpawnRequest: Failed to malloc AgentSpawnRequest" );
		return NULL;
	}

	asr->con = con;
	asr->ticket = *ticket;
	asr->type.uuid = type->uuid;
	asr->type.instance = type->instance;
	asr->timeout = NULL;

	return asr;
}