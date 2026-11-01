/*
 * SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SHORTCUT_ACTIONS_H
#define SHORTCUT_ACTIONS_H

#include <KActionCollection>

/**
 * The actions that can be triggered with system wide (global) shortcuts.
 *
 * The shortcuts of these actions are not used inside the application.
 * They are edited with the shortcuts settings page, stored in the configuration file
 * and registered as system wide hotkeys by GlobalShortcuts.
 */
class ShortcutActions
{
public:
    static ShortcutActions *self();

    KActionCollection *shortcutActions();

    QString componentName() const;

    QAction *openAction() const;
    QAction *fullScreenAction() const;
    QAction *currentScreenAction() const;
    QAction *activeWindowAction() const;
    QAction *regionAction() const;
    QAction *windowUnderCursorAction() const;
    QAction *openWithoutScreenshotAction() const;

    /// Load the shortcuts saved in the configuration file.
    void readSettings();
    /// Save the current shortcuts to the configuration file.
    void writeSettings();

private:
    ShortcutActions();
    QAction *addShortcutAction(const QString &name, const QString &text, const QList<QKeySequence> &defaultShortcuts);
    KActionCollection mActions;
};

#endif
