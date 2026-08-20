import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Components"

Item {
    id: pageHistory
    Layout.fillWidth: true
    Layout.fillHeight: true

    property bool isCurrent: StackLayout.isCurrentItem
    opacity: isCurrent ? 1.0 : 0.0
    transform: Translate {
        y: pageHistory.isCurrent ? 0 : 6
        Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
    }
    Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

    property int selectedRow: -1
    property int hoveredRow: -1
    property bool isDetailActive: false

    // 解析本地/网络封面统一格式 (规避 relative path 与 403 限制)
    function resolveCover(local, remote) {
        if (local && local.length > 0) {
            var p = local.replace(/\\/g, "/");
            if (p.startsWith("file://") || p.startsWith("http://") || p.startsWith("https://")) {
                return p;
            }
            if (p.startsWith("./")) p = p.substring(2);
            if (p.length >= 2 && p[1] === ':') {
                return "file:///" + p;
            }
            if (p.startsWith("/")) {
                return "file://" + p;
            }
            return "file:///" + p;
        }
        if (remote && remote.length > 0) {
            return remote.startsWith("//") ? ("https:" + remote) : remote;
        }
        return "";
    }

    // 详情卡悬停交互调度 (使用非侵入式判定，子控件点击/悬停绝不隐藏)
    Timer {
        id: hideDetailTimer
        interval: 320
        repeat: false
        onTriggered: {
            if (!detailHoverHandler.hovered && pageHistory.hoveredRow < 0) {
                pageHistory.isDetailActive = false
            }
        }
    }

    function showDetail(index) {
        hideDetailTimer.stop()
        pageHistory.hoveredRow = index
        pageHistory.selectedRow = index
        pageHistory.isDetailActive = true
    }

    function scheduleHideDetail() {
        pageHistory.hoveredRow = -1
        hideDetailTimer.restart()
    }

    // 当历史记录发生变化时自动矫正选中的 row
    Connections {
        target: biliController
        function onHistoryChanged() {
            if (biliController.historyCount === 0) {
                pageHistory.selectedRow = -1
                pageHistory.isDetailActive = false
            } else if (pageHistory.selectedRow >= biliController.historyCount) {
                pageHistory.selectedRow = biliController.historyCount - 1
            }
        }
    }

    // ================= 1. 全局空态展示 (历史为空时居中提示) =================
    Item {
        id: emptyHistoryView
        anchors.fill: parent
        visible: biliController.historyCount === 0
        z: 5

        Column {
            anchors.centerIn: parent
            spacing: 12

            Rectangle {
                width: 56
                height: 56
                radius: 28
                color: FluTheme.surfaceActive
                anchors.horizontalCenter: parent.horizontalCenter

                BiliIcon {
                    name: "download"
                    width: 24
                    height: 24
                    color: FluTheme.textDisabled
                    anchors.centerIn: parent
                }
            }

            Text {
                text: "历史记录还是空的"
                color: FluTheme.textPrimary
                font.pixelSize: FluTheme.fontSizeTitle
                font.bold: true
                font.family: FluTheme.fontTitle
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                text: "去下载一部视频，留存属于你的视听记忆"
                color: FluTheme.textSecondary
                font.pixelSize: FluTheme.fontSizeCaption
                font.family: FluTheme.fontBody
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }

    // ================= 2. 主页面内容 (精确定位胶片带 · 极致压缩简约卡片) =================
    Item {
        id: mainHistoryContent
        anchors.fill: parent
        anchors.margins: 18
        visible: biliController.historyCount > 0

        // 右上角极简清空纯图标微药丸 (微环境降噪 · 零多余气泡)
        Rectangle {
            id: clearBtn
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 2
            anchors.rightMargin: 4
            width: 28
            height: 28
            radius: 14
            color: clearMa.containsMouse ? FluTheme.surfaceHover : "transparent"
            border.color: clearMa.containsMouse ? FluTheme.danger : FluTheme.cardBorder
            border.width: 1
            opacity: clearMa.containsMouse ? 1.0 : 0.65
            z: 30

            scale: clearMa.pressed ? 0.92 : (clearMa.containsMouse ? 1.08 : 1.0)
            Behavior on scale { NumberAnimation { duration: FluTheme.durationFast; easing.type: Easing.OutCubic } }
            Behavior on opacity { NumberAnimation { duration: FluTheme.durationFast } }
            Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
            Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

            BiliIcon {
                name: "trash"
                width: 13
                height: 13
                color: clearMa.containsMouse ? FluTheme.danger : FluTheme.textSecondary
                anchors.centerIn: parent
                Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
            }

            MouseArea {
                id: clearMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: clearConfirmDialog.open()
            }
        }

        // ----------------------------------------------------
        // 居中横向胶片带 (用户指定黄金纵向位置 · 丝滑物理惯性阻尼滚动)
        // ----------------------------------------------------
        Item {
            id: filmContainer
            anchors.fill: parent
            clip: true

            ListView {
                id: filmListView
                anchors.fill: parent
                orientation: ListView.Horizontal
                spacing: 20
                clip: false
                boundsBehavior: Flickable.StopAtBounds
                model: biliController.historyModel

                // 平滑阻尼动力学滚动属性
                property real targetContentX: 0

                Behavior on contentX {
                    NumberAnimation {
                        duration: 260
                        easing.type: Easing.OutCubic
                    }
                }

                // 当总宽度未填满容器时自动居中，超出时自动靠左并支持平滑滚动
                leftMargin: Math.max(0, (filmContainer.width - (filmListView.count * 256 + (filmListView.count - 1) * 20)) / 2)
                rightMargin: leftMargin

                // 丝滑滚轮阻尼平滑推进 (卡片级平滑步进)
                WheelHandler {
                    target: filmListView
                    orientation: Qt.Vertical
                    onWheel: function(event) {
                        var delta = event.angleDelta.y
                        var maxScroll = Math.max(0, filmListView.contentWidth - filmContainer.width)
                        if (maxScroll > 0) {
                            var step = 276 // 单卡片 + 间距平滑步进
                            var nextX = filmListView.targetContentX - (delta > 0 ? step : -step)
                            filmListView.targetContentX = Math.max(0, Math.min(maxScroll, nextX))
                            filmListView.contentX = filmListView.targetContentX
                        }
                    }
                }

                delegate: Rectangle {
                    id: filmCard
                    width: 256
                    height: 144
                    radius: 10
                    color: FluTheme.surfaceBg
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    anchors.verticalCenterOffset: -65 // 严丝合缝匹配用户指定视窗黄金位置

                    readonly property bool isSelected: (pageHistory.isDetailActive && pageHistory.selectedRow === index) || cardMa.containsMouse
                    readonly property bool isHovered: cardMa.containsMouse

                    border.color: isSelected ? FluTheme.primaryColor : FluTheme.cardBorder
                    border.width: isSelected ? 2 : 1

                    scale: isSelected ? 1.05 : (isHovered ? 1.02 : 1.0)
                    z: isSelected ? 10 : (isHovered ? 5 : 1)

                    Behavior on scale {
                        NumberAnimation {
                            duration: 180
                            easing.type: Easing.OutCubic
                        }
                    }
                    Behavior on border.color {
                        ColorAnimation { duration: FluTheme.durationFast }
                    }

                    // 封面图片
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: filmCard.isSelected ? 2 : 1
                        radius: 8
                        color: FluTheme.surfaceActive
                        clip: true

                        Image {
                            id: cardCoverImg
                            anchors.fill: parent
                            // 2x 展示尺寸解码 (256×144 卡片), 历史网格多卡场景降内存最明显
                            sourceSize: Qt.size(512, 288)
                            source: pageHistory.resolveCover(model.localCover, model.coverUrl)
                            fillMode: Image.PreserveAspectCrop
                            smooth: true
                            asynchronous: true
                        }

                        // 底部渐变暗影
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 32
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "transparent" }
                                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.72) }
                            }
                        }

                        // 右下角时长胶囊
                        Text {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 6
                            text: model.durationDesc ? model.durationDesc : ""
                            color: "#FFFFFF"
                            font.pixelSize: 11
                            font.family: FluTheme.fontMono
                            font.bold: true
                        }
                    }

                    // 卡片主点击与悬停响应区
                    MouseArea {
                        id: cardMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: pageHistory.showDetail(index)
                        onExited: pageHistory.scheduleHideDetail()
                        onClicked: pageHistory.showDetail(index)
                        onDoubleClicked: biliController.openHistoryFile(index)
                    }
                }
            }
        }

        // ----------------------------------------------------
        // 详情卡 (极致压缩感 & 简约感 · 68px 横向纯净媒体微胶囊)
        // ----------------------------------------------------
        FluCard {
            id: detailCard
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            height: 68
            radius: 12
            hoverElevate: true
            z: 50

            // 仅在鼠标悬停或激活时优雅浮现
            opacity: (pageHistory.isDetailActive && pageHistory.selectedRow >= 0 && pageHistory.selectedRow < filmListView.count) ? 1.0 : 0.0
            visible: opacity > 0.001
            enabled: opacity > 0.5

            transform: Translate {
                y: pageHistory.isDetailActive ? 0 : 12
                Behavior on y {
                    NumberAnimation {
                        duration: 200
                        easing.type: Easing.OutCubic
                    }
                }
            }

            Behavior on opacity {
                NumberAnimation {
                    duration: 180
                    easing.type: Easing.OutCubic
                }
            }

            // 非侵入式 HoverHandler: 即使鼠标移入内部子按钮，hovered 也始终为 true，绝不误触隐藏
            HoverHandler {
                id: detailHoverHandler
                onHoveredChanged: {
                    if (hovered) {
                        hideDetailTimer.stop()
                        pageHistory.isDetailActive = true
                    } else {
                        pageHistory.scheduleHideDetail()
                    }
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 14
                spacing: 14

                // 1. 左侧 16:9 微海报封面 (88×50)
                Item {
                    Layout.preferredWidth: 88
                    Layout.preferredHeight: 50
                    Layout.alignment: Qt.AlignVCenter

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: FluTheme.surfaceActive
                        clip: true
                        border.color: FluTheme.cardBorder
                        border.width: 1

                        Image {
                            id: bigCoverImg
                            anchors.fill: parent
                            // 2x 展示尺寸解码 (88×50 微海报)
                            sourceSize: Qt.size(176, 100)
                            source: {
                                if (pageHistory.selectedRow < 0 || pageHistory.selectedRow >= filmListView.count) return ""
                                var idx = filmListView.model.index(pageHistory.selectedRow, 0)
                                var loc = filmListView.model.data(idx, 0x104 /*LocalCoverRole*/)
                                var url = filmListView.model.data(idx, 0x103 /*CoverUrlRole*/)
                                return pageHistory.resolveCover(loc, url)
                            }
                            fillMode: Image.PreserveAspectCrop
                            smooth: true
                            asynchronous: true
                        }

                        // 右下角：超微画质角标
                        Rectangle {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 3
                            height: 14
                            radius: 3
                            width: qBadgeTxt.implicitWidth + 6
                            color: Qt.rgba(0, 0, 0, 0.68)

                            Text {
                                id: qBadgeTxt
                                anchors.centerIn: parent
                                text: {
                                    if (pageHistory.selectedRow < 0 || pageHistory.selectedRow >= filmListView.count) return ""
                                    var idx = filmListView.model.index(pageHistory.selectedRow, 0)
                                    return filmListView.model.data(idx, 0x107 /*QualityTextRole*/) || "HD"
                                }
                                color: "#FFFFFF"
                                font.pixelSize: 9
                                font.bold: true
                                font.family: FluTheme.fontMono
                            }
                        }
                    }
                }

                // 2. 中间双行信息流 (标题 + 纯净点分隔元数据)
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 3

                    // 第 1 行: 视频大标题
                    Text {
                        Layout.fillWidth: true
                        text: {
                            if (pageHistory.selectedRow < 0 || pageHistory.selectedRow >= filmListView.count) return ""
                            var idx = filmListView.model.index(pageHistory.selectedRow, 0)
                            return filmListView.model.data(idx, 0x102 /*TitleRole*/) || ""
                        }
                        color: FluTheme.textPrimary
                        font.pixelSize: 14
                        font.bold: true
                        font.family: FluTheme.fontTitle
                        elide: Text.ElideRight
                    }

                    // 第 2 行: Apple 纯净点分隔元数据 (UP主 · 文件大小 · 完成时间)
                    Text {
                        Layout.fillWidth: true
                        text: {
                            if (pageHistory.selectedRow < 0 || pageHistory.selectedRow >= filmListView.count) return ""
                            var idx = filmListView.model.index(pageHistory.selectedRow, 0)
                            var owner = filmListView.model.data(idx, 0x105 /*OwnerNameRole*/) || "UP主"
                            var size = filmListView.model.data(idx, 0x108 /*SizeTextRole*/) || ""
                            var timeStr = filmListView.model.data(idx, 0x10E /*FinishedTextRole*/) || ""

                            var parts = []
                            if (owner) parts.push(owner)
                            if (size) parts.push(size)
                            if (timeStr) parts.push(timeStr)
                            return parts.join("  ·  ")
                        }
                        color: FluTheme.textSecondary
                        font.pixelSize: 11
                        font.family: FluTheme.fontBody
                        elide: Text.ElideRight
                    }
                }

                // 3. 右侧纯图标操作胶囊组 (水平紧凑阵列 · 30×30 正圆药丸 · 零多余注释气泡)
                Row {
                    spacing: 8
                    Layout.alignment: Qt.AlignVCenter

                    // 播放主胶囊 (实心激光红 30x30)
                    FluPillButton {
                        iconName: "play"
                        isFilled: true
                        themeColor: FluTheme.primaryColor
                        implicitHeight: 30
                        implicitWidth: 30
                        onClicked: biliController.openHistoryFile(pageHistory.selectedRow)
                    }

                    // 定位目录胶囊 (30x30)
                    FluPillButton {
                        iconName: "folder"
                        isFilled: false
                        implicitHeight: 30
                        implicitWidth: 30
                        onClicked: biliController.openHistoryFolder(pageHistory.selectedRow)
                    }

                    // 移除记录胶囊 (30x30)
                    FluPillButton {
                        iconName: "trash"
                        isFilled: false
                        themeColor: FluTheme.danger
                        implicitHeight: 30
                        implicitWidth: 30
                        onClicked: biliController.removeHistory(pageHistory.selectedRow)
                    }
                }
            }
        }
    }

    // ================= 3. 清空确认对话框 =================
    FluContentDialog {
        id: clearConfirmDialog
        title: "清空历史记录"
        dialogWidth: 320
        dialogHeight: 160

        ColumnLayout {
            anchors.fill: parent
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: "此操作将清除所有历史归档记录，但绝不会删除磁盘上已下载的视频文件。"
                color: FluTheme.textSecondary
                font.pixelSize: FluTheme.fontSizeBody
                font.family: FluTheme.fontBody
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignRight
                spacing: 10

                Item { Layout.fillWidth: true }

                FluPillButton {
                    text: "取消"
                    implicitHeight: 30
                    onClicked: clearConfirmDialog.close()
                }

                FluPillButton {
                    text: "确定清空"
                    isFilled: true
                    themeColor: FluTheme.danger
                    implicitHeight: 30
                    onClicked: {
                        clearConfirmDialog.close()
                        biliController.clearHistory()
                    }
                }
            }
        }
    }
}