import QtQuick
import HighlighterUI

// Takes a track dragged from the library, or files dragged from the Finder
// (added to the library too), and loads it into this deck. While one hovers
// it, it says what will happen (in red over a playing deck).
DropArea {
    id: root
    required property DeckController deck
    required property AppController app
    readonly property color tint: Theme.accentAt(root, Window.window)

    keys: ["beater-track", "text/uri-list"]
    readonly property bool files: containsDrag && drag.hasUrls
    z: 10
    onDropped: d => {
        if (d.hasUrls)
            root.app.loadUrlsToDeck(d.urls, root.deck.index)
        else
            root.app.loadToDeck(d.source.row, root.deck.index)
        d.accept()
    }

    Rectangle {
        anchors.fill: parent
        visible: root.containsDrag
        color: Qt.rgba(0, 0, 0, 0.45)
        border.width: Theme.s(2)
        border.color: root.deck.playing && !root.app.autoDj.enabled ? Theme.danger : root.tint
        ParamText {
            // at the top, clear of the dragged track under the pointer
            anchors.top: parent.top
            anchors.topMargin: Theme.s(10)
            anchors.horizontalCenter: parent.horizontalCenter
            text: (root.files ? "add to library · " : "")
                  + (root.app.autoDj.enabled ? "queue for the auto mix"
                     : root.deck.playing ? "replace the playing track" : "load into deck " + root.deck.name)
            tone: "normal"
            color: root.deck.playing && !root.app.autoDj.enabled ? Theme.danger : root.tint
            font.weight: Font.Bold
            wrapMode: Text.NoWrap
        }
    }
}
