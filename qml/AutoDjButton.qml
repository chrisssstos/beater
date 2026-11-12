import QtQuick
import HighlighterUI

// Auto mode on / off: dropped tracks queue up and mix themselves in (AutoDj).
ParamButton {
    id: root
    required property AppController app
    text: "Auto"
    tip: "Auto mix: drop tracks and each comes in over the playing one's breakdown"
    checkable: true
    checked: root.app.autoDj.enabled
    onToggled: v => root.app.autoDj.enabled = v
}
