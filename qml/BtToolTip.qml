import QtQuick
import QtQuick.Controls

// Themed tooltip for buttons. Put it inside a button (or a hover enabled MouseArea) and give it a text:
//     BtToolTip { text: "What this button does" }
// It shows while its parent is hovered, after a short delay.
ToolTip {
    id: tip

    readonly property bool parentHovered: parent ? (parent.hovered === true || parent.containsMouse === true) : false

    visible: parentHovered && text !== ""
    delay: 500
    timeout: 8000
    padding: 8
    width: Math.min(implicitContentWidth + leftPadding + rightPadding, 380)   // Long tips wrap.

    contentItem: Text {
        text: tip.text
        color: BtDarkStyle.textMain
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }

    background: Rectangle {
        color: BtDarkStyle.bgMedium
        border.color: BtDarkStyle.accentColor
        border.width: 1
        radius: 4
    }
}
