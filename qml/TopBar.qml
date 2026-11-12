import QtQuick
import QtQuick.Layouts
import HighlighterUI

Item {
    id: root
    required property AppController app
    signal modeRequested(string mode)
    implicitHeight: Theme.s(64)

    //! The width it needs with nothing cut.
    readonly property real minWidth: bar.implicitWidth

    RowLayout {
        id: bar
        anchors.fill: parent
        spacing: Theme.s(16)

        ParamText {
            text: "BEATER"
            color: Theme.accent
            font.pixelSize: Theme.s(20)
            font.weight: Font.Black
            font.letterSpacing: Theme.s(2)
            wrapMode: Text.NoWrap
        }

        Column {
            spacing: Theme.s(4)
            ParamText { text: "mode"; caps: true; wrapMode: Text.NoWrap }
            ModeSwitch { mode: "edit"; onSwitched: m => root.modeRequested(m) }
        }

        Item { Layout.fillWidth: true }

        Column {
            spacing: Theme.s(4)
            ParamText { text: " "; caps: true; wrapMode: Text.NoWrap }   // the LCD has its own label
            Row {
                spacing: Theme.s(4)
                ParamButton { text: "−1"; onClicked: root.app.nudgeMasterBpm(-1); anchors.verticalCenter: parent.verticalCenter }
                ParamButton { text: "−.1"; onClicked: root.app.nudgeMasterBpm(-0.1); anchors.verticalCenter: parent.verticalCenter }
                MasterTempo { app: root.app }
                ParamButton { text: "+.1"; onClicked: root.app.nudgeMasterBpm(0.1); anchors.verticalCenter: parent.verticalCenter }
                ParamButton { text: "+1"; onClicked: root.app.nudgeMasterBpm(1); anchors.verticalCenter: parent.verticalCenter }
            }
        }

        Column {
            spacing: Theme.s(4)
            ParamText { text: "bar"; caps: true; wrapMode: Text.NoWrap }
            BeatLights { beat: root.app.masterBeat }
        }

        Column {
            spacing: Theme.s(4)
            ParamText { text: "phase lock"; caps: true; wrapMode: Text.NoWrap }
            ParamChoice {
                width: Theme.s(110)
                model: ["Beat", "Bar"]
                currentIndex: root.app.phaseMode
                onActivated: i => root.app.phaseMode = i
            }
        }

        Item { Layout.fillWidth: true }

        Column {
            spacing: Theme.s(4)
            Layout.preferredWidth: Theme.s(200)
            ParamText { text: "output"; caps: true; wrapMode: Text.NoWrap }
            ParamCombo {
                width: parent.width
                model: root.app.outputDevices
                currentIndex: root.app.outputDevices.indexOf(root.app.outputDevice.length ? root.app.outputDevice : "System default")
                onActivated: i => root.app.outputDevice = root.app.outputDevices[i]
            }
            ParamText {
                // the device in use and its rate, or what went wrong
                width: parent.width
                text: root.app.audioStatus
                small: true
                tone: "faint"
                color: root.app.audioOk ? Theme.textFaint : Look.warn
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
            }
        }
        // devices come and go (an interface plugged in): keep the list current
        Timer {
            interval: 3000
            running: root.visible
            repeat: true
            onTriggered: root.app.refreshOutputs()
        }
    }
}
