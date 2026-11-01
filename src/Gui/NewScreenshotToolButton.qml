/* SPDX-FileCopyrightText: 2025 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import org.kde.kirigami as Kirigami
import foton.private

TtToolButton {
    // Can't rely on checked since clicking also toggles checked
    readonly property bool showCancel: FotonCore.captureTimeRemaining > 0
    readonly property real cancelWidth: QmlUtils.getButtonSize(display, cancelText(FotonCore.captureTimeRemaining / 1000), icon.name).width

    function cancelText(seconds) {
        return i18np("Cancel (%1 second)", "Cancel (%1 seconds)", Math.ceil(seconds))
    }

    checked: showCancel
    width: if (showCancel) {
        return cancelWidth
    } else {
        return display === TtToolButton.IconOnly ? height : implicitWidth
    }
    icon.name: showCancel ? "dialog-cancel" : "list-add"
    text: showCancel ?
        cancelText(FotonCore.captureTimeRemaining / 1000)
        : i18n("New Screenshot")
    onClicked: if (showCancel) {
        FotonCore.cancelScreenshot()
    } else {
        FotonCore.takeNewScreenshot()
    }
}
