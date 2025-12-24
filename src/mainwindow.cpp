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
#include <QCoreApplication>
#include <QFileInfo>
#include <QDirIterator>
#include <QClipboard>
#include <QByteArray>
#include <QScreen>
#include <cstdio>
#include <cstdlib>
#include <windows.h>
#include <winioctl.h>
#include <dbt.h>
#include <shlobj.h>
#include <iostream>
#include <sstream>
#include "disk.h"
#include "mainwindow.h"
#include "elapsedtimer.h"
#include "driveList.h"

TestModel::TestModel(QObject* parent) : QAbstractTableModel(parent)
{
}

MainWindow* MainWindow::instance = NULL;

// Debug logger (enabled via DEBUG_LOGGING define)
#ifdef DEBUG_LOGGING
extern void dbgLog(const char* msg);
#else
inline void dbgLog(const char*) {}
#endif

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
	// CRITICAL: Set instance immediately to prevent infinite recursion
	instance = this;

	dbgLog("MW 1: setupUi");
	setupUi(this);
	dbgLog("MW 2: setWindowTitle");
	this->setWindowTitle(this->windowTitle() + " " + VER + " eMMC/SD Edition [@imTheElectroboy]");
	dbgLog("MW 3: ElapsedTimer");
	elapsed_timer = new ElapsedTimer();
	statusbar->addPermanentWidget(elapsed_timer);   // "addpermanent" puts it on the RHS of the statusbar
	dbgLog("MW 4: loadDriveIgnoreList");
	loadDriveIgnoreList();
	dbgLog("MW 5: getLogicalDrives");
	getLogicalDrives();
	dbgLog("MW 6: status setup");
	status = STATUS_IDLE;
	progressbar->reset();
	clipboard = QApplication::clipboard();
	statusbar->showMessage(tr("Waiting for a task."));
	hVolume = INVALID_HANDLE_VALUE;
	hFile = INVALID_HANDLE_VALUE;
	hRawDisk = INVALID_HANDLE_VALUE;
	if (QCoreApplication::arguments().count() > 1)
	{
		QString fileLocation = QApplication::arguments().at(1);
		QFileInfo fileInfo(fileLocation);
		leFile->setText(fileInfo.absoluteFilePath());
	}
	// Add supported hash types.
	cboxHashType->addItem("MD5", QVariant(QCryptographicHash::Md5));
	cboxHashType->addItem("SHA1", QVariant(QCryptographicHash::Sha1));
	cboxHashType->addItem("SHA256", QVariant(QCryptographicHash::Sha256));
	dbgLog("MW 7: hash controls");
	updateHashControls();
	setReadWriteButtonState();
	sectorData = NULL;
	sectorsize = 0ul;

	dbgLog("MW 8: loadSettings");
	loadSettings();
	if (myHomeDir.isEmpty()) {
		dbgLog("MW 9: initializeHomeDir");
		initializeHomeDir();
	}
	dbgLog("MW 10: adjustWindowToScreen");
	adjustWindowToScreen();
	dbgLog("MW 11: constructor done!");
}

MainWindow::~MainWindow()
{
	saveSettings();
	if (hRawDisk != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hRawDisk);
		hRawDisk = INVALID_HANDLE_VALUE;
	}
	if (hFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hFile);
		hFile = INVALID_HANDLE_VALUE;
	}
	if (hVolume != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hVolume);
		hVolume = INVALID_HANDLE_VALUE;
	}
	if (sectorData != NULL)
	{
		delete[] sectorData;
		sectorData = NULL;
	}
	if (sectorData2 != NULL)
	{
		delete[] sectorData2;
		sectorData2 = NULL;
	}
	if (elapsed_timer != NULL)
	{
		delete elapsed_timer;
		elapsed_timer = NULL;
	}
	if (cboxHashType != NULL)
	{
		cboxHashType->clear();
	}
}

void MainWindow::saveSettings()
{
	QSettings userSettings("HKEY_CURRENT_USER\\Software\\Win32DiskImager", QSettings::NativeFormat);
	userSettings.beginGroup("Settings");
	userSettings.setValue("ImageDir", myHomeDir);
	userSettings.setValue("WindowGeometry", saveGeometry());
	userSettings.endGroup();
}

void MainWindow::loadSettings()
{
	QSettings userSettings("HKEY_CURRENT_USER\\Software\\Win32DiskImager", QSettings::NativeFormat);
	userSettings.beginGroup("Settings");
	myHomeDir = userSettings.value("ImageDir").toString();

	// Restore window geometry if saved
	QByteArray geometry = userSettings.value("WindowGeometry").toByteArray();
	if (!geometry.isEmpty()) {
		restoreGeometry(geometry);
	}
	userSettings.endGroup();
}

void MainWindow::initializeHomeDir()
{
	myHomeDir = QDir::homePath();
	// QString doesn't support NULL comparison - use isEmpty() instead
	if (myHomeDir.isEmpty()) {
		myHomeDir = qgetenv("USERPROFILE");
	}
	/* Get Downloads the Windows way */
	QString downloadPath = qgetenv("DiskImagesDir");
	if (downloadPath.isEmpty()) {
		PWSTR pPath = NULL;
		static GUID downloads = {
			0x374de290, 0x123f, 0x4565, 0x91, 0x64, 0x39, 0xc4, 0x92, 0x5e, 0x46, 0x7b
		};
		if (SHGetKnownFolderPath(downloads, 0, 0, &pPath) == S_OK) {
			downloadPath = QDir::fromNativeSeparators(QString::fromWCharArray(pPath));
			LocalFree(pPath);
			if (downloadPath.isEmpty() || !QDir(downloadPath).exists()) {
				downloadPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
			}
		}
	}
	if (downloadPath.isEmpty())
		downloadPath = QDir::currentPath();
	myHomeDir = downloadPath;
}

void MainWindow::adjustWindowToScreen()
{
	// Check if we have saved geometry - if so, restoreGeometry already handled positioning
	QSettings userSettings("HKEY_CURRENT_USER\\Software\\Win32DiskImager", QSettings::NativeFormat);
	userSettings.beginGroup("Settings");
	if (!userSettings.value("WindowGeometry").toByteArray().isEmpty()) {
		userSettings.endGroup();
		return;  // Geometry was restored in loadSettings()
	}
	userSettings.endGroup();

	// First launch - fit to screen and center
	QScreen* screen = QGuiApplication::primaryScreen();
	if (!screen) return;

	// Get available geometry (excludes taskbar, etc.)
	QRect availableGeometry = screen->availableGeometry();

	// Calculate 90% of screen size as maximum window size
	int maxWidth = static_cast<int>(availableGeometry.width() * 0.9);
	int maxHeight = static_cast<int>(availableGeometry.height() * 0.9);

	// Get current size
	QSize currentSize = size();

	// Constrain to screen
	int targetWidth = qMin(currentSize.width(), maxWidth);
	int targetHeight = qMin(currentSize.height(), maxHeight);

	// Resize window if needed
	resize(targetWidth, targetHeight);

	// Center on screen
	int x = availableGeometry.x() + (availableGeometry.width() - targetWidth) / 2;
	int y = availableGeometry.y() + (availableGeometry.height() - targetHeight) / 2;
	move(x, y);
}

void MainWindow::setReadWriteButtonState()
{
	bool fileSelected = !(leFile->text().isEmpty());
	bool deviceSelected = (cboxDevice->count() > 0);
	QFileInfo fi(leFile->text());

	// set read and write buttons according to status of file/device
	bRead->setEnabled(deviceSelected && fileSelected && (fi.exists() ? fi.isWritable() : true));
	bDetect->setEnabled(deviceSelected);
	bWrite->setEnabled(deviceSelected && fileSelected && fi.isReadable());
	bVerify->setEnabled(deviceSelected && fileSelected && fi.isReadable());
	this->statLabel->setText("");
}

void MainWindow::closeEvent(QCloseEvent* event)
{
	saveSettings();
	if (status == STATUS_READING)
	{
		if (QMessageBox::warning(this, tr("Exit?"), tr("Exiting now will result in a corrupt image file.\n"
			"Are you sure you want to exit?"),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
		{
			status = STATUS_EXIT;
		}
		event->ignore();
	}
	else if (status == STATUS_WRITING)
	{
		if (QMessageBox::warning(this, tr("Exit?"), tr("Exiting now will result in a corrupt disk.\n"
			"Are you sure you want to exit?"),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
		{
			status = STATUS_EXIT;
		}
		event->ignore();
	}
	else if (status == STATUS_VERIFYING)
	{
		if (QMessageBox::warning(this, tr("Exit?"), tr("Exiting now will cancel verifying image.\n"
			"Are you sure you want to exit?"),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
		{
			status = STATUS_EXIT;
		}
		event->ignore();
	}
}

void MainWindow::on_tbBrowse_clicked()
{
	// Use the location of already entered file
	QString fileLocation = leFile->text();
	QFileInfo fileinfo(fileLocation);

	// See if there is a user-defined file extension.
	QString fileType = qgetenv("DiskImagerFiles");
	if (fileType.length() && !fileType.endsWith(";;"))
	{
		fileType.append(";;");
	}
	fileType.append(tr("Disk Images (*.img *.IMG);;*.*"));
	// create a generic FileDialog
	QFileDialog dialog(this, tr("Select a disk image"));
	dialog.setNameFilter(fileType);
	dialog.setFileMode(QFileDialog::AnyFile);
	dialog.setViewMode(QFileDialog::Detail);
	dialog.setOption(QFileDialog::DontConfirmOverwrite, true);
	if (fileinfo.exists())
	{
		dialog.selectFile(fileLocation);
	}
	else
	{
		dialog.setDirectory(myHomeDir);
	}

	if (dialog.exec())
	{
		// selectedFiles returns a QStringList - we just want 1 filename,
		//	so use the zero'th element from that list as the filename
		fileLocation = (dialog.selectedFiles())[0];

		if (!fileLocation.isNull())
		{
			leFile->setText(fileLocation);
			QFileInfo newFileInfo(fileLocation);
			myHomeDir = newFileInfo.absolutePath();
		}
		setReadWriteButtonState();
		updateHashControls();
	}
}

void MainWindow::on_bHashCopy_clicked()
{
	QString hashSum(hashLabel->text());
	if (!(hashSum.isEmpty()) && clipboard != nullptr)
	{
		clipboard->setText(hashSum);
	}
}

// generates the hash
void MainWindow::generateHash(char* filename, int hashish)
{
	hashLabel->setText(tr("Generating..."));
	QApplication::processEvents();

	QCryptographicHash filehash((QCryptographicHash::Algorithm)hashish);

	// may take a few secs - display a wait cursor
	QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

	QFile file(filename);
	if (!file.open(QFile::ReadOnly)) {
		QApplication::restoreOverrideCursor();
		hashLabel->setText(tr("Error: Cannot open file"));
		return;
	}
	filehash.addData(&file);
	file.close();  // Explicitly close the file

	QByteArray hash = filehash.result();

	// display it in the textbox
	hashLabel->setText(hash.toHex());
	bHashCopy->setEnabled(true);
	// redisplay the normal cursor
	QApplication::restoreOverrideCursor();
}

// on an "editingFinished" signal (IE: return press), if the lineedit
// contains a valid file, update the controls
void MainWindow::on_leFile_editingFinished()
{
	setReadWriteButtonState();
	updateHashControls();
}

void MainWindow::on_bCancel_clicked()
{
	if ((status == STATUS_READING) || (status == STATUS_WRITING))
	{
		if (QMessageBox::warning(this, tr("Cancel?"), tr("Canceling now will result in a corrupt destination.\n"
			"Are you sure you want to cancel?"),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
		{
			status = STATUS_CANCELED;
		}
	}
	else if (status == STATUS_VERIFYING)
	{
		if (QMessageBox::warning(this, tr("Cancel?"), tr("Cancel Verify.\n"
			"Are you sure you want to cancel?"),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
		{
			status = STATUS_CANCELED;
		}
	}
}

void MainWindow::on_bWrite_clicked()
{
	bool passfail = true;
	if (!leFile->text().isEmpty())
	{
		QFileInfo fileinfo(leFile->text());
		if (fileinfo.exists() && fileinfo.isFile() &&
			fileinfo.isReadable() && (fileinfo.size() > 0))
		{
			// Bounds check before accessing string characters
			QString filePath = leFile->text();
			QString deviceText = cboxDevice->currentText();
			if (filePath.length() >= 1 && deviceText.length() >= 2 &&
				filePath.at(0) == deviceText.at(1))
			{
				QMessageBox::critical(this, tr("Write Error"), tr("Image file cannot be located on the target device."));
				return;
			}

			// build the drive letter as a const char *
			//   (without the surrounding brackets)
			QString qs = cboxDevice->currentText();
			qs.replace(QRegExp("[\\[\\]]"), "");
			QByteArray qba = qs.toLocal8Bit();
			const char* ltr = qba.data();
			if (QMessageBox::warning(this, tr("Confirm overwrite"), tr("Writing to a physical device can corrupt the device.\n"
				"(Target Device: %1 \"%2\")\n"
				"Are you sure you want to continue?").arg(cboxDevice->currentText()).arg(getDriveLabel(ltr)),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::No)
			{
				return;
			}
			status = STATUS_WRITING;
			bCancel->setEnabled(true);
			bWrite->setEnabled(false);
			bRead->setEnabled(false);
			bVerify->setEnabled(false);
			bDetect->setEnabled(false);
			double mbpersec;
			unsigned long long i, lasti, availablesectors, numsectors;
			DWORD deviceID = cboxDevice->currentData().toUInt();  // Device ID stored as item data
			hFile = getHandleOnFile(LPCWSTR(leFile->text().data()), GENERIC_READ);
			if (hFile == INVALID_HANDLE_VALUE)
			{
				removeLockOnVolume(hVolume);
				CloseHandle(hVolume);
				status = STATUS_IDLE;
				hVolume = INVALID_HANDLE_VALUE;
				bCancel->setEnabled(false);
				setReadWriteButtonState();
				return;
			}
			hRawDisk = getHandleOnDevice(deviceID, GENERIC_WRITE);

			if (!getLockOnVolume(hRawDisk))
			{
				// Close both hFile (opened above) and hRawDisk to prevent handle leaks
				CloseHandle(hFile);
				hFile = INVALID_HANDLE_VALUE;
				CloseHandle(hRawDisk);
				status = STATUS_IDLE;
				hRawDisk = INVALID_HANDLE_VALUE;
				bCancel->setEnabled(false);
				setReadWriteButtonState();
				return;
			}
			if (!unmountVolume(hRawDisk))
			{
				// Close both handles to prevent leaks
				CloseHandle(hFile);
				hFile = INVALID_HANDLE_VALUE;
				removeLockOnVolume(hRawDisk);
				CloseHandle(hRawDisk);
				status = STATUS_IDLE;
				hRawDisk = INVALID_HANDLE_VALUE;
				bCancel->setEnabled(false);
				setReadWriteButtonState();
				return;
			}
			// Note: This check is redundant but kept for safety. Only close valid handles.
			if (hRawDisk == INVALID_HANDLE_VALUE)
			{
				// hRawDisk is invalid, only clean up handles that are valid
				if (hFile != INVALID_HANDLE_VALUE) {
					CloseHandle(hFile);
					hFile = INVALID_HANDLE_VALUE;
				}
				if (hVolume != INVALID_HANDLE_VALUE) {
					CloseHandle(hVolume);
					hVolume = INVALID_HANDLE_VALUE;
				}
				status = STATUS_IDLE;
				bCancel->setEnabled(false);
				setReadWriteButtonState();
				return;
			}
			availablesectors = getNumberOfSectors(hRawDisk, &sectorsize);
			if (!availablesectors)
			{
				//For external card readers you may not get device change notification when you remove the card/flash.
				//(So no WM_DEVICECHANGE signal). Device stays but size goes to 0. [Is there special event for this on Windows??]
				removeLockOnVolume(hRawDisk);
				CloseHandle(hRawDisk);
				CloseHandle(hFile);
				//CloseHandle(hVolume);
				hRawDisk = INVALID_HANDLE_VALUE;
				hFile = INVALID_HANDLE_VALUE;
				passfail = false;
				status = STATUS_IDLE;
				return;
			}
			numsectors = getFileSizeInSectors(hFile, sectorsize);
			if (!numsectors)
			{
				//For external card readers you may not get device change notification when you remove the card/flash.
				//(So no WM_DEVICECHANGE signal). Device stays but size goes to 0. [Is there special event for this on Windows??]
				removeLockOnVolume(hRawDisk);
				CloseHandle(hRawDisk);
				CloseHandle(hFile);
				hRawDisk = INVALID_HANDLE_VALUE;
				hFile = INVALID_HANDLE_VALUE;
				status = STATUS_IDLE;
				return;
			}
			if (numsectors > availablesectors)
			{
				bool datafound = false;
				i = availablesectors;
				unsigned long nextchunksize = 0;
				while ((i < numsectors) && (datafound == false))
				{
					nextchunksize = ((numsectors - i) >= 1024ul) ? 1024ul : (numsectors - i);
					sectorData = readSectorDataFromHandle(hFile, i, nextchunksize, sectorsize);
					if (sectorData == NULL)
					{
						// if there's an error verifying the truncated data, just move on to the
						//  write, as we don't care about an error in a section that we're not writing...
						i = numsectors + 1;
					}
					else {
						unsigned int j = 0;
						unsigned limit = nextchunksize * sectorsize;
						while ((datafound == false) && (j < limit))
						{
							if (sectorData[j++] != 0)
							{
								datafound = true;
							}
						}
						i += nextchunksize;
					}
				}
				// delete the allocated sectorData
				delete[] sectorData;
				sectorData = NULL;
				// build the string for the warning dialog
				std::ostringstream msg;
				msg << "More space required than is available:"
					<< "\n  Required: " << numsectors << " sectors"
					<< "\n  Available: " << availablesectors << " sectors"
					<< "\n  Sector Size: " << sectorsize
					<< "\n\nThe extra space " << ((datafound) ? "DOES" : "does not") << " appear to contain data"
					<< "\n\nContinue Anyway?";
				if (QMessageBox::warning(this, tr("Not enough available space!"),
					tr(msg.str().c_str()), QMessageBox::Ok, QMessageBox::Cancel) == QMessageBox::Ok)
				{
					// truncate the image at the device size...
					numsectors = availablesectors;
				}
				else    // Cancel
				{
					removeLockOnVolume(hRawDisk);
					CloseHandle(hRawDisk);
					CloseHandle(hFile);
					status = STATUS_IDLE;
					hFile = INVALID_HANDLE_VALUE;
					hRawDisk = INVALID_HANDLE_VALUE;
					bCancel->setEnabled(false);
					setReadWriteButtonState();
					return;
				}
			}

			// Cap numsectors at INT_MAX to prevent overflow when casting to int
			progressbar->setRange(0, (numsectors == 0ul) ? 100 : (int)qMin(numsectors, (unsigned long long)INT_MAX));
			lasti = 0ul;
			update_timer.start();
			elapsed_timer->start();
			for (i = 0ul; i < numsectors && status == STATUS_WRITING; i += 1024ul)
			{
				sectorData = readSectorDataFromHandle(hFile, i, (numsectors - i >= 1024ul) ? 1024ul : (numsectors - i), sectorsize);
				if (sectorData == NULL)
				{
					removeLockOnVolume(hRawDisk);
					CloseHandle(hRawDisk);
					CloseHandle(hFile);
					status = STATUS_IDLE;
					hFile = INVALID_HANDLE_VALUE;
					hVolume = INVALID_HANDLE_VALUE;
					bCancel->setEnabled(false);
					setReadWriteButtonState();
					return;
				}
				if (!writeSectorDataToHandle(hRawDisk, sectorData, i, (numsectors - i >= 1024ul) ? 1024ul : (numsectors - i), sectorsize))
				{
					delete[] sectorData;
					removeLockOnVolume(hRawDisk);
					CloseHandle(hRawDisk);
					CloseHandle(hFile);
					status = STATUS_IDLE;
					sectorData = NULL;
					hRawDisk = INVALID_HANDLE_VALUE;
					hFile = INVALID_HANDLE_VALUE;
					bCancel->setEnabled(false);
					setReadWriteButtonState();
					return;
				}
				delete[] sectorData;
				sectorData = NULL;
				QCoreApplication::processEvents();
				if (update_timer.elapsed() >= ONE_SEC_IN_MS)
				{
					mbpersec = (((double)sectorsize * (i - lasti)) * ((float)ONE_SEC_IN_MS / update_timer.elapsed())) / 1024.0 / 1024.0;
					statusbar->showMessage(QString("%1 MB/s").arg(mbpersec));
					update_timer.start();
					elapsed_timer->update(i, numsectors);
					update_timer.start();
					lasti = i;
				}
				progressbar->setValue(i);
				QCoreApplication::processEvents();
			}
			removeLockOnVolume(hRawDisk);
			CloseHandle(hRawDisk);
			CloseHandle(hFile);
			hRawDisk = INVALID_HANDLE_VALUE;
			hFile = INVALID_HANDLE_VALUE;
			if (status == STATUS_CANCELED) {
				passfail = false;
			}
		}
		else if (!fileinfo.exists() || !fileinfo.isFile())
		{
			QMessageBox::critical(this, tr("File Error"), tr("The selected file does not exist."));
			passfail = false;
		}
		else if (!fileinfo.isReadable())
		{
			QMessageBox::critical(this, tr("File Error"), tr("You do not have permision to read the selected file."));
			passfail = false;
		}
		else if (fileinfo.size() == 0)
		{
			QMessageBox::critical(this, tr("File Error"), tr("The specified file contains no data."));
			passfail = false;
		}
		progressbar->reset();
		statusbar->showMessage(tr("Done."));
		bCancel->setEnabled(false);
		setReadWriteButtonState();
		if (passfail) {
			QMessageBox::information(this, tr("Complete"), tr("Write Successful."));
		}
	}
	else
	{
		QMessageBox::critical(this, tr("File Error"), tr("Please specify an image file to use."));
	}
	if (status == STATUS_EXIT)
	{
		close();
	}
	status = STATUS_IDLE;
	elapsed_timer->stop();
}

void MainWindow::on_bRead_clicked()
{
	QString myFile;
	if (!leFile->text().isEmpty())
	{
		myFile = leFile->text();
		QFileInfo fileinfo(myFile);
		if (fileinfo.path() == ".") {
			myFile = (myHomeDir + "/" + leFile->text());
		}
		// check whether source and target device is the same...
		// Bounds check before accessing string characters
		QString deviceText = cboxDevice->currentText();
		if (myFile.length() >= 1 && deviceText.length() >= 2 &&
			myFile.at(0) == deviceText.at(1))
		{
			QMessageBox::critical(this, tr("Write Error"), tr("Image file cannot be located on the target device."));
			return;
		}
		// confirm overwrite if the dest. file already exists
		if (fileinfo.exists())
		{
			if (QMessageBox::warning(this, tr("Confirm Overwrite"), tr("Are you sure you want to overwrite the specified file?"),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::No)
			{
				return;
			}
		}
		bCancel->setEnabled(true);
		bWrite->setEnabled(false);
		bRead->setEnabled(false);
		bVerify->setEnabled(false);
		bDetect->setEnabled(false);
		status = STATUS_READING;
		double mbpersec;
		unsigned long long i, lasti, numsectors, filesize, spaceneeded = 0ull;
		DWORD deviceID = cboxDevice->currentData().toUInt();  // Device ID stored as item data
		hFile = getHandleOnFile(LPCWSTR(myFile.data()), GENERIC_WRITE);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			removeLockOnVolume(hVolume);
			CloseHandle(hVolume);
			status = STATUS_IDLE;
			hVolume = INVALID_HANDLE_VALUE;
			bCancel->setEnabled(false);
			setReadWriteButtonState();
			return;
		}
		hRawDisk = getHandleOnDevice(deviceID, GENERIC_READ);
		if (hRawDisk == INVALID_HANDLE_VALUE)
		{
			// Close hFile (opened above) to prevent handle leak
			CloseHandle(hFile);
			hFile = INVALID_HANDLE_VALUE;
			status = STATUS_IDLE;
			bCancel->setEnabled(false);
			setReadWriteButtonState();
			return;
		}
		if (!getLockOnVolume(hRawDisk))
		{
			// Close both handles to prevent leaks
			CloseHandle(hFile);
			hFile = INVALID_HANDLE_VALUE;
			CloseHandle(hRawDisk);
			status = STATUS_IDLE;
			hRawDisk = INVALID_HANDLE_VALUE;
			bCancel->setEnabled(false);
			setReadWriteButtonState();
			return;
		}
		if (!unmountVolume(hRawDisk))
		{
			// Close both handles to prevent leaks
			CloseHandle(hFile);
			hFile = INVALID_HANDLE_VALUE;
			removeLockOnVolume(hRawDisk);
			CloseHandle(hRawDisk);
			status = STATUS_IDLE;
			hRawDisk = INVALID_HANDLE_VALUE;
			bCancel->setEnabled(false);
			setReadWriteButtonState();
			return;
		}
		// Note: This check is redundant after the previous check at line 644,
		// but kept for safety. Only close valid handles.
		if (hRawDisk == INVALID_HANDLE_VALUE)
		{
			// hRawDisk is invalid, clean up all valid handles
			if (hFile != INVALID_HANDLE_VALUE) {
				CloseHandle(hFile);
				hFile = INVALID_HANDLE_VALUE;
			}
			if (hVolume != INVALID_HANDLE_VALUE) {
				CloseHandle(hVolume);
				hVolume = INVALID_HANDLE_VALUE;
			}
			status = STATUS_IDLE;
			bCancel->setEnabled(false);
			setReadWriteButtonState();
			return;
		}
		numsectors = getNumberOfSectors(hRawDisk, &sectorsize);
		if (partitionCheckBox->isChecked())
		{
			// Read MBR partition table
			sectorData = readSectorDataFromHandle(hRawDisk, 0, 1ul, 512ul);
			if (sectorData != NULL)
			{
				numsectors = 1ul;
				// Read partition information - verify buffer has enough data for MBR partition table
				// MBR partition table starts at 0x1BE, each entry is 16 bytes, 4 entries = 64 bytes
				// Need at least 0x1BE + 64 = 510 bytes, but we read 512 so this is safe
				for (i = 0ul; i < 4ul; i++)
				{
					// Use memcpy for portable unaligned access (safe on all architectures)
					uint32_t partitionStartSector = 0;
					uint32_t partitionNumSectors = 0;
					memcpy(&partitionStartSector, sectorData + 0x1BE + 8 + 16 * i, sizeof(uint32_t));
					memcpy(&partitionNumSectors, sectorData + 0x1BE + 12 + 16 * i, sizeof(uint32_t));
					// Set numsectors to end of last partition
					if (partitionStartSector + partitionNumSectors > numsectors)
					{
						numsectors = partitionStartSector + partitionNumSectors;
					}
				}
				// Clean up after MBR read - sectorData will be reallocated in main read loop
				delete[] sectorData;
				sectorData = NULL;
			}
		}
		filesize = getFileSizeInSectors(hFile, sectorsize);
		if (filesize >= numsectors)
		{
			spaceneeded = 0ull;
		}
		else
		{
			spaceneeded = (unsigned long long)(numsectors - filesize) * (unsigned long long)(sectorsize);
		}
		if (!spaceAvailable(myFile.left(3).replace(QChar('/'), QChar('\\')).toLatin1().data(), spaceneeded))
		{
			QMessageBox::critical(this, tr("Write Error"), tr("Disk is not large enough for the specified image."));
			// Clean up sectorData if allocated during partition check
			if (sectorData != NULL) {
				delete[] sectorData;
				sectorData = NULL;
			}
			removeLockOnVolume(hRawDisk);
			CloseHandle(hRawDisk);
			CloseHandle(hFile);
			if (hVolume != INVALID_HANDLE_VALUE) {
				CloseHandle(hVolume);
				hVolume = INVALID_HANDLE_VALUE;
			}
			status = STATUS_IDLE;
			hRawDisk = INVALID_HANDLE_VALUE;
			hFile = INVALID_HANDLE_VALUE;
			bCancel->setEnabled(false);
			setReadWriteButtonState();
			return;
		}
		if (numsectors == 0ul)
		{
			progressbar->setRange(0, 100);
		}
		else
		{
			// Cap numsectors at INT_MAX to prevent overflow when casting to int
			progressbar->setRange(0, (int)qMin(numsectors, (unsigned long long)INT_MAX));
		}
		lasti = 0ul;
		update_timer.start();
		elapsed_timer->start();
		for (i = 0ul; i < numsectors && status == STATUS_READING; i += 1024ul)
		{
			sectorData = readSectorDataFromHandle(hRawDisk, i, (numsectors - i >= 1024ul) ? 1024ul : (numsectors - i), sectorsize);
			if (sectorData == NULL)
			{
				removeLockOnVolume(hRawDisk);
				CloseHandle(hRawDisk);
				CloseHandle(hFile);
				status = STATUS_IDLE;
				hRawDisk = INVALID_HANDLE_VALUE;
				hFile = INVALID_HANDLE_VALUE;
				bCancel->setEnabled(false);
				setReadWriteButtonState();
				return;
			}
			if (!writeSectorDataToHandle(hFile, sectorData, i, (numsectors - i >= 1024ul) ? 1024ul : (numsectors - i), sectorsize))
			{
				delete[] sectorData;
				removeLockOnVolume(hRawDisk);
				CloseHandle(hRawDisk);
				CloseHandle(hFile);
				status = STATUS_IDLE;
				sectorData = NULL;
				hFile = INVALID_HANDLE_VALUE;
				hVolume = INVALID_HANDLE_VALUE;
				bCancel->setEnabled(false);
				setReadWriteButtonState();
				return;
			}
			delete[] sectorData;
			sectorData = NULL;
			if (update_timer.elapsed() >= ONE_SEC_IN_MS)
			{
				mbpersec = (((double)sectorsize * (i - lasti)) * ((float)ONE_SEC_IN_MS / update_timer.elapsed())) / 1024.0 / 1024.0;
				statusbar->showMessage(QString("%1MB/s").arg(mbpersec));
				update_timer.start();
				elapsed_timer->update(i, numsectors);
				lasti = i;
			}
			progressbar->setValue(i);
			QCoreApplication::processEvents();
		}
		removeLockOnVolume(hRawDisk);
		CloseHandle(hRawDisk);
		CloseHandle(hFile);
		hRawDisk = INVALID_HANDLE_VALUE;
		hFile = INVALID_HANDLE_VALUE;
		progressbar->reset();
		statusbar->showMessage(tr("Done."));
		bCancel->setEnabled(false);
		setReadWriteButtonState();
		if (status == STATUS_CANCELED) {
			QMessageBox::information(this, tr("Complete"), tr("Read Canceled."));
		}
		else {
			QMessageBox::information(this, tr("Complete"), tr("Read Successful."));
		}
		updateHashControls();
	}
	else
	{
		QMessageBox::critical(this, tr("File Info"), tr("Please specify a file to save data to."));
	}
	if (status == STATUS_EXIT)
	{
		close();
	}
	status = STATUS_IDLE;
	elapsed_timer->stop();
}

// Verify image with device
void MainWindow::on_bVerify_clicked()
{
	bool passfail = true;
	if (!leFile->text().isEmpty())
	{
		QFileInfo fileinfo(leFile->text());
		if (fileinfo.exists() && fileinfo.isFile() && fileinfo.isReadable() && (fileinfo.size() > 0))
		{
			// Bounds check before accessing string characters
			QString filePath = leFile->text();
			QString deviceText = cboxDevice->currentText();
			if (filePath.length() >= 1 && deviceText.length() >= 2 &&
				filePath.at(0) == deviceText.at(1))
			{
				QMessageBox::critical(this, tr("Verify Error"), tr("Image file cannot be located on the target device."));
				return;
			}
			status = STATUS_VERIFYING;
			bCancel->setEnabled(true);
			bWrite->setEnabled(false);
			bRead->setEnabled(false);
			bVerify->setEnabled(false);
			bDetect->setEnabled(false);
			double mbpersec;
			unsigned long long i, lasti, availablesectors, numsectors;
			int memcmpResult;  // memcmp returns int, not unsigned long long
			DWORD deviceID = cboxDevice->currentData().toUInt();  // Device ID stored as item data
			hFile = getHandleOnFile(LPCWSTR(leFile->text().data()), GENERIC_READ);
			if (hFile == INVALID_HANDLE_VALUE)
			{
				removeLockOnVolume(hVolume);
				CloseHandle(hVolume);
				status = STATUS_IDLE;
				hVolume = INVALID_HANDLE_VALUE;
				bCancel->setEnabled(false);
				setReadWriteButtonState();
				return;
			}
			hRawDisk = getHandleOnDevice(deviceID, GENERIC_READ);
			if (!getLockOnVolume(hRawDisk))
			{
				// Close hFile (opened above) to prevent handle leak
				CloseHandle(hFile);
				hFile = INVALID_HANDLE_VALUE;
				CloseHandle(hRawDisk);
				status = STATUS_IDLE;
				hRawDisk = INVALID_HANDLE_VALUE;
				bCancel->setEnabled(false);
				setReadWriteButtonState();
				return;
			}
			if (!unmountVolume(hRawDisk))
			{
				// Close hFile to prevent handle leak
				CloseHandle(hFile);
				hFile = INVALID_HANDLE_VALUE;
				removeLockOnVolume(hRawDisk);
				CloseHandle(hRawDisk);
				// Close hVolume if valid to prevent handle leak
				if (hVolume != INVALID_HANDLE_VALUE) {
					CloseHandle(hVolume);
				}
				status = STATUS_IDLE;
				hVolume = INVALID_HANDLE_VALUE;
				bCancel->setEnabled(false);
				setReadWriteButtonState();
				return;
			}
			// Note: This check is redundant but kept for safety. Only close valid handles.
			if (hRawDisk == INVALID_HANDLE_VALUE)
			{
				// hRawDisk is invalid, only clean up handles that are valid
				if (hVolume != INVALID_HANDLE_VALUE) {
					removeLockOnVolume(hVolume);
					CloseHandle(hVolume);
					hVolume = INVALID_HANDLE_VALUE;
				}
				if (hFile != INVALID_HANDLE_VALUE) {
					CloseHandle(hFile);
					hFile = INVALID_HANDLE_VALUE;
				}
				status = STATUS_IDLE;
				bCancel->setEnabled(false);
				setReadWriteButtonState();
				return;
			}
			availablesectors = getNumberOfSectors(hRawDisk, &sectorsize);
			if (!availablesectors)
			{
				//For external card readers you may not get device change notification when you remove the card/flash.
				//(So no WM_DEVICECHANGE signal). Device stays but size goes to 0. [Is there special event for this on Windows??]
				removeLockOnVolume(hRawDisk);
				CloseHandle(hRawDisk);
				CloseHandle(hFile);
				hRawDisk = INVALID_HANDLE_VALUE;
				hFile = INVALID_HANDLE_VALUE;
				passfail = false;
				status = STATUS_IDLE;
				return;
			}
			numsectors = getFileSizeInSectors(hFile, sectorsize);
			if (!numsectors)
			{
				//For external card readers you may not get device change notification when you remove the card/flash.
				//(So no WM_DEVICECHANGE signal). Device stays but size goes to 0. [Is there special event for this on Windows??]
				removeLockOnVolume(hRawDisk);
				CloseHandle(hRawDisk);
				CloseHandle(hFile);
				hRawDisk = INVALID_HANDLE_VALUE;
				hFile = INVALID_HANDLE_VALUE;
				status = STATUS_IDLE;
				return;
			}
			if (numsectors > availablesectors)
			{
				bool datafound = false;
				i = availablesectors;
				unsigned long nextchunksize = 0;
				while ((i < numsectors) && (datafound == false))
				{
					nextchunksize = ((numsectors - i) >= 1024ul) ? 1024ul : (numsectors - i);
					sectorData = readSectorDataFromHandle(hFile, i, nextchunksize, sectorsize);
					if (sectorData == NULL)
					{
						// if there's an error verifying the truncated data, just move on to the
						//  write, as we don't care about an error in a section that we're not writing...
						i = numsectors + 1;
					}
					else {
						unsigned int j = 0;
						unsigned limit = nextchunksize * sectorsize;
						while ((datafound == false) && (j < limit))
						{
							if (sectorData[j++] != 0)
							{
								datafound = true;
							}
						}
						i += nextchunksize;
					}
				}
				// delete the allocated sectorData
				delete[] sectorData;
				sectorData = NULL;
				// build the string for the warning dialog
				std::ostringstream msg;
				msg << "Size of image larger than device:"
					<< "\n  Image: " << numsectors << " sectors"
					<< "\n  Device: " << availablesectors << " sectors"
					<< "\n  Sector Size: " << sectorsize
					<< "\n\nThe extra space " << ((datafound) ? "DOES" : "does not") << " appear to contain data"
					<< "\n\nContinue Anyway?";
				if (QMessageBox::warning(this, tr("Size Mismatch!"),
					tr(msg.str().c_str()), QMessageBox::Ok, QMessageBox::Cancel) == QMessageBox::Ok)
				{
					// truncate the image at the device size...
					numsectors = availablesectors;
				}
				else    // Cancel
				{
					removeLockOnVolume(hRawDisk);
					CloseHandle(hRawDisk);
					CloseHandle(hFile);
					status = STATUS_IDLE;
					hFile = INVALID_HANDLE_VALUE;
					hRawDisk = INVALID_HANDLE_VALUE;
					bCancel->setEnabled(false);
					setReadWriteButtonState();
					return;
				}
			}
			// Cap numsectors at INT_MAX to prevent overflow when casting to int
			progressbar->setRange(0, (numsectors == 0ul) ? 100 : (int)qMin(numsectors, (unsigned long long)INT_MAX));
			update_timer.start();
			elapsed_timer->start();
			lasti = 0ul;
			for (i = 0ul; i < numsectors && status == STATUS_VERIFYING; i += 1024ul)
			{
				sectorData = readSectorDataFromHandle(hFile, i, (numsectors - i >= 1024ul) ? 1024ul : (numsectors - i), sectorsize);
				if (sectorData == NULL)
				{
					removeLockOnVolume(hRawDisk);
					CloseHandle(hRawDisk);
					CloseHandle(hFile);
					status = STATUS_IDLE;
					hRawDisk = INVALID_HANDLE_VALUE;
					hFile = INVALID_HANDLE_VALUE;
					bCancel->setEnabled(false);
					setReadWriteButtonState();
					return;
				}
				sectorData2 = readSectorDataFromHandle(hRawDisk, i, (numsectors - i >= 1024ul) ? 1024ul : (numsectors - i), sectorsize);
				if (sectorData2 == NULL)
				{
					// Clean up sectorData before returning to prevent memory leak
					delete[] sectorData;
					sectorData = NULL;
					QMessageBox::critical(this, tr("Verify Failure"), tr("Verification failed at sector: %1").arg(i));
					removeLockOnVolume(hRawDisk);
					CloseHandle(hRawDisk);
					CloseHandle(hFile);
					status = STATUS_IDLE;
					hRawDisk = INVALID_HANDLE_VALUE;
					hFile = INVALID_HANDLE_VALUE;
					bCancel->setEnabled(false);
					setReadWriteButtonState();
					return;
				}
				memcmpResult = memcmp(sectorData, sectorData2, ((numsectors - i >= 1024ul) ? 1024ul : (numsectors - i)) * sectorsize);
				if (memcmpResult != 0)
				{
					QMessageBox::critical(this, tr("Verify Failure"), tr("Verification failed at sector: %1").arg(i));
					passfail = false;
					break;
				}
				if (update_timer.elapsed() >= ONE_SEC_IN_MS)
				{
					mbpersec = (((double)sectorsize * (i - lasti)) * ((float)ONE_SEC_IN_MS / update_timer.elapsed())) / 1024.0 / 1024.0;
					statusbar->showMessage(QString("%1MB/s").arg(mbpersec));
					update_timer.start();
					elapsed_timer->update(i, numsectors);
					lasti = i;
				}
				delete[] sectorData;
				delete[] sectorData2;
				sectorData = NULL;
				sectorData2 = NULL;
				progressbar->setValue(i);
				QCoreApplication::processEvents();
			}
			removeLockOnVolume(hRawDisk);
			CloseHandle(hRawDisk);
			CloseHandle(hFile);
			delete[] sectorData;
			delete[] sectorData2;
			sectorData = NULL;
			sectorData2 = NULL;
			hRawDisk = INVALID_HANDLE_VALUE;
			hFile = INVALID_HANDLE_VALUE;
			if (status == STATUS_CANCELED) {
				passfail = false;
			}
		}
		else if (!fileinfo.exists() || !fileinfo.isFile())
		{
			QMessageBox::critical(this, tr("File Error"), tr("The selected file does not exist."));
			passfail = false;
		}
		else if (!fileinfo.isReadable())
		{
			QMessageBox::critical(this, tr("File Error"), tr("You do not have permision to read the selected file."));
			passfail = false;
		}
		else if (fileinfo.size() == 0)
		{
			QMessageBox::critical(this, tr("File Error"), tr("The specified file contains no data."));
			passfail = false;
		}
		progressbar->reset();
		statusbar->showMessage(tr("Done."));
		bCancel->setEnabled(false);
		setReadWriteButtonState();
		if (passfail) {
			QMessageBox::information(this, tr("Complete"), tr("Verify Successful."));
		}
	}
	else
	{
		QMessageBox::critical(this, tr("File Error"), tr("Please specify an image file to use."));
	}
	if (status == STATUS_EXIT)
	{
		close();
	}
	status = STATUS_IDLE;
	elapsed_timer->stop();
}

// getLogicalDrives sets cBoxDevice with any logical drives found, as long
// as they indicate that they're either removable, or fixed and on USB bus
void MainWindow::getLogicalDrives()
{
	// GetLogicalDrives returns 0 on failure, or a bitmask representing
	// the drives available on the system (bit 0 = A:, bit 1 = B:, etc)
	unsigned long driveMask = GetLogicalDrives();
	int i = 0;
	ULONG pID = 0;  // Initialize to prevent use of uninitialized value

	dbgLog(QString("getLogicalDrives: driveMask=0x%1").arg(driveMask, 0, 16).toLatin1().data());

	cboxDevice->clear();

	// Track which physical drive IDs have been added (have drive letters)
	QSet<DWORD> addedPhysicalDrives;

	while (driveMask != 0)
	{
		if (driveMask & 1)
		{
			// the "A" in drivename will get incremented by the # of bits
			// we've shifted
			char drivename[] = "\\\\.\\A:\\";
			drivename[4] += i;
			bool ignored = isDriveIgnored(drivename[4]);
			dbgLog(QString("  Drive %1: ignored=%2").arg(drivename[4]).arg(ignored).toLatin1().data());
			if (!ignored)
			{
				bool result = checkDriveType(drivename, &pID);
				dbgLog(QString("  checkDriveType returned %1, pID=%2").arg(result).arg(pID).toLatin1().data());
				if (result)
				{
					// Get drive size for display
					HANDLE hDrive = getHandleOnDevice(pID, FILE_READ_ATTRIBUTES);
					unsigned long long sectorSize = 0;
					unsigned long long numSectors = 0;
					if (hDrive != INVALID_HANDLE_VALUE) {
						numSectors = getNumberOfSectors(hDrive, &sectorSize);
						CloseHandle(hDrive);
					}
					double sizeGB = (numSectors * sectorSize) / (1024.0 * 1024.0 * 1024.0);

					QString displayText = QString("PhysicalDrive%1 [%2:\\] (%3 GB)")
						.arg(pID).arg(drivename[4]).arg(sizeGB, 0, 'f', 2);
					cboxDevice->addItem(displayText, (qulonglong)pID);
					addedPhysicalDrives.insert(pID);
					dbgLog(QString("  ADDED drive %1 to list").arg(drivename[4]).toLatin1().data());
				}
			}
		}
		driveMask >>= 1;
		cboxDevice->setCurrentIndex(0);
		++i;
	}

	// Now add any unmounted physical drives (no drive letter assigned)
	dbgLog("Checking for unmounted physical drives...");
	QList<QPair<DWORD, qulonglong>> physicalDrives = enumeratePhysicalDrives();

	for (const auto& drive : physicalDrives)
	{
		DWORD driveNum = drive.first;
		qulonglong sizeBytes = drive.second;

		// Skip if this physical drive already has a drive letter
		if (addedPhysicalDrives.contains(driveNum))
		{
			dbgLog(QString("  PhysicalDrive%1 already has drive letter, skipping").arg(driveNum).toLatin1().data());
			continue;
		}

		// Format size nicely (GB with 2 decimal places)
		double sizeGB = sizeBytes / (1024.0 * 1024.0 * 1024.0);
		QString displayText = QString("PhysicalDrive%1 (%2 GB)").arg(driveNum).arg(sizeGB, 0, 'f', 2);

		cboxDevice->addItem(displayText, (qulonglong)driveNum);
		dbgLog(QString("  ADDED unmounted: %1").arg(displayText).toLatin1().data());
	}

	cboxDevice->setCurrentIndex(0);
	dbgLog(QString("getLogicalDrives: done, %1 devices in list").arg(cboxDevice->count()).toLatin1().data());
}

// support routine for winEvent - returns the drive letter for a given mask
//   taken from http://support.microsoft.com/kb/163503
char FirstDriveFromMask(ULONG unitmask)
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

// register to receive notifications when USB devices are inserted or removed
// adapted from http://www.known-issues.net/qt/qt-detect-event-windows.html
bool MainWindow::nativeEvent(const QByteArray& type, void* vMsg, long* result)
{
	Q_UNUSED(type);
	MSG* msg = (MSG*)vMsg;
	if (msg->message == WM_DEVICECHANGE)
	{
		if (status != STATUS_IDLE) {
			return false;
		}
		PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)msg->lParam;
		switch (msg->wParam)
		{
		case DBT_DEVICEARRIVAL:
			if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
			{
				PDEV_BROADCAST_VOLUME lpdbv = (PDEV_BROADCAST_VOLUME)lpdb;
				// Only process non-network drives (skip if DBTF_NET flag is set)
				if (!(lpdbv->dbcv_flags & DBTF_NET))
				{
					char ALET = FirstDriveFromMask(lpdbv->dbcv_unitmask);
					// add device to combo box (after sanity check that
					// it's not already there, which it shouldn't be)
					QString qs = QString("[%1:\\]").arg(ALET);
					if (cboxDevice->findText(qs) == -1)
					{
						ULONG pID;
						char longname[] = "\\\\.\\A:\\";
						longname[4] = ALET;
						if (!isDriveIgnored(longname[4]))
						{
							// checkDriveType gets the physicalID
							if (checkDriveType(longname, &pID))
							{
								//cboxDevice->addItem(qs, (qulonglong)pID);
								cboxDevice->clear();
								getLogicalDrives();
								setReadWriteButtonState();
							}
						}
					}
				}
			}
			break;
		case DBT_DEVICEREMOVECOMPLETE:
			if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
			{
				PDEV_BROADCAST_VOLUME lpdbv = (PDEV_BROADCAST_VOLUME)lpdb;
				// Only process non-network drives (skip if DBTF_NET flag is set)
				if (!(lpdbv->dbcv_flags & DBTF_NET))
				{
					cboxDevice->clear();
					getLogicalDrives();
					setReadWriteButtonState();
				}
			}
			break;
		default:
			// Other device change events (DBT_DEVNODES_CHANGED, etc.) - ignore
			break;
		} // skip the rest
	} // end of if msg->message
	*result = 0; //get rid of obnoxious compiler warning
	return false; // let qt handle the rest
}

void MainWindow::updateHashControls()
{
	QFileInfo fileinfo(leFile->text());
	bool validFile = (fileinfo.exists() && fileinfo.isFile() &&
		fileinfo.isReadable() && (fileinfo.size() > 0));

	bHashCopy->setEnabled(false);
	hashLabel->clear();

	if (cboxHashType->currentIndex() != 0 && !leFile->text().isEmpty() && validFile)
	{
		bHashGen->setEnabled(true);
	}
	else
	{
		bHashGen->setEnabled(false);
	}

	// if there's a value in the md5 label make the copy button visible
	bool haveHash = !(hashLabel->text().isEmpty());
	bHashCopy->setEnabled(haveHash);
}

void MainWindow::on_cboxHashType_currentIndexChanged(int)
{
	updateHashControls();
	QFileInfo fileinfo(leFile->text());
	bool validFile = (fileinfo.exists() && fileinfo.isFile() &&
		fileinfo.isReadable() && (fileinfo.size() > 0));
	if (cboxHashType->currentIndex() != 0 && !leFile->text().isEmpty() && validFile)
	{
		// Store QByteArray in local variable to prevent use-after-free
		// (toLatin1().data() returns pointer to temporary that would be destroyed)
		QByteArray filename = leFile->text().toLatin1();
		generateHash(filename.data(), cboxHashType->currentData().toInt());
	}
}

void MainWindow::on_bHashGen_clicked()
{
	// Store QByteArray in local variable to prevent use-after-free
	// (toLatin1().data() returns pointer to temporary that would be destroyed)
	QByteArray filename = leFile->text().toLatin1();
	generateHash(filename.data(), cboxHashType->currentData().toInt());
}

void MainWindow::on_bDetect_clicked()
{
	QByteArray blob;
	bCancel->setEnabled(true);
	bWrite->setEnabled(false);
	bRead->setEnabled(false);
	bVerify->setEnabled(false);
	status = STATUS_READING;
    int FLASHTYPE; //= 0x1024; // Default to UFS
	double mbpersec;
	unsigned long long i, lasti, numsectors = 0ull;
	// changes ..
	DWORD deviceID = cboxDevice->currentData().toUInt();  // Device ID stored as item data
	hRawDisk = getHandleOnDevice(deviceID, GENERIC_READ);

	// addition
	if (!getLockOnVolume(hRawDisk))
	{
		CloseHandle(hRawDisk);
		status = STATUS_IDLE;
		hRawDisk = INVALID_HANDLE_VALUE;
		bCancel->setEnabled(false);
		setReadWriteButtonState();
		return;
	}
	if (!unmountVolume(hRawDisk))
	{
		removeLockOnVolume(hRawDisk);
		CloseHandle(hRawDisk);
		status = STATUS_IDLE;
		hRawDisk = INVALID_HANDLE_VALUE;
		bCancel->setEnabled(false);
		setReadWriteButtonState();
		return;
	}

	// Note: hRawDisk cannot be INVALID_HANDLE_VALUE at this point since we would
	// have already returned earlier if getHandleOnDevice, getLockOnVolume, or
	// unmountVolume failed. Removed dead code that had wrong handle cleanup.
	bDetect->setEnabled(false);

	numsectors = getNumberOfSectors(hRawDisk, &sectorsize);
	if (partitionCheckBox->isChecked())
	{
		// Read MBR partition table
		sectorData = readSectorDataFromHandle(hRawDisk, 0, 1ul, 512ul);
		if (sectorData != NULL)
		{
			numsectors = 1ul;
			// Read partition information - verify buffer has enough data for MBR partition table
			// MBR partition table starts at 0x1BE, each entry is 16 bytes, 4 entries = 64 bytes
			// Need at least 0x1BE + 64 = 510 bytes, but we read 512 so this is safe
			for (i = 0ul; i < 4ul; i++)
			{
				// Use memcpy for portable unaligned access (safe on all architectures)
				uint32_t partitionStartSector = 0;
				uint32_t partitionNumSectors = 0;
				memcpy(&partitionStartSector, sectorData + 0x1BE + 8 + 16 * i, sizeof(uint32_t));
				memcpy(&partitionNumSectors, sectorData + 0x1BE + 12 + 16 * i, sizeof(uint32_t));
				// Set numsectors to end of last partition
				if (partitionStartSector + partitionNumSectors > numsectors) {
					numsectors = partitionStartSector + partitionNumSectors;
				}
			}
			// Clean up MBR sectorData before it gets overwritten by detection read
			delete[] sectorData;
			sectorData = NULL;
		}
	}
	if (numsectors == 0ul)
	{
		progressbar->setRange(0, 100);
	}
	else
	{
		// Cap numsectors at INT_MAX to prevent overflow when casting to int
			progressbar->setRange(0, (int)qMin(numsectors, (unsigned long long)INT_MAX));
	}
	lasti = 0ul;
	update_timer.start();
	elapsed_timer->start();
	const unsigned long long sectorsToRead = 64;
	// Note: Loop intentionally executes exactly once (i < 1) to read first sector block for detection
	// The status check allows cancellation during the read operation
	if (status == STATUS_READING)
	{
		i = 0ul;
		sectorData = readSectorDataFromHandle(hRawDisk, i, sectorsToRead, sectorsize);

		if (sectorData == NULL)
		{
			removeLockOnVolume(hRawDisk);
			CloseHandle(hRawDisk);
			// Note: hFile is not opened in on_bDetect_clicked, so only close if valid
			if (hFile != INVALID_HANDLE_VALUE) {
				CloseHandle(hFile);
				hFile = INVALID_HANDLE_VALUE;
			}
			status = STATUS_IDLE;
			hRawDisk = INVALID_HANDLE_VALUE;
			bCancel->setEnabled(false);
			setReadWriteButtonState();
			return;
		}
	}
	// Use calculated size instead of hard-coded value (was 32678, typo for 32768 = 64*512)
	int actualBlobSize = (int)(sectorsToRead * sectorsize);
	// Use QByteArray constructor for DEEP COPY instead of fromRawData() which creates shallow copy
	// This prevents use-after-free when sectorData is deleted later
	blob.append(QByteArray(sectorData, actualBlobSize));

	// do something with sectorData
	//
	//
	int j = 0;
	QString Location;
	// 0x1024 = 4096
	bool Found = false;
	while ((j = blob.indexOf(QByteArray::fromHex("4546492050415254"), j)) != -1) {
		//qDebug() << "Found EFI Header at index position " << "0x" + Location.setNum(j,16);
		this->statLabel->setText("Found EFI Header at index position 0x" + Location.setNum(j, 16));
		FLASHTYPE = Location.toUInt(nullptr, 16);
		Found = true;
		++j;
		break;
	}
	if (FLASHTYPE == 0x1000)
	{
		this->statLabel->setText("UFS Detected");
	}
	else
	{
		this->statLabel->setText("eMMC Detected");
	}
	if (Found) {
		//qDebug() << "Here we go again " << Location.toUInt(nullptr,16) << "\n";
		// Use qint64 to prevent overflow when PartitionEntries + 0x80 is repeated 128 times
		qint64 PartitionEntries = (qint64)Location.toUInt(nullptr, 16) + FLASHTYPE;
		//qDebug() << "Adress : " << PartitionEntries << "\n";
		QList<QString> pName;
		QList<QString> pStart;
		QList<QString> pSize;
		QList<QString> pEnd;

		pName.clear();
		pStart.clear();
		pEnd.clear();

		// Delete old model before creating new one to prevent memory leak
		QAbstractItemModel* oldModel = this->PartView->model();

		for (int i = 0; i < 0x80; i++)
		{
			QByteArray FirstLBA;
			QByteArray LastLBA;
			QByteArray PartitionName;
			QByteArray StartAdrHex;
			QByteArray EndAdrHex;

			// Bounds check: ensure blob has enough data for this partition entry
			// Need to access up to PartitionEntries + 0x38 + 72 = PartitionEntries + 0xAA
			if (PartitionEntries + 0xAA > blob.size())
			{
				break;  // Not enough data in blob
			}
			// Guard against qint64→int overflow when PartitionEntries exceeds INT_MAX
			if (PartitionEntries > INT_MAX - 0x80)
			{
				break;  // Prevent integer overflow in cast to int
			}

			// Cast to int AFTER addition to prevent overflow in intermediate calculation
			FirstLBA = blob.mid((int)(PartitionEntries + 0x20), 8);
			LastLBA = blob.mid((int)(PartitionEntries + 0x28), 8);
			PartitionName = blob.mid((int)(PartitionEntries + 0x38), 72);
			PartitionEntries = PartitionEntries + 0x80;

			StartAdrHex = this->swapper(FirstLBA);
			EndAdrHex = this->swapper(LastLBA);

			if (FirstLBA != QByteArray::fromHex("0000000000000000"))
			{
				// GET RID OF spaces - validate indexOf result before truncate
				int chop = PartitionName.indexOf(QByteArray::fromHex("0000"));
				if (chop >= 0) {
					PartitionName.truncate(chop + 1);
				}
				// else: pattern not found, keep PartitionName as-is

				bool ok;
				qulonglong rawStartVal = StartAdrHex.toHex().toULongLong(&ok, 16);
				qulonglong rawEndVal = EndAdrHex.toHex().toULongLong(&ok, 16);

				// Check for multiplication overflow before computing addresses
				// Max safe value: ULLONG_MAX / FLASHTYPE
				const qulonglong maxSafeValue = ULLONG_MAX / (qulonglong)FLASHTYPE;
				qulonglong tempStartAdr = 0;
				qulonglong tempEndAdr = 0;

				if (rawStartVal <= maxSafeValue) {
					tempStartAdr = rawStartVal * FLASHTYPE;
				}
				if (rawEndVal <= maxSafeValue) {
					tempEndAdr = (rawEndVal * FLASHTYPE) + FLASHTYPE;
				}

				QString StartAdr = QString("%1").arg(tempStartAdr, 12, 16, QLatin1Char('0'));
				QString EndAdr = QString("%1").arg(tempEndAdr, 12, 16, QLatin1Char('0'));

				// Check QTextCodec for NULL before using (may be unavailable on some systems)
				QTextCodec* codec = QTextCodec::codecForMib(1015);
				QString PartName;
				if (codec != nullptr) {
					PartName = QString("%1").arg(codec->toUnicode(PartitionName), 18);
				} else {
					// Fallback to Latin-1 if UTF-16LE codec not available
					PartName = QString("%1").arg(QString::fromLatin1(PartitionName), 18);
				}

				qulonglong PartitionSize = tempEndAdr - tempStartAdr;
				QString PartSize = QString("%1").arg(PartitionSize, 12, 16, QLatin1Char('0'));

				//qDebug() << PartName <<   " : " << StartAdr << " : " << EndAdr ;
				this->statLabel->setText(PartName + " : " + StartAdr + " : " + PartSize + " : " + EndAdr);

				if (pName.isEmpty())
				{
					QString GPT = QString("%1").arg(("GPT"), 18);
					pName.append(GPT);
					pStart.append("000000000000");
					pSize.append(StartAdr);
					pEnd.append(StartAdr);
				}

				pName.append(PartName);
				pStart.append(StartAdr);
				pSize.append(PartSize);
				pEnd.append(EndAdr);
			}
			else {
				// Hit the White space
				break;
			}

			if (update_timer.elapsed() >= ONE_SEC_IN_MS)
			{
				mbpersec = (((double)sectorsize * (i - lasti)) * ((float)ONE_SEC_IN_MS / update_timer.elapsed())) / 1024.0 / 1024.0;
				statusbar->showMessage(QString("%1MB/s").arg(mbpersec));
				update_timer.start();
				elapsed_timer->update(i, numsectors);
				lasti = i;
			}
			progressbar->setValue(i);
			QCoreApplication::processEvents();
		}

		// Create model ONCE after collecting all partition data (moved out of loop to prevent memory leak)
		// Delete old model if it exists
		if (oldModel != nullptr) {
			delete oldModel;
		}

		TestModel* PhoneBookModel = new TestModel(this);

		// Populate model with data:
		PhoneBookModel->populateData(pName, pStart, pEnd, pSize);

		// Connect model to table view:
		this->PartView->setModel(PhoneBookModel);

		// Make table header visible and display table:
		QHeaderView* verticalHeader = PartView->verticalHeader();
		verticalHeader->setSectionResizeMode(QHeaderView::Fixed);
		verticalHeader->setDefaultSectionSize(18);
		this->PartView->horizontalHeader()->setVisible(true);
		this->PartView->horizontalHeader()->setStretchLastSection(true);
		this->PartView->show();

		// Clean up sectorData after processing
		delete[] sectorData;
		sectorData = NULL;
	}

	removeLockOnVolume(hRawDisk);
	CloseHandle(hRawDisk);
	hRawDisk = INVALID_HANDLE_VALUE;
	// Only close hFile if it was opened (in on_bDetect_clicked, it's not used)
	if (hFile != INVALID_HANDLE_VALUE) {
		CloseHandle(hFile);
		hFile = INVALID_HANDLE_VALUE;
	}
	progressbar->reset();
	statusbar->showMessage(tr("Done."));
	bCancel->setEnabled(false);
	setReadWriteButtonState();
	bDetect->setEnabled(true);

	updateHashControls();
	if (status == STATUS_EXIT)
	{
		close();
	}
	status = STATUS_IDLE;
	elapsed_timer->stop();
}

void TestModel::populateData(const QList<QString>& pName, const QList<QString>& pStart, const QList<QString>& pEnd, const QList<QString>& pSize)
{
	tm_partition_name.clear();
	tm_partition_name = pName;
	tm_partition_start.clear();
	tm_partition_start = pStart;
	tm_partition_end.clear();
	tm_partition_end = pEnd;
	tm_partition_size.clear();
	tm_partition_size = pSize;
	return;
}

int TestModel::rowCount(const QModelIndex& parent) const
{
	Q_UNUSED(parent);
	return tm_partition_name.length();
}

int TestModel::columnCount(const QModelIndex& parent) const
{
	Q_UNUSED(parent);
	return 4;
}

QVariant TestModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || role != Qt::DisplayRole) {
		return QVariant();
	}
	// Bounds check to prevent undefined behavior if index.row() >= list size
	int row = index.row();
	if (row < 0 || row >= tm_partition_name.size()) {
		return QVariant();
	}
	if (index.column() == 0) {
		return tm_partition_name[row];
	}
	else if (index.column() == 1) {
		return tm_partition_start[row];
	}
	else if (index.column() == 2) {
		return tm_partition_size[row];
	}
	else if (index.column() == 3) {
		return tm_partition_end[row];
	}
	return QVariant();
}

QVariant TestModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
		if (section == 0) {
			return QString("PART.NAME");
		}
		else if (section == 1) {
			return QString("START");
		}
		else if (section == 2) {
			return QString("SIZE");
		}
		else if (section == 3) {
			return QString("END");
		}
	}
	return QVariant();
}

QByteArray MainWindow::swapper(QByteArray input)
{
	// Reverse byte order (for endianness conversion)
	// Use input.size() as loop limit instead of arbitrary 0x80
	QByteArray output;
	output.reserve(input.size());
	for (int i = input.size() - 1; i >= 0; --i)
	{
		output.append(input[i]);
	}
	return output;
}

void MainWindow::on_tbSearch_clicked()
{
	if (status == STATUS_IDLE) {
		cboxDevice->clear();
		getLogicalDrives();
	}
}
