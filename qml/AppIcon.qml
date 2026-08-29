import QtQuick

/// Small monochrome line-art icon (O3 from the UI audit), drawn on a 24px grid so
/// it stays crisp and fully theme-recolourable — unlike the emoji glyphs (🚆 ✕ ✓)
/// it replaces, which render multicolour/inconsistently and can't be tinted.
/// Usage: AppIcon { name: "close"; color: Theme.textMuted; size: 14 }
Canvas {
    id: root

    property string name: "train"       // "train" | "close" | "check" | "chevron"
    property color color: "black"
    property real size: 16

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    onColorChanged: requestPaint()
    onNameChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const s = width / 24            // design grid is 24×24
        ctx.strokeStyle = root.color
        ctx.fillStyle = root.color
        ctx.lineWidth = 2 * s
        ctx.lineCap = "round"
        ctx.lineJoin = "round"

        if (name === "close") {
            ctx.beginPath()
            ctx.moveTo(6 * s, 6 * s);  ctx.lineTo(18 * s, 18 * s)
            ctx.moveTo(18 * s, 6 * s); ctx.lineTo(6 * s, 18 * s)
            ctx.stroke()
        } else if (name === "chevron") {
            // Points down; callers rotate 180° for "up" (see CollapseButton).
            ctx.beginPath()
            ctx.moveTo(7 * s, 10 * s)
            ctx.lineTo(12 * s, 15 * s)
            ctx.lineTo(17 * s, 10 * s)
            ctx.stroke()
        } else if (name === "check") {
            ctx.beginPath()
            ctx.moveTo(5 * s, 13 * s)
            ctx.lineTo(10 * s, 18 * s)
            ctx.lineTo(19 * s, 6 * s)
            ctx.stroke()
        } else {                         // "train" — side line-art locomotive
            // body
            ctx.beginPath()
            ctx.roundedRect(4 * s, 4 * s, 16 * s, 12 * s, 3 * s, 3 * s)
            ctx.stroke()
            // window band
            ctx.beginPath()
            ctx.moveTo(4 * s, 9 * s)
            ctx.lineTo(20 * s, 9 * s)
            ctx.stroke()
            // wheels
            ctx.beginPath()
            ctx.arc(9 * s, 19 * s, 1.6 * s, 0, 2 * Math.PI)
            ctx.arc(15 * s, 19 * s, 1.6 * s, 0, 2 * Math.PI)
            ctx.fill()
            // rails of the cow-catcher / base
            ctx.beginPath()
            ctx.moveTo(5 * s, 16 * s);  ctx.lineTo(7 * s, 16 * s)
            ctx.moveTo(17 * s, 16 * s); ctx.lineTo(19 * s, 16 * s)
            ctx.stroke()
        }
    }
}
