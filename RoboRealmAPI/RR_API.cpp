
#include "stdafx.h"

#include <winsock2.h>
#include <windows.h>
#include <tchar.h>

#define _CRT_SECURE_NO_WARNINGS  1

#undef UNICODE
#include "RR_API.h"

#pragma comment(lib, "ws2_32.lib")


RR_API::RR_API()
{
	timeout=DEFAULT_TIMEOUT;
	lastDataTop=0;
	lastDataSize=0;
	connected=false;
	piProcInfoValid = false;
}

RR_API::~RR_API()
{
	if (connected)
    closesocket(handle);
}

/******************************************************************************/
/* Text string manipulation routines */
/******************************************************************************/

/* 
Escapes strings to be included in XML message. This can be accomplished by a 
sequence of replace statements. 
	& -> &amp;
	" -> &quote;
	< -> &lt;
	> -> &gt;
*/
void RR_API::escape(char *txt, char *dest, int max)
{
	int i, j;

	for (j=i=0;txt[i]&&(j<max);i++)
	{
		if (txt[i]=='&')
		{
			if (j+5<max)
			{
				strcpy(&dest[j], "&amp;");
				j+=5;
			}
		}
		else
		if (txt[i]=='"')
		{
			if (j+5<max)
			{
				strcpy(&dest[j], "&quote;");
				j+=5;
			}
		}
		else
		if (txt[i]=='<')
		{
			if (j+4<max)
			{
				strcpy(&dest[j], "&lt;");
				j+=4;
			}
		}
		else
		if (txt[i]=='>')
		{
			if (j+4<max)
			{
				strcpy(&dest[j], "&gt;");
				j+=4;
			}
		}
		else
		{
			dest[j++]=txt[i];
		}
	}

	dest[j]=0;
}

/* 
Unescapes strings that have been included in an XML message. This can be 
accomplished by a sequence of replace statements. 
	&amp; -> &
	&quote; -> "
	&lt; -> <
	&gt; -> >
*/
void RR_API::unescape(char *txt)
{
	int i, j;

	for (j=i=0;txt[i];i++)
	{
		if (txt[i]=='&')
		{
			if (_strnicmp(&txt[i], "&amp;", 5)==0)
			{
				txt[j++]='&';
				i+=4;
			}
			else
			if (_strnicmp(&txt[i], "&quote;", 7)==0)
			{
				txt[j++]='"';
				i+=6;
			}
			else
			if (_strnicmp(&txt[i], "&lt;", 4)==0)
			{
				txt[j++]='<';
				i+=3;
			}
			else
			if (_strnicmp(&txt[i], "&gt;", 4)==0)
			{
				txt[j++]='>';
				i+=3;
			}
		}
		else
		{
			txt[j++]=txt[i];
		}
	}

	txt[j]=0;
}

/******************************************************************************/
/* Socket Routines */
/******************************************************************************/

/* Initiates a socket connection to the RoboRealm server */
bool RR_API::connect(char *hostname, int port )
{
	connected=false;

  version_required = 0x0101; /* Version 1.1 */
  WSAStartup (version_required, &winsock_data);

  int sockaddr_in_length = sizeof(struct sockaddr_in);
  int fromlen = sizeof(struct sockaddr_in);

  if ((handle = (int)socket(AF_INET, SOCK_STREAM, 0))<0)
  {
	  strcpy(errorMsg, "Could not create socket!");
		return false;
  }

  int enable=1;

  if ((setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, (char *)&enable, sizeof(enable)))<0)
  {
		strcpy(errorMsg, "Could not set socket option SO_REUSEADDR!");
    return false;
  }

  if ((setsockopt(handle, SOL_SOCKET, SO_KEEPALIVE, (char *)&enable, sizeof(enable)))<0)
  {
	  strcpy(errorMsg, "Could not set socket option SO_KEEPALIVE!");
    return false;
  }

  if ((setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, (char *)&enable, sizeof(enable)))<0)
  {
	  strcpy(errorMsg, "Could not set socket option TCP_NODELAY!");
    return false;
  }

  struct linger ling;
  ling.l_onoff=1;
  ling.l_linger=10;
  if ((setsockopt(handle, SOL_SOCKET, SO_LINGER, (char *)&ling, sizeof(linger)))<0)
  {
	  strcpy(errorMsg, "Could not set socket option SO_LINGER!");
    return false;
  }

  sockaddr_in sockaddr;
  sockaddr.sin_family = AF_INET;
  sockaddr.sin_port = htons(port);

  struct hostent *remote_host;        /* internet numbers, names */
  if ((remote_host=gethostbyname(hostname))==(struct hostent *)NULL)
  {
	  _snprintf(errorMsg, 64, "Could not lookup hostname '%s'!", hostname);
    return false;
  }

  memcpy((char *)&sockaddr.sin_addr,(char *)remote_host->h_addr, remote_host->h_length);

  if (::connect(handle,(struct sockaddr *)&sockaddr, sizeof(sockaddr))<0)
  {
	  strcpy(errorMsg, "Could not connect to RoboRealm handle!");
    return false;
  }

	connected=true;

	return true;
}

/* close the socket handle */
void RR_API::disconnect()
{
	if (connected)
    closesocket(handle);
}

/* Timed read from a socket */
int RR_API::read(int hSocket, unsigned char *buffer, int len)
{
	struct timeval	tim;
	fd_set fds;

	do
	{
		FD_ZERO(&fds);
		FD_SET(hSocket, &fds);

		if ( timeout != DEFAULT_TIMEOUT )
			timeout = timeout;

		tim.tv_sec = (timeout/1000);
		tim.tv_usec = (timeout%1000)*10;

		if (select(1024, &fds, NULL, NULL, &tim)<=0)
		{
			return(-2);
		}
	}
	while (!(FD_ISSET(hSocket, &fds)));

  return recv(hSocket, (char *)buffer, len, NULL);
}

/* 
Buffered socket image read. Since we don't know how much data was read from a
previous socket operation we have to add in any previously read information
that may still be in our buffer. We detect the end of XML messages by the
</response> tag but this may require reading in part of the image data that
follows a message. Thus when reading the image data we have to move previously
read data to the front of the buffer and continuing reading in the
complete image size from that point.
*/ 
int RR_API::readImageData(int hSocket, unsigned char *buffer, int len, unsigned char *lastBuffer)
{
  int num;

  if ( lastBuffer == NULL )
	  lastBuffer = buffer;

	// check if we have any information left from the previous read
	num = lastDataSize-lastDataTop;
	if (num>len)
	{
		memcpy(buffer, &lastBuffer[lastDataTop], len);
		lastDataTop+=num;
		return num;
	}
	memcpy(buffer, &lastBuffer[lastDataTop], num);
	len-=num;
	lastDataSize=lastDataTop=0;

	// then keep reading until we're read in the entire image length
  do
  {
    int res = read(hSocket, (unsigned char *)&buffer[num], len);
		if (res<0) 
		{
			lastDataSize=lastDataTop=0;
			return -1;
		}
    num+=res;
    len-=res;
  }
  while (len>0);

  return num;
}

/*
Skips the specified length of data in case the incoming image is too big for the current
buffer. If we don't read in the image entirely then the next API statements will
not work since a large binary buffer will be before any response can be read
*/
void RR_API::skipData(int hSocket, int len)
{
	int num = lastDataSize-lastDataTop;
	lastDataSize=lastDataTop=0;
	len-=num;
	char skipBuffer[1024];
	do
	{
		int res = read(hSocket, (unsigned char *)skipBuffer, len>1024?1024:len);
		len-=res;
	}
	while (len>0);
}

/* Read's in an XML message from the RoboRealm Server. The message is always
delimited by a </response> tag. We need to keep reading in information until
this tag is seen. Sometimes this will accidentally read more than needed
into the buffer such as when the message is followed by image data. We
need to keep this information for the next readImage call.*/

int RR_API::readMessage(int hSocket, unsigned char *buffer, int len)
{
  int num=0;
	char *delimiter = "</response>";
	int top=0;
	int i;

	// read in blocks of data looking for the </response> delimiter
  do
  {
    int res = read(hSocket, (unsigned char *)&buffer[num], len);
		if (res<0) 
		{
			lastDataSize=lastDataTop=0;
			return -1;
		}
		lastDataSize=num+res;
    for (i=num;i<num+res;i++)
		{
			if (buffer[i]==delimiter[top])
			{
				top++;
				if (delimiter[top]==0)
				{
					num=i+1;
					buffer[num]=0;
			    lastDataTop=num;
					return num;
				}
			}
		}
		num+=res;
    len-=res;
  }
  while (len>0);
	
	lastDataTop=num;
	buffer[num]=0;
  return num;
}

/******************************************************************************/
/* API Routines */
/******************************************************************************/

/* Returns the current image dimension */
bool RR_API::getDimension(int *width, int *height)
{
	if (!connected) return false;

	char *cmd = "<request><get_dimension/></request>";

  send(handle, cmd, (int)strlen(cmd), NULL);

  // read in variable length
  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		sscanf(buffer, "<response><width>%d</width><height>%d</height></response>", width, height);
		return true;
	}

	return false;
}

/* 
Returns the current processed image. 
	pixels  - output - contains RGB 8 bit byte. 
	width - output - contains grabbed image width
	height - output - contains image height
	len - input - maximum size of pixels to read 
*/
bool RR_API::getImage(unsigned char *pixels, int *width, int *height, unsigned int len)
{
	return getImage(NULL, pixels, width, height, len);
}

/* 
Returns the named image.
	name - input - name of image to grab. Can be source, processed, or marker name. 
	pixels  - output - contains RGB 8 bit byte. 
	width - output - contains grabbed image width
	height - output - contains image height
	len - input - maximum size of pixels to read 
*/
bool RR_API::getImage(char *name, unsigned char *pixels, int *width, int *height, unsigned int max)
{
	unsigned int len;
	if (!connected) return false;
	if (name==NULL) name="";

	char ename[64];
	// escape the name for use in an XML stream
	escape(name, ename, 64);

	// create the message request
	_snprintf(buffer, 128, "<request><get_image>%s</get_image></request>", ename);
  send(handle, buffer, (int)strlen(buffer), NULL);

  // read in response which contains image information
  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		// parse image width and height
		sscanf(buffer, "<response><length>%d</length><width>%d</width><height>%d</height></response>", &len, width, height);
		if (len>max)
		{
	    skipData(handle, len);
			return false;
		}
		// actual image data follows the message
		if (readImageData(handle, (unsigned char *)pixels, len, (unsigned char *)buffer)!=len)
			return false;
		else
		{
			return true;
		}
	}

	return false;
}

/* 
Sets the current source image. 
	pixels  - input - contains RGB 8 bit byte. 
	width - input - contains grabbed image width
	height - input - contains image height
*/
bool RR_API::setImage(unsigned char *pixels, int width, int height, bool wait)
{
	return setImage(NULL, pixels, width, height, wait);
}

/* 
Sets the current source image. 
	name - input - the name of the image to set. Can be source or marker name
	pixels  - input - contains RGB 8 bit byte. 
	width - input - contains grabbed image width
	height - input - contains image height
*/
bool RR_API::setImage(char *name, unsigned char *pixels, int width, int height, bool wait)
{
	if (!connected) return false;
	if (name==NULL) name="";

	char ename[64];
	// escape the name for use in an XML string
	escape(name, ename, 64);

	// setup the message request
	_snprintf(buffer, 512, "<request><set_image><source>%s</source><width>%d</width><height>%d</height><wait>%s</wait></set_image></request>", ename, width, height, wait?"1":"");
	send(handle, buffer, (int)strlen(buffer), NULL);

  // send the RGB triplet pixels after message
	send(handle, (char *)pixels, width*height*3, NULL);

  // read message response 
	if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		if (_stricmp(buffer, "<response>ok</response>")!=0)
			return false;
		else
			return true;
	}

	return false;
}

/* 
Sets the current source image. 
	data  - input - contains compressed image data. 
	size - input - length of compressed data
*/
bool RR_API::setCompressedImage( unsigned char *data, int size, bool wait) {
	return setCompressedImage( NULL, data, size, wait );
}

/* 
Sets the current source image. 
	name - input - the name of the image to set. Can be source or marker name
	data  - input - contains compressed image data. 
	size - input - length of compressed data
*/
bool RR_API::setCompressedImage(char *name, unsigned char *data, int size, bool wait)
{
	if (!connected) return false;
	if (name==NULL) name="";

	char ename[64];
	// escape the name for use in an XML string
	escape(name, ename, 64);

	// setup the message request
	_snprintf(buffer, 512, "<request><set_image><compressed></compressed><source>%s</source><size>%d</size><width>320</width><height>256</height><wait>%s</wait></set_image></request>", ename, size, wait?"1":"");
	send(handle, buffer, (int)strlen(buffer), NULL);

  // send the compressed data
	send(handle, (char *)data, size, NULL);

  // read message response 
	if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		if (_stricmp(buffer, "<response>ok</response>")!=0)
			return false;
		else
			return true;
	}

	return false;
}

/*
Returns the value of the specified variable.
	name - input - the name of the variable to query
	result - output - contains the current value of the variable
	max - input - the maximum size of what the result can hold
*/
bool RR_API::getVariable(char *name, char *result, int max)
{
	if (!connected) return false;
	if ((name==NULL)||(name[0]==0)) return false;

	char ename[64];
	// escape the name for use in an XML string
	escape(name, ename, 64);

	result[0]=0;

	_snprintf(buffer, 128, "<request><get_variable>%s</get_variable></request>", ename);
  send(handle, buffer, (int)strlen(buffer), NULL);

  // read in variable length
  if (readMessage(handle, (unsigned char *)buffer, max)>0)
	{
		sscanf(buffer, "<response><%*[^>]>%[^<]</%*[^>]></response>", result);
		unescape(result);
		return true;
	}

	return false;
}

/*
Returns the value of the specified variables.
	name - input - the names of the variable to query
	result - output - contains the current values of the variables
	max - input - the maximum size of what the result can hold
*/
int RR_API::getVariables(char *names, char *results[], int len, int rows)
{
	int i, j;
	int readBytes, readTotal;
	if (!connected) return 0;
	if ((names==NULL)||(names[0]==0)) return 0;

	char ename[100];
	// escape the name for use in an XML string
	escape(names, ename, 100);

	results[0][0]=0;

	_snprintf(buffer, 128, "<request><get_variables>%s</get_variables></request>", ename);
  send(handle, buffer, (int)strlen(buffer), NULL);

  i = 0;
  j = 0;
  readTotal = 0;
  while ( i+j<len && (readBytes = readMessage(handle, (unsigned char *)buffer+i+j, len-i-j)) > 0 )
	{
		j = 11;
		readTotal += readBytes;
		while ( i<readTotal-11 && _strnicmp(buffer+i, "</response>", 11) != 0 ) // find the end of the response
			i++;
		if ( i == readTotal-11 )
			break;
		continue;
  }

  if ( readTotal && _strnicmp(buffer, "<response>", 10) == 0 ) {
		
		i = 10;
		j = 0;

		while (j<rows)
		{
			// read in start tag
			if (buffer[i]!='<') 
				return 0;
			while (buffer[i]&&(buffer[i]!='>')) i++;
			if (buffer[i]!='>') 
				return 0;
			// read in variable value
			int p=0;
			i++;
			while (buffer[i]&&(buffer[i]!='<')) 
				results[j][p++]=buffer[i++];
			// read in end tag
			if (buffer[i]!='<') 
				return 0;
			while (buffer[i]&&(buffer[i]!='>')) i++;
			if (buffer[i]!='>') 
				return 0;
			i++;
			// unescape the resulting value
			results[j][p]=0;
			unescape(results[j]);
			// continue to next variable
			j++;
			
			// last part of text should be the end response tag
			if (_strnicmp(&buffer[i], "</response>", 11)==0) break;
		}

		return j;
	}

	return 0;
}

/*
Sets the value of the specified variable.
	name - input - the name of the variable to set
	value - input - contains the current value of the variable to be set
*/
bool RR_API::setVariable(char *name, char *value)
{
	if (!connected) return false;
	if ((name==NULL)||(name[0]==0)) return false;

	// create request message
	strcpy(buffer, "<request><set_variable><name>");
	escape(name, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</name><value>");
	escape(value, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</value></set_variable></request>");
  
	// send that message to RR Server
	send(handle, buffer, (int)strlen(buffer), NULL);

  // read in confirmation
  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		if (_stricmp(buffer, "<response>ok</response>")!=0)
			return false;
		else
			return true;
	}

	return false;
}

/*
Sets the value of the specified variables.
	names - input - the name of the variable to set
	values - input - contains the current value of the variable to be set
*/
bool RR_API::setVariables(char *names[], char *values[], int num)
{
	if (!connected) return false;
	if ((names==NULL)||(values==NULL)||(names[0][0]==0)) return false;

	int j=0;
	int i;

	// create request message
	strcpy(buffer, "<request><set_variables>");
	j=(int)strlen(buffer);
	for (i=0;(i<num);i++)
	{
		if ((j+17)>=4096) return false;
		strcpy(&buffer[j], "<variable><name>");
		j+=(int)strlen(&buffer[j]);
		escape(names[i], &buffer[j], 4096-j);
		j+=(int)strlen(&buffer[j]);
		if ((j+16)>=4096) return false;
		strcpy(&buffer[j], "</name><value>");
		j+=(int)strlen(&buffer[j]);
		escape(values[i], &buffer[j], 4096-j);
		j+=(int)strlen(&buffer[j]);
		if ((j+20)>=4096) return false;
		strcpy(&buffer[j], "</value></variable>");
		j+=(int)strlen(&buffer[j]);
  }
	if ((j+25)>=4096) return false;
	strcpy(&buffer[j], "</set_variables></request>");
	j+=(int)strlen(&buffer[j]);

	// send that message to RR Server
	send(handle, buffer, j, NULL);

  // read in confirmation
  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		if (_stricmp(buffer, "<response>ok</response>")!=0)
			return false;
		else
			return true;
	}

	return false;
}

/*
Deletes the specified variable
	name - input - the name of the variable to delete
*/
bool RR_API::deleteVariable(char *name)
{
	if (!connected) return false;
	if ((name==NULL)||(name[0]==0)) return false;

	char ename[64];
	// escape the name for use in an XML string
	escape(name, ename, 64);

	_snprintf(buffer, 128, "<request><delete_variable>%s</delete_variable></request>", ename);

  send(handle, buffer, (int)strlen(buffer), NULL);

  // read in variable length
  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		if (_stricmp(buffer, "<response>ok</response>")!=0)
			return false;
		else
			return true;
	}

	return false;
}

/*
Executes the provided image processing pipeline
	source - the XML .robo file string
*/
bool RR_API::execute(char *source)
{
	if (!connected) return false;
	if ((source==NULL)||(source[0]==0)) return false;

	// create the request message
	strcpy(buffer, "<request><execute>");
	escape(source, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</execute></request>");

	//send the string
  send(handle, buffer, (int)strlen(buffer), NULL);

  // read in result
  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		if (_stricmp(buffer, "<response>ok</response>")!=0)
			return false;
		else
			return true;
	}

	return false;
}

/*
Executes the provided .robo file. Note that the file needs to be on the machine
running RoboRealm. This is similar to pressing the 'open program' button in the 
main RoboRealm dialog.
	filename - the XML .robo file to run
*/
bool RR_API::loadProgram(char *filename)
{
	if (!connected) return false;
	if ((filename==NULL)||(filename[0]==0)) return false;

	strcpy(buffer, "<request><load_program>");
	escape(filename, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</load_program></request>");

  send(handle, buffer, (int)strlen(buffer), NULL);

  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		if (_stricmp(buffer, "<response>ok</response>")!=0)
			return false;
		else
			return true;
	}

	return false;
}

/*
Loads an image into RoboRealm. Note that the image needs to exist
on the machine running RoboRealm. The image format must be one that
RoboRealm using the freeimage.dll component supports. This includes
gif, pgm, ppm, jpg, png, bmp, and tiff. This is 
similar to pressing the 'load image' button in the main RoboRealm
dialog.
	name - name of the image. Can be "source" or a marker name,
	filename - the filename of the image to load
*/
bool RR_API::loadImage(char *name, char *filename)
{
	if (!connected) return false;

	if ((filename==NULL)||(filename[0]==0)) return false;
	if ((name==NULL)||(name[0]==0)) name="source";

	strcpy(buffer, "<request><load_image><filename>");
	escape(filename, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</filename><name>");
	escape(name, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</name></load_image></request>");

  send(handle, buffer, (int)strlen(buffer), NULL);

  // read in variable length
  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		if (_stricmp(buffer, "<response>ok</response>")!=0)
			return false;
		else
			return true;
	}

	return false;
}

/*
Saves the specified image in RoboRealm to disk. Note that the filename is relative
to the machine that is running RoboRealm. The image format must be one that
RoboRealm using the freeimage.dll component supports. This includes
gif, pgm, ppm, jpg, png, bmp, and tiff. This is 
similar to pressing the 'save image' button in the main RoboRealm
dialog.
	name - name of the image. Can be "source","processed", or a marker name,
	filename - the filename of the image to save
*/
bool RR_API::saveImage(char *source, char *filename)
{
	if (!connected) return false;

	if ((filename==NULL)||(filename[0]==0)) return false;
	if ((source==NULL)||(source[0]==0)) source="processed";

	// create the save image message
	strcpy(buffer, "<request><save_image><filename>");
	escape(filename, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</filename><source>");
	escape(source, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</source></save_image></request>");

  // send it on its way
	send(handle, buffer, (int)strlen(buffer), NULL);

  // read in any result
  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		if (_stricmp(buffer, "<response>ok</response>")!=0)
			return false;
		else
			return true;
	}

	return false;
}

/*
Sets the current camera driver. This can be used to change the current viewing camera 
to another camera installed on the same machine. Note that this is a small delay
when switching between cameras. The specified name needs only to partially match
the camera driver name seen in the dropdown picklist in the RoboRealm options dialog.
For example, specifying "Logitech" will select any installed Logitech camera including
"Logitech QuickCam PTZ".
*/
bool RR_API::setCamera(char *name)
{
	if (!connected) return false;
	if ((name==NULL)||(name[0]==0)) return false;

	strcpy(buffer, "<request><set_camera>");
	escape(name, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</set_camera></request>");

  int res = send(handle, buffer, (int)strlen(buffer), NULL);

  // read in variable length
  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		if (_stricmp(buffer, "<response>ok</response>")!=0)
			return false;
		else
			return true;
	}

	return false;
}

/*
This routine provides a way to stop processing incoming video. Some image processing
tasks can be very CPU intensive and you may only want to enable processing when
required but otherwise not process any incoming images to release the CPU for other
tasks. The run mode can also be used to processing individual frames or only run
the image processing pipeline for a short period. This is similar to pressing the
"run" button in the main RoboRealm dialog.
	mode - can be toggle, on, off, once, or a number of frames to process
	*/
bool RR_API::run(char *mode)
{
	if (!connected) return false;
	if ((mode==NULL)||(mode[0]==0)) return false;

	strcpy(buffer, "<request><run>");
	escape(mode, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</run></request>");

  send(handle, buffer, (int)strlen(buffer), NULL);

  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		if (_stricmp(buffer, "<response>ok</response>")!=0)
			return false;
		else
			return true;
	}

	return false;
}

/* 
There is often a need to pause your own Robot Controller program to wait for
RoboRealm to complete its task. The eaisest way to accomplish this is to wait
on a specific variable that is set to a specific value by RoboRealm. Using the
waitVariable routine you can pause processing and then continue when a variable
changes within RoboRealm.
	name - name of the variable to wait for
	value - the value of that variable which will cancel the wait
	timeout - the maximum time to wait for the variable value to be set
*/
bool RR_API::waitVariable(char *name, char *value, int timeout)
{
	if (!connected) return false;
	if ((name==NULL)||(name[0]==0)) return false;

	strcpy(buffer, "<request><wait_variable><name>");
	escape(name, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</name><value>");
	escape(value, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</value><timeout>");
	_itoa(timeout, &buffer[(int)strlen(buffer)], 10);
	strcat(buffer, "</timeout></wait_variable></request>");

	this->timeout=timeout;
	if (timeout==0) timeout=100000000;

  send(handle, buffer, (int)strlen(buffer), NULL);

  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		this->timeout=DEFAULT_TIMEOUT;
		if (_stricmp(buffer, "<response>ok</response>")!=0)
			return false;
		else
			return true;
	}

	this->timeout=DEFAULT_TIMEOUT;
	return false;
}

/*
If you are rapdily grabbing images you will need to wait inbetween each
get_image for a new image to be grabbed from the video camera. The wait_image
request ensures that a new image is available to grab. Without this routine
you may be grabbing the same image more than once.
*/
bool RR_API::waitImage()
{
	if (!connected) return false;

	strcpy(buffer, "<request><wait_image></wait_image></request>");

  send(handle, buffer, (int)strlen(buffer), NULL);

  // read in variable length
  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		if (_stricmp(buffer, "<response>ok</response>")!=0)
			return false;
		else
			return true;
	}

	return false;
}

BOOL CALLBACK minimizeByProcessId( HWND hwnd, LPARAM lParam ) {
	DWORD pid;
	GetWindowThreadProcessId( hwnd, &pid );

	if ( pid == *((DWORD*)lParam) ) {
		if ( !(GetWindowLong(hwnd,GWL_STYLE) & WS_VISIBLE) )
			return TRUE;

		// found the right window
		ShowWindow( hwnd, SW_MINIMIZE );

		return FALSE;
	}

	return TRUE;
}

/* If you are running RoboRealm on the same machine as your API program you can use
this routine to start RoboRealm if it is not already running.
	filename - the path to RoboRealm on your machine
*/
int RR_API::open( WCHAR *filename, char *args, int port )
{
	HANDLE serverReady = CreateEvent(
		NULL,					//use default security attributes
		TRUE,                  //event will be auto reset
		FALSE,                  //initial state is non-signalled
		_T("RoboRealm_Server_Event"));

/* we should never have an instance running on our port
	int res = WaitForSingleObject(serverReady, 100);
	if (res==WAIT_OBJECT_0) { // instance is already running, check if it's the right port
		if ( connect( "localhost", port ) ) {
			disconnect();
			return 2; // assuming control
		}
	}*/

	STARTUPINFO siStartInfo;

  ZeroMemory( &piProcInfo, sizeof(piProcInfo));
  ZeroMemory( &siStartInfo, sizeof(siStartInfo));

  siStartInfo.cb = sizeof(STARTUPINFO);
  siStartInfo.lpReserved = NULL;

	WCHAR cmdline[1024];
	wsprintf( cmdline, _T("%s %hs"), filename, args ); 

  /* Create the child process. */
  if (!CreateProcess(filename,
	  cmdline,       /* command line                       */
	  NULL,          /* process security attributes        */
	  NULL,          /* primary thread security attributes */
	  TRUE,          /* handles are inherited              */
	  0,             /* creation flags                     */
	  NULL,          /* use parent's environment           */
	  NULL,          /* use parent's current directory     */

	  &siStartInfo,  /* STARTUPINFO pointer                */
	  &piProcInfo))  /* receives PROCESS_INFORMATION       */
    return 0;
  else
	{
		piProcInfoValid = true;

		//need to wait to ensure that process has started. We do this by
		// waiting for the roborealm server event for up to 30 seconds!!
		int res = WaitForSingleObject(serverReady, 30000);
		if (res==WAIT_OBJECT_0) {
			// wait a bit to make sure the window is ready
			Sleep( 500 );

			// try and minimize the window
			EnumWindows( minimizeByProcessId, (LPARAM)&piProcInfo.dwProcessId );

	  		return 1;
		} else
			return 0;
	}
}


/* Closes the roborealm application nicely. */
int RR_API::close()
{
	if (!connected) return 0;

	int i;
	for ( i=0; i<10; i++ ) { // try a few times if it doesn't go through
		 strcpy(buffer, "<request><close></close></request>");
		 send(handle, buffer, (int)strlen(buffer), NULL);

		  // read in variable length
		  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
			{
				if (_stricmp(buffer, "<response>ok</response>")!=0) { // failure
					if ( piProcInfoValid ) { // attempt to force the process to close
						TerminateProcess( piProcInfo.hProcess, 1 );
					}
					return 2; // attempted to force close
				} else
					return 1; // success
			}
		  Sleep( 10 );
	}

	return -2; // give up
}

//////////////////////////////////// Basic Image Load/Save routines ////////////////////////

// Utility routine to save a basic PPM
int RR_API::savePPM(char *filename, unsigned char *buffer, int width, int height)
{
  FILE *fp;
  int num;
  int len;
  int length=width*height*3;

  if ((fp=fopen(filename,"wb"))!=NULL)
  {
    fprintf(fp, "P6\n%d %d\n255\n", width, height);

    for (len=0;len<length;len+=num)
    {
      if ((num=(length-len))>4096)
        num=4096;
      num=(int)fwrite(&buffer[len], 1, num, fp);
    }

		fclose(fp);
  }
  return len;
}

unsigned char *RR_API::readLine(FILE *fp, unsigned char *buffer)
{
  while (!feof(fp))
  {
    fscanf(fp, "%[^\n]\n", buffer);
    if (buffer[0]!='#')
      return buffer;
  }
  return NULL;
}

// Utility routine to load a basic PPM. Note that this routine does NOT handle
// comments and is only included as a quick example.
int RR_API::loadPPM(char *filename, unsigned char *buffer, int *width, int *height, int max)
{
  FILE *fp;
  int len, num, w,h;

  if ((fp=fopen(filename,"rb"))!=NULL)
	{
    readLine(fp, buffer);
		if (strcmp((char *)buffer, "P6")!=0)
		{
			printf("Illegal format!\n");
			fclose(fp);
			return -1;
		}
		
    readLine(fp, buffer);
		sscanf((char *)buffer, "%d %d", &w, &h);
		
		*width=w;
		*height=h;
		
    readLine(fp, buffer);
		if (strcmp((char *)buffer, "255")!=0)
		{
			printf("Illegal format!\n");
			fclose(fp);
			return -1;
		}
		
		for (len=0;(len<w*h*3)&&(len<max);len+=num)
		{
			if (len+65535>max) num=max-len; else num=65535;
			num=(int)fread(&buffer[len], 1, num, fp);
			if (num==0) break;
		}
		
		fclose(fp);
		
		return 1;
	}
	
	return -1;
}

/*
Sets the value of the specified parameter.
	module - input - the name of the module which contains the parameter
	module_number - input - module count in case you have more than one of the same module
	name - input - the name of the variable to set
	value - input - contains the current value of the variable to be set
*/
bool RR_API::setParameter(char *module, int count, char *name, char *value)
{
	if (!connected) return false;
	if ((module==NULL)||(module[0]==0)) return false;
	if ((name==NULL)||(name[0]==0)) return false;

	// create request message
	strcpy(buffer, "<request><set_parameter><module>");
	escape(module, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</module><module_number>");
	_itoa(count, &buffer[(int)strlen(buffer)], 10);
	strcat(buffer, "</module_number><name>");
	escape(name, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</name><value>");
	escape(value, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</value></set_parameter></request>");
  
	// send that message to RR Server
	send(handle, buffer, (int)strlen(buffer), NULL);

  // read in confirmation
  if (readMessage(handle, (unsigned char *)buffer, 1024)>0)
	{
		if (_stricmp(buffer, "<response>ok</response>")!=0)
			return false;
		else
			return true;
	}

	return false;
}

/*
Returns the value of the specified parameter.
	module - input - the name of the module which contains the parameter
	module_number - input - module count in case you have more than one of the same module
	name - input - the name of the parameter to query
	result - output - contains the current value of the parameter
	max - input - the maximum size of what the result can hold
*/
bool RR_API::getParameter(char *module, int count, char *name, char *result, int max)
{
	if (!connected) return false;
	if ((name==NULL)||(name[0]==0)) return false;

	strcpy(buffer, "<request><get_parameter><module>");
	escape(module, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</module><module_number>");
	_itoa(count, &buffer[(int)strlen(buffer)], 10);
	strcat(buffer, "</module_number><name>");
	escape(name, &buffer[(int)strlen(buffer)], 4096-(int)strlen(buffer));
	strcat(buffer, "</name></get_parameter></request>");

	result[0]=0;

  send(handle, buffer, (int)strlen(buffer), NULL);

  // read in variable length
  if (readMessage(handle, (unsigned char *)buffer, max)>0)
	{
		sscanf(buffer, "<response><%*[^>]>%[^<]</%*[^>]></response>", result);
		unescape(result);
		return true;
	}

	return false;
}
