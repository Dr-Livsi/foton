/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC
import org.kde.kirigami as Kirigami
import foton.private

ColumnLayout {
    spacing: Kirigami.Units.mediumSpacing
    WinCheckBox {
        Layout.fillWidth: true
        text: i18n("Include mouse pointer")
        QQC.ToolTip.text: i18n("Show the mouse cursor in the screenshot image.")
        QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC.ToolTip.visible: hovered
        checked: Settings.includePointer
        onToggled: Settings.includePointer = checked
    }
    WinCheckBox {
        Layout.fillWidth: true
        text: i18n("Include window titlebar and borders")
        QQC.ToolTip.text: i18n("Show the window title bar and border when taking a screenshot of a window.")
        QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC.ToolTip.visible: hovered
        checked: Settings.includeDecorations
        onToggled: Settings.includeDecorations = checked
    }
    WinCheckBox {
        Layout.fillWidth: true
        text: i18n("Include window shadow")
        QQC.ToolTip.text: i18n("Show the window shadow when taking a screenshot of a window.")
        QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC.ToolTip.visible: hovered
        enabled: Settings.includeDecorations
        checked: Settings.includeShadow
        onToggled: Settings.includeShadow = checked
    }
    WinCheckBox {
        Layout.fillWidth: true
        text: i18n("Quit after manual Save or Copy")
        QQC.ToolTip.text: i18n("Quit Foton after manually saving or copying the image.")
        QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC.ToolTip.visible: hovered
        checked: Settings.quitAfterSaveCopyExport
        onToggled: Settings.quitAfterSaveCopyExport = checked
    }
}
