import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import com.wouldymods.blobset 1.0

// File Mapping Editor: the loaded game's .mapping (file path <-> hash). Double click a cell (or press F2 / Enter)
// to edit it, Save writes the file. The search bar finds paths ('/' or '\') and hashes; Enter / F3 = next match,
// Shift+Enter / Shift+F3 = previous.
Item {
    id: editorRoot
    anchors.fill: parent

    readonly property string iconDir: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/"
    readonly property color errorColor: Material.color(Material.Red)

    // Empty = the loaded game's mapping (AppOrchestrator::mappingFilePath()).
    property string targetFilePath: ""

    // Search state
    property int currentRow: -1
    property int matchCount: 0
    property int matchPosition: 0

    // Status line
    property string statusText: ""
    property bool statusOk: true

    // Column geometry, shared by the header and the table.
    readonly property int hashColWidth: 200
    readonly property int pathColWidth: Math.max(300, tableView.width - hashColWidth)

    Rectangle {
        anchors.fill: parent
        color: BtDarkStyle.bgDarkest
        z: -1
    }

    function setStatus(ok, text) {
        statusOk = ok
        statusText = text
    }

    FileMappingEditorModel {
        id: mappingModel
        onActionCompleted: (msg) => editorRoot.setStatus(true, msg)
        onErrorOccurred: (msg) => editorRoot.setStatus(false, msg)
        onModelReset: {
            editorRoot.currentRow = -1
            editorRoot.updateMatches()
        }
    }

    ItemSelectionModel {
        id: selection
        model: mappingModel
    }

    // ------------------------------------------------------------------------------------------------
    // Search
    // ------------------------------------------------------------------------------------------------
    function selectRow(row) {
        currentRow = row
        if (row < 0) {
            selection.clear()
            return
        }
        selection.setCurrentIndex(mappingModel.index(row, 0),
                                  ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
        tableView.positionViewAtRow(row, TableView.AlignVCenter)
    }

    function updateMatches() {
        const query = searchInput.text.trim()
        if (query === "") {
            matchCount = 0
            matchPosition = 0
            return
        }
        const info = mappingModel.searchInfo(query, currentRow)
        matchCount = info.count
        matchPosition = info.position
    }

    // Finds the next (or previous) match after the current row, wrapping around.
    function find(backward, fromCurrent) {
        const query = searchInput.text.trim()
        if (query === "" || mappingModel.count === 0) {
            updateMatches()
            return
        }
        let start
        if (currentRow < 0)
            start = backward ? mappingModel.count - 1 : 0
        else if (fromCurrent)
            start = currentRow
        else
            start = backward ? currentRow - 1 : currentRow + 1
        if (start < 0) start = mappingModel.count - 1
        if (start >= mappingModel.count) start = 0

        const row = mappingModel.search(query, start, backward)
        if (row >= 0) {
            selectRow(row)
            updateMatches()
            setStatus(true, "Match " + matchPosition + " of " + matchCount + " at row " + (row + 1))
        } else {
            updateMatches()
            setStatus(false, "No file path or hash contains \"" + query + "\"")
        }
    }

    // Typing searches as you go (from the current row, so the selection stays put while it still matches).
    Timer {
        id: searchDelay
        interval: 250
        onTriggered: editorRoot.find(false, true)
    }

    // ------------------------------------------------------------------------------------------------
    // Load / leave
    // ------------------------------------------------------------------------------------------------
    property var pendingAction: null    // Runs after the unsaved changes dialog says Discard / Save.

    function guarded(action) {
        if (mappingModel.modified) {
            pendingAction = action
            unsavedDialog.open()
        } else {
            action()
        }
    }

    function goBack() {
        appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml")
    }

    function fileName(path) {
        const i = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        return i >= 0 ? path.substring(i + 1) : path
    }

    Component.onCompleted: {
        const path = targetFilePath !== "" ? targetFilePath : appOrchestrator.mappingFilePath()
        if (path === "")
            setStatus(false, "There's no file mapping for this game yet: run 'Update File Mapping Data' first.")
        else
            mappingModel.loadMapping(path)
        searchInput.forceActiveFocus()
    }

    Shortcut { sequences: [StandardKey.Find]; onActivated: { searchInput.forceActiveFocus(); searchInput.selectAll() } }
    Shortcut { sequences: ["F3"]; onActivated: editorRoot.find(false, false) }
    Shortcut { sequences: ["Shift+F3"]; onActivated: editorRoot.find(true, false) }
    Shortcut { sequences: [StandardKey.Save]; enabled: mappingModel.modified; onActivated: mappingModel.saveMapping() }

    // ------------------------------------------------------------------------------------------------
    // Components
    // ------------------------------------------------------------------------------------------------
    component HeaderButton: AbstractButton {
        id: headerButton
        property url iconSource: ""
        property bool accent: false
        property string tip: ""
        BtToolTip { text: headerButton.tip }

        implicitHeight: 40
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        leftPadding: 12
        rightPadding: 12
        hoverEnabled: true
        opacity: enabled ? 1.0 : 0.4

        readonly property bool lit: hovered && enabled

        background: Rectangle {
            color: headerButton.lit ? BtDarkStyle.bgMedium : "transparent"
            border.color: headerButton.lit || headerButton.accent ? BtDarkStyle.accentColor : "transparent"
            border.width: 1
            radius: 4
        }

        contentItem: RowLayout {
            spacing: 6
            Image {
                visible: headerButton.iconSource.toString() !== ""
                source: headerButton.iconSource
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                Layout.alignment: Qt.AlignVCenter
                fillMode: Image.PreserveAspectFit
            }
            Text {
                text: headerButton.text
                color: headerButton.lit || headerButton.accent ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                font.bold: true
                font.pixelSize: 14
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // Small square button inside the search bar (previous / next / clear).
    component SearchButton: AbstractButton {
        id: searchButton
        property string tip: ""
        BtToolTip { text: searchButton.tip }
        implicitWidth: 24
        implicitHeight: 24
        hoverEnabled: true
        opacity: enabled ? 1.0 : 0.35
        background: Rectangle {
            radius: 3
            color: searchButton.hovered && searchButton.enabled ? BtDarkStyle.bgHover : "transparent"
        }
        contentItem: Text {
            text: searchButton.text
            color: searchButton.hovered && searchButton.enabled ? BtDarkStyle.accentColor : BtDarkStyle.textDim
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Page
    // ------------------------------------------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: BtDarkStyle.bgDark
            border.color: BtDarkStyle.borderDark
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 15
                anchors.rightMargin: 15
                spacing: 12

                HeaderButton {
                    text: "◀ Back"
                    tip: "Return to the main workspace"
                    onClicked: editorRoot.guarded(editorRoot.goBack)
                }

                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }

                Image {
                    source: editorRoot.iconDir + "mapping_32.png"
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    fillMode: Image.PreserveAspectFit
                }

                ColumnLayout {
                    spacing: 0
                    Layout.maximumWidth: 260
                    Text {
                        text: "File Mapping Editor"
                        color: BtDarkStyle.accentColor
                        font.bold: true
                        font.pixelSize: 16
                    }
                    Text {
                        Layout.fillWidth: true
                        text: mappingModel.filePath !== "" ? editorRoot.fileName(mappingModel.filePath)
                                                           : "No mapping loaded"
                        color: BtDarkStyle.textDim
                        font.pixelSize: 11
                        font.family: "Consolas"
                        elide: Text.ElideMiddle
                    }
                }

                Item { Layout.fillWidth: true; Layout.minimumWidth: 8 }

                // Search bar (same look as the asset search in MainForm.qml)
                Rectangle {
                    Layout.preferredWidth: 520
                    Layout.minimumWidth: 240
                    Layout.preferredHeight: 34
                    color: BtDarkStyle.bgMedium
                    border.width: 1
                    border.color: searchInput.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                    radius: 4
                    opacity: mappingModel.count > 0 ? 1.0 : 0.5

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 4
                        spacing: 4

                        Text { text: "🔍"; font.pixelSize: 12; color: BtDarkStyle.textDim }

                        TextInput {
                            id: searchInput
                            Layout.fillWidth: true
                            Layout.leftMargin: 4
                            color: BtDarkStyle.textMain
                            selectionColor: BtDarkStyle.accentDim
                            font.pixelSize: 13
                            font.family: "Consolas"
                            selectByMouse: true
                            clip: true
                            verticalAlignment: TextInput.AlignVCenter
                            enabled: mappingModel.count > 0

                            onTextEdited: searchDelay.restart()
                            Keys.onReturnPressed: (event) => {
                                searchDelay.stop()
                                editorRoot.find((event.modifiers & Qt.ShiftModifier) !== 0, false)
                            }
                            Keys.onEnterPressed: (event) => {
                                searchDelay.stop()
                                editorRoot.find((event.modifiers & Qt.ShiftModifier) !== 0, false)
                            }
                            Keys.onEscapePressed: {
                                text = ""
                                editorRoot.updateMatches()
                            }

                            Text {
                                anchors.fill: parent
                                verticalAlignment: Text.AlignVCenter
                                text: "Search file paths or hashes..."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 13
                                font.italic: true
                                visible: searchInput.text === "" && !searchInput.activeFocus
                            }
                        }

                        Text {
                            visible: searchInput.text.trim() !== ""
                            text: editorRoot.matchCount === 0 ? "No matches"
                                  : (editorRoot.matchPosition > 0 ? editorRoot.matchPosition + " / " : "")
                                    + editorRoot.matchCount
                            color: editorRoot.matchCount === 0 ? editorRoot.errorColor : BtDarkStyle.textDim
                            font.pixelSize: 11
                            font.family: "Consolas"
                            Layout.rightMargin: 4
                        }

                        SearchButton {
                            text: "▲"
                            tip: "Previous match (Shift+Enter / Shift+F3)"
                            enabled: editorRoot.matchCount > 0
                            onClicked: editorRoot.find(true, false)
                        }
                        SearchButton {
                            text: "▼"
                            tip: "Next match (Enter / F3)"
                            enabled: editorRoot.matchCount > 0
                            onClicked: editorRoot.find(false, false)
                        }
                        SearchButton {
                            text: "✕"
                            tip: "Clear the search"
                            visible: searchInput.text !== ""
                            onClicked: {
                                searchInput.text = ""
                                editorRoot.updateMatches()
                                searchInput.forceActiveFocus()
                            }
                        }
                    }
                }

                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }

                HeaderButton {
                    text: "Reload"
                    tip: "Read the mapping file again (unsaved edits are dropped)"
                    iconSource: editorRoot.iconDir + "reset_32.png"
                    enabled: mappingModel.filePath !== ""
                    onClicked: editorRoot.guarded(() => mappingModel.reload())
                }

                HeaderButton {
                    text: mappingModel.modified ? "Save Changes •" : "Save Changes"
                    tip: "Save your edits to the file mapping (Ctrl+S)"
                    iconSource: editorRoot.iconDir + "save_32.png"
                    accent: mappingModel.modified
                    enabled: mappingModel.modified
                    onClicked: mappingModel.saveMapping()
                }
            }
        }

        // Table
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            rowSpacing: 0
            columnSpacing: 0

            // Corner above the row numbers
            Rectangle {
                Layout.preferredWidth: rowHeader.width
                Layout.preferredHeight: 32
                color: BtDarkStyle.bgDark
                border.color: BtDarkStyle.borderDark
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: "#"
                    color: BtDarkStyle.textDim
                    font.bold: true
                    font.pixelSize: 12
                }
            }

            // Column titles (scroll sideways with the table)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                color: BtDarkStyle.bgDark
                clip: true

                Row {
                    x: -tableView.contentX
                    height: parent.height

                    Repeater {
                        model: [{ title: "File Path", width: editorRoot.pathColWidth },
                                { title: "Hash", width: editorRoot.hashColWidth }]

                        Rectangle {
                            required property var modelData
                            width: modelData.width
                            height: parent.height
                            color: BtDarkStyle.bgDark
                            border.color: BtDarkStyle.borderDark
                            border.width: 1
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                text: modelData.title
                                color: BtDarkStyle.accentColor
                                font.bold: true
                                font.pixelSize: 13
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }

            // Row numbers
            VerticalHeaderView {
                id: rowHeader
                Layout.fillHeight: true
                syncView: tableView
                clip: true
                implicitWidth: Math.max(48, String(mappingModel.count).length * 8 + 20)

                delegate: Rectangle {
                    required property var display
                    required property int row
                    implicitWidth: rowHeader.implicitWidth
                    implicitHeight: 28
                    color: row === editorRoot.currentRow ? BtDarkStyle.bgMedium : BtDarkStyle.bgDark
                    Rectangle {
                        anchors.right: parent.right
                        width: 1
                        height: parent.height
                        color: BtDarkStyle.borderDark
                    }
                    Text {
                        anchors.fill: parent
                        anchors.rightMargin: 8
                        text: display
                        color: row === editorRoot.currentRow ? BtDarkStyle.accentColor : BtDarkStyle.textDim
                        font.pixelSize: 11
                        font.family: "Consolas"
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            TableView {
                id: tableView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: mappingModel
                selectionModel: selection
                selectionBehavior: TableView.SelectRows
                selectionMode: TableView.SingleSelection
                editTriggers: TableView.DoubleTapped | TableView.EditKeyPressed
                boundsBehavior: Flickable.StopAtBounds
                keyNavigationEnabled: true

                columnWidthProvider: (column) => column === 0 ? editorRoot.pathColWidth : editorRoot.hashColWidth
                rowHeightProvider: () => 28
                onWidthChanged: forceLayout()

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                // Clicking / arrow keys move the current row (the search continues from it).
                Connections {
                    target: selection
                    function onCurrentChanged(current) {
                        if (current.valid && current.row !== editorRoot.currentRow) {
                            editorRoot.currentRow = current.row
                            editorRoot.updateMatches()
                        }
                    }
                }

                delegate: Rectangle {
                    id: cell
                    required property int row
                    required property int column
                    required property string display
                    required property bool selected
                    required property bool current

                    implicitHeight: 28
                    color: selected ? BtDarkStyle.bgMedium
                                    : (cellArea.containsMouse ? BtDarkStyle.bgHover
                                                              : (row % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark))

                    // Gold bar in front of the selected row.
                    Rectangle {
                        visible: cell.selected && cell.column === 0
                        width: 3
                        height: parent.height
                        color: BtDarkStyle.accentColor
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: BtDarkStyle.borderDark
                    }
                    Rectangle {
                        anchors.right: parent.right
                        width: 1
                        height: parent.height
                        color: BtDarkStyle.borderDark
                    }

                    MouseArea {
                        id: cellArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton   // Clicks go to the TableView (selection / editing).
                    }

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        text: cell.display
                        color: cell.selected ? BtDarkStyle.white
                                             : (cell.column === 1 ? BtDarkStyle.accentDim : BtDarkStyle.textMain)
                        font.family: "Consolas"
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideMiddle
                    }

                    TableView.editDelegate: TextField {
                        anchors.fill: parent
                        leftPadding: 9
                        rightPadding: 9
                        topPadding: 0
                        bottomPadding: 0
                        text: cell.display
                        color: BtDarkStyle.textMain
                        selectionColor: BtDarkStyle.accentDim
                        font.family: "Consolas"
                        font.pixelSize: 13
                        verticalAlignment: TextInput.AlignVCenter
                        selectByMouse: true
                        background: Rectangle {
                            color: BtDarkStyle.bgDarkest
                            border.color: BtDarkStyle.accentColor
                            border.width: 1
                        }
                        Component.onCompleted: selectAll()
                        // Validated by the model: a bad path / hash is refused and reported in the status bar.
                        TableView.onCommit: cell.display = text
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: mappingModel.count === 0
                    text: mappingModel.filePath === "" ? "No file mapping loaded" : "The file mapping is empty"
                    color: BtDarkStyle.textDim
                    font.pixelSize: 14
                }
            }
        }

        // Status bar
        ToolBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            background: Rectangle {
                color: BtDarkStyle.bgDark
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: BtDarkStyle.borderDark
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 15
                anchors.rightMargin: 15
                spacing: 16

                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font.pointSize: 9
                    color: editorRoot.statusOk ? BtDarkStyle.textDim : editorRoot.errorColor
                    text: "Status: " + (editorRoot.statusText !== ""
                                        ? editorRoot.statusText
                                        : "Ready | Double click a cell (or press F2) to edit it")
                }

                Text {
                    text: mappingModel.count.toLocaleString(Qt.locale(), "f", 0) + " entries"
                          + (mappingModel.modified ? "  ·  unsaved changes" : "")
                    color: mappingModel.modified ? BtDarkStyle.accentColor : BtDarkStyle.textDim
                    font.pointSize: 9
                }

                Text {
                    text: Qt.application.version !== "" ? "v" + Qt.application.version : ""
                    color: BtDarkStyle.accentColor
                    font.pointSize: 9
                }
            }
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Unsaved changes
    // ------------------------------------------------------------------------------------------------
    Dialog {
        id: unsavedDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 420
        padding: 20
        title: "Unsaved changes"

        background: Rectangle {
            color: BtDarkStyle.bgDark
            border.color: BtDarkStyle.accentColor
            border.width: 1
            radius: 6
        }
        header: Text {
            text: unsavedDialog.title
            color: BtDarkStyle.accentColor
            font.bold: true
            font.pixelSize: 15
            leftPadding: 20
            topPadding: 16
        }

        contentItem: Text {
            text: "The file mapping has unsaved changes. Save them first?"
            color: BtDarkStyle.textMain
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            HeaderButton {
                text: "Save"
                accent: true
                implicitHeight: 34
                onClicked: {
                    mappingModel.saveMapping()
                    unsavedDialog.close()
                    if (!mappingModel.modified && editorRoot.pendingAction)
                        editorRoot.pendingAction()
                    editorRoot.pendingAction = null
                }
            }
            HeaderButton {
                text: "Discard"
                implicitHeight: 34
                onClicked: {
                    unsavedDialog.close()
                    const action = editorRoot.pendingAction
                    editorRoot.pendingAction = null
                    if (action)
                        action()
                }
            }
            HeaderButton {
                text: "Cancel"
                implicitHeight: 34
                Layout.rightMargin: 16
                Layout.bottomMargin: 14
                onClicked: {
                    editorRoot.pendingAction = null
                    unsavedDialog.close()
                }
            }
        }
    }
}
