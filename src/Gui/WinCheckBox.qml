/* This file is part of Foton, a screenshot utility based on KDE Spectacle
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Shapes
import org.kde.kirigami as Kirigami

/**
 * A check box that looks like the check boxes of Windows 10.
 * The indicator is drawn here, because the native one from the desktop style
 * is cut off on Windows with display scaling.
 */
QQC.CheckBox {
    id: root

    indicator: Rectangle {
        // The desktop style uses these for the space between the indicator and the text.
        function pixelMetric(name) {
            if (name === "indicatorwidth") {
                return width
            } else if (name === "checkboxlabelspacing") {
                return Kirigami.Units.smallSpacing
            }
            return 0
        }

        implicitWidth: Math.round(Kirigami.Units.gridUnit * 0.75)
        implicitHeight: implicitWidth
        x: root.mirrored ? root.width - width - root.rightPadding : root.leftPadding
        y: root.topPadding + Math.round((root.availableHeight - height) / 2)
        color: !root.enabled ? root.palette.window : root.down ? "#cce4f7" : "#ffffff"
        border.width: 1
        border.color: !root.enabled ? "#bfbfbf"
            : root.hovered || root.visualFocus ? "#0078d7" : "#333333"

        Shape {
            id: checkMark
            anchors.fill: parent
            anchors.margins: Math.round(parent.width * 0.2)
            visible: root.checkState !== Qt.Unchecked
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: root.enabled ? "#333333" : "#bfbfbf"
                strokeWidth: Math.max(1, Kirigami.Units.gridUnit / 12)
                fillColor: "transparent"
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                startX: 0
                startY: checkMark.height * 0.5
                PathLine { x: checkMark.width * 0.38; y: checkMark.height * 0.88 }
                PathLine { x: checkMark.width; y: checkMark.height * 0.1 }
            }
        }
    }
}
