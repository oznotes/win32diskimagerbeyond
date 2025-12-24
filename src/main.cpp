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

#include <QApplication>
#include <QFile>
#include <cstdio>
#include <cstdlib>
#include <windows.h>
#include <winioctl.h>
#include "mainwindow.h"

#ifdef DEBUG_LOGGING
// Simple debug logger (only enabled in debug builds)
static FILE* debugLog = nullptr;

void dbgLog(const char* msg) {
	if (!debugLog) {
		debugLog = fopen("debug.log", "w");
	}
	if (debugLog) {
		fprintf(debugLog, "%s\n", msg);
		fflush(debugLog);
	}
}

void dbgLogClose() {
	if (debugLog) {
		fclose(debugLog);
		debugLog = nullptr;
	}
}
#else
// No-op stubs for release builds
inline void dbgLog(const char*) {}
inline void dbgLogClose() {}
#endif

int main(int argc, char* argv[])
{
	dbgLog("Step 1: Starting");
	QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
	QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
	dbgLog("Step 2: Creating QApplication");
	QApplication app(argc, argv);
	dbgLog("Step 3: QApplication created");
	app.setApplicationDisplayName(VER);

	QTranslator translator;
	translator.load("translations/diskimager_" + QLocale::system().name());
	app.installTranslator(&translator);

	// Load modern stylesheet
	QFile styleFile(":/style.qss");
	if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
		app.setStyleSheet(styleFile.readAll());
		styleFile.close();
		dbgLog("Step 3.5: Stylesheet loaded");
	}

	dbgLog("Step 4: Creating MainWindow");
	MainWindow* mainwindow = MainWindow::getInstance();
	dbgLog("Step 5: MainWindow created, showing");
	mainwindow->show();
	dbgLog("Step 6: Entering event loop");
	int result = app.exec();
	dbgLog("Step 7: Event loop exited, cleaning up");
	dbgLogClose();
	return result;
}
