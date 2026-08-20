import QtQuick

Rectangle {
    id: btn
    implicitWidth: contentRow.implicitWidth + 24
    implicitHeight: 32
    radius: FluTheme.radius
    color: ma.pressed ? FluTheme.surfaceActive : (ma.containsMouse ? FluTheme.surfaceHover : FluTheme.surfaceBg)
    border.color: ma.containsMouse ? FluTheme.primaryColor : FluTheme.cardBorder
    border.width: 1

    property string text: "按钮"
    property string icon: ""

    signal clicked()

    scale: ma.pressed ? FluTheme.scalePressed : (ma.containsMouse ? FluTheme.scaleHover : 1.0)
    Behavior on scale { NumberAnimation { duration: FluTheme.durationFast; easing.type: FluTheme.easingStandard } }
    Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
    Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

    Row {
        id: contentRow
        anchors.centerIn: parent
        spacing: 6

        Text {
            visible: btn.icon !== ""
            text: btn.icon
            color: FluTheme.textPrimary
            font.pixelSize: FluTheme.fontSizeBody
            font.family: FluTheme.fontBody
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            id: btnText
            text: btn.text
            color: FluTheme.textPrimary
            font.pixelSize: FluTheme.fontSizeBody
            font.bold: true
            font.family: FluTheme.fontBody
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: btn.enabled
        cursorShape: btn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (btn.enabled) btn.clicked()
        }
    }
}
