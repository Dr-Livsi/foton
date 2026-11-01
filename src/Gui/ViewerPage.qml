/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls as QQC
import org.kde.kirigami as Kirigami
import foton.private
import org.kde.kquickimageeditor

/**
 * This page is shown when a screenshot has been taken
 * or accepted from the rectangular region capture mode.
 *
 * - There is a `contextWindow` context property that can be used to
 * access the instance of the ViewerWindow.
 */
EmptyPage {
    id: root
    focus: true

    // Used in ViewerWindow::setMode()
    readonly property real minimumWidth: Math.max(
        header.implicitWidth,
        annotationsToolBar.implicitWidth + separator.implicitWidth + footerLoader.implicitWidth,
        480 // leave some room for content if necessary
    )
    readonly property real minimumHeight: header.implicitHeight
        + Math.max(annotationsToolBar.implicitHeight,
                   footerLoader.implicitHeight)

    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    header: QQC.ToolBar {
        id: header
        contentItem: ButtonGrid {
            id: mainToolBarContents
            animationsEnabled: true
            AnimatedLoader {
                state: contextWindow.annotating ? "active" : "inactive"
                sourceComponent: UndoRedoGroup {
                    buttonHeight: QmlUtils.iconTextButtonHeight
                    spacing: mainToolBarContents.spacing
                }
            }
            TtToolButton {
                display: TtToolButton.IconOnly
                visible: action.enabled
                action: SaveAction {}
            }
            TtToolButton {
                display: TtToolButton.IconOnly
                action: SaveAsAction {}
            }
            TtToolButton {
                display: TtToolButton.IconOnly
                visible: action.enabled
                action: CopyImageAction {}
            }
            TtToolButton {
                visible: action.enabled
                action: RecognizeTextAction {}
                QQC.ToolTip.text: i18nc("@info:tooltip", "Recognize the text in the screenshot and show it in a window where it can be copied")
                QQC.ToolTip.visible: hovered
                QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
            TtToolButton {
                visible: action.enabled
                action: EditAction {}
            }
            QQC.ToolSeparator {
                height: QmlUtils.iconTextButtonHeight
            }
            ScreenshotModeMenuButton {}
            OptionsMenuButton {}
        }
    }

    // Needed for scrolling via keyboard input
    Keys.priority: Keys.AfterItem
    Keys.enabled: contentLoader.item !== null
    Keys.forwardTo: contentLoader.item

    InlineMessageList {
        id: inlineMessageList
        // parent is contentItem
        anchors.left: annotationsToolBar.right
        anchors.right: parent.right
        anchors.top: parent.top
    }

    QQC.Pane { // parent is contentItem
        id: annotationsToolBar
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.left
        visible: false
        leftPadding: header.leftPadding
        rightPadding: header.rightPadding
        topPadding: header.topPadding
        bottomPadding: header.bottomPadding
        contentItem: AnnotationsToolBarContents {
            id: annotationsToolBarContents
            displayMode: QQC.AbstractButton.IconOnly
            flow: Grid.TopToBottom
            showUndoRedo: false
            showNoneButton: true
        }
        background: Rectangle {
            color: parent.palette.window
        }
    }

    Kirigami.Separator { // parent is contentItem
        id: separator
        visible: false
        anchors.left: annotationsToolBar.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
    }

    Loader { // parent is contentItem
        id: contentLoader
        anchors {
            left: footerLoader.left
            right: parent.right
            top: inlineMessageList.bottom
            bottom: footerLoader.top
        }
        sourceComponent: ScreenshotView {}
    }

    Loader {
        id: footerLoader
        anchors.left: separator.right
        anchors.right: parent.right
        anchors.top: parent.bottom
        visible: false
        active: visible
        sourceComponent: QQC.ToolBar { // parent is contentItem
            position: QQC.ToolBar.Footer
            // Spin boxes can be higher than buttons, they are squeezed and cut off otherwise.
            contentHeight: Math.max(QmlUtils.iconTextButtonHeight, zoomEditor.implicitHeight)
            contentItem: RowLayout {
                spacing: Kirigami.Units.mediumSpacing
                AnimatedLoader {
                    id: loader
                    Layout.fillWidth: true
                    active: opacity > 0
                    visible: true
                    state: if (FotonCore.annotationDocument.tool.options !== AnnotationTool.NoOptions
                              || (FotonCore.annotationDocument.tool.type === AnnotationTool.SelectTool
                                 && FotonCore.annotationDocument.selectedItem.options !== AnnotationTool.NoOptions)
                    ) {
                        return "active"
                    } else {
                        return "inactive"
                    }
                    source: "AnnotationOptionsToolBarContents.qml"
                }
                QQC.ToolSeparator {
                    Layout.fillHeight: true
                    visible: loader.implicitWidth
                        + implicitWidth
                        + zoomLabel.implicitWidth
                        + zoomEditor.implicitWidth
                        + parent.spacing * 3 >= parent.width
                }
                QQC.Label {
                    id: zoomLabel
                    text: i18n("Zoom:")
                }
                WinSpinBox {
                    id: zoomEditor
                    from: contentLoader.item.minZoom * 100
                    to: contentLoader.item.maxZoom * 100
                    stepSize: 25
                    value: contentLoader.item.currentZoom * 100
                    textFromValue: (value, locale) => {
                        return Number(Math.round(value)).toLocaleString(locale, 'f', 0) + locale.percent
                    }
                    valueFromText: (text, locale) => {
                        return Number.fromLocaleString(locale, text.replace(/\D/g,''))
                    }
                    QQC.ToolTip.text: i18n("Image Zoom")
                    QQC.ToolTip.visible: hovered
                    QQC.ToolTip.delay: Kirigami.Units.toolTipDelay
                    Binding {
                        target: zoomEditor.contentItem
                        property: "horizontalAlignment"
                        value: Text.AlignRight
                        restoreMode: Binding.RestoreNone
                    }
                    TextContextMenuConnection {
                        target: zoomEditor.contentItem
                    }
                    onValueModified: contentLoader.item.zoomToPercent(Math.round(value) / 100)
                }
            }
        }
    }

    // Zooming while viewing the screenshot, the footer has the zoom while annotating.
    FloatingToolBar { // parent is contentItem
        id: zoomBar
        visible: !contextWindow.annotating && contentLoader.item !== null
        anchors.right: contentLoader.right
        anchors.bottom: contentLoader.bottom
        // Above the scroll bars of the zoomed in screenshot.
        anchors.rightMargin: Kirigami.Units.largeSpacing + (contentLoader.item ? contentLoader.item.rightPadding : 0)
        anchors.bottomMargin: Kirigami.Units.largeSpacing + (contentLoader.item ? contentLoader.item.bottomPadding : 0)
        padding: Kirigami.Units.smallSpacing
        contentItem: Row {
            spacing: Kirigami.Units.smallSpacing
            TtToolButton {
                display: TtToolButton.IconOnly
                icon.name: "zoom-out"
                text: i18nc("@action:button", "Zoom Out")
                enabled: contentLoader.item && contentLoader.item.currentZoom > contentLoader.item.minZoom
                onClicked: contentLoader.item.zoomOut()
            }
            QQC.Label {
                anchors.verticalCenter: parent.verticalCenter
                width: zoomMetrics.advanceWidth
                horizontalAlignment: Text.AlignHCenter
                text: contentLoader.item ? Number(Math.round(contentLoader.item.currentZoom * 100)).toLocaleString(Qt.locale(), 'f', 0) + Qt.locale().percent : ""
                TextMetrics {
                    id: zoomMetrics
                    text: "8888%"
                }
            }
            TtToolButton {
                display: TtToolButton.IconOnly
                icon.name: "zoom-in"
                text: i18nc("@action:button", "Zoom In")
                enabled: contentLoader.item && contentLoader.item.currentZoom < contentLoader.item.maxZoom
                onClicked: contentLoader.item.zoomIn()
            }
            TtToolButton {
                display: TtToolButton.IconOnly
                icon.name: "zoom-fit-best"
                text: i18nc("@action:button", "Fit to Window")
                enabled: contentLoader.item && contentLoader.item.viewerZoom > 0
                onClicked: contentLoader.item.zoomToFit()
            }
        }
    }

    Shortcut {
        enabled: contentLoader.item !== null
        sequences: [StandardKey.ZoomIn, "Ctrl+="]
        onActivated: contentLoader.item.zoomIn()
    }
    Shortcut {
        enabled: contentLoader.item !== null
        sequences: [StandardKey.ZoomOut]
        onActivated: contentLoader.item.zoomOut()
    }
    Shortcut {
        enabled: contentLoader.item !== null
        sequences: ["Ctrl+0"]
        onActivated: contentLoader.item.zoomToFit()
    }
    // FIXME: This shortcut only exists here because Foton interprets "Ctrl+Shift+,"
    // as "Ctrl+Shift+<" for some reason unless we use a QML Shortcut.
    Shortcut {
        sequences: [StandardKey.Preferences]
        onActivated: contextWindow.showPreferencesDialog()
    }

    state: "normal"
    states: [
        State {
            name: "annotating"
            when: contextWindow.annotating
                && contentLoader.item !== null
            AnchorChanges {
                target: annotationsToolBar
                anchors.left: parent.left
                anchors.right: undefined
            }
            AnchorChanges {
                target: footerLoader
                anchors.bottom: parent.bottom
                anchors.top: undefined
            }
        },
        State {
            name: "normal"
            when: !contextWindow.annotating
                || contentLoader.item === null
            AnchorChanges {
                target: annotationsToolBar
                anchors.left: undefined
                anchors.right: parent.left
            }
            AnchorChanges {
                target: footerLoader
                anchors.bottom: undefined
                anchors.top: parent.bottom
            }
        }
    ]
    transitions: [
        Transition {
            to: "annotating"
            SequentialAnimation {
                PropertyAction {
                    targets: [annotationsToolBar, separator, footerLoader]
                    property: "visible"
                    value: true
                }
                AnchorAnimation {
                    duration: Kirigami.Units.longDuration
                    easing.type: Easing.OutCubic
                }
            }
        },
        Transition {
            to: "normal"
            SequentialAnimation {
                AnchorAnimation {
                    duration: Kirigami.Units.longDuration
                    easing.type: Easing.OutCubic
                }
                PropertyAction {
                    targets: [annotationsToolBar, separator, footerLoader]
                    property: "visible"
                    value: false
                }
            }
        }
    ]
}
