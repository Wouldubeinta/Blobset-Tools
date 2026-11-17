import QtQuick
import QtQuick.Controls.Material

// A ComboBox whose list has a proper scroll bar: long lists (hair styles, heads, boots, teams ...) open at a
// sensible height, start at the current item, and can be dragged / wheeled / scrolled with the bar.
//     BtComboBox { model: ...; ... }   - used like a ComboBox.
ComboBox {
    id: control

    // The most the list grows to before it scrolls.
    property int maxPopupHeight: 420

    popup: Popup {
        id: comboPopup
        y: control.height
        width: Math.max(control.width, 160)
        topMargin: 8
        bottomMargin: 8
        padding: 1
        height: Math.min(list.contentHeight + topPadding + bottomPadding, control.maxPopupHeight,
                         control.Window.height > 0 ? control.Window.height - 16 : control.maxPopupHeight)

        contentItem: ListView {
            id: list
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                width: 10
                contentItem: Rectangle {
                    implicitWidth: 6
                    radius: 3
                    color: parent.pressed ? BtDarkStyle.accentColor : BtDarkStyle.textDim
                }
                background: Rectangle { color: BtDarkStyle.bgDarkest }
            }
        }

        background: Rectangle {
            color: BtDarkStyle.bgMedium
            border.color: BtDarkStyle.borderDark
            radius: 3
        }

        onOpened: list.positionViewAtIndex(Math.max(0, control.currentIndex), ListView.Center)
    }
}
