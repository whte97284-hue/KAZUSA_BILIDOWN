import QtQuick

Rectangle {
    id: btn
    height: 36
    radius: FluTheme.radius
    color: !enabled ? FluTheme.cardBorder : (ma.pressed ? Qt.darker(FluTheme.primaryColor, 1.15) : (ma.containsMouse ? Qt.lighter(FluTheme.primaryColor, 1.1) : FluTheme.primaryColor))

    property string text: "搜索"
    property string icon: ""
    property bool loading: false

    signal clicked()

    scale: ma.pressed ? FluTheme.scalePressed : (ma.containsMouse ? FluTheme.scaleHover : 1.0)
    Behavior on scale { NumberAnimation { duration: FluTheme.durationFast; easing.type: FluTheme.easingStandard } }
    Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }

    Row {
        anchors.centerIn: parent
        spacing: 6

        // 加载动画指示器
        FluProgressRing {
            visible: btn.loading
            width: 16
            height: 16
            color: "#FFFFFF"
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            visible: btn.icon !== "" && !btn.loading
            text: btn.icon
            color: "#FFFFFF"
            font.pixelSize: FluTheme.fontSizeBody
            font.family: FluTheme.fontBody
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: btn.text
            color: "#FFFFFF"
            font.pixelSize: FluTheme.fontSizeBody
            font.bold: true
            font.family: FluTheme.fontBody
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: btn.enabled && !btn.loading
        cursorShape: btn.enabled && !btn.loading ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (btn.enabled && !btn.loading) {
                btn.clicked()
            }
        }
    }
}
