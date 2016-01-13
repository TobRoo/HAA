#pragma once
#include "afxwin.h"

#include <gl/gl.h>
#include <gl/glu.h>

#include <sys/timeb.h>
#include <map>

class Visualize;
class AgentMirror;
//class Simulation;

class COpenGLControl :
	public CWnd
{
public:
	COpenGLControl(void);
	~COpenGLControl(void);

public:
	/******************/
	/* PUBLIC MEMBERS */
	/******************/

	// Viewport
	float viewportLeft, viewportRight, viewportBottom, viewportTop;

	void setView( float x, float y, float zoom = -1 );
	float getZoomE() { return m_fZoomE; };

	// Timer
	UINT_PTR m_unpTimer;
	bool m_bIsMaximized;

	void oglCreate(CRect rect, CWnd *parent );
	void oglInitialize(void);

	void oglDrawScene(void);

	GLvoid glPrint(const char *fmt, ...);
	int	setFontHeight( int height );

	void resetSimulation();
	void setSimulationSpeed( int speed ) { m_simSpeed = speed; };

	CRect *getWindowRect() { return &m_rect; };
	float getMouseX() { return mouseX; };
	float getMouseY() { return mouseY; };

	int getFontHeight() { return this->fontHeight; };

private:
	/*******************/
	/* PRIVATE MEMBERS */
	/*******************/
	// Window information
	CWnd    *hWnd;
	HDC     hdc;
	HGLRC   hrc;
	int     m_nPixelFormat;
	CRect   m_rect;
	CRect   m_oldWindow;
	CRect   m_originalRect;

	float m_fViewH;
	float m_fViewW;
	float m_fAspectRatio;

	float m_fPosX;    // X position of model in camera view
	float m_fPosY;    // Y position of model in camera view
	float m_fLastX;
	float m_fLastY;
	float m_fRClickX, m_fRClickY;
	float m_fZoom;   // Zoom on model in camera view
	float m_fZoomE;  // exp zoom to smooth zooming scale
	float m_fRotX;    // Rotation on model in camera view
	float m_fRotY;
	
	float mouseX, mouseY; // stores current mouse loc

	int frameCount;
	_timeb frameTime[8];

	// font
	int fontHeight;
	std::map<int,HFONT> font;
//	std::map<int,HFONT> oldfont;
	std::map<int,GLuint> fontBase;
	GLvoid BuildFont( int height );
	GLvoid KillFont(GLvoid);

	void UpdateViewport();

	// TEMP
	
	float target[64*64], source[64*64], transformed[128*128], scale[1000];
	
public:
	Visualize *m_Vis;

	AgentMirror *m_AgentMirror;

	void setAgentMirror( AgentMirror * am ) { this->m_AgentMirror = am; };

	//Simulation *m_Sim;
	int m_simSpeed;

public:
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnDraw(CDC *pDC);

	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);

public:
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
};
