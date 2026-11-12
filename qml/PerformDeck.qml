import QtQuick
import QtQuick.Layouts
import HighlighterUI

// One deck, as small as it gets: track and time, the overview (click seeks,
// drag sets the loop), play / sync / loop, and the deck's volume.
ParamPlate {
    id: root
    required property DeckController deck
    required property AppController app
    property color accent: Look.deckA

    implicitHeight: col.implicitHeight + 2 * Theme.s(8)
    //! The width it needs with nothing cut (the title elides).
    readonly property real minWidth: col.implicitWidth + 2 * Theme.s(8)

    function fmt(s) {
        if (!(s > 0)) return "0:00"
        const t = Math.floor(s)
        return Math.floor(t / 60) + ":" + String(t % 60).padStart(2, "0")
    }

    DeckDrop {
        anchors.fill: parent
        deck: root.deck
        app: root.app
    }

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: Theme.s(8)
        spacing: Theme.s(6)

        RowLayout {
            spacing: Theme.s(6)
            Rectangle {
                id: badge
                implicitWidth: Theme.s(18)
                implicitHeight: Theme.s(18)
                radius: Theme.radius
                color: Theme.litOf(root.accent)
                layer.enabled: Theme.glow
                layer.effect: ParamGlow { w: badge.width; h: badge.height; glowColor: root.accent }
                ParamText {
                    anchors.centerIn: parent
                    text: root.deck.name
                    tone: "normal"
                    small: true
                    color: Theme.inkOf(root.accent)
                    font.weight: Font.Bold
                    wrapMode: Text.NoWrap
                }
            }
            ParamText {
                Layout.fillWidth: true
                text: root.deck.loading ? "loading…" : root.deck.error.length ? root.deck.error
                    : root.deck.loaded ? root.deck.title : "empty"
                tone: root.deck.loaded ? "normal" : "faint"
                small: true
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }
            ParamText {
                // key (the shifted key when shifted) and tempo, at a glance
                text: (root.deck.keyShift !== 0 ? root.deck.shiftedKeyText : root.deck.keyText)
                      + (root.deck.bpm > 0 ? "   " + Look.fmtBpm(root.deck.bpm) : "")
                color: Theme.textDim
                mono: true
                small: true
                wrapMode: Text.NoWrap
            }
            DeckBeat {
                deck: root.deck
                app: root.app
                size: Theme.s(10)
                spacing: Theme.s(3)
                Layout.alignment: Qt.AlignVCenter
            }
            DeckClock {
                visible: root.deck.loaded
                deck: root.deck
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.s(30)
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
            // the deck's level along the bottom edge (under the loop lane)
            Rectangle {
                anchors.bottom: parent.bottom
                height: Theme.s(2)
                width: parent.width * Math.min(1, root.deck.level)
                color: root.deck.level > 0.9 ? Theme.danger : root.accent
            }
        }

        LoopLane {
            Layout.fillWidth: true
            Layout.topMargin: -Theme.s(4)
            implicitHeight: Theme.s(12)
            deck: root.deck
        }

        RowLayout {
            spacing: Theme.s(4)
            RowLayout {
                spacing: Theme.s(3)
                ParamButton {
                    // paused: play from here; playing: again from the start
                    implicitWidth: Theme.s(34)
                    icon: "play"
                    tip: "Play (playing: again from the start)"
                    accent: root.deck.playing
                    enabled: root.deck.loaded
                    onClicked: root.deck.play()
                }
                ParamButton {
                    implicitWidth: Theme.s(34)
                    icon: "pause"
                    tip: "Pause"
                    accent: root.deck.loaded && !root.deck.playing
                    enabled: root.deck.loaded
                    onClicked: root.deck.pause()
                }
            }
            RowLayout {
                ParamButton {
                    text: "Sync"
                    horizontalPadding: Theme.s(7)
                    checkable: true
                    checked: root.deck.sync
                    onToggled: v => root.deck.sync = v
                }
            }
            RowLayout {
                spacing: Theme.s(3)
                ParamButton {
                    text: "Intro"
                    horizontalPadding: Theme.s(7)
                    checkable: true
                    checked: root.deck.introArmed
                    enabled: root.deck.loaded
                    onToggled: v => root.deck.introArmed = v
                }
                ParamButton {
                    text: "In"
                    horizontalPadding: Theme.s(7)
                    checked: root.deck.customPending
                    enabled: root.deck.loaded && root.deck.bpm > 0
                    onClicked: root.deck.loopIn()
                }
                ParamButton {
                    text: "Out"
                    horizontalPadding: Theme.s(7)
                    enabled: root.deck.customPending || root.deck.hasCustom
                    onClicked: root.deck.loopOut()
                }
                ParamButton {
                    text: "Loop"
                    horizontalPadding: Theme.s(7)
                    checkable: true
                    checked: root.deck.customOn
                    enabled: root.deck.hasCustom
                    onToggled: v => root.deck.customOn = v
                }
                ParamButton {
                    text: "Outro"
                    horizontalPadding: Theme.s(7)
                    checkable: true
                    checked: root.deck.endArmed
                    enabled: root.deck.loaded
                    onToggled: v => root.deck.endArmed = v
                }
            }
            Item { Layout.fillWidth: true }
    }

        ParamSlider {
            Layout.fillWidth: true
            key: "perform.deck" + root.deck.name
            compact: true
            sizeToggle: false
            unit: " %"
            from: 0; to: 100; limitFrom: 0; limitTo: 100
            defaultValue: 80
            value: Math.round(root.deck.volume * 100)
            onMoved: v => root.deck.volume = v / 100
        }
    }
}
