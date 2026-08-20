import QtQuick

Rectangle {
    id: splashRoot
    anchors.fill: parent
    z: 9999
    color: FluTheme.windowBg
    visible: opacity > 0.0
    opacity: 1.0

    // 幕布状态：0=Idle, 1=Covering(淡入中), 2=Covered(100%完全遮挡), 3=Revealing(淡出中)
    property int transitionPhase: 0
    property bool pendingFinish: false

    signal covered()

    // 阶段 1：开始拉上幕布 (平滑淡入遮盖旧界面)
    function startParseTransition() {
        if (fadeOutAnim.running) fadeOutAnim.stop()
        if (finishAnim.running) finishAnim.stop()
        splashRoot.visible = true
        transitionPhase = 1 // Covering
        pendingFinish = false
        fadeInAnim.restart()
    }

    // 阶段 2/3：准备拉开幕布 (已收到数据解析完成信号)
    function finishParseTransition() {
        if (transitionPhase === 2 && !holdTimer.running) {
            // 幕布已经 100% 遮挡且驻留期已过，直接执行淡出
            executeReveal()
        } else {
            // 幕布还在淡入或仍在驻留保护期，记录挂起标志，等待 holdTimer 结束后自动拉开
            pendingFinish = true
        }
    }

    function executeReveal() {
        transitionPhase = 3 // Revealing
        pendingFinish = false
        if (fadeInAnim.running) fadeInAnim.stop()
        if (fadeOutAnim.running) fadeOutAnim.stop()
        finishAnim.restart()
    }

    // 点击直接快速跳过动效
    MouseArea {
        anchors.fill: parent
        enabled: splashRoot.visible
        onClicked: {
            if (fadeInAnim.running) fadeInAnim.complete()
            if (finishAnim.running) finishAnim.complete()
            if (fadeOutAnim.running) fadeOutAnim.complete()
        }
    }

    // 正中央 100% 原生无损 PNG 高清图标 (GPU 硬件纹理点对点直采, 绝不模糊)
    Image {
        id: pureIcon
        anchors.centerIn: parent
        width: 96
        height: 96
        source: "qrc:/kazusa.png"
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        asynchronous: false
        scale: 1.0

        // 解析中轻微呼吸微动效 (0.98 ~ 1.03 柔和脉冲)
        SequentialAnimation on scale {
            id: pulseAnim
            running: splashRoot.visible && (splashRoot.transitionPhase === 1 || splashRoot.transitionPhase === 2 || splashRoot.opacity > 0.8)
            loops: Animation.Infinite
            alwaysRunToEnd: false
            NumberAnimation { to: 1.03; duration: 600; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.98; duration: 600; easing.type: Easing.InOutSine }
        }
    }

    // 1. 开屏初始淡出动画 (App 启动时执行)
    SequentialAnimation {
        id: fadeOutAnim
        running: true

        PauseAnimation { duration: 140 }

        NumberAnimation {
            target: splashRoot
            property: "opacity"
            from: 1.0
            to: 0.0
            duration: 380
            easing.type: Easing.InOutQuad
        }

        ScriptAction {
            script: {
                splashRoot.visible = false
                splashRoot.transitionPhase = 0
                pureIcon.scale = 1.0
            }
        }
    }

    // 2. 阶段 1：拉上幕布淡入动画 (0.0 -> 1.0)
    SequentialAnimation {
        id: fadeInAnim
        running: false

        NumberAnimation {
            target: splashRoot
            property: "opacity"
            to: 1.0
            duration: 180
            easing.type: Easing.OutCubic
        }

        ScriptAction {
            script: {
                // 此时遮罩已 100% 不透明完全遮蔽旧视图，触发幕后换景
                splashRoot.transitionPhase = 2 // Covered
                splashRoot.covered()
                holdTimer.restart()
            }
        }
    }

    // 幕后换景驻留计时器 (确保在 100% 遮罩下底层完成排版与首帧渲染)
    Timer {
        id: holdTimer
        interval: 160 // 160ms 黄金静止期
        repeat: false
        onTriggered: {
            if (splashRoot.pendingFinish) {
                splashRoot.executeReveal()
            }
        }
    }

    // 3. 阶段 3：拉开幕布平滑淡出动画 (1.0 -> 0.0)
    SequentialAnimation {
        id: finishAnim
        running: false

        NumberAnimation {
            target: splashRoot
            property: "opacity"
            from: 1.0
            to: 0.0
            duration: 360
            easing.type: Easing.InOutQuad
        }

        ScriptAction {
            script: {
                splashRoot.visible = false
                splashRoot.transitionPhase = 0
                pureIcon.scale = 1.0
            }
        }
    }
}
