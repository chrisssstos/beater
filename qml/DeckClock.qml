import QtQuick
import HighlighterUI

// A deck's clock on a small reflective LCD: time played and time left.
ParamLCD {
    id: root
    required property DeckController deck
    padding: Theme.s(3)

    function fmt(s) {
        if (!(s > 0)) return "0:00"
        const t = Math.floor(s)
        return Math.floor(t / 60) + ":" + String(t % 60).padStart(2, "0")
    }

    ParamText {
        // as wide as the widest time, so it never jumps
        width: widest.width
        text: root.fmt(root.deck.position) + "  -" + root.fmt(root.deck.duration - root.deck.position)
        mono: true
        color: Theme.lcdInk
        font.pixelSize: Theme.s(13)
        font.weight: Font.Bold
        wrapMode: Text.NoWrap
        horizontalAlignment: Text.AlignRight
        TextMetrics { id: widest; font.family: Theme.mono; font.pixelSize: Theme.s(13); font.weight: Font.Bold; text: "88:88  -88:88" }
    }
}
