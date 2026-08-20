import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Item {
    id: control
    property string source: ""
    property real size: 64
    property color maskColor: FluTheme.windowBg
    
    width: size
    height: size

    // 1. 底层图片
    Image {
        id: avatarImg
        anchors.fill: parent
        source: control.source
        fillMode: Image.PreserveAspectCrop
        smooth: true
        mipmap: true
        visible: false
    }

    // 2. 完美的圆角遮罩源
    Item {
        id: maskSourceItem
        width: control.size
        height: control.size
        layer.enabled: true
        visible: false

        Rectangle {
            anchors.fill: parent
            radius: control.size / 2
            color: "#FFFFFF"
            antialiasing: true
        }
    }

    // 3. Qt 6 官方高性能硬件加速遮罩
    MultiEffect {
        anchors.fill: parent
        source: avatarImg
        maskEnabled: true
        maskSource: maskSourceItem
        visible: control.source !== ""
    }

    // 4. 缺省用户图标
    BiliIcon {
        anchors.centerIn: parent
        name: "user"
        color: "#888888"
        width: size * 0.45
        height: size * 0.45
        visible: control.source === ""
    }

    // 5. 极简浅灰细线外框，增添精致质感
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Qt.rgba(0.5, 0.5, 0.5, 0.18)
        border.width: 1
        radius: size / 2
        antialiasing: true
    }
}

