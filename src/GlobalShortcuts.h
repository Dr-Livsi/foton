/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QKeySequence>
#include <QList>
#include <QObject>

#include <memory>

class QAction;
class GlobalShortcutsPrivate;

/**
 * Registers the shortcuts of the actions in ShortcutActions as system wide hotkeys.
 *
 * KGlobalAccel is not available on Windows, so the hotkeys are registered with
 * RegisterHotKey(). The hotkeys are updated automatically when the shortcuts of the actions change.
 * On other platforms, this class does nothing.
 */
class GlobalShortcuts : public QObject
{
    Q_OBJECT

public:
    static GlobalShortcuts *instance();
    ~GlobalShortcuts() override;

    /// Whether system wide hotkeys can be registered on this platform.
    static bool isSupported();

    /// Start or stop registering hotkeys.
    void setEnabled(bool enabled);
    bool isEnabled() const;

    /**
     * Temporarily unregister all hotkeys, e.g. while the shortcuts are being edited.
     * Pressing a hotkey would otherwise take a screenshot instead of being recorded.
     */
    void setSuspended(bool suspended);
    bool isSuspended() const;

    /// The shortcuts that could not be registered, usually because another application or Windows uses them.
    QList<QKeySequence> failedShortcuts() const;

    /// Check which shortcuts can be registered right now, even while suspended.
    void checkShortcuts();

Q_SIGNALS:
    void activated(QAction *action);
    void failedShortcutsChanged();

private:
    GlobalShortcuts();
    void scheduleUpdate();
    void updateRegistrations();
    void registerAll(bool keepRegistered);
    void unregisterAll();
    void setFailedShortcuts(const QList<QKeySequence> &failed);

    std::unique_ptr<GlobalShortcutsPrivate> d;
    friend class GlobalShortcutsPrivate;
};
