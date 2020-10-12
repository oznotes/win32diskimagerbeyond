#ifndef DRIVELIST_H
#define DRIVELIST_H

#include <QString>

QList<QString> list_devices();
int FakeDosNameForDevice (char *lpszDiskFile, char *lpszDosDevice, char *lpszCFDevice, bool bNameOnly);
void GetSizeString (long long size, wchar_t *str);
int RemoveFakeDosName (char *lpszDiskFile, char *lpszDosDevice);
QString list_device(char *format_str, char *szTmp, int n);


#endif // DRIVELIST_H
