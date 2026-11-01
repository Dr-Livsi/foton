/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QList>
#include <QString>

/**
 * The language of the user interface.
 *
 * The choice is stored where KDE's "Switch Application Language" dialog stores it,
 * KXmlGui reads it while the application starts and sets the LANGUAGE
 * environment variable, which KI18n uses. So a change takes effect after a restart.
 */
namespace Language
{
struct Option {
    // An empty code means the language of the system.
    QString code;
    QString name;
};

// The languages that Foton has translations for, the system language first.
QList<Option> options();

// The name of the language in the language itself, e.g. "Deutsch".
QString displayName(const QString &code);

QString current();
void setCurrent(const QString &code);
}
