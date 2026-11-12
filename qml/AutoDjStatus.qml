import QtQuick
import HighlighterUI

// What auto mode is doing, and what's queued, in a line (only while it's on).
ParamText {
    id: root
    required property AppController app
    readonly property var queue: root.app.autoDj.queue
    visible: root.app.autoDj.enabled
    text: root.app.autoDj.status
          + (queue.length ? " · next: " + queue[0] + (queue.length > 1 ? " (+" + (queue.length - 1) + ")" : "") : "")
    tone: root.app.autoDj.mixing ? "normal" : "dim"
    small: true
    wrapMode: Text.NoWrap
    elide: Text.ElideRight
}
