import QtQuick
import QtQuick.Controls

Item {
    id: textBox
    height: 38

    property alias text: input.text
    property alias placeholderText: placeholder.text
    property alias readOnly: input.readOnly
    property string icon: ""
    property bool showClearButton: true

    signal accepted()

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: FluTheme.radius
        color: input.activeFocus ? FluTheme.surfaceBg : FluTheme.surfaceHover
        border.color: input.activeFocus ? FluTheme.primaryColor : FluTheme.cardBorder
        border.width: input.activeFocus ? 1.5 : 1

        Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }
        Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }

        Row {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            spacing: 8

            // 左侧可选图标
            Text {
                visible: textBox.icon !== ""
                text: textBox.icon
                color: input.activeFocus ? FluTheme.primaryColor : FluTheme.textSecondary
                font.pixelSize: FluTheme.fontSizeBody
                font.family: FluTheme.fontBody
                anchors.verticalCenter: parent.verticalCenter
            }

            Item {
                width: parent.width - (textBox.icon !== "" ? 22 : 0) - (clearBtn.visible ? 28 : 0) - 16
                height: parent.height

                TextInput {
                    id: input
                    anchors.fill: parent
                    verticalAlignment: TextInput.AlignVCenter
                    color: FluTheme.textPrimary
                    font.pixelSize: FluTheme.fontSizeBody
                    font.family: FluTheme.fontMono
                    selectByMouse: true
                    selectionColor: FluTheme.primaryColor
                    selectedTextColor: "#FFFFFF"
                    clip: true

                    onAccepted: textBox.accepted()
                }

                Text {
                    id: placeholder
                    anchors.fill: parent
                    verticalAlignment: Text.AlignVCenter
                    text: "粘贴链接 / 输入 BV号，回车解析"
                    color: FluTheme.textDisabled
                    font.pixelSize: FluTheme.fontSizeBody
                    font.family: FluTheme.fontBody
                    visible: input.text === "" && !input.activeFocus
                }
            }

            // 清除按钮
            Rectangle {
                id: clearBtn
                width: 20
                height: 20
                radius: 10
                color: clearMa.containsMouse ? FluTheme.surfaceActive : "transparent"
                anchors.verticalCenter: parent.verticalCenter
                visible: textBox.showClearButton && input.text !== ""

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    font.pixelSize: 10
                    color: FluTheme.textSecondary
                }

                MouseArea {
                    id: clearMa
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        input.text = ""
                        input.forceActiveFocus()
                    }
                }
            }
        }
    }
}
