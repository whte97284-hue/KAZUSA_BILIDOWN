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

    property int selectedQuality: biliController.defaultQuality || 120
    property int selectedCodec: biliController.defaultCodec || 12

    // 设置项圆润字体
    property string fontRound: "Sarasa UI SC"

    // 限速输入激活态
    property bool speedPending: false

    Flickable {
        anchors.fill: parent
        contentWidth: parent.width
        contentHeight: contentCol.height + 60
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: contentCol
            width: Math.min(parent.width - 48, 760)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 20
            spacing: 8

            // 1. kazusa. 极简工业风品牌卡片 (打磨升级版)
            FluCard {
                width: parent.width
                implicitHeight: 46

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 0

                    // 1. 左侧标题栏: 绝对坐标锁定 (x: 0 放图标, x: 26 放文字)
                    Item {
                        Layout.preferredWidth: 104
                        Layout.fillHeight: true

                        // 16x16 App 图标 (绝对 x: 0)
                        Rectangle {
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            radius: 3.5
                            clip: true
                            color: "transparent"

                            Image {
                                anchors.fill: parent
                                source: "qrc:/kazusa.png"
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                            }
                        }

                        // 品牌名 kazusa. (绝对 x: 26, 与下方所有汉字绝对 100% 垂直重合)
                        Row {
                            x: 26
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 0

                            Text {
                                text: "kazusa"
                                color: FluTheme.textPrimary
                                font.pixelSize: 14
                                font.bold: true
                                font.family: FluTheme.fontTitle
                                verticalAlignment: Text.AlignVCenter
                            }

                            Text {
                                text: "."
                                color: FluTheme.primaryColor
                                font.pixelSize: 16
                                font.bold: true
                                font.family: FluTheme.fontTitle
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    // 2. 开发者与项目介绍 (与 kazusa. 完全相同的高清纯白粗体样式与字号)
                    Text {
                        text: "由 Wh1te 使用 C++ / Qt 开发的哔哩哔哩下载器"
                        color: FluTheme.textPrimary
                        font.pixelSize: 14
                        font.bold: true
                        font.family: FluTheme.fontTitle
                        verticalAlignment: Text.AlignVCenter
                        Layout.alignment: Qt.AlignVCenter
                        elide: Text.ElideRight
                    }

                    Item { Layout.fillWidth: true }

                    // 3. 右侧外链纯图标胶囊 (↗ 跳转 Wh1te11 B站个人主页)
                    FluPillButton {
                        iconName: "external_link"
                        toolTip: "访问 Wh1te 哔哩哔哩个人主页"
                        isFilled: false
                        implicitWidth: 28
                        implicitHeight: 28
                        themeColor: FluTheme.primaryColor
                        Layout.alignment: Qt.AlignVCenter
                        onClicked: Qt.openUrlExternally("https://space.bilibili.com/551898501?spm_id_from=333.788.0.0")
                    }
                }
            }

            // 2. 外观主题模式 (暗色极夜 / 明亮日间)
            FluCard {
                width: parent.width
                implicitHeight: 46

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 0

                    Item {
                        Layout.preferredWidth: 110
                        Layout.fillHeight: true

                        BiliIcon {
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            name: FluTheme.darkMode === 1 ? "moon" : "sun"
                            width: 16
                            height: 16
                            color: FluTheme.textSecondary
                        }

                        Text {
                            x: 26
                            anchors.verticalCenter: parent.verticalCenter
                            text: "外观主题"
                            color: FluTheme.textPrimary
                            font.pixelSize: 13
                            font.bold: true
                            font.family: FluTheme.fontTitle
                        }
                    }

                    Row {
                        Layout.fillWidth: true
                        spacing: 8
                        Layout.alignment: Qt.AlignVCenter

                        SettingPill {
                            text: "深色模式"
                            toolTip: "极夜深黑模式"
                            isSelected: FluTheme.darkMode === 1
                            onClicked: FluTheme.darkMode = 1
                        }
                        SettingPill {
                            text: "浅色模式"
                            toolTip: "日间明亮模式"
                            isSelected: FluTheme.darkMode === 0
                            onClicked: FluTheme.darkMode = 0
                        }
                    }
                }
            }

            // 3. 主题色彩
            FluCard {
                width: parent.width
                implicitHeight: 46

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 0

                    Item {
                        Layout.preferredWidth: 110
                        Layout.fillHeight: true

                        BiliIcon {
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            name: "sun"
                            width: 16
                            height: 16
                            color: FluTheme.textSecondary
                        }

                        Text {
                            x: 26
                            anchors.verticalCenter: parent.verticalCenter
                            text: "主题色彩"
                            color: FluTheme.textPrimary
                            font.pixelSize: 13
                            font.bold: true
                            font.family: FluTheme.fontTitle
                        }
                    }

                    Row {
                        Layout.fillWidth: true
                        spacing: 12
                        Layout.alignment: Qt.AlignVCenter

                        ColorPill {
                            pillColor: "#FF1E42"
                            toolTip: "激光红 (标志性主色)"
                            isSelected: FluTheme.primaryColor.toString().toUpperCase() === "#FF1E42"
                            onClicked: {
                                FluTheme.primaryColor = "#FF1E42"
                                biliController.setPrimaryColor("#FF1E42")
                            }
                        }

                        ColorPill {
                            pillColor: "#9D00FF"
                            toolTip: "初号紫 (EVA 质感)"
                            isSelected: FluTheme.primaryColor.toString().toUpperCase() === "#9D00FF"
                            onClicked: {
                                FluTheme.primaryColor = "#9D00FF"
                                biliController.setPrimaryColor("#9D00FF")
                            }
                        }

                        ColorPill {
                            pillColor: "#FF9100"
                            toolTip: "明亮橙"
                            isSelected: FluTheme.primaryColor.toString().toUpperCase() === "#FF9100"
                            onClicked: {
                                FluTheme.primaryColor = "#FF9100"
                                biliController.setPrimaryColor("#FF9100")
                            }
                        }

                        ColorPill {
                            pillColor: "#00B0FF"
                            toolTip: "科技蓝"
                            isSelected: FluTheme.primaryColor.toString().toUpperCase() === "#00B0FF"
                            onClicked: {
                                FluTheme.primaryColor = "#00B0FF"
                                biliController.setPrimaryColor("#00B0FF")
                            }
                        }
                    }
                }
            }

            // 4. 默认画质
            FluCard {
                width: parent.width
                implicitHeight: 46

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 0

                    Item {
                        Layout.preferredWidth: 110
                        Layout.fillHeight: true

                        BiliIcon {
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            name: "video"
                            width: 16
                            height: 16
                            color: FluTheme.textSecondary
                        }

                        Text {
                            x: 26
                            anchors.verticalCenter: parent.verticalCenter
                            text: "默认画质"
                            color: FluTheme.textPrimary
                            font.pixelSize: 13
                            font.bold: true
                            font.family: FluTheme.fontTitle
                        }
                    }

                    Row {
                        Layout.fillWidth: true
                        spacing: 8
                        Layout.alignment: Qt.AlignVCenter

                        SettingPill {
                            text: "4K"
                            toolTip: "优先解析 2160P (需大会员权限)"
                            isSelected: page.selectedQuality === 120
                            onClicked: {
                                page.selectedQuality = 120
                                biliController.setDefaultQuality(120)
                            }
                        }
                        SettingPill {
                            text: "1080P 60"
                            toolTip: "优先解析 1080P 60FPS 极清帧率"
                            isSelected: page.selectedQuality === 116
                            onClicked: {
                                page.selectedQuality = 116
                                biliController.setDefaultQuality(116)
                            }
                        }
                        SettingPill {
                            text: "1080P"
                            toolTip: "优先解析 1080P 标准全高清"
                            isSelected: page.selectedQuality === 80
                            onClicked: {
                                page.selectedQuality = 80
                                biliController.setDefaultQuality(80)
                            }
                        }
                        SettingPill {
                            text: "720P"
                            toolTip: "优先解析 720P 高清 (节省流量空间)"
                            isSelected: page.selectedQuality === 64
                            onClicked: {
                                page.selectedQuality = 64
                                biliController.setDefaultQuality(64)
                            }
                        }
                    }
                }
            }

            // 5. 视频编码
            FluCard {
                width: parent.width
                implicitHeight: 46

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 0

                    Item {
                        Layout.preferredWidth: 110
                        Layout.fillHeight: true

                        BiliIcon {
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            name: "settings"
                            width: 16
                            height: 16
                            color: FluTheme.textSecondary
                        }

                        Text {
                            x: 26
                            anchors.verticalCenter: parent.verticalCenter
                            text: "视频编码"
                            color: FluTheme.textPrimary
                            font.pixelSize: 13
                            font.bold: true
                            font.family: FluTheme.fontTitle
                        }
                    }

                    Row {
                        Layout.fillWidth: true
                        spacing: 8
                        Layout.alignment: Qt.AlignVCenter

                        SettingPill {
                            text: "HEVC"
                            toolTip: "高压缩比率，画质清晰体积小 (推荐)"
                            isSelected: page.selectedCodec === 12
                            onClicked: {
                                page.selectedCodec = 12
                                biliController.setDefaultCodec(12)
                            }
                        }
                        SettingPill {
                            text: "AV1"
                            toolTip: "下一代开源编码格式"
                            isSelected: page.selectedCodec === 13
                            onClicked: {
                                page.selectedCodec = 13
                                biliController.setDefaultCodec(13)
                            }
                        }
                        SettingPill {
                            text: "AVC"
                            toolTip: "最广泛的设备硬件播放兼容性"
                            isSelected: page.selectedCodec === 7
                            onClicked: {
                                page.selectedCodec = 7
                                biliController.setDefaultCodec(7)
                            }
                        }
                    }
                }
            }

            // 6. 存储路径
            FluCard {
                width: parent.width
                implicitHeight: 46

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 0

                    Item {
                        Layout.preferredWidth: 110
                        Layout.fillHeight: true

                        BiliIcon {
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            name: "folder"
                            width: 16
                            height: 16
                            color: FluTheme.textSecondary
                        }

                        Text {
                            x: 26
                            anchors.verticalCenter: parent.verticalCenter
                            text: "下载存储"
                            color: FluTheme.textPrimary
                            font.pixelSize: 13
                            font.bold: true
                            font.family: FluTheme.fontTitle
                        }
                    }

                    FluPillButton {
                        iconName: "folder"
                        text: biliController.downloadDir
                        toolTip: "点击在资源管理器中选择保存路径: " + biliController.downloadDir
                        Layout.maximumWidth: 260
                        implicitHeight: 26
                        onClicked: biliController.chooseDownloadDir()
                    }

                    Item { Layout.fillWidth: true }
                }
            }

            // 7. 自动混流合并
            FluCard {
                width: parent.width
                implicitHeight: 46

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 0

                    Item {
                        Layout.preferredWidth: 110
                        Layout.fillHeight: true

                        BiliIcon {
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            name: "refresh"
                            width: 16
                            height: 16
                            color: FluTheme.textSecondary
                        }

                        Text {
                            x: 26
                            anchors.verticalCenter: parent.verticalCenter
                            text: "自动混流"
                            color: FluTheme.textPrimary
                            font.pixelSize: 13
                            font.bold: true
                            font.family: FluTheme.fontTitle
                        }
                    }

                    Row {
                        Layout.fillWidth: true
                        spacing: 8
                        Layout.alignment: Qt.AlignVCenter

                        SettingPill {
                            text: "开启"
                            toolTip: "下载后自动将分离的视频流与音频流合并为 MP4"
                            isSelected: biliController.muxEnabled
                            onClicked: biliController.setMuxEnabled(true)
                        }
                        SettingPill {
                            text: "关闭"
                            toolTip: "分别保留原始音视频独立文件"
                            isSelected: !biliController.muxEnabled
                            onClicked: biliController.setMuxEnabled(false)
                        }
                    }
                }
            }

            // 8. 附属资源 (弹幕 XML + 字幕 SRT)
            FluCard {
                width: parent.width
                implicitHeight: 46

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 0

                    Item {
                        Layout.preferredWidth: 110
                        Layout.fillHeight: true

                        BiliIcon {
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            name: "chat"
                            width: 16
                            height: 16
                            color: FluTheme.textSecondary
                        }

                        Text {
                            x: 26
                            anchors.verticalCenter: parent.verticalCenter
                            text: "弹幕字幕"
                            color: FluTheme.textPrimary
                            font.pixelSize: 13
                            font.bold: true
                            font.family: FluTheme.fontTitle
                        }
                    }

                    Row {
                        Layout.fillWidth: true
                        spacing: 8
                        Layout.alignment: Qt.AlignVCenter

                        SettingPill {
                            text: "开启"
                            toolTip: "下载视频时同步保存弹幕 (XML) 与 CC字幕 (SRT)"
                            isSelected: biliController.downloadSubRes
                            onClicked: biliController.setDownloadSubRes(true)
                        }
                        SettingPill {
                            text: "关闭"
                            toolTip: "仅下载音视频本体"
                            isSelected: !biliController.downloadSubRes
                            onClicked: biliController.setDownloadSubRes(false)
                        }
                    }
                }
            }

            // 9. 带宽限速
            FluCard {
                width: parent.width
                implicitHeight: 46

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 0

                    Item {
                        Layout.preferredWidth: 110
                        Layout.fillHeight: true

                        BiliIcon {
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            name: "download"
                            width: 16
                            height: 16
                            color: FluTheme.textSecondary
                        }

                        Text {
                            x: 26
                            anchors.verticalCenter: parent.verticalCenter
                            text: "带宽限速"
                            color: FluTheme.textPrimary
                            font.pixelSize: 13
                            font.bold: true
                            font.family: FluTheme.fontTitle
                        }
                    }

                    Row {
                        Layout.fillWidth: true
                        spacing: 8
                        Layout.alignment: Qt.AlignVCenter

                        SettingPill {
                            text: "不限速"
                            toolTip: "全速多线程并发下载"
                            isSelected: !page.speedPending && biliController.downloadLimitKB === 0
                            onClicked: {
                                page.speedPending = false
                                speedInputBox.visible = false
                                biliController.setDownloadLimitKB(0)
                            }
                        }
                        SettingPill {
                            text: "限速"
                            toolTip: "自定义最高下载速率 (MB/s)"
                            isSelected: page.speedPending || biliController.downloadLimitKB > 0
                            onClicked: {
                                page.speedPending = true
                                speedInputBox.visible = true
                                speedInput.text = biliController.downloadLimitKB > 0 ? String(biliController.downloadLimitKB / 1024) : ""
                                speedInput.forceActiveFocus()
                            }
                        }

                        Rectangle {
                            id: speedInputBox
                            visible: false
                            width: 100
                            height: 26
                            radius: 13
                            color: FluTheme.surfaceBg
                            border.color: speedInput.activeFocus ? FluTheme.primaryColor : FluTheme.cardBorder
                            border.width: 1
                            Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

                            TextField {
                                id: speedInput
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                verticalAlignment: Text.AlignVCenter
                                placeholderText: "MB/s"
                                placeholderTextColor: FluTheme.textDisabled
                                color: FluTheme.textPrimary
                                font.pixelSize: FluTheme.fontSizeCaption
                                font.family: FluTheme.fontMono
                                background: Item {}
                                selectByMouse: true
                                validator: IntValidator { bottom: 1; top: 10240 }

                                onAccepted: {
                                    var mb = parseInt(text, 10)
                                    if (!isNaN(mb) && mb > 0) biliController.setDownloadLimitKB(mb * 1024)
                                    page.speedPending = biliController.downloadLimitKB > 0
                                    speedInputBox.visible = false
                                }
                                onActiveFocusChanged: {
                                    if (!activeFocus && speedInputBox.visible) {
                                        page.speedPending = false
                                        speedInputBox.visible = false
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 底部安全呼吸留白垫 (确保即使在小窗口或滚动到底部也有从容留白)
            Item {
                width: parent.width
                height: 28
            }
        }
    }

    // 设置选项胶囊 (自适应药丸 + ToolTip 提示)
    component SettingPill: Rectangle {
        id: sPill
        property string text: ""
        property string toolTip: ""
        property bool isSelected: false
        signal clicked()

        implicitWidth: spText.implicitWidth + 22
        implicitHeight: 26
        radius: 13
        color: sPill.isSelected ? FluTheme.primaryColor : (spMa.containsMouse ? FluTheme.surfaceHover : FluTheme.surfaceBg)
        border.color: sPill.isSelected ? "transparent" : (spMa.containsMouse ? Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g, FluTheme.primaryColor.b, 0.4) : FluTheme.cardBorder)
        border.width: 1

        scale: spMa.pressed ? 0.95 : 1.0
        Behavior on scale { NumberAnimation { duration: FluTheme.durationFast; easing.type: FluTheme.easingStandard } }
        Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
        Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

        Text {
            id: spText
            anchors.centerIn: parent
            text: sPill.text
            color: sPill.isSelected ? "#FFFFFF" : FluTheme.textPrimary
            font.pixelSize: FluTheme.fontSizeCaption
            font.bold: sPill.isSelected
            font.family: page.fontRound
        }

        MouseArea {
            id: spMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: sPill.clicked()

            ToolTip.visible: sPill.toolTip !== "" && containsMouse
            ToolTip.text: sPill.toolTip
            ToolTip.delay: 350
        }
    }

    // 主题色选择胶囊 (纯色圆点 + 外描边 + ToolTip 提示)
    component ColorPill: Rectangle {
        id: cPill
        property color pillColor: "#FF1E42"
        property string toolTip: ""
        property bool isSelected: false
        signal clicked()

        implicitWidth: 26
        implicitHeight: 26
        radius: 13
        color: "transparent"

        scale: cpMa.pressed ? 0.94 : 1.0
        Behavior on scale { NumberAnimation { duration: FluTheme.durationFast; easing.type: FluTheme.easingStandard } }

        Rectangle {
            anchors.centerIn: parent
            width: 16
            height: 16
            radius: 8
            color: cPill.pillColor
        }

        Rectangle {
            anchors.fill: parent
            radius: 13
            color: "transparent"
            border.color: cPill.isSelected ? cPill.pillColor : FluTheme.cardBorder
            border.width: cPill.isSelected ? 2 : 1
        }

        MouseArea {
            id: cpMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: cPill.clicked()

            ToolTip.visible: cPill.toolTip !== "" && containsMouse
            ToolTip.text: cPill.toolTip
            ToolTip.delay: 350
        }
    }
}