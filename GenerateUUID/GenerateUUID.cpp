// GenerateUUID.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <rpc.h>
#include <windows.h>

int _tmain(int argc, _TCHAR* argv[])
{
	UUID uuid;
	
	static RPC_WSTR rpc_wstr;

	UuidCreate( &uuid );
	UuidToString( &uuid, &rpc_wstr );

	printf( "%ls\n", rpc_wstr );

	
	int ok = OpenClipboard(NULL);
   
	if (ok) {
		HGLOBAL clipbuffer;
		char * buffer;
		EmptyClipboard();
		clipbuffer = GlobalAlloc(GMEM_DDESHARE, wcslen((const wchar_t *)rpc_wstr)+1);
		buffer = (char*)GlobalLock(clipbuffer);
		sprintf( buffer, "%ls", rpc_wstr ); //strcpy(buffer, source);
		GlobalUnlock(clipbuffer);
		SetClipboardData(CF_TEXT,clipbuffer);
		CloseClipboard();

		printf( "UUID copied to clipboard..\n" );
	} else {
		printf( "Copy to clipboard failed!\n" );
	}

	RpcStringFree( &rpc_wstr );

	printf( "Press Enter to exit" );
	getchar();

	return 0;
}

