import QtQuick
import HighlighterUI

// A titled raised panel that looks like a ParamSection, but whose body
// stretches to the panel's height, so a waveform or list inside can fill it.
// Lay its content out with anchors.fill: parent.
Item {
    id: root
    property string title: ""
    property string note: ""
    property color titleColor: Theme.text
    default property alias content: body.data
    //! Items at the right end of the title row.
    property alias tools: tools.data

    Item {
        id: head
        width: parent.width
        // no title, no header row
        height: root.title.length || root.note.length || tools.children.length ? Theme.s(30) : 0
        visible: height > 0
        ParamText {
            id: titleText
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            caps: true
            color: root.titleColor
            font.pixelSize: Theme.labelSize
            font.letterSpacing: Theme.s(0.5)
            wrapMode: Text.NoWrap
        }
        ParamText {
            x: titleText.width + Theme.s(8)
            width: Math.max(0, tools.x - x - Theme.s(8))
            anchors.baseline: titleText.baseline
            text: root.note
            tone: "faint"
            small: true
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
        }
        Row {
            id: tools
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.s(8)
        }
    }
    ParamPlate {
        y: head.height
        width: parent.width
        height: parent.height - head.height
        Item {
            id: body
            anchors.fill: parent
            anchors.leftMargin: Theme.sectionInset
            anchors.rightMargin: Theme.sectionInset
            anchors.topMargin: Theme.s(12)
            anchors.bottomMargin: Theme.s(12)
        }
    }
}
