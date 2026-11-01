/*
 *  SPDX-FileCopyrightText: 2025 Jhair Paris <dev@jhairparis.com>
 *  SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 *  SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "GeneralOptionsPage.h"

#include "Language.h"
#include "SettingsPageUtils.h"
#include "ui_GeneralOptions.h"

#include <KLocalizedString>

using namespace Qt::Literals::StringLiterals;

GeneralOptionsPage::GeneralOptionsPage(QWidget *parent)
    : QWidget(parent)
    , m_ui(new Ui_GeneralOptions)
{
    m_ui->setupUi(this);
    SettingsPageUtils::setUpFormLayouts(this);
    SettingsPageUtils::makeHint(m_ui->languageHint);

    for (const auto &option : Language::options()) {
        m_ui->languageComboBox->addItem(option.name, option.code);
    }
    loadLanguage();
    connect(m_ui->languageComboBox, &QComboBox::currentIndexChanged, this, &GeneralOptionsPage::languageChanged);

#ifndef Q_OS_WIN
    // Running in the background and starting automatically are only implemented for Windows.
    m_ui->kcfg_startWithWindows->setVisible(false);
    m_ui->kcfg_runInBackground->setVisible(false);
#endif
}

GeneralOptionsPage::~GeneralOptionsPage() = default;

QString GeneralOptionsPage::selectedLanguage() const
{
    return m_ui->languageComboBox->currentData().toString();
}

bool GeneralOptionsPage::isLanguageChanged() const
{
    return selectedLanguage() != Language::current();
}

bool GeneralOptionsPage::isLanguageDefault() const
{
    return selectedLanguage().isEmpty();
}

void GeneralOptionsPage::loadLanguage()
{
    selectLanguage(Language::current());
}

void GeneralOptionsPage::setDefaultLanguage()
{
    selectLanguage(QString());
}

void GeneralOptionsPage::selectLanguage(const QString &code)
{
    int index = m_ui->languageComboBox->findData(code);
    if (index < 0) {
        // A language without a translation of Foton, e.g. chosen with KDE's language dialog.
        m_ui->languageComboBox->addItem(Language::displayName(code), code);
        index = m_ui->languageComboBox->count() - 1;
    }
    m_ui->languageComboBox->setCurrentIndex(index);
}

#include "moc_GeneralOptionsPage.cpp"
