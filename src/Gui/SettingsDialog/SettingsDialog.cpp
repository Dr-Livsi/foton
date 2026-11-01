/*
 *  SPDX-FileCopyrightText: 2025 Jhair Paris <dev@jhairparis.com>
 *  SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "SettingsDialog.h"

#include "FotonCore.h"
#include "GeneralOptionsPage.h"
#include "GlobalShortcuts.h"
#include "ImageSaveOptionsPage.h"
#include "Language.h"
#include "ShortcutsOptionsPage.h"
#include "settings.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QWindow>

#include <KLocalizedString>
#include <KMessageBox>

using namespace Qt::StringLiterals;

SettingsDialog::SettingsDialog(QWidget *parent)
    : KConfigDialog(parent, "settings"_L1, Settings::self())
    , m_generalPage(new GeneralOptionsPage(this))
    , m_imagesPage(new ImageSaveOptionsPage(this))
    , m_shortcutsPage(new ShortcutsOptionsPage(this))
{
    // Tabs, like the property dialogs of Windows.
    setFaceType(KPageDialog::Tabbed);
    addPage(m_generalPage, Settings::self(), i18nc("Settings category", "General"), "configure"_L1);
    addPage(m_imagesPage, Settings::self(), i18nc("Settings category", "Image Saving"), "image-x-generic"_L1);
    addPage(m_shortcutsPage, i18nc("Settings category", "Shortcuts"), "preferences-desktop-keyboard"_L1);
    connect(m_shortcutsPage, &ShortcutsOptionsPage::shortCutsChanged, this, [this] {
        updateButtons();
    });
    connect(m_generalPage, &GeneralOptionsPage::languageChanged, this, [this] {
        updateButtons();
    });
    connect(this, &KConfigDialog::currentPageChanged, this, &SettingsDialog::updateButtons);

    // There is no handbook for Foton.
    if (auto helpButton = buttonBox()->button(QDialogButtonBox::Help)) {
        helpButton->hide();
    }
}

SettingsDialog::~SettingsDialog()
{
    // Global shortcuts are suspended while the dialog exists so that they can be edited.
    GlobalShortcuts::instance()->setSuspended(false);
}

QSize SettingsDialog::sizeHint() const
{
    // Avoid having pages that need to be scrolled,
    // unless size is larger than available screen height.
    const auto headerSize = pageWidget()->pageHeader()->sizeHint();
    const auto footerSize = pageWidget()->pageFooter()->sizeHint();
    auto sh = m_generalPage->sizeHint();
    sh = sh.expandedTo(m_imagesPage->sizeHint());
    sh = sh.expandedTo(m_shortcutsPage->sizeHint());
    sh.rheight() += headerSize.height() + footerSize.height()
                 + style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) * 2;
    // The tab bar and the frame around the pages.
    sh.rheight() += style()->pixelMetric(QStyle::PM_TabBarBaseHeight) + fontMetrics().height() * 2;
    sh.rwidth() += style()->pixelMetric(QStyle::PM_DefaultFrameWidth) * 2 + style()->pixelMetric(QStyle::PM_LayoutLeftMargin) * 2;
    sh = KConfigDialog::sizeHint().expandedTo(sh);
    const auto screenHeight = screen() ? screen()->availableGeometry().height() : 0;
    sh.setHeight(std::min(sh.height(), screenHeight));
    return sh;
}

void SettingsDialog::showEvent(QShowEvent *event)
{
    auto parent = parentWidget();
    bool onTop = parent && parent->windowHandle()->flags().testFlag(Qt::WindowStaysOnTopHint);
    windowHandle()->setFlag(Qt::WindowStaysOnTopHint, onTop);

    // Pressing a key combination that is used as a global shortcut
    // must not take a screenshot while the shortcuts are being edited.
    GlobalShortcuts::instance()->setSuspended(true);

    KConfigDialog::showEvent(event);
}

bool SettingsDialog::hasChanged()
{
    return m_shortcutsPage->isModified() || m_generalPage->isLanguageChanged() || KConfigDialog::hasChanged();
}

bool SettingsDialog::isDefault()
{
    return m_shortcutsPage->isDefault() && m_generalPage->isLanguageDefault() && KConfigDialog::isDefault();
}

void SettingsDialog::updateSettings()
{
    KConfigDialog::updateSettings();
    m_shortcutsPage->saveChanges();
    FotonCore::instance()->applyBackgroundSettings();

    if (m_generalPage->isLanguageChanged()) {
        Language::setCurrent(m_generalPage->selectedLanguage());
        // Ask after the dialog has finished saving the settings.
        QMetaObject::invokeMethod(this, &SettingsDialog::askForRestart, Qt::QueuedConnection);
    }
}

void SettingsDialog::askForRestart()
{
    const auto result = KMessageBox::questionTwoActions(this,
                                                        i18nc("@info", "The new language will be used after Foton is restarted."),
                                                        i18nc("@title:window", "Language Changed"),
                                                        KGuiItem(i18nc("@action:button", "Restart Now"), u"view-refresh"_s),
                                                        KGuiItem(i18nc("@action:button", "Later"), u"dialog-cancel"_s));
    if (result == KMessageBox::PrimaryAction) {
        FotonCore::instance()->restart();
    }
}

void SettingsDialog::updateWidgets()
{
    KConfigDialog::updateWidgets();
    m_shortcutsPage->resetChanges();
    m_generalPage->loadLanguage();
}

void SettingsDialog::updateWidgetsDefault()
{
    KConfigDialog::updateWidgetsDefault();
    m_shortcutsPage->defaults();
    m_generalPage->setDefaultLanguage();
}

#include "moc_SettingsDialog.cpp"
