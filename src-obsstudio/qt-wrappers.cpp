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
#include <QWidget>
#include <QLayout>
#include <QMessageBox>
#include <QDataStream>
#include <QKeyEvent>
#include <QFileDialog>

static inline void OBSErrorBoxva(QWidget *parent, const char *msg, va_list args)
{
	char full_message[4096];
	vsnprintf(full_message, 4095, msg, args);

	QMessageBox::critical(parent, "Error", full_message);
}

void OBSErrorBox(QWidget *parent, const char *msg, ...)
{
	va_list args;
	va_start(args, msg);
	OBSErrorBoxva(parent, msg, args);
	va_end(args);
}

uint32_t TranslateQtKeyboardEventModifiers(Qt::KeyboardModifiers mods)
{
	int obsModifiers = INTERACT_NONE;

	if (mods.testFlag(Qt::ShiftModifier))
		obsModifiers |= INTERACT_SHIFT_KEY;
	if (mods.testFlag(Qt::AltModifier))
		obsModifiers |= INTERACT_ALT_KEY;
#ifdef __APPLE__
	// Mac: Meta = Control, Control = Command
	if (mods.testFlag(Qt::ControlModifier))
		obsModifiers |= INTERACT_COMMAND_KEY;
	if (mods.testFlag(Qt::MetaModifier))
		obsModifiers |= INTERACT_CONTROL_KEY;
#else
	// Handle windows key? Can a browser even trap that key?
	if (mods.testFlag(Qt::ControlModifier))
		obsModifiers |= INTERACT_CONTROL_KEY;
	if (mods.testFlag(Qt::MetaModifier))
		obsModifiers |= INTERACT_COMMAND_KEY;

#endif

	return obsModifiers;
}

QDataStream &operator<<(QDataStream &out,
			const std::vector<std::shared_ptr<OBSSignal>> &)
{
	return out;
}

QDataStream &operator>>(QDataStream &in,
			std::vector<std::shared_ptr<OBSSignal>> &)
{
	return in;
}

QDataStream &operator<<(QDataStream &out, const OBSScene &scene)
{
	return out << QString(obs_source_get_name(obs_scene_get_source(scene)));
}

QDataStream &operator>>(QDataStream &in, OBSScene &scene)
{
	QString sceneName;

	in >> sceneName;

	obs_source_t *source = obs_get_source_by_name(QT_TO_UTF8(sceneName));
	scene = obs_scene_from_source(source);
	obs_source_release(source);

	return in;
}

QDataStream &operator<<(QDataStream &out, const OBSSceneItem &si)
{
	obs_scene_t *scene = obs_sceneitem_get_scene(si);
	obs_source_t *source = obs_sceneitem_get_source(si);
	return out << QString(obs_source_get_name(obs_scene_get_source(scene)))
		   << QString(obs_source_get_name(source));
}

QDataStream &operator>>(QDataStream &in, OBSSceneItem &si)
{
	QString sceneName;
	QString sourceName;

	in >> sceneName >> sourceName;

	obs_source_t *sceneSource =
		obs_get_source_by_name(QT_TO_UTF8(sceneName));

	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	si = obs_scene_find_source(scene, QT_TO_UTF8(sourceName));

	obs_source_release(sceneSource);

	return in;
}

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

QString SelectDirectory(QWidget *parent, QString title, QString path)
{
#if defined(BROWSER_AVAILABLE) && defined(__linux__)
	QString dir = QFileDialog::getExistingDirectory(
		parent, title, path,
		QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks |
			QFileDialog::DontUseNativeDialog);
#else
	QString dir = QFileDialog::getExistingDirectory(
		parent, title, path,
		QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
#endif

	return dir;
}

QString SaveFile(QWidget *parent, QString title, QString path,
		 QString extensions)
{
#if defined(BROWSER_AVAILABLE) && defined(__linux__)
	QString file = QFileDialog::getSaveFileName(
		parent, title, path, extensions, nullptr,
		QFileDialog::DontUseNativeDialog);
#else
	QString file =
		QFileDialog::getSaveFileName(parent, title, path, extensions);
#endif

	return file;
}

QString OpenFile(QWidget *parent, QString title, QString path,
		 QString extensions)
{
#if defined(BROWSER_AVAILABLE) && defined(__linux__)
	QString file = QFileDialog::getOpenFileName(
		parent, title, path, extensions, nullptr,
		QFileDialog::DontUseNativeDialog);
#else
	QString file =
		QFileDialog::getOpenFileName(parent, title, path, extensions);
#endif

	return file;
}

QStringList OpenFiles(QWidget *parent, QString title, QString path,
		      QString extensions)
{
#if defined(BROWSER_AVAILABLE) && defined(__linux__)
	QStringList files = QFileDialog::getOpenFileNames(
		parent, title, path, extensions, nullptr,
		QFileDialog::DontUseNativeDialog);
#else
	QStringList files =
		QFileDialog::getOpenFileNames(parent, title, path, extensions);
#endif

	return files;
}
