import QtQuick
import QtQuick.Controls

Item {
    id: dialog
    anchors.fill: parent
    visible: opacity > 0.0
    opacity: 0.0
    z: 999

    property string title: ""
    property int dialogWidth: 280
    property int dialogHeight: 290
    default property alias content: bodyArea.data

    signal closed()

    function open() {
        dialog.opacity = 1.0
    }

    function close() {
        dialog.opacity = 0.0
        dialog.closed()
    }

    Behavior on opacity { NumberAnimation { duration: FluTheme.durationPopup; easing.type: FluTheme.easingStandard } }

    // 遮罩蒙层 (半透明沉浸曜石黑)
    Rectangle {
        anchors.fill: parent
        color: "#80000000"
        MouseArea {
            anchors.fill: parent
            onClicked: dialog.close()
        }
    }

    // 居中对话框卡片 (高级纯净表面 + 自然微弹性动效)
    Rectangle {
        id: dialogCard
        width: dialog.dialogWidth
        height: dialog.dialogHeight
        radius: FluTheme.radius
        color: FluTheme.surfaceBg
        border.color: FluTheme.cardBorder
        border.width: 1
        anchors.centerIn: parent
        transformOrigin: Item.Center

        scale: dialog.opacity > 0.1 ? 1.0 : 0.92
        Behavior on scale { NumberAnimation { duration: FluTheme.durationPopup; easing.type: FluTheme.easingStandard } }
        Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
        Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

        // 顶部操作区 (若无 title 则只保留右上角极简关闭按钮)
        Item {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: dialog.title !== "" ? 40 : 28

            Text {
                visible: dialog.title !== ""
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: dialog.title
                color: FluTheme.textPrimary
                font.pixelSize: FluTheme.fontSizeTitle
                font.bold: true
                font.family: FluTheme.fontTitle
            }

            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.top: parent.top
                anchors.topMargin: 8
                width: 22
                height: 22
                radius: 11
                color: closeMa.containsMouse ? FluTheme.surfaceHover : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    font.pixelSize: 10
                    color: closeMa.containsMouse ? FluTheme.danger : FluTheme.textSecondary
                }

                MouseArea {
                    id: closeMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: dialog.close()
                }
            }
        }

        Item {
            id: bodyArea
            anchors.top: parent.top
            anchors.topMargin: dialog.title !== "" ? 44 : 28
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
        }
    }
}
