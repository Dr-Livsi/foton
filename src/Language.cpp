/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "Language.h"

#include <KLanguageName>
#include <KLocalizedString>

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

// The same file and keys as in kswitchlanguagedialog_p.cpp of KXmlGui.
static std::unique_ptr<QSettings> languageSettings()
{
    const QString configPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QDir().mkpath(configPath);
    auto settings = std::make_unique<QSettings>(configPath + u"/klanguageoverridesrc"_s, QSettings::IniFormat);
    settings->beginGroup(u"Language"_s);
    return settings;
}

namespace Language
{
QString displayName(const QString &code)
{
    if (code == u"en_US") {
        return u"English"_s;
    }
    QString name;
    const QLocale locale(code);
    // Qt knows the names of the languages in the languages themselves, but not variants like "sr@latin".
    if (!code.contains(u'@') && locale.language() != QLocale::C) {
        // Without the territory, e.g. "English" instead of "British English" for en_GB.
        const auto language = code.section(u'_', 0, 0);
        if (language == u"en") {
            name = u"English"_s;
        } else if (language == u"es") {
            name = u"Español"_s;
        } else {
            name = QLocale(language).nativeLanguageName();
        }
        if (code.contains(u'_')) {
            name += u" ("_s + locale.nativeTerritoryName() + u')';
        }
    } else {
        name = KLanguageName::nameForCodeInLocale(code, code);
    }
    if (name.isEmpty()) {
        return code;
    }
    // Language names are lower case in some languages, e.g. "русский".
    return name.at(0).toUpper() + name.mid(1);
}

QList<Option> options()
{
    QStringList codes = KLocalizedString::availableApplicationTranslations().values();
    // English is the language of the source code, so there is no translation for it.
    codes.removeAll(u"en"_s);
    codes.append(u"en_US"_s);
    codes.removeDuplicates();

    QList<Option> result;
    result.reserve(codes.size() + 1);
    for (const auto &code : std::as_const(codes)) {
        result.append({code, displayName(code)});
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(result.begin(), result.end(), [&collator](const Option &a, const Option &b) {
        return collator.compare(a.name, b.name) < 0;
    });
    result.prepend({QString(), i18nc("@item:inlistbox interface language", "System language")});
    return result;
}

QString current()
{
    // KDE's "Switch Application Language" dialog stores a list of languages, e.g. "ru:en_US".
    const auto languages = languageSettings()->value(QCoreApplication::applicationName()).toString();
    return languages.section(u':', 0, 0);
}

void setCurrent(const QString &code)
{
    auto settings = languageSettings();
    if (code.isEmpty()) {
        settings->remove(QCoreApplication::applicationName());
    } else {
        settings->setValue(QCoreApplication::applicationName(), code);
    }
}
}
