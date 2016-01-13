// AutonomicGUIDlg.h : header file
//

#pragma once

#include "resource.h"

#include "ThinSocket.h"

class COpenGLControl;

class AgentMirrorGUI;

// CAutonomicGUIDlg dialog
class CAutonomicGUIDlg : public CDialog
{
// Construction
public:
	CAutonomicGUIDlg(CWnd* pParent = NULL);	// standard constructor
	~CAutonomicGUIDlg();

	// socket stuff
	void socketOnAccept();
	void socketOnConnect();
	void socketOnClose();
	void socketOnSend();
	void socketOnReceive();

// Dialog Data
	enum { IDD = IDD_AUTONOMICGUI_DIALOG };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support

private:
	COpenGLControl *m_oglWindow;
	
	UINT_PTR m_unpTimer;

	int m_socketConnected;
	ThinSocket *m_socket;

	AgentMirrorGUI *m_AgentMirrorGUI;

// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnNMReleasedcaptureSpeedslider(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnBnClickedPause();
	afx_msg void OnBnClickedReset();
	afx_msg void OnBnClickedNextdump();
	afx_msg void OnBnClickedPrevdump();
	afx_msg void OnBnClickedTogglenet();
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedDebug1();
	afx_msg void OnBnClickedDebug2();
	afx_msg void OnBnClickedDebug3();
	afx_msg void OnBnClickedDebug4();
	afx_msg void OnBnClickedDebug5();
	afx_msg void OnBnClickedDebug6();
	afx_msg void OnClose();
	afx_msg void OnTvnSelchangedEntities(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnNMRclickEntities(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnNMClickEntities(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnTvnKeydownEntities(NMHDR *pNMHDR, LRESULT *pResult);
};
