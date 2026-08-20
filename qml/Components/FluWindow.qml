import QtQuick
import QtQuick.Controls
import QtQuick.Window

Window {
    id: window
    color: FluTheme.windowBg
    visible: true

    default property alias content: contentArea.data

    Item {
        id: contentArea
        anchors.fill: parent
    }
}
