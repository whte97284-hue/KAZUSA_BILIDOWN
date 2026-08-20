import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: fluToast
    width: Math.min(460, (parent ? parent.width : 500) - 40)
    height: 36
    radius: FluTheme.radius
    color: Qt.rgba(FluTheme.surfaceBg.r, FluTheme.surfaceBg.g, FluTheme.surfaceBg.b, 0.94)
    border.color: FluTheme.cardBorder
    border.width: 1
    z: 999

    property string message: ""
    property color dotColor: FluTheme.warning

    opacity: 0.0
    visible: opacity > 0.001
    y: opacity > 0 ? 0 : 8

    Behavior on opacity {
        NumberAnimation {
            duration: FluTheme.durationPopup
            easing.type: FluTheme.easingStandard
        }
    }

    Behavior on y {
        NumberAnimation {
            duration: FluTheme.durationPopup
            easing.type: FluTheme.easingStandard
        }
    }

    function show(text, customDotColor) {
        message = text
        if (customDotColor !== undefined) {
            dotColor = customDotColor
        } else {
            dotColor = FluTheme.warning
        }
        hideTimer.restart()
        opacity = 1.0
    }

    Timer {
        id: hideTimer
        interval: 3500
        repeat: false
        onTriggered: fluToast.opacity = 0.0
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 8

        // 6px 语义色小圆点
        Rectangle {
            width: 6
            height: 6
            radius: 3
            color: fluToast.dotColor
            Layout.alignment: Qt.AlignVCenter
        }

        // 提示文案
        Text {
            Layout.fillWidth: true
            text: fluToast.message
            color: FluTheme.textPrimary
            font.pixelSize: FluTheme.fontSizeBody
            font.family: FluTheme.fontBody
            elide: Text.ElideRight
            Layout.alignment: Qt.AlignVCenter
        }
    }
}