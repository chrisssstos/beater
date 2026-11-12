import QtQuick
import QtQuick.Layouts
import HighlighterUI

// Perform, as a column to keep beside Resolume: master tempo, both decks'
// essentials with the crossfader standing beside them (A above, B below, as
// the decks are), the master volume and the library.
Item {
    id: root
    required property AppController app
    property bool onTop: false
    signal modeRequested(string mode)
    signal onTopToggled(bool on)
    //! The smallest window that shows everything uncut.
    readonly property real minWidth: 2 * Theme.s(8) + Math.max(header.implicitWidth,
                                         Math.max(deckA.minWidth, deckB.minWidth) + Theme.s(8) + Theme.s(48))
    readonly property real minHeight: 2 * Theme.s(8) + column.implicitHeight + Theme.s(120)

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: Theme.s(8)
        spacing: Theme.s(8)

        RowLayout {
            id: header
            spacing: Theme.s(8)
            MasterTempo {
                app: root.app
                digitHeight: Theme.s(20)
            }
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
            ModeSwitch {
                mode: "column"
                onSwitched: m => root.modeRequested(m)
            }
        }

        AutoDjStatus {
            app: root.app
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: Theme.s(8)
            ColumnLayout {
                id: decks
                Layout.fillWidth: true
                spacing: Theme.s(8)
                PerformDeck {
                    id: deckA
                    app: root.app
                    deck: root.app.deckA
                    accent: Look.deckA
                    Layout.fillWidth: true
                }
                PerformDeck {
                    id: deckB
                    app: root.app
                    deck: root.app.deckB
                    accent: Look.deckB
                    Layout.fillWidth: true
                }
            }
            Crossfader {
                app: root.app
                key: "perform.crossfader"
                implicitWidth: Theme.s(48) // narrow from the start: its height follows its width
                Layout.preferredWidth: Theme.s(48)
                Layout.alignment: Qt.AlignTop
                aspect: Theme.s(48) / Math.max(Theme.s(80), decks.implicitHeight)
            }
        }

        ParamSlider {
            property color accent: Look.neutral   // neutral, like the edit view's master fader
            Layout.fillWidth: true
            key: "perform.master"
            label: "master"
            compact: true
            sizeToggle: false
            unit: " %"
            from: 0; to: 100; limitFrom: 0; limitTo: 100
            defaultValue: 85
            value: Math.round(root.app.masterVolume * 100)
            onMoved: v => root.app.masterVolume = v / 100
        }

        LibraryPanel {
            app: root.app
            compact: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Theme.s(120)
        }
    }
}
