/**********************************************************************
 *  This program is free software; you can redistribute it and/or     *
 *  modify it under the terms of the GNU General Public License       *
 *  as published by the Free Software Foundation; either version 2    *
 *  of the License, or (at your option) any later version.            *
 *                                                                    *
 *  This program is distributed in the hope that it will be useful,   *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of    *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the     *
 *  GNU General Public License for more details.                      *
 *                                                                    *
 *  You should have received a copy of the GNU General Public License *
 *  along with this program; if not, see http://gnu.org/licenses/     *
 *  ---                                                               *
 *  Copyright (C) 2009, Justin Davis <tuxdavis@gmail.com>             *
 *  Copyright (C) 2009-2017 ImageWriter developers                    *
 *                 https://sourceforge.net/projects/win32diskimager/  *
 **********************************************************************/

#include "stdafx.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>
#include <winioctl.h>
#include "disk.h"
#include "resource.h"

void ShowErrorMessage(HWND hWnd, LPCTSTR lpszMessage, LPCTSTR lpszTail=NULL)
{
	LPTSTR errormessage = NULL;
	DWORD dwError = GetLastError();
	::FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, dwError, 0,
		(LPWSTR)&errormessage, 0, NULL);
	CString errText;
	errText.Format(_T("%s\nError %d: %s"), lpszMessage, dwError, errormessage);
	if (lpszTail)
		errText.AppendFormat(_T("\n%s"), lpszTail);
	AtlMessageBox(hWnd,  (LPCTSTR)errText, IDR_MAINFRAME, MB_ICONERROR | MB_OK);
	LocalFree(errormessage);
}

CAtlFile GetHandleOnFile(HWND hWnd, LPCTSTR filelocation, DWORD access)
{
	CAtlFile file;
    if(FAILED(file.Create(filelocation, access, (access == GENERIC_READ) ? FILE_SHARE_READ : 0, (access == GENERIC_READ) ? OPEN_EXISTING:CREATE_ALWAYS)))
    {
		ShowErrorMessage(hWnd, _T("An error occurred when attempting to get a handle on the file."));
    }
    return file;
}

DWORD CVolume::GetDeviceID()
{
    VOLUME_DISK_EXTENTS sd;
    DWORD bytesreturned;
    if (!DeviceIoControl(m_File, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &sd, sizeof(sd), &bytesreturned, NULL))
    {
		ShowErrorMessage(m_hMsgWnd, _T("An error occurred when attempting to get information on volume."));
    }
    return sd.Extents[0].DiskNumber;
}

CAtlFile GetHandleOnDevice(HWND hWnd, int device, DWORD access)
{
    CAtlFile DeviceFile;
	CString strDeviceName;
	strDeviceName.Format(_T("\\\\.\\PhysicalDrive%d"), device);
	if(FAILED(DeviceFile.Create(strDeviceName, access, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING)))
    {
		ShowErrorMessage(hWnd, _T("An error occurred when attempting to get a handle on the device."));
    }
    return DeviceFile;
}

bool CVolume::Open(HWND hWnd, char volume, DWORD access)
{
    TCHAR szVolumeName[] = _T("\\\\.\\A:");
	szVolumeName[4] += volume-'A';
	m_hMsgWnd = hWnd;
	if(FAILED(m_File.Create(szVolumeName, access, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING)))
    {
		ShowErrorMessage(m_hMsgWnd, _T("An error occurred when attempting to get a handle on the volume."));
		return false;
    }
    return true;
}

bool CVolume::Lock()
{
    DWORD bytesreturned;
    BOOL bResult;
    bResult = DeviceIoControl(m_File, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesreturned, NULL);
	if (bResult)
		m_bLocked = true;
	else
		ShowErrorMessage(m_hMsgWnd, _T("An error occurred when attempting to lock the volume."));
    return (bResult);
}

bool CVolume::Unlock()
{
    DWORD junk;
    BOOL bResult;
    bResult = DeviceIoControl(m_File, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &junk, NULL);
	if (bResult)
		m_bLocked = false;
	else
		ShowErrorMessage(m_hMsgWnd, _T("An error occurred when attempting to unlock the volume."));
    return (bResult);
}

bool CVolume::Unmount()
{
    DWORD junk;
    BOOL bResult;
    bResult = DeviceIoControl(m_File, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &junk, NULL);
    if (!bResult)
    {
		ShowErrorMessage(m_hMsgWnd, _T("An error occurred when attempting to dismount the volume."));
    }
    return (bResult);
}

bool CVolume::IsUnmounted()
{
    DWORD junk;
    BOOL bResult;
    bResult = DeviceIoControl(m_File, FSCTL_IS_VOLUME_MOUNTED, NULL, 0, NULL, 0, &junk, NULL);
    return (!bResult);
}

void ReadSectorDataFromHandle(HWND hWnd, HANDLE handle, unsigned long long startsector, unsigned long long numsectors, unsigned long long sectorsize,
	std::vector<char>& SectorData)
{
    unsigned long bytesread;
	SectorData.resize(sectorsize * numsectors);
    LARGE_INTEGER li;
    li.QuadPart = startsector * sectorsize;
    SetFilePointer(handle, li.LowPart, &li.HighPart, FILE_BEGIN);
    if (!ReadFile(handle, SectorData.data(), SectorData.size(), &bytesread, NULL))
    {
		ShowErrorMessage(hWnd, _T("An error occurred when attempting to read data from handle."));
		SectorData.clear();
    }
    if (bytesread < (sectorsize * numsectors))
    {
		SectorData.resize(bytesread);
    }
}

bool WriteSectorDataToHandle(HWND hWnd, HANDLE handle, char *data, unsigned long long startsector, unsigned long long numsectors, unsigned long long sectorsize)
{
    unsigned long byteswritten;
    BOOL bResult;
    LARGE_INTEGER li;
    li.QuadPart = startsector * sectorsize;
    SetFilePointer(handle, li.LowPart, &li.HighPart, FILE_BEGIN);
    bResult = WriteFile(handle, data, sectorsize * numsectors, &byteswritten, NULL);
    if (!bResult)
    {
		ShowErrorMessage(hWnd, _T("An error occurred when attempting to write data to handle."));
    }
    return (bResult);
}

unsigned long long GetNumberOfSectors(HWND hWnd, HANDLE handle, unsigned long long *sectorsize)
{
    DWORD junk;
    DISK_GEOMETRY_EX diskgeometry;
    BOOL bResult;
    bResult = DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &diskgeometry, sizeof(diskgeometry), &junk, NULL);
    if (!bResult)
    {
		ShowErrorMessage(hWnd, _T("An error occurred when attempting to get the device's geometry."));
        return 0;
    }
    if (sectorsize != NULL)
    {
        *sectorsize = (unsigned long long)diskgeometry.Geometry.BytesPerSector;
    }
    return (unsigned long long)diskgeometry.DiskSize.QuadPart / (unsigned long long)diskgeometry.Geometry.BytesPerSector;
}

unsigned long long GetFileSizeInSectors(HWND hWnd, HANDLE handle, unsigned long long sectorsize)
{
    unsigned long long retVal = 0;
    if (sectorsize) // avoid divide by 0
    {
        LARGE_INTEGER filesize;
        if(GetFileSizeEx(handle, &filesize) == 0)
        {
			ShowErrorMessage(hWnd, _T("An error occurred when attempting to get the device's geometry."));
            retVal = 0;
        }
        else
        {
            retVal = ((unsigned long long)filesize.QuadPart / sectorsize ) + (((unsigned long long)filesize.QuadPart % sectorsize )?1:0);
        }
    }
    return(retVal);
}

bool SpaceAvailable(HWND hWnd, LPCTSTR location, unsigned long long spaceneeded)
{
    ULARGE_INTEGER freespace;
    BOOL bResult;
    bResult = GetDiskFreeSpaceEx(location, NULL, NULL, &freespace);
    if (!bResult)
    {
		CString strMessage;
		strMessage.Format(_T("Failed to get the free space on drive %s."), location);
		ShowErrorMessage(hWnd, strMessage, _T("Checking of free space will be skipped."));
        return true;
    }
    return (spaceneeded <= freespace.QuadPart);
}

// given a drive letter (ending in a slash), return the label for that drive
// TODO make this more robust by adding input verification
CString getDriveLabel(LPCTSTR drv)
{
    CString retVal;
    int szNameBuf = MAX_PATH + 1;
    LPTSTR nameBuf = retVal.GetBuffer(szNameBuf);
    ::GetVolumeInformation(drv, nameBuf, szNameBuf, NULL,
                                        NULL, NULL, NULL, 0);

    // if malloc fails, nameBuf will be NULL.
    // if GetVolumeInfo fails, nameBuf will contain empty string
    // if all succeeds, nameBuf will contain label
	retVal.ReleaseBuffer();
    return retVal;
}

BOOL GetDisksProperty(HWND hWnd, HANDLE hDevice, PSTORAGE_DEVICE_DESCRIPTOR pDevDesc,
                      DEVICE_NUMBER *devInfo)
{
    STORAGE_PROPERTY_QUERY Query; // input param for query
    DWORD dwOutBytes; // IOCTL output length
    BOOL bResult; // IOCTL return val
    BOOL retVal = true;
    DWORD cbBytesReturned;

    // specify the query type
    Query.PropertyId = StorageDeviceProperty;
    Query.QueryType = PropertyStandardQuery;

    // Query using IOCTL_STORAGE_QUERY_PROPERTY
    bResult = ::DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
                &Query, sizeof(STORAGE_PROPERTY_QUERY), pDevDesc,
                pDevDesc->Size, &dwOutBytes, (LPOVERLAPPED)NULL);
    if (bResult)
    {
        bResult = ::DeviceIoControl(hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER,
                    NULL, 0, devInfo, sizeof(DEVICE_NUMBER), &dwOutBytes,
                    (LPOVERLAPPED)NULL);
        if (!bResult)
        {
            retVal = false;
			ShowErrorMessage(hWnd, _T("An error occurred while getting the device number.\n")
				_T("This usually means something is currently accessing the device;")
				_T("please close all applications and try again."));
        }
    }
    else
    {
        if (DeviceIoControl(hDevice, IOCTL_STORAGE_CHECK_VERIFY2, NULL, 0, NULL, 0, &cbBytesReturned,
                            (LPOVERLAPPED) NULL))
        {
			ShowErrorMessage(hWnd, _T("An error occurred while querying the properties.\n")
				_T("This usually means something is currently accessing the device;")
				_T(" please close all applications and try again."));
        }
            retVal = false;
    }

    return(retVal);
}

// some routines fail if there's no trailing slash in a name,
// 		others fail if there is.  So this routine takes a name (trailing
// 		slash or no), and creates 2 versions - one with the slash, and one w/o
//
// 		CALLER MUST FREE THE 2 RETURNED STRINGS
bool slashify(LPCTSTR str, LPTSTR *slash, LPTSTR *noSlash)
{
	bool retVal = false;
	size_t strLen = _tcslen(str);
	if (strLen > 0)
	{
		if (*(str + strLen - 1) == '\\')
		{
			// trailing slash exists
			if (((*slash = (TCHAR *)calloc((strLen + 1), sizeof(TCHAR))) != NULL) &&
				((*noSlash = (TCHAR *)calloc(strLen, sizeof(TCHAR))) != NULL))
			{
				_tcsncpy(*slash, str, strLen);
				_tcsncpy(*noSlash, *slash, (strLen - 1));
				retVal = true;
			}
		}
		else
		{
			// no trailing slash exists
			if (((*slash = (TCHAR *)calloc((strLen + 2), sizeof(TCHAR))) != NULL) &&
				((*noSlash = (TCHAR *)calloc((strLen + 1), sizeof(TCHAR))) != NULL))
			{
				_tcsncpy(*noSlash, str, strLen);
				_stprintf(*slash, _T("%s\\"), *noSlash);
				retVal = true;
			}
		}
	}
	return(retVal);
}

bool GetMediaType(HANDLE hDevice)
{
    DISK_GEOMETRY diskGeo;
    DWORD cbBytesReturned;
    if (DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY,NULL, 0, &diskGeo, sizeof(diskGeo), &cbBytesReturned, NULL))
    {
        if ((diskGeo.MediaType == FixedMedia) || (diskGeo.MediaType == RemovableMedia))
        {
            return true; // Not a floppy
        }
    }
    return false;
}

bool CheckDriveType(HWND hWnd, LPCTSTR name, ULONG *pid)
{
    HANDLE hDevice;
    PSTORAGE_DEVICE_DESCRIPTOR pDevDesc;
    DEVICE_NUMBER deviceInfo;
    bool retVal = false;
    LPTSTR nameWithSlash;
    LPTSTR nameNoSlash;
    int driveType;
    DWORD cbBytesReturned;

    // some calls require no tailing slash, some require a trailing slash...
    if ( !(slashify(name, &nameWithSlash, &nameNoSlash)) )
    {
        return(retVal);
    }

    driveType = GetDriveType(nameWithSlash);
    switch( driveType )
    {
    case DRIVE_REMOVABLE: // The media can be removed from the drive.
    case DRIVE_FIXED:     // The media cannot be removed from the drive. Some USB drives report as this.
        hDevice = CreateFile(nameNoSlash, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hDevice == INVALID_HANDLE_VALUE)
        {
			CString strMessage;
			strMessage.Format(_T("An error occurred when attempting to get a handle on %s."), nameWithSlash);
			ShowErrorMessage(hWnd, strMessage);
        }
        else
        {
            int arrSz = sizeof(STORAGE_DEVICE_DESCRIPTOR) + 512 - 1;
            pDevDesc = (PSTORAGE_DEVICE_DESCRIPTOR)new BYTE[arrSz];
            pDevDesc->Size = arrSz;

            // get the device number if the drive is
            // removable or (fixed AND on the usb bus, SD, or MMC (undefined in XP/mingw))
            if(GetMediaType(hDevice) && GetDisksProperty(hWnd, hDevice, pDevDesc, &deviceInfo) &&
                    ( ((driveType == DRIVE_REMOVABLE) && (pDevDesc->BusType != BusTypeSata))
                      || ( (driveType == DRIVE_FIXED) && ((pDevDesc->BusType == BusTypeUsb)
                      || (pDevDesc->BusType == BusTypeSd ) || (pDevDesc->BusType == BusTypeMmc )) ) ) )
            {
                // ensure that the drive is actually accessible
                // multi-card hubs were reporting "removable" even when empty
                if(DeviceIoControl(hDevice, IOCTL_STORAGE_CHECK_VERIFY2, NULL, 0, NULL, 0, &cbBytesReturned, (LPOVERLAPPED) NULL))
                {
                    *pid = deviceInfo.DeviceNumber;
                    retVal = true;
                }
                else
                // IOCTL_STORAGE_CHECK_VERIFY2 fails on some devices under XP/Vista, try the other (slower) method, just in case.
                {
                    CloseHandle(hDevice);
                    hDevice = CreateFile(nameNoSlash, FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
                    if(DeviceIoControl(hDevice, IOCTL_STORAGE_CHECK_VERIFY, NULL, 0, NULL, 0, &cbBytesReturned, (LPOVERLAPPED) NULL))
                    {
                        *pid = deviceInfo.DeviceNumber;
                        retVal = true;
                    }
                }
            }

            delete[] pDevDesc;
            CloseHandle(hDevice);
        }

        break;
    default:
        retVal = false;
    }

    // free the strings allocated by slashify
    free(nameWithSlash);
    free(nameNoSlash);

    return(retVal);
}
