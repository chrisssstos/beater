import QtQuick
import QtQuick.Layouts
import HighlighterUI

// Perform, as a strip to keep above or below Resolume: the master and the
// mode at the side, deck A, the crossfader standing between the decks, deck
// B, and the library taking the rest of the width.
Item {
    id: root
    required property AppController app
    property bool onTop: false
    signal modeRequested(string mode)
    signal onTopToggled(bool on)
    readonly property real gap: Theme.s(8)
    //! The smallest window that shows everything uncut.
    readonly property real minWidth: 2 * gap + side.implicitWidth + deckA.minWidth + Theme.s(48) + deckB.minWidth
                                     + Theme.s(260) + 4 * gap
    readonly property real minHeight: 2 * gap + Math.max(side.implicitHeight, deckA.implicitHeight, Theme.s(200))

    RowLayout {
        anchors.fill: parent
        anchors.margins: root.gap
        spacing: root.gap

        ColumnLayout {
            id: side
            Layout.fillHeight: true
            Layout.preferredWidth: implicitWidth
            spacing: root.gap
            MasterTempo {
                app: root.app
                digitHeight: Theme.s(20)
            }
            RowLayout {
                spacing: root.gap
                BeatLights {
                    beat: root.app.masterBeat
                    size: Theme.s(10)
                    spacing: Theme.s(3)
                    Layout.alignment: Qt.AlignVCenter
                }
                Item { Layout.fillWidth: true }
                AutoDjButton { app: root.app }
                ParamButton {
                    // keep the window above Resolume
                    text: "Pin"
                    checkable: true
                    checked: root.onTop
                    onToggled: v => root.onTopToggled(v)
                }
            }
            ModeSwitch {
                mode: "strip"
                onSwitched: m => root.modeRequested(m)
            }
            AutoDjStatus {
                app: root.app
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.s(240)
            }
            ParamSlider {
                property color accent: Look.neutral
                Layout.fillWidth: true
                key: "strip.master"
                label: "master"
                compact: true
                sizeToggle: false
                unit: " %"
                from: 0; to: 100; limitFrom: 0; limitTo: 100
                defaultValue: 85
                value: Math.round(root.app.masterVolume * 100)
                onMoved: v => root.app.masterVolume = v / 100
            }
            Item { Layout.fillHeight: true }
        }

        PerformDeck {
            id: deckA
            app: root.app
            deck: root.app.deckA
            accent: Look.deckA
            Layout.preferredWidth: Math.max(minWidth, Theme.s(380))
            Layout.alignment: Qt.AlignTop
        }
        Crossfader {
            app: root.app
            key: "strip.crossfader"
            implicitWidth: Theme.s(48) // narrow from the start: its height follows its width
            Layout.preferredWidth: Theme.s(48)
            Layout.alignment: Qt.AlignTop
            aspect: Theme.s(48) / Math.max(Theme.s(80), deckA.implicitHeight)
        }
        PerformDeck {
            id: deckB
            app: root.app
            deck: root.app.deckB
            accent: Look.deckB
            Layout.preferredWidth: Math.max(minWidth, Theme.s(380))
            Layout.alignment: Qt.AlignTop
        }

        LibraryPanel {
            app: root.app
            compact: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: Theme.s(260)
        }
    }
}
