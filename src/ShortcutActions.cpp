/*
 * SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ShortcutActions.h"

#include <QAction>
#include <QCoreApplication>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

using namespace Qt::StringLiterals;

static const auto s_configGroupName = u"GlobalShortcuts"_s;

ShortcutActions *ShortcutActions::self()
{
    static ShortcutActions self;
    return &self;
}

ShortcutActions::ShortcutActions()
    : mActions{nullptr, QString()}
{
    mActions.setComponentName(componentName());
    mActions.setConfigGroup(s_configGroupName);
    mActions.setConfigGlobal(false);

    // The order of the actions matters, see the getters below.
    // The default shortcuts are similar to what other screenshot tools on Windows use.
    // Win+PrintScreen and Win+Shift+S are reserved by Windows, so we can't use them.
    addShortcutAction(u"_launch"_s, //
                      i18nc("@action global shortcut", "Launch Foton"),
                      {Qt::Key_Print});
    addShortcutAction(u"FullScreenScreenShot"_s, //
                      i18nc("@action global shortcut", "Capture Entire Desktop"),
                      {Qt::SHIFT | Qt::Key_Print});
    addShortcutAction(u"CurrentMonitorScreenShot"_s, //
                      i18nc("@action global shortcut", "Capture Current Monitor"),
                      {});
    addShortcutAction(u"ActiveWindowScreenShot"_s, //
                      i18nc("@action global shortcut", "Capture Active Window"),
                      {Qt::ALT | Qt::Key_Print});
    addShortcutAction(u"RectangularRegionScreenShot"_s, //
                      i18nc("@action global shortcut", "Capture Rectangular Region"),
                      {Qt::CTRL | Qt::Key_Print});
    addShortcutAction(u"WindowUnderCursorScreenShot"_s, //
                      i18nc("@action global shortcut", "Capture Window Under Cursor"),
                      {Qt::CTRL | Qt::ALT | Qt::Key_Print});
    addShortcutAction(u"OpenWithoutScreenshot"_s, //
                      i18nc("@action global shortcut", "Launch Foton without capturing"),
                      {});
}

QAction *ShortcutActions::addShortcutAction(const QString &name, const QString &text, const QList<QKeySequence> &defaultShortcuts)
{
    auto action = new QAction(text, &mActions);
    action->setObjectName(name);
    action->setProperty("isConfigurationAction", true);
    // These shortcuts are only used as system wide hotkeys, never inside a window.
    action->setShortcutContext(Qt::ApplicationShortcut);
    mActions.addAction(name, action);
    KActionCollection::setDefaultShortcuts(action, defaultShortcuts);
    return action;
}

KActionCollection *ShortcutActions::shortcutActions()
{
    return &mActions;
}

QString ShortcutActions::componentName() const
{
    return QCoreApplication::applicationName();
}

void ShortcutActions::readSettings()
{
    KConfigGroup group(KSharedConfig::openConfig(), s_configGroupName);
    mActions.readSettings(&group);
}

void ShortcutActions::writeSettings()
{
    KConfigGroup group(KSharedConfig::openConfig(), s_configGroupName);
    mActions.writeSettings(&group);
    group.sync();
}

QAction *ShortcutActions::openAction() const
{
    return mActions.action(0);
}

QAction *ShortcutActions::fullScreenAction() const
{
    return mActions.action(1);
}

QAction *ShortcutActions::currentScreenAction() const
{
    return mActions.action(2);
}

QAction *ShortcutActions::activeWindowAction() const
{
    return mActions.action(3);
}

QAction *ShortcutActions::regionAction() const
{
    return mActions.action(4);
}

QAction *ShortcutActions::windowUnderCursorAction() const
{
    return mActions.action(5);
}

QAction *ShortcutActions::openWithoutScreenshotAction() const
{
    return mActions.action(6);
}
