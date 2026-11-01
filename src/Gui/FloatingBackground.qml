/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import org.kde.kirigami as Kirigami

// Square corners and a soft shadow, like the menus and flyouts of Windows 10.
Kirigami.ShadowedRectangle {
    radius: 0
    shadow.color: Qt.rgba(0,0,0,0.2)
    shadow.size: 8
    shadow.yOffset: 2
}
