import QtQuick

Item {
    id: cardRoot
    property real radius: 10
    property alias color: innerCard.color
    property alias border: innerCard.border
    property bool hoverElevate: false
    property alias isHovered: ma.containsMouse

    // 1. 弥散环境柔和微阴影 (亮色模式下提供轻盈悬浮感，暗色模式下沉静融合)
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: (cardRoot.hoverElevate && ma.containsMouse) ? 2 : 1
        anchors.bottomMargin: (cardRoot.hoverElevate && ma.containsMouse) ? -4 : -2
        anchors.leftMargin: 0
        anchors.rightMargin: 0
        radius: cardRoot.radius + 1
        color: FluTheme.darkMode === 1 ? "transparent" : (ma.containsMouse ? Qt.rgba(0, 0, 0, 0.05) : Qt.rgba(0, 0, 0, 0.035))
        visible: FluTheme.darkMode === 0
        Behavior on color { ColorAnimation { duration: FluTheme.durationNormal } }
    }
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: (cardRoot.hoverElevate && ma.containsMouse) ? 4 : 2
        anchors.bottomMargin: (cardRoot.hoverElevate && ma.containsMouse) ? -6 : -4
        anchors.leftMargin: 1
        anchors.rightMargin: 1
        radius: cardRoot.radius + 2
        color: FluTheme.darkMode === 1 ? "transparent" : (ma.containsMouse ? Qt.rgba(0, 0, 0, 0.025) : Qt.rgba(0, 0, 0, 0.015))
        visible: FluTheme.darkMode === 0
        Behavior on color { ColorAnimation { duration: FluTheme.durationNormal } }
    }

    // 2. 主卡片表面
    Rectangle {
        id: innerCard
        anchors.fill: parent
        radius: cardRoot.radius
        color: FluTheme.surfaceBg
        border.color: FluTheme.darkMode === 1 ? FluTheme.cardBorder : (cardRoot.hoverElevate && ma.containsMouse ? Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g, FluTheme.primaryColor.b, 0.28) : FluTheme.cardBorder)
        border.width: 1

        y: 0
        Behavior on color { ColorAnimation { duration: FluTheme.durationNormal } }
        Behavior on border.color { ColorAnimation { duration: FluTheme.durationNormal } }
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: cardRoot.hoverElevate
        acceptedButtons: Qt.NoButton
    }
}
