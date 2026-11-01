/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "OptionsMenu.h"

#include "CaptureModeModel.h"
#include "Gui/SettingsDialog/SettingsDialog.h"
#include "FotonCore.h"
#include "WidgetWindowUtils.h"
#include "settings.h"

#include <KLocalizedString>

#include <QList>
#include <QStyle>

using namespace Qt::StringLiterals;

static OptionsMenu *s_instance = nullptr;

OptionsMenu::OptionsMenu(QWidget *parent)
    : FotonMenu(parent)
{
    setToolTipsVisible(true);
    // QMenu::addSection just adds an action with text and separator mode enabled
    addSection(i18nc("@title:menu", "Screenshot Settings"));

    auto includeMousePointerAction = addAction(i18nc("@option:check for screenshots", "Include mouse pointer"));
    includeMousePointerAction->setToolTip(i18nc("@info:tooltip", "Show the mouse cursor in the screenshot image"));
    includeMousePointerAction->setCheckable(true);
    includeMousePointerAction->setChecked(Settings::includePointer());
    QObject::connect(includeMousePointerAction, &QAction::toggled, Settings::self(), &Settings::setIncludePointer);
    QObject::connect(Settings::self(), &Settings::includePointerChanged, includeMousePointerAction, [includeMousePointerAction](){
        includeMousePointerAction->setChecked(Settings::includePointer());
    });

    auto includeWindowDecorationsAction = addAction(i18nc("@option:check", "Include window titlebar and borders"));
    includeWindowDecorationsAction->setToolTip(i18nc("@info:tooltip", "Show the window title bar, the minimize/maximize/close buttons, and the window border"));
    includeWindowDecorationsAction->setCheckable(true);
    includeWindowDecorationsAction->setChecked(Settings::includeDecorations());
    QObject::connect(includeWindowDecorationsAction, &QAction::toggled, Settings::self(), Settings::setIncludeDecorations);
    QObject::connect(Settings::self(), &Settings::includeDecorationsChanged, includeWindowDecorationsAction, [includeWindowDecorationsAction](){
        includeWindowDecorationsAction->setChecked(Settings::includeDecorations());
    });

    auto includeWindowShadowAction = addAction(i18nc("@option:check", "Include window shadow"));
    includeWindowShadowAction->setToolTip(i18nc("@info:tooltip", "Show the window shadow"));
    includeWindowShadowAction->setCheckable(true);
    includeWindowShadowAction->setChecked(Settings::includeShadow());
    QObject::connect(includeWindowShadowAction, &QAction::toggled, Settings::self(), &Settings::setIncludeShadow);
    QObject::connect(Settings::self(), &Settings::includeShadowChanged, includeWindowShadowAction, [includeWindowShadowAction]() {
        includeWindowShadowAction->setChecked(Settings::includeShadow());
    });

    auto quitAfterSaveAction = addAction(i18nc("@option:check", "Quit after manual Save or Copy"));
    quitAfterSaveAction->setToolTip(i18nc("@info:tooltip", "Quit Foton after manually saving or copying the image"));
    quitAfterSaveAction->setCheckable(true);
    quitAfterSaveAction->setChecked(Settings::quitAfterSaveCopyExport());
    QObject::connect(quitAfterSaveAction, &QAction::toggled, Settings::self(), &Settings::setQuitAfterSaveCopyExport);
    QObject::connect(Settings::self(), &Settings::quitAfterSaveCopyExportChanged, quitAfterSaveAction, [quitAfterSaveAction](){
        quitAfterSaveAction->setChecked(Settings::quitAfterSaveCopyExport());
    });

}

OptionsMenu::~OptionsMenu()
{
    s_instance = nullptr;
}

OptionsMenu *OptionsMenu::instance()
{
    if (!s_instance && FotonCore::instance()) {
        s_instance = new OptionsMenu;
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

void OptionsMenu::showPreferencesDialog()
{
    KConfigDialog *dialog = KConfigDialog::exists(u"settings"_s);
    if (!dialog) {
        dialog = new SettingsDialog;
        dialog->setAttribute(Qt::WA_DeleteOnClose);
    }

    // properly set the transientparent chain
    setWidgetTransientParent(dialog, getWidgetTransientParent(this));

    // HACK: how to make it appear on top of the fullscreen window? dialog->setWindowFlags(Qt::Popup); is an ugly way

    dialog->show();
}

void OptionsMenu::keyPressEvent(QKeyEvent *event)
{
    // Try to keep menu open when triggering checkable actions
    const auto key = event->key();
    const auto action = activeAction();
    if (action && action->isEnabled() && action->isCheckable() //
        && (key == Qt::Key_Return || key == Qt::Key_Enter //
            || (key == Qt::Key_Space && style()->styleHint(QStyle::SH_Menu_SpaceActivatesItem, nullptr, this)))) {
        action->trigger();
        event->accept();
        return;
    }
    FotonMenu::keyPressEvent(event);
}

void OptionsMenu::mouseReleaseEvent(QMouseEvent *event)
{
    // Try to keep menu open when triggering checkable actions
    const auto action = activeAction() == actionAt(event->position().toPoint()) ? activeAction() : nullptr;
    if (action && action->isEnabled() && action->isCheckable()) {
        action->trigger();
        return;
    }
    FotonMenu::mouseReleaseEvent(event);
}

#include "moc_OptionsMenu.cpp"
