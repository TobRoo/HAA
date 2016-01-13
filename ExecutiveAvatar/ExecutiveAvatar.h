// ExecutiveAvatar.h : main header file for the ExecutiveAvatar DLL
//

#pragma once

class ExecutiveAvatar : public AgentBase {
public:
	ExecutiveAvatar( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );
	~ExecutiveAvatar();

	virtual int configure();	// initial configuration
	virtual int start( char *missionFile );		// start agent
	virtual int stop();			// stop agent
	virtual int step();			// run one step

private:

public:
	DECLARE_CALLBACK_CLASS( ExecutiveAvatar )

};

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile );


// CExecutiveAvatarDLL: Override ExitInstance for debugging memory leaks
class CExecutiveAvatarDLL : public CWinApp {
public:
	CExecutiveAvatarDLL();
	virtual BOOL ExitInstance();
	DECLARE_MESSAGE_MAP()
};