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

#ifndef DISK_H
#define DISK_H

#include <stdio.h>
#include <stdlib.h>
#include <winioctl.h>
#include <vector>
#include <atlfile.h>

#ifndef FSCTL_IS_VOLUME_MOUNTED
#define FSCTL_IS_VOLUME_MOUNTED  CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 10, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif // FSCTL_IS_VOLUME_MOUNTED

typedef struct _DEVICE_NUMBER
{
    DEVICE_TYPE  DeviceType;
    ULONG  DeviceNumber;
    ULONG  PartitionNumber;
} DEVICE_NUMBER, *PDEVICE_NUMBER;

class CVolume
{
public:
	CVolume() : m_hMsgWnd(NULL), m_bLocked(false) { }
	~CVolume()
	{
		if (m_bLocked)
			Unlock();
	}
	bool Open(HWND hWnd, char volume, DWORD access);
	bool Lock();
	bool Unlock();
	DWORD GetDeviceID();
	bool Unmount();
	bool IsUnmounted();

private:
	CAtlFile m_File;
	HWND m_hMsgWnd;
	bool m_bLocked;
};

// IOCTL control code
#define IOCTL_STORAGE_QUERY_PROPERTY   CTL_CODE(IOCTL_STORAGE_BASE, 0x0500, METHOD_BUFFERED, FILE_ANY_ACCESS)

CAtlFile GetHandleOnFile(HWND hWnd, LPCTSTR filelocation, DWORD access);
CAtlFile GetHandleOnDevice(HWND hWnd, int device, DWORD access);
CString getDriveLabel(LPCTSTR drv);
void ReadSectorDataFromHandle(HWND hWnd, HANDLE handle, unsigned long long startsector, unsigned long long numsectors, unsigned long long sectorsize, std::vector<char>& SectorData);
bool WriteSectorDataToHandle(HWND hWnd, HANDLE handle, char *data, unsigned long long startsector, unsigned long long numsectors, unsigned long long sectorsize);
unsigned long long GetNumberOfSectors(HWND hWnd, HANDLE handle, unsigned long long *sectorsize);
unsigned long long GetFileSizeInSectors(HWND hWnd, HANDLE handle, unsigned long long sectorsize);
bool SpaceAvailable(HWND hWnd, LPCTSTR location, unsigned long long spaceneeded);
bool CheckDriveType(HWND hWnd, LPCTSTR name, ULONG *pid);

#endif // DISK_H
