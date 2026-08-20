import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Components"

Item {
    Layout.fillWidth: true
    Layout.fillHeight: true

    FluCard {
        anchors.centerIn: parent
        width: 380
        height: 160

        Column {
            anchors.centerIn: parent
            spacing: 10

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "批量任务"
                color: FluTheme.textPrimary
                font.pixelSize: FluTheme.fontSizeTitle
                font.bold: true
                font.family: FluTheme.fontTitle
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "支持分 P / 剧集与链接列表全量解析下载"
                color: FluTheme.textSecondary
                font.pixelSize: FluTheme.fontSizeBody
                font.family: FluTheme.fontBody
            }
        }
    }
}
