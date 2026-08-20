import QtQuick

Item {
    id: ring
    width: 24
    height: 24

    property color color: FluTheme.primaryColor
    property real strokeWidth: 3

    Canvas {
        id: canvas
        anchors.fill: parent
        renderTarget: Canvas.FramebufferObject

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = ring.color
            ctx.lineWidth = ring.strokeWidth
            ctx.lineCap = "round"

            var centerX = width / 2
            var centerY = height / 2
            var radius = Math.min(centerX, centerY) - ring.strokeWidth

            ctx.beginPath()
            ctx.arc(centerX, centerY, radius, 0, 1.5 * Math.PI)
            ctx.stroke()
        }
    }

    RotationAnimator {
        target: ring
        from: 0
        to: 360
        duration: 800
        loops: Animation.Infinite
        running: ring.visible
    }
}
