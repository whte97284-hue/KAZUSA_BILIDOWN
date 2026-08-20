import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "Components"
import "Pages"

Window {
    id: rootWindow
    title: "KAZUSA BILIDOWN"
    // 折中尺寸: 介于 640×480 迷你态与 1120×720 大窗之间, 兼顾观感与内容展示
    width: 900
    height: 600
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinimizeButtonHint
    color: FluTheme.windowBg

    // 外层容器 (自适应明暗模式)
    Rectangle {
        id: bgContainer
        anchors.fill: parent
        color: FluTheme.windowBg
        border.color: FluTheme.cardBorder
        border.width: 1

        Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
        Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

        Item {
            anchors.fill: parent

            // ================= 1. 左侧一体化极简导航轨 (顶天立地，对齐参考图) =================
            FluNavigationView {
                id: rootNav
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 58
                currentIndex: pageStack.currentIndex

                onItemClicked: function(idx) {
                    pageStack.currentIndex = idx
                }

                onSettingsClicked: function() {
                    pageStack.currentIndex = 3
                }

                onThemeToggleClicked: function() {
                    FluTheme.darkMode = (FluTheme.darkMode === 1 ? 0 : 1)
                }

                onLoginClicked: function() {
                    qrDialog.open()
                    if (!biliController.isLogin) {
                        if (biliController.qrCodeUrl === "" || biliController.qrState === 5) {
                            biliController.requestQrCode()
                        }
                        qrPollTimer.restart()
                    }
                }
            }

            // ================= 2. 顶部无缝拖拽与右上角极简三键 =================
            FluTitleBar {
                id: customTitleBar
                anchors.top: parent.top
                anchors.left: rootNav.right
                anchors.right: parent.right
                height: 32
                z: 10
                targetWindow: rootWindow
            }

            // ================= 3. 右侧主内容展示区 =================
            StackLayout {
                id: pageStack
                objectName: "pageStack"
                anchors.left: rootNav.right
                anchors.right: parent.right
                anchors.top: customTitleBar.bottom
                anchors.bottom: parent.bottom
                currentIndex: 0
                clip: true

                PageBangumi {}
                PageDownload {}
                PageHistory {}
                PageSettings {}
            }
        }
    }

    // ================= 0. 极速开屏微呼吸动效 (420ms 仪式感无缝入场) =================
    FluSplashScreen {
        id: appSplashScreen
    }

    // ================= 5. 全局极简 Toast 提示 (会员专享警告等) =================
    FluToast {
        id: globalToast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        z: 999
    }

    Connections {
        target: biliController
        function onVipLockNotice(text) {
            globalToast.show(text, FluTheme.warning)
        }
    }

    // ================= 4. 无边框窗口四周拉伸热区 =================
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        width: 5
        cursorShape: Qt.SizeHorCursor
        onPressed: rootWindow.startSystemResize(Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        width: 5
        cursorShape: Qt.SizeHorCursor
        onPressed: rootWindow.startSystemResize(Qt.RightEdge)
    }
    MouseArea {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        height: 5
        cursorShape: Qt.SizeVerCursor
        onPressed: rootWindow.startSystemResize(Qt.TopEdge)
    }
    MouseArea {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        height: 5
        cursorShape: Qt.SizeVerCursor
        onPressed: rootWindow.startSystemResize(Qt.BottomEdge)
    }
    MouseArea {
        anchors.top: parent.top
        anchors.left: parent.left
        width: 8
        height: 8
        cursorShape: Qt.SizeFDiagCursor
        onPressed: rootWindow.startSystemResize(Qt.TopEdge | Qt.LeftEdge)
    }
    MouseArea {
        anchors.top: parent.top
        anchors.right: parent.right
        width: 8
        height: 8
        cursorShape: Qt.SizeBDiagCursor
        onPressed: rootWindow.startSystemResize(Qt.TopEdge | Qt.RightEdge)
    }
    MouseArea {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: 8
        height: 8
        cursorShape: Qt.SizeBDiagCursor
        onPressed: rootWindow.startSystemResize(Qt.BottomEdge | Qt.LeftEdge)
    }
    MouseArea {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 8
        height: 8
        cursorShape: Qt.SizeFDiagCursor
        onPressed: rootWindow.startSystemResize(Qt.BottomEdge | Qt.RightEdge)
    }

    // ================= 5. 账号中心 / 扫码登录弹窗 (极简双态 · 零延迟预热) =================
    FluContentDialog {
        id: qrDialog
        title: ""
        dialogWidth: 260
        dialogHeight: biliController.isLogin ? 290 : 256

        onClosed: {
            qrPollTimer.stop()
        }

        // ================= 态 A: 已登录展示极简个人中心 =================
        Column {
            anchors.centerIn: parent
            visible: biliController.isLogin
            width: parent.width - 32
            spacing: 12

            // 64px 完美纯正圆形大头像，无任何冗余线框
            BiliAvatar {
                size: 64
                source: biliController.userAvatar
                maskColor: qrDialog.backgroundColor || FluTheme.windowBg
                anchors.horizontalCenter: parent.horizontalCenter
            }

            // 昵称与身份徽章 (纯净文本)
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 4

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: biliController.userName
                    color: FluTheme.textPrimary
                    font.pixelSize: 18
                    font.bold: true
                    font.family: FluTheme.fontTitle
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: {
                        var badges = []
                        if (biliController.vipLabel !== "") badges.push(biliController.vipLabel)
                        if (biliController.userLevel !== "") badges.push("Lv." + biliController.userLevel)
                        return badges.join(" · ")
                    }
                    color: FluTheme.textSecondary
                    font.pixelSize: 12
                    font.family: FluTheme.fontBody
                    visible: text !== ""
                }
            }

            // 硬币与 UID
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "硬币 " + biliController.userCoins.toFixed(0) + "   ·   UID " + (biliController.userMidStr !== "" ? biliController.userMidStr : biliController.userMid)
                color: FluTheme.textSecondary
                font.pixelSize: 12
                font.family: FluTheme.fontMono
            }

            // 极简登出按钮 (去除喧宾夺主的红底，改为浅灰边框次要按钮)
            FluPillButton {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "登出当前账号"
                isFilled: false
                themeColor: FluTheme.textSecondary
                onClicked: {
                    biliController.logout()
                    qrPollTimer.restart()
                }
            }
        }

        // ================= 态 B: 未登录展示极速预热二维码 =================
        Column {
            anchors.centerIn: parent
            visible: !biliController.isLogin
            spacing: 10

            // 二维码白底容器 (带过期遮罩与刷新交互)
            Rectangle {
                width: 160
                height: 160
                radius: FluTheme.radius
                color: "#FFFFFF"
                anchors.horizontalCenter: parent.horizontalCenter
                border.color: FluTheme.cardBorder
                border.width: 1
                clip: true

                Image {
                    anchors.fill: parent
                    anchors.margins: 6
                    // 零延迟预热: 启动时即在后台加载缓存图片，弹窗秒开无延迟
                    source: biliController.qrCodeUrl !== "" ? ("https://api.qrserver.com/v1/create-qr-code/?size=148x148&data=" + encodeURIComponent(biliController.qrCodeUrl)) : ""
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    visible: biliController.qrState === 2 || biliController.qrState === 3 || biliController.qrState === 4
                }

                // 已扫码待确认遮罩 (状态热更新视觉反馈)
                Rectangle {
                    anchors.fill: parent
                    color: Qt.rgba(0, 0, 0, 0.45)
                    visible: biliController.qrState === 3
                    radius: FluTheme.radius

                    Column {
                        anchors.centerIn: parent
                        spacing: 6

                        // 手机确认提示
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "✓"
                            color: "#FFFFFF"
                            font.pixelSize: 30
                            font.bold: true
                            font.family: FluTheme.fontTitle
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "请在手机上确认"
                            color: "#FFFFFF"
                            font.pixelSize: FluTheme.fontSizeCaption
                            font.family: FluTheme.fontBody
                        }
                    }
                }

                // 生成中加载动画
                FluProgressRing {
                    anchors.centerIn: parent
                    visible: biliController.qrState === 0 || biliController.qrState === 1
                    color: FluTheme.primaryColor
                }

                // 过期或错误遮罩
                Rectangle {
                    anchors.fill: parent
                    color: "#D9151515"
                    visible: biliController.qrState === 5 || biliController.qrState === 6

                    Column {
                        anchors.centerIn: parent
                        spacing: 8

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: biliController.qrState === 5 ? "二维码已过期" : "生成失败"
                            color: "#FFFFFF"
                            font.pixelSize: FluTheme.fontSizeCaption
                            font.family: FluTheme.fontBody
                        }

                        FluPillButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "点击刷新"
                            isFilled: true
                            themeColor: FluTheme.primaryColor
                            onClicked: {
                                biliController.requestQrCode()
                                qrPollTimer.restart()
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: (biliController.qrState === 5 || biliController.qrState === 6) ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        if (biliController.qrState === 5 || biliController.qrState === 6) {
                            biliController.requestQrCode()
                            qrPollTimer.restart()
                        }
                    }
                }
            }

            // 极简状态反馈文字
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: biliController.qrStatusText
                color: {
                    if (biliController.qrState === 3) return FluTheme.warning
                    if (biliController.qrState === 4) return FluTheme.success
                    if (biliController.qrState === 5 || biliController.qrState === 6) return FluTheme.danger
                    return FluTheme.textSecondary
                }
                font.pixelSize: FluTheme.fontSizeBody
                font.bold: biliController.qrState === 3 || biliController.qrState === 4
                font.family: FluTheme.fontBody
            }
        }
    }

    Timer {
        id: qrPollTimer
        interval: 2000
        repeat: true
        running: false
        onTriggered: {
            if (qrDialog.visible && !biliController.isLogin) {
                biliController.pollQrStatus()
            } else {
                qrPollTimer.stop()
            }
        }
    }

    Connections {
        target: appSplashScreen
        function onCovered() {
            // 幕布 100% 严密遮挡时在幕后安全切页，绝无画面抢跑
            if (biliController.pageCount > 0 || biliController.isBangumi) {
                pageStack.currentIndex = 0
            }
        }
    }

    Connections {
        target: biliController
        function onIsParsingChanged() {
            if (biliController.isParsing) {
                appSplashScreen.startParseTransition()
            }
        }
        function onParseSuccess() {
            appSplashScreen.finishParseTransition()
        }
        function onParseFailed(error) {
            appSplashScreen.finishParseTransition()
        }
        function onQrCodeGenerated() {
            // 二维码已在后台就绪
        }
        function onQrLoginSuccess() {
            qrPollTimer.stop()
        }
        function onRequestLoginDialog() {
            qrDialog.open()
            if (!biliController.isLogin) {
                if (biliController.qrCodeUrl === "" || biliController.qrState === 5) {
                    biliController.requestQrCode()
                }
                qrPollTimer.restart()
            }
        }
    }

    // ================= 6. 全局下载目录选择 =================
    FolderDialog {
        id: folderDialog
        title: "选择下载目录"
        onAccepted: {
            var url = folderDialog.selectedFolder.toString()
            var prefix = "file:///"
            if (url.startsWith(prefix)) {
                biliController.setDownloadDir(url.substring(prefix.length))
            } else {
                biliController.setDownloadDir(url)
            }
        }
    }

    Connections {
        target: biliController
        function onRequestFolderPicker() {
            folderDialog.open()
        }
    }
}
