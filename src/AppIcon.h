/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QIcon>

/// The application icon, embedded in the executable.
inline QIcon fotonIcon()
{
    static const QIcon icon = [] {
        QIcon icon;
        for (int size : {16, 20, 24, 32, 40, 48, 64, 128, 256}) {
            icon.addFile(QStringLiteral(":/icons/foton-%1.png").arg(size), QSize(size, size));
        }
        return icon;
    }();
    return icon;
}
