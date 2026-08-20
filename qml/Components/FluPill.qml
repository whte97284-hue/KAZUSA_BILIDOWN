import QtQuick

Rectangle {
    id: pill
    implicitWidth: contentRow.implicitWidth + 24
    implicitHeight: 28
    radius: height / 2
    border.width: selected ? 1.5 : 1
    border.color: selected ? FluTheme.primaryColor : (ma.containsMouse ? FluTheme.primaryColor : FluTheme.cardBorder)
    color: selected ? Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g, FluTheme.primaryColor.b, 0.12) : (ma.containsMouse ? FluTheme.surfaceHover : FluTheme.surfaceBg)

    property string text: ""
    property string subText: ""
    property bool selected: false
    property string badgeText: ""

    signal clicked()

    scale: ma.pressed ? FluTheme.scalePressed : (ma.containsMouse ? FluTheme.scaleHover : 1.0)
    Behavior on scale { NumberAnimation { duration: FluTheme.durationFast; easing.type: FluTheme.easingStandard } }
    Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
    Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

    Row {
        id: contentRow
        anchors.centerIn: parent
        spacing: 6

        // 选中圆点
        Rectangle {
            visible: pill.selected
            width: 6
            height: 6
            radius: 3
            color: FluTheme.primaryColor
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: pill.text
            color: pill.selected ? FluTheme.primaryColor : FluTheme.textPrimary
            font.pixelSize: FluTheme.fontSizeCaption
            font.bold: pill.selected
            font.family: FluTheme.fontBody
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            visible: pill.subText !== ""
            text: pill.subText
            color: FluTheme.textSecondary
            font.pixelSize: FluTheme.fontSizeCaption
            font.family: FluTheme.fontBody
            anchors.verticalCenter: parent.verticalCenter
        }

        // 小角标
        Rectangle {
            visible: pill.badgeText !== ""
            height: 16
            width: badgeLabel.width + 8
            radius: 4
            color: FluTheme.warning
            anchors.verticalCenter: parent.verticalCenter

            Text {
                id: badgeLabel
                anchors.centerIn: parent
                text: pill.badgeText
                color: "#FFFFFF"
                font.pixelSize: FluTheme.fontSizeCaption
                font.bold: true
                font.family: FluTheme.fontMono
            }
        }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: pill.clicked()
    }
}
