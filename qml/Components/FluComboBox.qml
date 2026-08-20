import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ComboBox {
    id: control
    implicitWidth: 148
    implicitHeight: 30

    // 禁用外层 scale 缩放以避免 Popup 相对原点抖动
    delegate: ItemDelegate {
        width: control.width - 8
        height: 28
        padding: 6
        anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined

        contentItem: RowLayout {
            spacing: 6
            anchors.fill: parent

            Text {
                Layout.fillWidth: true
                text: modelData
                color: (control.currentIndex === index || highlighted) ? FluTheme.primaryColor : FluTheme.textPrimary
                font.pixelSize: FluTheme.fontSizeCaption
                font.bold: control.currentIndex === index || highlighted
                font.family: FluTheme.fontBody
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }

            BiliIcon {
                name: "check"
                width: 9
                height: 9
                color: FluTheme.primaryColor
                visible: control.currentIndex === index
                Layout.alignment: Qt.AlignVCenter
            }
        }

        background: Rectangle {
            color: highlighted ? FluTheme.surfaceHover : "transparent"
            radius: 4
        }
    }

    // 平滑旋转箭头 (无 Canvas 重绘抖动)
    indicator: Item {
        id: arrowContainer
        x: control.width - width - 8
        y: (control.height - height) / 2
        width: 10
        height: 10

        // 旋转指示
        rotation: control.popup.visible ? 180 : 0
        Behavior on rotation {
            NumberAnimation {
                duration: 180
                easing.type: Easing.OutCubic
            }
        }

        Canvas {
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = control.hovered ? FluTheme.primaryColor : FluTheme.textSecondary
                ctx.lineWidth = 1.4
                ctx.beginPath()
                ctx.moveTo(2, 3.5)
                ctx.lineTo(5, 6.5)
                ctx.lineTo(8, 3.5)
                ctx.stroke()
            }

            Connections {
                target: control
                function onHoveredChanged() { arrowContainer.children[0].requestPaint() }
            }
            Connections {
                target: FluTheme
                function onDarkModeChanged() { arrowContainer.children[0].requestPaint() }
                function onPrimaryColorChanged() { arrowContainer.children[0].requestPaint() }
            }
        }
    }

    contentItem: Text {
        leftPadding: 10
        rightPadding: control.indicator.width + 12
        text: control.displayText
        font.pixelSize: FluTheme.fontSizeCaption
        font.family: FluTheme.fontBody
        color: FluTheme.textPrimary
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 30
        border.color: control.popup.visible ? FluTheme.primaryColor : (control.hovered ? FluTheme.primaryColor : FluTheme.cardBorder)
        border.width: 1
        radius: 6
        color: control.hovered ? FluTheme.surfaceHover : FluTheme.surfaceBg
        Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
        Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }
    }

    // 零抖动丝滑弹出层 (纯净垂直轻滑入 + 透明度过渡)
    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(220, contentItem.implicitHeight + 8)
        padding: 4

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 160; easing.type: Easing.OutCubic }
            NumberAnimation { property: "y"; from: control.height; to: control.height + 4; duration: 160; easing.type: Easing.OutCubic }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 100; easing.type: Easing.InQuad }
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator { }
        }

        background: Rectangle {
            border.color: FluTheme.cardBorder
            border.width: 1
            radius: 7
            color: FluTheme.surfaceBg

            // 下拉微阴影
            Rectangle {
                anchors.fill: parent
                anchors.topMargin: 2
                anchors.bottomMargin: -3
                radius: 8
                color: Qt.rgba(0, 0, 0, 0.06)
                z: -1
                visible: FluTheme.darkMode === 0
            }
        }
    }
}