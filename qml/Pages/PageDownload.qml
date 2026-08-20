import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Components"

Item {
    id: page
    Layout.fillWidth: true
    Layout.fillHeight: true

    property bool isCurrent: StackLayout.isCurrentItem
    opacity: isCurrent ? 1.0 : 0.0
    transform: Translate {
        y: page.isCurrent ? 0 : 6
        Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    }
    Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

    property int currentTab: 0 // 0=待下载, 1=下载中, 2=已下载

    function parseAction() {
        if (searchInput.text.trim() !== "") {
            biliController.parseVideo(searchInput.text.trim())
            searchInput.text = ""
        }
    }

    Connections {
        target: biliController
        function onParseFailed(error) {
            if (typeof globalToast !== "undefined" && globalToast) {
                globalToast.show("解析失败: " + error, FluTheme.danger)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        anchors.topMargin: 8
        spacing: 12

        // ================= 1. 顶部 Tab 栏 (动态滑动指示条) =================
        Item {
            Layout.fillWidth: true
            height: 30

            Row {
                id: tabRow
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                spacing: 24

                TabButton {
                    id: tBtn0
                    text: "待下载 (" + biliController.pendingCount + ")"
                    isActive: page.currentTab === 0
                    onClicked: page.currentTab = 0
                }

                TabButton {
                    id: tBtn1
                    text: "下载中 (" + biliController.downloadingCount + ")"
                    isActive: page.currentTab === 1
                    onClicked: page.currentTab = 1
                }

                TabButton {
                    id: tBtn2
                    text: "已下载 (" + biliController.completedCount + ")"
                    isActive: page.currentTab === 2
                    onClicked: page.currentTab = 2
                }
            }

            // 底部平滑滑动胶囊指示器
            Rectangle {
                id: tabIndicator
                height: 2
                radius: 1
                color: FluTheme.primaryColor
                anchors.bottom: parent.bottom

                x: {
                    if (page.currentTab === 0) return tBtn0.x
                    if (page.currentTab === 1) return tBtn1.x
                    return tBtn2.x
                }
                width: {
                    if (page.currentTab === 0) return tBtn0.width
                    if (page.currentTab === 1) return tBtn1.width
                    return tBtn2.width
                }

                Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            }
        }

        // ================= 2. 顶栏操作胶囊矩阵 (极致纯图标中性降噪 · 统一 30x30 / 紧凑路径) =================
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            // 1. 存储路径选择 (带路径文本的紧凑胶囊, 默认纯净中性)
            FluPillButton {
                iconName: "folder"
                text: biliController.downloadDir
                Layout.maximumWidth: 260
                implicitHeight: 30
                themeColor: FluTheme.primaryColor
                onClicked: biliController.chooseDownloadDir()
            }

            // 2. 自定义批量 (纯图标 30x30, 默认中性)
            FluPillButton {
                iconName: "list"
                isFilled: false
                implicitWidth: 30
                implicitHeight: 30
                themeColor: FluTheme.primaryColor
                onClicked: batchDialog.open()
            }

            // 3. 全部下载 (纯图标 30x30, 默认中性无红底, 悬停平滑点亮)
            FluPillButton {
                iconName: "play"
                isFilled: false
                implicitWidth: 30
                implicitHeight: 30
                themeColor: FluTheme.primaryColor
                onClicked: biliController.startDownloadAll()
            }

            // 4. 清空 (纯图标 30x30, 默认中性, 悬停柔和危险色)
            FluPillButton {
                iconName: "trash"
                isFilled: false
                implicitWidth: 30
                implicitHeight: 30
                themeColor: FluTheme.danger
                onClicked: biliController.clearTab(page.currentTab)
            }

            Item { Layout.fillWidth: true }
        }

        // ================= 3. 极简视频链接输入搜索栏 (36px 现代圆角胶囊) =================
        Rectangle {
            Layout.fillWidth: true
            height: 36
            radius: 18
            color: FluTheme.surfaceBg
            border.color: searchInput.activeFocus ? FluTheme.primaryColor : (FluTheme.darkMode === 1 ? FluTheme.cardBorder : Qt.rgba(0, 0, 0, 0.06))
            border.width: 1

            Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
            Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 7
                spacing: 8

                TextInput {
                    id: searchInput
                    Layout.fillWidth: true
                    verticalAlignment: TextInput.AlignVCenter
                    color: FluTheme.textPrimary
                    font.pixelSize: FluTheme.fontSizeBody
                    font.family: FluTheme.fontMono
                    selectByMouse: true
                    selectionColor: FluTheme.primaryColor
                    selectedTextColor: "#FFFFFF"
                    clip: true
                    text: ""

                    onAccepted: parseAction()
                }

                Rectangle {
                    width: 20
                    height: 20
                    radius: 10
                    color: clearMa.containsMouse ? FluTheme.surfaceHover : "transparent"
                    visible: searchInput.text !== ""
                    Layout.alignment: Qt.AlignVCenter

                    BiliIcon {
                        anchors.centerIn: parent
                        name: "clear"
                        color: FluTheme.textSecondary
                        width: 10
                        height: 10
                    }

                    MouseArea {
                        id: clearMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            searchInput.text = ""
                            searchInput.forceActiveFocus()
                        }
                    }
                }

                // 放大镜解析按钮 (24x24 紧凑内嵌圆形)
                Rectangle {
                    id: searchBtn
                    width: 24
                    height: 24
                    radius: 12
                    color: searchBtnMa.containsMouse ? FluTheme.surfaceHover : "transparent"
                    opacity: biliController.isParsing ? 0.5 : 1.0
                    Layout.alignment: Qt.AlignVCenter

                    BiliIcon {
                        anchors.centerIn: parent
                        name: "search"
                        width: 13
                        height: 13
                        color: searchBtnMa.containsMouse ? FluTheme.primaryColor : FluTheme.textSecondary
                        Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
                    }

                    MouseArea {
                        id: searchBtnMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            biliController.playClickSound() // 拍子木: 解析即跨页导航
                            parseAction()
                        }
                    }
                }
            }
        }

        // ================= 4. 三态动态任务卡片列表 =================
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: page.currentTab

            // Tab 0: 待下载列表
            ListView {
                clip: true
                spacing: 10
                model: biliController.pendingModel
                boundsBehavior: Flickable.StopAtBounds

                delegate: VideoTaskCard {
                    width: ListView.view.width
                    title: model.title
                    coverUrl: model.coverUrl
                    durationDesc: model.durationDesc
                    audioSizeText: model.audioSizeText
                    videoSizeText: model.videoSizeText
                    qualityOptions: model.qualityOptions
                    audioOptions: model.audioOptions

                    onDownloadClicked: biliController.startDownloadTask(index)
                    onDownloadAudioClicked: biliController.downloadAudioOnly(index)
                    onDownloadCoverClicked: biliController.downloadCoverOnly(index)
                    onDeleteClicked: biliController.removePendingTask(index)
                    onSelectionChanged: (qIdx, aIdx) => {
                        biliController.setTaskSelection(index, qIdx, aIdx)
                    }
                }

                Item {
                    visible: biliController.pendingCount === 0 && !biliController.isParsing
                    anchors.fill: parent
                }
            }

            // Tab 1: 下载中列表
            ListView {
                clip: true
                spacing: 10
                model: biliController.downloadingModel
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    width: ListView.view.width
                    height: 84
                    radius: 8
                    color: FluTheme.surfaceBg
                    border.color: FluTheme.darkMode === 1 ? FluTheme.cardBorder : Qt.rgba(0, 0, 0, 0.05)
                    border.width: 1

                    Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
                    Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 14

                        Rectangle {
                            Layout.preferredWidth: 114
                            Layout.preferredHeight: 64
                            radius: 6
                            clip: true
                            color: FluTheme.surfaceHover

                            Image {
                                anchors.fill: parent
                                source: model.coverUrl
                                sourceSize: Qt.size(228, 128)
                                fillMode: Image.PreserveAspectCrop
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 5

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: model.title
                                    color: FluTheme.textPrimary
                                    font.pixelSize: FluTheme.fontSizeTitle
                                    font.bold: true
                                    font.family: FluTheme.fontTitle
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: model.speedText
                                    color: FluTheme.warning
                                    font.pixelSize: FluTheme.fontSizeCaption
                                    font.family: FluTheme.fontBody
                                }
                                Text {
                                    text: Math.round(model.progress) + "%"
                                    color: FluTheme.primaryColor
                                    font.pixelSize: FluTheme.fontSizeBody
                                    font.bold: true
                                    font.family: FluTheme.fontMono
                                }
                            }

                            FluProgressBar {
                                Layout.fillWidth: true
                                height: 4
                                value: model.progress
                                progressColor: FluTheme.primaryColor
                            }

                            Text {
                                text: model.statusText
                                color: FluTheme.textSecondary
                                font.pixelSize: FluTheme.fontSizeCaption
                                font.family: FluTheme.fontBody
                            }
                        }
                    }
                }
            }

            // Tab 2: 已下载列表 (按参考图打造影视级优雅信息流 + 极简操作工具栏)
            ListView {
                clip: true
                spacing: 10
                model: biliController.completedModel
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    id: completedCard
                    width: ListView.view.width
                    height: 82
                    radius: 8
                    color: compCardMa.containsMouse ? FluTheme.surfaceHover : FluTheme.surfaceBg
                    border.color: compCardMa.containsMouse ? Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g, FluTheme.primaryColor.b, 0.35) : (FluTheme.darkMode === 1 ? FluTheme.cardBorder : Qt.rgba(0, 0, 0, 0.05))
                    border.width: 1

                    Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
                    Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

                    MouseArea {
                        id: compCardMa
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 14

                        // 1. 左侧 16:9 封面 (内嵌时长角标)
                        Rectangle {
                            Layout.preferredWidth: 114
                            Layout.preferredHeight: 62
                            radius: 6
                            clip: true
                            color: FluTheme.surfaceHover

                            Image {
                                anchors.fill: parent
                                source: model.coverUrl
                                sourceSize: Qt.size(228, 124)
                                fillMode: Image.PreserveAspectCrop
                                smooth: true
                            }

                            // 时长角标
                            Rectangle {
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 4
                                height: 16
                                width: compDurLabel.width + 6
                                radius: 3
                                color: "#D0000000"
                                visible: model.durationDesc !== ""

                                Text {
                                    id: compDurLabel
                                    anchors.centerIn: parent
                                    text: model.durationDesc || "00:00"
                                    color: "#FFFFFF"
                                    font.pixelSize: 10
                                    font.bold: true
                                    font.family: FluTheme.fontMono
                                }
                            }
                        }

                        // 2. 中间核心双行信息流 (标题 + UP主与规格徽章)
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 6

                            // 上行: 视频标题
                            Text {
                                Layout.fillWidth: true
                                text: model.title
                                color: FluTheme.textPrimary
                                font.pixelSize: FluTheme.fontSizeTitle
                                font.bold: true
                                font.family: FluTheme.fontTitle
                                elide: Text.ElideRight
                                maximumLineCount: 1
                            }

                            // 下行: UP 主胶囊徽章 + 大小与画质规格
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                // UP 极简微徽章
                                Rectangle {
                                    width: 22
                                    height: 15
                                    radius: 3
                                    color: FluTheme.darkMode === 1 ? Qt.rgba(255, 255, 255, 0.08) : Qt.rgba(0, 0, 0, 0.06)
                                    border.color: FluTheme.cardBorder
                                    border.width: 1
                                    Layout.alignment: Qt.AlignVCenter

                                    Text {
                                        anchors.centerIn: parent
                                        text: "UP"
                                        color: FluTheme.textSecondary
                                        font.pixelSize: 9
                                        font.bold: true
                                        font.family: FluTheme.fontTitle
                                    }
                                }

                                // UP 主名称
                                Text {
                                    text: (model.ownerName && model.ownerName !== "") ? model.ownerName : "哔哩哔哩"
                                    color: FluTheme.textSecondary
                                    font.pixelSize: FluTheme.fontSizeCaption
                                    font.family: FluTheme.fontBody
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: 180
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                Text {
                                    text: " · "
                                    color: FluTheme.textDisabled
                                    font.pixelSize: FluTheme.fontSizeCaption
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                // 文件大小或状态
                                Text {
                                    text: (model.videoSizeText && model.videoSizeText !== "-") ? model.videoSizeText : (model.statusText || "已完成")
                                    color: FluTheme.textSecondary
                                    font.pixelSize: FluTheme.fontSizeCaption
                                    font.family: FluTheme.fontMono
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }

                        // 3. 右侧工具栏与类型标签
                        ColumnLayout {
                            Layout.fillHeight: true
                            spacing: 4
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                            // 上行: 4 颗纯图标微药丸 (播放, 定位, 复制, 删除)
                            RowLayout {
                                Layout.alignment: Qt.AlignRight
                                spacing: 6

                                // 播放按钮 (30x30 纯图标)
                                FluPillButton {
                                    iconName: "play"
                                    isFilled: false
                                    implicitWidth: 30
                                    implicitHeight: 30
                                    toolTip: "播放视频"
                                    themeColor: FluTheme.primaryColor
                                    onClicked: biliController.openFile(model.filePath)
                                }

                                // 定位目录 (30x30 纯图标)
                                FluPillButton {
                                    iconName: "folder"
                                    isFilled: false
                                    implicitWidth: 30
                                    implicitHeight: 30
                                    toolTip: "打开所在目录"
                                    themeColor: FluTheme.primaryColor
                                    onClicked: biliController.openFolder(model.filePath)
                                }

                                // 复制链接 (30x30 纯图标)
                                FluPillButton {
                                    iconName: "link"
                                    isFilled: false
                                    implicitWidth: 30
                                    implicitHeight: 30
                                    toolTip: "复制视频链接"
                                    themeColor: FluTheme.primaryColor
                                    onClicked: {
                                        var url = model.bvid ? ("https://www.bilibili.com/video/" + model.bvid) : model.title
                                        biliController.copyToClipboard(url)
                                        if (typeof globalToast !== "undefined" && globalToast) {
                                            globalToast.show("已复制链接到剪贴板", FluTheme.success)
                                        }
                                    }
                                }

                                // 移出列表 (30x30 纯图标)
                                FluPillButton {
                                    iconName: "trash"
                                    isFilled: false
                                    implicitWidth: 30
                                    implicitHeight: 30
                                    toolTip: "移出下载列表"
                                    themeColor: FluTheme.danger
                                    onClicked: biliController.removeCompletedTask(index)
                                }
                            }

                            // 下行: 类型与媒体格式
                            Text {
                                text: "单视频 · [ 有声视频 ]"
                                color: FluTheme.textDisabled
                                font.pixelSize: 11
                                font.family: FluTheme.fontBody
                                Layout.alignment: Qt.AlignRight
                            }
                        }
                    }
                }
            }
        }
    }

    // 批量解析弹窗 (极简降噪)
    Dialog {
        id: batchDialog
        title: "批量解析"
        anchors.centerIn: parent
        width: 480
        height: 280
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        background: Rectangle {
            color: FluTheme.surfaceBg
            border.color: FluTheme.cardBorder
            radius: FluTheme.radius
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Text {
                text: "每行输入一个 BV 号或链接:"
                color: FluTheme.textPrimary
                font.pixelSize: FluTheme.fontSizeBody
                font.bold: true
                font.family: FluTheme.fontBody
            }

            TextArea {
                id: batchInput
                Layout.fillWidth: true
                Layout.fillHeight: true
                placeholderText: "BV1fK4y1t7hj\nBV1GJ411x7h7"
                placeholderTextColor: FluTheme.textDisabled
                font.pixelSize: FluTheme.fontSizeBody
                font.family: FluTheme.fontMono
                color: FluTheme.textPrimary
                background: Rectangle {
                    color: FluTheme.surfaceHover
                    border.color: FluTheme.cardBorder
                    radius: 5
                }
            }
        }

        onAccepted: {
            var lines = batchInput.text.split("\n")
            for (var i = 0; i < lines.length; i++) {
                if (lines[i].trim() !== "") {
                    biliController.parseVideo(lines[i].trim())
                }
            }
            batchInput.text = ""
        }
    }

    // Tab 组件 (继承设计尺寸)
    component TabButton: Item {
        id: tabRoot
        property string text: ""
        property bool isActive: false
        signal clicked()

        width: tabTxt.implicitWidth
        height: 28

        Text {
            id: tabTxt
            anchors.centerIn: parent
            text: tabRoot.text
            color: tabRoot.isActive ? FluTheme.primaryColor : FluTheme.textSecondary
            font.pixelSize: FluTheme.fontSizeTitle
            font.bold: tabRoot.isActive
            font.family: FluTheme.fontTitle

            Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: tabRoot.clicked()
        }
    }
}