import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: card
    width: parent ? parent.width : 600
    height: 94
    radius: 10
    color: cardMa.containsMouse ? FluTheme.surfaceHover : FluTheme.surfaceBg
    border.color: cardMa.containsMouse ? Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g, FluTheme.primaryColor.b, 0.45) : FluTheme.cardBorder
    border.width: 1

    // 保持静态几何尺寸，绝对不缩放跳动
    scale: 1.0
    Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
    Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

    property string title: ""
    property string coverUrl: ""
    property string durationDesc: "00:00"
    property string audioSizeText: "18.67 MB"
    property string videoSizeText: "41.33 MB"
    property var qualityOptions: ["1080P 高清 | hvc1", "1080P 60帧 | avc1", "720P 高清 | avc1", "360P 流畅 | hvc1"]
    property var audioOptions: ["192K | mp4a", "128K | mp4a", "64K | mp4a"]
    property int selectedQualityIndex: 0
    property int selectedAudioIndex: 0

    signal downloadClicked()
    signal downloadAudioClicked()
    signal downloadCoverClicked()
    signal deleteClicked()
    signal selectionChanged(int qualityIdx, int audioIdx)

    MouseArea {
        id: cardMa
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 14

        // 左侧 16:9 封面
        Rectangle {
            Layout.preferredWidth: 136
            Layout.preferredHeight: 74
            radius: 6
            color: FluTheme.surfaceHover
            border.color: FluTheme.cardBorder
            border.width: 1
            clip: true

            Image {
                anchors.fill: parent
                source: card.coverUrl
                // 2x 展示尺寸解码, 控制封面纹理内存
                sourceSize: Qt.size(272, 148)
                fillMode: Image.PreserveAspectCrop
                smooth: true
            }

            // 时长角标
            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 4
                height: 18
                width: durLabel.width + 8
                radius: 3
                color: "#E0000000"

                Text {
                    id: durLabel
                    anchors.centerIn: parent
                    text: card.durationDesc
                    color: "#FFFFFF"
                    font.pixelSize: FluTheme.fontSizeCaption
                    font.bold: true
                    font.family: FluTheme.fontMono
                }
            }
        }

        // 右侧核心信息 (零文字噪音)
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            // 第 1 行: 视频标题 + 统一胶囊按钮
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: card.title
                    color: FluTheme.textPrimary
                    font.pixelSize: FluTheme.fontSizeTitle
                    font.bold: true
                    font.family: FluTheme.fontTitle
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                // [ 封面 ] (极简相框图标 30x30)
                FluPillButton {
                    iconName: "image"
                    isFilled: false
                    implicitWidth: 30
                    implicitHeight: 30
                    themeColor: FluTheme.primaryColor
                    onClicked: card.downloadCoverClicked()
                }

                // [ 音频 ] (极简音符图标 30x30)
                FluPillButton {
                    iconName: "music"
                    isFilled: false
                    implicitWidth: 30
                    implicitHeight: 30
                    themeColor: FluTheme.primaryColor
                    onClicked: card.downloadAudioClicked()
                }

                // [ 下载 ] (实心下载图标 30x30)
                FluPillButton {
                    iconName: "download"
                    isFilled: true
                    implicitWidth: 30
                    implicitHeight: 30
                    themeColor: FluTheme.primaryColor
                    onClicked: card.downloadClicked()
                }

                // [ 删除 ] (极简垃圾桶图标 30x30)
                FluPillButton {
                    iconName: "trash"
                    isFilled: false
                    implicitWidth: 30
                    implicitHeight: 30
                    themeColor: FluTheme.danger
                    onClicked: card.deleteClicked()
                }
            }

            // 第 2 行: 媒体大小 + 音质/画质纯下拉
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Text {
                    text: card.audioSizeText + "  ·  " + card.videoSizeText
                    color: FluTheme.textSecondary
                    font.pixelSize: FluTheme.fontSizeCaption
                    font.family: FluTheme.fontBody
                }

                Item { Layout.fillWidth: true }

                FluComboBox {
                    implicitWidth: 120
                    implicitHeight: 28
                    model: card.audioOptions
                    currentIndex: card.selectedAudioIndex
                    onActivated: (index) => {
                        card.selectedAudioIndex = index
                        card.selectionChanged(card.selectedQualityIndex, index)
                    }
                }

                FluComboBox {
                    implicitWidth: 160
                    implicitHeight: 28
                    model: card.qualityOptions
                    currentIndex: card.selectedQualityIndex
                    onActivated: (index) => {
                        card.selectedQualityIndex = index
                        card.selectionChanged(index, card.selectedAudioIndex)
                    }
                }
            }
        }
    }
}
