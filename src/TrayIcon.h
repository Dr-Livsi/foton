/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QObject>

#include <memory>

class QMenu;
class QSystemTrayIcon;

/**
 * The system tray icon that is shown while Foton runs in the background.
 * Clicking it takes a screenshot, the context menu has the settings, help and quit.
 */
class TrayIcon : public QObject
{
    Q_OBJECT

public:
    static TrayIcon *instance();
    ~TrayIcon() override;

    void setVisible(bool visible);
    bool isVisible() const;

private:
    TrayIcon();
    void createTrayIcon();
    void updateToolTip();

    std::unique_ptr<QSystemTrayIcon> m_trayIcon;
    std::unique_ptr<QMenu> m_menu;
};
