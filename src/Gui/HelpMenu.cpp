/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "HelpMenu.h"
#include "FotonCore.h"
#include "WidgetWindowUtils.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QApplication>
#include <QDialog>
#include <QWindow>

#include <cstring>

using namespace Qt::StringLiterals;

static HelpMenu *s_instance = nullptr;

static QObject *findWidgetOfType(const char *className)
{
    if (strlen(className) == 0) {
        return nullptr;
    }
    const auto widgets = qApp->allWidgets();
    for (const auto w : widgets) {
        if (w->inherits(className)) {
            return w;
        }
    }
    return nullptr;
}

HelpMenu::HelpMenu(QWidget *parent)
    : FotonMenu(parent)
    , kHelpMenu(new KHelpMenu(parent, KAboutData::applicationData()))
{
    setTitle(i18nc("@title:menu", "Help"));
    setIcon(QIcon::fromTheme(u"help-contents"_s));
    kHelpMenu->menu(); // creates the actions
    // There is no handbook for Foton, and bugs should not be reported to the KDE bug tracker.
    // The language is chosen in the settings of Foton.
    for (auto menuId : {KHelpMenu::menuHelpContents, KHelpMenu::menuReportBug, KHelpMenu::menuDonate, KHelpMenu::menuSwitchLanguage}) {
        if (auto action = kHelpMenu->action(menuId)) {
            action->setVisible(false);
            action->setEnabled(false);
        }
    }
    addActions(kHelpMenu->menu()->actions());
    connect(this, &QMenu::triggered, this, &HelpMenu::onTriggered);
}

HelpMenu::~HelpMenu()
{
    s_instance = nullptr;
}

HelpMenu *HelpMenu::instance()
{
    if (!s_instance && FotonCore::instance()) {
        s_instance = new HelpMenu;
        // We have to destroy this after FotonCore to ensure that destructors
        // are called. We don't just rely on smart pointers because they won't delete
        // the menus at the right time and cause a crash while quitting.
        connect(FotonCore::instance(), &QObject::destroyed, qApp, [] {
            if (s_instance) {
                delete s_instance;
            }
        });
    }
    return s_instance;
}

void HelpMenu::showAppHelp()
{
    // There is no handbook, show the about dialog instead.
    kHelpMenu->aboutApplication();
}

void HelpMenu::onTriggered(QAction *action)
{
    auto transientParent = getWidgetTransientParent(this);
    if (!transientParent || !transientParent->isVisible() || action == kHelpMenu->action(KHelpMenu::menuWhatsThis)) {
        return;
    }

    QDialog *dialog = nullptr;
    // KHelpMenu creates these dialogs and sets the parent of KHelpMenu as the parent of the dialogs.
    // KHelpMenu doesn't expose pointers to the dialogs,
    // so we have to search for them in the parent.
    // 2 of the dialogs we need to find are private types.
    if (action == kHelpMenu->action(KHelpMenu::menuReportBug)) {
        dialog = qobject_cast<QDialog *>(findWidgetOfType("KBugReport"));
    } else if (action == kHelpMenu->action(KHelpMenu::menuSwitchLanguage)) {
        dialog = qobject_cast<QDialog *>(findWidgetOfType("KDEPrivate::KSwitchLanguageDialog"));
    } else if (action == kHelpMenu->action(KHelpMenu::menuAboutApp)) {
        dialog = qobject_cast<QDialog *>(findWidgetOfType("KAboutApplicationDialog"));
    } else if (action == kHelpMenu->action(KHelpMenu::menuAboutKDE)) {
        dialog = qobject_cast<QDialog *>(findWidgetOfType("KDEPrivate::KAboutKdeDialog"));
    }

    if (dialog) {
        setWidgetTransientParent(dialog, transientParent);
        dialog->windowHandle()->requestActivate();
    }
}

#include "moc_HelpMenu.cpp"
