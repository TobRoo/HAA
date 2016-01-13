// reloader.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "windows.h"
#include "io.h"


int _tmain(int argc, _TCHAR* argv[])
{
	printf( "Sleeping for 5 seconds...\n" );
	Sleep( 5000 );

	if ( _access( "Autonomic.new", 00 ) == -1 ) { // no new file!
		printf( "No Autonomic.new found, reloading...\n" );
	} else {
		printf( "Backing up old host...\n" );
		if ( _access( "Autonomic.bak", 00 ) != -1 ) { // backup already exists
			if ( remove( "Autonomic.bak" ) == -1 ) {
				printf( "Could not delete old backup... exiting\n");
				return 1;
			}
		}
		if ( rename( "Autonomic.exe", "Autonomic.bak" ) != 0 ) {
			printf( "Could not rename Autonomic.exe... exiting\n");
			return 1;
		}

		printf( "Applying update...\n" );
		if ( rename( "Autonomic.new", "Autonomic.exe" ) != 0 ) {
			printf( "Could not rename Autonomic.new... restoring backup\n");
			if ( rename( "Autonomic.bak", "Autonomic.exe" ) != 0 ) {
				printf( "Could not rename Autonomic.bak... exiting\n");
				return 1;
			}
		}
	}

	printf( "Launching Autonomic.exe\n" );
	// Launch Autonomic.exe
	
	Sleep( 5000 );

	// Check if launch was ok.
	if ( 0 ) {
		printf( "Everything seems to be working... exiting\n" );
	} else {
		printf( "Launch failed!  Restoring backup...\n" );
		if ( _access( "Autonomic.bak", 00 ) != -1 ) {
			if ( remove( "Autonomic.exe" ) == -1 ) {
				printf( "Could not delete new Autonomic.exe... exiting\n");
				return 1;
			}
			if ( rename( "Autonomic.bak", "Autonomic.exe" ) != 0 ) {
				printf( "Could not rename Autonomic.bak... exiting\n");
				return 1;
			}
		} else {
			printf( "No backup exists!  Exiting\n" );
			return 1;
		}
		printf( "Relaunching Autonomic.exe\n" );
		// Launch Autonomic.exe
	}

	return 0;
}



