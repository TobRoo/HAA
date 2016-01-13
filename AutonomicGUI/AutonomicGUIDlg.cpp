// AutonomicGUIDlg.cpp : implementation file
//

#include "stdafx.h"
#include "AutonomicGUI.h"
#include "AutonomicGUIDlg.h"

#include "AgentMirrorGUI.h"

#include "OpenGLControl.h"
#include "..\\autonomic\\DDB.h"
//#include "Simulation.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif 

// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	enum { IDD = IDD_ABOUTBOX };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

// Implementation
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
END_MESSAGE_MAP()


// CAutonomicGUIDlg dialog




CAutonomicGUIDlg::CAutonomicGUIDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CAutonomicGUIDlg::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	#ifdef _DEBUG
	_crtBreakAlloc = -1; // break on Nth memory allocation
	#endif

	m_oglWindow = new COpenGLControl();
	m_AgentMirrorGUI = NULL;

}
CAutonomicGUIDlg::~CAutonomicGUIDlg() {

	delete m_oglWindow;
}

void CAutonomicGUIDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAutonomicGUIDlg, CDialog)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	//}}AFX_MSG_MAP
	ON_WM_SIZE()
	ON_NOTIFY(NM_RELEASEDCAPTURE, IDC_SPEEDSLIDER, &CAutonomicGUIDlg::OnNMReleasedcaptureSpeedslider)
	ON_BN_CLICKED(IDPAUSE, &CAutonomicGUIDlg::OnBnClickedPause)
	ON_BN_CLICKED(IDRESET, &CAutonomicGUIDlg::OnBnClickedReset)
	ON_BN_CLICKED(IDNEXTDUMP, &CAutonomicGUIDlg::OnBnClickedNextdump)
	ON_BN_CLICKED(IDPREVDUMP, &CAutonomicGUIDlg::OnBnClickedPrevdump)
	ON_BN_CLICKED(IDTOGGLENET, &CAutonomicGUIDlg::OnBnClickedTogglenet)
	ON_BN_CLICKED(IDOK, &CAutonomicGUIDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDC_DEBUG1, &CAutonomicGUIDlg::OnBnClickedDebug1)
	ON_BN_CLICKED(IDC_DEBUG2, &CAutonomicGUIDlg::OnBnClickedDebug2)
	ON_BN_CLICKED(IDC_DEBUG3, &CAutonomicGUIDlg::OnBnClickedDebug3)
	ON_BN_CLICKED(IDC_DEBUG4, &CAutonomicGUIDlg::OnBnClickedDebug4)
	ON_BN_CLICKED(IDC_DEBUG5, &CAutonomicGUIDlg::OnBnClickedDebug5)
	ON_BN_CLICKED(IDC_DEBUG6, &CAutonomicGUIDlg::OnBnClickedDebug6)
	ON_WM_CLOSE()
	ON_NOTIFY(TVN_SELCHANGED, IDC_ENTITIES, &CAutonomicGUIDlg::OnTvnSelchangedEntities)
	ON_NOTIFY(NM_RCLICK, IDC_ENTITIES, &CAutonomicGUIDlg::OnNMRclickEntities)
	ON_NOTIFY(NM_CLICK, IDC_ENTITIES, &CAutonomicGUIDlg::OnNMClickEntities)
	ON_NOTIFY(TVN_KEYDOWN, IDC_ENTITIES, &CAutonomicGUIDlg::OnTvnKeydownEntities)
END_MESSAGE_MAP()


// CAutonomicGUIDlg message handlers

BOOL CAutonomicGUIDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	// TODO: Add extra initialization here
	
	CEdit *hostIpText = (CEdit*)GetDlgItem(IDC_EDIT_HOSTIP);
	hostIpText->SetWindowText( L"127.0.0.1" );
	CEdit *hostPortText = (CEdit*)GetDlgItem(IDC_EDIT_HOSTPORT);
	hostPortText->SetWindowText( L"50000" );

	CRect rect;

	// Get size and position of the picture control
	GetDlgItem(IDC_MAPGL)->GetWindowRect(rect);

	// Convert screen coordinates to client coordinates
	ScreenToClient(rect);

	// Create OpenGL Control window
	m_oglWindow->oglCreate(rect, this);

	// Set up the OpenGL Window's timer to render
	m_oglWindow->m_unpTimer = m_oglWindow->SetTimer(1, 200, 0);

	// Set up the tree control
	CTreeCtrl *tree = (CTreeCtrl *)GetDlgItem(IDC_ENTITIES);
	tree->InsertItem( L"Agents" );
	HTREEITEM catDDB = tree->InsertItem( L"DDB" );
	tree->InsertItem( L"Landmarks", catDDB );

	m_socketConnected = 0;

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CAutonomicGUIDlg::socketOnAccept() {

}

void CAutonomicGUIDlg::socketOnConnect() {
	CButton *btn = (CButton*)GetDlgItem(IDTOGGLENET);
	btn->SetWindowText( L"Disconnect" );

	m_socketConnected = true;

	// prepare AgentMirrorGUI
	CTreeCtrl *tree = (CTreeCtrl*)GetDlgItem(IDC_ENTITIES);
	CEdit *absText = (CEdit*)GetDlgItem(IDC_ABSTRACT);
	m_AgentMirrorGUI = new AgentMirrorGUI( m_socket, m_oglWindow->m_Vis, tree, absText );
	if ( !m_AgentMirrorGUI->configure() ) 
		m_AgentMirrorGUI->start();

	this->m_oglWindow->setAgentMirror( m_AgentMirrorGUI );
}

void CAutonomicGUIDlg::socketOnClose() {
	CButton *btn = (CButton*)GetDlgItem(IDTOGGLENET);
	btn->SetWindowText( L"Connect" );

	m_socket->Close();
	delete m_socket;
	m_socketConnected = false;

	// kill AgentMirrorGUI
	delete m_AgentMirrorGUI;
	m_AgentMirrorGUI = NULL;
	this->m_oglWindow->setAgentMirror( NULL );
}

void CAutonomicGUIDlg::socketOnSend() {

}

void CAutonomicGUIDlg::socketOnReceive() {
	if ( m_AgentMirrorGUI )
		m_AgentMirrorGUI->conReceiveData();
}

void CAutonomicGUIDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CAutonomicGUIDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CAutonomicGUIDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


void CAutonomicGUIDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);

	switch (nType)
   {
      case SIZE_RESTORED:
      {
         if (m_oglWindow->m_bIsMaximized)
         {
            m_oglWindow->OnSize(nType, cx, cy);
            m_oglWindow->m_bIsMaximized = false;
         }

         break;
      }

      case SIZE_MAXIMIZED:
      {
         m_oglWindow->OnSize(nType, cx, cy);
         m_oglWindow->m_bIsMaximized = true;

         break;
      }
   }

}

void CAutonomicGUIDlg::OnNMReleasedcaptureSpeedslider(NMHDR *pNMHDR, LRESULT *pResult)
{
	// TODO: Add your control notification handler code here
	
	int pos = ((CSliderCtrl*)GetDlgItem(IDC_SPEEDSLIDER))->GetPos();

	m_oglWindow->setSimulationSpeed( pos/10 + 1 );

	*pResult = 0;
}

void CAutonomicGUIDlg::OnBnClickedPause()
{
//	m_oglWindow->m_Sim->TogglePause();
}

void CAutonomicGUIDlg::OnBnClickedReset()
{
//	m_oglWindow->resetSimulation();
}

void CAutonomicGUIDlg::OnBnClickedNextdump()
{
//	m_oglWindow->m_Sim->loadNextDump();
}

void CAutonomicGUIDlg::OnBnClickedPrevdump()
{
//	m_oglWindow->m_Sim->loadPrevDump();
}

void CAutonomicGUIDlg::OnBnClickedTogglenet()
{
	CButton *btn = (CButton*)GetDlgItem(IDTOGGLENET);
		
	if ( m_socketConnected == 1 ) { // connected
		socketOnClose();
	} else {
		if ( m_socketConnected == -1 ) { // trying to connect
			m_socket->Close();
			delete m_socket;
		}

		WCHAR ipBuf[64];
		WCHAR portBuf[64];
		int port;
		CEdit *hostIpText = (CEdit*)GetDlgItem(IDC_EDIT_HOSTIP);
		hostIpText->GetWindowText( ipBuf, 64 );
		CEdit *hostPortText = (CEdit*)GetDlgItem(IDC_EDIT_HOSTPORT);
		hostPortText->GetWindowText( portBuf, 64 );
		swscanf_s( portBuf, L"%d", &port );

		m_socket = new ThinSocket();
		
		m_socket->SetParentDlg( this );

		m_socket->Create();
		m_socket->Connect( ipBuf, port );
		m_socketConnected = -1;
			
		btn->SetWindowText( L"Retry" );
	}
}

void CAutonomicGUIDlg::OnBnClickedOk()
{
	if ( m_socketConnected != 0 ) {
		m_socket->Close();
		delete m_socket;
		m_socketConnected = 0;
	}

	OnOK();
}

void VisPOGDump( Visualize *vis, char *pogFile, float x, float y, float w, float h ) {
	// load map
	FILE *fp;
	FIMAGE *fimage;
	float cell;
	int rows, cols, r, c;

	fopen_s( &fp, pogFile, "r" );

	cols = int(w*10 + 0.5f);
	rows = int(h*10 + 0.5f);
	fimage = NewImage( rows, cols );

	for ( r = rows-1; r >= 0; r-- ) {
		for ( c = 0; c < cols; c++ ) {
			fscanf_s( fp, "%f", &cell );
			Px(fimage,r,c) = cell;
		}
	}

	fclose( fp );

	vis->newfImage( x, y, 0.1f, 0, 0, fimage );
}

void VisPFDump( Visualize *vis, char *pfFile, int ind ) {
	FILE *fp;
	long sec, ms;
	float x, y, r;
	int count = 0;
	float *xs, *ys, *rs;

	float colours[][3] = {  { 1, 0, 0 }, // red
							{ 0, 1, 0 }, // green
							{ 0, 0, 1 }, // blue
							{ 1, 1, 0 }, // yellow
							{ 0, 1, 1 }, // cyan
							{ 1, 0, 1 }, // magenta
							{ 1, 0.5f, 0 }, // orange
							{ 0, 1, 0.5f }, // turquoise
							{ 0.5f, 0, 1 }, // purple
						 };

	fopen_s( &fp, pfFile, "r" );

	// first pass to count
	while ( 1 ) {
		if ( 5 != fscanf_s( fp, "%d.%d	%f	%f	%f", &sec, &ms, &x, &y, &r ) )
			break;

		count++;
	}

	fseek( fp, 0, 0 );

	xs = (float *)malloc(count*sizeof(float));
	ys = (float *)malloc(count*sizeof(float));
	rs = (float *)malloc(count*sizeof(float));
	
	int i = 0;
	while ( 1 ) {
		if ( 5 != fscanf_s( fp, "%d.%d	%f	%f	%f", &sec, &ms, &x, &y, &r ) )
			break;
		
		xs[i] = x;
		ys[i] = y;
		rs[i] = r;
		i++;
	}

	fclose( fp );

	int path = vis->newPath( count, xs, ys );
	vis->newStaticObject( 0, 0, 0, 1, path, colours[ind], 1 );

	free( xs );
	free( ys );
	free( rs );

}

void VisDump( Visualize *vis, int loadPaths ) {
	
	WCHAR cwd[1024];
	GetCurrentDirectory( 1024, cwd );

	// open dialog
	WCHAR *strFilter = L"Text Files (*.txt)|*.txt|All Files (*.*)|*.*||";

	CFileDialog FileDlg(TRUE, L".txt", NULL, 0, strFilter);

	if( FileDlg.DoModal() != IDOK )
		return;

	int i;
	bool firstPF = true;
	float x, y, w, h;
	char fileName[1024];

	char buf[1024];
	FILE *xData;
	sprintf_s( buf, 1024, "%ws", FileDlg.GetPathName() );
	fopen_s( &xData, buf, "r" );

	int avInd = 0;
	while ( fgets( buf, 1024, xData ) ) {
		if ( !strncmp( buf + 15, "POG_DUMP", 8 ) ) {
			sscanf_s( buf + 61, "x %f y %f w %f h %f", &x, &y, &w, &h );
			i = 100;
			while ( strncmp( buf+i, "dump", 4 ) ) i++;
			buf[strlen(buf)-1] = 0;
			sprintf_s( fileName, 1024, "%s", buf + i );
		} else if ( !strncmp( buf + 15, "PARTICLE_FILTER_DUMP", 20 ) ) {
			if ( firstPF ) {
				// parse pog now
				VisPOGDump( vis, fileName, x, y, w, h );
				firstPF = false;
				if ( !loadPaths )
					break; // we're done
			}
			// parse pf
			buf[strlen(buf)-1] = 0;
			i = 100;
			while ( strncmp( buf+i, "dump", 4 ) ) i++;
			sprintf_s( fileName, 1024, "%s", buf + i );
			VisPFDump( vis, fileName, avInd );
			avInd++;
		}
	}

	fclose( xData );

	SetCurrentDirectory( cwd );

}

void CAutonomicGUIDlg::OnBnClickedDebug1()
{
	// toggle avatarExtras, landmarks, particle filters, console
//	m_oglWindow->m_Sim->setVisibility( -1, -1, 2, 2, 2, 2 );

}

void CAutonomicGUIDlg::OnBnClickedDebug2()
{
	// clear vis
	m_oglWindow->m_Vis->ClearAll();
}

void CAutonomicGUIDlg::OnBnClickedDebug3()
{
	// load small arena
	m_oglWindow->setView( -0.967f, -3.565f, 1.49f );

	m_oglWindow->m_Vis->loadPathFile( "data\\paths\\layout1.path" );
	m_oglWindow->m_Vis->loadPathFile( "data\\paths\\boundary1.path" );
	m_oglWindow->m_Vis->loadPathFile( "data\\paths\\simExtraWalls.path" );

}

void CAutonomicGUIDlg::OnBnClickedDebug4()
{
	// load large arena
	m_oglWindow->setView( -9.109f, -9.847f, 2.34f );

	m_oglWindow->m_Vis->loadPathFile( "data\\paths\\layoutLarge.path" );
	m_oglWindow->m_Vis->loadPathFile( "data\\paths\\boundaryLarge.path" );

}

void CAutonomicGUIDlg::OnBnClickedDebug5()
{
	// TODO: Add your control notification handler code here
	
	// load dump no paths
	VisDump( m_oglWindow->m_Vis, 0 );
}

void CAutonomicGUIDlg::OnBnClickedDebug6()
{
	// load dump with paths
	VisDump( m_oglWindow->m_Vis, 1 );
	
}

void CAutonomicGUIDlg::OnClose()
{
	// TODO: Add your message handler code here and/or call default
	if ( m_socketConnected != 0 ) { // clean up socket
		m_socket->Close();
		delete m_socket;
	}
	
	if ( m_AgentMirrorGUI != NULL )
		delete m_AgentMirrorGUI;

	CDialog::OnClose();
}

void CAutonomicGUIDlg::OnTvnSelchangedEntities(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMTREEVIEW pNMTreeView = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);
	// TODO: Add your control notification handler code here

	if ( m_AgentMirrorGUI != NULL ) {
		m_AgentMirrorGUI->treeSelectionChanged( pNMTreeView->itemNew.hItem );
	} else {
		CEdit *absText = (CEdit*)GetDlgItem(IDC_ABSTRACT);
		absText->SetWindowText( L"Not Connected" );
	}

	*pResult = 0;
}

void CAutonomicGUIDlg::OnNMClickEntities(NMHDR *pNMHDR, LRESULT *pResult)
{
	// TODO: Add your control notification handler code here
	if ( m_AgentMirrorGUI != NULL ) {
		CPoint pt, clientpt; 
		CRect rect;
		TVHITTESTINFO hti;
		CTreeCtrl *tree = (CTreeCtrl *)GetDlgItem(IDC_ENTITIES);
		VERIFY(GetCursorPos( &pt )); 
		tree->GetWindowRect( &rect );
		hti.pt = pt;
		hti.pt.x -= rect.left;
		hti.pt.y -= rect.top;
		HTREEITEM hItem = tree->HitTest( &hti );
		if ( hti.flags & TVHT_ONITEMSTATEICON ) { // clicked checkbox
			m_AgentMirrorGUI->treeCheckChanged( hItem );
		}
	} else {
		CEdit *absText = (CEdit*)GetDlgItem(IDC_ABSTRACT);
		absText->SetWindowText( L"Not Connected" );
	}

	*pResult = 0;
}

void CAutonomicGUIDlg::OnNMRclickEntities(NMHDR *pNMHDR, LRESULT *pResult)
{
	// TODO: Add your control notification handler code here

	if ( m_AgentMirrorGUI != NULL ) {
		CPoint pt, clientpt; 
		CRect rect;
		CTreeCtrl *tree = (CTreeCtrl *)GetDlgItem(IDC_ENTITIES);
		VERIFY(GetCursorPos( &pt )); 
		tree->GetWindowRect( &rect );
		clientpt = pt;
		clientpt.x -= rect.left;
		clientpt.y -= rect.top;
		HTREEITEM hItem = tree->HitTest(clientpt);

		m_AgentMirrorGUI->treeRClick( hItem, &pt );
	} else {
		CEdit *absText = (CEdit*)GetDlgItem(IDC_ABSTRACT);
		absText->SetWindowText( L"Not Connected" );
	}

	*pResult = 0;
}

void CAutonomicGUIDlg::OnTvnKeydownEntities(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMTVKEYDOWN pTVKeyDown = reinterpret_cast<LPNMTVKEYDOWN>(pNMHDR);
	// TODO: Add your control notification handler code here

	if ( pTVKeyDown->wVKey == VK_SPACE && m_AgentMirrorGUI != NULL ) {
		CTreeCtrl *tree = (CTreeCtrl *)GetDlgItem(IDC_ENTITIES);

		// Determine the selected tree item.
		HTREEITEM item = tree->GetSelectedItem();

		if ( item != 0 ) {
			// Set the tree item state to the state it should have after
			// processing of the key down.
			tree->SetCheck( item, tree->GetCheck( item ) );

			// Perform the callback. The tree item state had to be set before
			// because a callback most likely will check the state of the tree
			// item.
			m_AgentMirrorGUI->treeCheckChanged( item );

			// Set the tree item state back to the state it has before. This
			// because whether or not TRUE is being returned, the default
			// implementation of SysTreeView32 is always being processed for
			// a key down!
			tree->SetCheck( item, tree->GetCheck( item ) );
		}
	}

	*pResult = 0;
}

