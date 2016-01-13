#include "StdAfx.h"
#include "OpenGLControl.h"
#include "Visualize.h"

#include "..\\autonomic\\DDB.h"
//#include "Simulation.h"
#include "AgentMirror.h"

#include "math.h"

COpenGLControl::COpenGLControl(void)
{
	m_bIsMaximized = false;

	m_fViewH = 1.0f;
	m_fViewW = 1.0f;
	m_fAspectRatio = 1.0f;

	m_fPosX = 0.0f;    // X position of model in camera view
	m_fPosY = 0.0f;    // Y position of model in camera view
	m_fZoom = 2.0f;   // Zoom on model in camera view
	m_fZoomE = exp( m_fZoom );
	m_fRotX = 0.0f;    // Rotation on model in camera view
	m_fRotY = 0.0f;    // Rotation on model in camera view

	// camera start position
	m_fPosX = -1.5949129f;
	m_fPosY = -1.9106094f;
	m_fZoom = 1.5100001f;
	m_fZoomE = 4.5267315f;

	this->mouseX = 0;
	this->mouseY = 0;

	// TEMP
	int i;
	for ( i = 0; i < 64*64; i++ )
		source[i] = 1;

	for ( i = 0; i < 1000; i++ )
		scale[i] = 0.001f;

	m_Vis = new Visualize();
	m_Vis->Initialize( this );

	m_AgentMirror = NULL;

//	m_Sim = NULL;
	
	m_simSpeed = 5;

	UpdateViewport();
}

COpenGLControl::~COpenGLControl(void)
{
//	delete m_Sim;
	delete m_Vis;
	m_Vis = NULL;

	this->KillFont();
}

void COpenGLControl::setView( float x, float y, float zoom ) { 
	
	m_fPosX = x;
	m_fPosY = y;

	if ( zoom != -1 ) {
		m_fZoom = zoom;
		m_fZoomE = exp( m_fZoom );
	}

	UpdateViewport();
	OnDraw(NULL);

	return;
}

GLvoid COpenGLControl::BuildFont( int height )					// Build Our Bitmap Font
{
	HFONT	font;						// Windows Font ID
//	HFONT	oldfont;					// Used For Good House Keeping

	GLuint fBase = glGenLists(96);					// Storage For 96 Characters ( NEW )

	font = CreateFont(	-height,				// Height Of Font ( NEW )
				0,				// Width Of Font
				0,				// Angle Of Escapement
				0,				// Orientation Angle
				FW_NORMAL,			// Font Weight
				FALSE,				// Italic
				FALSE,				// Underline
				FALSE,				// Strikeout
				ANSI_CHARSET,			// Character Set Identifier
				OUT_TT_PRECIS,			// Output Precision
				CLIP_DEFAULT_PRECIS,		// Clipping Precision
				ANTIALIASED_QUALITY,		// Output Quality
				FF_DONTCARE|DEFAULT_PITCH,	// Family And Pitch
				_T("Courier New"));			// Font Name

//	oldfont = (HFONT)SelectObject(hdc, font);		// Selects The Font We Want
//	wglUseFontBitmaps(hdc, 32, 96, this->fontBase);			// Builds 96 Characters Starting At Character 32
//	SelectObject(hdc, oldfont);				// Selects The Font We Want
//	DeleteObject(font);					// Delete The Font

	this->fontBase[height] = fBase;
	this->font[height] = font;
//	this->oldfont[height] = oldfont;
}

GLvoid COpenGLControl::KillFont(GLvoid)						// Delete The Font List
{
	std::map<int,GLuint>::iterator iFB;
	for ( iFB = this->fontBase.begin(); iFB != this->fontBase.end(); iFB++ ) {
		glDeleteLists(iFB->second, 96);				// Delete All 96 Characters ( NEW )
	}
	this->fontBase.clear();
}

GLvoid COpenGLControl::glPrint(const char *fmt, ...)				// Custom GL "Print" Routine
{
	char		text[512];				// Holds Our String
	va_list		ap;					// Pointer To List Of Arguments

	if (fmt == NULL || !strlen(fmt) )					// If There's No Text
		return;						// Do Nothing

	va_start(ap, fmt);					// Parses The String For Variables
		vsprintf(text, fmt, ap);				// And Converts Symbols To Actual Numbers
	va_end(ap);						// Results Are Stored In Text

	glPushAttrib(GL_LIST_BIT);				// Pushes The Display List Bits		( NEW )

	glListBase(this->fontBase[this->fontHeight] - 32);					// Sets The Base Character to 32	( NEW )
	glCallLists((GLsizei)strlen(text), GL_UNSIGNED_BYTE, text);	// Draws The Display List Text	( NEW )

	glPopAttrib();						// Pops The Display List Bits	( NEW )
}

int COpenGLControl::setFontHeight( int height ) {
	
	if ( this->fontHeight == height )
		return 0;

	std::map<int,HFONT>::iterator iF = this->font.find( height );

	if ( iF == this->font.end() ) {
		this->BuildFont( height );
	}

	this->fontHeight = height;

	HFONT oldfont = (HFONT)SelectObject(hdc, this->font[height]);		// Selects The Font We Want
	wglUseFontBitmaps(hdc, 32, 96, this->fontBase[height]);			// Builds 96 Characters Starting At Character 32
	SelectObject(hdc, oldfont);				// Selects The Font We Want

	return 0;
}

void COpenGLControl::resetSimulation() {
//	m_Sim->CleanUp();
//	delete m_Sim;

//	m_Sim = new Simulation( m_Vis );
}

void COpenGLControl::oglCreate(CRect rect, CWnd *parent )
{
   CString className = AfxRegisterWndClass(CS_HREDRAW |
      CS_VREDRAW | CS_OWNDC, NULL,
      (HBRUSH)GetStockObject(BLACK_BRUSH), NULL);

   CreateEx(0, className, _T("OpenGL"), WS_CHILD | WS_VISIBLE |
            WS_CLIPSIBLINGS | WS_CLIPCHILDREN, rect, parent, 0);

   // Set initial variables' values
   m_oldWindow    = rect;
   m_originalRect = rect;
   m_rect = rect;

   hWnd = parent;

   this->frameCount = 0;

   //m_Sim = new Simulation( m_Vis );

   	// TEMP for now hard code the simulation loads here
   //m_Sim->loadConfig( "data\\sim\\TestPerfect.ini" );
   //m_Sim->loadConfig( "data\\sim\\TestCSLAM_discard.ini" );
   //m_Sim->loadConfig( "data\\sim\\TestCSLAM_delay.ini" );
   //m_Sim->loadConfig( "data\\sim\\TestJCSLAM_landmark.ini" );
   //m_Sim->loadConfig( "data\\sim\\TestJCSLAM_nopriority.ini" );
   //m_Sim->loadConfig( "data\\sim\\Setup1.ini" );
   
   //m_Sim->loadConfig( "data\\sim\\J1Localization.ini" );
   //m_Sim->loadConfig( "data\\sim\\J1Mapping_CSLAM_discard.ini" );
   //m_Sim->loadConfig( "data\\sim\\J1Mapping_CSLAM_delay.ini" );
   //m_Sim->loadConfig( "data\\sim\\J1Mapping_CSLAM_delay.ini" );
   
   //m_Sim->loadConfig( "data\\sim\\rooms.ini" );
   //m_Sim->TogglePause();

	//m_Vis->loadPathFile( "data\\paths\\boundary1.path" );
	//m_Vis->loadPathFile( "data\\paths\\layout1.path" );
	//m_Sim->addAvatar( 0, 0, 0, "data\\sim\\AvatarCam.ini" );
	//Avatar *pioneer = m_Sim->addAvatar( "data\\run1black\\poseData.txt" );
	//m_Sim->avatarConfigureSonar( pioneer, 8, "data\\run1black\\sonarData.txt", "data\\run1black\\sonarPose.txt" );
	//Avatar *surveyor2 = m_Sim->addAvatar( "data\\cameraTest\\poseData.txt" );
	//m_Sim->avatarConfigureCameras( surveyor2, 1, "data\\cameraTest\\cameraData.txt", "data\\cameraTest\\cameraPose.txt", "data\\cameraTest" );
}


void COpenGLControl::oglInitialize(void)
{
   // Initial Setup:
   //

	
   static PIXELFORMATDESCRIPTOR pfd =
   {
      sizeof(PIXELFORMATDESCRIPTOR),
      1,
      PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
      PFD_TYPE_RGBA,
      32,    // bit depth
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      16,    // z-buffer depth
      0, 0, 0, 0, 0, 0, 0,
   };

   // Get device context only once.
   hdc = GetDC()->m_hDC;


   // Pixel format.
   m_nPixelFormat = ChoosePixelFormat(hdc, &pfd);
   SetPixelFormat(hdc, m_nPixelFormat, &pfd);

   // Create the OpenGL Rendering Context.
   hrc = wglCreateContext(hdc);
   wglMakeCurrent(hdc, hrc);

   // Basic Setup:
   //
   // Set color to use when clearing the background.
   glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
   glClearDepth(1.0f);

   // Turn on backface culling
   glFrontFace(GL_CCW);
   glCullFace(GL_BACK);

   // Turn on depth testing
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LEQUAL);

   // Build the font
   this->setFontHeight( 12 );
   //this->BuildFont();	


   // Send draw request
   OnDraw(NULL);
}

void COpenGLControl::oglDrawScene(void)
{

/*
	int i;


	// draw grid
	
	glPolygonMode(GL_FRONT, GL_LINE); // Wireframe Mode
	
	glColor3f( 0, 0, 0 );
	glBegin(GL_LINES);
		for ( i = (int)floor(bottom); i <= ceil(top); i++ ) {
		   glVertex3f( left, (float)i, 0 );
		   glVertex3f( right, (float)i, 0 );
		}
		for ( i = (int)floor(left); i <= ceil(right); i++ ) {
		   glVertex3f( (float)i, bottom, 0 );
		   glVertex3f( (float)i, top, 0 );
		}
   glEnd();

   // TEMP
/*   int j, k;
   memset( target, 0, sizeof(target) );
   for ( k = 0; k < 1000; k++ ) {
	   for ( i = 0; i < 64*64; i++ ) {
			target[i] = source[i]*scale[k];
	   }
	   for ( i = 0; i < 128; i++ ) {
		   for ( j = 0; j < 128; j++ ) {
				target[i+j*128] = source[i+j*128]*scale[k];
		   }
	   }
   }
*/
   /*
   glBegin(GL_QUADS);
      // Top Side
      glVertex3f( 1.0f, 1.0f,  1.0f);
      glVertex3f( 1.0f, 1.0f, -1.0f);
      glVertex3f(-1.0f, 1.0f, -1.0f);
      glVertex3f(-1.0f, 1.0f,  1.0f);

      // Bottom Side
      glVertex3f(-1.0f, -1.0f, -1.0f);
      glVertex3f( 1.0f, -1.0f, -1.0f);
      glVertex3f( 1.0f, -1.0f,  1.0f);
      glVertex3f(-1.0f, -1.0f,  1.0f);

      // Front Side
      glVertex3f( 1.0f,  1.0f, 1.0f);
      glVertex3f(-1.0f,  1.0f, 1.0f);
      glVertex3f(-1.0f, -1.0f, 1.0f);
      glVertex3f( 1.0f, -1.0f, 1.0f);

      // Back Side
      glVertex3f(-1.0f, -1.0f, -1.0f);
      glVertex3f(-1.0f,  1.0f, -1.0f);
      glVertex3f( 1.0f,  1.0f, -1.0f);
      glVertex3f( 1.0f, -1.0f, -1.0f);

      // Left Side
      glVertex3f(-1.0f, -1.0f, -1.0f);
      glVertex3f(-1.0f, -1.0f,  1.0f);
      glVertex3f(-1.0f,  1.0f,  1.0f);
      glVertex3f(-1.0f,  1.0f, -1.0f);

      // Right Side
      glVertex3f( 1.0f,  1.0f,  1.0f);
      glVertex3f( 1.0f, -1.0f,  1.0f);
      glVertex3f( 1.0f, -1.0f, -1.0f);
      glVertex3f( 1.0f,  1.0f, -1.0f);
   glEnd(); */
}

void COpenGLControl::UpdateViewport() {
	this->viewportLeft = -m_fZoomE*m_fAspectRatio - m_fPosX ;
	this->viewportRight = m_fZoomE*m_fAspectRatio - m_fPosX;
	this->viewportBottom = -m_fZoomE - m_fPosY;
	this->viewportTop = m_fZoomE - m_fPosY;
}

BEGIN_MESSAGE_MAP(COpenGLControl, CWnd)
	ON_WM_PAINT()
	ON_WM_CREATE()
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_MOUSEMOVE()
	ON_WM_RBUTTONDOWN()
END_MESSAGE_MAP()

void COpenGLControl::OnPaint()
{
	//CPaintDC dc(this); // device context for painting
	ValidateRect(NULL);
	// TODO: Add your message handler code here
	// Do not call CWnd::OnPaint() for painting messages
}

int COpenGLControl::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	// TODO:  Add your specialized creation code here
	oglInitialize();

	return 0;
}
void COpenGLControl::OnDraw(CDC *pDC)
{

	glMatrixMode(GL_PROJECTION);

	glLoadIdentity();

	// Set our current view perspective
	gluOrtho2D( -m_fZoomE*m_fAspectRatio, m_fZoomE*m_fAspectRatio, -m_fZoomE, m_fZoomE );
	glTranslatef(m_fPosX, m_fPosY, 0.0f);
	
	// Model view
	glMatrixMode(GL_MODELVIEW);

	//glLoadIdentity();
	//glTranslatef(0.0f, 0.0f, -m_fZoom);
	//glRotatef(m_fRotX, 1.0f, 0.0f, 0.0f);
	//glRotatef(m_fRotY, 0.0f, 1.0f, 0.0f);
}

void COpenGLControl::OnTimer(UINT_PTR nIDEvent)
{
//	int i;

   switch (nIDEvent)
   {
      case 1:
      {
		  float fps;
		  _ftime_s( &this->frameTime[this->frameCount&0x07] );
		  if ( this->frameCount < 8 ) {
			fps = 0;
		  } else {
			  int cur = this->frameCount & 0x07;
			  int prev = (this->frameCount-7) & 0x07;
			  fps = 8.0f/max(0.001f, (float)((this->frameTime[cur].time - this->frameTime[prev].time) + (this->frameTime[cur].millitm - this->frameTime[prev].millitm)*0.001f));
		  }
		  this->frameCount++;

		  if ( this->m_AgentMirror )
			this->m_AgentMirror->step();

         // Clear color and depth buffer bits
         glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		 // Step simulation
//		 if ( m_Sim ) {
//			 for ( i=0; i<m_simSpeed; i++ )
//				m_Sim->Step();
//		 }


		 if ( m_Vis ) {
			 // update status
			 m_Vis->statusClear();
			 m_Vis->statusPrintLn( NULL, "FPS: %.2f", fps );
			 m_Vis->statusPrintLn( NULL, "Frame: %d", this->frameCount );
			 m_Vis->statusPrintLn( NULL, "Mouse Loc: %.3f, %.3f", this->mouseX, this->mouseY );
		//	 if ( m_Sim )
		//		  m_Vis->statusPrintLn( NULL, "SimTime: %.3f", m_Sim->getSimTime()*0.001f );

			 // PreDraw 
			 m_Vis->PreDraw();

			 // Draw OpenGL scene
			 //oglDrawScene();

			 // Draw
			 m_Vis->Draw();

			 // Swap buffers
			 SwapBuffers(hdc);
		 }

         break;
      }

      default:
         break;
   }

   CWnd::OnTimer(nIDEvent);
}

void COpenGLControl::OnSize(UINT nType, int cx, int cy)
{
   CWnd::OnSize(nType, cx, cy);

   if (0 >= cx || 0 >= cy || nType == SIZE_MINIMIZED) return;

   // Map the OpenGL coordinates.
   glViewport(0, 0, cx, cy);

   m_fViewH = (float)cy;
   m_fViewW = (float)cx;
   m_fAspectRatio = ((float)cx)/cy;
   
   // Projection view
   //glMatrixMode(GL_PROJECTION);

   //glLoadIdentity();

   // Set our current view perspective
   //gluPerspective(35.0f, (float)cx / (float)cy, 0.01f, 2000.0f);
   
   // Model view
   //glMatrixMode(GL_MODELVIEW);

   switch (nType)
	{
	   // If window resize token is "maximize"
	   case SIZE_MAXIMIZED:
	   {
		  // Get the current window rect
		  GetWindowRect(m_rect);

		  // Move the window accordingly
		  MoveWindow(6, 6, cx - 14, cy - 14);

		  // Get the new window rect
		  GetWindowRect(m_rect);

		  // Store our old window as the new rect
		  m_oldWindow = m_rect;

		  break;
	   }

	   // If window resize token is "restore"
	   case SIZE_RESTORED:
	   {
		  // If the window is currently maximized
		  if (m_bIsMaximized)
		  {
			 // Get the current window rect
			 GetWindowRect(m_rect);

			 // Move the window accordingly (to our stored old window)
			 MoveWindow(m_oldWindow.left,
						m_oldWindow.top - 18,
						m_originalRect.Width() - 4,
						m_originalRect.Height() - 4);

			 // Get the new window rect
			 GetWindowRect(m_rect);

			 // Store our old window as the new rect
			 m_oldWindow = m_rect;
		  }

		  break;
	   }
	}

	OnDraw(NULL);

}

void COpenGLControl::OnMouseMove(UINT nFlags, CPoint point)
{
   int diffX = (int)(point.x - m_fLastX);
   int diffY = (int)(point.y - m_fLastY);
   m_fLastX  = (float)point.x;
   m_fLastY  = (float)point.y;

   // rotate
   /*if (nFlags & MK_LBUTTON)
   {
      m_fRotX += (float)0.5f * diffY;

      if ((m_fRotX > 360.0f) || (m_fRotX < -360.0f))
      {
         m_fRotX = 0.0f;
      }

      m_fRotY += (float)0.5f * diffX;

      if ((m_fRotY > 360.0f) || (m_fRotY < -360.0f))
      {
         m_fRotY = 0.0f;
      }
   }*/

   // zoom
    if (nFlags & MK_RBUTTON)
   {
/*	  
	   // TEMP
	   float mouseWX, mouseWY;
	   mouseWX = (m_fLastX - 0.5f*m_fViewW)/m_fViewH * 2 * m_fZoomE - m_fPosX;
	   mouseWY = -(m_fLastY/m_fViewH - 0.5f) * 2 * m_fZoomE - m_fPosY;

	   m_Sim->_setAvatarPos( mouseWX, mouseWY );
*/
	   float oldX, oldY;
	   float newX, newY;
	   float lastZoomE = m_fZoomE;
       m_fZoom -= (float)0.01f * diffY;
	   m_fZoomE = exp( m_fZoom );

	   oldX = (m_fRClickX - 0.5f*m_fViewW)/m_fViewH * 2 * lastZoomE;
	   newX = oldX * m_fZoomE/lastZoomE;
	   m_fPosX -= oldX - newX;
	   oldY = (m_fRClickY/m_fViewH - 0.5f) * 2 * lastZoomE;
	   newY = oldY * m_fZoomE/lastZoomE;
	   m_fPosY += oldY - newY;
   }

   // pan
   else if (nFlags & MK_LBUTTON)
   {
      m_fPosX += (diffX/m_fViewH) * 2 * m_fZoomE;
      m_fPosY -= (diffY/m_fViewH) * 2 * m_fZoomE;
   }

	UpdateViewport();

	this->mouseX = (m_fLastX - 0.5f*m_fViewW)/m_fViewH * 2 * m_fZoomE - m_fPosX;
	this->mouseY = -(m_fLastY/m_fViewH - 0.5f) * 2 * m_fZoomE - m_fPosY;

   OnDraw(NULL);

   CWnd::OnMouseMove(nFlags, point);

}

void COpenGLControl::OnRButtonDown(UINT nFlags, CPoint point)
{
	// TODO: Add your message handler code here and/or call default
	m_fRClickX = (float)point.x;
	m_fRClickY = (float)point.y;

	CWnd::OnRButtonDown(nFlags, point);
}
