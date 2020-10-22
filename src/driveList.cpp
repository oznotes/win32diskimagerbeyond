#include <QCoreApplication>
#include <windows.h>

#include <stdio.h>
#include <time.h>
#include <process.h>
#include <wincrypt.h>
#include "getopt.h"

#define BYTES_PER_ELEMENT (3)
#define SECTORS_PER_READ (64)

typedef enum {
	WIPEMODE_NORMAL,
	WIPEMODE_DOD,
	WIPEMODE_DOD7,
	WIPEMODE_GUTMANN,
	WIPEMODE_DOE,
	WIPEMODE_SCHNEIER,
	WIPEMODE_BCI
} WipeMode;

typedef enum {
	RANDOM_NONE,
	RANDOM_PSEUDO,
	RANDOM_WINDOWS,
#ifdef HAVE_CRYPTOGRAPHIC
	RANDOM_CRYPTOGRAPHIC
#endif
} RandomMode;

typedef enum {
	EXIT_NONE,
	EXIT_POWEROFF,
	EXIT_SHUTDOWN,
	EXIT_HIBERNATE,
	EXIT_LOGOFF,
	EXIT_REBOOT,
	EXIT_STANDBY
} ExitMode;

struct _opt {
	bool			list;
	unsigned int	passes;
	WipeMode		mode;
	bool			yes;
	RandomMode		random;
	ExitMode		restart;
	bool			force;
	unsigned int	quiet;
	unsigned int	sectors;
	ULONGLONG		start;
	ULONGLONG		end;
	bool			read;
	unsigned int	help;
	bool			kilobyte;
	unsigned int	refresh;
	bool			ignore;
};

typedef struct _opt t_opt;

static t_opt opt = {
	false,				/* list */
	0,					/* passes */
	WIPEMODE_NORMAL,	/* normal, dod, dod7, gutmann */
	false,				/* yes */
	RANDOM_NONE,		/* pseudo, windows, cryptographic */
	EXIT_NONE,			/* none, poweroff, shutdown, hibernate, logoff, reboot, standby */
	false,				/* force */
	0,					/* quiet */
	SECTORS_PER_READ,	/* sectors */
	Q_UINT64_C(0),		/* start */
	Q_UINT64_C(0),		/* end */
	false,				/* read */
	0,					/* help */
	false,				/* kilobyte */
	1,					/* refresh */
	false,				/* ignore */
};

static void GetSizeString(LONGLONG size, wchar_t* str) {
	static const wchar_t* b, * kb, * mb, * gb, * tb, * pb;

	if (b == NULL) {
		if (opt.kilobyte) {
			kb = L"KiB";
			mb = L"MiB";
			gb = L"GiB";
			tb = L"TiB";
			pb = L"PiB";
		}
		else {
			kb = L"KB";
			mb = L"MB";
			gb = L"GB";
			tb = L"TB";
			pb = L"PB";
		}
		b = L"bytes";
	}

	DWORD kilo = opt.kilobyte ? 1024 : 1000;
	LONGLONG kiloI64 = kilo;
	double kilod = kilo;

	if (size > kiloI64 * kilo * kilo * kilo * kilo * 99)
		swprintf(str, L"%I64d %s", size / kilo / kilo / kilo / kilo / kilo, pb);
	else if (size > kiloI64 * kilo * kilo * kilo * kilo)
		swprintf(str, L"%.1f %s", (double)(size / kilod / kilo / kilo / kilo / kilo), pb);
	else if (size > kiloI64 * kilo * kilo * kilo * 99)
		swprintf(str, L"%I64d %s", size / kilo / kilo / kilo / kilo, tb);
	else if (size > kiloI64 * kilo * kilo * kilo)
		swprintf(str, L"%.1f %s", (double)(size / kilod / kilo / kilo / kilo), tb);
	else if (size > kiloI64 * kilo * kilo * 99)
		swprintf(str, L"%I64d %s", size / kilo / kilo / kilo, gb);
	else if (size > kiloI64 * kilo * kilo)
		swprintf(str, L"%.1f %s", (double)(size / kilod / kilo / kilo), gb);
	else if (size > kiloI64 * kilo * 99)
		swprintf(str, L"%I64d %s", size / kilo / kilo, mb);
	else if (size > kiloI64 * kilo)
		swprintf(str, L"%.1f %s", (double)(size / kilod / kilo), mb);
	else if (size > kiloI64)
		swprintf(str, L"%I64d %s", size / kilo, kb);
	else
		swprintf(str, L"%I64d %s", size, b);
}

static int FakeDosNameForDevice(char* lpszDiskFile, char* lpszDosDevice, char* lpszCFDevice, BOOL bNameOnly) {
	if (strncmp(lpszDiskFile, "\\\\", 2) == 0) {
		strcpy(lpszCFDevice, lpszDiskFile);
		return 1;
	}

	BOOL bDosLinkCreated = TRUE;
	_snprintf(lpszDosDevice, MAX_PATH, "dskwipe%lu", GetCurrentProcessId());

	if (bNameOnly == FALSE)
		bDosLinkCreated = DefineDosDeviceA(DDD_RAW_TARGET_PATH, lpszDosDevice, lpszDiskFile);

	if (bDosLinkCreated == FALSE) {
		return 1;
	}
	else {
		_snprintf(lpszCFDevice, MAX_PATH, "\\\\.\\%s", lpszDosDevice);
	}

	return 0;
}

static int RemoveFakeDosName(char* lpszDiskFile, char* lpszDosDevice) {
	BOOL bDosLinkRemoved = DefineDosDeviceA(DDD_RAW_TARGET_PATH | DDD_EXACT_MATCH_ON_REMOVE |
		DDD_REMOVE_DEFINITION, lpszDosDevice, lpszDiskFile);
	if (bDosLinkRemoved == FALSE) {
		return 1;
	}

	return 0;
}

static QString list_device(char* format_str, char* szTmp, int n) {
	int nDosLinkCreated;
	HANDLE dev;
	DWORD dwResult;
	BOOL bResult;
	PARTITION_INFORMATION diskInfo;
	DISK_GEOMETRY driveInfo;
	char szDosDevice[MAX_PATH], szCFDevice[MAX_PATH];
	static LONGLONG deviceSize = 0;
	wchar_t size[100] = { 0 }, partTypeStr[1024] = { 0 };
	const wchar_t* partType = partTypeStr; // splitted from ^ added const and got rid of compiler warnings.
	QString device;
	BOOL drivePresent = FALSE;
	BOOL removable = FALSE;

	drivePresent = TRUE;

	nDosLinkCreated = FakeDosNameForDevice(szTmp, szDosDevice,
		szCFDevice, FALSE);

	dev = CreateFileA(szCFDevice, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);

	bResult = DeviceIoControl(dev, IOCTL_DISK_GET_PARTITION_INFO, NULL, 0,
		&diskInfo, sizeof(diskInfo), &dwResult, NULL);

	// Test if device is removable
	if (/* n == 0 && */ DeviceIoControl(dev, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0,
		&driveInfo, sizeof(driveInfo), &dwResult, NULL))
		removable = driveInfo.MediaType == RemovableMedia;

	RemoveFakeDosName(szTmp, szDosDevice);
	CloseHandle(dev);

	if (!bResult)
		return 0;//return; << fucking pay attention

	// System creates a virtual partition1 for some storage devices without
	// partition table. We try to detect this case by comparing sizes of
	// partition0 and partition1. If they match, no partition of the device
	// is displayed to the user to avoid confusion. Drive letter assigned by
	// system to partition1 is displayed as subitem of partition0

	if (n == 0) {
		deviceSize = diskInfo.PartitionLength.QuadPart;
	}

	if (n > 0 && diskInfo.PartitionLength.QuadPart == deviceSize) {
		return 0;
	}

	switch (diskInfo.PartitionType) {
	case PARTITION_ENTRY_UNUSED:	partType = L""; break;
	case PARTITION_XINT13_EXTENDED:
	case PARTITION_EXTENDED:		partType = L"Extended"; break;
	case PARTITION_HUGE:			wsprintfW(partTypeStr, L"%s (0x%02X)", L"Unformatted", diskInfo.PartitionType); partType = partTypeStr; break;
	case PARTITION_FAT_12:			partType = L"FAT12"; break;
	case PARTITION_FAT_16:			partType = L"FAT16"; break;
	case PARTITION_FAT32:
	case PARTITION_FAT32_XINT13:	partType = L"FAT32"; break;
	case 0x08:						partType = L"DELL (spanning)"; break;
	case 0x12:						partType = L"Config/diagnostics"; break;
	case 0x11:
	case 0x14:
	case 0x16:
	case 0x1b:
	case 0x1c:
	case 0x1e:						partType = L"Hidden FAT"; break;
	case PARTITION_IFS:				partType = L"NTFS"; break;
	case 0x17:						partType = L"Hidden NTFS"; break;
	case 0x3c:						partType = L"PMagic recovery"; break;
	case 0x3d:						partType = L"Hidden NetWare"; break;
	case 0x41:						partType = L"Linux/MINIX"; break;
	case 0x42:						partType = L"SFS/LDM/Linux Swap"; break;
	case 0x51:
	case 0x64:
	case 0x65:
	case 0x66:
	case 0x67:
	case 0x68:
	case 0x69:						partType = L"Novell"; break;
	case 0x55:						partType = L"EZ-Drive"; break;
	case PARTITION_OS2BOOTMGR:		partType = L"OS/2 BM"; break;
	case PARTITION_XENIX_1:
	case PARTITION_XENIX_2:			partType = L"Xenix"; break;
	case PARTITION_UNIX:			partType = L"UNIX"; break;
	case 0x74:						partType = L"Scramdisk"; break;
	case 0x78:						partType = L"XOSL FS"; break;
	case 0x80:
	case 0x81:						partType = L"MINIX"; break;
	case 0x82:						partType = L"Linux Swap"; break;
	case 0x43:
	case 0x83:						partType = L"Linux"; break;
	case 0xc2:
	case 0x93:						partType = L"Hidden Linux"; break;
	case 0x86:
	case 0x87:						partType = L"NTFS volume set"; break;
	case 0x9f:						partType = L"BSD/OS"; break;
	case 0xa0:
	case 0xa1:						partType = L"Hibernation"; break;
	case 0xa5:						partType = L"BSD"; break;
	case 0xa8:						partType = L"Mac OS-X"; break;
	case 0xa9:						partType = L"NetBSD"; break;
	case 0xab:						partType = L"Mac OS-X Boot"; break;
	case 0xb8:						partType = L"BSDI BSD/386 swap"; break;
	case 0xc3:						partType = L"Hidden Linux swap"; break;
	case 0xfb:						partType = L"VMware"; break;
	case 0xfc:						partType = L"VMware swap"; break;
	case 0xfd:						partType = L"Linux RAID"; break;
	case 0xfe:						partType = L"WinNT hidden"; break;
	default:						wsprintfW(partTypeStr, L"0x%02X", diskInfo.PartitionType); partType = partTypeStr; break;
	}

	GetSizeString(diskInfo.PartitionLength.QuadPart, size);
	char const* s_type = removable ? "Removable" : "Fixed";
	//Debug
	//printf(format_str, szTmp, size, s_type, partType);
	device = szTmp;
	return device;
}

static QList<QString> list_devices() {
	QList<QString> DriveList;
	QString device;
	//    Debug.
	//    printf(
	//        "Device Name                         Size Type      Partition Type\n"
	//        "------------------------------ --------- --------- --------------------\n"
	//       123456789012345678901234567890 123456789 123456789 12345678901234567890
	//       \Device\Harddisk30\Partition03 1234.1 GB Removable SFS/LDM/Linux Swap
	//    );

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

//int main(int argc, char *argv[])
//{
//    QList<QString> DeviceList;

//    QCoreApplication a(argc, argv);

//    DeviceList = list_devices();
//    DeviceList.removeAll(QString(""));

//    return a.exec();
//}