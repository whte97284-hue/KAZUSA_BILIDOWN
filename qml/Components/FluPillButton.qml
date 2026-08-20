import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: pillRoot

    property string text: ""
    property string iconName: ""
    property string toolTip: ""
    property bool isFilled: false
    property color themeColor: FluTheme.primaryColor
    property color customTextColor: "transparent"
    property color customBgColor: "transparent"
    property color customBorderColor: "transparent"

    signal clicked()

    implicitWidth: {
        if (text === "" && iconName !== "") return 30
        var w = btnText.implicitWidth + 22
        if (iconName !== "") w += 14
        return w
    }
    implicitHeight: 30
    radius: height / 2

    // 悬停功能说明 ToolTip (Apple 磨砂深墨色风格)
    ToolTip.visible: pillRoot.toolTip !== "" && pillMa.containsMouse
    ToolTip.text: pillRoot.toolTip
    ToolTip.delay: 350
    ToolTip.timeout: 4000
    ToolTip.toolTip.background: Rectangle {
        color: FluTheme.darkMode === 1 ? "#282828" : "#1F2328"
        radius: 6
        border.color: FluTheme.darkMode === 1 ? "#383838" : Qt.rgba(255, 255, 255, 0.08)
        border.width: 1
    }
    ToolTip.toolTip.contentItem: Text {
        text: pillRoot.toolTip
        font.pixelSize: 11
        font.family: FluTheme.fontBody
        color: "#F5F5F5"
    }

    // 状态背景计算 (统一柔和色调，拒绝突兀刺眼)
    color: {
        if (customBgColor !== "transparent" && customBgColor.a > 0) {
            return pillMa.pressed ? Qt.darker(customBgColor, 1.15) : (pillMa.containsMouse ? (FluTheme.darkMode === 1 ? Qt.lighter(customBgColor, 1.15) : Qt.darker(customBgColor, 1.05)) : customBgColor)
        }
        if (isFilled) {
            return pillMa.pressed ? Qt.darker(themeColor, 1.15) : themeColor
        }
        return pillMa.containsMouse ? (FluTheme.darkMode === 1 ? Qt.rgba(themeColor.r, themeColor.g, themeColor.b, 0.12) : Qt.rgba(themeColor.r, themeColor.g, themeColor.b, 0.07)) : (FluTheme.darkMode === 1 ? FluTheme.surfaceBg : FluTheme.surfaceBg)
    }

    // 状态边框计算
    border.color: {
        if (customBorderColor !== "transparent" && customBorderColor.a > 0) {
            return customBorderColor
        }
        if (isFilled) {
            return "transparent"
        }
        return pillMa.containsMouse ? Qt.rgba(themeColor.r, themeColor.g, themeColor.b, 0.40) : FluTheme.cardBorder
    }
    border.width: 1

    // 稳定触觉反馈 (按压 0.96 微回弹，悬停几何尺寸恒定不挤占邻近元素)
    scale: pillMa.pressed ? 0.96 : 1.0
    Behavior on scale { NumberAnimation { duration: FluTheme.durationFast; easing.type: FluTheme.easingStandard } }
    Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
    Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

    RowLayout {
        anchors.centerIn: parent
        spacing: 5

        BiliIcon {
            id: pIcon
            visible: pillRoot.iconName !== ""
            name: pillRoot.iconName
            width: 13
            height: 13
            color: {
                if (pillRoot.isFilled) return "#FFFFFF"
                return pillMa.containsMouse ? pillRoot.themeColor : FluTheme.textSecondary
            }
            Layout.alignment: Qt.AlignVCenter
            Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
        }

        Text {
            id: btnText
            visible: pillRoot.text !== ""
            text: pillRoot.text
            color: {
                if (customTextColor !== "transparent" && customTextColor.a > 0) {
                    return customTextColor
                }
                if (pillRoot.isFilled) {
                    return "#FFFFFF"
                }
                return pillMa.containsMouse ? pillRoot.themeColor : FluTheme.textPrimary
            }
            font.pixelSize: FluTheme.fontSizeCaption
            font.bold: pillRoot.isFilled
            font.family: FluTheme.fontBody
            Layout.alignment: Qt.AlignVCenter
            Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
        }
    }

    MouseArea {
        id: pillMa
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: pillRoot.clicked()
    }
}