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

#include "elapsedtimer.h"

ElapsedTimer::ElapsedTimer(QWidget* parent) : QLabel(parent)
{
	timer = new QElapsedTimer();
	setText("00:00:00");
	setToolTip(tr("Elapsed Time"));
}

ElapsedTimer::~ElapsedTimer()
{
	delete timer;
	timer = NULL;
}

int ElapsedTimer::ms()
{
	return timer->elapsed();
}

void ElapsedTimer::update(unsigned long long current, unsigned long long total)
{
	// Use qint64 to avoid integer overflow for long-running operations
	qint64 elapsedMs = timer->elapsed();
	qint64 baseSecs = elapsedMs / MS_PER_SEC;

	if (total > 0 && current < total && current > 0)
	{
		// Use double for better precision in remaining time calculation
		double ratio = (double)(total - current) / (double)current;
		qint64 totalSecs = (qint64)(ratio * baseSecs);

		// Cap at reasonable maximum (999:59:59)
		if (totalSecs > 3599999) totalSecs = 3599999;

		qint64 hours = totalSecs / (SEC_PER_MIN * MIN_PER_HOUR);
		qint64 mins = (totalSecs % (SEC_PER_MIN * MIN_PER_HOUR)) / SEC_PER_MIN;
		qint64 secs = totalSecs % SEC_PER_MIN;
		setText(QString("%1:%2:%3")
			.arg(hours, 2, 10, QChar('0'))
			.arg(mins, 2, 10, QChar('0'))
			.arg(secs, 2, 10, QChar('0')));
	}
	else
	{
		// Cap at reasonable maximum (999:59:59)
		if (baseSecs > 3599999) baseSecs = 3599999;

		qint64 hours = baseSecs / (SEC_PER_MIN * MIN_PER_HOUR);
		qint64 mins = (baseSecs % (SEC_PER_MIN * MIN_PER_HOUR)) / SEC_PER_MIN;
		qint64 secs = baseSecs % SEC_PER_MIN;
		setText(QString("%1:%2:%3")
			.arg(hours, 2, 10, QChar('0'))
			.arg(mins, 2, 10, QChar('0'))
			.arg(secs, 2, 10, QChar('0')));
	}
}

void ElapsedTimer::start()
{
	timer->start();
	setText("00:00:00");
}

void ElapsedTimer::stop()
{
	timer->invalidate();
	setText("00:00:00");
}