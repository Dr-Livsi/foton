/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

// Text recognition is only available on Windows.

#include "TextRecognition.h"

#include <KLocalizedString>

namespace TextRecognition
{
bool isSupported()
{
    return false;
}

QList<Language> availableLanguages()
{
    return {};
}

QString defaultLanguage()
{
    return {};
}

Result recognize(const QImage &, const QString &)
{
    Result result;
    result.error = i18nc("@info", "Text recognition is not available on this system.");
    return result;
}
}
