/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QUrl>

class QWindow;

/**
 * Functions for integrating with the Windows shell.
 * On other platforms, they fall back to generic Qt implementations or do nothing.
 */
namespace WindowsIntegration
{
/// Open the folder containing the file and select the file.
void showInFileManager(const QUrl &url);

/// Add the file to the recently used documents of the user.
void addToRecentDocuments(const QUrl &url);

/// Show the progress in the taskbar button of the window.
void setTaskbarProgress(QWindow *window, bool visible, qreal progress);

/// Disable the animations of the desktop compositor for the window.
void disableWindowTransitions(QWindow *window);

/// Start the application in the system tray when the user logs in.
void setStartWithSystem(bool enabled);
bool startsWithSystem();

/**
 * Allow other processes (e.g. an already running instance) to bring their windows to the front.
 * Must be called by the process that currently has the right to do that.
 */
void allowOtherProcessToActivateWindows();

/// Wait until the process with the given ID has quit, at most for the given time.
void waitForProcessToQuit(qint64 processId, int timeoutMs);
}
