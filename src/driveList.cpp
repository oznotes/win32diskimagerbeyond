#include <QCoreApplication>
#include <windows.h>

#include <stdio.h>
#include <time.h>
#include <process.h>
#include <wincrypt.h>
//#include "getopt.h"

#define BYTES_PER_ELEMENT (3)
#define SECTORS_PER_READ (64)

QString list_device(char* format_str, char* szTmp, int n) {
	Q_UNUSED(format_str);
	Q_UNUSED(szTmp);
	Q_UNUSED(n);
	
	QString result;
	DWORD drives = GetLogicalDrives();
	int i;
	char driveName[32];  // Buffer for PhysicalDrive name

	for (i = 0; i < 32; i++) {
		if (!(drives & (1 << i))) {
			continue;
		}

		// Use snprintf for safe string formatting (handles i >= 10 correctly)
		_snprintf(driveName, sizeof(driveName), "\\\\.\\PhysicalDrive%d", i);
		HANDLE hDevice = CreateFileA(driveName, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, 0, NULL);
			
		if (hDevice == INVALID_HANDLE_VALUE) {
			continue;
		}
		
		DISK_GEOMETRY_EX diskGeometry;
		DWORD bytesReturned;
		
		if (DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
			NULL, 0, &diskGeometry, sizeof(diskGeometry), &bytesReturned, NULL))
		{
			result = QString("\\\\.\\PhysicalDrive%1").arg(i);
		}
		
		CloseHandle(hDevice);
	}
	
	return result;
}

QList<QString> list_devices() {
	QList<QString> DriveList;
	QString device;

	char* format_str = (char*)"%-30s %9S %-9s %-20S\n";
	char szTmp[MAX_PATH];
	int i;

	for (i = 0; i < 64; i++) {
		_snprintf(szTmp, sizeof(szTmp), "\\\\.\\PhysicalDrive%d", i);
		device = list_device(format_str, szTmp, 0);
		DriveList.append(device);
	}

	return DriveList;
}
