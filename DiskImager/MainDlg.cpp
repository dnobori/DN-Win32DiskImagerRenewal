// MainDlg.cpp : implementation of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"

#include <ShlObj.h>
#include <Dbt.h>
#include <atlcomcli.h>
#include <objidl.h>
#include <atlfile.h>
#include <atlimage.h>
#include <tuple>
#include <algorithm>
#include "MainDlg.h"
#include "disk.h"

CMainDlg::CMainDlg(LPCTSTR lpszFileLocation)
	: m_bPartition(FALSE)
{
	if (lpszFileLocation)
	{
		TCHAR szModulePath[MAX_PATH];
		TCHAR szFilePath[MAX_PATH];
		GetModuleFileName(NULL, szModulePath, MAX_PATH);
		PathRemoveFileSpec(szModulePath);
		PathCombine(szFilePath, szModulePath, lpszFileLocation);
		m_strImageFile = szFilePath;
	}
}

BOOL CMainDlg::PreTranslateMessage(MSG* pMsg)
{
	return CWindow::IsDialogMessage(pMsg);
}

BOOL CMainDlg::OnIdle()
{
	UIUpdateChildWindows();
	return FALSE;
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
	UINT num = 0;
	UINT size = 0;
	Gdiplus::ImageCodecInfo* pImageCodecInfo = NULL;
	Gdiplus::GetImageEncodersSize(&num, &size);
	if (size == 0) {
		return -1;
	}
	pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
	if (pImageCodecInfo == NULL) {
		return -1;
	}
	Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
	for (UINT j = 0; j < num; ++j) {
		if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
			*pClsid = pImageCodecInfo[j].Clsid;
			free(pImageCodecInfo);
			return j;
		}
	}
	free(pImageCodecInfo);
	return -1;
}

LRESULT CMainDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	// center the dialog on the screen
	CenterWindow();

	// set icons
	HICON hIcon = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON));
	SetIcon(hIcon, TRUE);
	HICON hIconSmall = AtlLoadIconImage(IDR_MAINFRAME, LR_DEFAULTCOLOR, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
	SetIcon(hIconSmall, FALSE);

	// register object for message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->AddMessageFilter(this);
	pLoop->AddIdleHandler(this);

	UIAddChildWindowContainer(m_hWnd);

	m_wndStatusBar.Create(m_hWnd, ATL_IDS_IDLEMESSAGE, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	int nPanes[] = { ID_DEFAULT_PANE, ID_ELAPSED_TIME };
	m_wndStatusBar.SetPanes(nPanes, _countof(nPanes), FALSE);
	m_wndStatusBar.SendMessage(WM_SIZE);
	m_wndStatusBar.SetSimple(TRUE);

	DoDataExchange(FALSE);

	m_wndDevice.ResetContent();
	GetPhysicalDisks();
	GetLogicalDrives();
	InitDeviceList();

	m_Status = STATUS_IDLE;
	m_wndProgress.SetPos(0);
	m_wndStatusBar.SetPaneText(ID_DEFAULT_PANE, _T("Waiting for a task."));
	// Add supported hash types.
	AddHashType(_T("None"), 0);
	AddHashType(_T("MD5"), CALG_MD5);
	AddHashType(_T("SHA1"), CALG_SHA1);
	AddHashType(_T("SHA256"), CALG_SHA_256);
	m_wndHashType.SetCurSel(0);
	UpdateHashControls();
	SetReadWriteButtonState();
	m_SectorSize = 0ul;

	SetButtonBitmap(IDC_BROWSE, IDB_BROWSE);
	
	LoadSettings();
	if (m_strHomeDir.IsEmpty()) 
	{
		InitializeHomeDir();
	}

	return TRUE;
}

LRESULT CMainDlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	SaveSettings();

	// unregister message filtering and idle updates
	CMessageLoop* pLoop = _Module.GetMessageLoop();
	ATLASSERT(pLoop != NULL);
	pLoop->RemoveMessageFilter(this);
	pLoop->RemoveIdleHandler(this);

	return 0;
}

LRESULT CMainDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	CloseDialog(wID);
	return 0;
}

LRESULT CMainDlg::OnAbortClicked(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if ((m_Status == STATUS_READING) || (m_Status == STATUS_WRITING))
	{
		if(ShowMessage(_T("Canceling now will result in a corrupt destination.\n")
			_T("Are you sure you want to cancel?"), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2)==IDYES)
		{
			m_Status = STATUS_CANCELED;
		}
	}
	else if (m_Status == STATUS_VERIFYING)
	{
		if(ShowMessage(_T("Cancel Verify.\nAre you sure you want to cancel?"), 
			MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2)==IDYES)
		{
			m_Status = STATUS_CANCELED;
		}

	}

	return 0;
}

void CMainDlg::CloseDialog(int nVal)
{
	DestroyWindow();
	::PostQuitMessage(nVal);
}

static bool EndsWith(LPCTSTR lpszText, LPCTSTR lpszEnds)
{
	size_t nText = _tcslen(lpszText);
	size_t nEnds = _tcslen(lpszEnds);
	if (nText < nEnds) return false;

	return _tcscmp(lpszText - nEnds, lpszEnds) == 0;
}

LRESULT CMainDlg::OnBrowseClicked(WORD wNotifyCode, WORD wID, HWND hWndCtl)
{
	// Use the location of already entered file
	DoDataExchange(TRUE, IDC_IMAGEFILE);

	// See if there is a user-defined file extension.
	TCHAR szFileType[1024] = { 0 };
	const TCHAR szImageFilter[] = _T("Disk Images (*.img)\0*.img\0All Files\0*.*\0");
	DWORD dwSize = GetEnvironmentVariable(_T("DiskImagerFiles"), szFileType, _countof(szFileType));
	memcpy(szFileType + dwSize, szImageFilter, sizeof(szImageFilter));
	// create a generic FileDialog
	CFileDialog dialog(TRUE, NULL, NULL, OFN_HIDEREADONLY | OFN_EXPLORER | OFN_DONTADDTORECENT, szFileType);
	dialog.m_ofn.lpstrTitle = _T("Select a disk image");
	//if (PathFileExists(m_strImageFile))
	{
		_tcscpy(dialog.m_szFileName, m_strImageFile);
	}
	//else
	//{
	//	_tcscpy(dialog.m_szFileName, m_strHomeDir);
	//}

	if (dialog.DoModal(m_hWnd)==IDOK)
	{
		// selectedFiles returns a CStringList - we just want 1 filename,
		//	so use the zero'th element from that list as the filename
		m_strImageFile = dialog.m_szFileName;
		DoDataExchange(FALSE, IDC_IMAGEFILE);
		PathRemoveFileSpec(dialog.m_szFileName);
		m_strHomeDir = dialog.m_szFileName;

		CAtlFile file;
		SetDlgItemText(IDC_FILESIZE, NULL);
		if (SUCCEEDED(file.Create(m_strImageFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING)))
		{
			GetImageFileSize(file);
		}

		SetReadWriteButtonState();
		UpdateHashControls();
	}

	return 0;
}

void CMainDlg::CopyText(const CString& strText)
{
	OpenClipboard();
	EmptyClipboard();
	const size_t nSize = (strText.GetLength()+1) * sizeof(TCHAR);
	HANDLE hGlobal = ::GlobalAlloc(GMEM_MOVEABLE, nSize);
	if (hGlobal != NULL)
	{
		void* p = ::GlobalLock(hGlobal);
		SecureHelper::memcpy_x(p, nSize, (LPCTSTR)strText, nSize);
		::GlobalUnlock(hGlobal);
		SetClipboardData(CF_UNICODETEXT, hGlobal);
		CloseClipboard();
	}
}

LRESULT CMainDlg::OnHashCopyClicked(WORD wNotifyCode, WORD wID, HWND hWndCtl)
{
	if (!(m_strHashLabel.IsEmpty()))
	{
		CopyText(m_strHashLabel);
	}

	return 0;
}

LRESULT CMainDlg::OnHashGenerateClicked(WORD wNotifyCode, WORD wID, HWND hWndCtl)
{
	int nIndex = m_wndHashType.GetCurSel();
	int nHashType = m_wndHashType.GetItemData(nIndex);
	if(nHashType>0)
		GenerateHash(m_strImageFile, nHashType);

	return 0;
}

LRESULT CMainDlg::OnReadClicked(WORD wNotifyCode, WORD wID, HWND hWndCtl)
{
	CString strFullPath;
	DoDataExchange(TRUE, IDC_IMAGEFILE);
	CVolume Volume;
	CAtlFile File, RawDisk;
	if (!m_strImageFile.IsEmpty())
	{
		strFullPath = GetFullFilePath();
		// check whether source and target device is the same...
		if (IsSameDrive(strFullPath[0]))
		{
			ShowMessage(_T("Image file cannot be located on the target device."),
				MB_OK | MB_ICONERROR);
			return 0;
		}
		// confirm overwrite if the dest. file already exists
		if (PathFileExists(strFullPath))
		{
			if(ShowMessage(_T("Are you sure you want to overwrite the specified file? "),
				MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2)==IDNO)
			{
				return 0;
			}
		}

		UIEnable(IDCANCEL, TRUE);
		UIEnable(IDC_WRITE, FALSE);
		UIEnable(IDC_READ, FALSE);
		UIEnable(IDC_VERIFY_ONLY, FALSE);
		m_Status = STATUS_READING;
		double mbpersec;
		std::vector<char> SectorData;
		DWORD dwDevice = m_wndDevice.GetItemData(m_wndDevice.GetCurSel());
		WORD DeviceType=LOWORD(dwDevice);
		unsigned long long i, lasti, nSectors, filesize, spaceneeded = 0ull;
		std::vector<CVolume> volumes = OpenCurrentVolumes();
		HANDLE hDevice = INVALID_HANDLE_VALUE;

		File = GetHandleOnFile(m_hWnd, strFullPath, GENERIC_WRITE);
		if (File == INVALID_HANDLE_VALUE)
		{
			m_Status = STATUS_IDLE;
			UIEnable(IDCANCEL, FALSE);
			SetReadWriteButtonState();
			return 0;
		}

		if (DeviceType == DEVICE_DISK)
		{
			if (!UnmountVolumes(volumes))
				return 0;
			RawDisk.Attach(OpenDevice(dwDevice, GENERIC_READ).Detach());
			if (RawDisk == INVALID_HANDLE_VALUE)
			{
				m_Status = STATUS_IDLE;
				UIEnable(IDCANCEL, FALSE);
				SetReadWriteButtonState();
				return 0;
			}

			nSectors = GetDiskSectors(m_hWnd, RawDisk, &m_SectorSize);
			if (m_bPartition)
			{
				// Read MBR partition table
				ReadSectorDataFromHandle(m_hWnd, RawDisk, 0, 1ul, 512ul, SectorData);
				nSectors = 1ul;
				// Read partition information
				for (i = 0ul; i < 4ul; i++)
				{
					uint32_t partitionStartSector = *((uint32_t*)(SectorData.data() + 0x1BE + 8 + 16 * i));
					uint32_t partitionNumSectors = *((uint32_t*)(SectorData.data() + 0x1BE + 12 + 16 * i));
					// Set numsectors to end of last partition
					if (partitionStartSector + partitionNumSectors > nSectors)
					{
						nSectors = partitionStartSector + partitionNumSectors;
					}
				}
			}
			hDevice = RawDisk;
		}
		else
		{
			nSectors = GetVolumeSectors(m_hWnd, HIWORD(dwDevice), &m_SectorSize);
			if (!volumes[0].Lock())
			{
				m_Status = STATUS_IDLE;
				UIEnable(IDCANCEL, FALSE);
				SetReadWriteButtonState();
				return false;
			}
			hDevice = volumes[0].GetHandle();
		}

		filesize = GetFileSizeInSectors(m_hWnd, File, m_SectorSize);
		if (filesize >= nSectors)
		{
			spaceneeded = 0ull;
		}
		else
		{
			spaceneeded = (unsigned long long)(nSectors - filesize) * (unsigned long long)(m_SectorSize);
		}
		CString strDrive=strFullPath.Left(3);
		strDrive.Replace(_T('/'), _T('\\'));
		if (!SpaceAvailable(m_hWnd, strDrive, spaceneeded))
		{
			ShowMessage(_T("Disk is not large enough for the specified image."),
				MB_OK | MB_ICONERROR);
			m_Status = STATUS_IDLE;
			SectorData.clear();
			UIEnable(IDCANCEL, FALSE);
			SetReadWriteButtonState();
			return 0;
		}
		if (nSectors == 0ul)
		{
			m_wndProgress.SetRange32(0, 100);
		}
		else
		{
			m_wndProgress.SetRange32(0, (int)nSectors);
		}
		lasti = 0ul;
		m_UpdateTimer.start();
		StartTimer();
		for (i = 0ul; i < nSectors && m_Status == STATUS_READING; i += 1024ul)
		{
			ReadSectorDataFromHandle(m_hWnd, hDevice, i, (nSectors - i >= 1024ul) ? 1024ul : (nSectors - i), m_SectorSize, SectorData);
			if (SectorData.empty())
			{
				m_Status = STATUS_IDLE;
				UIEnable(IDCANCEL, FALSE);
				SetReadWriteButtonState();
				return 0;
			}
			if (!WriteSectorDataToHandle(m_hWnd, File, SectorData.data(), i, (nSectors - i >= 1024ul) ? 1024ul : (nSectors - i), m_SectorSize))
			{
				SectorData.clear();
				m_Status = STATUS_IDLE;
				UIEnable(IDCANCEL, FALSE);
				SetReadWriteButtonState();
				return 0;
			}
			SectorData.clear();
			if (m_UpdateTimer.elapsed() >= ONE_SEC_IN_MS)
			{
				mbpersec = (((double)m_SectorSize * (i - lasti)) * ((float)ONE_SEC_IN_MS / m_UpdateTimer.elapsed())) / 1024.0 / 1024.0;
				UpdateSpeed(mbpersec);
				UpdateElapsedTime(i, nSectors);
				//elapsed_timer->update(i, numsectors);
				lasti = i;
			}
			m_wndProgress.SetPos(i);
			HandleMessages();
		}
		m_wndProgress.SetPos(0);
		m_wndStatusBar.SetPaneText(ID_PANE_NEXT, _T("Done."));
		UIEnable(IDCANCEL, FALSE);
		SetReadWriteButtonState();
		if (m_Status == STATUS_CANCELED) {
			ShowMessage(_T("Read Canceled."), MB_OK | MB_ICONINFORMATION);
		}
		else {
			ShowMessage(_T("Read Successful."), MB_OK | MB_ICONINFORMATION);

		}
		UpdateHashControls();
	}
	else
	{
		ShowMessage(_T("Please specify a file to save data to."), MB_OK | MB_ICONERROR);
	}
	if (m_Status == STATUS_EXIT)
	{
		PostQuitMessage(0);
	}
	m_Status = STATUS_IDLE;
	StopTimer();

	return 0;
}

LRESULT CMainDlg::OnWriteClicked(WORD wNotifyCode, WORD wID, HWND hWndCtl)
{
	bool passfail = true;
	DoDataExchange(TRUE, IDC_IMAGEFILE);
	CVolume Volume;
	CAtlFile File, RawDisk;
	if (!m_strImageFile.IsEmpty())
	{
		if (IsReatableFile())
		{
			if (IsSameDrive(m_strImageFile[0]))
			{
				ShowMessage(_T("Image file cannot be located on the target device."),
					MB_OK | MB_ICONERROR);
				return 0;
			}

			// build the drive letter as a const char *
			//   (without the surrounding brackets)
			CString strMessage, strDevice;
			m_wndDevice.GetLBText(m_wndDevice.GetCurSel(), strDevice);
			strMessage.Format(_T("Writing to a physical device can corrupt the device.\n")
				_T("(Target Device: %s \"%s\")\n")
				_T("Are you sure you want to continue?"), (LPCTSTR)strDevice, getDriveLabel(strDevice));
			if (ShowMessage((LPCTSTR)strMessage, MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDNO)
			{
				return 0;
			}
			m_Status = STATUS_WRITING;
			UIEnable(IDCANCEL, TRUE);
			UIEnable(IDC_WRITE, FALSE);
			UIEnable(IDC_READ, FALSE);
			UIEnable(IDC_VERIFY_ONLY, FALSE);
			double mbpersec;
			std::vector<char> SectorData;
			DWORD dwDevice = m_wndDevice.GetItemData(m_wndDevice.GetCurSel());
			WORD DeviceType=LOWORD(dwDevice);
			unsigned long long i, lasti, nAvailableSectors, nSectors;
			std::vector<CVolume> volumes = OpenCurrentVolumes();
			HANDLE hDevice = INVALID_HANDLE_VALUE;

			File=GetHandleOnFile(m_hWnd, m_strImageFile, GENERIC_READ);
			if (File == NULL)
			{
				m_Status = STATUS_IDLE;
				UIEnable(IDCANCEL, FALSE);
				SetReadWriteButtonState();
				return 0;
			}
			
			if (DeviceType == DEVICE_DISK)
			{
				if (!UnmountVolumes(volumes))
					return 0;

				RawDisk.Attach(OpenDevice(dwDevice, GENERIC_WRITE).Detach());
				if (RawDisk == INVALID_HANDLE_VALUE)
				{
					m_Status = STATUS_IDLE;
					UIEnable(IDCANCEL, FALSE);
					SetReadWriteButtonState();
					return 0;
				}
				nAvailableSectors = GetDiskSectors(m_hWnd, RawDisk, &m_SectorSize);
				if (LOWORD(dwDevice) == DEVICE_DRIVE)
					nAvailableSectors = GetVolumeSectors(m_hWnd, HIWORD(dwDevice), &m_SectorSize);
				if (!nAvailableSectors)
				{
					//For external card readers you may not get device change notification when you remove the card/flash.
					//(So no WM_DEVICECHANGE signal). Device stays but size goes to 0. [Is there special event for this on Windows??]
					passfail = false;
					m_Status = STATUS_IDLE;
					return 0;
				}
				hDevice = RawDisk;
			}
			else
			{
				nSectors = GetVolumeSectors(m_hWnd, HIWORD(dwDevice), &m_SectorSize);
				if (!volumes[0].Lock())
				{
					m_Status = STATUS_IDLE;
					UIEnable(IDCANCEL, FALSE);
					SetReadWriteButtonState();
					return false;
				}
				hDevice = volumes[0].GetHandle();
			}

			nSectors = GetFileSizeInSectors(m_hWnd, File, m_SectorSize);
			if (!nSectors)
			{
				//For external card readers you may not get device change notification when you remove the card/flash.
				//(So no WM_DEVICECHANGE signal). Device stays but size goes to 0. [Is there special event for this on Windows??]
				m_Status = STATUS_IDLE;
				return 0;

			}
			if (nSectors > nAvailableSectors)
			{
				bool datafound = false;
				i = nAvailableSectors;
				unsigned long nextchunksize = 0;
				while ((i < nSectors) && (datafound == false))
				{
					nextchunksize = ((nSectors - i) >= 1024ul) ? 1024ul : (nSectors - i);
					ReadSectorDataFromHandle(m_hWnd, File, i, nextchunksize, m_SectorSize, SectorData);
					if (SectorData.empty())
					{
						// if there's an error verifying the truncated data, just move on to the
						//  write, as we don't care about an error in a section that we're not writing...
						i = nSectors + 1;
					}
					else {
						unsigned int j = 0;
						unsigned limit = nextchunksize * m_SectorSize;
						while ((datafound == false) && (j < limit))
						{
							if (SectorData[j++] != 0)
							{
								datafound = true;
							}
						}
						i += nextchunksize;
					}
				}
				// delete the allocated SectorData
				SectorData.clear();
				// build the string for the warning dialog
				CString strMessage;
				strMessage.Format(_T("More space required than is available: ")
					_T("\n  Required: %llu sectors")
					_T("\n  Available: %llu sectors")
					_T("\n  Sector Size: %llu")
					_T("\n\nThe extra space %s appear to contain data")
					_T("\n\nContinue Anyway?"),
					nSectors, nAvailableSectors, m_SectorSize, (datafound) ? _T("DOES") : _T("does not"));
				if (ShowMessage((LPCTSTR)strMessage, MB_YESNO|MB_ICONQUESTION)==IDYES)
				{
					// truncate the image at the device size...
					nSectors = nAvailableSectors;
				}
				else    // Cancel
				{
					m_Status = STATUS_IDLE;
					UIEnable(IDCANCEL, FALSE);
					SetReadWriteButtonState();
					return 0;
				}
			}

			m_wndProgress.SetRange32(0, (nSectors == 0ul) ? 100 : (int)nSectors);
			lasti = 0ul;
			m_UpdateTimer.start();
			StartTimer();
			for (i = 0ul; i < nSectors && m_Status == STATUS_WRITING; i += 1024ul)
			{
				ReadSectorDataFromHandle(m_hWnd, File, i, (nSectors - i >= 1024ul) ? 1024ul : (nSectors - i), m_SectorSize, SectorData);
				if (SectorData.empty())
				{
					m_Status = STATUS_IDLE;
					UIEnable(IDCANCEL, FALSE);
					SetReadWriteButtonState();
					return 0;
				}
				if (!WriteSectorDataToHandle(m_hWnd, hDevice, SectorData.data(), i, (nSectors - i >= 1024ul) ? 1024ul : (nSectors - i), m_SectorSize))
				{
					SectorData.clear();
					m_Status = STATUS_IDLE;
					UIEnable(IDCANCEL, FALSE);
					SetReadWriteButtonState();
					return 0;
				}
				SectorData.clear();
				HandleMessages();
				if (m_UpdateTimer.elapsed() >= ONE_SEC_IN_MS)
				{
					mbpersec = (((double)m_SectorSize * (i - lasti)) * ((float)ONE_SEC_IN_MS / m_UpdateTimer.elapsed())) / 1024.0 / 1024.0;
					UpdateSpeed(mbpersec);
					UpdateElapsedTime(i, nSectors);
					lasti = i;
				}
				m_wndProgress.SetPos(i);
				HandleMessages();
			}
			if (m_Status == STATUS_CANCELED) {
				passfail = false;
			}
		}
		else
		{
			passfail = false;
		}
		m_wndProgress.SetPos(0);
		m_wndStatusBar.SetPaneText(ID_DEFAULT_PANE, _T("Done."));
		UIEnable(IDCANCEL, FALSE);
		SetReadWriteButtonState();
		if (passfail) {
			ShowMessage(_T("Write Successful."), MB_OK | MB_ICONINFORMATION);
		}

	}
	else
	{
		ShowMessage(_T("Please specify an image file to use."), MB_OK | MB_ICONERROR);
	}
	if (m_Status == STATUS_EXIT)
	{
		PostQuitMessage(0);
	}
	m_Status = STATUS_IDLE;
	StopTimer();
	//elapsed_timer->stop();

	return 0;
}

LRESULT CMainDlg::OnVerifyOnlyClicked(WORD wNotifyCode, WORD wID, HWND hWndCtl)
{
	bool passfail = true;
	DoDataExchange(TRUE, IDC_IMAGEFILE);
	std::vector<char> DiskData; //for verify
	CAtlFile File, RawDisk;
	if (!m_strImageFile.IsEmpty())
	{
		if (IsReatableFile())
		{
			if (IsSameDrive(m_strImageFile[0]))
			{
				ShowMessage(_T("Image file cannot be located on the target device."),
					MB_OK | MB_ICONERROR);
				return 0;
			}
			m_Status = STATUS_VERIFYING;
			UIEnable(IDCANCEL, TRUE);
			UIEnable(IDC_WRITE, FALSE);
			UIEnable(IDC_READ, FALSE);
			UIEnable(IDC_VERIFY_ONLY, FALSE);
			double mbpersec;
			std::vector<char> SectorData;
			DWORD dwDevice = m_wndDevice.GetItemData(m_wndDevice.GetCurSel());
			WORD DeviceType=LOWORD(dwDevice);
			unsigned long long i, lasti, nAvailableSectors, nSectors, result;
			std::vector<CVolume> volumes = OpenCurrentVolumes();
			HANDLE hDevice = INVALID_HANDLE_VALUE;

			File=GetHandleOnFile(m_hWnd, m_strImageFile, GENERIC_READ);
			if (File == INVALID_HANDLE_VALUE)
			{
				m_Status = STATUS_IDLE;
				UIEnable(IDCANCEL, FALSE);
				SetReadWriteButtonState();
				return 0;
			}

			if (DeviceType == DEVICE_DISK)
			{
				if (!UnmountVolumes(volumes))
					return 0;
				RawDisk.Attach(OpenDevice(dwDevice, GENERIC_READ).Detach());
				if (RawDisk == INVALID_HANDLE_VALUE)
				{
					m_Status = STATUS_IDLE;
					UIEnable(IDCANCEL, FALSE);
					SetReadWriteButtonState();
					return 0;
				}
				nAvailableSectors = GetDiskSectors(m_hWnd, RawDisk, &m_SectorSize);
				if (!nAvailableSectors)
				{
					//For external card readers you may not get device change notification when you remove the card/flash.
					//(So no WM_DEVICECHANGE signal). Device stays but size goes to 0. [Is there special event for this on Windows??]
					passfail = false;
					m_Status = STATUS_IDLE;
					return 0;
				}
				hDevice = RawDisk;
			}
			else
			{
				nAvailableSectors = GetVolumeSectors(m_hWnd, HIWORD(dwDevice), &m_SectorSize);
				if (!volumes[0].Lock())
				{
					m_Status = STATUS_IDLE;
					UIEnable(IDCANCEL, FALSE);
					SetReadWriteButtonState();
					return false;
				}
				hDevice = volumes[0].GetHandle();
			}

			nSectors = GetFileSizeInSectors(m_hWnd, File, m_SectorSize);
			if (!nSectors)
			{
				//For external card readers you may not get device change notification when you remove the card/flash.
				//(So no WM_DEVICECHANGE signal). Device stays but size goes to 0. [Is there special event for this on Windows??]
				m_Status = STATUS_IDLE;
				return 0;

			}
			if (nSectors > nAvailableSectors)
			{
				bool bDataFound = false;
				i = nAvailableSectors;
				unsigned long nNextChunkSize = 0;
				while ((i < nSectors) && (bDataFound == false))
				{
					nNextChunkSize = ((nSectors - i) >= 1024ul) ? 1024ul : (nSectors - i);
					ReadSectorDataFromHandle(m_hWnd, File, i, nNextChunkSize, m_SectorSize, SectorData);
					if (SectorData.empty())
					{
						// if there's an error verifying the truncated data, just move on to the
						//  write, as we don't care about an error in a section that we're not writing...
						i = nSectors + 1;
					}
					else {
						unsigned int j = 0;
						unsigned limit = nNextChunkSize * m_SectorSize;
						while ((bDataFound == false) && (j < limit))
						{
							if (SectorData[j++] != 0)
							{
								bDataFound = true;
							}
						}
						i += nNextChunkSize;
					}
				}
				// delete the allocated SectorData
				SectorData.clear();
				// build the string for the warning dialog
				CString strMsg;
				strMsg.Format(IDS_IMAGE_LARGER_THEN_DISK, nSectors, nAvailableSectors, m_SectorSize,
					(bDataFound) ? _T("DOES") : _T("does not"));
				if(ShowMessage((LPCTSTR)strMsg, MB_OKCANCEL|MB_ICONQUESTION)==IDYES)
				{
					// truncate the image at the device size...
					nSectors = nAvailableSectors;
				}
				else    // Cancel
				{
					m_Status = STATUS_IDLE;
					UIEnable(IDCANCEL, FALSE);
					SetReadWriteButtonState();
					return 0;
				}
			}
			m_wndProgress.SetRange32(0, (nSectors == 0ul) ? 100 : (int)nSectors);
			m_UpdateTimer.start();
			StartTimer();
			lasti = 0ul;
			for (i = 0ul; i < nSectors && m_Status == STATUS_VERIFYING; i += 1024ul)
			{
				ReadSectorDataFromHandle(m_hWnd, File, i, (nSectors - i >= 1024ul) ? 1024ul : (nSectors - i), m_SectorSize, SectorData);
				if (SectorData.empty())
				{
					m_Status = STATUS_IDLE;
					UIEnable(IDCANCEL, FALSE);
					SetReadWriteButtonState();
					return 0;
				}
				ReadSectorDataFromHandle(m_hWnd, hDevice, i, (nSectors - i >= 1024ul) ? 1024ul : (nSectors - i), m_SectorSize, DiskData);
				if (DiskData.empty())
				{
					CString strMessage; 
					strMessage.Format(_T("Verification failed at sector: %d"), i);
					ShowMessage((LPCTSTR)strMessage, MB_OK | MB_ICONERROR);
					m_Status = STATUS_IDLE;
					UIEnable(IDCANCEL, FALSE);
					SetReadWriteButtonState();
					return 0;
				}
				result = memcmp(SectorData.data(), DiskData.data(), ((nSectors - i >= 1024ul) ? 1024ul : (nSectors - i)) * m_SectorSize);
				if (result)
				{
					CString strMessage;
					strMessage.Format(_T("Verification failed at sector: %d"), i);
					ShowMessage((LPCTSTR)strMessage, MB_OK | MB_ICONERROR);
					passfail = false;
					break;

				}
				if (m_UpdateTimer.elapsed() >= ONE_SEC_IN_MS)
				{
					mbpersec = (((double)m_SectorSize * (i - lasti)) * ((float)ONE_SEC_IN_MS / m_UpdateTimer.elapsed())) / 1024.0 / 1024.0;
					UpdateSpeed(mbpersec);
					UpdateElapsedTime(i, nSectors);
					lasti = i;
				}
				SectorData.clear();
				DiskData.clear();
				m_wndProgress.SetPos(i);
				HandleMessages();
			}
			SectorData.clear();
			DiskData.clear();
			if (m_Status == STATUS_CANCELED) {
				passfail = false;
			}

		}
		else
		{
			passfail = false;
		}
		m_wndProgress.SetPos(0);
		m_wndStatusBar.SetPaneText(ID_DEFAULT_PANE, _T("Done."));
		UIEnable(IDCANCEL, FALSE);
		SetReadWriteButtonState();
		if (passfail) {
			ShowMessage(_T("Verify Successful."), MB_OK | MB_ICONINFORMATION);
		}
	}
	else
	{
		ShowMessage(_T("Please specify an image file to use."), MB_OK | MB_ICONERROR);
	}
	if (m_Status == STATUS_EXIT)
	{
		PostQuitMessage(0);
	}
	m_Status = STATUS_IDLE;
	StopTimer();
	//elapsed_timer->stop();

	return 0;
}

// getLogicalDrives sets cBoxDevice with any logical drives found, as long
// as they indicate that they're either removable, or fixed and on USB bus
void CMainDlg::GetLogicalDrives()
{
	// GetLogicalDrives returns 0 on failure, or a bitmask representing
	// the drives available on the system (bit 0 = A:, bit 1 = B:, etc)
	DWORD driveMask = ::GetLogicalDrives();
	int i = 0;
	ULONG pID;

	while (driveMask != 0)
	{
		if (driveMask & 1)
		{
			// the "A" in drivename will get incremented by the # of bits
			// we've shifted
			TCHAR lpszDriveName[8] = _T("\\\\.\\A:\\") ;
			lpszDriveName[4] += i;
			if (CheckDriveType(m_hWnd, lpszDriveName, &pID))
			{
				CVolume volume;
				TCHAR szDeviceLabel[8] = _T("[A:\\]");
				szDeviceLabel[1] = lpszDriveName[4];
				if (volume.Open(m_hWnd, lpszDriveName[4], GENERIC_READ))
				{
					DWORD nDeviceNumber = volume.GetDeviceID();
					CDiskInfo* pDiskInfo = FindDiskInfo(nDeviceNumber);
					if (pDiskInfo)
						pDiskInfo->m_Volumes.push_back(lpszDriveName[4]);
				}
				//int nIndex = m_wndDevice.AddString(szDeviceLabel);
				//m_wndDevice.SetItemData(nIndex, (DWORD_PTR)MAKELONG(DEVICE_DRIVE, lpszDriveName[4]));
			}
		}
		driveMask >>= 1;
		++i;
	}
}

CDiskInfo* CMainDlg::FindDiskInfo(DWORD nDeviceNumber)
{
	for (CDiskInfo& disk : m_Disks)
	{
		if (disk.m_nDeviceNumber == nDeviceNumber)
			return &disk;
	}
	return NULL;
}

void CMainDlg::GetPhysicalDisks()
{
	m_Disks.clear();
	ScanDiskDevices(m_Disks);
}

void CMainDlg::RefreshPhysicalDisks()
{
	std::vector<CDiskInfo> temp;
	ScanDiskDevices(temp);
	for(std::vector<CDiskInfo>::iterator it=m_Disks.begin(); it!=m_Disks.end(); )
	{
		bool found=false;
		for(size_t j=0; j!=temp.size(); j++)
			if(temp[j].m_nDeviceNumber==it->m_nDeviceNumber)
			{
				found=true;
				break;
			}
		if(found) ++it;
		else it = m_Disks.erase(it);
	}
	for(size_t i=0; i!=temp.size(); i++)
	{
		bool found=false;
		for(size_t j=0; j!=m_Disks.size(); j++)
			if(m_Disks[j].m_nDeviceNumber==temp[i].m_nDeviceNumber)
			{
				found=true;
				break;
			}
		if(!found)
			m_Disks.push_back(temp[i]);
	}
	InitDeviceList();
}

void CMainDlg::InitDeviceList()
{
	m_wndDevice.ResetContent();
	for (const CDiskInfo& disk : m_Disks)
	{
		//LPTSTR lpszPath = new TCHAR[32];
		//lpszPath[0] == DEVICE_DISK;
		//wsprintf(lpszPath+1, _T("\\\\?\\PhysicalDrive%d"), disk.m_nDeviceNumber);
		//int nIndex = m_wndDevice.AddString(disk.m_strFriendlyName);
		//m_wndDevice.SetItemData(nIndex, (DWORD_PTR)MAKELONG(DEVICE_DISK, disk.m_nDeviceNumber));
		m_wndDevice.AddItem(disk.m_strFriendlyName, 0, 0, 0, MAKELPARAM(DEVICE_DISK, disk.m_nDeviceNumber));
		TCHAR szDeviceLabel[8] = _T("[A:\\]");
		for (TCHAR nVolume : disk.m_Volumes)
		{
			szDeviceLabel[1] = nVolume;
			m_wndDevice.AddItem(szDeviceLabel, 0, 0, 1, MAKELPARAM(DEVICE_DRIVE, nVolume));
		}
	}

	if (m_wndDevice.GetCount() > 0)
	{
		m_wndDevice.SetCurSel(0);
		GetDeviceSize();
	}
}

void CMainDlg::SetReadWriteButtonState()
{
	bool bFileSelected = !m_strImageFile.IsEmpty();
	bool bDeviceSelected = (m_wndDevice.GetCount() > 0);
	DWORD dwAttributes = GetFileAttributes(m_strImageFile);
	BOOL bIsReadOnly = TRUE;
	if(dwAttributes!=INVALID_FILE_ATTRIBUTES)
		bIsReadOnly=dwAttributes&FILE_ATTRIBUTE_READONLY;

	// set read and write buttons according to m_Status of file/device
	UIEnable(IDC_READ, bDeviceSelected && bFileSelected);
	UIEnable(IDC_WRITE, bDeviceSelected && bFileSelected && !bIsReadOnly );
	UIEnable(IDC_VERIFY_ONLY, bDeviceSelected && bFileSelected && (dwAttributes!=INVALID_FILE_ATTRIBUTES) );
}

void CMainDlg::SaveSettings()
{
	CRegKey key;
	if (key.Create(HKEY_CURRENT_USER, _T("Software\\Win32DiskImager\\Settings")) == ERROR_SUCCESS)
	{
		key.SetStringValue(_T("ImageDir"), m_strHomeDir);
	}
}

void CMainDlg::LoadSettings()
{
	CRegKey key;
	if (key.Open(HKEY_CURRENT_USER, _T("Software\\Win32DiskImager\\Settings")) == ERROR_SUCCESS)
	{
		LPTSTR lpszText = m_strHomeDir.GetBuffer(MAX_PATH);
		DWORD dwText=0;
		key.QueryStringValue(_T("ImageDir"), lpszText, &dwText);
		m_strHomeDir.ReleaseBuffer(dwText);
	}
}

void CMainDlg::InitializeHomeDir()
{
	TCHAR szHomeFolder[MAX_PATH] = {};
	BOOL bRet = SHGetSpecialFolderPath(m_hWnd, szHomeFolder, CSIDL_PROFILE, TRUE);
	if (!bRet) 
	{
		GetEnvironmentVariable(_T("USERPROFILE"), szHomeFolder, MAX_PATH);
	}
	/* Get Downloads the Windows way */
	TCHAR szDownloadPath[MAX_PATH] = {};
	GetEnvironmentVariable(_T("DiskImagesDir"), szDownloadPath, MAX_PATH);
	if (_tcslen(szDownloadPath)==0) 
	{
		LPTSTR pPath = NULL;
#if (NTDDI_VERSION >= NTDDI_WIN7)
		if (SHGetKnownFolderPath(FOLDERID_Downloads, 0, 0, &pPath) == S_OK) {
			_tcscpy(szDownloadPath, pPath);
			CoTaskMemFree(pPath);
		}
#endif
	}
	if (_tcslen(szDownloadPath) == 0)
		GetCurrentDirectory(MAX_PATH, szDownloadPath);
	m_strHomeDir = szDownloadPath;
}

void CMainDlg::UpdateHashControls()
{
	UIEnable(IDC_HASH_COPY, FALSE);
	UIEnable(IDC_HASH_GENERATE, FALSE);
	UIEnable(IDC_HASH_COPY, FALSE);

	DWORD dwAttributes = GetFileAttributes(m_strImageFile);
	if(dwAttributes==INVALID_FILE_ATTRIBUTES)
		return;

	CAtlFile file;
	if (file.Create(m_strImageFile, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING) != ERROR_SUCCESS)
		return;

	ULONGLONG llFileSize;
	file.GetSize(llFileSize);
	bool validFile = (PathFileExists(m_strImageFile) && !(dwAttributes&FILE_ATTRIBUTE_DIRECTORY) &&
		llFileSize>0);

	SetDlgItemText(IDC_HASH_LABEL, NULL);

	if (m_wndHashType.GetCurSel() != 0 && !m_strImageFile.IsEmpty() && validFile)
	{
		UIEnable(IDC_HASH_GENERATE, TRUE);
	}
	else
	{
		UIEnable(IDC_HASH_GENERATE, FALSE);
	}

	// if there's a value in the md5 label make the copy button visible
	DoDataExchange(FALSE, IDC_HASH_LABEL);
	bool haveHash = !(m_strHashLabel.IsEmpty());
	UIEnable(IDC_HASH_COPY, haveHash);
}

LRESULT CMainDlg::OnHashTypeSelChange(WORD wNotifyCode, WORD wID, HWND hWndCtl)
{
	UpdateHashControls();
	return 0;
}

// support routine for winEvent - returns the drive letter for a given mask
//   taken from http://support.microsoft.com/kb/163503
static char FirstDriveFromMask(ULONG unitmask)
{
	char i;

	for (i = 0; i < 26; ++i)
	{
		if (unitmask & 0x1)
		{
			break;
		}
		unitmask = unitmask >> 1;
	}

	return (i + 'A');
}

LRESULT CMainDlg::OnDeviceChange(UINT uEvent, DWORD dwEventData)
{
	PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)dwEventData;
	switch (uEvent)
	{
	case DBT_DEVNODES_CHANGED:
		RefreshPhysicalDisks();
		break;
	case DBT_DEVICEARRIVAL:
		if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
		{
			PDEV_BROADCAST_VOLUME lpdbv = (PDEV_BROADCAST_VOLUME)lpdb;
			if (DBTF_NET)
			{
				char nDrive = FirstDriveFromMask(lpdbv->dbcv_unitmask);
				// add device to combo box (after sanity check that
				// it's not already there, which it shouldn't be)
				AddDrive(nDrive);
			}
		}
		break;
	case DBT_DEVICEREMOVECOMPLETE:
		if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
		{
			PDEV_BROADCAST_VOLUME lpdbv = (PDEV_BROADCAST_VOLUME)lpdb;
			if (DBTF_NET)
			{
				char nDrive = FirstDriveFromMask(lpdbv->dbcv_unitmask);
				//  find the device that was removed in the combo box,
				//  and remove it from there....
				//  "removeItem" ignores the request if the index is
				//  out of range, and findText returns -1 if the item isn't found.
				TCHAR szLabel[]=_T("[%c:\\]");
				szLabel[1]=nDrive;
				for(size_t i=0; i!=m_Disks.size(); i++)
				{
					CDiskInfo& DiskInfo =m_Disks[i];
					std::vector<TCHAR>::iterator it=std::find(DiskInfo.m_Volumes.begin(), DiskInfo.m_Volumes.end(), nDrive);
					if(it!=DiskInfo.m_Volumes.end())
						DiskInfo.m_Volumes.erase(it);
				}
				m_wndDevice.DeleteString(m_wndDevice.FindStringExact(-1, szLabel));
				SetReadWriteButtonState();
			}
		}
		break;
	} // skip the rest

	return TRUE;
}

// generates the hash
void CMainDlg::GenerateHash(LPCTSTR filename, int hashish)
{
	CString strHashResult;
	m_strHashLabel = _T("Generating...");
	DoDataExchange(FALSE, IDC_HASH_LABEL);
	UIEnable(IDC_HASH_GENERATE, FALSE);

	// may take a few secs - display a wait cursor
	HCURSOR hOldCursor = GetCursor();
	SetCursor(LoadCursor(NULL, IDC_WAIT));

	HandleMessages();
	BOOL bResult = HashFile(hashish, m_strHashLabel, filename);
	if (!bResult)
	{
		m_strHashLabel.Empty();
	}

	// display it in the textbox
	UIEnable(IDC_HASH_GENERATE, bResult);
	UIEnable(IDC_HASH_COPY, bResult);
	DoDataExchange(FALSE, IDC_HASH_LABEL);
	// redisplay the normal cursor
	SetCursor(hOldCursor);
}

bool CMainDlg::IsSameDrive(TCHAR nVolume)
{
	int nIndex = m_wndDevice.GetCurSel();
	if (nIndex >= 0)
	{
		DWORD dwDrive = m_wndDevice.GetItemData(nIndex);
		uint8_t nDeviceType = LOWORD(dwDrive);
		if (nDeviceType == DEVICE_DRIVE)
		{
			return nVolume== HIWORD(dwDrive);
		}
		else if (nDeviceType == DEVICE_DISK)
		{
			CDiskInfo* pDiskInfo = FindDiskInfo(HIWORD(dwDrive));
			return find(pDiskInfo->m_Volumes.begin(), pDiskInfo->m_Volumes.end(), nVolume) != pDiskInfo->m_Volumes.end();
		}
	}
	return false;
}

CString CMainDlg::GetFullFilePath() const
{
	CString strFullPath;
	LPTSTR lpszBuffer = strFullPath.GetBuffer(MAX_PATH);
	PathCombine(lpszBuffer, m_strHomeDir, m_strImageFile);
	strFullPath.ReleaseBuffer();
	return strFullPath;
}

bool CMainDlg::IsReatableFile() const
{
	CAtlFile file;
	if (!PathFileExists(m_strImageFile))
	{
		ShowMessage(_T("The selected file does not exist."), MB_OK | MB_ICONERROR);
		return false;
	}
	if (file.Create(m_strImageFile, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING) != ERROR_SUCCESS)
	{
		ShowMessage(_T("You do not have permision to read the selected file."), MB_OK | MB_ICONERROR);
		return false;
	}

	ULONGLONG llFileSize;
	file.GetSize(llFileSize);
	if (llFileSize == 0)
	{
		ShowMessage(_T("The specified file contains no data."), MB_OK | MB_ICONERROR);
		return false;
	}
	return true;
}

void CMainDlg::HandleMessages()
{
	MSG msg;
	OnIdle();
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		if (!PreTranslateMessage(&msg))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
}

void CMainDlg::StartTimer()
{
	m_ElapsedTimer.start();
	m_wndStatusBar.SetSimple(FALSE);
}

void CMainDlg::StopTimer()
{
	m_wndStatusBar.SetSimple(TRUE);
}

void CMainDlg::UpdateElapsedTime(uint64_t position, uint64_t total)
{
	const unsigned short MS_PER_SEC = 1000;
	const unsigned short SECS_PER_MIN = 60;
	const unsigned short MINS_PER_HOUR = 60;
	const unsigned short SECS_PER_HOUR = (SECS_PER_MIN * MINS_PER_HOUR);

	DWORD seconds = m_ElapsedTimer.elapsed()/ MS_PER_SEC;

	unsigned short eSec = 0, eMin = 0, eHour = 0;
	unsigned short tSec = 0, tMin = 0, tHour = 0;

	eSec = seconds % SECS_PER_MIN;
	if (seconds >= SECS_PER_MIN)
	{
		eMin = seconds / SECS_PER_MIN;
		if (seconds >= SECS_PER_HOUR)
		{
			eHour = seconds / SECS_PER_HOUR;
		}
	}

	unsigned int totalSecs = (unsigned int)((float)seconds * ((float)total / (float)position));
	unsigned int totalMin = 0;
	tSec = totalSecs % SECS_PER_MIN;
	if (totalSecs >= SECS_PER_MIN)
	{
		totalMin = totalSecs / SECS_PER_MIN;
		tMin = totalMin % MINS_PER_HOUR;
		if (totalMin > MINS_PER_HOUR)
		{
			tHour = totalMin / MINS_PER_HOUR;
		}
	}

	CString qs;
	if (eHour > 0)
		qs.Format(_T("%02d:"), eHour);
	qs.AppendFormat(_T("%02d:%02d/"), eMin, eSec);
	if (tHour > 0)
	{
		qs.AppendFormat(_T("%02d:"), tHour);
	}
	qs.AppendFormat(_T("%02d:%02d"), tMin, tSec);
	// added a space following the times to separate the text slightly from the right edge of the status bar...
	// there's probably a more "QT-correct" way to do that (like, margins or something),
	// but this was simple and effective.
	m_wndStatusBar.SetPaneText(ID_ELAPSED_TIME, qs);
}

void CMainDlg::UpdateSpeed(double speed)
{
	CString strText;
	strText.Format(_T("%fMB/s"), speed);
	m_wndStatusBar.SetPaneText(ID_DEFAULT_PANE, strText);
	m_UpdateTimer.start();
}

bool CMainDlg::HashFile(int hash_type, CString& strHashResult, LPCTSTR lpszFileName)
{
	HCRYPTPROV hCryptProv;
	HCRYPTHASH hCryptHash;

	if (!CryptAcquireContext(&hCryptProv, NULL, MS_ENH_RSA_AES_PROV, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
	{
		return false;
	}

	if (!CryptCreateHash(hCryptProv, hash_type, 0, 0, &hCryptHash))
	{
		CryptReleaseContext(hCryptProv, NULL);
		return false;
	}

	CAtlFile file;
	if (file.Create(lpszFileName, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING) != ERROR_SUCCESS)
	{
		CryptDestroyHash(hCryptHash);
		CryptReleaseContext(hCryptProv, NULL);
		return false;
	}

	ULONGLONG llFileSize;
	const size_t nMaxBufSize = 1024 * 1024;
	ULONGLONG llTotalReaded = 0;
	DWORD dwReaded;
	file.GetSize(llFileSize);
	std::vector<BYTE> buffer;
	buffer.resize(min(llFileSize, nMaxBufSize));
	while (file.Read(buffer.data(), buffer.size(), dwReaded) == ERROR_SUCCESS && dwReaded>0)
	{
		llTotalReaded += dwReaded;
		if (!CryptHashData(hCryptHash, buffer.data(), dwReaded, 0))
		{
			break;
		}
		HandleMessages();
	}

	if (llTotalReaded!=llFileSize)
	{
		CryptDestroyHash(hCryptHash);
		CryptReleaseContext(hCryptProv, NULL);
		return false;
	}

	BYTE hash_data[512];
	DWORD hash_len = 512;
	if (!CryptGetHashParam(hCryptHash, HP_HASHVAL, hash_data, &hash_len, 0))
	{
		return false;
	}

	strHashResult.Empty();
	for (int i = 0; i <= hash_len - 1; i++)
	{
		strHashResult.AppendFormat(_T("%02X"), hash_data[i] & 0xFF);
	}
	CryptDestroyHash(hCryptHash);
	CryptReleaseContext(hCryptProv, NULL);
	return true;
}

void CMainDlg::AddHashType(LPCTSTR lpszHashName, int nHashTag)
{
	int nIndex = m_wndHashType.AddString(lpszHashName);
	m_wndHashType.SetItemData(nIndex, nHashTag);
}

//LRESULT CMainDlg::OnDevModeChange(LPCTSTR lpszDeviceName)
//{
//
//	return 0;
//}

void CMainDlg::SetButtonBitmap(UINT nButtonID, UINT nImageID)
{
	Gdiplus::Image* image;
	CComPtr<IStream> spStream;
	CButton wndButton = GetDlgItem(nButtonID);
	CResource res;
	res.Load(_T("PNG"), nImageID);
	HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &spStream);
	LARGE_INTEGER llSize = { 0 };
	hr = spStream->Write(res.Lock(), res.GetSize(), NULL);
	hr = spStream->Seek(llSize, STREAM_SEEK_CUR, NULL);
	if (SUCCEEDED(hr))
	{
		image = Gdiplus::Image::FromStream(spStream);
	}

	CWindowDC windc(m_hWnd);
	CDC dc;
	long width, height;
	width = image->GetWidth();
	height = image->GetHeight();
	dc.CreateCompatibleDC(windc);
	m_BrowseBitmap.CreateCompatibleBitmap(windc, width, height);
	HBITMAP hOldBmp = dc.SelectBitmap(m_BrowseBitmap);

	{
		Gdiplus::Graphics gs(dc);
		Gdiplus::Status status = gs.DrawImage(image, 0, 0, width, height);
	}
	delete image;
	dc.SelectBitmap(hOldBmp);
	wndButton.SetBitmap(m_BrowseBitmap);
}

LRESULT CMainDlg::OnDeleteItem(UINT ControlID, LPDELETEITEMSTRUCT lpDeleteItem)
{
	if (lpDeleteItem->hwndItem == m_wndDevice)
	{
		LPTSTR lpszPath = (LPTSTR)lpDeleteItem->itemData;
		delete[] lpszPath;
	}
	return TRUE;
}

std::vector<CVolume> CMainDlg::OpenCurrentVolumes()
{
	std::vector<CVolume> volumes;
	int nIndex = m_wndDevice.GetCurSel();
	DWORD dwDevice = m_wndDevice.GetItemData(nIndex);
	if (LOWORD(dwDevice) == DEVICE_DRIVE)
	{
		TCHAR nVolume = HIWORD(dwDevice);
		CVolume volume;
		if (volume.Open(m_hWnd, nVolume, GENERIC_READ))
		{
			volumes.push_back(std::move(volume));
		}
	}
	else if (LOWORD(dwDevice) == DEVICE_DISK)
	{
		CDiskInfo* pDiskInfo = FindDiskInfo(HIWORD(dwDevice));
		if (pDiskInfo)
		{
			for (size_t i = 0; i != pDiskInfo->m_Volumes.size(); i++)
			{
				CVolume volume;
				if (volume.Open(m_hWnd, pDiskInfo->m_Volumes[i], GENERIC_READ))
				{
					volumes.push_back(std::move(volume));
				}
			}
		}
	}
	return volumes;
}

CAtlFile CMainDlg::OpenDevice(DWORD dwDevice, DWORD dwAccess)
{
	return GetHandleOnDevice(m_hWnd, dwDevice, dwAccess);
}

bool CMainDlg::UnmountVolumes(std::vector<CVolume>& volumes)
{
	for (CVolume& volume : volumes)
	{
		if (!volume.Lock())
		{
			m_Status = STATUS_IDLE;
			UIEnable(IDCANCEL, FALSE);
			SetReadWriteButtonState();
			return false;
		}
		if (!volume.Unmount())
		{
			volume.Unlock();
			m_Status = STATUS_IDLE;
			UIEnable(IDCANCEL, FALSE);
			SetReadWriteButtonState();
			return false;
		}
	}
	return true;
}

LRESULT CMainDlg::OnImageFileKillFocus(WORD wNotifyCode, WORD wID, HWND hWndCtl)
{
	DoDataExchange(TRUE, IDC_IMAGEFILE);
	SetReadWriteButtonState();
	UpdateHashControls();

	CAtlFile file;
	SetDlgItemText(IDC_FILESIZE, NULL);
	if (SUCCEEDED(file.Create(m_strImageFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING)))
	{
		GetImageFileSize(file);
	}

	return 0;
}

template<typename T>
static CString FormatCapicity(const T& value)
{
	CString strText;;
	if (value.QuadPart >= 1024 * 1024 * 1024)
	{
		strText.Format(_T("%.2f GB"), (double)value.QuadPart / (1024 * 1024 * 1024));
	}
	else if (value.QuadPart >= 1024 * 1024)
	{
		strText.Format(_T("%.2f MB"), (double)value.QuadPart / (1024 * 1024));
	}
	else if (value.QuadPart >= 1024)
	{
		strText.Format(_T("%.2f KB"), (double)value.QuadPart / 1024);
	}
	else
	{
		strText.Format(_T("%d B"), value.LowPart);
	}
	return strText;
}

void CMainDlg::GetImageFileSize(HANDLE hFile)
{
	LARGE_INTEGER llSize;
	if (GetFileSizeEx(hFile, &llSize))
	{
		CString strText = FormatCapicity(llSize);
		SetDlgItemText(IDC_FILESIZE, strText);
	}
}

void CMainDlg::GetDeviceSize()
{
	int nIndex = m_wndDevice.GetCurSel();
	SetDlgItemText(IDC_DRIVESIZE, NULL);
	if (nIndex >= 0)
	{
		DWORD dwDevice = m_wndDevice.GetItemData(nIndex);
		CAtlFile device = GetHandleOnDevice(m_hWnd, dwDevice, GENERIC_READ);
		if (device != INVALID_HANDLE_VALUE)
		{
			ULARGE_INTEGER llSize = ::GetDeviceSize(m_hWnd, dwDevice, device);
			if (llSize.QuadPart > 0)
			{
				CString strText = FormatCapicity(llSize);
				SetDlgItemText(IDC_DRIVESIZE, strText);
			}
		}
	}
}

LRESULT CMainDlg::OnDeviceSelChange(WORD wNotifyCode, WORD wID, HWND hWndCtl)
{
	GetDeviceSize();
	return 0;
}

void CMainDlg::AddDrive(TCHAR nDrive)
{
	TCHAR szLabel[]=_T("[%c:\\]");
	szLabel[1]=nDrive;
	if (m_wndDevice.FindStringExact(-1, szLabel) == -1)
	{
		ULONG nDiskNumber;
		TCHAR szPath[] = _T("\\\\.\\A:\\");
		szPath[4] = nDrive;
		// checkDriveType gets the physicalID
		if (CheckDriveType(m_hWnd, szPath, &nDiskNumber))
		{
			CDiskInfo* pDiskInfo = FindDiskInfo(nDiskNumber);
			if(pDiskInfo)
			{
				pDiskInfo->m_Volumes.push_back(nDrive);
				std::sort(pDiskInfo->m_Volumes.begin(), pDiskInfo->m_Volumes.end());
				InitDeviceList();
			}
			SetReadWriteButtonState();
		}
	}
}
