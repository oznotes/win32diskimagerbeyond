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

#ifndef WINVER
#define WINVER 0x0601
#endif

#include <QtWidgets>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>
#include <winioctl.h>
#include "disk.h"
#include "mainwindow.h"

HANDLE getHandleOnFile(LPCWSTR filelocation, DWORD access)
{
	HANDLE hFile;
	hFile = CreateFileW(filelocation, access, (access == GENERIC_READ) ? FILE_SHARE_READ : 0, NULL, (access == GENERIC_READ) ? OPEN_EXISTING : CREATE_ALWAYS, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		wchar_t* errormessage = NULL;
		::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, GetLastError(), 0,
			(LPWSTR)&errormessage, 0, NULL);
		QString errText = QString::fromUtf16((const ushort*)errormessage);
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("File Error"), QObject::tr("An error occurred when attempting to get a handle on the file.\n"
			"Error %1: %2").arg(GetLastError()).arg(errText));
		LocalFree(errormessage);
	}
	return hFile;
}
DWORD getDeviceID(HANDLE hVolume)
{
	VOLUME_DISK_EXTENTS sd;
	DWORD bytesreturned;
	// Initialize sd to prevent accessing uninitialized data on failure
	memset(&sd, 0, sizeof(sd));

	if (!DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &sd, sizeof(sd), &bytesreturned, NULL))
	{
		wchar_t* errormessage = NULL;
		::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, GetLastError(), 0,
			(LPWSTR)&errormessage, 0, NULL);
		QString errText = QString::fromUtf16((const ushort*)errormessage);
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Volume Error"),
			QObject::tr("An error occurred when attempting to get information on volume.\n"
				"Error %1: %2").arg(GetLastError()).arg(errText));
		LocalFree(errormessage);
		// Return invalid value on error instead of accessing uninitialized data
		return (DWORD)-1;
	}
	// Verify we have at least one extent before accessing
	if (sd.NumberOfDiskExtents == 0)
	{
		return (DWORD)-1;
	}
	return sd.Extents[0].DiskNumber;
}

#ifdef DEBUG_LOGGING
void DebugToFile(QString txt)
{
	QString appDir = qApp->applicationDirPath() + "\\";
	QFile f(appDir + "debug.log");
	if (f.open(QIODevice::WriteOnly | QIODevice::Append))
	{
		QTextStream stream(&f);
		stream << txt << Qt::endl;
	}
}
#endif
// When DEBUG_LOGGING is not defined, the inline stub from disk.h is used

HANDLE getHandleOnDevice(int device, DWORD access)
{
	HANDLE hDevice;
	QString devicename = QString("\\\\.\\PhysicalDrive%1").arg(device);
	hDevice = CreateFile(devicename.toLatin1().data(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hDevice == INVALID_HANDLE_VALUE)
	{
		wchar_t* errormessage = NULL;
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, GetLastError(), 0, (LPWSTR)&errormessage, 0, NULL);
		QString errText = QString::fromUtf16((const ushort*)errormessage);
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Device Error"),
			QObject::tr("An error occurred when attempting to get a handle on the device.\n"
				"Error %1: %2").arg(GetLastError()).arg(errText));
		LocalFree(errormessage);
	}
	return hDevice;
}

HANDLE getHandleOnVolume(int volume, DWORD access)
{
	HANDLE hVolume;
	// Validate volume is in valid range (A-Z = 0-25)
	if (volume < 0 || volume > 25)
	{
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Volume Error"),
			QObject::tr("Invalid volume number: %1. Must be between 0 and 25.").arg(volume));
		return INVALID_HANDLE_VALUE;
	}
	char volumename[] = "\\\\.\\A:";
	volumename[4] += volume;
	hVolume = CreateFile(volumename, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hVolume == INVALID_HANDLE_VALUE)
	{
		wchar_t* errormessage = NULL;
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, GetLastError(), 0, (LPWSTR)&errormessage, 0, NULL);
		QString errText = QString::fromUtf16((const ushort*)errormessage);
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Volume Error"),
			QObject::tr("An error occurred when attempting to get a handle on the volume.\n"
				"Error %1: %2").arg(GetLastError()).arg(errText));
		LocalFree(errormessage);
	}
	return hVolume;
}

bool getLockOnVolume(HANDLE handle)
{
	DWORD bytesreturned;
	BOOL bResult;
	bResult = DeviceIoControl(handle, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesreturned, NULL);
	if (!bResult)
	{
		wchar_t* errormessage = NULL;
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, GetLastError(), 0, (LPWSTR)&errormessage, 0, NULL);
		QString errText = QString::fromUtf16((const ushort*)errormessage);
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Lock Error"),
			QObject::tr("An error occurred when attempting to lock the volume.\n"
				"Error %1: %2").arg(GetLastError()).arg(errText));
		LocalFree(errormessage);
	}
	return (bResult);
}

bool removeLockOnVolume(HANDLE handle)
{
	DWORD junk;
	BOOL bResult;
	bResult = DeviceIoControl(handle, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &junk, NULL);
	if (!bResult)
	{
		wchar_t* errormessage = NULL;
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, GetLastError(), 0, (LPWSTR)&errormessage, 0, NULL);
		QString errText = QString::fromUtf16((const ushort*)errormessage);
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Unlock Error"),
			QObject::tr("An error occurred when attempting to unlock the volume.\n"
				"Error %1: %2").arg(GetLastError()).arg(errText));
		LocalFree(errormessage);
	}
	return (bResult);
}

bool unmountVolume(HANDLE handle)
{
	DWORD junk;
	BOOL bResult;
	bResult = DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &junk, NULL);
	if (!bResult)
	{
		wchar_t* errormessage = NULL;
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, GetLastError(), 0, (LPWSTR)&errormessage, 0, NULL);
		QString errText = QString::fromUtf16((const ushort*)errormessage);
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Dismount Error"),
			QObject::tr("An error occurred when attempting to dismount the volume.\n"
				"Error %1: %2").arg(GetLastError()).arg(errText));
		LocalFree(errormessage);
	}
	return (bResult);
}

bool isVolumeUnmounted(HANDLE handle)
{
	DWORD junk;
	BOOL bResult;
	bResult = DeviceIoControl(handle, FSCTL_IS_VOLUME_MOUNTED, NULL, 0, NULL, 0, &junk, NULL);
	return (!bResult);
}

char* readSectorDataFromHandle(HANDLE handle, unsigned long long startsector, unsigned long long numsectors, unsigned long long sectorsize)
{
	// Validate parameters to prevent overflow and buffer issues
	if (sectorsize == 0 || sectorsize > 65536 || numsectors == 0)
	{
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Read Error"),
			QObject::tr("Invalid sector parameters: sectorsize=%1, numsectors=%2").arg(sectorsize).arg(numsectors));
		return NULL;
	}

	// Check for arithmetic overflow in offset calculation
	if (startsector > ULLONG_MAX / sectorsize)
	{
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Read Error"),
			QObject::tr("Sector offset overflow: startsector=%1, sectorsize=%2").arg(startsector).arg(sectorsize));
		return NULL;
	}

	// Check for overflow in buffer size calculation
	if (numsectors > ULLONG_MAX / sectorsize)
	{
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Read Error"),
			QObject::tr("Buffer size overflow: numsectors=%1, sectorsize=%2").arg(numsectors).arg(sectorsize));
		return NULL;
	}

	unsigned long bytesread = 0;  // Initialize to prevent undefined behavior on error
	unsigned long long bufferSize = sectorsize * numsectors;
	char* data = new char[bufferSize];
	LARGE_INTEGER li;
	li.QuadPart = startsector * sectorsize;

	// Use SetFilePointerEx for better large file support and error handling
	if (!SetFilePointerEx(handle, li, NULL, FILE_BEGIN))
	{
		DWORD err = GetLastError();
		wchar_t* errormessage = NULL;
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, err, 0, (LPWSTR)&errormessage, 0, NULL);
		QString errText = QString::fromUtf16((const ushort*)errormessage);
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Seek Error"),
			QObject::tr("An error occurred when attempting to seek in file.\n"
				"Error %1: %2").arg(err).arg(errText));
		LocalFree(errormessage);
		delete[] data;
		return NULL;
	}

	if (!ReadFile(handle, data, (DWORD)bufferSize, &bytesread, NULL))
	{
		DWORD err = GetLastError();
		wchar_t* errormessage = NULL;
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, err, 0, (LPWSTR)&errormessage, 0, NULL);
		QString errText = QString::fromUtf16((const ushort*)errormessage);
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Read Error"),
			QObject::tr("An error occurred when attempting to read data from handle.\n"
				"Error %1: %2").arg(err).arg(errText));
		LocalFree(errormessage);
		delete[] data;
		return NULL;
	}

	// Zero-fill any remaining bytes if partial read (e.g., end of file)
	// Guard condition prevents integer underflow in the subtraction
	if (bytesread < bufferSize)
	{
		size_t remaining = (size_t)(bufferSize - bytesread);
		memset(data + bytesread, 0, remaining);
	}
	return data;
}

bool writeSectorDataToHandle(HANDLE handle, char* data, unsigned long long startsector, unsigned long long numsectors, unsigned long long sectorsize)
{
	// Validate parameters to prevent overflow and buffer issues
	if (sectorsize == 0 || sectorsize > 65536 || numsectors == 0 || data == NULL)
	{
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Write Error"),
			QObject::tr("Invalid write parameters: sectorsize=%1, numsectors=%2").arg(sectorsize).arg(numsectors));
		return false;
	}

	// Check for arithmetic overflow in offset calculation
	if (startsector > ULLONG_MAX / sectorsize)
	{
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Write Error"),
			QObject::tr("Sector offset overflow: startsector=%1, sectorsize=%2").arg(startsector).arg(sectorsize));
		return false;
	}

	// Check for overflow in buffer size calculation
	if (numsectors > ULLONG_MAX / sectorsize)
	{
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Write Error"),
			QObject::tr("Buffer size overflow: numsectors=%1, sectorsize=%2").arg(numsectors).arg(sectorsize));
		return false;
	}

	unsigned long byteswritten = 0;
	BOOL bResult;
	unsigned long long bufferSize = sectorsize * numsectors;
	LARGE_INTEGER li;
	li.QuadPart = startsector * sectorsize;

	// Use SetFilePointerEx for better large file support and error handling
	if (!SetFilePointerEx(handle, li, NULL, FILE_BEGIN))
	{
		DWORD err = GetLastError();
		wchar_t* errormessage = NULL;
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, err, 0, (LPWSTR)&errormessage, 0, NULL);
		QString errText = QString::fromUtf16((const ushort*)errormessage);
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Seek Error"),
			QObject::tr("An error occurred when attempting to seek in file.\n"
				"Error %1: %2").arg(err).arg(errText));
		LocalFree(errormessage);
		return false;
	}

	bResult = WriteFile(handle, data, (DWORD)bufferSize, &byteswritten, NULL);
	if (!bResult)
	{
		DWORD err = GetLastError();
		wchar_t* errormessage = NULL;
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, err, 0, (LPWSTR)&errormessage, 0, NULL);
		QString errText = QString::fromUtf16((const ushort*)errormessage);
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Write Error"),
			QObject::tr("An error occurred when attempting to write data to handle.\n"
				"Error %1: %2").arg(err).arg(errText));
		LocalFree(errormessage);
		return false;
	}

	// Verify all bytes were written
	if (byteswritten != bufferSize)
	{
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Write Error"),
			QObject::tr("Incomplete write: expected %1 bytes, wrote %2 bytes").arg(bufferSize).arg(byteswritten));
		return false;
	}

	return true;
}

unsigned long long getNumberOfSectors(HANDLE handle, unsigned long long* sectorsize)
{
	DWORD junk;
	DISK_GEOMETRY_EX diskgeometry;
	BOOL bResult;
	bResult = DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &diskgeometry, sizeof(diskgeometry), &junk, NULL);
	if (!bResult)
	{
		wchar_t* errormessage = NULL;
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, GetLastError(), 0, (LPWSTR)&errormessage, 0, NULL);
		QString errText = QString::fromUtf16((const ushort*)errormessage);
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Device Error"),
			QObject::tr("An error occurred when attempting to get the device's geometry.\n"
				"Error %1: %2").arg(GetLastError()).arg(errText));
		LocalFree(errormessage);
		return 0;
	}
	if (sectorsize != NULL)
	{
		*sectorsize = (unsigned long long)diskgeometry.Geometry.BytesPerSector;
	}
	// Prevent division by zero
	if (diskgeometry.Geometry.BytesPerSector == 0)
	{
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Device Error"),
			QObject::tr("Invalid sector size (0) returned from device."));
		return 0;
	}
	return (unsigned long long)diskgeometry.DiskSize.QuadPart / (unsigned long long)diskgeometry.Geometry.BytesPerSector;
}

unsigned long long getFileSizeInSectors(HANDLE handle, unsigned long long sectorsize)
{
	unsigned long long retVal = 0;
	if (sectorsize) // avoid divide by 0
	{
		LARGE_INTEGER filesize;
		if (GetFileSizeEx(handle, &filesize) == 0)
		{
			// error
			wchar_t* errormessage = NULL;
			FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, GetLastError(), 0, (LPWSTR)&errormessage, 0, NULL);
			QString errText = QString::fromUtf16((const ushort*)errormessage);
			QMessageBox::critical(MainWindow::getInstance(), QObject::tr("File Error"),
				QObject::tr("An error occurred while getting the file size.\n"
					"Error %1: %2").arg(GetLastError()).arg(errText));
			LocalFree(errormessage);
			retVal = 0;
		}
		else
		{
			retVal = ((unsigned long long)filesize.QuadPart / sectorsize) + (((unsigned long long)filesize.QuadPart % sectorsize) ? 1 : 0);
		}
	}
	return(retVal);
}

bool spaceAvailable(char* location, unsigned long long spaceneeded)
{
	ULARGE_INTEGER freespace;
	BOOL bResult;
	bResult = GetDiskFreeSpaceEx(location, NULL, NULL, &freespace);
	if (!bResult)
	{
		wchar_t* errormessage = NULL;
		FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, GetLastError(), 0, (LPWSTR)&errormessage, 0, NULL);
		QString errText = QString::fromUtf16((const ushort*)errormessage);
		QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Free Space Error"),
			QObject::tr("Failed to get the free space on drive %1.\n"
				"Error %2: %3\n"
				"Operation cannot continue without verifying free space.").arg(location).arg(GetLastError()).arg(errText));
		LocalFree(errormessage);
		// Return false on error - caller should NOT proceed without knowing space is available
		return false;
	}
	return (spaceneeded <= freespace.QuadPart);
}

// given a drive letter (ending in a slash), return the label for that drive
// TODO make this more robust by adding input verification
QString getDriveLabel(const char* drv)
{
	QString retVal;
	int szNameBuf = MAX_PATH + 1;
	char* nameBuf = NULL;
	if ((nameBuf = (char*)calloc(szNameBuf, sizeof(char))) != 0)
	{
		::GetVolumeInformationA(drv, nameBuf, szNameBuf, NULL,
			NULL, NULL, NULL, 0);
	}

	// if malloc fails, nameBuf will be NULL.
	// if GetVolumeInfo fails, nameBuf will contain empty string
	// if all succeeds, nameBuf will contain label
	if (nameBuf == NULL)
	{
		retVal = QString("");
	}
	else
	{
		retVal = QString(nameBuf);
		free(nameBuf);
	}

	return(retVal);
}

BOOL GetDisksProperty(HANDLE hDevice, PSTORAGE_DEVICE_DESCRIPTOR pDevDesc,
	DEVICE_NUMBER* devInfo)
{
	STORAGE_PROPERTY_QUERY Query = {0}; // Zero-initialize to avoid garbage in AdditionalParameters
	DWORD dwOutBytes; // IOCTL output length
	BOOL bResult; // IOCTL return val
	BOOL retVal = true;

	// specify the query type
	Query.PropertyId = StorageDeviceProperty;
	Query.QueryType = PropertyStandardQuery;

	DebugToFile(QString("  GetDisksProperty: calling IOCTL_STORAGE_QUERY_PROPERTY"));

	// Query using IOCTL_STORAGE_QUERY_PROPERTY
	bResult = ::DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
		&Query, sizeof(STORAGE_PROPERTY_QUERY), pDevDesc,
		pDevDesc->Size, &dwOutBytes, (LPOVERLAPPED)NULL);

	// CRITICAL: Save GetLastError() IMMEDIATELY before any other calls (including DebugToFile which resets it!)
	DWORD queryError = GetLastError();

	DebugToFile(QString("  QUERY_PROPERTY result=%1, error=%2, bytesOut=%3").arg(bResult).arg(queryError).arg(dwOutBytes));

	if (bResult)
	{
		DebugToFile(QString("  QUERY_PROPERTY OK, BusType=%1, calling GET_DEVICE_NUMBER").arg(pDevDesc->BusType));
		bResult = ::DeviceIoControl(hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER,
			NULL, 0, devInfo, sizeof(DEVICE_NUMBER), &dwOutBytes,
			(LPOVERLAPPED)NULL);
		DWORD devNumError = GetLastError();
		DebugToFile(QString("  GET_DEVICE_NUMBER result=%1, error=%2, DevNum=%3").arg(bResult).arg(devNumError).arg(devInfo->DeviceNumber));
		if (!bResult)
		{
			retVal = false;
			wchar_t* errormessage = NULL;
			FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, devNumError, 0, (LPWSTR)&errormessage, 0, NULL);
			QString errText = QString::fromUtf16((const ushort*)errormessage);
			QMessageBox::critical(MainWindow::getInstance(), QObject::tr("File Error"),
				QObject::tr("An error occurred while getting the device number.\n"
					"This usually means something is currently accessing the device;"
					"please close all applications and try again.\n\nError %1: %2").arg(devNumError).arg(errText));
			LocalFree(errormessage);
		}
	}
	else
	{
		// queryError already saved above BEFORE DebugToFile calls
		DebugToFile(QString("  QUERY_PROPERTY FAILED, error=%1").arg(queryError));

		// These errors are expected for system drives, empty card readers, etc. - silently skip
		if (queryError == ERROR_INVALID_FUNCTION ||     // System/internal drives don't support this IOCTL
			queryError == ERROR_NOT_READY ||            // Drive not ready (empty card reader)
			queryError == ERROR_NO_MEDIA_IN_DRIVE)      // No media in drive (1112)
		{
			DebugToFile(QString("  Silently skipping (expected error for system/empty drives)"));
			retVal = false;
		}
		else
		{
			DebugToFile(QString("  UNEXPECTED error, showing dialog"));
			// Only show error dialog for truly unexpected failures
			wchar_t* errormessage = NULL;
			FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, queryError, 0, (LPWSTR)&errormessage, 0, NULL);
			QString errText = QString::fromUtf16((const ushort*)errormessage);
			QMessageBox::critical(MainWindow::getInstance(), QObject::tr("File Error"),
				QObject::tr("An error occurred while querying the properties.\n"
					"This usually means something is currently accessing the device;"
					" please close all applications and try again.\n\nError %1: %2").arg(queryError).arg(errText));
			LocalFree(errormessage);
			retVal = false;
		}
	}

	return(retVal);
}

// some routines fail if there's no trailing slash in a name,
// 		others fail if there is.  So this routine takes a name (trailing
// 		slash or no), and creates 2 versions - one with the slash, and one w/o
//
// 		CALLER MUST FREE THE 2 RETURNED STRINGS
bool slashify(char* str, char** slash, char** noSlash)
{
	bool retVal = false;
	*slash = NULL;
	*noSlash = NULL;

	if (str == NULL)
		return false;

	size_t strLen = strlen(str);
	if (strLen > 0)
	{
		if (*(str + strLen - 1) == '\\')
		{
			// trailing slash exists
			*slash = (char*)calloc((strLen + 1), sizeof(char));
			*noSlash = (char*)calloc(strLen, sizeof(char));
			if (*slash != NULL && *noSlash != NULL)
			{
				memcpy(*slash, str, strLen);
				(*slash)[strLen] = '\0';
				memcpy(*noSlash, *slash, strLen - 1);
				(*noSlash)[strLen - 1] = '\0';
				retVal = true;
			}
			else
			{
				// Clean up partial allocation
				free(*slash);
				free(*noSlash);
				*slash = NULL;
				*noSlash = NULL;
			}
		}
		else
		{
			// no trailing slash exists
			// Need strLen + 1 (backslash) + 1 (null) = strLen + 2 for slash buffer
			*slash = (char*)calloc((strLen + 2), sizeof(char));
			*noSlash = (char*)calloc((strLen + 1), sizeof(char));
			if (*slash != NULL && *noSlash != NULL)
			{
				memcpy(*noSlash, str, strLen);
				(*noSlash)[strLen] = '\0';
				// Use snprintf for safer string formatting
				// Ensure null termination even if _snprintf fills buffer exactly
				_snprintf(*slash, strLen + 2, "%s\\", *noSlash);
				(*slash)[strLen + 1] = '\0';  // Explicit null termination for safety
				retVal = true;
			}
			else
			{
				// Clean up partial allocation
				free(*slash);
				free(*noSlash);
				*slash = NULL;
				*noSlash = NULL;
			}
		}
	}
	return(retVal);
}

bool GetMediaType(HANDLE hDevice)
{
	DISK_GEOMETRY diskGeo;
	DWORD cbBytesReturned;
	BOOL result = DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &diskGeo, sizeof(diskGeo), &cbBytesReturned, NULL);
	DebugToFile(QString("  GetMediaType: IOCTL result=%1, error=%2, MediaType=%3 (Fixed=12, Removable=11)")
		.arg(result).arg(GetLastError()).arg(result ? diskGeo.MediaType : -1));
	if (result)
	{
		if ((diskGeo.MediaType == FixedMedia) || (diskGeo.MediaType == RemovableMedia))
		{
			DebugToFile(QString("  GetMediaType: returning TRUE (not a floppy)"));
			return true; // Not a floppy
		}
		DebugToFile(QString("  GetMediaType: returning FALSE (floppy or unknown media)"));
	}
	return false;
}

bool checkDriveType(char* name, ULONG* pid)
{
	HANDLE hDevice;
	PSTORAGE_DEVICE_DESCRIPTOR pDevDesc;
	DEVICE_NUMBER deviceInfo;
	bool retVal = false;
	char* nameWithSlash;
	char* nameNoSlash;
	int driveType;
	DWORD cbBytesReturned;

	DebugToFile(QString("checkDriveType: name=%1").arg(name));

	// some calls require no tailing slash, some require a trailing slash...
	if (!(slashify(name, &nameWithSlash, &nameNoSlash)))
	{
		DebugToFile(QString("  slashify FAILED"));
		return(retVal);
	}

	driveType = GetDriveType(nameWithSlash);
	DebugToFile(QString("  driveType=%1 (REMOVABLE=2, FIXED=3, REMOTE=4, CDROM=5)").arg(driveType));

	switch (driveType)
	{
	case DRIVE_REMOVABLE: // The media can be removed from the drive.
	  //or Virtual [e.g. PCloud] [TODO]
	case DRIVE_FIXED:     // The media cannot be removed from the drive. Some USB drives report as this.
		hDevice = CreateFile(nameNoSlash, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hDevice == INVALID_HANDLE_VALUE)
		{
			DebugToFile(QString("  CreateFile FAILED, error=%1").arg(GetLastError()));
			wchar_t* errormessage = NULL;
			FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, GetLastError(), 0, (LPWSTR)&errormessage, 0, NULL);
			QString errText = QString::fromUtf16((const ushort*)errormessage);
			QMessageBox::critical(MainWindow::getInstance(), QObject::tr("Volume Error"),
				QObject::tr("An error occurred when attempting to get a handle on %3.\n"
					"Error %1: %2").arg(GetLastError()).arg(errText).arg(nameWithSlash));
			LocalFree(errormessage);
		}
		else
		{
			DebugToFile(QString("  CreateFile OK, handle=%1").arg((quintptr)hDevice));
			int arrSz = sizeof(STORAGE_DEVICE_DESCRIPTOR) + 512 - 1;
			pDevDesc = (PSTORAGE_DEVICE_DESCRIPTOR)new BYTE[arrSz];
			pDevDesc->Size = arrSz;

			// get the device number if the drive is
			// removable or (fixed AND on the usb bus, SD, or MMC (undefined in XP/mingw))
			bool mediaOk = GetMediaType(hDevice);
			DebugToFile(QString("  GetMediaType=%1").arg(mediaOk));

			bool propsOk = false;
			if (mediaOk) {
				propsOk = GetDisksProperty(hDevice, pDevDesc, &deviceInfo);
				DebugToFile(QString("  GetDisksProperty=%1, BusType=%2 (USB=7,SATA=11,SD=12,MMC=13), DevNum=%3")
					.arg(propsOk).arg(pDevDesc->BusType).arg(deviceInfo.DeviceNumber));
			}

			bool busTypeOk = false;
			if (propsOk) {
				busTypeOk = (((driveType == DRIVE_REMOVABLE) && (pDevDesc->BusType != BusTypeSata))
					|| ((driveType == DRIVE_FIXED) && ((pDevDesc->BusType == BusTypeUsb)
						|| (pDevDesc->BusType == BusTypeSd) || (pDevDesc->BusType == BusTypeMmc))));
				DebugToFile(QString("  busTypeOk=%1 (driveType=%2, BusType=%3)").arg(busTypeOk).arg(driveType).arg(pDevDesc->BusType));
			}

			if (mediaOk && propsOk && busTypeOk)
			{
				// ensure that the drive is actually accessible
				// multi-card hubs were reporting "removable" even when empty
				BOOL verifyResult = DeviceIoControl(hDevice, IOCTL_STORAGE_CHECK_VERIFY2, NULL, 0, NULL, 0, &cbBytesReturned, (LPOVERLAPPED)NULL);
				DebugToFile(QString("  CHECK_VERIFY2=%1, error=%2").arg(verifyResult).arg(GetLastError()));
				if (verifyResult)
				{
					*pid = deviceInfo.DeviceNumber;
					retVal = true;
					DebugToFile(QString("  SUCCESS via CHECK_VERIFY2, pid=%1").arg(*pid));
				}
				else
					// IOCTL_STORAGE_CHECK_VERIFY2 fails on some devices under XP/Vista, try the other (slower) method, just in case.
				{
					CloseHandle(hDevice);
					hDevice = CreateFile(nameNoSlash, FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
					verifyResult = DeviceIoControl(hDevice, IOCTL_STORAGE_CHECK_VERIFY, NULL, 0, NULL, 0, &cbBytesReturned, (LPOVERLAPPED)NULL);
					DebugToFile(QString("  CHECK_VERIFY (fallback)=%1, error=%2").arg(verifyResult).arg(GetLastError()));
					if (verifyResult)
					{
						*pid = deviceInfo.DeviceNumber;
						retVal = true;
						DebugToFile(QString("  SUCCESS via CHECK_VERIFY fallback, pid=%1").arg(*pid));
					}
				}
			}

			delete[] pDevDesc;
			CloseHandle(hDevice);
		}

		break;
	default:
		DebugToFile(QString("  SKIPPED - driveType %1 not REMOVABLE or FIXED").arg(driveType));
		retVal = false;
	}

	// free the strings allocated by slashify
	free(nameWithSlash);
	free(nameNoSlash);

	return(retVal);
}

// Note: isDriveVirtual() was removed - it was dead code (never called)
// If virtual drive detection is needed in the future, implement it properly

bool isDriveIgnored(char drive)
{
	// DebugToFile(QString("Comparing %1").arg(drive));
	if (ignoredDrives.indexOf(drive) > -1) return true;
	return false;
}

// Enumerate all physical drives that are removable/USB and return their device numbers and sizes
QList<QPair<DWORD, qulonglong>> enumeratePhysicalDrives()
{
	QList<QPair<DWORD, qulonglong>> drives;

	DebugToFile(QString("enumeratePhysicalDrives: scanning PhysicalDrive0-15"));

	for (int i = 0; i < 16; i++)
	{
		QString devicePath = QString("\\\\.\\PhysicalDrive%1").arg(i);
		HANDLE hDevice = CreateFileA(devicePath.toLatin1().data(),
			FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, 0, NULL);

		if (hDevice == INVALID_HANDLE_VALUE)
		{
			continue; // Drive doesn't exist
		}

		// Query storage properties to get bus type
		STORAGE_PROPERTY_QUERY Query = {0};
		Query.PropertyId = StorageDeviceProperty;
		Query.QueryType = PropertyStandardQuery;

		int arrSz = sizeof(STORAGE_DEVICE_DESCRIPTOR) + 512 - 1;
		PSTORAGE_DEVICE_DESCRIPTOR pDevDesc = (PSTORAGE_DEVICE_DESCRIPTOR)new BYTE[arrSz];
		// Null check after allocation
		if (pDevDesc == NULL)
		{
			CloseHandle(hDevice);
			continue;
		}
		pDevDesc->Size = arrSz;

		DWORD dwOutBytes;
		BOOL bResult = DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
			&Query, sizeof(Query), pDevDesc, pDevDesc->Size, &dwOutBytes, NULL);

		if (bResult)
		{
			// Check if it's a removable/USB/SD/MMC device
			bool isRemovable = (pDevDesc->RemovableMedia != 0);
			bool isUSB = (pDevDesc->BusType == BusTypeUsb);
			bool isSD = (pDevDesc->BusType == BusTypeSd);
			bool isMMC = (pDevDesc->BusType == BusTypeMmc);

			DebugToFile(QString("  PhysicalDrive%1: BusType=%2, Removable=%3")
				.arg(i).arg(pDevDesc->BusType).arg(isRemovable));

			// USB drives must also be marked as removable (internal USB controllers report BusType=7 but Removable=0)
			// SD and MMC are always removable by design
			if ((isUSB && isRemovable) || isSD || isMMC)
			{
				// Get drive size
				DISK_GEOMETRY_EX diskGeo;
				DWORD bytesRet;
				qulonglong size = 0;

				if (DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
					NULL, 0, &diskGeo, sizeof(diskGeo), &bytesRet, NULL))
				{
					size = diskGeo.DiskSize.QuadPart;
				}

				DebugToFile(QString("  -> Added: PhysicalDrive%1 (%2 bytes)").arg(i).arg(size));
				drives.append(qMakePair((DWORD)i, size));
			}
		}

		delete[] pDevDesc;
		CloseHandle(hDevice);
	}

	DebugToFile(QString("enumeratePhysicalDrives: found %1 removable drives").arg(drives.count()));
	return drives;
}

void loadDriveIgnoreList()
{
	ignoredDrives = "";
	/*
	[todo] add more paths to look for the config file
		   or this could also be moved to the registry
		   but it will be harder to edit
		   so I'm sticking with config file for now
	*/
	QString appDir = qApp->applicationDirPath() + "\\";
	QString fileName = appDir + "ignored_drives.cfg";

	if (!QFileInfo(fileName).exists()) return;

	QFile inputFile(fileName);
	if (inputFile.open(QIODevice::ReadOnly))
	{
		QTextStream in(&inputFile);
		while (!in.atEnd())
		{
			QString line = in.readLine();
			line = line.trimmed();
			if (line == "") continue; //empty line
			QString firstChar = line.left(1);
			if ((firstChar == "#") || (firstChar == ";")) continue; //comments

			if (firstChar.at(0).isLetter()) //otherwise it's probably a drive letter
			{
				ignoredDrives += firstChar.toUpper(); //duplicates aren't important
			}
		}
		inputFile.close();
	}
	// DebugToFile("Drive ignore list: " + ignoredDrives);
}