
#pragma once

// ThinSocket.h : header file
//



/////////////////////////////////////////////////////////////////////////////
// ThinSocket command target

class ThinSocket : public CAsyncSocket
{
// Attributes
public:

// Operations
public:
	ThinSocket();
	virtual ~ThinSocket();

// Overrides
public:
	void SetParentDlg(CDialog *pDlg);
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(ThinSocket)
	public:
	virtual void OnAccept(int nErrorCode);
	virtual void OnClose(int nErrorCode);
	virtual void OnConnect(int nErrorCode);
	virtual void OnOutOfBandData(int nErrorCode);
	virtual void OnReceive(int nErrorCode);
	virtual void OnSend(int nErrorCode);
	//}}AFX_VIRTUAL

	// Generated message map functions
	//{{AFX_MSG(ThinSocket)
		// NOTE - the ClassWizard will add and remove member functions here.
	//}}AFX_MSG

// Implementation
protected:
private:
	CDialog * m_pDlg;
};
