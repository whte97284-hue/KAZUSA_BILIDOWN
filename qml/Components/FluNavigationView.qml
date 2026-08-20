import QtQuick
import QtQuick.Controls

Rectangle {
    id: navRail
    width: 58
    color: FluTheme.navBg
    border.color: FluTheme.cardBorder
    border.width: 1

    Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
    Behavior on border.color { ColorAnimation { duration: FluTheme.durationFast } }

    property int currentIndex: 0
    signal itemClicked(int index)
    signal loginClicked()
    signal themeToggleClicked()
    signal settingsClicked()

    // 顶部用户头像 / 登录入口
    Item {
        id: avatarBtn
        width: 38
        height: 38
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 14

        scale: loginMa.pressed ? 0.94 : 1.0
        Behavior on scale { NumberAnimation { duration: FluTheme.durationFast; easing.type: Easing.OutCubic } }

        BiliAvatar {
            anchors.fill: parent
            size: 38
            source: biliController.isLogin ? biliController.userAvatar : ""
            maskColor: navRail.color
        }

        MouseArea {
            id: loginMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: navRail.loginClicked()

            ToolTip.visible: containsMouse
            ToolTip.text: biliController.isLogin ? (biliController.userName + " (已登录)") : "点击扫码登录 B站账号"
            ToolTip.delay: 350
        }
    }

    // 中间导航栏目 (解析 / 批量 / 历史)
    Item {
        id: navContainer
        anchors.top: avatarBtn.bottom
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        width: 38
        height: 38 * 3 + 12 * 2

        // 全局平滑纵向浮动激活胶囊指示器 (像 iOS / Fluent 一样顺滑滑移)
        Rectangle {
            id: activeBar
            width: 3
            height: 16
            radius: 1.5
            color: FluTheme.primaryColor
            anchors.left: parent.left
            anchors.leftMargin: -7
            visible: navRail.currentIndex >= 0 && navRail.currentIndex <= 2

            y: {
                if (navRail.currentIndex === 0) return 11
                if (navRail.currentIndex === 1) return 38 + 12 + 11
                if (navRail.currentIndex === 2) return (38 + 12) * 2 + 11
                return 11
            }

            Behavior on y {
                NumberAnimation {
                    duration: 200
                    easing.type: Easing.OutCubic
                }
            }
        }

        Column {
            anchors.fill: parent
            spacing: 12

            // 1. 视频 / 番剧解析
            NavItem {
                iconName: "search"
                toolTip: "视频 / 番剧解析"
                isActive: navRail.currentIndex === 0
                onClicked: navRail.itemClicked(0)
            }

            // 2. 任务列表 / 下载管理
            NavItem {
                iconName: "list"
                toolTip: "下载任务管理"
                isActive: navRail.currentIndex === 1
                badgeActive: biliController.downloadingCount > 0
                onClicked: navRail.itemClicked(1)
            }

            // 3. 下载历史时光隧道
            NavItem {
                iconName: "download"
                toolTip: "下载历史记录"
                isActive: navRail.currentIndex === 2
                onClicked: navRail.itemClicked(2)
            }
        }
    }

    // 底部工具 (主题切换 + 设置)
    Column {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 10

        // 主题切换 (日 / 月 · 360° 平滑弹性旋转)
        Rectangle {
            width: 38
            height: 38
            radius: 8
            color: themeMa.containsMouse ? FluTheme.surfaceHover : "transparent"
            anchors.horizontalCenter: parent.horizontalCenter

            scale: themeMa.pressed ? 0.94 : 1.0
            Behavior on scale { NumberAnimation { duration: FluTheme.durationFast; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }

            BiliIcon {
                id: themeIcon
                anchors.centerIn: parent
                name: FluTheme.darkMode === 1 ? "moon" : "sun"
                color: themeMa.containsMouse ? FluTheme.primaryColor : FluTheme.textPrimary
                width: 17
                height: 17

                property real iconRotation: 0
                rotation: iconRotation
                Behavior on rotation { NumberAnimation { duration: 320; easing.type: Easing.OutBack } }
                Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
            }

            MouseArea {
                id: themeMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    themeIcon.iconRotation += 360
                    navRail.themeToggleClicked()
                }

                ToolTip.visible: containsMouse
                ToolTip.text: FluTheme.darkMode === 1 ? "切换为日间明亮模式" : "切换为极夜深色模式"
                ToolTip.delay: 350
            }
        }

        // 设置入口
        Rectangle {
            id: settingsBtn
            width: 38
            height: 38
            radius: 8
            color: navRail.currentIndex === 3 ? Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g, FluTheme.primaryColor.b, 0.12) : (settingsMa.containsMouse ? FluTheme.surfaceHover : "transparent")
            anchors.horizontalCenter: parent.horizontalCenter

            scale: settingsMa.pressed ? 0.94 : 1.0
            Behavior on scale { NumberAnimation { duration: FluTheme.durationFast; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }

            BiliIcon {
                id: settingsIcon
                anchors.centerIn: parent
                name: "settings"
                color: navRail.currentIndex === 3 ? FluTheme.primaryColor : (settingsMa.containsMouse ? FluTheme.primaryColor : FluTheme.textPrimary)
                width: 17
                height: 17

                property real iconRotation: 0
                rotation: iconRotation
                Behavior on rotation { NumberAnimation { duration: 360; easing.type: Easing.OutCubic } }
                Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }
            }

            MouseArea {
                id: settingsMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    settingsIcon.iconRotation += 180
                    navRail.settingsClicked()
                }

                ToolTip.visible: containsMouse
                ToolTip.text: "偏好设置与系统状态"
                ToolTip.delay: 350
            }
        }
    }

    // 独立可复用导航单项 (规整 38x38，零多余位移，纯净触觉反馈)
    component NavItem: Rectangle {
        id: navItemRoot
        property string iconName: ""
        property string toolTip: ""
        property bool isActive: false
        property bool badgeActive: false
        signal clicked()

        width: 38
        height: 38
        radius: 8
        color: isActive ? Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g, FluTheme.primaryColor.b, 0.12) : (itemMa.containsMouse ? FluTheme.surfaceHover : "transparent")
        anchors.horizontalCenter: parent.horizontalCenter

        scale: itemMa.pressed ? 0.94 : 1.0
        Behavior on scale { NumberAnimation { duration: FluTheme.durationFast; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }

        BiliIcon {
            id: innerIcon
            anchors.centerIn: parent
            name: iconName
            color: navItemRoot.isActive ? FluTheme.primaryColor : (itemMa.containsMouse ? FluTheme.textPrimary : FluTheme.textSecondary)
            width: 18
            height: 18
            Behavior on color { ColorAnimation { duration: FluTheme.durationFast } }

            property real bounceScale: 1.0
            scale: bounceScale
        }

        // 活跃指示小红点 (呼吸脉冲)
        Rectangle {
            visible: badgeActive
            width: 6
            height: 6
            radius: 3
            color: FluTheme.primaryColor
            anchors.top: parent.top
            anchors.topMargin: 6
            anchors.right: parent.right
            anchors.rightMargin: 6
        }

        SequentialAnimation {
            id: clickBounceAnim
            NumberAnimation { target: innerIcon; property: "bounceScale"; to: 0.86; duration: 50; easing.type: Easing.OutQuad }
            NumberAnimation { target: innerIcon; property: "bounceScale"; to: 1.12; duration: 80; easing.type: Easing.OutBack }
            NumberAnimation { target: innerIcon; property: "bounceScale"; to: 1.0; duration: 70; easing.type: Easing.OutCubic }
        }

        MouseArea {
            id: itemMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                clickBounceAnim.restart()
                biliController.playClickSound() // 跨页导航点击音
                navItemRoot.clicked()
            }

            ToolTip.visible: navItemRoot.toolTip !== "" && containsMouse
            ToolTip.text: navItemRoot.toolTip
            ToolTip.delay: 350
        }
    }
}