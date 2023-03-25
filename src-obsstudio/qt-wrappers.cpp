/******************************************************************************
    Copyright (C) 2013 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "qt-wrappers.hpp"
#define QTStr(x) (QString(x))

#include <graphics/graphics.h>
#include <util/threading.h>
#include <QKeyEvent>

bool LineEditCanceled(QEvent *event)
{
	if (event->type() == QEvent::KeyPress) {
		QKeyEvent *keyEvent = reinterpret_cast<QKeyEvent *>(event);
		return keyEvent->key() == Qt::Key_Escape;
	}

	return false;
}

bool LineEditChanged(QEvent *event)
{
	if (event->type() == QEvent::KeyPress) {
		QKeyEvent *keyEvent = reinterpret_cast<QKeyEvent *>(event);

		switch (keyEvent->key()) {
		case Qt::Key_Tab:
		case Qt::Key_Backtab:
		case Qt::Key_Enter:
		case Qt::Key_Return:
			return true;
		}
	} else if (event->type() == QEvent::FocusOut) {
		return true;
	}

	return false;
}
