// MainDlg.h : interface of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once
#include <atlcrack.h>
#include <atlctrlx.h>
#include <vector>
#include "disk.h"

class CTickCount
{
public:
	CTickCount() { start(); }

	void start() { m_dwStartTime = GetTickCount(); }
	DWORD elapsed() const { return GetTickCount() - m_dwStartTime;  }
	DWORD restart()
	{
		DWORD dwElapsed = elapsed();
		start();
		return dwElapsed;
	}

private:
	DWORD m_dwStartTime;
};

class CMainDlg : public CDialogImpl<CMainDlg>, public CUpdateUI<CMainDlg>,
		public CMessageFilter, public CIdleHandler,
	public CWinDataExchange<CMainDlg>
{
public:
	enum { IDD = IDD_MAINDLG }; 

	CComboBoxEx m_wndDevice; 
	CString m_strImageFile; 
	CProgressBarCtrl m_wndProgress; 
	CComboBox m_wndHashType; 
	CString m_strHashLabel;
	CMultiPaneStatusBarCtrl m_wndStatusBar; 
	BOOL m_bPartition; 

	CMainDlg(LPCTSTR lpszFileLocation);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	virtual BOOL OnIdle();

	BEGIN_UPDATE_UI_MAP(CMainDlg)
		UPDATE_ELEMENT(IDC_READ, UPDUI_CHILDWINDOW)
		UPDATE_ELEMENT(IDC_WRITE, UPDUI_CHILDWINDOW)
		UPDATE_ELEMENT(IDC_VERIFY_ONLY, UPDUI_CHILDWINDOW)
		UPDATE_ELEMENT(IDC_HASH_COPY, UPDUI_CHILDWINDOW)
		UPDATE_ELEMENT(IDC_HASH_GENERATE, UPDUI_CHILDWINDOW)
	END_UPDATE_UI_MAP()

	BEGIN_MSG_MAP(CMainDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MSG_WM_DEVICECHANGE(OnDeviceChange)
		MSG_WM_DELETEITEM(OnDeleteItem)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
		COMMAND_ID_HANDLER(IDC_ABORT, OnAbortClicked)
		COMMAND_HANDLER_EX(IDC_BROWSE, BN_CLICKED, OnBrowseClicked)
		COMMAND_HANDLER_EX(IDC_HASH_COPY, BN_CLICKED, OnHashCopyClicked)
		COMMAND_HANDLER_EX(IDC_HASH_GENERATE, BN_CLICKED, OnHashGenerateClicked)
		COMMAND_HANDLER_EX(IDC_READ, BN_CLICKED, OnReadClicked)
		COMMAND_HANDLER_EX(IDC_WRITE, BN_CLICKED, OnWriteClicked)
		COMMAND_HANDLER_EX(IDC_VERIFY_ONLY, BN_CLICKED, OnVerifyOnlyClicked)
		COMMAND_HANDLER_EX(IDC_HASH_TYPE, CBN_SELCHANGE, OnHashTypeSelChange)
		COMMAND_HANDLER_EX(IDC_IMAGEFILE, EN_KILLFOCUS, OnImageFileKillFocus)
		COMMAND_HANDLER_EX(IDC_DEVICE, CBN_SELCHANGE, OnDeviceSelChange)
	END_MSG_MAP()

	BEGIN_DDX_MAP(CMainDlg)
		DDX_CONTROL_HANDLE(IDC_DEVICE, m_wndDevice)
		DDX_TEXT(IDC_IMAGEFILE, m_strImageFile)
		DDX_CONTROL_HANDLE(IDC_PROGRESS, m_wndProgress)
		DDX_CONTROL_HANDLE(IDC_HASH_TYPE, m_wndHashType)
		DDX_TEXT(IDC_HASH_LABEL, m_strHashLabel)
		DDX_CHECK(IDC_PARTITION, m_bPartition)
	END_DDX_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnAbortClicked(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	void CloseDialog(int nVal);
	LRESULT OnBrowseClicked(WORD wNotifyCode, WORD wID, HWND hWndCtl);
	LRESULT OnHashCopyClicked(WORD wNotifyCode, WORD wID, HWND hWndCtl);
	LRESULT OnHashGenerateClicked(WORD wNotifyCode, WORD wID, HWND hWndCtl);
	LRESULT OnReadClicked(WORD wNotifyCode, WORD wID, HWND hWndCtl);
	LRESULT OnWriteClicked(WORD wNotifyCode, WORD wID, HWND hWndCtl);
	LRESULT OnVerifyOnlyClicked(WORD wNotifyCode, WORD wID, HWND hWndCtl);

private:
	CBitmap m_BrowseBitmap;

	enum Status { STATUS_IDLE = 0, STATUS_READING, STATUS_WRITING, STATUS_VERIFYING, STATUS_EXIT, STATUS_CANCELED };

	static const unsigned short ONE_SEC_IN_MS = 1000;
	uint64_t m_SectorSize;
	int m_Status;
	CTickCount m_UpdateTimer, m_ElapsedTimer;
	CString m_strHomeDir;
	std::vector<CDiskInfo> m_Disks;

	void GetLogicalDrives();
	void GetPhysicalDisks();
	void RefreshPhysicalDisks();
	void InitDeviceList();
	void SetReadWriteButtonState();
	void SaveSettings();
	void LoadSettings();
	void InitializeHomeDir();
	void UpdateHashControls();	
	void GenerateHash(LPCTSTR filename, int hashish);
	void CopyText(const CString& strText);
	bool IsSameDrive(TCHAR nVolume);
	CString GetFullFilePath() const;
	bool IsReatableFile() const;
	void HandleMessages();
	void StartTimer();
	void StopTimer();
	void UpdateElapsedTime(uint64_t position, uint64_t total);
	void AddHashType(LPCTSTR lpszHashName, int nHashTag);
	bool HashFile(int hash_type, CString& strHashResult, LPCTSTR lpszFileName);
	void SetButtonBitmap(UINT nButtonID, UINT nImageID);
	void UpdateSpeed(double speed);
	static uint8_t* NewDeviceItemData(uint8_t nDeviceType, LPCTSTR lpszDevicePath);
	std::vector<CVolume> OpenCurrentVolumes();
	CAtlFile OpenDevice(DWORD dwDevice, DWORD dwAccess);
	bool UnmountVolumes(std::vector<CVolume>& volumes);

	int ShowMessage(LPCTSTR message, UINT type) const
	{
		return AtlMessageBox(m_hWnd, message, IDR_MAINFRAME, type);
	}
	CDiskInfo* FindDiskInfo(DWORD nDeviceNumber);
	void GetImageFileSize(HANDLE hFile);
	void GetDeviceSize();
	void AddDrive(TCHAR nDrive);

public:
	LRESULT OnHashTypeSelChange(WORD wNotifyCode, WORD wID, HWND hWndCtl);
	LRESULT OnDeviceChange(UINT uEvent, DWORD dwEventData);
//	LRESULT OnDevModeChange(LPCTSTR lpszDeviceName);
	LRESULT OnDeleteItem(UINT ControlID, LPDELETEITEMSTRUCT lpDeleteItem);
	LRESULT OnImageFileKillFocus(WORD wNotifyCode, WORD wID, HWND hWndCtl);
	LRESULT OnDeviceSelChange(WORD wNotifyCode, WORD wID, HWND hWndCtl);
};
