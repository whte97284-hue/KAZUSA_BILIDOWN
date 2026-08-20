import QtQuick
import QtQuick.Controls

Item {
    id: titleBar
    height: 32

    property var targetWindow: null

    // 顶部拖拽移动区域 (双击最大化/还原，按住拖动窗口)
    MouseArea {
        anchors.fill: parent
        anchors.rightMargin: winBtnRow.width
        acceptedButtons: Qt.LeftButton

        onPressed: {
            if (titleBar.targetWindow) {
                titleBar.targetWindow.startSystemMove()
            }
        }

        onDoubleClicked: {
            if (titleBar.targetWindow) {
                if (titleBar.targetWindow.visibility === Window.Maximized) {
                    titleBar.targetWindow.showNormal()
                } else {
                    titleBar.targetWindow.showMaximized()
                }
            }
        }
    }

    // 右上角极简三键 (完全对齐参考截图，纯净无额外噪点)
    Row {
        id: winBtnRow
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height
        spacing: 0

        // 最小化
        TitleBtn {
            iconType: "min"
            onClicked: {
                if (titleBar.targetWindow) titleBar.targetWindow.showMinimized()
            }
        }

        // 最大化 / 还原
        TitleBtn {
            iconType: titleBar.targetWindow && titleBar.targetWindow.visibility === Window.Maximized ? "restore" : "max"
            onClicked: {
                if (titleBar.targetWindow) {
                    if (titleBar.targetWindow.visibility === Window.Maximized) {
                        titleBar.targetWindow.showNormal()
                    } else {
                        titleBar.targetWindow.showMaximized()
                    }
                }
            }
        }

        // 关闭
        TitleBtn {
            iconType: "close"
            isClose: true
            onClicked: {
                if (titleBar.targetWindow) titleBar.targetWindow.close()
            }
        }
    }

    // 极简线框控制按钮组件
    component TitleBtn: Rectangle {
        id: btn
        property string iconType: "min"
        property bool isClose: false
        signal clicked()

        width: 42
        height: titleBar.height
        color: btnMa.pressed ? (isClose ? Qt.darker(FluTheme.evaRed, 1.2) : FluTheme.surfaceActive) : (btnMa.containsMouse ? (isClose ? FluTheme.evaRed : FluTheme.surfaceHover) : "transparent")

        Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }

        Canvas {
            id: btnCanvas
            anchors.centerIn: parent
            width: 10
            height: 10
            contextType: "2d"

            Connections {
                target: btnMa
                function onContainsMouseChanged() { btnCanvas.requestPaint() }
            }

            Connections {
                target: FluTheme
                function onDarkModeChanged() { btnCanvas.requestPaint() }
            }

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = btn.isClose && btnMa.containsMouse ? "#FFFFFF" : FluTheme.textSecondary
                ctx.lineWidth = 1.0

                if (btn.iconType === "min") {
                    ctx.beginPath()
                    ctx.moveTo(0, 5)
                    ctx.lineTo(10, 5)
                    ctx.stroke()
                } else if (btn.iconType === "max") {
                    ctx.strokeRect(0.5, 0.5, 9, 9)
                } else if (btn.iconType === "restore") {
                    ctx.strokeRect(2.5, 0.5, 7, 7)
                    ctx.strokeRect(0.5, 2.5, 7, 7)
                } else if (btn.iconType === "close") {
                    ctx.beginPath()
                    ctx.moveTo(1, 1)
                    ctx.lineTo(9, 9)
                    ctx.moveTo(9, 1)
                    ctx.lineTo(1, 9)
                    ctx.stroke()
                }
            }
        }

        property string toolTip: {
            if (btn.iconType === "min") return "最小化"
            if (btn.iconType === "max") return "最大化"
            if (btn.iconType === "restore") return "还原窗口"
            if (btn.iconType === "close") return "关闭"
            return ""
        }

        MouseArea {
            id: btnMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.ArrowCursor
            onClicked: btn.clicked()

            ToolTip.visible: containsMouse
            ToolTip.text: btn.toolTip
            ToolTip.delay: 450
        }
    }
}
