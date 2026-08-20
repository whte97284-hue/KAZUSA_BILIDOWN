import QtQuick

Rectangle {
    id: bar
    height: 6
    radius: 3
    color: FluTheme.surfaceActive
    clip: true

    property real value: 0.0 // 0.0 ~ 100.0
    property color progressColor: FluTheme.primaryColor

    Rectangle {
        id: fill
        height: parent.height
        radius: parent.radius
        width: Math.max(0, Math.min(parent.width, parent.width * (bar.value / 100.0)))
        color: bar.progressColor

        Behavior on width {
            NumberAnimation { duration: 180; easing.type: Easing.OutQuad }
        }

        // 前导微光泽
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 8
            radius: 3
            color: "#FFFFFF"
            opacity: 0.35
            visible: bar.value > 2 && bar.value < 99
        }
    }
}
