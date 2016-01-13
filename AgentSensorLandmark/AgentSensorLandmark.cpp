// AgentSensorLandmark.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"

#include "..\\autonomic\\autonomic.h"
#include "..\\autonomic\\DDB.h"
#include "..\\autonomic\\fImage.h"

#include "AgentSensorLandmark.h"
#include "AgentSensorLandmarkVersion.h"

#include "..\\AgentSensorProcessing\\AgentSensorProcessingVersion.h"
#include "..\\SupervisorSLAM\\SupervisorSLAMVersion.h"

#include "..\\include\\CxImage\\ximage.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define pfStateX( pfState, i ) pfState[i*3]
#define pfStateY( pfState, i ) pfState[i*3+1]
#define pfStateR( pfState, i ) pfState[i*3+2]


int drawLine( unsigned char *pixels, int width, int height, float a, float b ) {
	unsigned char color[] = { 0, 0, 255 }; // b,g,r
	int r, c, nextc;
	int start, end, dir;

	if ( b >= 0 ) {
		start = 0;
		end = height;
		dir = 1;
	} else {
		start = height-1;
		end = -1;
		dir = -1;
	}
	
	c = (int)(a + b*start);
	for ( r = start; r != end; r += dir ) {
		nextc = (int)(a + b*r);	
		while ( c <= nextc ) {
			if ( c < 0 )
				continue;
			if ( c >= width )
				break;
			memcpy( pixels + (c + width*r)*3, color, 3 );
			c++;
		}
		if ( c >= width )
			break;
		c = nextc;
	}

	return 0;
}

int drawPinhole( unsigned char *pixels, int width, int height, int c, int r, int color = 0 ) {
	unsigned char colors[2][3] = { {0, 255, 255}, {255, 0, 255} }; // b,g,r

	if ( c < 0 || c >= width )
		return 1;
	if ( r < 0 || r >= height )
		return 1;

	if ( c - 1 >= 0 ) {
		//if ( r - 1 >= 0 ) 
		//	memcpy( pixels + ((c-1) + width*(r-1))*3, colors[color], 3 );
		memcpy( pixels + ((c-1) + width*(r))*3, colors[color], 3 );
		//if ( r + 1 < height )
		//	memcpy( pixels + ((c-1) + width*(r+1))*3, colors[color], 3 );
	}

	//if ( r - 1 >= 0 ) 
	//	memcpy( pixels + ((c) + width*(r-1))*3, colors[color], 3 );
	//memcpy( pixels + ((c) + width*(r))*3, colors[color], 3 );
	//if ( r + 1 < height )
	//	memcpy( pixels + ((c) + width*(r+1))*3, colors[color], 3 );

	if ( c + 1 >= 0 ) {
		//if ( r - 1 >= 0 ) 
		//	memcpy( pixels + ((c+1) + width*(r-1))*3, colors[color], 3 );
		memcpy( pixels + ((c+1) + width*(r))*3, colors[color], 3 );
		//if ( r + 1 < height )
		//	memcpy( pixels + ((c+1) + width*(r+1))*3, colors[color], 3 );
	}

	return 0;
}

int drawX( unsigned char *pixels, int width, int height, int c, int r, int color = 0 ) {
	unsigned char colors[2][3] = { {0, 255, 0}, {255, 255, 255} }; // b,g,r

	if ( c < 0 || c >= width )
		return 1;
	if ( r < 0 || r >= height )
		return 1;

	/*if ( c - 3 > 0 ) {
		if ( r + 3 <= height - 1 )
			memcpy( pixels + ((c-3) + width*(r+3))*3, colors[color], 3 );
		if ( r - 3 > 0 )
			memcpy( pixels + ((c-3) + width*(r-3))*3, colors[color], 3 );
	}
	if ( c - 2 > 0 ) {
		if ( r + 2 <= height - 1 )
			memcpy( pixels + ((c-2) + width*(r+2))*3, colors[color], 3 );
		if ( r - 2 > 0 )
			memcpy( pixels + ((c-2) + width*(r-2))*3, colors[color], 3 );
	}
	if ( c - 1 > 0 ) {
		if ( r + 1 <= height - 1 )
			memcpy( pixels + ((c-1) + width*(r+1))*3, colors[color], 3 );
		if ( r - 1 > 0 )
			memcpy( pixels + ((c-1) + width*(r-1))*3, colors[color], 3 );
	}
	*/

	memcpy( pixels + (c + width*r)*3, colors[color], 3 );

	if ( c + 1 <= width - 1 ) {
		if ( r + 1 <= height - 1 )
			memcpy( pixels + ((c+1) + width*(r+1))*3, colors[color], 3 );
		if ( r - 1 > 0 )
			memcpy( pixels + ((c+1) + width*(r-1))*3, colors[color], 3 );
	}
	if ( c + 2 <= width - 1 ) {
		if ( r + 2 <= height - 1 )
			memcpy( pixels + ((c+2) + width*(r+2))*3, colors[color], 3 );
		if ( r - 2 > 0 )
			memcpy( pixels + ((c+2) + width*(r-2))*3, colors[color], 3 );
	}
	/*if ( c + 3 <= width - 1 ) {
		if ( r + 3 <= height - 1 )
			memcpy( pixels + ((c+3) + width*(r+3))*3, colors[color], 3 );
		if ( r - 3 > 0 )
			memcpy( pixels + ((c+3) + width*(r-3))*3, colors[color], 3 );
	}*/

	return 0;
}

//*****************************************************************************
// AgentSensorLandmark

//-----------------------------------------------------------------------------
// Constructor	
AgentSensorLandmark::AgentSensorLandmark( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile )  : AgentBase( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile ) {
	
	// allocate state
	ALLOCATE_STATE( AgentSensorLandmark, AgentBase )

	sprintf_s( STATE(AgentBase)->agentType.name, sizeof(STATE(AgentBase)->agentType.name), "AgentSensorLandmark" );
	UUID typeId;
	UuidFromString( (RPC_WSTR)_T(AgentSensorLandmark_UUID), &typeId );
	STATE(AgentBase)->agentType.uuid = typeId;

	if ( AgentSensorLandmark_TRANSFER_PENALTY < 1 ) STATE(AgentBase)->noCrash = false; // we're ok to crash

	// Prepare callbacks
	this->callback[AgentSensorLandmark_CBR_convRequestSensorInfo] = NEW_MEMBER_CB(AgentSensorLandmark,convRequestSensorInfo);
	this->callback[AgentSensorLandmark_CBR_convRequestPFInfo] = NEW_MEMBER_CB(AgentSensorLandmark,convRequestPFInfo);
	this->callback[AgentSensorLandmark_CBR_cbRepeatRPFInfo] = NEW_MEMBER_CB(AgentSensorLandmark,cbRepeatRPFInfo);
	this->callback[AgentSensorLandmark_CBR_convRequestData] = NEW_MEMBER_CB(AgentSensorLandmark,convRequestData);
	this->callback[AgentSensorLandmark_CBR_convRequestLandmarkInfo] = NEW_MEMBER_CB(AgentSensorLandmark,convRequestLandmarkInfo);
	this->callback[AgentSensorLandmark_CBR_convRequestPFId] = NEW_MEMBER_CB(AgentSensorLandmark,convRequestPFId);
	this->callback[AgentSensorLandmark_CBR_convRequestLandmarkPF] = NEW_MEMBER_CB(AgentSensorLandmark,convRequestLandmarkPF);
	this->callback[AgentSensorLandmark_CBR_cbRepeatRLandmarkPF] = NEW_MEMBER_CB(AgentSensorLandmark,cbRepeatRLandmarkPF);

	STATE(AgentSensorLandmark)->nextReadingId = 0;

	this->obsDensitySize = 0;
	this->obsDensity = NULL;
	this->obsDensitySize2 = 0;
	this->obsDensity = NULL;
}

//-----------------------------------------------------------------------------
// Destructor
AgentSensorLandmark::~AgentSensorLandmark() {

	if ( STATE(AgentBase)->started ) {
		this->stop();
	}

}

//-----------------------------------------------------------------------------
// Configure

int AgentSensorLandmark::configure() {
	
	if ( Log.getLogMode() == LOG_MODE_OFF ) {
		// Init logging
		RPC_WSTR rpc_wstr;
		char logName[256];
		char timeBuf[64];
		time_t t_t;
		struct tm stm;
		apb->apbtime( &t_t );
		localtime_s( &stm, &t_t );
		strftime( timeBuf, 64, "%y.%m.%d [%H.%M.%S]", &stm );
		UuidToString( &STATE(AgentBase)->uuid, &rpc_wstr );
		sprintf_s( logName, "%s\\AgentSensorLandmark %s %ls.txt", logDirectory, timeBuf, rpc_wstr );

		Log.setLogMode( LOG_MODE_COUT );
		Log.setLogMode( LOG_MODE_FILE, logName );
		Log.setLogLevel( LOG_LEVEL_VERBOSE );
		Log.log( 0, "AgentSensorLandmark %.2d.%.2d.%.5d.%.2d", AgentSensorLandmark_MAJOR, AgentSensorLandmark_MINOR, AgentSensorLandmark_BUILDNO, AgentSensorLandmark_EXTEND );
	}

	if ( AgentBase::configure() ) 
		return 1;
	
	return 0;
}

//-----------------------------------------------------------------------------
// Start

int AgentSensorLandmark::start( char *missionFile ) {

	if ( AgentBase::start( missionFile ) ) 
		return 1;
	
	return 0;
}


//-----------------------------------------------------------------------------
// Stop

int AgentSensorLandmark::stop() {

	// clean up poses
	this->sensorPose.clear();

	// clean up readings.
	mapReadings::iterator iterR;
	while ( this->readings.size() ) {
		iterR = this->readings.begin();
		if ( this->frozen )
			this->deleteReading( &iterR->second );
		else
			this->finishReading( &iterR->second, 0 ); // graceful exit
	}

	// clean up buffers
	if ( this->obsDensitySize ) {
		free( this->obsDensity );
		this->obsDensitySize = 0;
	}
	if ( this->obsDensitySize2 ) {
		free( this->obsDensity2 );
		this->obsDensitySize2 = 0;
	}
	
	return AgentBase::stop();
}


//-----------------------------------------------------------------------------
// Step

int AgentSensorLandmark::step() {
	return AgentBase::step();
}

int AgentSensorLandmark::configureParameters( DataStream *ds ) {
	UUID uuid;

	ds->unpackUUID( &uuid );
	STATE(AgentSensorLandmark)->ownerId = uuid;

	return 0;
}

int AgentSensorLandmark::addPose( UUID *sensor, CameraPose *pose ) {
	this->sensorPose[ *sensor ] = *pose;

	return 0;
}

int AgentSensorLandmark::addReading( UUID *sensor, _timeb *time ) {
	PROCESS_READING pr;
	UUID thread;

	pr.id = STATE(AgentSensorLandmark)->nextReadingId++;
	pr.waiting = 0;
	pr.sensor = *sensor;
	pr.time = *time;

	pr.landmarks = new std::list<LANDMARK_HIT>;

	Log.log( LOG_LEVEL_NORMAL, "AgentSensorLandmark::addReading: %d %s", pr.id, Log.formatUUID(LOG_LEVEL_VERBOSE,&pr.sensor) );

	pr.waiting |= WAITING_ON_PF;

	// get avatar and pose, or PF if avatar and pose are already known
	mapPose::iterator iterP = this->sensorPose.find( *sensor );
	if ( iterP == this->sensorPose.end() ) {
		pr.waiting |= WAITING_ON_POSE;
		// request avatar/pose
		thread = this->conversationInitiate( AgentSensorLandmark_CBR_convRequestSensorInfo, DDB_REQUEST_TIMEOUT, &pr.id, sizeof(int) );
		if ( thread == nilUUID ) {
			return 1;
		}
		this->ds.reset();
		this->ds.packUUID( sensor );
		this->ds.packInt32( DDBSENSORINFO_PF | DDBSENSORINFO_POSE );
		this->ds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RSENSORINFO, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::addReading: requesting pf id and pose (reading %d)", pr.id );
	} else { // we know the avatar so we can request the PF right away
		// request PF
		thread = this->conversationInitiate( AgentSensorLandmark_CBR_convRequestPFInfo, DDB_REQUEST_TIMEOUT, &pr.id, sizeof(int) );
		if ( thread == nilUUID ) {
			return 1;
		}
		this->ds.reset();
		this->ds.packUUID( &this->sensorAvatar[ *sensor ] );
		this->ds.packInt32( DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT );
		this->ds.packData( &pr.time, sizeof(_timeb) );
		this->ds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::addReading: requesting pf data (reading %d)", pr.id );
	}

	// get Data
	pr.waiting |= WAITING_ON_DATA;
	// request data
	thread = this->conversationInitiate( AgentSensorLandmark_CBR_convRequestData, DDB_REQUEST_TIMEOUT, &pr.id, sizeof(int) );
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID( sensor );
	this->ds.packData( time, sizeof(_timeb) );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RSENSORDATA, this->ds.stream(), this->ds.length() );
	this->ds.unlock();
	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::addReading: requesting data (reading %d)", pr.id );
	
	// add reading
	this->readings[pr.id] = pr;

	return 0;
}

int AgentSensorLandmark::finishReading( PROCESS_READING *pr, char success ) {

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::finishReading: reading %d %s success == %d", pr->id, Log.formatUUID(LOG_LEVEL_VERBOSE,&pr->sensor), success );

	// notify owner
	this->ds.reset();
	this->ds.packUUID( &STATE(AgentBase)->uuid );
	this->ds.packUUID( &pr->sensor );
	this->ds.packData( &pr->time, sizeof(_timeb) );
	this->ds.packChar( success );
	this->sendMessage( this->hostCon, MSG_DONE_SENSOR_READING, this->ds.stream(), this->ds.length(), &STATE(AgentSensorLandmark)->ownerId );
	this->ds.unlock();

	this->deleteReading( pr );

	return 0;
}

int AgentSensorLandmark::deleteReading( PROCESS_READING *pr ) {
	
	// clean up reading
	if ( !(pr->waiting & WAITING_ON_DATA) ) {
		freeDynamicBuffer( pr->readingRef );
		if ( pr->dataSize )
			freeDynamicBuffer( pr->dataRef );
	}
	if ( !(pr->waiting & WAITING_ON_PF) ) {
		freeDynamicBuffer( pr->pfStateRef );
		freeDynamicBuffer( pr->pfWeightRef );
	}

	delete pr->landmarks;

	this->readings.erase( pr->id );

	return 0;
}

int findPattern( unsigned char *pixels, int width, int height, std::list<HIT_LOC> *hits ) {

	HIT_LOC coord;
	int r, c, e;
	int spacing = PATTERN_SPACING;
	int window = PATTERN_WINDOW;
	int dx = window/10;

	float p = PATTERN_P;
	
	float pr = sqrt( p );
	int pelow, pehigh, prlow, prhigh;
	float interpe, interpr;

	float mxThresh = PATTERN_THRESHOLD;
	float mxShape = PATTERN_SHAPE;

	float *mx = (float *)malloc( sizeof(float)*width*2 ); // allocate extra so we can use some for bw
	if ( mx == NULL )
		return 1;

	float *bw = mx + width;

	r = 0;
	while ( r < height ) {
		// black and white the row
		for ( c=0; c<width; c++ )
			bw[ c ] = (( (float)pixels[ (c + width*r)*3 ] + pixels[ (c + width*r)*3 + 1 ] + pixels[ (c + width*r)*3 + 2 ] ) / 3.0f) / 255;

		for ( c=0; c<width-window; c++ ) {
			mx[c] = 0;

			for ( e=0; e<window; e++ ) {
				pelow = (int)floor(p*e);
				pehigh = pelow + 1;
				prlow = (int)floor(pr*e);
				prhigh = prlow + 1;
				
				interpe = 1 - (p*e - pelow);
				interpr = 1 - (pr*e - prlow);

				mx[c] += fabs( bw[c+e] - interpr*bw[c+prlow] - (1-interpr)*bw[c+prhigh] )
						- fabs( bw[c+e] - interpe*bw[c+pelow] - (1-interpe)*bw[c+pelow] ); 
			
			
			}

			mx[c] /= window;
		}

		// find peaks
		for ( c=dx; c<width-window-dx; c++ ) {
			if ( mx[c-1] < mx[c] && mx[c+1] < mx[c] ) {					// local max
				if ( mx[c] > mxThresh ) {								// passes threshold
					if ( mx[c-dx] < mx[c]/mxShape && mx[c+dx] < mx[c]/mxShape ) {	// correct shape
						coord.r = r;
						coord.c = c;
						hits->push_back( coord ); 
					}
				}
			}
		}


		r += spacing;
	}

	free( mx );

	return 0;
}

int findEndpoints( float a, float b, float mid, unsigned char *pixels, int width, int height, LANDMARK_HIT *landmark ) {
	int r, c;
	float *bw, *edge;
	float ed[] = { 1, 1, 0, -1, -1 };
	float thresh = 0.3f;

	// recognition offset (we always find hits to the right of the real start point)
	float offset = RECOGNITION_OFFSET;
	float ro, co;
	co = sqrt( offset*offset / ( b*b + 1 ) );
	ro = b*co;

	// allocate memory
	edge = (float *)malloc(sizeof(float)*height*2);
	if ( edge == NULL )
		return 1; // malloc failed

	bw = edge + height;

	// bw the line
	for ( r=0; r<height; r++ ) {
		c = (int)(a + b*r) + 1; // shift one pixel right to get away from barcode
		bw[r] = (( (float)pixels[ (c + width*r)*3 ] + pixels[ (c + width*r)*3 + 1 ] + pixels[ (c + width*r)*3 + 2 ] ) / 3.0f) / 255;
	}

	// run edge detector
	for ( r=2; r<height-2; r++ ) {
		edge[r] = bw[r-2]*ed[0] + bw[r-1]*ed[1] + bw[r]*ed[2] + bw[r+1]*ed[3] + bw[r+2]*ed[4];
	}

	// search up for local min
	r = (int)mid + 1;
	while ( r < height - 3 ) {
		if ( edge[r] < -thresh && edge[r] < edge[r-1] && edge[r] < edge[r+1] )
			break;
		r++;
	}
	if ( r >= height - 3 ) {
		free( edge );
		return 1; // didn't find a top point
	}

	landmark->r1 = (float)r;
	landmark->c1 = a + b*r;

	// compensate for recognition offset
	landmark->r1 += ro;
	landmark->c1 += -co;

	// search down for local max
	r = (int)mid - 1;
	while ( r >= 3 ) {
		if ( edge[r] > thresh && edge[r] >= edge[r-1] && edge[r] >= edge[r+1] )
			break;
		r--;
	}
	if ( r < 3 ) {
		free( edge );
		return 1; // didn't find a bottom point
	}

	landmark->r2 = (float)r;
	landmark->c2 = a + b*r;

	// compensate for recognition offset
	landmark->r2 += ro;
	landmark->c2 += -co;

	free( edge );

	return 0;
}

int findLandmarks( std::list<HIT_LOC> *points, unsigned char *pixels, int width, int height, std::list<LANDMARK_HIT> *landmarks ) {
	std::list<HIT_LOC> claimed;
	std::list<HIT_LOC>::iterator iter, iterErase;
	int i;
	float a, b;
	float xbar, ybar, ssxx, ssyy, ssxy, sumxx, sumyy, sumxy, wtotal;
	float rr, lastrr, rrstep;
	float d;
	float mid;
	LANDMARK_HIT lmark;

	lmark.simStream = false; // all landmarks we find here will be real

	rrstep = 0.0001f;

	while ( points->size() > 2 ) {

		// prepare weights
		iter = points->begin();
		while ( iter != points->end() ) {
			iter->w = 1;
			iter++;
		}

		lastrr = 999999999.9f;

		for ( i=0; i<100; i++ ) {
			// E(xpectation)
			xbar = 0;
			ybar = 0;
			sumxx = 0;
			sumyy = 0;
			sumxy = 0;
			wtotal = 0;
			iter = points->begin();
			while ( iter != points->end() ) {
				sumxx += iter->c * iter->c * iter->w;
				sumyy += iter->r * iter->r * iter->w;
				sumxy += iter->r * iter->c * iter->w;
				xbar += iter->c * iter->w;
				ybar += iter->r * iter->w;
				wtotal += iter->w;
				iter++;
			}
			xbar /= wtotal;
			ybar /= wtotal;

			ssxx = sumxx - wtotal*xbar*xbar;
			ssyy = sumyy - wtotal*ybar*ybar;
			ssxy = sumxy - wtotal*xbar*ybar;

			b = ssxy/ssyy;
			a = xbar - b*ybar;

			// check fit
			rr = ssxy*ssxy / (ssxx*ssyy);
			if ( lastrr - rr > 0 && lastrr - rr < rrstep ) {
				break;
			}
			lastrr = rr;


			// M(aximization)
			iter = points->begin();
			while ( iter != points->end() ) {
				d = iter->c - (a + b*iter->r);
				iter->w = 1.0f / ( 1 + d*d );
				iter++;
			}
		}

		// claim points with high weights
		claimed.clear();
		iter = points->begin();
		while ( iter != points->end() ) {
			if ( iter->w > 1.0f / ( 1 + 5*5 ) ) {
				claimed.push_back( *iter );
				iterErase = iter;
				iter++;
				points->erase( iterErase );
			} else {
				iter++;
			}
		}

		if ( claimed.size() >= 3 ) {
			iter = claimed.begin();
			iter++;
			mid = (float)iter->r; // use second point as a safe midpoint
			if ( !findEndpoints( a, b, mid, pixels, width, height, &lmark ) ) {
				landmarks->push_back( lmark );
			}
		} else if ( claimed.empty() ) { // give up because we couldn't find a good line
			break;
		}
	}

	return 0;
}

int AgentSensorLandmark::readBarcodes( std::list<LANDMARK_HIT> *landmarks, unsigned char *pixels, int width, int height ) {
	std::list<LANDMARK_HIT>::iterator iter, iterErase;

	float dr, dc, bw1, bw2;
	float fr, interp;
	int i, r, c, offset;
	int rmost, cmost;
	int check;

	offset = BARCODE_OFFSET;

	iter = landmarks->begin();
	while ( iter != landmarks->end() ) {
		
		dr = iter->r2-iter->r1;
		dc = iter->c2-iter->c1;

		iter->id = 0;
		for ( i=0; i<8; i++ ) {
			fr = (iter->r1 + dr*(i+0.5f)/8);
			r = (int)floor(iter->r1 + dr*(i+0.5f)/8);
			c = (int)floor(iter->c1 + dc*(i+0.5f)/8 + 0.5f) - offset;

			interp = fr - r;

			if ( interp < 0.5f ) {
				rmost = r;
				cmost = c;
			}

			bw1 = (( (float)pixels[ (c + width*r)*3 ] + pixels[ (c + width*r)*3 + 1 ] + pixels[ (c + width*r)*3 + 2 ] ) / 3.0f) / 255;

			r += 1;
			c = (int)floor(iter->c1 + dc * (r-iter->r1)/dr + 0.5f) - offset;

			if ( interp >= 0.5f ) {
				rmost = r;
				cmost = c;
			}

			bw2 = (( (float)pixels[ (c + width*r)*3 ] + pixels[ (c + width*r)*3 + 1 ] + pixels[ (c + width*r)*3 + 2 ] ) / 3.0f) / 255;

			if ( bw1*(1-interp) + bw2*interp > 0.5f ) {
				iter->id += 0x1 << i;
				// DEBUG draw dot
				if ( DEBUG_IMG_DUMP ) drawPinhole( pixels, width, height, cmost, rmost, 0 );
			} else {
				// DEBUG draw dot
				if ( DEBUG_IMG_DUMP ) drawPinhole( pixels, width, height, cmost, rmost, 1 );
			}
		}

		// check checksum
		check = 0;
		for ( i=0; i<6; i++ ) {
			check += ((iter->id & (0x1 << i)) != 0);
		}
		if ( check % 4 == (unsigned char)(iter->id) >> 6 ) {
			iter->id = iter->id & 0x3F;
			Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::readBarcodes: found landmark %d", iter->id );
			iter++;
		} else { // discard landmark
			iterErase = iter;
			iter++;
			landmarks->erase( iterErase );
			Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::readBarcodes: bad checksum" );
		}
	}

	return 0;
}

void applyHomography( float *H, float px, float py, float *qx, float *qy ) {
	float norm = 1.0f/(H[6]*px + H[7]*py + H[8]);
	*qx = (H[0]*px + H[1]*py + H[2])*norm;
	*qy = (H[3]*px + H[4]*py + H[5])*norm;
}

int AgentSensorLandmark::processReading( PROCESS_READING *pr ) {
	RPC_STATUS Status;

	CxImage *image;
	CxMemFile memfile;
	BYTE *bmpbuffer;
	int bmpbufferSize;
	int bmpoffset;

	CameraReading *reading;
	unsigned char *pixels;
	int width, height;

	std::list<HIT_LOC> hits;

	bool simStream;
	
	reading = (CameraReading *)getDynamicBuffer( pr->readingRef );
	void *data = getDynamicBuffer( pr->dataRef );

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::processReading: starting reading %d", pr->id );

	// convert to bmp and ready pixel pointer
	if ( !strcmp( reading->format, "bmp" ) ) {
		bmpbuffer = (BYTE *)data;
		bmpbufferSize = pr->dataSize;
		bmpoffset = *(int *)((char *)(data)+0x000A);
		pixels = (unsigned char *)((char *)(data) + bmpoffset);
	} else if ( !strcmp( reading->format, "jpg" ) ) {
		image = new CxImage( (BYTE*)data, pr->dataSize, CXIMAGE_FORMAT_JPG );
		memfile.Open();
		image->Encode(&memfile,CXIMAGE_FORMAT_BMP);
		bmpbuffer = memfile.GetBuffer();
		bmpbufferSize = memfile.Size();
		
		bmpoffset = *(int *)(bmpbuffer+0x000A);
		pixels = (unsigned char *)(bmpbuffer + bmpoffset);
	} else if ( !strcmp( reading->format, "stream" ) ) { // simulation input
		simStream = true;
	} else { 
		Log.log( LOG_LEVEL_NORMAL, "AgentSensorLandmark::processReading: unknown image format %s", reading->format );
		this->finishReading( pr, 0 ); 
		return 0;
	}

	if ( !simStream ) { // process image
		width = reading->w;
		height = reading->h;

		findPattern( pixels, width, height, &hits );
		int hitCount = (int)hits.size();

		if ( DEBUG_IMG_DUMP && pr->id < 15 ) { // draw hits, only dump 15 images at most to cut down on file counts
			// dump clean bmp
			char filename[256];
			sprintf_s( filename, 256, "%s\\AgentSensorLandmark%d.bmp", this->logDirectory, pr->id );
			FILE *dumpF;
			if ( !fopen_s( &dumpF, filename, "wb" ) ) {
				fwrite( bmpbuffer, 1, bmpbufferSize, dumpF );
				fclose( dumpF );
			}

			std::list<HIT_LOC>::iterator iter;
			
			iter = hits.begin();
			while ( iter != hits.end() ) {
				//drawX( pixels, width, height, iter->c, iter->r );  // CAREFUL, this will mess up later parts by drawing over the image
				iter++;
			}
		}
		
		findLandmarks( &hits, pixels, width, height, pr->landmarks );

		Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::processReading: found %d hits and %d landmarks", hitCount, pr->landmarks->size() );

		readBarcodes( pr->landmarks, pixels, width, height );

		if ( DEBUG_IMG_DUMP && pr->id < 15 ) { // draw end points and dump image
			
			std::list<LANDMARK_HIT>::iterator iter;

			iter = pr->landmarks->begin();
			while ( iter != pr->landmarks->end() ) {
				
				drawX( pixels, width, height, (int)iter->c1, (int)iter->r1, 1 );	
				drawX( pixels, width, height, (int)iter->c2, (int)iter->r2, 1 );

				iter++;

			}

			// dump bmp
			char filename[256];
			sprintf_s( filename, 256, "%s\\AgentSensorLandmark%dDebug.bmp", this->logDirectory, pr->id );
			FILE *dumpF;
			if ( !fopen_s( &dumpF, filename, "wb" ) ) {
				fwrite( bmpbuffer, 1, bmpbufferSize, dumpF );
				fclose( dumpF );
			}
		}

		// clean up CxImage if necessary
		if ( !strcmp( reading->format, "jpg" ) ) {
			image->FreeMemory(bmpbuffer);
			memfile.Close();
			delete image;
		}
	} else { // parse simualtion image stream
		LANDMARK_HIT lmark;
		float lmD, lmA;
		char dataType;
		
		this->ds.setData( (char *)data, pr->dataSize );

		while (1) {
			dataType = this->ds.unpackChar();
			if ( dataType == SimCamera_DATA_END )
				break;
			switch ( dataType ) {
			case SimCamera_DATA_LANDMARK:
				lmark.simStream = true;
				lmark.id = this->ds.unpackUChar();
				
				lmD = this->ds.unpackFloat32();
				lmA = this->ds.unpackFloat32();

				lmark.px = lmD * cos(lmA);
				lmark.py = lmD * sin(lmA);		

				pr->landmarks->push_back( lmark );
				break;
			case SimCamera_DATA_FLOORFINDER:
				this->ds.unpackData( 11*sizeof(float) ); // skip
				break;
			default:
				this->ds.unlock();
				Log.log( 0, "AgentSensorLandmark::processReading: unhandled data type in simStream %d", dataType );
				this->finishReading( pr, 0 ); 
				return 1; // unhandled data type
			};
		}
		this->ds.unlock();
		Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::processReading: parsed %d landmarks from simStream", pr->landmarks->size() );
	}

	// if we have no landmarks then we're done
	if ( pr->landmarks->size() == 0 ) {
		this->finishReading( pr, 1 );
		return 0;
	}

	// calculate vectors and initiate landmark processing
	CameraPose *pose;
	mapPose::iterator poseIter = this->sensorPose.find( pr->sensor );
	if ( poseIter == this->sensorPose.end() ) {
		this->finishReading( pr, 0 ); // how did this happen?
		return 0;
	}
	pose = &poseIter->second;

	float px, pz;

	std::list<LANDMARK_HIT>::iterator iter;
	std::list<LANDMARK_HIT>::iterator next;

	iter = pr->landmarks->begin();
	while ( iter != pr->landmarks->end() ) {
		iter->waiting = 0;

		if ( !simStream ) { // process real landmark
			applyHomography( pose->I, iter->c1 - width/2, iter->r1 - height/2, &px, &pz );
			iter->u[0] = px;
			iter->u[1] = pz - pose->z;
			iter->u[2] = -pose->planeD;

			applyHomography( pose->I, iter->c2 - width/2, iter->r2 - height/2, &px, &pz );
			iter->v[0] = px;
			iter->v[1] = pz - pose->z;
			iter->v[2] = -pose->planeD;
		} // else landmark->px and landmark->py are already set

		// look up landmark id
		mapLandmark::iterator iterId = this->landmark.find( iter->id );
		if ( iterId == this->landmark.end() || iterId->second.estimatedPos == true ) { // get the pose again if it is being estimated
			iter->waiting = WAITING_ON_POSE;
			this->getLandmarkInfo( pr->id, iter->id );

			iter++;
			continue;
		}

		iter->info = iterId->second;

		if ( !UuidIsNil( &iterId->second.owner, &Status ) ) {
			std::map<UUID,UUID,UUIDless>::iterator iPF = this->avatarPF.find( iterId->second.owner );
			if ( iPF == this->avatarPF.end() ) { // need to find out pf id
				iter->waiting = WAITING_ON_PF_ID;
				this->getLandmarkPFId( pr, iter->id, &iterId->second.owner );
			} else { // know the pf id
				iter->waiting = WAITING_ON_PF;
				this->getLandmarkPF( pr, iter->id, &iPF->second );
			}
		}

		iter++;

	}

	// process all the landmarks that are ready now
	next = pr->landmarks->begin();
	while ( next != pr->landmarks->end() ) {
		iter = next;
		next++;
		
		if ( !iter->waiting ) { // we're not waiting on anything
			if ( -1 == this->processLandmark( pr, iter ) )
				break; // this reading is finished
		}
	}

	return 0;
}

int AgentSensorLandmark::processLandmark( PROCESS_READING *pr, std::list<LANDMARK_HIT>::iterator landmark ) {
	RPC_STATUS Status;
	float s, t, d;
	int p, q;

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::processLandmark: reading %d landmark %d, estimatedPos %d", pr->id, landmark->id, landmark->info.estimatedPos );

	CameraPose *pose = &this->sensorPose[pr->sensor];
	float *pfState = (float *)this->getDynamicBuffer( pr->pfStateRef );
	float *pfWeight = (float *)this->getDynamicBuffer( pr->pfWeightRef );


	float sn, cs;
	float px, py; // landmark pos in particle frame
	float lx, ly; // landmark pos in origin frame
	float temp; 
	float landmarkH; // landmark heading from particle
	float lH; // landmark heading from origin frame
	float errX, errY; // error vector from estimated pos to real pos

	// make sure arrays are big enough
	if ( this->obsDensitySize < pr->pfNum ) {
		if ( this->obsDensitySize ) {
			free( this->obsDensity );
		}
		this->obsDensitySize = pr->pfNum;
		this->obsDensity = (float *)malloc( sizeof(float)*this->obsDensitySize );
		if ( !this->obsDensity ) {
			this->obsDensitySize = 0;
			return 1; // malloc failed
		}
	}

	if ( !landmark->simStream ) { // process real landmark
		// calculate correct points based on landmark height
		d = landmark->info.height;

		s = sqrt( d*d 
			/ (
			(landmark->u[0]*landmark->u[0] + landmark->u[1]*landmark->u[1] + landmark->u[2]*landmark->u[2]) 
			+ (landmark->v[0]*landmark->v[0] + landmark->v[1]*landmark->v[1] + landmark->v[2]*landmark->v[2])*(landmark->u[0]*landmark->u[0] + landmark->u[2]*landmark->u[2])/(landmark->v[0]*landmark->v[0] + landmark->v[2]*landmark->v[2]) 
			- 2*(landmark->u[0]*landmark->v[0] + landmark->u[1]*landmark->v[1] + landmark->u[2]*landmark->v[2])*sqrt((landmark->u[0]*landmark->u[0] + landmark->u[2]*landmark->u[2])/(landmark->v[0]*landmark->v[0] + landmark->v[2]*landmark->v[2]))
			) );
		t = s * sqrt((landmark->u[0]*landmark->u[0] + landmark->u[2]*landmark->u[2])/(landmark->v[0]*landmark->v[0] + landmark->v[2]*landmark->v[2]));

		// calculate landmark pos (camera frame)
		px = landmark->px = -(s*landmark->v[2] + t*landmark->u[2]) / 2;
		py = landmark->py = -(s*landmark->v[0] + t*landmark->u[0]) / 2;
	} // else landmark->px and landmark->py should already be set

	px = landmark->px;
	py = landmark->py;
	landmarkH = atan2( py, px );

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::processLandmark: landmark in camera frame %f,%f", px, py );

	// calculate sigmas and other constants
	float sigY = OBS_SIGMA_Y * max( 0.1f, sqrt(px*px + py*py) );  // scale based on distance from the camera
	float normX = 1/(OBS_SIGMA_X*sqrt(2*fM_PI)); 
	float normY = 1/(sigY*sqrt(2*fM_PI));
	float invSigXX2 = 1/(2 * OBS_SIGMA_X*OBS_SIGMA_X);
	float invSigYY2 = 1/(2 * sigY*sigY);

	// transform to particle frame
	sn = (float)sin(pose->r);
	cs = (float)cos(pose->r);
	temp = px*cs - py*sn;
	py = px*sn + py*cs;
	px = temp + pose->x;
	py = py + pose->y;
	landmarkH += pose->r;

	if ( UuidIsNil( &landmark->info.owner, &Status ) ) { // stationary update
		float obsDenMax = 0.0f;
		float obsDenRMS = 0.0f;
		float R, *X;

		if ( landmark->info.estimatedPos ) { // prepare a landmark pos update
			R = 2*sqrt(px*px + py*py); // observation covariance, conservative estimate as twice the distance to the landmark (i.e. even if two particles are pointing in opposite directions it should still be within R)
			if ( R < 1 )
				R = 1; // don't let the uncertainty get to small 
			//R = R*0.5f; // fudge a little more confidence to see what happens
			X = (float *)malloc(pr->pfNum*2*sizeof(float));
		}

		// loop through particles
		for ( p=0; p<pr->pfNum; p++ ) {

			// transform to origin frame
			sn = (float)sin(pfStateR(pfState,p));
			cs = (float)cos(pfStateR(pfState,p));
			lx = px*cs - py*sn + pfStateX(pfState,p);
			ly = px*sn + py*cs + pfStateY(pfState,p);
			lH = landmarkH + pfStateR(pfState,p);

			if ( landmark->info.estimatedPos ) { // prepare a landmark pos update
				X[2*p] = lx;
				X[2*p+1] = ly;
			}

			if ( landmark->info.posInitialized ) {
				// calculate err vector in origin frame
				errX = lx - landmark->info.x;
				errY = ly - landmark->info.y;

				// rotate into estimate frame
				sn = (float)sin(-lH);
				cs = (float)cos(lH);
				temp = errX*cs - errY*sn;
				errY = errX*sn + errY*cs;
				errX = temp;

				// calculate observation density
				obsDensity[p] = OBS_DENSITY_AMBIENT
					+ normX * exp( -errX*errX * invSigXX2 )
					* normY * exp( -errY*errY * invSigYY2 );
				if ( landmark->info.estimatedPos ) // factor in position uncertainty
					obsDensity[p] += landmark->info.P*10;

				if ( obsDenMax < obsDensity[p] )
					obsDenMax = obsDensity[p];
				obsDenRMS += obsDensity[p]*obsDensity[p];
			}
		}

		obsDenRMS = sqrt( obsDenRMS / pr->pfNum );

		Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::processLandmark: obsDenMax %f obsDenRMS %f", obsDenMax, obsDenRMS );

		if ( landmark->info.estimatedPos ) { // send landmark pos update
			this->ds.reset();
			this->ds.packUChar( landmark->id );
			this->ds.packInt32( DDBLANDMARKINFO_POS );
			this->ds.packInt32( pr->pfNum );
			this->ds.packFloat32( R );
			this->ds.packData( X, sizeof(float)*2*pr->pfNum );
			this->ds.packData( pfWeight, sizeof(float)*pr->pfNum );
			this->sendMessage( this->hostCon, MSG_DDB_LANDMARKSETINFO, this->ds.stream(), this->ds.length() );
			this->ds.unlock();
			free( X );
		}

		if ( landmark->info.posInitialized ) {
			// submit particle filter update
			this->ds.reset();
			this->ds.packUUID( &this->sensorAvatar[ pr->sensor ] );
			this->ds.packData( &pr->time, sizeof(_timeb) );
			this->ds.packData( obsDensity, sizeof(float)*pr->pfNum );
			this->sendMessage( this->hostCon, MSG_DDB_APPLYPFCORRECTION, this->ds.stream(), this->ds.length() );
			this->ds.unlock();
		}

	} else { // filter-filter update
		float *lState = (float *)getDynamicBuffer( landmark->pfStateRef );
		float *lWeight = (float *)getDynamicBuffer( landmark->pfWeightRef );

		// make sure arrays are big enough
		if ( this->obsDensitySize2 < landmark->pfNum ) {
			if ( this->obsDensitySize2 ) {
				free( this->obsDensity2 );
			}
			this->obsDensitySize2 = landmark->pfNum;
			this->obsDensity2 = (float *)malloc( sizeof(float)*this->obsDensitySize2 );
			if ( !this->obsDensity2 ) {
				this->obsDensitySize2 = 0;
				return 1; // malloc failed
			}
		}

		// zero observation densities
		memset( obsDensity, 0, sizeof(float)*pr->pfNum );
		memset( obsDensity2, 0, sizeof(float)*landmark->pfNum );

		// move particle origin to landmark origin
		for ( q=0; q<landmark->pfNum; q++ ) {
			sn = (float)sin(pfStateR(lState,q));
			cs = (float)cos(pfStateR(lState,q));
			pfStateX(lState,q) += landmark->info.x*cs - landmark->info.y*sn;
			pfStateY(lState,q) += landmark->info.x*sn + landmark->info.y*cs;
		}

		// loop through our particles
		for ( p=0; p<pr->pfNum; p++ ) {
			// transform to origin frame
			sn = (float)sin(pfStateR(pfState,p));
			cs = (float)cos(pfStateR(pfState,p));
			lx = px*cs - py*sn + pfStateX(pfState,p);
			ly = px*sn + py*cs + pfStateY(pfState,p);
			lH = landmarkH + pfStateR(pfState,p);

			// loop through landmark particles
			for ( q=0; q<landmark->pfNum; q++ ) {
				// calculate err vector in origin frame
				errX = lx - pfStateX(lState,q);
				errY = ly - pfStateY(lState,q);

				// rotate into estimate frame
				sn = (float)sin(-lH);
				cs = (float)cos(lH);
				temp = errX*cs - errY*sn;
				errY = errX*sn + errY*cs;
				errX = temp;

				// calculate observation density 
				temp = OBS_DENSITY_AMBIENT
					+ normX * exp( -errX*errX * invSigXX2 )
					* normY * exp( -errY*errY * invSigYY2 ); 
				obsDensity[p] += lWeight[q] * temp;
				obsDensity2[q] += pfWeight[p] * temp;
			}
		}

		// submit our particle filter update
		this->ds.reset();
		this->ds.packUUID( &this->sensorAvatar[ pr->sensor ] );
		this->ds.packData( &pr->time, sizeof(_timeb) );
		this->ds.packData( obsDensity, sizeof(float)*pr->pfNum );
		this->sendMessage( this->hostCon, MSG_DDB_APPLYPFCORRECTION, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		
		// submit landmark particle filter update
		this->ds.reset();
		this->ds.packUUID( &landmark->info.owner );
		this->ds.packData( &pr->time, sizeof(_timeb) );
		this->ds.packData( obsDensity2, sizeof(float)*landmark->pfNum );
		this->sendMessage( this->hostCon, MSG_DDB_APPLYPFCORRECTION, this->ds.stream(), this->ds.length() );
		this->ds.unlock();

		// free pf data
		freeDynamicBuffer( landmark->pfStateRef );
		freeDynamicBuffer( landmark->pfWeightRef );
	}

	// finished landmark
	pr->landmarks->erase( landmark );

	if ( pr->landmarks->empty() ) { // we're done with this reading
		this->finishReading( pr, 1 );
		return -1; // reading finished
	}

	return 0;
}


int AgentSensorLandmark::getLandmarkInfo( int readingId, unsigned char landmarkId ) {
	UUID thread;

	// request info
	thread = this->conversationInitiate( AgentSensorLandmark_CBR_convRequestLandmarkInfo, DDB_REQUEST_TIMEOUT, &readingId, sizeof(int) );
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUChar( landmarkId );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RLANDMARKBYID, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::getLandmarkInfo: request landmark info %d", landmarkId );
	
	return 0;
}

int AgentSensorLandmark::getLandmarkPFId( PROCESS_READING *pr, unsigned char landmarkId, UUID *ownerId ) {
	UUID thread;

	// request pf id
	this->ds.reset();
	this->ds.packInt32( pr->id );
	this->ds.packUChar( landmarkId );
	thread = this->conversationInitiate( AgentSensorLandmark_CBR_convRequestPFId, DDB_REQUEST_TIMEOUT, this->ds.stream(), this->ds.length() );
	this->ds.unlock();
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID( ownerId );
	this->ds.packInt32( DDBAVATARINFO_RPF );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_AVATARGETINFO, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::getLandmarkPFId: request landmark pf id %d", ownerId );
		
	return 0;
}


int AgentSensorLandmark::getLandmarkPF( PROCESS_READING *pr, unsigned char landmarkId, UUID *pfId ) {
	UUID thread;

	// request pf
	this->ds.reset();
	this->ds.packInt32( pr->id );
	this->ds.packUChar( landmarkId );
	thread = this->conversationInitiate( AgentSensorLandmark_CBR_convRequestLandmarkPF, DDB_REQUEST_TIMEOUT, this->ds.stream(), this->ds.length() );
	this->ds.unlock();
	if ( thread == nilUUID ) {
		return 1;
	}
	this->ds.reset();
	this->ds.packUUID( pfId );
	this->ds.packInt32( DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT );
	this->ds.packData( &pr->time, sizeof(_timeb) );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::getLandmarkPF: request landmark pf info %d", landmarkId );
		
	return 0;
}

//-----------------------------------------------------------------------------
// Process message

int AgentSensorLandmark::conProcessMessage( spConnection con, unsigned char message, char *data, unsigned int len ) {
	UUID uuid;

	if ( !AgentBase::conProcessMessage( con, message, data, len ) ) // message handled
		return 0;

	switch ( message ) {
	case AgentSensorProcessing_MSGS::MSG_CONFIGURE:
		this->ds.setData( data, len );
		this->configureParameters( &this->ds );
		this->ds.unlock();
		break;	
	case AgentSensorProcessing_MSGS::MSG_ADD_READING:
		{
			_timeb tb;
			this->ds.setData( data, len );
			this->ds.unpackUUID( &uuid );
			tb = *(_timeb *)this->ds.unpackData( sizeof(_timeb) );
			this->ds.unlock();
			this->addReading( &uuid, &tb );
		}
		break;
	default:
		return 1; // unhandled message
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Callbacks

bool AgentSensorLandmark::convRequestSensorInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	PROCESS_READING *pr;
	int infoFlags;
	UUID avatar;
	int poseSize;
	void *pose;
	UUID thread;
	mapReadings::iterator it;

	it = this->readings.find( *(int*)conv->data );

	if ( it == this->readings.end() ) {
		Log.log( 0, "AgentSensorLandmark::convRequestSensorInfo: reading does not exist %d", *(int*)conv->data );
		
		return 0; // end conversation
	}

	pr = &it->second;

	Log.log( LOG_LEVEL_NORMAL, "AgentSensorLandmark::convRequestSensorInfo: recieved data (reading %d)", pr->id );

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorLandmark::convRequestSensorInfo: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	if ( this->ds.unpackChar() == DDBR_OK ) { // succeeded
  
		// handle info
		infoFlags = this->ds.unpackInt32();
		if ( infoFlags != (DDBSENSORINFO_PF | DDBSENSORINFO_POSE) ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0; // what happened?
		}

		this->ds.unpackUUID( &avatar );
		this->sensorAvatar[ pr->sensor ] = avatar;

		poseSize = this->ds.unpackInt32();
		pose = malloc( poseSize );
		if ( !pose ) { // malloc failed
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0;
		}
		memcpy( pose, this->ds.unpackData( poseSize ), poseSize );
		this->ds.unlock();
	
		this->addPose( &pr->sensor, (CameraPose *)pose );

		free( pose );
		
		this->backup(); // sensorPose, sensorAvatar

		pr->waiting &= ~WAITING_ON_POSE;

		// request pf
		thread = this->conversationInitiate( AgentSensorLandmark_CBR_convRequestPFInfo, DDB_REQUEST_TIMEOUT, &pr->id, sizeof(int) );
		if ( thread == nilUUID ) {
			this->finishReading( pr, 0 );
			return 0;
		}
		this->ds.reset();
		this->ds.packUUID( &avatar );
		this->ds.packInt32( DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT );
		this->ds.packData( &pr->time, sizeof(_timeb) );
		this->ds.packUUID( &thread );
		this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
		this->ds.unlock();
		Log.log( LOG_LEVEL_NORMAL, "AgentSensorLandmark::convRequestSensorInfo: request pf data (reading %d)", pr->id );

	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}

bool AgentSensorLandmark::convRequestPFInfo( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	PROCESS_READING *pr;
	int infoFlags;
	char response;

	mapReadings::iterator iter = this->readings.find( *(int*)conv->data );
	if ( iter == this->readings.end() ) {
		Log.log( 0, "AgentSensorLandmark::convRequestPFInfo: reading not found (timed out == %d)", conv->response == NULL ? 1 : 0 );
		return 0;
	}

	pr = &iter->second;

	Log.log( 0, "AgentSensorLandmark::convRequestPFInfo: recieved data (reading %d)", pr->id );

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorLandmark::convRequestPFInfo: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
  
		// handle info
		infoFlags = this->ds.unpackInt32();
		if ( infoFlags != (DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT) ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0; // what happened?
		}

		pr->pfNum = this->ds.unpackInt32();
		pr->pfStateSize = this->ds.unpackInt32();
		pr->pfStateRef = newDynamicBuffer( pr->pfNum*pr->pfStateSize*sizeof(float) );
		if ( pr->pfStateRef == nilUUID ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0;
		}
		pr->pfWeightRef = newDynamicBuffer( pr->pfNum*sizeof(float) );
		if ( pr->pfWeightRef == nilUUID ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0;
		}

		memcpy( getDynamicBuffer( pr->pfStateRef ), this->ds.unpackData( pr->pfNum*pr->pfStateSize*sizeof(float) ), pr->pfNum*pr->pfStateSize*sizeof(float) );
		memcpy( getDynamicBuffer( pr->pfWeightRef ), this->ds.unpackData( pr->pfNum*sizeof(float) ), pr->pfNum*sizeof(float) );
		this->ds.unlock();

		pr->waiting &= ~WAITING_ON_PF;

		// see if we're ready to process
		if ( !pr->waiting )
			this->processReading( pr );

	} else if ( response == DDBR_TOOLATE )  {
		this->ds.unlock();

		// wait 250 ms and try again
		this->addTimeout( REPEAT_RPFINFO_PERIOD, AgentSensorLandmark_CBR_cbRepeatRPFInfo, &pr->id, sizeof(int) );
	} else if ( response == DDBR_TOOEARLY ) {
		this->ds.unlock();
		this->finishReading( pr, -1 ); // permenant failure
	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}


bool AgentSensorLandmark::cbRepeatRPFInfo( void *vpid ) {
	UUID thread;
	PROCESS_READING *pr;
	mapReadings::iterator it;

	it = this->readings.find( *(int*)vpid );

	if ( it == this->readings.end() ) {
		Log.log( 0, "AgentSensorLandmark::cbRepeatRPFInfo: reading does not exist %d", *(int*)vpid );
		
		return 0; // end conversation
	}

	pr = &it->second;

	
	// request PF
	thread = this->conversationInitiate( AgentSensorLandmark_CBR_convRequestPFInfo, DDB_REQUEST_TIMEOUT, &pr->id, sizeof(int) );
	if ( thread == nilUUID ) {
		this->finishReading( pr, 0 );
		return 0;
	}
	this->ds.reset();
	this->ds.packUUID( &this->sensorAvatar[ pr->sensor ] );
	this->ds.packInt32( DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT );
	this->ds.packData( &pr->time, sizeof(_timeb) );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	Log.log( LOG_LEVEL_NORMAL, "AgentSensorLandmark::cbRepeatRPFInfo: request pf data (reading %d)", pr->id );

	return 0;
}

bool AgentSensorLandmark::convRequestData( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	PROCESS_READING *pr;
	char response;
	mapReadings::iterator it;

	it = this->readings.find( *(int*)conv->data );

	if ( it == this->readings.end() ) {
		Log.log( 0, "AgentSensorLandmark::convRequestData: reading does not exist %d", *(int*)conv->data );
		
		return 0; // end conversation
	}

	pr = &it->second;

	Log.log( 0, "AgentSensorLandmark::convRequestData: recieved data (reading %d)", pr->id );

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorLandmark::convRequestData: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
			
		pr->readingSize = this->ds.unpackInt32();
		pr->readingRef = newDynamicBuffer( pr->readingSize );
		if ( pr->readingRef == nilUUID ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0;
		}
		memcpy( getDynamicBuffer( pr->readingRef ), this->ds.unpackData( pr->readingSize ), pr->readingSize );

		pr->dataSize = this->ds.unpackInt32();
		pr->dataRef = newDynamicBuffer( pr->dataSize );
		if ( pr->dataRef == nilUUID ) {
			this->ds.unlock();
			freeDynamicBuffer( pr->readingRef );
			this->finishReading( pr, 0 );
			return 0;
		}
		memcpy( getDynamicBuffer( pr->dataRef ), this->ds.unpackData( pr->dataSize ), pr->dataSize );
		this->ds.unlock();

		pr->waiting &= ~WAITING_ON_DATA;

		// see if we're ready to process
		if ( !pr->waiting )
			this->processReading( pr );

	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}


bool AgentSensorLandmark::convRequestLandmarkInfo( void *vpConv ) {
	DataStream lds;
	RPC_STATUS Status;
	spConversation conv = (spConversation)vpConv;
	PROCESS_READING *pr;
	char response;
	mapReadings::iterator it;

	it = this->readings.find( *(int*)conv->data );

	if ( it == this->readings.end() ) {
		Log.log( 0, "AgentSensorLandmark::convRequestLandmarkInfo: reading does not exist %d", *(int*)conv->data );
		
		return 0; // end conversation
	}

	pr = &it->second;

	Log.log( 0, "AgentSensorLandmark::convRequestLandmarkInfo: recieved data (reading %d)", pr->id );

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorLandmark::convRequestLandmarkInfo: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	lds.setData( conv->response, conv->responseLen );
	lds.unpackData(sizeof(UUID)); // discard thread
	
	response = lds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded
		DDBLandmark landmark;

		landmark = *(DDBLandmark *)lds.unpackData( sizeof(DDBLandmark) );
		lds.unlock();

		bool newLandmark = false;
		if ( this->landmark.find(landmark.code) == this->landmark.end() )
			newLandmark = true;

		this->landmark[landmark.code] = landmark;
		
		if ( newLandmark )
			this->backup(); // landmark

		// find the landmark instance
		std::list<LANDMARK_HIT>::iterator iterL = pr->landmarks->begin();
		while ( iterL != pr->landmarks->end() ) {
			if ( iterL->id == landmark.code )
				break;
			iterL++;
		}
		if ( iterL == pr->landmarks->end() ) {
			Log.log( 0, "AgentSensorLandmark::convRequestLandmarkInfo: landmark not found" );
			return 0;
		}

		iterL->info = this->landmark[landmark.code];
		
		if ( UuidIsNil( &landmark.owner, &Status ) ) { // stationary
			iterL->waiting = 0;
			this->processLandmark( pr, iterL );
		} else { // we need to get the pf
			std::map<UUID,UUID,UUIDless>::iterator iPF = this->avatarPF.find( landmark.owner );
			if ( iPF == this->avatarPF.end() ) { // need to find out pf id
				iterL->waiting = WAITING_ON_PF_ID;
				this->getLandmarkPFId( pr, landmark.code, &landmark.owner );
			} else { // know the pf id
				iterL->waiting = WAITING_ON_PF;
				this->getLandmarkPF( pr, landmark.code, &iPF->second );
			}
		}

	} else {
		lds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}

bool AgentSensorLandmark::convRequestPFId( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	int prId;
	unsigned char landmarkId;
	PROCESS_READING *pr;
	LANDMARK_HIT *landmark;
	int infoFlags;
	char response;
	UUID pfId;

	this->ds.setData( (char *)conv->data, conv->dataLen );
	prId = this->ds.unpackInt32();
	landmarkId = this->ds.unpackUChar();
	this->ds.unlock();

	mapReadings::iterator iter = this->readings.find( prId );
	if ( iter == this->readings.end() ) {
		Log.log( 0, "AgentSensorLandmark::convRequestPFId: reading %d not found (timed out == %d)", prId, conv->response == NULL ? 1 : 0 );
		return 0;
	}

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::convRequestPFId: data recieved (reading %d)", prId );
		
	pr = &iter->second;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorLandmark::convRequestPFId: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded

		// find landmark instance
		std::list<LANDMARK_HIT>::iterator iterL = pr->landmarks->begin();
		while ( iterL != pr->landmarks->end() ) {
			if ( iterL->id == landmarkId )
				break;
			iterL++;
		}
		if ( iterL == pr->landmarks->end() ) {
			this->ds.unlock();
			Log.log( 0, "AgentSensorLandmark::convRequestLandmarkPF: landmark not found" );
			return 0;
		}
		landmark = &(*iterL);
  
		// handle info
		infoFlags = this->ds.unpackInt32();
		if ( infoFlags != (DDBAVATARINFO_RPF) ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0; // what happened?
		}

		this->ds.unpackUUID( &pfId );
		this->ds.unlock();

		this->avatarPF[landmark->info.owner] = pfId;

		// get pf state
		landmark->waiting = WAITING_ON_PF;
		this->getLandmarkPF( pr, landmark->info.code, &pfId );

	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}

bool AgentSensorLandmark::convRequestLandmarkPF( void *vpConv ) {
	spConversation conv = (spConversation)vpConv;
	int prId;
	unsigned char landmarkId;
	PROCESS_READING *pr;
	LANDMARK_HIT *landmark;
	int infoFlags;
	char response;

	this->ds.setData( (char *)conv->data, conv->dataLen );
	prId = this->ds.unpackInt32();
	landmarkId = this->ds.unpackUChar();
	this->ds.unlock();

	mapReadings::iterator iter = this->readings.find( prId );
	if ( iter == this->readings.end() ) {
		Log.log( 0, "AgentSensorLandmark::convRequestLandmarkPF: reading %d not found (timed out == %d)", prId, conv->response == NULL ? 1 : 0 );
		return 0;
	}

	Log.log( LOG_LEVEL_VERBOSE, "AgentSensorLandmark::convRequestLandmarkPF: data recieved (reading %d)", prId );
		
	pr = &iter->second;

	if ( conv->response == NULL ) {
		Log.log( 0, "AgentSensorLandmark::convRequestLandmarkPF: request timed out" );
		
		this->finishReading( pr, 0 );
		return 0; // end conversation
	}

	this->ds.setData( conv->response, conv->responseLen );
	this->ds.unpackData(sizeof(UUID)); // discard thread
	
	response = this->ds.unpackChar();
	if ( response == DDBR_OK ) { // succeeded

		// find landmark instance
		std::list<LANDMARK_HIT>::iterator iterL = pr->landmarks->begin();
		while ( iterL != pr->landmarks->end() ) {
			if ( iterL->id == landmarkId )
				break;
			iterL++;
		}
		if ( iterL == pr->landmarks->end() ) {
			this->ds.unlock();
			Log.log( 0, "AgentSensorLandmark::convRequestLandmarkPF: landmark not found" );
			return 0;
		}
		landmark = &(*iterL);
  
		// handle info
		infoFlags = this->ds.unpackInt32();
		if ( infoFlags != (DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT) ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0; // what happened?
		}

		landmark->pfNum = this->ds.unpackInt32();
		landmark->pfStateSize = this->ds.unpackInt32();
		landmark->pfStateRef = newDynamicBuffer( landmark->pfNum*landmark->pfStateSize*sizeof(float) );
		if ( landmark->pfStateRef == nilUUID ) {
			this->ds.unlock();
			this->finishReading( pr, 0 );
			return 0;
		}
		landmark->pfWeightRef = newDynamicBuffer( landmark->pfNum*sizeof(float) );
		if ( landmark->pfWeightRef == nilUUID ) {
			freeDynamicBuffer( landmark->pfStateRef );
			this->finishReading( pr, 0 );
			return 0;
		}

		memcpy( getDynamicBuffer( landmark->pfStateRef ), this->ds.unpackData( landmark->pfNum*landmark->pfStateSize*sizeof(float) ), landmark->pfNum*landmark->pfStateSize*sizeof(float) );
		memcpy( getDynamicBuffer( landmark->pfWeightRef ), this->ds.unpackData( landmark->pfNum*sizeof(float) ), landmark->pfNum*sizeof(float) );
		this->ds.unlock();

		landmark->waiting = 0;

		// process landmark
		this->processLandmark( pr, iterL );

	} else if ( response == DDBR_TOOLATE )  {
		this->ds.unlock();

		// wait 250 ms and try again
		this->addTimeout( REPEAT_RPFINFO_PERIOD, AgentSensorLandmark_CBR_cbRepeatRLandmarkPF, conv->data, conv->dataLen );
	} else {
		this->ds.unlock();
		this->finishReading( pr, 0 );
	}

	return 0;
}

bool AgentSensorLandmark::cbRepeatRLandmarkPF( void *data ) {
	UUID thread;	
	int prId;
	char landmarkId;
	PROCESS_READING *pr;
	DDBLandmark *landmark;

	this->ds.setData( (char *)data, sizeof(int) + sizeof(char) );
	prId = this->ds.unpackInt32();
	landmarkId = this->ds.unpackChar();
	this->ds.unlock();

	mapReadings::iterator iter = this->readings.find( prId );
	if ( iter == this->readings.end() ) {
		Log.log( 0, "AgentSensorLandmark::cbRepeatRLandmarkPF: reading not found" );
		return 0;
	}
	
	pr = &iter->second;

	mapLandmark::iterator iterL = this->landmark.find( landmarkId );
	if ( iterL == this->landmark.end() ) {
		Log.log( 0, "AgentSensorLandmark::cbRepeatRLandmarkPF: landmark not found" );
		return 0;
	}

	landmark = &iterL->second;

	// request PF
	thread = this->conversationInitiate( AgentSensorLandmark_CBR_convRequestLandmarkPF, DDB_REQUEST_TIMEOUT, this->ds.stream(), this->ds.length() );
	if ( thread == nilUUID ) {
		this->finishReading( pr, 0 );
		return 0;
	}
	this->ds.reset();
	this->ds.packUUID( &landmark->owner );
	this->ds.packInt32( DDBPFINFO_NUM_PARTICLES | DDBPFINFO_STATE_SIZE | DDBPFINFO_STATE | DDBPFINFO_WEIGHT );
	this->ds.packData( &pr->time, sizeof(_timeb) );
	this->ds.packUUID( &thread );
	this->sendMessage( this->hostCon, MSG_DDB_RPFINFO, this->ds.stream(), this->ds.length() );
	this->ds.unlock();

	return 0;
}


//-----------------------------------------------------------------------------
// State functions

int AgentSensorLandmark::freeze( UUID *ticket ) {

	return AgentBase::freeze( ticket );
}

int AgentSensorLandmark::thaw( DataStream *ds, bool resumeReady ) {

	return AgentBase::thaw( ds, resumeReady );
}

int	AgentSensorLandmark::writeState( DataStream *ds, bool top ) {

	if ( top ) _WRITE_STATE(AgentSensorLandmark);

	_WRITE_STATE_MAP_LESS( UUID, CameraPose, UUIDless, &this->sensorPose );
	_WRITE_STATE_MAP_LESS( UUID, UUID, UUIDless, &this->sensorAvatar );
	_WRITE_STATE_MAP( char, DDBLandmark, &this->landmark );
	_WRITE_STATE_MAP_LESS( UUID, UUID, UUIDless, &this->avatarPF );

	// readings
	std::map<int,PROCESS_READING>::iterator iR;
	for ( iR = this->readings.begin(); iR != this->readings.end(); iR++ ) {
		ds->packBool( 1 );
		ds->packInt32( iR->first );
		ds->packData( (void *)&iR->second, sizeof(PROCESS_READING) );
		_WRITE_STATE_LIST( LANDMARK_HIT, iR->second.landmarks ); // pack landmark list
	}
	ds->packBool( 0 );


	return AgentBase::writeState( ds, false );;
}

int	AgentSensorLandmark::readState( DataStream *ds, bool top ) {

	if ( top ) _READ_STATE(AgentSensorLandmark);

	_READ_STATE_MAP( UUID, CameraPose, &this->sensorPose );
	_READ_STATE_MAP( UUID, UUID, &this->sensorAvatar );
	_READ_STATE_MAP( char, DDBLandmark, &this->landmark );
	_READ_STATE_MAP( UUID, UUID, &this->avatarPF );

	// readings
	int keyR;
	PROCESS_READING varR;
	while ( ds->unpackBool() ) {
		keyR = ds->unpackInt32();
		varR = *(PROCESS_READING *)ds->unpackData( sizeof(PROCESS_READING) );
		// unpack landmarks
		varR.landmarks = new std::list<LANDMARK_HIT>;
		_READ_STATE_LIST( LANDMARK_HIT, varR.landmarks );		
		this->readings[keyR] = varR;
	}

	return AgentBase::readState( ds, false );
}

int AgentSensorLandmark::recoveryFinish() {
	DataStream lds;

	if ( AgentBase::recoveryFinish() ) 
		return 1;

	if ( STATE(AgentBase)->started ) { 
		// redo relevant start items
	
	}

	return 0;
}

int AgentSensorLandmark::writeBackup( DataStream *ds ) {

	ds->packUUID( &STATE(AgentSensorLandmark)->ownerId );

	_WRITE_STATE_MAP_LESS( UUID, CameraPose, UUIDless, &this->sensorPose );
	_WRITE_STATE_MAP_LESS( UUID, UUID, UUIDless, &this->sensorAvatar );
	_WRITE_STATE_MAP( char, DDBLandmark, &this->landmark );

	return AgentBase::writeBackup( ds );
}

int AgentSensorLandmark::readBackup( DataStream *ds ) {

	ds->unpackUUID( &STATE(AgentSensorLandmark)->ownerId );

	_READ_STATE_MAP( UUID, CameraPose, &this->sensorPose );
	_READ_STATE_MAP( UUID, UUID, &this->sensorAvatar );
	_READ_STATE_MAP( char, DDBLandmark, &this->landmark );

	return AgentBase::readBackup( ds );
}


//*****************************************************************************
// Threading

DWORD WINAPI RunThread( LPVOID vpAgent ) {
	AgentSensorLandmark *agent = (AgentSensorLandmark *)vpAgent;

	if ( agent->configure() ) {
		return 1;
	}

	while ( !agent->step() );

	if ( agent->isStarted() )
		agent->stop();
	delete agent;

	return 0;
}

int Spawn( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) {
	AgentSensorLandmark *agent = new AgentSensorLandmark( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
	HANDLE hThread;
	DWORD  dwThreadId;

	hThread = CreateThread( 
        NULL,                   // default security attributes
        0,                      // use default stack size  
        RunThread,				// thread function name
        agent,					// argument to thread function 
        0,                      // use default creation flags 
        &dwThreadId);			// returns the thread identifier 

	if ( hThread == NULL ) {
		delete agent;
		return 1;
	}

	return 0;
}

int Playback( spAddressPort ap, UUID *ticket, int logLevel, char *logDirectory, char playbackMode, char *playbackFile ) {
	AgentSensorLandmark *agent = new AgentSensorLandmark( ap, ticket, logLevel, logDirectory, playbackMode, playbackFile );
	
	if ( agent->configure() ) {
		delete agent;
		return 1;
	}

	while ( !agent->step() );

	if ( agent->isStarted() )
		agent->stop();

	printf_s( "Playback: agent finished\n" );

	delete agent;

	return 0;
}


// CAgentSensorLandmarkDLL: Override ExitInstance for debugging memory leaks
BEGIN_MESSAGE_MAP(CAgentSensorLandmarkDLL, CWinApp)
END_MESSAGE_MAP()

CAgentSensorLandmarkDLL::CAgentSensorLandmarkDLL() {}

// The one and only CAgentSensorLandmarkDLL object
CAgentSensorLandmarkDLL theApp;

int CAgentSensorLandmarkDLL::ExitInstance() {
  TRACE(_T("--- ExitInstance() for regular DLL: CAgentSensorLandmarkDLL ---\n"));
  return CWinApp::ExitInstance();
}