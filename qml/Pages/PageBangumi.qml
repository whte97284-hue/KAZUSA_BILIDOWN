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
        Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
    }
    Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

    readonly property var qMap: [120, 116, 80, 64]
    readonly property var cMap: [12, 13, 7]

    property var selectedIndices: []
    property bool isTransitioning: false
    property bool isReturning: false

    function doBack() {
        if (page.isReturning) return
        page.isReturning = true
        backTransitionTimer.restart()
    }

    Timer {
        id: backTransitionTimer
        interval: 180
        repeat: false
        onTriggered: {
            biliController.clearDetail()
            page.isReturning = false
        }
    }

    onSelectedIndicesChanged: {
        if (typeof badgeBounceAnim !== "undefined" && badgeBounceAnim) {
            badgeBounceAnim.restart()
        }
    }

    // 切换所有分P选中/取消
    function toggleSelectAll() {
        if (biliController.pageCount === 0) {
            selectedIndices = []
            return
        }
        if (selectedIndices.length === biliController.pageCount) {
            selectedIndices = []
        } else {
            var arr = []
            for (var i = 0; i < biliController.pageCount; ++i) {
                arr.push(i)
            }
            selectedIndices = arr
        }
    }

    // 切换单P选中
    function toggleSelect(idx) {
        var copy = selectedIndices.slice()
        var pos = copy.indexOf(idx)
        if (pos >= 0) {
            copy.splice(pos, 1)
        } else {
            copy.push(idx)
        }
        selectedIndices = copy
    }

    function isSelected(idx) {
        return selectedIndices.indexOf(idx) >= 0
    }

    function doParse() {
        if (centerInput.text.trim() !== "" && !biliController.isParsing) {
            page.isTransitioning = true
            searchBox.scale = 0.97
            scaleResetTimer.restart()
            biliController.parseVideo(centerInput.text.trim())
        }
    }

    Timer {
        id: scaleResetTimer
        interval: 120
        onTriggered: searchBox.scale = 1.0
    }

    // 监听全屏转场幕布完全拉上事件 (幕后换景)
    Connections {
        target: appSplashScreen
        function onCovered() {
            page.isTransitioning = false
        }
    }

    // 当解析到新视频/番剧时，默认全选分P
    Connections {
        target: biliController
        function onVideoDetailChanged() {
            var arr = []
            for (var i = 0; i < biliController.pageCount; ++i) {
                arr.push(i)
            }
            page.selectedIndices = arr
        }
        function onParseFailed(error) {
            page.isTransitioning = false
        }
    }

    Component.onCompleted: {
        var arr = []
        for (var i = 0; i < biliController.pageCount; ++i) {
            arr.push(i)
        }
        page.selectedIndices = arr
    }

    // ================= 1. 空态居中纯净输入框 (无任务时展示 · 严格隔离 · 缩放不移位) =================
    Item {
        id: emptyView
        anchors.fill: parent
        visible: opacity > 0.001
        opacity: (biliController.pageCount === 0 || page.isTransitioning) ? 1.0 : 0.0
        scale: (biliController.pageCount === 0 || page.isTransitioning) ? 1.0 : 0.96

        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

        // 外层微环境弥散阴影
        Rectangle {
            anchors.centerIn: searchBox
            width: searchBox.width + 4
            height: searchBox.height + 4
            radius: 24
            color: "transparent"
            border.color: Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g, FluTheme.primaryColor.b, 0.22)
            border.width: centerInput.activeFocus ? 3 : 0
            opacity: centerInput.activeFocus ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on border.width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        }

        Rectangle {
            id: searchBox
            width: Math.min(520, parent.width - 64)
            height: 44
            radius: 22
            color: FluTheme.surfaceBg
            border.color: (centerInput.activeFocus || biliController.isParsing) ? FluTheme.primaryColor : FluTheme.cardBorder
            border.width: 1

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -42

            Behavior on scale { NumberAnimation { duration: FluTheme.durationFast; easing.type: FluTheme.easingStandard } }
            Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

            // 解析中呼吸光晕动效
            SequentialAnimation on border.color {
                running: biliController.isParsing && emptyView.visible
                loops: Animation.Infinite
                alwaysRunToEnd: true
                ColorAnimation { from: FluTheme.primaryColor; to: Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g, FluTheme.primaryColor.b, 0.25); duration: 600; easing.type: Easing.InOutCubic }
                ColorAnimation { from: Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g, FluTheme.primaryColor.b, 0.25); to: FluTheme.primaryColor; duration: 600; easing.type: Easing.InOutCubic }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 7
                spacing: 8

                TextInput {
                    id: centerInput
                    Layout.fillWidth: true
                    font.pixelSize: FluTheme.fontSizeBody
                    font.family: FluTheme.fontMono
                    color: FluTheme.textPrimary
                    selectByMouse: true
                    clip: true
                    verticalAlignment: TextInput.AlignVCenter
                    onAccepted: page.doParse()
                }

                // 清空小图标 (有输入内容时淡入)
                Rectangle {
                    width: 22
                    height: 22
                    radius: 11
                    color: clearMa.containsMouse ? FluTheme.surfaceHover : "transparent"
                    visible: centerInput.text.length > 0 && !biliController.isParsing
                    Layout.alignment: Qt.AlignVCenter

                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        color: FluTheme.textSecondary
                        font.pixelSize: 15
                        font.bold: true
                    }

                    MouseArea {
                        id: clearMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: centerInput.text = ""
                    }
                }

                // 核心解析触发圆形按钮 (内嵌放大镜图标 / 解析中转换为加载环)
                Rectangle {
                    id: parseBtn
                    width: 30
                    height: 30
                    radius: 15
                    color: FluTheme.primaryColor
                    Layout.alignment: Qt.AlignVCenter

                    scale: parseBtnMa.pressed ? 0.94 : 1.0
                    Behavior on scale { NumberAnimation { duration: FluTheme.durationFast; easing.type: Easing.OutCubic } }
                    Behavior on opacity { NumberAnimation { duration: FluTheme.durationFast } }

                    opacity: parseBtnMa.containsMouse ? 0.92 : 1.0

                    // 放大镜图标 (替代原有"解析"文字)
                    BiliIcon {
                        id: parseIcon
                        name: "search"
                        width: 14
                        height: 14
                        color: "#FFFFFF"
                        anchors.centerIn: parent
                        opacity: biliController.isParsing ? 0.0 : 1.0
                        Behavior on opacity { NumberAnimation { duration: FluTheme.durationFast } }
                    }

                    // 加载环
                    FluProgressRing {
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        color: "#FFFFFF"
                        opacity: biliController.isParsing ? 1.0 : 0.0
                        Behavior on opacity { NumberAnimation { duration: FluTheme.durationFast } }
                    }

                    MouseArea {
                        id: parseBtnMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        enabled: !biliController.isParsing
                        onClicked: {
                            biliController.playClickSound() // 拍子木: 解析即跨页导航
                            page.doParse()
                        }

                        ToolTip.visible: containsMouse && !biliController.isParsing
                        ToolTip.text: "解析输入链接"
                        ToolTip.delay: 350
                    }
                }
            }
        }
    }

    // ================= 2. 详细下载内容布局 (Hero 影视卡片 + 现代选集) =================
    ColumnLayout {
        id: detailView
        anchors.fill: parent
        anchors.margins: 16
        anchors.topMargin: 4
        spacing: 10
        visible: opacity > 0.001
        opacity: (biliController.pageCount > 0 && !page.isTransitioning && !page.isReturning) ? 1.0 : 0.0
        transform: Translate {
            y: (biliController.pageCount > 0 && !page.isTransitioning && !page.isReturning) ? 0 : 12
            Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        }
        Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

        // ================= 1. Hero 电影级媒体卡片 (Apple TV+ 极简主义) =================
        FluCard {
            Layout.fillWidth: true
            implicitHeight: 126
            radius: 12
            hoverElevate: true

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 16

                // 1. 16:9 Hero 海报封面 (整合内嵌悬浮返回键 + 状态角标)
                Item {
                    Layout.preferredWidth: 180
                    Layout.preferredHeight: 101
                    Layout.alignment: Qt.AlignVCenter

                    Rectangle {
                        id: posterRect
                        anchors.fill: parent
                        radius: 8
                        clip: true
                        color: FluTheme.surfaceActive
                        border.color: FluTheme.cardBorder
                        border.width: 1

                        Image {
                            anchors.fill: parent
                            source: biliController.currentCover
                            // 2x 展示尺寸解码 (180×101 Hero 海报)
                            sourceSize: Qt.size(360, 202)
                            fillMode: Image.PreserveAspectCrop
                            smooth: true
                        }

                        // 封面悬停柔黑渐变
                        Rectangle {
                            anchors.fill: parent
                            color: "#000000"
                            opacity: posterMa.containsMouse ? 0.25 : 0.0
                            Behavior on opacity { NumberAnimation { duration: FluTheme.durationFast } }
                        }

                        // 右下角：集数/分P纯净毛玻璃微胶囊
                        Rectangle {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 6
                            height: 18
                            radius: 4
                            width: tagEpisodeTxt.implicitWidth + 10
                            color: Qt.rgba(0, 0, 0, 0.65)

                            Text {
                                id: tagEpisodeTxt
                                anchors.centerIn: parent
                                text: biliController.isBangumi ? ("全 " + biliController.pageCount + " 话") : ("共 " + biliController.pageCount + " P")
                                color: "#FFFFFF"
                                font.pixelSize: 10
                                font.bold: true
                                font.family: FluTheme.fontMono
                            }
                        }

                        MouseArea {
                            id: posterMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: descPopup.open()

                            ToolTip.visible: containsMouse && !backMa.containsMouse
                            ToolTip.text: "点击查看完整简介"
                            ToolTip.delay: 350
                        }
                    }

                    // 悬浮在海报左上角的极简毛玻璃返回药丸
                    Rectangle {
                        id: floatingBackBtn
                        width: 26
                        height: 26
                        radius: 13
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 6
                        z: 10
                        color: backMa.pressed ? Qt.rgba(0, 0, 0, 0.85) : (backMa.containsMouse ? Qt.rgba(0, 0, 0, 0.7) : Qt.rgba(0, 0, 0, 0.45))
                        border.color: Qt.rgba(255, 255, 255, 0.25)
                        border.width: 1

                        scale: backMa.pressed ? 0.92 : 1.0
                        Behavior on scale { NumberAnimation { duration: FluTheme.durationFast; easing.type: Easing.OutCubic } }
                        Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }

                        BiliIcon {
                            name: "back"
                            width: 11
                            height: 11
                            anchors.centerIn: parent
                            color: "#FFFFFF"
                        }

                        MouseArea {
                            id: backMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.doBack()

                            ToolTip.visible: containsMouse
                            ToolTip.text: "返回解析首页"
                            ToolTip.delay: 350
                        }
                    }

                    // 简介浮层 Popup (自适应毛玻璃展开)
                    Popup {
                        id: descPopup
                        x: 190
                        y: 0
                        width: 340
                        height: Math.min(240, descText.implicitHeight + 28)
                        padding: 14
                        modal: false
                        focus: true
                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                        background: Rectangle {
                            color: FluTheme.surfaceBg
                            border.color: FluTheme.cardBorder
                            border.width: 1
                            radius: 8
                        }

                        Flickable {
                            anchors.fill: parent
                            contentWidth: width
                            contentHeight: descText.implicitHeight
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds

                            Text {
                                id: descText
                                width: parent.width
                                text: biliController.currentDesc !== "" ? biliController.currentDesc : "暂无剧情简介"
                                color: FluTheme.textPrimary
                                font.pixelSize: FluTheme.fontSizeBody
                                font.family: FluTheme.fontBody
                                wrapMode: Text.Wrap
                                lineHeight: 1.4
                            }
                        }
                    }
                }

                // 2. 右侧核心信息流 (大标题 -> 点分隔元数据 -> 优雅简介摘要)
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 5

                    // 第 1 层: 媒体大标题 + 琥珀金评分徽章 + 播放量
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: biliController.currentTitle !== "" ? biliController.currentTitle : "未命名媒体"
                            color: FluTheme.textPrimary
                            font.pixelSize: 17
                            font.bold: true
                            font.family: FluTheme.fontTitle
                            elide: Text.ElideRight
                            Layout.maximumWidth: 380
                        }

                        // 琥珀金评分胶囊 (⭐ 9.8)
                        Rectangle {
                            visible: biliController.currentScore > 0
                            height: 18
                            radius: 4
                            width: scoreRow.implicitWidth + 10
                            color: Qt.rgba(255, 170, 0, 0.15)
                            border.color: Qt.rgba(255, 170, 0, 0.4)
                            border.width: 1
                            Layout.alignment: Qt.AlignVCenter

                            Row {
                                id: scoreRow
                                anchors.centerIn: parent
                                spacing: 3

                                Text {
                                    text: biliController.currentScore.toFixed(1)
                                    color: FluTheme.warning
                                    font.pixelSize: 11
                                    font.bold: true
                                    font.family: FluTheme.fontMono
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                BiliIcon {
                                    name: "star"
                                    color: FluTheme.warning
                                    width: 9
                                    height: 9
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }

                        // 播放量
                        Text {
                            visible: biliController.currentViews !== ""
                            text: biliController.currentViews + " 播放"
                            color: FluTheme.textSecondary
                            font.pixelSize: 12
                            font.family: FluTheme.fontBody
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Item { Layout.fillWidth: true }
                    }

                    // 第 2 层: Apple 纯净点分隔元数据 (降噪 80%, 无笨重黑框)
                    Text {
                        Layout.fillWidth: true
                        text: {
                            var meta = []
                            if (biliController.currentSeasonInfo !== "") meta.push(biliController.currentSeasonInfo)
                            if (biliController.currentOwner !== "") meta.push(biliController.currentOwner)
                            return meta.join("  ·  ")
                        }
                        color: FluTheme.textSecondary
                        font.pixelSize: 12
                        font.family: FluTheme.fontBody
                        elide: Text.ElideRight
                        visible: text !== ""
                    }

                    // 第 3 层: 剧情简介两行摘要 (充分填充右侧空白, 优雅叙事)
                    Text {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: biliController.currentDesc !== "" ? biliController.currentDesc : "暂无详细简介说明"
                        color: FluTheme.textDisabled
                        font.pixelSize: 12
                        font.family: FluTheme.fontBody
                        lineHeight: 1.35
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        // ================= 2. 分 P / 选集列表卡片 (内置微勾选状态与极简指示器) =================
        FluCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            clip: true
            hoverElevate: false

            Item {
                anchors.fill: parent
                anchors.margins: 6

                // 选集列表
                ListView {
                    id: episodeList
                    anchors.fill: parent
                    anchors.topMargin: 2
                    anchors.bottomMargin: 2
                    spacing: 2
                    clip: true
                    model: biliController.pageModel
                    boundsBehavior: Flickable.StopAtBounds

                    // 极简微细滚动条
                    ScrollBar.vertical: ScrollBar {
                        id: epScrollBar
                        width: 4
                        anchors.right: parent.right
                        anchors.rightMargin: 1
                        policy: episodeList.contentHeight > episodeList.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                        contentItem: Rectangle {
                            implicitWidth: 4
                            radius: 2
                            color: epScrollBar.pressed ? FluTheme.primaryColor : (epScrollBar.hovered ? FluTheme.textSecondary : Qt.rgba(128, 128, 128, 0.35))
                        }
                    }

                    delegate: Rectangle {
                        id: rowItem
                        width: episodeList.width - (episodeList.contentHeight > episodeList.height ? 6 : 0)
                        height: 40
                        radius: 6

                        property bool active: page.isSelected(index)

                        color: active ? (FluTheme.darkMode === 1 ? Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g, FluTheme.primaryColor.b, 0.12) : Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g, FluTheme.primaryColor.b, 0.06)) : (rowMa.containsMouse ? FluTheme.surfaceHover : "transparent")

                        border.color: active ? Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g, FluTheme.primaryColor.b, 0.25) : "transparent"
                        border.width: 1

                        Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
                        Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 12

                            // 序号 (Mono 等宽字体)
                            Text {
                                text: (index + 1 < 10 ? "0" : "") + (index + 1)
                                font.pixelSize: 12
                                font.bold: rowItem.active
                                font.family: FluTheme.fontMono
                                color: rowItem.active ? FluTheme.primaryColor : FluTheme.textSecondary
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: 20
                            }

                            // 分P标题
                            Text {
                                Layout.fillWidth: true
                                text: model.title !== "" ? model.title : ("第 " + (index + 1) + " 话")
                                font.pixelSize: FluTheme.fontSizeBody
                                font.family: FluTheme.fontBody
                                font.bold: rowItem.active
                                color: rowItem.active ? FluTheme.textPrimary : (rowMa.containsMouse ? FluTheme.textPrimary : FluTheme.textSecondary)
                                elide: Text.ElideRight
                                Layout.alignment: Qt.AlignVCenter
                            }

                            // 时长
                            Text {
                                text: model.durationDesc !== "" ? model.durationDesc : "24:00"
                                font.pixelSize: 11
                                font.family: FluTheme.fontMono
                                color: FluTheme.textDisabled
                                Layout.alignment: Qt.AlignVCenter
                            }

                            // 右侧精巧勾选指示徽章 (18x18 药丸)
                            Rectangle {
                                width: 18
                                height: 18
                                radius: 9
                                color: rowItem.active ? FluTheme.primaryColor : "transparent"
                                border.color: rowItem.active ? "transparent" : (rowMa.containsMouse ? FluTheme.textSecondary : Qt.rgba(128, 128, 128, 0.2))
                                border.width: 1
                                opacity: (rowItem.active || rowMa.containsMouse) ? 1.0 : 0.5
                                Layout.alignment: Qt.AlignVCenter

                                scale: 1.0
                                Behavior on opacity { NumberAnimation { duration: FluTheme.durationFast } }
                                Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }

                                BiliIcon {
                                    name: "check"
                                    width: 10
                                    height: 10
                                    color: "#FFFFFF"
                                    anchors.centerIn: parent
                                    visible: rowItem.active
                                }
                            }
                        }

                        MouseArea {
                            id: rowMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.toggleSelect(index)
                        }
                    }
                }
            }
        }

        // ================= 3. 底部下载设置条 (纯净降噪 · 无多余注释文字与提示) =================
        FluCard {
            Layout.fillWidth: true
            implicitHeight: 46
            radius: 10

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                // 清晰度下拉 (纯粹画质规格)
                FluComboBox {
                    id: qualityCombo
                    Layout.preferredWidth: 92
                    Layout.preferredHeight: 30
                    model: ["4K", "1080P 60", "1080P", "720P"]
                    currentIndex: {
                        if (biliController.defaultQuality === 120) return 0
                        if (biliController.defaultQuality === 116) return 1
                        if (biliController.defaultQuality === 80) return 2
                        return 3
                    }
                    onActivated: {
                        biliController.setDefaultQuality(page.qMap[currentIndex])
                    }
                }

                // 编码下拉 (纯粹编码名称)
                FluComboBox {
                    id: codecCombo
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 30
                    model: ["HEVC", "AV1", "AVC"]
                    currentIndex: {
                        if (biliController.defaultCodec === 12) return 0
                        if (biliController.defaultCodec === 13) return 1
                        return 2
                    }
                    onActivated: {
                        biliController.setDefaultCodec(page.cMap[currentIndex])
                    }
                }

                // 保存路径胶囊 (文件夹图标 + 纯路径)
                FluPillButton {
                    iconName: "folder"
                    text: biliController.downloadDir
                    Layout.maximumWidth: 260
                    implicitHeight: 30
                    onClicked: biliController.chooseDownloadDir()
                }

                // 全选 / 取消全选快捷胶囊 (纯图标 32x30)
                FluPillButton {
                    iconName: (biliController.pageCount > 0 && page.selectedIndices.length === biliController.pageCount) ? "clear" : "check"
                    text: ""
                    implicitWidth: 32
                    implicitHeight: 30
                    onClicked: page.toggleSelectAll()
                }

                Item { Layout.fillWidth: true; height: 1 }

                // 唯一主操作：Apple 纯粹数字下载药丸 (数字 + 下载图标 + 动态跳动反馈)
                Item {
                    id: dlBtnContainer
                    Layout.preferredWidth: Math.max(54, mainDlTxt.implicitWidth + 26)
                    Layout.preferredHeight: 30

                    // 扩散微光晕 (选中数量改变时脉冲扩散)
                    Rectangle {
                        id: pulseRing
                        anchors.centerIn: parent
                        width: parent.width
                        height: parent.height
                        radius: 15
                        color: "transparent"
                        border.color: FluTheme.primaryColor
                        border.width: 1.5
                        opacity: 0.0
                        scale: 1.0
                    }

                    Rectangle {
                        id: mainDownloadBtn
                        anchors.fill: parent
                        radius: 15
                        color: page.selectedIndices.length > 0 ? FluTheme.primaryColor : FluTheme.surfaceHover
                        opacity: page.selectedIndices.length > 0 ? (mainDlMa.pressed ? 0.88 : (mainDlMa.containsMouse ? 0.94 : 1.0)) : 0.45

                        property real popScale: 1.0

                        scale: popScale * (mainDlMa.pressed ? 0.96 : 1.0)
                        Behavior on opacity { NumberAnimation { duration: FluTheme.durationFast } }
                        Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 4

                            BiliIcon {
                                name: "download"
                                width: 12
                                height: 12
                                color: page.selectedIndices.length > 0 ? "#FFFFFF" : FluTheme.textDisabled
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Text {
                                id: mainDlTxt
                                text: page.selectedIndices.length.toString()
                                font.pixelSize: 13
                                font.bold: true
                                font.family: FluTheme.fontMono
                                color: page.selectedIndices.length > 0 ? "#FFFFFF" : FluTheme.textDisabled
                                Layout.alignment: Qt.AlignVCenter

                                property real textBounceScale: 1.0
                                scale: textBounceScale
                            }
                        }

                        MouseArea {
                            id: mainDlMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: page.selectedIndices.length > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                            enabled: page.selectedIndices.length > 0
                            onClicked: {
                                var q = page.qMap[qualityCombo.currentIndex]
                                var c = page.cMap[codecCombo.currentIndex]
                                biliController.enqueuePages(page.selectedIndices, q, c, biliController.downloadDir)
                                pageStack.currentIndex = 1
                            }

                            ToolTip.visible: containsMouse
                            ToolTip.text: page.selectedIndices.length > 0 ? ("批量下载已选 " + page.selectedIndices.length + " 个视频/分P") : "请先勾选需要下载的分P"
                            ToolTip.delay: 350
                        }
                    }

                    // 数字跳动 + 脉冲扩散动画
                    ParallelAnimation {
                        id: badgeBounceAnim

                        // 整体药丸微跳动
                        SequentialAnimation {
                            NumberAnimation { target: mainDownloadBtn; property: "popScale"; to: 1.18; duration: 90; easing.type: Easing.OutQuad }
                            NumberAnimation { target: mainDownloadBtn; property: "popScale"; to: 0.94; duration: 70; easing.type: Easing.InOutQuad }
                            NumberAnimation { target: mainDownloadBtn; property: "popScale"; to: 1.0; duration: 80; easing.type: Easing.OutBack }
                        }

                        // 内部数字跳动
                        SequentialAnimation {
                            NumberAnimation { target: mainDlTxt; property: "textBounceScale"; to: 1.38; duration: 90; easing.type: Easing.OutQuad }
                            NumberAnimation { target: mainDlTxt; property: "textBounceScale"; to: 0.88; duration: 70; easing.type: Easing.InOutQuad }
                            NumberAnimation { target: mainDlTxt; property: "textBounceScale"; to: 1.0; duration: 80; easing.type: Easing.OutBack }
                        }

                        // 扩散光圈
                        SequentialAnimation {
                            ScriptAction {
                                script: {
                                    pulseRing.scale = 1.0
                                    pulseRing.opacity = 0.65
                                }
                            }
                            ParallelAnimation {
                                NumberAnimation { target: pulseRing; property: "scale"; to: 1.35; duration: 240; easing.type: Easing.OutQuad }
                                NumberAnimation { target: pulseRing; property: "opacity"; to: 0.0; duration: 240; easing.type: Easing.OutQuad }
                            }
                        }
                    }
                }
            }
        }
    }
}