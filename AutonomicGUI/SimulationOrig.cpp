
#include "StdAfx.h"
#include "Simulation.h"

#define _USE_MATH_DEFINES
#include "math.h"

#define fM_PI (float)M_PI
#define fM_PI_2 (float)M_PI_2
#define fM_PI_4 (float)M_PI_4


boost::lagged_fibonacci607 * randEngine_lf607() {
	static int firstRun = 1;
	static boost::lagged_fibonacci607 engine;
	
	if ( firstRun ) {
		_timeb seedt;
		_ftime_s( &seedt );
		engine.seed( (unsigned int) (seedt.time*1000 + seedt.millitm) );
	
		firstRun = 0;
	}

	return &engine;
}

double randNormalDistribution( double mean, double sigma ) {
	static boost::normal_distribution<double> nd(0,1);
	static boost::variate_generator<boost::lagged_fibonacci607&,boost::normal_distribution<double>> gen(*randEngine_lf607(),nd);

	return mean + sigma * gen();
}

double randUniform01() {
	static boost::uniform_int<int> uni_int(0,999999);
	static boost::variate_generator<boost::lagged_fibonacci607&,boost::uniform_int<int>> gen(*randEngine_lf607(),uni_int);

	return gen()*0.000001;
}

Simulation::Simulation() {
	int i;

	densityT = NewImage( TEMPLATE_SIZE, TEMPLATE_SIZE );
	absoluteT = NewImage( TEMPLATE_SIZE, TEMPLATE_SIZE );
	integralTarget = NewImage( TEMPLATE_SIZE, TEMPLATE_SIZE );

	for ( i=0; i<ROTATION_DIVISIONS; i++ ) {
		densityR[i] = NewImage( 2*TEMPLATE_SIZE, 2*TEMPLATE_SIZE );
		absoluteR[i] = NewImage( 2*TEMPLATE_SIZE, 2*TEMPLATE_SIZE );
	}

	mapUpdate = NewImage( 64, 64 );

	particleFilters[0] = NULL;
	particleFilters[1] = NULL;

	return;
}

Simulation::~Simulation() {
	int i;

	FreeImage( densityT );
	FreeImage( absoluteT );
	FreeImage( integralTarget );
	
	for ( i=0; i<ROTATION_DIVISIONS; i++ ) {
		FreeImage( densityR[i] );
		FreeImage( absoluteR[i] );
	}
	
	FreeImage( mapUpdate );

	FreeFilter( particleFilters[0] );
	FreeFilter( particleFilters[1] );
}

void prepareOrder( Robot *r, int which ) {
	r->curOrder = which;
	r->curVal = 0;
}

int Simulation::Initialize() {
	int i, j;
	
	simTime = 0;
	simStep = 10;

	paused = 0;

	// init map
	for ( i=0; i<MAP_SIZE*MAP_DIVISIONS; i++ ) {
		for ( j=0; j<MAP_SIZE*MAP_DIVISIONS; j++ ) {
			map[i][j] = 0.5f;
		}
	}
	mapLoc[0] = -MAP_SIZE/2.0f;
	mapLoc[1] = MAP_SIZE/2.0f;

	// init blocks
	blocks[0].x = -3;
	blocks[0].y = 1;
	blocks[0].width = 2;
	blocks[0].height = 2;
	blocks[1].x = 1;
	blocks[1].y = 2;
	blocks[1].width = 3;
	blocks[1].height = 2;
	blocks[2].x = 2;
	blocks[2].y = 0;
	blocks[2].width = 1;
	blocks[2].height = 1;
	blocks[3].x = 1;
	blocks[3].y = -3;
	blocks[3].width = 1;
	blocks[3].height = 1;
	blocks[4].x = 3;
	blocks[4].y = -5;
	blocks[4].width = 1;
	blocks[4].height = 1;
	blocks[5].x = -3;
	blocks[5].y = -2;
	blocks[5].width = 2;
	blocks[5].height = 1;

	// init robot
	robots[0].x = -4;
	robots[0].y = -3;
	robots[0].r = fM_PI_2;
	robots[0].lastx = -4;
	robots[0].lasty = -3;
	robots[0].lastr = fM_PI_2;
	robots[0].lastPrediction = 0;
	robots[0].nextPrediction = 0;
	robots[0].v = 0;	
	robots[0].nextSonar = 0;
	robots[0].orderType[0] = ORD_MOVE;
	robots[0].orderVal[0] = 3;
	robots[0].orderType[1] = ORD_ROTATE;
	robots[0].orderVal[1] = -fM_PI_2;
	robots[0].orderType[2] = ORD_MOVE;
	robots[0].orderVal[2] = 4;
	robots[0].orderType[3] = ORD_ROTATE;
	robots[0].orderVal[3] = -fM_PI_2;
	robots[0].orderType[4] = ORD_MOVE;
	robots[0].orderVal[4] = 4;
	robots[0].orderType[5] = ORD_ROTATE;
	robots[0].orderVal[5] = fM_PI_2;
	robots[0].orderType[6] = ORD_MOVE;
	robots[0].orderVal[6] = 2.5;
	robots[0].orderType[7] = ORD_ROTATE;
	robots[0].orderVal[7] = fM_PI_2;
	robots[0].orderType[8] = ORD_MOVE;
	robots[0].orderVal[8] = 3;
	robots[0].orderType[9] = ORD_ROTATE;
	robots[0].orderVal[9] = fM_PI_2;
	robots[0].orderType[10] = ORD_MOVE;
	robots[0].orderVal[10] = 1.5;
	robots[0].orderType[11] = ORD_ROTATE;
	robots[0].orderVal[11] = -fM_PI_2;
	robots[0].orderType[12] = ORD_MOVE;
	robots[0].orderVal[12] = 2.5;
	robots[0].orderType[13] = ORD_ROTATE;
	robots[0].orderVal[13] = -fM_PI_2;
	robots[0].orderType[14] = ORD_MOVE;
	robots[0].orderVal[14] = 3;
	robots[0].orderType[15] = ORD_ROTATE;
	robots[0].orderVal[15] = -fM_PI_2;
	robots[0].orderType[16] = ORD_MOVE;
	robots[0].orderVal[16] = 0.5;
	robots[0].orderType[17] = ORD_DONE;
	prepareOrder( &robots[0], 0 );

	robots[1].x = 2.5;
	robots[1].y = -5;
	robots[1].r = fM_PI_2;
	robots[1].lastx = 2.5;
	robots[1].lasty = -5;
	robots[1].lastr = fM_PI_2;
	robots[1].lastPrediction = 0;
	robots[1].nextPrediction = 0;
	robots[1].v = 0;
	robots[1].nextSonar = 0;
	robots[1].orderType[0] = ORD_MOVE;
	robots[1].orderVal[0] = 4;
	robots[1].orderType[1] = ORD_ROTATE;
	robots[1].orderVal[1] = -fM_PI_2;
	robots[1].orderType[2] = ORD_MOVE;
	robots[1].orderVal[2] = 1.5;
	robots[1].orderType[3] = ORD_ROTATE;
	robots[1].orderVal[3] = fM_PI_2;
	robots[1].orderType[4] = ORD_MOVE;
	robots[1].orderVal[4] = 2.5;
	robots[1].orderType[5] = ORD_ROTATE;
	robots[1].orderVal[5] = fM_PI_2;
	robots[1].orderType[6] = ORD_MOVE;
	robots[1].orderVal[6] = 4;
	robots[1].orderType[7] = ORD_WAIT;
	robots[1].orderVal[7] = 1;
	robots[1].orderType[8] = ORD_ROTATE;
	robots[1].orderVal[8] = -fM_PI_2;
	robots[1].orderType[9] = ORD_MOVE;
	robots[1].orderVal[9] = 2.5;
	robots[1].orderType[10] = ORD_ROTATE;
	robots[1].orderVal[10] = fM_PI_2;
	robots[1].orderType[11] = ORD_MOVE;
	robots[1].orderVal[11] = 4;
	robots[1].orderType[12] = ORD_ROTATE;
	robots[1].orderVal[12] = fM_PI_2;
	robots[1].orderType[13] = ORD_MOVE;
	robots[1].orderVal[13] = 7;
	robots[1].orderType[14] = ORD_DONE;
	prepareOrder( &robots[1], 0 );

	// init filter 
	_timeb tb;
	tb.time = 0;
	tb.millitm = 0;
	particleFilters[0] = CreateFilter( NUM_PARTICLES, &tb, generateParticleState_Avatar, (void *)&robots[0] );
	particleFilters[1] = CreateFilter( NUM_PARTICLES, &tb, generateParticleState_Avatar, (void *)&robots[1] );

	// init sonar history
	sonarHistoryWrite = 0;
	sonarHistoryCount = 0;

	return 0;
}


int Simulation::Step() {
	int i;
	float dt = simStep*0.001f;
	_timeb tb;

	if ( paused )
		return 0;

	simTime += simStep;
	tb.time = simTime/1000;
	tb.millitm = simTime % 1000;

	for ( i=0; i<2; i++ ) {
		float targetVal = robots[i].orderVal[robots[i].curOrder];
		switch ( robots[i].orderType[robots[i].curOrder] ) {
		case ORD_MOVE:
			//break; // TEMP
			{
				float decelT = robots[i].v/ACCEL;
				float decelD = 0.5f*ACCEL*decelT*decelT;
				if ( decelD >= targetVal - robots[i].curVal ) { // decelerate
					robots[i].v -= ACCEL*dt;
				} else if ( robots[i].v < VELMAX ) {		    // accelerate
					robots[i].v += ACCEL*dt;
				}
				// move
				robots[i].x += cos( robots[i].r )*robots[i].v*dt;
				robots[i].y += sin( robots[i].r )*robots[i].v*dt;
				robots[i].curVal += robots[i].v*dt;

				// check if we're done
				if ( targetVal <= robots[i].curVal ) {
					prepareOrder( &robots[i], robots[i].curOrder + 1 );
				}
			}
			break;
		case ORD_ROTATE:
			{ 
				char dir;
				// choose direction
				if ( targetVal > 0 ) {
					dir = 1;
				} else {
					dir = -1;
				}

				// rotate
				robots[i].r += dir*VELROTATE*dt;
				robots[i].curVal += dir*VELROTATE*dt;

				// check if we're done
				if ( fabs( targetVal ) <= fabs( robots[i].curVal*dir ) ) {
					prepareOrder( &robots[i], robots[i].curOrder + 1 );
				}
			}
			break;
		case ORD_WAIT:
			// increment time
			robots[i].curVal += dt;

			// check if we're done
			if ( targetVal <= robots[i].curVal ) {
				prepareOrder( &robots[i], robots[i].curOrder + 1 );
			}
			break;
		case ORD_DONE:
		default:
			// do nothing
			break;
		}

		if ( robots[i].orderType[robots[i].curOrder] != ORD_DONE ) {
			float d;
			if ( simTime >= robots[i].nextPrediction ) {
				float sn, cs;
				float forwardD, tangentialD, rotationalD;
				int dtime = simTime - robots[i].lastPrediction;

				sn = sin( robots[i].lastr );
				cs = cos( robots[i].lastr );
				forwardD = (robots[i].x - robots[i].lastx)*cs + (robots[i].y - robots[i].lasty)*sn 
					+ (float)randNormalDistribution(0,AVATAR_LINEARV_SIG)*dtime;
				tangentialD = (robots[i].x - robots[i].lastx)*-sn + (robots[i].y - robots[i].lasty)*cs 
					+ (float)randNormalDistribution(0,AVATAR_LINEARV_SIG)*dtime;
				rotationalD = robots[i].r - robots[i].lastr 
					+ (float)randNormalDistribution(0,AVATAR_ROTATIONALV_SIG)*dtime;

				predictPF_Avatar( particleFilters[i], dtime, forwardD/dtime, tangentialD/dtime, rotationalD/dtime );

				robots[i].lastx = robots[i].x;
				robots[i].lasty = robots[i].y;
				robots[i].lastr = robots[i].r;
				robots[i].lastPrediction = simTime;
				robots[i].nextPrediction += PREDICTION_PERIOD;
			}
			if ( simTime >= robots[i].nextSonar ) {
				d = takeSonar( robots[i].x, robots[i].y, robots[i].r )
					+ (float)randNormalDistribution(0,SONAR_SIG);
				correctPF_AvatarConicSensor( particleFilters[i], d, 0, &tb );
				d = takeSonar( robots[i].x, robots[i].y, robots[i].r + fM_PI_4 )
					+ (float)randNormalDistribution(0,SONAR_SIG);
				correctPF_AvatarConicSensor( particleFilters[i], d, fM_PI_4, &tb );
				d = takeSonar( robots[i].x, robots[i].y, robots[i].r - fM_PI_4 )
					+ (float)randNormalDistribution(0,SONAR_SIG);
				correctPF_AvatarConicSensor( particleFilters[i], d, -fM_PI_4, &tb );
				robots[i].nextSonar += SONAR_PERIOD;
			}
		}
	}

	return 0;
}

int drawImage( FIMAGE *img, float x_offset, float y_offset, float img_offset ) {
	int i, j;
	float c;
	float s = 1.0f/MAP_DIVISIONS;

	glPolygonMode(GL_FRONT, GL_FILL); // Filled Mode

	for ( i=0; i<img->rows; i++ ) {
		for ( j=0; j<img->cols; j++ ) {
			c = Px(img,i,j) + img_offset;
			glColor3f( c, c, c );
			glBegin(GL_QUADS);
				glVertex3f( j*s + x_offset, -i*s + y_offset, 0 );
				glVertex3f( (j+1)*s + x_offset, -i*s + y_offset, 0 );
				glVertex3f( (j+1)*s + x_offset, (-i-1)*s + y_offset, 0 );
				glVertex3f( j*s + x_offset, (-i-1)*s + y_offset, 0 );
			glEnd();
		}
	}

	return 0;
}

int Simulation::PreDraw() {
	int i, j;
	float c;
	float s;

	// draw map
	s = 1.0f/MAP_DIVISIONS;
	glPolygonMode(GL_FRONT, GL_FILL); // Filled Mode
	glColor3f( 0.5, 0.5, 0.5 );
	for ( i=0; i<MAP_SIZE*MAP_DIVISIONS; i++ ) {
		for ( j=0; j<MAP_SIZE*MAP_DIVISIONS; j++ ) {
			c = map[i][j];
			glColor3f( c, c, c );
			glBegin(GL_QUADS);
				glVertex3f( i*s + mapLoc[0], -j*s + mapLoc[1], 0 );
				glVertex3f( (i+1)*s + mapLoc[0], -j*s + mapLoc[1], 0 );
				glVertex3f( (i+1)*s + mapLoc[0], (-j-1)*s + mapLoc[1], 0 );
				glVertex3f( i*s + mapLoc[0], (-j-1)*s + mapLoc[1], 0 );
			glEnd();
		}
	}

	//drawImage( densityT, 0, 0, 0.5f );
	//drawImage( absoluteT, 0, 2*TEMPLATE_SIZE*0.1f, 0.5f );

	//drawImage( densityR[1], TEMPLATE_SIZE*0.1f, 0, 0.5f );
	//drawImage( absoluteR[1], TEMPLATE_SIZE*0.1f, 2*TEMPLATE_SIZE*0.1f, 0.5f );

	//drawImage( mapUpdate, mapUpdateLoc[0], mapUpdateLoc[1], -0.1f );
	
	return 0;
}

int Simulation::Draw() {
	float s, c, scale;
	int i, j;

	glPolygonMode(GL_FRONT, GL_LINE); // Wireframe Mode

	// draw blocks
	
	glColor3f( 1, 0, 0 );
	glLineWidth( 2 );
	for ( i=0; i<6; i++ ) {
		glBegin(GL_QUADS);
			glVertex3f( blocks[i].x, blocks[i].y, 0 );
			glVertex3f( blocks[i].x+blocks[i].width, blocks[i].y, 0 );
			glVertex3f( blocks[i].x+blocks[i].width, blocks[i].y+blocks[i].height, 0 );
			glVertex3f( blocks[i].x, blocks[i].y+blocks[i].height, 0 );
		glEnd();
	}
	glLineWidth( 1 );

	// draw robots
	glColor3f( 0, 1, 0 );
	glLineWidth( 2 );
	for ( i=0; i<2; i++ ) {
		s = sin( robots[i].r );
		c = cos( robots[i].r );
		glBegin( GL_TRIANGLES );
			glVertex3f( robots[i].x + c*-0.2f - s*0.2f, robots[i].y + s*-0.2f + c*0.2f, 0 );
			glVertex3f( robots[i].x + c*-0.2f - s*-0.2f, robots[i].y + s*-0.2f + c*-0.2f, 0 ); 
			glVertex3f( robots[i].x + c*0.3f - s*0.0f, robots[i].y + s*0.3f + c*0.0f, 0 );
		glEnd();
	}
	glLineWidth( 1 );

	// draw sonar history
	glColor3f( 0, 0, 1 );
	glLineWidth( 1 );
	for ( i=0; i<sonarHistoryCount; i++ ) {
		glBegin(GL_LINE_STRIP);
			for ( j=0; j<6; j++ ) {
				glVertex3f( sonarHistory[i].x[j], sonarHistory[i].y[j], 0 );
			}
			glVertex3f( sonarHistory[i].x[0], sonarHistory[i].y[0], 0 );
		glEnd();
	}

	// draw particle filters
	glColor3f( 1, 0, 1 );
	for ( i=0; i<2; i++ ) {
		std::list<Particle*>::iterator p = particleFilters[i]->regions->front()->particles->begin();
		std::list<void*>::iterator pstate = particleFilters[i]->regions->front()->states->front()->begin();
		while ( p != particleFilters[i]->regions->front()->particles->end() ) {
			ParticleState_Avatar *state = (ParticleState_Avatar *)*pstate;
			scale = (*p)->weight * NUM_PARTICLES;
			p++;
			pstate++;

			s = sin( state->r );
			c = cos( state->r );
			glBegin( GL_LINE_STRIP );
				glVertex3f( state->x + c*0.3f*scale, state->y + s*0.3f*scale, 0 );
				glVertex3f( state->x, state->y, 0 );
				glVertex3f( state->x - s*-0.1f*scale, state->y + c*-0.1f*scale, 0 );
			glEnd();
		}
	}


	return 0;
}

int Simulation::generateConicTemplates( float d, float max, float sig, float a, float b, FIMAGE *densityT, FIMAGE *absoluteT, float *scale ) {
	int i, j;
	float r, t;
	float coverage;
	float var = sig*sig;
	float norm = ( 1/(var*sqrt(2*fM_PI)) );
	float expon;
	bool skipDensity;

	*scale = 0.7071f * (d + 3*var)/TEMPLATE_SIZE_DIV2;
	
	int cols = (int)ceil(0.7071f * TEMPLATE_SIZE);
	densityT->cols = cols;
	absoluteT->cols = cols;

	if ( d < max ) {
		for ( i=0; i<TEMPLATE_SIZE_DIV2; i++ ) {
			skipDensity = false;
			for ( j=cols-1; j>=0; j-- ) {
				t = fabs( atan( (i+0.5f)/(j+0.5f) ) );
				if ( a < t ) {
					while ( j >= 0 ) { // early exit
						Px(densityT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(densityT,TEMPLATE_SIZE_DIV2+i,j) = 0;
						
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = 0;
						j--;
					}
				} else { 
					r = *scale * sqrt( (i+0.5f)*(i+0.5f) + (j+0.5f)*(j+0.5f) );
					coverage = ( t < b ? 1 : (a-t)/(a-b) );
					
					if ( skipDensity ) {
						Px(densityT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(densityT,TEMPLATE_SIZE_DIV2+i,j) = 0;
					} else {
						expon = exp( -(d-r)*(d-r)/(2*var*var) );
						if ( expon < 1E-15 ) {
							Px(densityT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
							Px(densityT,TEMPLATE_SIZE_DIV2+i,j) = 0;
							if ( r < d ) // early exit
								skipDensity = true;
						} else {
							Px(densityT,TEMPLATE_SIZE_DIV2-1-i,j) 
								= Px(densityT,TEMPLATE_SIZE_DIV2+i,j) 
								= - *scale * coverage * 0.45f 
								* ( 1/(2*r*a) ) 
								* norm * expon;
						}
					}

					if ( r > d ) {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = 0;	
					} else if ( r > d - 3*var ) {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) 
							= Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j)
							= coverage * (d - r)/(3*var);	
					} else {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = coverage;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = coverage;
					}
				}
			}
		}
	} else { // no density
		for ( i=0; i<TEMPLATE_SIZE_DIV2; i++ ) {
			for ( j=cols-1; j>=0; j-- ) {
				t = fabs( atan( (i+0.5f)/(j+0.5f) ) );
				if ( a < t ) {
					while ( j >= 0 ) { // early exit
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = 0;
						j--;
					}
				} else { 
					r = *scale * sqrt( (i+0.5f)*(i+0.5f) + (j+0.5f)*(j+0.5f) );
					coverage = ( t < b ? 1 : (a-t)/(a-b) );

					if ( r > d ) {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = 0;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = 0;	
					} else if ( r > d - 3*var ) {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) 
							= Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j)
							= coverage * (d - r)/(3*var);	
					} else {
						Px(absoluteT,TEMPLATE_SIZE_DIV2-1-i,j) = coverage;
						Px(absoluteT,TEMPLATE_SIZE_DIV2+i,j) = coverage;
					}
				}
			}
		}
	}

	return 0;
}

float Simulation::takeSonar( float x, float y, float r ) {
	float d = SONAR_RANGE_MAX;
	float ra = r + SONAR_A, rb = r - SONAR_A; // rotation of edges
	float sa = (float)sin( ra ), ca = (float)cos( ra );
	float sb = (float)sin( rb ), cb = (float)cos( rb );
	float xa = ca + x, ya = sa + y; // end point of edge a
	float xb = cb + x, yb = sb + y; // end point of edge b
	float den, num1, num2, u1, u2; // intersection calculations
	float u, xd, yd; // closest point

	float x1, y1, x2, y2;
	int i, j;

	for ( i=0; i<BLOCK_COUNT; i++ ) {
		for ( j=0; j<4; j++ ) {
			if ( j == 0 ) {
				x1 = blocks[i].x; y1 = blocks[i].y;
				x2 = blocks[i].x + blocks[i].width; y2 = blocks[i].y;
			} else if ( j == 1 ) {
				x1 = blocks[i].x; y1 = blocks[i].y;
				x2 = blocks[i].x; y2 = blocks[i].y + blocks[i].height;
			} else if ( j == 2 ) {
				x1 = blocks[i].x; y1 = blocks[i].y + blocks[i].height;
				x2 = blocks[i].x + blocks[i].width; y2 = blocks[i].y + blocks[i].height;
			} else {
				x1 = blocks[i].x + blocks[i].width; y1 = blocks[i].y;
				x2 = blocks[i].x + blocks[i].width; y2 = blocks[i].y + blocks[i].height;
			}

			// find intersection with a (http://local.wasp.uwa.edu.au/~pbourke/geometry/lineline2d/)
			den = (y2 - y1)*(xa - x) - (x2 - x1)*(ya - y);
			num1 = (x2 - x1)*(y - y1) - (y2 - y1)*(x - x1);
			num2 = (xa - x)*(y - y1) - (ya - y)*(x - x1);

			if ( den == 0 ) { // parallel
				if ( num1 == 0 && num2 == 0 ) { // coincident
					// nothing to do
				}
			} else {
				u1 = num1/den;
				u2 = num2/den;
				if ( u2 >0 && u2 < 1 ) { // segment intersects
					// which point is outside the cone? move it to the intersection
					if ( num2 < 0 ) { // p1
						x1 = x1 + u2*(x2 - x1);
						y1 = y1 + u2*(y2 - y1);
					} else { // p2
						x2 = x1 + u2*(x2 - x1);
						y2 = y1 + u2*(y2 - y1);
					}
				}
			}

			// find intersection with b
			den = (y2 - y1)*(xb - x) - (x2 - x1)*(yb - y);
			num1 = (x2 - x1)*(y - y1) - (y2 - y1)*(x - x1);
			num2 = (xb - x)*(y - y1) - (yb - y)*(x - x1);

			if ( den == 0 ) { // parallel
				if ( num1 == 0 && num2 == 0 ) { // coincident
					// nothing to do
				}
			} else {
				u1 = num1/den;
				u2 = num2/den;
				if ( u2 >0 && u2 < 1 ) { // segment intersects
					// which point is outside the cone? move it to the intersection
					if ( num2 > 0 ) { // p1
						x1 = x1 + u2*(x2 - x1);
						y1 = y1 + u2*(y2 - y1);
					} else { // p2
						x2 = x1 + u2*(x2 - x1);
						y2 = y1 + u2*(y2 - y1);
					}
				}
			}

			// find the shortest distance from x,y to new segment (http://local.wasp.uwa.edu.au/~pbourke/geometry/pointline/)
			u = ((x - x1)*(x2 - x1) + (y - y1)*(y2 - y1)) / ((x2 - x1)*(x2 - x1) + (y2 - y1)*(y2 - y1));
			if ( u <= 0 ) { // p1 is the closest
				xd = x1; 
				yd = y1;
			} else if ( u >= 1 ) { // p2 is the closest
				xd = x2; 
				yd = y2;
			} else {
				xd = x1 + u*(x2 - x1); 
				yd = y1 + u*(y2 - y1);
			}

			// make sure the point is "within" the cone
			if (   (xd - x)*(ya - y) + (yd - y)*(-xa + x) >= -0.0001
				&& (xd - x)*(yb - y) + (yd - y)*(-xb + x) <= 0.0001 ) {
					// we're good
				d = min( d, (float)sqrt((xd - x)*(xd - x) + (yd - y)*(yd - y)) );
			}
		}
	}

	d = max( d, SONAR_RANGE_MIN );

	// add to history
	sonarHistory[sonarHistoryWrite].x[0] = x;
	sonarHistory[sonarHistoryWrite].y[0] = y;
	for ( i = 1; i<6; i++ ) {
		sonarHistory[sonarHistoryWrite].x[i] = x + d*(float)cos( rb + (i-1)/4.0f*2*SONAR_A );
		sonarHistory[sonarHistoryWrite].y[i] = y + d*(float)sin( rb + (i-1)/4.0f*2*SONAR_A );
	}
	sonarHistoryWrite = (sonarHistoryWrite + 1) % SONAR_HISTORY_SIZE;
	if ( sonarHistoryCount < SONAR_HISTORY_SIZE )
		sonarHistoryCount++;

	return d;
}

void * generateParticleState_Avatar( int index, void *genParams ) {
	Robot *r = (Robot *)genParams;

	ParticleState_Avatar *ps_a = (ParticleState_Avatar *)malloc(sizeof(ParticleState_Avatar));
	if ( ps_a == NULL ) {
		return NULL;
	}

	ps_a->x = r->x + (float)randNormalDistribution( 0, AVATAR_INIT_LINEAR_SIG );
	ps_a->y = r->y + (float)randNormalDistribution( 0, AVATAR_INIT_LINEAR_SIG );
	ps_a->r = r->r + (float)randNormalDistribution( 0, AVATAR_INIT_ROTATIONAL_SIG );
	
	//ps_a->r = 0; // TEMP

	return (void *)ps_a;
}

void * copyParticleState_Avatar( void *vpstate ) {
	ParticleState_Avatar *state = (ParticleState_Avatar *)vpstate;
	ParticleState_Avatar *copy = (ParticleState_Avatar *)malloc(sizeof(ParticleState_Avatar));
	if ( copy == NULL ) {
		return NULL;
	}

	copy->x = state->x;
	copy->y = state->y;
	copy->r = state->r;

	return (void *)copy;
}

int predictPF_Avatar( ParticleFilter *pf, unsigned int dtime, float forwardV, float tangentialV, float rotationalV ) {
	ParticleRegion *pr;
	std::list<Particle*>::iterator p;
	std::list<void*>::iterator stateIter;
	std::list<void*> *ps;
	ParticleState_Avatar *oldState, *newState;
	_timeb *tb;
	float forwardD, tangentialD, rotationalD;
	float s, c;

	if ( pf == NULL )
		return 1;

	ps = new std::list<void*>();
	if ( ps == NULL )
		return 1;

	pr = pf->regions->front();
	p = pr->particles->begin();
	stateIter = pr->states->front()->begin();
	while ( p != pr->particles->end() ) {
		oldState = (ParticleState_Avatar *)*stateIter;
		
		newState = (ParticleState_Avatar *)malloc(sizeof(ParticleState_Avatar));
		if ( newState == NULL )
			return 1;

		forwardD = (forwardV + (float)randNormalDistribution(0,AVATAR_LINEARV_SIG_EST))*dtime;
		tangentialD = (tangentialV + (float)randNormalDistribution(0,AVATAR_LINEARV_SIG_EST))*dtime;
		rotationalD = (rotationalV + (float)randNormalDistribution(0,AVATAR_ROTATIONALV_SIG_EST))*dtime;

		// forwardD = tangentialD = rotationalD = 0; // TEMP

		s = sin( oldState->r );
		c = cos( oldState->r );
		newState->x = oldState->x + c*forwardD - s*tangentialD;
		newState->y = oldState->y + s*forwardD + c*tangentialD;
		newState->r = oldState->r + rotationalD;

		ps->push_back( (void*)newState );

		p++;
		stateIter++;
	}
	tb = (_timeb *)malloc(sizeof(_timeb));
	if ( tb == NULL ) {
		return 1;
	}
	tb->millitm = pr->times->front()->millitm + dtime;
	tb->time = pr->times->front()->time + tb->millitm/1000;
	tb->millitm %= 1000;
	pr->times->push_front( tb );
	pr->states->push_front( ps );

	return 0;	
}

int Simulation::correctPF_AvatarConicSensor( ParticleFilter *pf, float d, float sensorRot, _timeb *time ) {
	std::list<ParticleRegion*>::iterator pr;
	std::list<Particle*>::iterator p;
	std::list<std::list<void*>*>::iterator psBefore;
	std::list<std::list<void*>*>::iterator psAfter;
	std::list<_timeb*>::iterator tbBefore;
	std::list<_timeb*>::iterator tbAfter;
	std::list<void*>::iterator stateBefore;
	std::list<void*>::iterator stateAfter;
	ParticleState_Avatar state;
	float scale;
	float interp;
	int border;
	int i, j;
	int r, c;
	float fr, fc;
	int div;

	float PFbound[4];
	float origin[2]; // origin in densityR/absoluteR image coordinates
	float offset[2]; // offset for adding images to mapUpdate

	float penalty, support;
	float maxAbsoluteOd, maxDensityOd;
	float absoluteNorm, densityNorm;
	float sumWeightedObs;
	float particleNumEff;

	generateConicTemplates( d, SONAR_RANGE_MAX, SONAR_SIG_EST, SONAR_A_EST, SONAR_B_EST, densityT, absoluteT, &scale );

	// find region and bounding particles
	pr = pf->regions->begin();
	while ( pr != pf->regions->end() ) {
		if ( (*pr)->startTime.time < time->time 
	    || ( (*pr)->startTime.time == time->time && (*pr)->startTime.millitm < time->millitm ) )
			break;
		pr++;
	}
	if ( pr == pf->regions->end() )
		return 1;

	tbBefore = (*pr)->times->begin();
	psBefore = (*pr)->states->begin();
	tbAfter = tbBefore;
	psAfter = psBefore;
	while ( tbBefore != (*pr)->times->end() ) {
		if ( (*tbBefore)->time < time->time 
	    || ( (*tbBefore)->time == time->time && (*tbBefore)->millitm < time->millitm ) )
			break;
		tbAfter = tbBefore;
		psAfter = psBefore;
		tbBefore++;
		psBefore++;
	}

	if ( tbAfter == tbBefore ) { // we're at the top of the stack!?
		return 1;
	}

	interp = ( (time->time - (*tbBefore)->time)*1000 + (time->millitm - (*tbBefore)->millitm) ) / (float)( ((*tbAfter)->time - (*tbBefore)->time)*1000 + ((*tbAfter)->millitm - (*tbBefore)->millitm) );

	// update PFbound
	// TODO ignore particles with very little weight?
	stateBefore = (*psBefore)->begin();
	stateAfter = (*psAfter)->begin();
	PFbound[0] = ((ParticleState_Avatar*)*stateBefore)->x + (((ParticleState_Avatar*)*stateAfter)->x - ((ParticleState_Avatar*)*stateBefore)->x)*interp;
	PFbound[1] = ((ParticleState_Avatar*)*stateBefore)->y + (((ParticleState_Avatar*)*stateAfter)->y - ((ParticleState_Avatar*)*stateBefore)->y)*interp;
	PFbound[2] = 0;
	PFbound[3] = 0;
	stateBefore++;
	stateAfter++;
	while ( stateBefore != (*psBefore)->end() ) {
		state.x = ((ParticleState_Avatar*)*stateBefore)->x + (((ParticleState_Avatar*)*stateAfter)->x - ((ParticleState_Avatar*)*stateBefore)->x)*interp;
		state.y = ((ParticleState_Avatar*)*stateBefore)->y + (((ParticleState_Avatar*)*stateAfter)->y - ((ParticleState_Avatar*)*stateBefore)->y)*interp;
		if ( PFbound[0] > state.x ) {
			PFbound[2] = PFbound[0] + PFbound[2] - state.x;
			PFbound[0] = state.x;
		} else if ( PFbound[0] + PFbound[2] < state.x ) {
			PFbound[2] = state.x - PFbound[0];
		}
		if ( PFbound[1] > state.y ) {
			PFbound[3] = PFbound[1] + PFbound[3] - state.y;
			PFbound[1] = state.y;
		} else if ( PFbound[1] + PFbound[3] < state.y ) {
			PFbound[3] = state.y - PFbound[1];
		}
		stateBefore++;
		stateAfter++;
	}
	PFbound[2] = ceil((PFbound[0]+PFbound[2])*MAP_DIVISIONS)/MAP_DIVISIONS;
	PFbound[0] = floor(PFbound[0]*MAP_DIVISIONS)/MAP_DIVISIONS;
	PFbound[2] -= PFbound[0];
	PFbound[3] = ceil((PFbound[1]+PFbound[3])*MAP_DIVISIONS)/MAP_DIVISIONS;
	PFbound[1] = floor(PFbound[1]*MAP_DIVISIONS)/MAP_DIVISIONS;
	PFbound[3] -= PFbound[1];
	
	// prepare map update
	border = (int)ceil(5 + scale*MAP_DIVISIONS*TEMPLATE_SIZE);
	mapUpdateLoc[0] = PFbound[0] - border/(float)MAP_DIVISIONS;
	mapUpdateLoc[1] = PFbound[1] + (ceil(PFbound[3]*MAP_DIVISIONS) + border)/(float)MAP_DIVISIONS;
	if ( mapUpdate->bufSize < (int)sizeof(float)*((int)(PFbound[2]*MAP_DIVISIONS)+border*2) * ((int)(PFbound[3]*MAP_DIVISIONS)+border*2) ) {
		// allocate more space for the map update
		FreeImage( mapUpdate );
		mapUpdate = NewImage( ((int)(PFbound[3]*MAP_DIVISIONS)+border*2), ((int)(PFbound[2]*MAP_DIVISIONS)+border*2) );
	} else {
		mapUpdate->rows = ((int)(PFbound[3]*MAP_DIVISIONS)+border*2);
		mapUpdate->cols = ((int)(PFbound[2]*MAP_DIVISIONS)+border*2);
		mapUpdate->stride = mapUpdate->rows;
	}
	for ( r=0; r<mapUpdate->rows; r++ ) {
		for ( c=0; c<mapUpdate->cols; c++ ) {
			Px(mapUpdate,r,c) = 0.5f;
		}
	}

	// clear the rotationDivision flags
	memset( rotationDivision, 0, sizeof(rotationDivision) );
	
	// for each particle
	maxAbsoluteOd = 0;
	maxDensityOd = 0;
	p = (*pr)->particles->begin();
	stateBefore = (*psBefore)->begin();
	stateAfter = (*psAfter)->begin();
	while ( p != (*pr)->particles->end() ) {
		state.x = ((ParticleState_Avatar*)*stateBefore)->x + (((ParticleState_Avatar*)*stateAfter)->x - ((ParticleState_Avatar*)*stateBefore)->x)*interp;
		state.y = ((ParticleState_Avatar*)*stateBefore)->y + (((ParticleState_Avatar*)*stateAfter)->y - ((ParticleState_Avatar*)*stateBefore)->y)*interp;
		state.r = sensorRot + ((ParticleState_Avatar*)*stateBefore)->r + (((ParticleState_Avatar*)*stateAfter)->r - ((ParticleState_Avatar*)*stateBefore)->r)*interp;
		while ( state.r < 0 ) state.r += 2*fM_PI;
		while ( state.r > 2*fM_PI ) state.r -= 2*fM_PI;
		div = (int)(ROTATION_DIVISIONS*(state.r + ROTATION_RESOLUTION*0.5)/(2*fM_PI)) % ROTATION_DIVISIONS;
		// rotate
		if ( rotationDivision[div] == false ) {
			if ( d < SONAR_RANGE_MAX )
				RotateImageEx( densityT, (float)RadiansToDegrees( div*ROTATION_RESOLUTION ), densityR[div], integralTarget, 0 );
			RotateImageEx( absoluteT, (float)RadiansToDegrees( div*ROTATION_RESOLUTION ), absoluteR[div], integralTarget, 0 );
			rotationDivision[div] = true;
		}
		origin[0] = absoluteR[div]->cols/2.0f - absoluteT->cols/2.0f*(float)cos(div*ROTATION_RESOLUTION);
		origin[1] = absoluteR[div]->rows/2.0f + absoluteT->cols/2.0f*(float)sin(div*ROTATION_RESOLUTION);
		
		// compare against map
		offset[0] = (state.x - mapLoc[0])*MAP_DIVISIONS - origin[0]*scale*MAP_DIVISIONS;
		offset[1] = -(state.y - mapLoc[1])*MAP_DIVISIONS - origin[1]*scale*MAP_DIVISIONS;
		(*p)->absoluteOd = 0;
		(*p)->densityOd = 0;
		fc = offset[0];
		c = (int)fc;
		for ( j=0; j<absoluteR[div]->cols; j++ ) {
			fr = offset[1];
			r = (int)fr;
			for ( i=0; i<absoluteR[div]->rows; i++ ) {
				// update absoluteOd, total penalty
				if ( Px(absoluteR[div],i,j) < 0 ) {
					penalty = max( 0, map[c][r] - 0.5f ) * -Px(absoluteR[div],i,j);
				} else {
					penalty = max( 0, 0.5f - map[c][r] ) * Px(absoluteR[div],i,j);
				}
				(*p)->absoluteOd += penalty;
				// update densityOd, max support
				if ( d < SONAR_RANGE_MAX ) {
					if ( Px(densityR[div],i,j) < 0 ) {
						support = max( 0, 0.5f - map[c][r] ) * -Px(densityR[div],i,j);
					} else {
						support = max( 0, map[c][r] - 0.5f ) * Px(densityR[div],i,j);
					}
					(*p)->densityOd = max( (*p)->densityOd, support );
				}
				fr += scale*MAP_DIVISIONS;
				r = (int)fr;
			}
			fc += scale*MAP_DIVISIONS;
			c = (int)fc;
		}
		maxAbsoluteOd = max( maxAbsoluteOd, (*p)->absoluteOd );
		maxDensityOd = max( maxDensityOd, (*p)->densityOd );

		// TEMP
		//offset[0] = (state.x - mapLoc[0])*MAP_DIVISIONS;
		//offset[1] = -(state.y - mapLoc[1])*MAP_DIVISIONS;
		//map[(int)offset[0]][(int)offset[1]] = 0;

		// stamp on update
		offset[0] = (state.x - mapUpdateLoc[0])*MAP_DIVISIONS - origin[0]*scale*MAP_DIVISIONS;
		offset[1] = -(state.y - mapUpdateLoc[1])*MAP_DIVISIONS - origin[1]*scale*MAP_DIVISIONS;
		if ( d < SONAR_RANGE_MAX )
			ImageAdd( densityR[div], mapUpdate, offset[0], offset[1], scale*MAP_DIVISIONS, (*p)->weight, 1 );
		ImageAdd( absoluteR[div], mapUpdate, offset[0], offset[1], scale*MAP_DIVISIONS, (*p)->weight, 0 );
		
		// TEMP
		//offset[0] = (state.x - mapUpdateLoc[0])*MAP_DIVISIONS;
		//offset[1] = -(state.y - mapUpdateLoc[1])*MAP_DIVISIONS;
		//Px(mapUpdate,(int)offset[1],(int)offset[0]) = 1;

		p++;
		stateBefore++;
		stateAfter++;
	}
	// apply observations
	if ( maxAbsoluteOd > 0 ) absoluteNorm = 1/maxAbsoluteOd;
	else absoluteNorm = 1;
	if ( maxDensityOd > 0 ) densityNorm = 1/maxDensityOd;
	else densityNorm = 1;
	sumWeightedObs = 0;
	p = (*pr)->particles->begin();
	while ( p != (*pr)->particles->end() ) {
		(*p)->obsDensity *= OBS_DENSITY_RATIO*(*p)->absoluteOd*absoluteNorm 
			             + (1-OBS_DENSITY_RATIO)*(*p)->densityOd*densityNorm
						 + OBS_DENSITY_AMBIENT;
		sumWeightedObs += (*p)->weight * (*p)->obsDensity;
		p++;
	}
	p = (*pr)->particles->begin();
	particleNumEff = 0;
	while ( p != (*pr)->particles->end() ) {
		(*p)->weight = (*p)->weight * (*p)->obsDensity / sumWeightedObs;
		(*p)->obsDensity = 1;
		particleNumEff += (*p)->weight * (*p)->weight;
		p++;
	}
	particleNumEff = 1/particleNumEff;
	if ( particleNumEff < MIN_EFF_PARTICLES && pr ==  pf->regions->begin() )
		resampleParticleFilter( pf, copyParticleState_Avatar );

	// apply update to map
	float pC, pSC, pS;
	int mapOffset[2];
	mapOffset[0] = (int)((mapUpdateLoc[0] - mapLoc[0])*MAP_DIVISIONS + 0.5f);
	mapOffset[1] = -(int)((mapUpdateLoc[1] - mapLoc[1])*MAP_DIVISIONS + 0.5f);
	for ( r=0; r<mapUpdate->rows; r++ ) {
		if ( r+mapOffset[1] < 0 )
			continue;
		if ( r+mapOffset[1] >= MAP_SIZE*MAP_DIVISIONS )
			break;
		for ( c=0; c<mapUpdate->cols; c++ ) {
			if ( c+mapOffset[0] < 0 )
				continue;
			if ( c+mapOffset[0] >= MAP_SIZE*MAP_DIVISIONS )
				break;
			pSC = min( 0.95f, max( 0.05f, Px(mapUpdate,r,c) ) );
			pC = map[c+mapOffset[0]][r+mapOffset[1]];
			pS = pC*pSC + (1-pC)*(1-pSC);
			map[c+mapOffset[0]][r+mapOffset[1]] = pC*pSC/pS;
		}
	}

	return 0;
}

int resampleParticleFilter( ParticleFilter *pf, void * (*copyState)( void *vpstate ) ) {
	int i;
	ParticleRegion *pr;
	Particle	   *p;
	std::list<void*> *ps;
	void *state;
	_timeb *tb;
	_timeb *startTime = pf->regions->front()->times->front(); // time of latest state
	float cdfOld, cdfNew;
	std::list<Particle*>::iterator parentP;
	std::list<void*>::iterator parentS;

	float particleNumInv = 1.0f/pf->particleNum;

	parentP = pf->regions->front()->particles->begin();
	parentS = pf->regions->front()->states->front()->begin();

	// create new region
	pr = (ParticleRegion *)malloc(sizeof(ParticleRegion));
	if ( pr == NULL ) {
		return 1;
	}
	pr->index = pf->regionCount;
	pr->startTime.time = startTime->time;
	pr->startTime.millitm = startTime->millitm;
	pr->particles = new std::list<Particle*>();
	pr->times = new std::list<_timeb*>();
	pr->states = new std::list<std::list<void*>*>();
	tb = (_timeb *)malloc(sizeof(_timeb));
	if ( tb == NULL ) {
		free( pr );
		return 1;
	}
	tb->time = startTime->time;
	tb->millitm = startTime->millitm;
	pr->times->push_front( tb );

	ps = new std::list<void*>();
	if ( ps == NULL ) {
		FreeFilter( pf );
		return 1;
	}
	pr->states->push_front( ps );
	
	pf->regions->push_front( pr );
	pf->regionCount++;

	cdfOld = (*parentP)->weight;
	cdfNew = (float)randUniform01()*particleNumInv;

	// create particles
	for ( i=0; i<(int)pf->particleNum; i++ ) {
		p = (Particle *)malloc(sizeof(Particle));
		if ( p == NULL ) {
			FreeFilter( pf );
			return 1;
		}
		p->weight = particleNumInv;
		p->obsDensity = 1;
		p->obsDensityForward = 1;
		
		while ( cdfOld < cdfNew ) {
			parentP++;
			parentS++;
			cdfOld += (*parentP)->weight;
		}

		// copy state		
		state = (*copyState)( (void *) *parentS );
		if ( state == NULL ) {
			free( p );
			FreeFilter( pf );
			return 1;
		}

		ps->push_front( state );

		pr->particles->push_front( p );

		cdfNew += particleNumInv;
	}

	return 0;
}

ParticleFilter * CreateFilter( int particleNum, _timeb *startTime, void * (*genState)(int index, void *genParams), void *genParams ) {
	ParticleFilter *pf;
	ParticleRegion *pr;
	Particle	   *p;
	_timeb *tb;
	std::list<void*> *ps;
	void *state;
	int i;

	pf = (ParticleFilter *)malloc(sizeof(ParticleFilter));
	if ( pf == NULL ) {
		return NULL;
	}

	pf->regionCount = 0;
	pf->particleNum = particleNum;
	pf->forwardMarker = -1;
	pf->regions = new std::list<ParticleRegion*>();

	// create first region
	pr = (ParticleRegion *)malloc(sizeof(ParticleRegion));
	if ( pr == NULL ) {
		FreeFilter( pf );
		return NULL;
	}
	pr->index = pf->regionCount;
	pr->startTime.time = startTime->time;
	pr->startTime.millitm = startTime->millitm;
	pr->particles = new std::list<Particle*>();
	pr->times = new std::list<_timeb*>();
	pr->states = new std::list<std::list<void*>*>();
	tb = (_timeb *)malloc(sizeof(_timeb));
	if ( tb == NULL ) {
		FreeFilter( pf );
		return NULL;
	}
	tb->time = startTime->time;
	tb->millitm = startTime->millitm;
	pr->times->push_front( tb );

	ps = new std::list<void*>();
	if ( ps == NULL ) {
		FreeFilter( pf );
		return NULL;
	}
	pr->states->push_front( ps );
	
	pf->regions->push_front( pr );
	pf->regionCount++;
	
	// create particles
	for ( i=0; i<particleNum; i++ ) {
		p = (Particle *)malloc(sizeof(Particle));
		if ( p == NULL ) {
			FreeFilter( pf );
			return NULL;
		}
		p->weight = 1.0f/particleNum;
		p->obsDensity = 1;
		p->obsDensityForward = 1;
		
		// create first state		
		state = (*genState)( i, genParams );
		if ( state == NULL ) {
			free( p );
			FreeFilter( pf );
			return NULL;
		}

		ps->push_front( state );

		pr->particles->push_front( p );
	}

	return pf;
}

void FreeFilter( ParticleFilter *pf ) {
	ParticleRegion *pr;
	Particle	   *p;
	std::list<void*> *ps;

	if ( pf != NULL ) {
		while ( pf->regions->begin() != pf->regions->end() ) {
			pr = *pf->regions->begin();
			pf->regions->pop_front();
			while ( pr->particles->begin() != pr->particles->end() ) {
				p = *pr->particles->begin();
				pr->particles->pop_front();
				free( p );
			}
			delete pr->particles;
			while ( pr->times->begin() != pr->times->end() ) {
				free( pr->times->front() );
				pr->times->pop_front();
			}
			delete pr->times;
			while ( pr->states->begin() != pr->states->end() ) {
				ps = pr->states->front();
				pr->states->pop_front();
				while ( ps->begin() != ps->end() ) {
					free( ps->front() );
					ps->pop_front();
				}
				delete ps;
			}
			delete pr->states;
			free( pr );
		}
		delete pf->regions;
		free( pf );
	}
}