/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "TrayIcon.h"

#include "AppIcon.h"
#include "FotonCore.h"
#include "Gui/HelpMenu.h"
#include "Gui/OptionsMenu.h"
#include "ShortcutActions.h"

#include <KLocalizedString>

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPointer>
#include <QSystemTrayIcon>

using namespace Qt::StringLiterals;

TrayIcon *TrayIcon::instance()
{
    static QPointer<TrayIcon> s_instance;
    if (!s_instance) {
        s_instance = new TrayIcon;
        s_instance->setParent(QCoreApplication::instance());
    }
    return s_instance;
}

TrayIcon::TrayIcon()
    : QObject(nullptr)
{
}

TrayIcon::~TrayIcon() = default;

void TrayIcon::createTrayIcon()
{
    m_trayIcon = std::make_unique<QSystemTrayIcon>(fotonIcon());
    m_menu = std::make_unique<QMenu>();

    auto core = FotonCore::instance();
    auto actions = ShortcutActions::self();

    // Screenshots are taken by clicking the icon or with the global shortcuts.
    auto configureAction = m_menu->addAction(QIcon::fromTheme(u"configure"_s), i18nc("@action:inmenu", "Configure Foton…"));
    connect(configureAction, &QAction::triggered, this, [] {
        OptionsMenu::instance()->showPreferencesDialog();
    });

    m_menu->addMenu(HelpMenu::instance());

    m_menu->addSeparator();

    auto quitAction = m_menu->addAction(QIcon::fromTheme(u"application-exit"_s), i18nc("@action:inmenu", "Quit"));
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit, Qt::QueuedConnection);

    m_trayIcon->setContextMenu(m_menu.get());

    connect(m_trayIcon.get(), &QSystemTrayIcon::activated, core, [core](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            // The same as pressing the screenshot key.
            core->activate({QCoreApplication::applicationFilePath()}, {});
        }
    });

    connect(actions->openAction(), &QAction::changed, this, &TrayIcon::updateToolTip);
    updateToolTip();
}

void TrayIcon::updateToolTip()
{
    if (!m_trayIcon) {
        return;
    }
    const auto shortcuts = ShortcutActions::self()->openAction()->shortcuts();
    const auto shortcut = shortcuts.isEmpty() ? QKeySequence() : shortcuts.constFirst();
    if (shortcut.isEmpty()) {
        m_trayIcon->setToolTip(i18nc("@info:tooltip", "Foton\nClick to take a screenshot"));
    } else {
        m_trayIcon->setToolTip(i18nc("@info:tooltip %1 is a keyboard shortcut",
                                     "Foton\nClick or press %1 to take a screenshot",
                                     shortcut.toString(QKeySequence::NativeText)));
    }
}

void TrayIcon::setVisible(bool visible)
{
    if (visible && !m_trayIcon) {
        if (!QSystemTrayIcon::isSystemTrayAvailable()) {
            return;
        }
        createTrayIcon();
    }
    if (m_trayIcon) {
        m_trayIcon->setVisible(visible);
    }
}

bool TrayIcon::isVisible() const
{
    return m_trayIcon && m_trayIcon->isVisible();
}

#include "moc_TrayIcon.cpp"
