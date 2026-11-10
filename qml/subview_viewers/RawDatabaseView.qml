import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import com.wouldymods.blobset 1.0

// Raw Databases: the C# editors' Raw ... windows. A table of the database / save (every player, team, lineup, ...) as
// text cells, edited in place, written back in one go (Save Changes), and exported to / imported from CSV (the C#
// editors' files import too: columns are matched by name). Shared by the Database, Save and Career Editors: the
// manager gives the tables (rawTables), fills the grid (fillRawTable) and writes it back (applyRawTable).
//
//     RawDatabaseView { id: rawView; anchors.fill: parent; manager: db; filePrefix: db.gameName }
//     rawView.open()
Rectangle {
    id: rawRoot
    visible: false
    color: BtDarkStyle.bgDarkest
    z: 50

    property var manager: null
    property string filePrefix: ""
    property var tables: []
    property int tableIndex: 0
    readonly property var table: tables[tableIndex] || ({})
    property string statusText: ""
    property bool statusOk: true
    property int currentRow: -1
    property var widths: []
    readonly property string iconDir: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/"

    signal closed()

    function open(tableId) {
        tables = manager && manager.rawTables ? manager.rawTables() : []
        let index = 0
        for (let i = 0; i < tables.length; ++i)
            if (tables[i].id === (tableId || settings.lastTable))
                index = i
        visible = true
        load(index)
        tableView.forceActiveFocus()
    }
    function close() {
        guarded(() => {
            rawModel.clear()
            visible = false
            closed()
        })
    }
    function setStatus(ok, text) {
        statusOk = ok
        statusText = text
    }
    // Runs `action` now, or after asking when the grid has changes not saved to the file yet.
    function guarded(action) {
        if (!rawModel.dirty) {
            action()
            return
        }
        discardDialog.action = action
        discardDialog.open()
    }
    function load(index) {
        if (index < 0 || index >= tables.length)
            return
        tableIndex = index
        tableBox.currentIndex = index
        settings.lastTable = tables[index].id
        if (!manager.fillRawTable(tables[index].id, rawModel)) {
            setStatus(false, "Couldn't read " + tables[index].title + ".")
            return
        }
        const w = []
        for (let c = 0; c < rawModel.columns; ++c)
            w.push(rawModel.columnWidth(c))
        widths = w
        currentRow = -1
        tableView.forceLayout()
        if (rawModel.rows > 0 && rawModel.columns > 0)
            tableView.positionViewAtCell(Qt.point(0, 0), TableView.AlignLeft | TableView.AlignTop)
        setStatus(true, rawModel.rows + " rows  ·  " + rawModel.columns + " columns. Double-click a cell (or start typing) to edit it, then Save Changes.")
    }
    function saveChanges() {
        tableView.closeEditor()
        const error = manager.applyRawTable(rawModel)
        if (error !== "")
            setStatus(false, error)
        else
            setStatus(true, manager.statusText + (manager.unsavedChanges ? "  Save the file to keep them." : ""))
    }
    function find(fromNext) {
        const row = rawModel.find(findField.text, fromNext ? currentRow + 1 : Math.max(0, currentRow))
        if (row < 0) {
            setStatus(false, "\"" + findField.text + "\" isn't in this table.")
            return
        }
        currentRow = row
        tableView.positionViewAtRow(row, TableView.AlignVCenter)
        selection.setCurrentIndex(rawModel.index(row, 0), ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
    }
    function folder() {
        return settings.lastFolder !== "" ? settings.lastFolder
                                          : StandardPaths.writableLocation(StandardPaths.DocumentsLocation).toString()
    }

    Settings {
        id: settings
        category: "RawDatabases"
        property string lastFolder: ""
        property string lastTable: ""
    }

    RawTableModel { id: rawModel }

    // Over the editor: nothing under it takes the mouse while it's open.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: (wheel) => wheel.accepted = true
    }

    ItemSelectionModel {
        id: selection
        model: rawModel
        onCurrentChanged: (current) => { if (current.valid) rawRoot.currentRow = current.row }
    }

    // The file's data changed under the grid (reloaded / another file): read it again.
    Connections {
        target: rawRoot.manager
        enabled: rawRoot.visible
        function onLoadedChanged() {
            if (!rawRoot.manager.loaded) {
                rawModel.clear()
                rawRoot.visible = false
                rawRoot.closed()
            } else {
                rawRoot.open(rawRoot.table.id)
            }
        }
    }

    Shortcut {
        sequence: StandardKey.Save
        enabled: rawRoot.visible && rawModel.dirty
        onActivated: rawRoot.saveChanges()
    }
    Shortcut {
        sequence: StandardKey.Find
        enabled: rawRoot.visible
        onActivated: findField.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: BtDarkStyle.bgDark
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: BtDarkStyle.borderDark
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 15
                anchors.rightMargin: 15
                spacing: 10

                RawButton {
                    text: "◀ Back"
                    tip: "Back to the editor"
                    onClicked: rawRoot.close()
                }
                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }
                Image {
                    source: "qrc:/qt/qml/com/wouldymods/blobset/assets/editors/raw_database.png"
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    fillMode: Image.PreserveAspectFit
                }
                Text {
                    text: "Raw Databases"
                    color: BtDarkStyle.accentColor
                    font.bold: true
                    font.pixelSize: 16
                }
                ComboBox {
                    id: tableBox
                    Layout.preferredWidth: 300
                    model: rawRoot.tables
                    textRole: "title"
                    currentIndex: rawRoot.tableIndex
                    onActivated: (index) => {
                        if (index === rawRoot.tableIndex)
                            return
                        currentIndex = rawRoot.tableIndex
                        rawRoot.guarded(() => rawRoot.load(index))
                    }
                    BtToolTip { text: "The table to show (the C# editors' Raw Databases menu)" }
                }
                TextField {
                    id: findField
                    Layout.preferredWidth: 200
                    placeholderText: "Find (Ctrl+F)"
                    selectByMouse: true
                    color: BtDarkStyle.textMain
                    onAccepted: rawRoot.find(true)
                    BtToolTip { text: "Find a row with this in any cell (Enter: the next one)" }
                }
                Item { Layout.fillWidth: true }
                RawButton {
                    text: "Import CSV..."
                    tip: "Replace the grid with a CSV file's rows (exported here or by the C# editors). Columns are matched by name; the ones the file doesn't have keep what the grid had. Nothing is written until Save Changes."
                    iconSource: rawRoot.iconDir + "open_32.png"
                    enabled: rawModel.columns > 0
                    onClicked: rawRoot.guarded(() => csvDialog.start(false))
                }
                RawButton {
                    text: "Export CSV..."
                    tip: "Save the grid as a CSV file (opens in Excel)"
                    iconSource: rawRoot.iconDir + "extract_32.png"
                    enabled: rawModel.columns > 0
                    onClicked: csvDialog.start(true)
                }
                RawButton {
                    text: "Repair IDs"
                    visible: rawRoot.table.repair === true
                    tip: "The C# editor's Repair Id's: player ids in these lists that are a custom player's own id become the id of the game player they replace (their original id)"
                    onClicked: {
                        const n = rawRoot.manager.repairRawIds(rawModel)
                        rawRoot.setStatus(true, n > 0 ? "Repaired " + n + " ids. Save Changes to keep them." : "No ids to repair.")
                    }
                }
                RawButton {
                    text: "Reload"
                    tip: "Read the table again (drops the grid's changes)"
                    iconSource: rawRoot.iconDir + "reset_32.png"
                    enabled: rawModel.columns > 0
                    onClicked: rawRoot.guarded(() => rawRoot.load(rawRoot.tableIndex))
                }
                RawButton {
                    text: rawModel.dirty ? "Save Changes •" : "Save Changes"
                    tip: "Write the changed cells into the " + (rawRoot.manager && rawRoot.manager.databaseHash !== undefined ? "database" : "save")
                         + " (Ctrl+S). Then save the file as usual to keep them."
                    iconSource: rawRoot.iconDir + "save_32.png"
                    accent: rawModel.dirty
                    enabled: rawModel.dirty
                    onClicked: rawRoot.saveChanges()
                }
            }
        }

        // Grid
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            rowSpacing: 0
            columnSpacing: 0

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

            HorizontalHeaderView {
                id: columnHeader
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                syncView: tableView
                clip: true
                delegate: Rectangle {
                    required property int column
                    implicitHeight: 32
                    implicitWidth: rawRoot.widths[column] || 80
                    color: BtDarkStyle.bgDark
                    border.color: BtDarkStyle.borderDark
                    border.width: 1
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 6
                        text: rawModel.headerName(column)
                        color: rawModel.columnReadOnly(column) ? BtDarkStyle.textDim : BtDarkStyle.accentColor
                        font.bold: true
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea {
                        id: headArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                    BtToolTip {
                        parent: headArea
                        text: rawModel.headerName(column) + (rawModel.columnReadOnly(column) ? " (read only)" : "")
                    }
                }
            }

            VerticalHeaderView {
                id: rowHeader
                Layout.fillHeight: true
                syncView: tableView
                clip: true
                implicitWidth: Math.max(44, String(rawModel.rows).length * 8 + 18)
                delegate: Rectangle {
                    required property int row
                    implicitWidth: rowHeader.implicitWidth
                    implicitHeight: 26
                    color: row === rawRoot.currentRow ? BtDarkStyle.bgMedium : BtDarkStyle.bgDark
                    Rectangle {
                        anchors.right: parent.right
                        width: 1
                        height: parent.height
                        color: BtDarkStyle.borderDark
                    }
                    Text {
                        anchors.fill: parent
                        anchors.rightMargin: 8
                        text: row + 1
                        color: row === rawRoot.currentRow ? BtDarkStyle.accentColor : BtDarkStyle.textDim
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
                model: rawModel
                selectionModel: selection
                selectionBehavior: TableView.SelectCells
                selectionMode: TableView.SingleSelection
                editTriggers: TableView.DoubleTapped | TableView.EditKeyPressed | TableView.AnyKeyPressed
                boundsBehavior: Flickable.StopAtBounds
                keyNavigationEnabled: true
                columnWidthProvider: (column) => rawRoot.widths[column] || 80
                rowHeightProvider: () => 26

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: cell
                    required property int row
                    required property int column
                    required property string display
                    required property bool readOnly
                    required property bool changed
                    required property bool current

                    implicitHeight: 26
                    color: cell.current ? BtDarkStyle.bgMedium
                         : row === rawRoot.currentRow ? Qt.rgba(1, 1, 1, 0.035)
                         : (row % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
                    border.color: cell.current ? BtDarkStyle.accentColor : "transparent"
                    border.width: cell.current ? 1 : 0

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
                    // A cell changed since the table was read.
                    Rectangle {
                        visible: cell.changed
                        width: 6
                        height: 6
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 2
                        color: BtDarkStyle.accentColor
                    }
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        text: cell.display
                        color: cell.changed ? BtDarkStyle.accentColor : cell.readOnly ? BtDarkStyle.textDim : BtDarkStyle.textMain
                        font.family: "Consolas"
                        font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    TableView.editDelegate: TextField {
                        anchors.fill: parent
                        leftPadding: 7
                        rightPadding: 7
                        topPadding: 0
                        bottomPadding: 0
                        text: cell.display
                        color: BtDarkStyle.textMain
                        selectionColor: BtDarkStyle.accentDim
                        font.family: "Consolas"
                        font.pixelSize: 12
                        verticalAlignment: TextInput.AlignVCenter
                        selectByMouse: true
                        background: Rectangle {
                            color: BtDarkStyle.bgDarkest
                            border.color: BtDarkStyle.accentColor
                            border.width: 1
                        }
                        Component.onCompleted: selectAll()
                        TableView.onCommit: rawModel.setText(cell.row, cell.column, text)
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: rawModel.rows === 0
                    text: rawModel.columns === 0 ? "No table" : "This table is empty in this file"
                    color: BtDarkStyle.textDim
                    font.pixelSize: 14
                }
            }
        }

        // Status bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: BtDarkStyle.bgDark
            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: BtDarkStyle.borderDark
            }
            Text {
                anchors.fill: parent
                anchors.leftMargin: 15
                anchors.rightMargin: 15
                text: rawRoot.statusText
                color: rawRoot.statusOk ? BtDarkStyle.textDim : Material.color(Material.Red)
                font.pixelSize: 12
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                maximumLineCount: 1
                MouseArea {
                    id: statusArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }
                BtToolTip { parent: statusArea; text: rawRoot.statusText.indexOf("\n") >= 0 ? rawRoot.statusText : "" }
            }
        }
    }

    FileDialog {
        id: csvDialog
        property bool exporting: true
        function start(exp) {
            exporting = exp
            currentFolder = rawRoot.folder()
            fileMode = exp ? FileDialog.SaveFile : FileDialog.OpenFile
            nameFilters = ["CSV (*.csv)", "All files (*)"]
            defaultSuffix = "csv"
            title = (exp ? "Export " : "Import ") + (rawRoot.table.title || "the table") + (exp ? " to CSV" : " from CSV")
            if (exp)
                selectedFile = rawRoot.folder() + "/" + encodeURIComponent(rawModel.suggestedFileName(rawRoot.filePrefix))
            open()
        }
        onAccepted: {
            const url = selectedFile.toString()
            settings.lastFolder = url.substring(0, url.lastIndexOf("/"))
            const name = decodeURIComponent(url.replace(/^file:\/{2,3}/, ""))
            if (exporting) {
                const error = rawModel.exportCsv(url)
                rawRoot.setStatus(error === "", error === "" ? "Exported " + rawModel.rows + " rows to " + name : error)
            } else {
                const message = rawModel.importCsv(url)
                if (message === "")
                    rawRoot.setStatus(true, "Imported " + rawModel.rows + " rows from " + name + ". Save Changes to write them.")
                else if (message.startsWith("!"))
                    rawRoot.setStatus(true, message.substring(1) + " Save Changes to write them.")
                else
                    rawRoot.setStatus(false, message)
                const w = []
                for (let c = 0; c < rawModel.columns; ++c)
                    w.push(rawModel.columnWidth(c))
                rawRoot.widths = w
                tableView.forceLayout()
            }
        }
    }

    Dialog {
        id: discardDialog
        property var action: null
        anchors.centerIn: parent
        modal: true
        title: "Unsaved changes"
        standardButtons: Dialog.Discard | Dialog.Cancel
        Label {
            text: "The grid has changes that aren't saved (Save Changes).\nDiscard them?"
            color: BtDarkStyle.textMain
        }
        onDiscarded: {
            close()
            rawRoot.load(rawRoot.tableIndex)   // Back to what the file has (not dirty any more).
            if (action)
                action()
            action = null
        }
        onRejected: action = null
    }

    component RawButton: AbstractButton {
        id: rawButton
        property url iconSource: ""
        property bool accent: false
        property string tip: ""
        BtToolTip { text: rawButton.tip }
        implicitHeight: 38
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        leftPadding: 10
        rightPadding: 10
        hoverEnabled: true
        opacity: enabled ? 1.0 : 0.4
        readonly property bool lit: hovered && enabled
        background: Rectangle {
            color: rawButton.lit ? BtDarkStyle.bgMedium : "transparent"
            border.color: rawButton.lit || rawButton.accent ? BtDarkStyle.accentColor : "transparent"
            border.width: 1
            radius: 4
        }
        contentItem: RowLayout {
            spacing: 6
            Image {
                visible: rawButton.iconSource.toString() !== ""
                source: rawButton.iconSource
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                Layout.alignment: Qt.AlignVCenter
                fillMode: Image.PreserveAspectFit
            }
            Text {
                text: rawButton.text
                color: rawButton.lit || rawButton.accent ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                font.bold: true
                font.pixelSize: 13
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }
}
