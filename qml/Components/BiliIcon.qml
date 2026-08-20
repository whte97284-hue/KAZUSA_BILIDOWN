import QtQuick

Item {
    id: iconItem
    width: 20
    height: 20

    property string name: "search" // search, download, list, chat, sun, moon, settings, clear, user, check
    property color color: FluTheme.textPrimary
    property real iconSize: 18

    Canvas {
        id: canvas
        anchors.fill: parent
        renderTarget: Canvas.FramebufferObject

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = iconItem.color
            ctx.fillStyle = iconItem.color

            var s = Math.min(width, height)
            var cx = width / 2
            var cy = height / 2

            ctx.lineWidth = Math.max(1.3, s * 0.09)
            ctx.lineCap = "round"
            ctx.lineJoin = "round"

            if (iconItem.name === "search") {
                // 完美自适应矢量放大镜 (四周 10% 安全留白，绝对不削角)
                var r = s * 0.25
                var ox = cx - s * 0.08
                var oy = cy - s * 0.08

                ctx.beginPath()
                ctx.arc(ox, oy, r, 0, 2 * Math.PI)
                ctx.stroke()

                var hx1 = ox + r * 0.707
                var hy1 = oy + r * 0.707
                var hx2 = cx + s * 0.35
                var hy2 = cy + s * 0.35

                ctx.beginPath()
                ctx.moveTo(hx1, hy1)
                ctx.lineTo(hx2, hy2)
                ctx.stroke()
            } else if (iconItem.name === "download") {
                // 下载托盘与向下箭头
                ctx.beginPath()
                ctx.moveTo(cx, cy - s * 0.3)
                ctx.lineTo(cx, cy + s * 0.15)
                ctx.moveTo(cx - s * 0.18, cy - s * 0.03)
                ctx.lineTo(cx, cy + s * 0.15)
                ctx.lineTo(cx + s * 0.18, cy - s * 0.03)
                ctx.stroke()

                ctx.beginPath()
                ctx.moveTo(cx - s * 0.3, cy + s * 0.28)
                ctx.lineTo(cx + s * 0.3, cy + s * 0.28)
                ctx.stroke()
            } else if (iconItem.name === "list") {
                // 剧集/多P卡片列表
                var box = s * 0.6
                ctx.beginPath()
                ctx.rect(cx - box / 2, cy - box / 2, box, box)
                ctx.stroke()

                ctx.beginPath()
                ctx.moveTo(cx - s * 0.16, cy - s * 0.1)
                ctx.lineTo(cx + s * 0.16, cy - s * 0.1)
                ctx.moveTo(cx - s * 0.16, cy + s * 0.1)
                ctx.lineTo(cx + s * 0.16, cy + s * 0.1)
                ctx.stroke()
            } else if (iconItem.name === "chat") {
                // 弹幕对话气泡
                ctx.beginPath()
                ctx.arc(cx, cy - s * 0.05, s * 0.3, 0.2 * Math.PI, 1.8 * Math.PI)
                ctx.lineTo(cx + s * 0.3, cy + s * 0.26)
                ctx.closePath()
                ctx.stroke()
            } else if (iconItem.name === "sun") {
                // 太阳
                ctx.beginPath()
                ctx.arc(cx, cy, s * 0.18, 0, 2 * Math.PI)
                ctx.stroke()
                for (var i = 0; i < 8; i++) {
                    var angle = i * Math.PI / 4
                    ctx.beginPath()
                    ctx.moveTo(cx + Math.cos(angle) * s * 0.28, cy + Math.sin(angle) * s * 0.28)
                    ctx.lineTo(cx + Math.cos(angle) * s * 0.38, cy + Math.sin(angle) * s * 0.38)
                    ctx.stroke()
                }
            } else if (iconItem.name === "moon") {
                // 月亮
                ctx.beginPath()
                ctx.arc(cx, cy, s * 0.3, 0.5 * Math.PI, 1.5 * Math.PI)
                ctx.arc(cx - s * 0.08, cy, s * 0.24, 1.5 * Math.PI, 0.5 * Math.PI, true)
                ctx.closePath()
                ctx.stroke()
            } else if (iconItem.name === "settings") {
                // 齿轮
                ctx.beginPath()
                ctx.arc(cx, cy, s * 0.16, 0, 2 * Math.PI)
                ctx.stroke()
                ctx.beginPath()
                ctx.arc(cx, cy, s * 0.32, 0, 2 * Math.PI)
                ctx.stroke()
            } else if (iconItem.name === "clear") {
                // X
                var cl = s * 0.22
                ctx.beginPath()
                ctx.moveTo(cx - cl, cy - cl)
                ctx.lineTo(cx + cl, cy + cl)
                ctx.moveTo(cx + cl, cy - cl)
                ctx.lineTo(cx - cl, cy + cl)
                ctx.stroke()
            } else if (iconItem.name === "back") {
                // 返回左箭头
                var b1 = s * 0.15
                var b2 = s * 0.26
                ctx.beginPath()
                ctx.moveTo(cx + b1, cy - b2)
                ctx.lineTo(cx - b1, cy)
                ctx.lineTo(cx + b1, cy + b2)
                ctx.stroke()
            } else if (iconItem.name === "refresh") {
                // 刷新圆弧箭头
                ctx.beginPath()
                ctx.arc(cx, cy, s * 0.26, -0.3 * Math.PI, 1.3 * Math.PI)
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(cx + s * 0.24, cy - s * 0.24)
                ctx.lineTo(cx + s * 0.24, cy - s * 0.05)
                ctx.lineTo(cx + s * 0.05, cy - s * 0.05)
                ctx.stroke()
            } else if (iconItem.name === "star") {
                // 五角星
                ctx.beginPath()
                for (var st = 0; st < 5; st++) {
                    var outA = (st * 4 * Math.PI / 5) - Math.PI / 2
                    var inA = outA + Math.PI / 5
                    var ox_s = cx + Math.cos(outA) * s * 0.32
                    var oy_s = cy + Math.sin(outA) * s * 0.32
                    var ix_s = cx + Math.cos(inA) * s * 0.14
                    var iy_s = cy + Math.sin(inA) * s * 0.14
                    if (st === 0) ctx.moveTo(ox_s, oy_s)
                    else ctx.lineTo(ox_s, oy_s)
                    ctx.lineTo(ix_s, iy_s)
                }
                ctx.closePath()
                ctx.fill()
            } else if (iconItem.name === "folder") {
                // 文件夹
                ctx.beginPath()
                ctx.moveTo(cx - s * 0.32, cy - s * 0.22)
                ctx.lineTo(cx - s * 0.1, cy - s * 0.22)
                ctx.lineTo(cx, cy - s * 0.1)
                ctx.lineTo(cx + s * 0.32, cy - s * 0.1)
                ctx.lineTo(cx + s * 0.32, cy + s * 0.26)
                ctx.lineTo(cx - s * 0.32, cy + s * 0.26)
                ctx.closePath()
                ctx.stroke()
            } else if (iconItem.name === "play") {
                // 播放实心三角
                ctx.beginPath()
                ctx.moveTo(cx - s * 0.18, cy - s * 0.26)
                ctx.lineTo(cx + s * 0.28, cy)
                ctx.lineTo(cx - s * 0.18, cy + s * 0.26)
                ctx.closePath()
                ctx.fill()
            } else if (iconItem.name === "pause") {
                // 暂停双竖条
                ctx.beginPath()
                ctx.moveTo(cx - s * 0.18, cy - s * 0.26)
                ctx.lineTo(cx - s * 0.18, cy + s * 0.26)
                ctx.moveTo(cx + s * 0.18, cy - s * 0.26)
                ctx.lineTo(cx + s * 0.18, cy + s * 0.26)
                ctx.stroke()
            } else if (iconItem.name === "trash") {
                // 垃圾桶
                ctx.beginPath()
                ctx.moveTo(cx - s * 0.26, cy - s * 0.16)
                ctx.lineTo(cx + s * 0.26, cy - s * 0.16)
                ctx.moveTo(cx - s * 0.1, cy - s * 0.26)
                ctx.lineTo(cx + s * 0.1, cy - s * 0.26)
                ctx.moveTo(cx - s * 0.2, cy - s * 0.16)
                ctx.lineTo(cx - s * 0.16, cy + s * 0.26)
                ctx.lineTo(cx + s * 0.16, cy + s * 0.26)
                ctx.lineTo(cx + s * 0.2, cy - s * 0.16)
                ctx.stroke()
            } else if (iconItem.name === "video") {
                // 视频/摄影机
                ctx.beginPath()
                ctx.rect(cx - s * 0.32, cy - s * 0.2, s * 0.42, s * 0.42)
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(cx + s * 0.1, cy - s * 0.1)
                ctx.lineTo(cx + s * 0.32, cy - s * 0.22)
                ctx.lineTo(cx + s * 0.32, cy + s * 0.22)
                ctx.lineTo(cx + s * 0.1, cy + s * 0.1)
                ctx.closePath()
                ctx.stroke()
            } else if (iconItem.name === "music") {
                // 音符
                ctx.beginPath()
                ctx.arc(cx - s * 0.12, cy + s * 0.16, s * 0.11, 0, 2 * Math.PI)
                ctx.fill()
                ctx.beginPath()
                ctx.moveTo(cx - s * 0.01, cy + s * 0.16)
                ctx.lineTo(cx - s * 0.01, cy - s * 0.26)
                ctx.lineTo(cx + s * 0.22, cy - s * 0.16)
                ctx.lineTo(cx + s * 0.22, cy + s * 0.02)
                ctx.stroke()
            } else if (iconItem.name === "image") {
                // 图片相框
                var iw = s * 0.62
                var ih = s * 0.52
                ctx.beginPath()
                ctx.rect(cx - iw / 2, cy - ih / 2, iw, ih)
                ctx.stroke()
                ctx.beginPath()
                ctx.arc(cx - s * 0.1, cy - s * 0.08, s * 0.06, 0, 2 * Math.PI)
                ctx.fill()
            } else if (iconItem.name === "check") {
                // 对勾
                ctx.beginPath()
                ctx.moveTo(cx - s * 0.26, cy)
                ctx.lineTo(cx - s * 0.08, cy + s * 0.18)
                ctx.lineTo(cx + s * 0.26, cy - s * 0.2)
                ctx.stroke()
            } else if (iconItem.name === "link") {
                // 链接/链条
                ctx.beginPath()
                ctx.moveTo(cx - s * 0.06, cy + s * 0.06)
                ctx.lineTo(cx + s * 0.06, cy - s * 0.06)
                ctx.stroke()
                ctx.beginPath()
                ctx.arc(cx - s * 0.12, cy + s * 0.12, s * 0.14, 0.75 * Math.PI, 1.75 * Math.PI)
                ctx.stroke()
                ctx.beginPath()
                ctx.arc(cx + s * 0.12, cy - s * 0.12, s * 0.14, -0.25 * Math.PI, 0.75 * Math.PI)
                ctx.stroke()
            } else if (iconItem.name === "github") {
                // GitHub 矢量轮廓 (猫耳圆环)
                ctx.beginPath()
                ctx.arc(cx, cy, s * 0.30, 0, 2 * Math.PI)
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(cx - s * 0.20, cy - s * 0.20)
                ctx.lineTo(cx - s * 0.14, cy - s * 0.32)
                ctx.lineTo(cx - s * 0.06, cy - s * 0.28)
                ctx.moveTo(cx + s * 0.06, cy - s * 0.28)
                ctx.lineTo(cx + s * 0.14, cy - s * 0.32)
                ctx.lineTo(cx + s * 0.20, cy - s * 0.20)
                ctx.stroke()
            } else if (iconItem.name === "globe") {
                // 经纬度地球
                ctx.beginPath()
                ctx.arc(cx, cy, s * 0.30, 0, 2 * Math.PI)
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(cx - s * 0.30, cy)
                ctx.lineTo(cx + s * 0.30, cy)
                ctx.stroke()
                ctx.beginPath()
                ctx.ellipse(cx - s * 0.15, cy - s * 0.30, s * 0.30, s * 0.60)
                ctx.stroke()
            } else if (iconItem.name === "heart") {
                // 爱心
                var hr = s * 0.14
                ctx.beginPath()
                ctx.arc(cx - hr, cy - hr * 0.6, hr, Math.PI, 0, false)
                ctx.arc(cx + hr, cy - hr * 0.6, hr, Math.PI, 0, false)
                ctx.lineTo(cx, cy + s * 0.30)
                ctx.closePath()
                ctx.fill()
            } else if (iconItem.name === "code") {
                // 代码 </>
                var cd = s * 0.20
                ctx.beginPath()
                ctx.moveTo(cx - cd * 0.5, cy - cd)
                ctx.lineTo(cx - cd * 1.3, cy)
                ctx.lineTo(cx - cd * 0.5, cy + cd)
                ctx.moveTo(cx + cd * 0.5, cy - cd)
                ctx.lineTo(cx + cd * 1.3, cy)
                ctx.lineTo(cx + cd * 0.5, cy + cd)
                ctx.stroke()
            } else if (iconItem.name === "external_link") {
                // ↗ 外链方框折线箭头 (现代极简 1:1 矢量)
                var box = s * 0.28
                ctx.beginPath()
                // 左下开口方框: 从 (cx + box*0.2, cy + box) -> (cx - box, cy + box) -> (cx - box, cy - box) -> (cx - box*0.2, cy - box)
                ctx.moveTo(cx + box * 0.3, cy + box)
                ctx.lineTo(cx - box, cy + box)
                ctx.lineTo(cx - box, cy - box)
                ctx.lineTo(cx - box * 0.3, cy - box)
                ctx.stroke()

                // 东北向引出箭头
                var ax1 = cx - box * 0.2
                var ay1 = cy + box * 0.2
                var ax2 = cx + box
                var ay2 = cy - box
                ctx.beginPath()
                ctx.moveTo(ax1, ay1)
                ctx.lineTo(ax2, ay2)
                // 箭头两翼
                ctx.lineTo(ax2 - box * 0.6, ay2)
                ctx.moveTo(ax2, ay2)
                ctx.lineTo(ax2, ay2 + box * 0.6)
                ctx.stroke()
            }
        }
    }

    onColorChanged: canvas.requestPaint()
    onNameChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
}