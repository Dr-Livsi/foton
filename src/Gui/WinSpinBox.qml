/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls as QQC
import org.kde.kirigami as Kirigami

/**
 * A spin box whose text is not cut off with the Windows style.
 * The desktop style keeps a padding of 6 pixels above and below the text,
 * but the spin boxes of the Windows style are lower than the ones of KDE's Breeze style.
 */
QQC.SpinBox {
    id: root

    topPadding: 0
    bottomPadding: 0
    implicitHeight: Math.max(Math.ceil(fontMetrics.height) + Kirigami.Units.smallSpacing * 2,
                             background && background.fullRectSizeHint ? background.fullRectSizeHint.height : 0)

    FontMetrics {
        id: fontMetrics
        font: root.font
    }
}
