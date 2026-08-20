import QtQuick

Rectangle {
    id: badge
    height: 20
    width: label.width + 12
    radius: 4
    color: FluTheme.primaryColor

    property alias text: label.text
    property alias textColor: label.color

    Text {
        id: label
        anchors.centerIn: parent
        text: "4K"
        color: "#FFFFFF"
        font.pixelSize: FluTheme.fontSizeCaption
        font.bold: true
        font.family: FluTheme.fontMono
    }
}
