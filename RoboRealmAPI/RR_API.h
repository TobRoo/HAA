#ifndef _RR_API
#define _RR_API 1

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// default read and write socket timeout
#define DEFAULT_TIMEOUT 5000

// the port number to listen on ... needs to match that used in RR interface
#define SERVER_PORTNUM 6060

class RR_API
{
	// windows socket specific data structures
	WSADATA winsock_data;
	WORD version_required; /* Version 1.1 */
  int handle;

	// indicates that the application is connected to RoboRealm Server
	bool connected;
	// holds the previously read data size
	int lastDataTop;
	// holds the previously read data buffer
	int lastDataSize;
	// contains the read/write socket timeouts
	int timeout;
	// general buffer for data manipulation and socket reading
	char buffer[4096];

	// process information if we have a local instance
	PROCESS_INFORMATION piProcInfo;
	bool piProcInfoValid;

	char errorMsg[64];
	int readMessage(int hSocket, unsigned char *buffer, int len);
	int readImageData(int hSocket, unsigned char *buffer, int len, unsigned char *lastBuffer = NULL);
	void unescape(char *txt);
	void escape(char *txt, char *dest, int max);

public:

	~RR_API();
	RR_API();
	bool connect(char *hostname, int port = SERVER_PORTNUM );
	bool getDimension(int *width, int *height);
	bool getImage(char *name, unsigned char *pixels, int *width, int *height, unsigned int len);
	bool getImage(unsigned char *pixels, int *width, int *height, unsigned int len);
	void disconnect();
	bool getVariable(char *name, char *buffer, int max);
	bool setVariable(char *name, char *value);
	bool deleteVariable(char *name);
	bool setImage(unsigned char *image, int width, int height, bool wait=false);
	bool setImage(char *name, unsigned char *image, int width, int height, bool wait=false);
	bool setCompressedImage(unsigned char *data, int size, bool wait=false);
	bool setCompressedImage(char *name, unsigned char *data, int size, bool wait=false);
	bool execute(char *source);
	bool loadProgram(char *filename);
	bool loadImage(char *name, char *filename);
	bool saveImage(char *source, char *filename);
	bool setCamera(char *mode);
	int read(int hSocket, unsigned char *buffer, int len);
	bool run(char *mode);
	bool waitVariable(char *name, char *value, int timeout);
	bool waitImage();
	int close();
	int open( WCHAR *filename, char *args = "", int port = SERVER_PORTNUM );
	unsigned char *readLine(FILE *fp, unsigned char *buffer);
	int savePPM(char *filename, unsigned char *buffer, int width, int height);
	int loadPPM(char *filename, unsigned char *buffer, int *width, int *height, int max);
	int getVariables(char *names, char *results[], int len, int rows);
	bool setVariables(char *names[], char *values[], int num);
	void skipData(int hSocket, int len);
	bool getParameter(char *module, int count, char *name, char *result, int max);
	bool setParameter(char *module, int count, char *name, char *value);

	char *getLastError() { return this->errorMsg; };
};

#endif