// ThinSocket.cpp : implementation file
//

#include "stdafx.h"
#include "ThinSocket.h"

#include "AutonomicGUIDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// MyEchoSocket

ThinSocket::ThinSocket()
{
}

ThinSocket::~ThinSocket()
{
}


// Do not edit the following lines, which are needed by ClassWizard.
#if 0
BEGIN_MESSAGE_MAP(ThinSocket, CAsyncSocket)
	//{{AFX_MSG_MAP(ThinSocket)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()
#endif	// 0

/////////////////////////////////////////////////////////////////////////////
// ThinSocket member functions

void ThinSocket::OnAccept(int nErrorCode) 
{
	// TODO: Add your specialized code here and/or call the base class
	if(nErrorCode==0)
	{
		((CAutonomicGUIDlg*)m_pDlg)->socketOnAccept();
	}
	CAsyncSocket::OnAccept(nErrorCode);
}

void ThinSocket::OnClose(int nErrorCode) 
{
	// TODO: Add your specialized code here and/or call the base class
	if(nErrorCode==0)
	{
		((CAutonomicGUIDlg*)m_pDlg)->socketOnClose(); 
	}

	
	CAsyncSocket::OnClose(nErrorCode);
}

void ThinSocket::OnConnect(int nErrorCode) 
{
	// TODO: Add your specialized code here and/or call the base class
	if(nErrorCode==0)
	{
		((CAutonomicGUIDlg*)m_pDlg)->socketOnConnect(); 
	}
	
	CAsyncSocket::OnConnect(nErrorCode);
}

void ThinSocket::OnOutOfBandData(int nErrorCode) 
{
	// TODO: Add your specialized code here and/or call the base class
	
	CAsyncSocket::OnOutOfBandData(nErrorCode);
}

void ThinSocket::OnReceive(int nErrorCode) 
{
	// TODO: Add your specialized code here and/or call the base class
	if(nErrorCode==0)
	{
		((CAutonomicGUIDlg*)m_pDlg)->socketOnReceive(); 
	}
	
	CAsyncSocket::OnReceive(nErrorCode);
}

void ThinSocket::OnSend(int nErrorCode) 
{
	// TODO: Add your specialized code here and/or call the base class
	if(nErrorCode==0)
	{
		((CAutonomicGUIDlg*)m_pDlg)->socketOnSend(); 
	}

	CAsyncSocket::OnSend(nErrorCode);
}

void ThinSocket::SetParentDlg(CDialog *pDlg)
{
	m_pDlg=pDlg;
}
