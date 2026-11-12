import QtQuick
import QtQuick.Layouts
import HighlighterUI

// One deck in Edit mode, full width: track, the big waveform, the timeline
// with its loop lane and tags, one row of controls, and the space reserved
// for effects. The pencil at the end of the row switches it to grid edit:
// BPM, grid and key, and the big waveform drags the grid instead of the track.
// Rows keep a fixed height whatever their labels say, so nothing jumps.
Panel {
    id: root
    required property DeckController deck
    required property AppController app
    //! The deck's colour; every kit control inside draws in it (Theme.accentAt).
    property color accent: Look.deckA
    //! Editing the grid, BPM and key (the pencil).
    property bool gridEdit: false
    // ---- the row never cuts: when it doesn't fit, pieces drop out in this
    // order, and the deck is never narrower than what's left (minWidth).
    // A dropped piece collapses to nothing (zero width, transparent) rather
    // than hiding: a hidden kit button reports a smaller natural width, which
    // the fit below reads (it would loop).
    readonly property var dropOrder: [playingBpm, shiftReset, shiftLabel, keyAuto, bpmLabel, halveButton, doubleButton]
    function dropped(item) { return dropOrder.indexOf(item) < dropLevel }
    // What a piece takes laid out, shown or not (a nested row: its pieces and spacing).
    function natural(it) {
        if (it.layoutDirection !== undefined) {
            let sum = 0
            for (let i = 0; i < it.children.length; ++i)
                sum += natural(it.children[i])
            return sum + Math.max(0, it.children.length - 1) * it.spacing
        }
        return it.Layout.preferredWidth > 0 ? it.Layout.preferredWidth : it.implicitWidth
    }
    readonly property real fullRowWidth: natural(playRow) + Theme.s(2)
    readonly property int dropLevel: {
        let need = fullRowWidth, level = 0
        while (need > rowBox.width && level < dropOrder.length) {
            need -= natural(dropOrder[level])
            ++level
        }
        return level
    }
    readonly property real minRowWidth: {
        let least = fullRowWidth
        for (const it of dropOrder)
            least -= natural(it)
        return least
    }
    //! The narrowest this deck can be with nothing cut (Edit mode's minimum width).
    readonly property real minWidth: Math.max(minRowWidth, natural(gridRow) - natural(gridHint) - Theme.s(14))
                                     + 2 * Theme.sectionInset
    //! Its lowest height.
    readonly property real minHeight: column.implicitHeight + 2 * Theme.s(12)

    function fmt(s) {
        if (!(s > 0)) return "0:00"
        const t = Math.floor(s)
        return Math.floor(t / 60) + ":" + String(t % 60).padStart(2, "0")
    }

    DeckDrop {
        anchors.fill: parent
        anchors.margins: -Theme.s(8)
        deck: root.deck
        app: root.app
    }

    component Label: ParamText { caps: true; wrapMode: Text.NoWrap; Layout.alignment: Qt.AlignVCenter }
    component Gap: Item { implicitWidth: Theme.s(6) }
    // the row is full: a little less padding than the kit's default
    component Btn: ParamButton { horizontalPadding: Theme.s(8) }

    ColumnLayout {
        id: column
        anchors.fill: parent
        spacing: Theme.s(8)

        // ---- deck, track, time ----
        RowLayout {
            spacing: Theme.s(10)
            Rectangle {
                id: badge
                implicitWidth: Theme.s(22)
                implicitHeight: Theme.s(22)
                radius: Theme.radius
                color: Theme.litOf(root.accent)
                layer.enabled: Theme.glow
                layer.effect: ParamGlow { w: badge.width; h: badge.height; glowColor: root.accent }
                ParamText {
                    anchors.centerIn: parent
                    text: root.deck.name
                    tone: "normal"
                    color: Theme.inkOf(root.accent)
                    font.weight: Font.Bold
                    wrapMode: Text.NoWrap
                }
            }
            ParamText {
                Layout.fillWidth: true
                text: root.deck.loading ? "loading…"
                    : root.deck.error.length ? root.deck.error
                    : root.deck.loaded ? root.deck.title
                    : "empty: drag a track here, double-click one, or press " + root.deck.name + " in the library"
                color: root.deck.error.length ? Look.warn : root.deck.loaded ? Theme.text : Theme.textFaint
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }
            DeckBeat {
                deck: root.deck
                app: root.app
                size: Theme.s(14)
                spacing: Theme.s(3)
                Layout.alignment: Qt.AlignVCenter
            }
            DeckClock {
                visible: root.deck.loaded
                deck: root.deck
            }
        }

        // ---- the big waveform ----
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Theme.s(60)
            color: Look.wave
            clip: true
            WaveformItem {
                anchors.fill: parent
                deck: root.deck
                zoom: true
                zoomBeats: 16
                color: root.accent
                gridColor: Look.grid
                glow: Theme.hardware
                coreColor: Theme.litOf(root.accent)
                loopColor: Look.neutral
                gridEdit: root.gridEdit
            }
            // phase error vs master (only meaningful with sync on)
            Rectangle {
                visible: root.deck.playing && root.deck.sync
                anchors.bottom: parent.bottom
                x: parent.width / 2 + root.deck.phaseError * parent.width / 16 - 1
                width: 3
                height: 4
                color: Math.abs(root.deck.phaseError) < 0.02 ? root.accent : Look.warn
            }
        }

        // ---- timeline + loop lane with tags ----
        ColumnLayout {
            spacing: Theme.s(2)
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: Theme.s(40)
                color: Look.wave
                clip: true
                WaveformItem {
                    anchors.fill: parent
                    deck: root.deck
                    color: root.accent
                    gridColor: Look.grid
                    glow: Theme.hardware
                    coreColor: Theme.litOf(root.accent)
                    loopColor: Look.neutral
                }
            }
            LoopLane {
                Layout.fillWidth: true
                deck: root.deck
                tags: true
            }
        }

        // ---- one row: playing, or (pencil) editing the grid ----
        Item {
            id: rowBox
            Layout.fillWidth: true
            implicitHeight: Theme.s(30)

            // playing: transport · sync · in / out · length · tempo · key · shift
            RowLayout {
                id: playRow
                anchors.fill: parent
                visible: !root.gridEdit
                spacing: Theme.s(4)

                RowLayout {
                    // the state you're in is the solid one
                    spacing: Theme.s(4)
                    Btn {
                        // paused: play from here; playing: again from the start
                        icon: "play"
                        tip: "Play (playing: again from the start)"
                        accent: root.deck.playing
                        enabled: root.deck.loaded
                        onClicked: root.deck.play()
                    }
                    Btn {
                        icon: "pause"
                        tip: "Pause"
                        accent: root.deck.loaded && !root.deck.playing
                        enabled: root.deck.loaded
                        onClicked: root.deck.pause()
                    }
                }
                Gap {}
                RowLayout {
                    Btn {
                        text: "Sync"
                        checkable: true
                        checked: root.deck.sync
                        onToggled: v => root.deck.sync = v
                    }
                }
                Gap {}
                RowLayout {
                    spacing: Theme.s(4)
                    Btn {
                        text: "In"
                        checked: root.deck.customPending   // lit while it waits for an Out
                        enabled: root.deck.loaded && root.deck.bpm > 0
                        onClicked: root.deck.loopIn()
                    }
                    Btn {
                        text: "Out"
                        enabled: root.deck.customPending || root.deck.hasCustom
                        onClicked: root.deck.loopOut()
                    }
                    Btn {
                        // the In / Out loop on / off (back on past it jumps back in)
                        text: "Loop"
                        checkable: true
                        checked: root.deck.customOn
                        enabled: root.deck.hasCustom
                        onToggled: v => root.deck.customOn = v
                    }
                    Gap {}
                    ParamChoice {
                        Layout.preferredWidth: implicitWidth
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: 4 * Theme.s(28) + 2 * Theme.s(36)   // what its cells take
                        cellMinWidth: Theme.s(26)
                        model: ["1", "2", "4", "8", "16", "32"]
                        // the length of the loop in use (none lit when it's not one of these)
                        currentIndex: model.indexOf(String((root.deck.activeLoopEndBeat - root.deck.activeLoopStartBeat) / 4))
                        onActivated: i => root.deck.setLoopLength(parseInt(model[i]))
                    }
                }
                Gap {}
                RowLayout {
                    spacing: Theme.s(4)
                    Label {
                        id: bpmLabel
                        Layout.maximumWidth: root.dropped(bpmLabel) ? 0 : Number.POSITIVE_INFINITY; opacity: root.dropped(bpmLabel) ? 0 : (enabled ? 1 : 0.4)
                        text: root.deck.analyzing ? "bpm …" : root.deck.gridManual ? "bpm *" : "bpm"
                    }
                    ParamValueBox {
                        // click to type, Up / Down step 0.01 (Shift 1)
                        visible: root.deck.bpm > 0
                        Layout.alignment: Qt.AlignVCenter
                        value: root.deck.bpm
                        decimals: Look.bpmDecimals(root.deck.bpm)
                        interactive: root.deck.loaded
                        fontSize: Theme.s(15)
                        boxHeight: Theme.s(26)
                        onSubmitted: v => root.deck.setBpm(v)
                        onStepped: (d, big) => root.deck.setBpm(root.deck.bpm + d * (big ? 1 : 0.01))
                    }
                    ParamText {
                        // the BPM it plays at; as wide as its widest value, so it never cuts
                        id: playingBpm
                        Layout.maximumWidth: root.dropped(playingBpm) ? 0 : Number.POSITIVE_INFINITY; opacity: root.dropped(playingBpm) ? 0 : (enabled ? 1 : 0.4)
                        text: root.deck.playingBpm > 0 ? "→ " + Look.fmtBpm(root.deck.playingBpm) : ""
                        color: root.accent
                        mono: true
                        wrapMode: Text.NoWrap
                        Layout.preferredWidth: widest.width
                        TextMetrics { id: widest; font: playingBpm.font; text: "→ 000.00" }
                    }
                    Btn { id: doubleButton; Layout.maximumWidth: root.dropped(doubleButton) ? 0 : Number.POSITIVE_INFINITY; opacity: root.dropped(doubleButton) ? 0 : (enabled ? 1 : 0.4); text: "×2"; enabled: root.deck.bpm > 0; onClicked: root.deck.doubleBpm() }
                    Btn { id: halveButton; Layout.maximumWidth: root.dropped(halveButton) ? 0 : Number.POSITIVE_INFINITY; opacity: root.dropped(halveButton) ? 0 : (enabled ? 1 : 0.4); text: "÷2"; enabled: root.deck.bpm > 0; onClicked: root.deck.halveBpm() }
                }
                Gap {}
                RowLayout {
                    spacing: Theme.s(4)
                    Label { text: root.deck.analyzing ? "key …" : root.deck.keyManual ? "key *" : "key" }
                    ParamCombo {
                        Layout.preferredWidth: Theme.s(84)
                        enabled: root.deck.loaded
                        model: root.deck.keyNames
                        currentIndex: root.deck.key
                        onActivated: i => root.deck.setKey(i)
                    }
                    Btn { id: keyAuto; Layout.maximumWidth: root.dropped(keyAuto) ? 0 : Number.POSITIVE_INFINITY; opacity: root.dropped(keyAuto) ? 0 : (enabled ? 1 : 0.4); text: "Auto"; enabled: root.deck.keyManual && !root.deck.analyzing; onClicked: root.deck.autoKey() }
                    Gap {}
                    Label { id: shiftLabel; Layout.maximumWidth: root.dropped(shiftLabel) ? 0 : Number.POSITIVE_INFINITY; opacity: root.dropped(shiftLabel) ? 0 : (enabled ? 1 : 0.4); text: "shift" }
                    Btn { text: "−"; implicitWidth: Theme.s(26); enabled: root.deck.keyShift > -12; onClicked: root.deck.keyShift = root.deck.keyShift - 1 }
                    ParamValueBox {
                        Layout.alignment: Qt.AlignVCenter
                        value: root.deck.keyShift
                        unit: " st"
                        boxHeight: Theme.s(26)
                        onSubmitted: v => root.deck.keyShift = Math.round(v)
                        onStepped: (d, big) => root.deck.keyShift = root.deck.keyShift + d * (big ? 12 : 1)
                    }
                    Btn { text: "+"; implicitWidth: Theme.s(26); enabled: root.deck.keyShift < 12; onClicked: root.deck.keyShift = root.deck.keyShift + 1 }
                    Btn { id: shiftReset; Layout.maximumWidth: root.dropped(shiftReset) ? 0 : Number.POSITIVE_INFINITY; opacity: root.dropped(shiftReset) ? 0 : (enabled ? 1 : 0.4); text: "0"; implicitWidth: Theme.s(26); enabled: root.deck.keyShift !== 0; onClicked: root.deck.keyShift = 0 }
                    ParamText {
                        // the key it plays in now, when shifted; as wide as the widest key name
                        id: shiftedKey
                        text: root.deck.keyShift !== 0 && root.deck.key >= 0 ? "→ " + root.deck.shiftedKeyText : ""
                        color: root.accent
                        mono: true
                        wrapMode: Text.NoWrap
                        Layout.preferredWidth: widestKey.width
                        TextMetrics { id: widestKey; font: shiftedKey.font; text: "→ 12A·" }
                    }
                }
                Item { Layout.fillWidth: true }
                RowLayout {
                    Btn {
                        objectName: "gridEditButton"
                        icon: "pencil"
                        implicitWidth: Theme.s(34)
                        enabled: root.deck.loaded
                        onClicked: root.gridEdit = true
                    }
                }
            }

            // editing: the track's BPM and grid, and its key
            RowLayout {
                id: gridRow
                anchors.fill: parent
                visible: root.gridEdit
                spacing: Theme.s(4)

                RowLayout {
                    spacing: Theme.s(4)
                    Label { text: "bpm" }
                    ParamValueBox {
                        Layout.alignment: Qt.AlignVCenter
                        value: root.deck.bpm
                        decimals: Look.bpmDecimals(root.deck.bpm)
                        interactive: root.deck.loaded
                        fontSize: Theme.s(15)
                        boxHeight: Theme.s(26)
                        onSubmitted: v => root.deck.setBpm(v)
                        onStepped: (d, big) => root.deck.setBpm(root.deck.bpm + d * (big ? 1 : 0.01))
                    }
                    Btn { text: "×2"; enabled: root.deck.bpm > 0; onClicked: root.deck.doubleBpm() }
                    Btn { text: "÷2"; enabled: root.deck.bpm > 0; onClicked: root.deck.halveBpm() }
                    Btn { text: "Tap"; onClicked: root.deck.tap() }
                    Btn { text: "=M"; enabled: root.deck.bpm > 0; onClicked: root.app.matchMasterTo(root.deck.index) }
                    Gap {}
                    Label { text: "grid" }
                    Btn { icon: "back"; tip: "Grid 5 ms earlier"; implicitWidth: Theme.s(28); enabled: root.deck.bpm > 0; onClicked: root.deck.nudgeGrid(-5) }
                    Btn { icon: "play"; tip: "Grid 5 ms later"; implicitWidth: Theme.s(28); enabled: root.deck.bpm > 0; onClicked: root.deck.nudgeGrid(5) }
                    Btn { text: "Set 1"; enabled: root.deck.bpm > 0; onClicked: root.deck.setDownbeatHere() }
                    // back to the detected BPM and grid
                    Btn { text: "Auto"; enabled: root.deck.gridManual && !root.deck.analyzing; onClicked: root.deck.autoGrid() }
                }
                Gap {}
                RowLayout {
                    spacing: Theme.s(4)
                    Label { text: "key" }
                    ParamCombo {
                        Layout.preferredWidth: Theme.s(84)
                        model: root.deck.keyNames
                        currentIndex: root.deck.key
                        onActivated: i => root.deck.setKey(i)
                    }
                    Btn { text: "Auto"; enabled: root.deck.keyManual && !root.deck.analyzing; onClicked: root.deck.autoKey() }
                }
                ParamText {
                    // shown only when it fits whole
                    id: gridHint
                    text: "drag the waveform to move the grid · double-click a beat to make it the 1"
                    tone: "faint"
                    small: true
                    wrapMode: Text.NoWrap
                    opacity: width >= implicitWidth ? 1 : 0
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.s(10)
                }
                RowLayout {
                    Btn {
                        // back to playing
                        icon: "pencil"
                        text: "Done"
                        checkable: true
                        checked: true
                        onToggled: root.gridEdit = false
                    }
                }
            }
        }

        // ---- reserved: effects go here ----
        Item {
            Layout.fillWidth: true
            implicitHeight: Theme.s(64)
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.05)
            }
            ParamText {
                anchors.centerIn: parent
                text: "effects"
                caps: true
                tone: "faint"
                opacity: 0.4
                wrapMode: Text.NoWrap
            }
        }
    }
}
