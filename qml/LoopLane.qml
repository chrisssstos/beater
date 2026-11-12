import QtQuick
import HighlighterUI

// The track's loops as bars under the timeline, on the same scale: the intro,
// the In / Out loop and the outro (end loop). The one the deck loops right now
// is solid, one that's armed is half lit, one that's off is dim. With tags on,
// each loop has a name tag under it; click it to switch that loop on or off.
//  Drag an end to resize a loop, its middle to move it, or the empty lane to
//  draw an In / Out loop. Double-click a loop to reset it (the intro and end
//  back to automatic, the In / Out loop gone). Snaps to bars; Option for beats.
Item {
    id: lane
    required property DeckController deck
    readonly property color tint: Theme.accentAt(lane, Window.window)   // the deck's

    //! Name tags under the loops (click: on / off).
    property bool tags: false
    readonly property real laneHeight: tags ? Theme.s(16) : Theme.s(12)
    readonly property real tagHeight: Theme.s(18)
    implicitHeight: laneHeight + (tags ? Theme.s(3) + tagHeight : 0)

    readonly property bool ready: deck.loaded && deck.bpm > 0 && deck.duration > 0
    readonly property real handle: Theme.s(7)

    // Without a BPM (a dropped file still being analysed) there's no grid: 0,
    // never Infinity (an infinite width hangs Qt's renderer in the glow).
    function xAtBeat(b) { return ready ? (deck.firstBeat + b * 60 / deck.bpm) / deck.duration * width : 0 }
    function beatAtX(x) { return ready && width > 0 ? (x / width * deck.duration - deck.firstBeat) * deck.bpm / 60 : 0 }
    function snap(b, fine) { const q = fine ? 1 : 4; return Math.round(b / q) * q }
    // The bar lines (beats with Option) a loop may use: as BeatGrid's
    // first/lastLoopBeat, a bar cut short by under half a beat still counts.
    function firstBeat(fine) { const q = fine ? 1 : 4; return Math.ceil((beatAtX(0) - 0.5) / q) * q }
    function lastBeat(fine) { const q = fine ? 1 : 4; return Math.floor((beatAtX(width) + 0.5) / q) * q }

    // while dragging: which loop and where it's going
    property string editKind: ""
    property real editA: 0
    property real editB: 0

    Rectangle { width: parent.width; height: lane.laneHeight; color: Theme.well }

    component Region: Rectangle {
        id: r
        required property string kind   // "intro", "custom", "end"
        readonly property bool editing: lane.editKind === kind
        readonly property real a: editing ? lane.editA
                                : kind === "intro" ? lane.deck.introStartBeat
                                : kind === "custom" ? lane.deck.customStartBeat : lane.deck.endStartBeat
        readonly property real b: editing ? lane.editB
                                : kind === "intro" ? lane.deck.introEndBeat
                                : kind === "custom" ? lane.deck.customEndBeat : lane.deck.endEndBeat
        readonly property bool active: lane.deck.activeLoop === kind
        readonly property bool armed: kind === "intro" ? lane.deck.introArmed
                                    : kind === "custom" ? lane.deck.customOn : lane.deck.endArmed
        readonly property bool auto: kind === "intro" ? lane.deck.introAuto : kind === "end" ? lane.deck.endAuto : false

        visible: lane.ready && b > a
        x: lane.xAtBeat(a)
        width: Math.max(Theme.s(2), lane.xAtBeat(b) - x)
        height: lane.laneHeight
        z: active || editing ? 2 : kind === "custom" ? 1 : 0
        color: editing || active ? Theme.litOf(lane.tint) : lane.tint
        opacity: editing || active ? 1 : armed ? 0.55 : 0.22
        layer.enabled: Theme.glow && (r.active || r.editing) && isFinite(r.width)
        layer.effect: ParamGlow { w: r.width; h: r.height; glowColor: lane.tint; strength: 0.7 }
        // the ends you can grab
        Rectangle { width: Math.min(lane.handle, parent.width / 2); height: parent.height; color: Theme.edgeOf(r.color) }
        Rectangle { x: parent.width - width; width: Math.min(lane.handle, parent.width / 2); height: parent.height; color: Theme.edgeOf(r.color) }
        ParamText {
            // without tags (Perform), the bar says what it is
            anchors.centerIn: parent
            visible: !lane.tags && implicitWidth < parent.width - 2 * lane.handle - Theme.s(4)
            readonly property real beats: r.b - r.a
            readonly property string len: beats % 4 === 0 ? beats / 4 + (beats === 4 ? " bar" : " bars") : beats + " beats"
            text: r.kind === "intro" ? "intro · " + len : r.kind === "end" ? "outro · " + len : "in/out · " + len
            small: true
            color: Theme.inkOf(r.color)
            font.weight: Font.Bold
            wrapMode: Text.NoWrap
        }
    }

    Region { id: introRegion; kind: "intro" }
    Region { id: customRegion; kind: "custom" }
    Region { id: endRegion; kind: "end" }

    // an In placed, waiting for its Out: the loop so far, growing with the playhead
    Rectangle {
        id: pendingRegion
        readonly property string kind: "custom"
        readonly property bool active: true
        readonly property bool armed: true
        visible: lane.ready && lane.deck.customPending
        x: lane.xAtBeat(lane.deck.customStartBeat) - Theme.s(1)
        width: Math.max(Theme.s(3), lane.xAtBeat(lane.deck.beat) - x)
        height: lane.laneHeight
        color: Theme.litOf(lane.tint)
        opacity: 0.9
        z: 3
        layer.enabled: Theme.glow && isFinite(pendingRegion.width)
        layer.effect: ParamGlow { w: pendingRegion.width; h: pendingRegion.height; glowColor: lane.tint; strength: 0.7 }
    }

    // ---- name tags: solid while looping, outlined while armed, dim when off; click = on / off
    component Tag: Rectangle {
        id: t
        required property var region
        required property string text
        readonly property bool on: region.active
        readonly property bool armed: region.armed
        visible: lane.tags && region.visible
        y: lane.laneHeight + Theme.s(3)
        x: lane.tagX(t)
        width: label.implicitWidth + Theme.s(12)
        height: lane.tagHeight
        color: on ? Theme.litOf(lane.tint) : "transparent"
        border.width: 1
        border.color: on || armed ? lane.tint : Theme.line
        ParamText {
            id: label
            anchors.centerIn: parent
            text: t.text
            small: true
            color: t.on ? Theme.inkOf(lane.tint) : t.armed ? lane.tint : Theme.textFaint
            wrapMode: Text.NoWrap
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                const k = t.region.kind
                if (k === "intro") lane.deck.introArmed = !lane.deck.introArmed
                else if (k === "end") lane.deck.endArmed = !lane.deck.endArmed
                else lane.deck.customOn = !lane.deck.customOn
            }
        }
        z: 5
    }
    Tag { id: introTag; region: introRegion; text: "Intro" }
    Tag { id: customTag; region: customRegion; text: "In / Out" }
    Tag { id: pendingTag; region: pendingRegion; text: "In / Out" }
    Tag { id: endTag; region: endRegion; text: "Outro" }

    // Tags sit under their loop's start, left to right, and never cover each
    // other: one that would moves right, clear of the one before (and the
    // last ones move left to stay inside the lane).
    readonly property var tagPlaces: {
        const gap = Theme.s(4)
        const shown = [introTag, customTag, pendingTag, endTag]
            .filter(t => lane.tags && t.region.visible)
            .sort((a, b) => a.region.x - b.region.x)
        const xs = shown.map(t => Math.max(0, t.region.x))
        for (let i = 1; i < shown.length; ++i)
            xs[i] = Math.max(xs[i], xs[i - 1] + shown[i - 1].width + gap)
        // pushed past the end: pull back from the right
        let limit = lane.width
        for (let i = shown.length - 1; i >= 0; --i) {
            xs[i] = Math.max(0, Math.min(xs[i], limit - shown[i].width))
            limit = xs[i] - gap
        }
        return { order: shown, xs: xs }
    }
    function tagX(t) {
        const i = tagPlaces.order.indexOf(t)
        return i < 0 ? 0 : tagPlaces.xs[i]
    }

    MouseArea {
        id: ma
        width: parent.width
        height: lane.laneHeight
        z: 4
        enabled: lane.ready
        hoverEnabled: true
        preventStealing: true
        property string part: ""     // "start", "end", "move", "draw"
        property real pressBeat: 0
        property real fromA: 0
        property real fromB: 0

        // What's under x: the loop in use first, then the In / Out loop, the intro, the end.
        function hit(x) {
            const all = [introRegion, customRegion, endRegion]
            const order = all.filter(r => r.active).concat([customRegion, introRegion, endRegion].filter(r => !r.active))
            for (const r of order) {
                if (!r.visible) continue
                const x0 = r.x, x1 = r.x + r.width
                if (Math.abs(x - x0) <= lane.handle && x < x1 - 1) return { kind: r.kind, part: "start", r: r }
                if (Math.abs(x - x1) <= lane.handle) return { kind: r.kind, part: "end", r: r }
                if (x > x0 && x < x1) return { kind: r.kind, part: "move", r: r }
            }
            return { kind: "custom", part: "draw", r: null }
        }
        cursorShape: {
            const p = pressed ? part : hit(mouseX).part
            return p === "start" || p === "end" ? Qt.SizeHorCursor
                 : p === "move" ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
                 : Qt.CrossCursor
        }
        onPressed: m => {
            const h = hit(m.x)
            part = h.part
            pressBeat = lane.beatAtX(m.x)
            if (h.r) {
                fromA = h.r.a
                fromB = h.r.b
            } else {
                fromA = fromB = lane.snap(pressBeat, m.modifiers & Qt.AltModifier)
            }
            lane.editA = fromA
            lane.editB = fromB
            lane.editKind = h.kind
        }
        onPositionChanged: m => {
            if (!pressed) return
            const fine = m.modifiers & Qt.AltModifier
            const min = fine ? 1 : 4
            const lo = lane.firstBeat(fine), hi = lane.lastBeat(fine)
            const beat = Math.max(lo, Math.min(hi, lane.snap(lane.beatAtX(m.x), fine)))
            if (part === "start") {
                lane.editA = Math.min(beat, fromB - min)
            } else if (part === "end") {
                lane.editB = Math.max(beat, fromA + min)
            } else if (part === "move") {
                const len = fromB - fromA
                const d = lane.snap(lane.beatAtX(m.x) - pressBeat, fine)
                const s = Math.max(lo, Math.min(hi - len, fromA + d))
                lane.editA = s
                lane.editB = s + len
            } else {
                lane.editA = Math.min(fromA, beat)
                lane.editB = Math.max(fromA, beat)
            }
        }
        onReleased: {
            if (lane.editB - lane.editA >= 1 && (lane.editA !== fromA || lane.editB !== fromB)) {
                if (lane.editKind === "intro")
                    lane.deck.setIntroBeats(lane.editA, lane.editB)
                else if (lane.editKind === "end")
                    lane.deck.setEndBeats(lane.editA, lane.editB)
                else
                    lane.deck.setCustomBeats(lane.editA, lane.editB)
            }
            lane.editKind = ""
            part = ""
        }
        onCanceled: { lane.editKind = ""; part = "" }
        onDoubleClicked: m => {
            const h = hit(m.x)
            if (!h.r) return
            if (h.kind === "intro") lane.deck.autoIntro()
            else if (h.kind === "end") lane.deck.autoEnd()
            else lane.deck.clearCustom()
        }
    }
}
