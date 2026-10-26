import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import com.wouldymods.blobset 1.0

// String Editor: the game's text\strings\*.str tables (key -> text, one table per language).
// Opened by double clicking a .str in MainForm (or with Open File... for a table on disk). Select a string on the
// left, edit its text on the right; Save to Mods writes the table into games\<game>\<platform>\mods at its blobset
// path, so Modify / Create put it in the game.
Item {
    id: editorRoot
    anchors.fill: parent

    readonly property string iconDir: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/"
    readonly property color errorColor: Material.color(Material.Red)
    readonly property color addedColor: "#5cb85c"

    property int currentRow: -1
    property int searchScope: 0           // 0 = keys and texts, 1 = keys, 2 = texts
    property int matchCount: 0
    property int matchPosition: 0

    property string statusText: ""
    property bool statusOk: true

    readonly property int keyColumnWidth: 300

    Rectangle {
        anchors.fill: parent
        color: BtDarkStyle.bgDarkest
        z: -1
    }

    function setStatus(ok, text) {
        statusOk = ok
        statusText = text
    }

    StringTableModel {
        id: strings
        onActionCompleted: (msg) => editorRoot.setStatus(true, msg)
        onErrorOccurred: (msg) => editorRoot.setStatus(false, msg)
        onModelReset: {
            editorRoot.selectRow(strings.count > 0 ? 0 : -1)
            editorRoot.updateMatches()
        }
    }

    Settings {
        id: settings
        category: "StringEditor"
        property string lastFolder: ""
    }

    Connections {
        target: appOrchestrator
        function onOpenStringEditorByHash(hexHash, displayName) {
            strings.loadFromBlobset(hexHash, displayName)
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Selection / search
    // ------------------------------------------------------------------------------------------------
    function selectRow(row) {
        currentRow = row
        if (row >= 0) {
            listView.currentIndex = row
            listView.positionViewAtIndex(row, ListView.Contain)
        }
        editor.load()
    }

    function updateMatches() {
        const query = searchInput.text.trim()
        if (query === "") {
            matchCount = 0
            matchPosition = 0
            return
        }
        const info = strings.searchInfo(query, currentRow, searchScope)
        matchCount = info.count
        matchPosition = info.position
    }

    function find(backward, fromCurrent) {
        const query = searchInput.text.trim()
        if (query === "" || strings.count === 0) {
            updateMatches()
            return
        }
        let start
        if (currentRow < 0)
            start = backward ? strings.count - 1 : 0
        else if (fromCurrent)
            start = currentRow
        else
            start = backward ? currentRow - 1 : currentRow + 1
        if (start < 0) start = strings.count - 1
        if (start >= strings.count) start = 0

        const row = strings.search(query, start, backward, searchScope)
        if (row >= 0) {
            selectRow(row)
            updateMatches()
            setStatus(true, "Match " + matchPosition + " of " + matchCount + ": " + strings.keyAt(row))
        } else {
            updateMatches()
            setStatus(false, "No string contains \"" + query + "\"")
        }
    }

    Timer {
        id: searchDelay
        interval: 250
        onTriggered: editorRoot.find(false, true)
    }

    // ------------------------------------------------------------------------------------------------
    // Unsaved changes guard
    // ------------------------------------------------------------------------------------------------
    property var pendingAction: null

    function guarded(action) {
        if (strings.modified) {
            pendingAction = action
            unsavedDialog.open()
        } else {
            action()
        }
    }

    function goBack() {
        appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml")
    }

    function saveToModsOrAs() {
        if (strings.workspacePath !== "")
            return strings.saveToMods() !== ""
        fileDialog.start("saveAs")
        return false
    }

    Shortcut { sequences: [StandardKey.Find]; onActivated: { searchInput.forceActiveFocus(); searchInput.selectAll() } }
    Shortcut { sequences: ["F3"]; onActivated: editorRoot.find(false, false) }
    Shortcut { sequences: ["Shift+F3"]; onActivated: editorRoot.find(true, false) }
    Shortcut { sequences: [StandardKey.Save]; enabled: strings.count > 0; onActivated: editorRoot.saveToModsOrAs() }

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

        readonly property bool lit: (hovered && enabled) || checked

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

    component SearchButton: AbstractButton {
        id: searchButton
        property string tip: ""
        BtToolTip { text: searchButton.tip }
        implicitWidth: contentItem.implicitWidth + 12
        implicitHeight: 24
        hoverEnabled: true
        opacity: enabled ? 1.0 : 0.35
        background: Rectangle {
            radius: 3
            color: searchButton.checked ? BtDarkStyle.bgDarkest
                                        : (searchButton.hovered && searchButton.enabled ? BtDarkStyle.bgHover : "transparent")
            border.color: searchButton.checked ? BtDarkStyle.accentDim : "transparent"
            border.width: 1
        }
        contentItem: Text {
            text: searchButton.text
            color: searchButton.checked || (searchButton.hovered && searchButton.enabled) ? BtDarkStyle.accentColor
                                                                                           : BtDarkStyle.textDim
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component PanelLabel: Text {
        color: BtDarkStyle.textDim
        font.pixelSize: 11
        font.bold: true
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.5
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
                spacing: 10

                HeaderButton {
                    text: "◀ Back"
                    tip: "Return to the main workspace"
                    onClicked: editorRoot.guarded(editorRoot.goBack)
                }

                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }

                Image {
                    source: editorRoot.iconDir + "str_32.png"
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    fillMode: Image.PreserveAspectFit
                }

                ColumnLayout {
                    spacing: 0
                    Layout.maximumWidth: 240
                    Text {
                        text: "String Editor"
                        color: BtDarkStyle.accentColor
                        font.bold: true
                        font.pixelSize: 16
                    }
                    Text {
                        Layout.fillWidth: true
                        text: strings.sourceName !== "" ? strings.sourceName : "No string table loaded"
                        color: BtDarkStyle.textDim
                        font.pixelSize: 11
                        font.family: "Consolas"
                        elide: Text.ElideMiddle
                    }
                }

                Item { Layout.fillWidth: true; Layout.minimumWidth: 6 }

                // Search
                Rectangle {
                    Layout.preferredWidth: 560
                    Layout.minimumWidth: 260
                    Layout.preferredHeight: 34
                    color: BtDarkStyle.bgMedium
                    border.width: 1
                    border.color: searchInput.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                    radius: 4
                    opacity: strings.count > 0 ? 1.0 : 0.5

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 4
                        spacing: 3

                        Text { text: "🔍"; font.pixelSize: 12; color: BtDarkStyle.textDim }

                        TextInput {
                            id: searchInput
                            Layout.fillWidth: true
                            Layout.leftMargin: 4
                            color: BtDarkStyle.textMain
                            selectionColor: BtDarkStyle.accentDim
                            font.pixelSize: 13
                            selectByMouse: true
                            clip: true
                            enabled: strings.count > 0
                            verticalAlignment: TextInput.AlignVCenter

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
                                text: "Search keys and texts..."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 13
                                font.italic: true
                                visible: searchInput.text === "" && !searchInput.activeFocus
                            }
                        }

                        Text {
                            visible: searchInput.text.trim() !== ""
                            text: editorRoot.matchCount === 0 ? "No matches"
                                  : (editorRoot.matchPosition > 0 ? editorRoot.matchPosition + " / " : "") + editorRoot.matchCount
                            color: editorRoot.matchCount === 0 ? editorRoot.errorColor : BtDarkStyle.textDim
                            font.pixelSize: 11
                            font.family: "Consolas"
                            Layout.rightMargin: 2
                        }

                        // Scope: All / Keys / Text
                        Repeater {
                            model: [{ label: "All", tip: "Search keys and texts" },
                                    { label: "Keys", tip: "Search the keys only" },
                                    { label: "Text", tip: "Search the texts only" }]
                            SearchButton {
                                required property var modelData
                                required property int index
                                text: modelData.label
                                tip: modelData.tip
                                checkable: true
                                checked: editorRoot.searchScope === index
                                onClicked: {
                                    editorRoot.searchScope = index
                                    editorRoot.find(false, true)
                                }
                            }
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
                    id: fileButton
                    text: "File ▾"
                    tip: "Open, save, import or export a string table"
                    iconSource: editorRoot.iconDir + "open_32.png"
                    onClicked: fileMenu.popup(fileButton, 0, fileButton.height)
                }

                HeaderButton {
                    text: "Save to Mods"
                    tip: strings.workspacePath !== ""
                         ? "Save into the mods folder as " + strings.workspacePath + ", ready for Modify (Ctrl+S)"
                         : "Opened from a file: use File > Save As"
                    iconSource: editorRoot.iconDir + "save_32.png"
                    accent: strings.modified
                    enabled: strings.count > 0 && strings.workspacePath !== ""
                    onClicked: strings.saveToMods()
                }
            }
        }

        // Body
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 4
                color: SplitHandle.hovered ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
            }

            // ---- String list -----------------------------------------------------------------------------
            ColumnLayout {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 400
                spacing: 0

                // Column titles
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    color: BtDarkStyle.bgDark
                    border.color: BtDarkStyle.borderDark
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        spacing: 0
                        Text {
                            text: "#"
                            color: BtDarkStyle.textDim
                            font.bold: true
                            font.pixelSize: 12
                            Layout.preferredWidth: 52
                        }
                        Text {
                            text: "Key"
                            color: BtDarkStyle.accentColor
                            font.bold: true
                            font.pixelSize: 13
                            Layout.preferredWidth: editorRoot.keyColumnWidth
                        }
                        Text {
                            text: "Text"
                            color: BtDarkStyle.accentColor
                            font.bold: true
                            font.pixelSize: 13
                            Layout.fillWidth: true
                        }
                    }
                }

                ListView {
                    id: listView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: strings
                    boundsBehavior: Flickable.StopAtBounds
                    highlightMoveDuration: 0
                    keyNavigationEnabled: true
                    focus: true
                    reuseItems: true
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    onCurrentIndexChanged: {
                        if (currentIndex !== editorRoot.currentRow && currentIndex >= 0) {
                            editorRoot.currentRow = currentIndex
                            editor.load()
                            editorRoot.updateMatches()
                        }
                    }

                    delegate: Rectangle {
                        id: row
                        required property int index
                        required property string key
                        required property string preview
                        required property bool modified
                        required property bool added

                        readonly property bool isCurrent: index === editorRoot.currentRow

                        width: listView.width
                        height: 28
                        color: isCurrent ? BtDarkStyle.bgMedium
                                         : (rowArea.containsMouse ? BtDarkStyle.bgHover
                                                                  : (index % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark))

                        Rectangle {
                            visible: row.isCurrent
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

                        MouseArea {
                            id: rowArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                listView.forceActiveFocus()
                                listView.currentIndex = row.index
                            }
                            onDoubleClicked: {
                                listView.currentIndex = row.index
                                textArea.forceActiveFocus()
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 0

                            Text {
                                text: row.index + 1
                                color: row.isCurrent ? BtDarkStyle.accentColor : BtDarkStyle.textDim
                                font.pixelSize: 11
                                font.family: "Consolas"
                                Layout.preferredWidth: 52
                            }
                            Text {
                                text: row.key
                                color: row.added ? editorRoot.addedColor
                                                 : (row.isCurrent ? BtDarkStyle.white : BtDarkStyle.accentDim)
                                font.pixelSize: 12
                                font.family: "Consolas"
                                elide: Text.ElideRight
                                Layout.preferredWidth: editorRoot.keyColumnWidth - 10
                                Layout.rightMargin: 10
                            }
                            Text {
                                text: row.preview
                                color: row.isCurrent ? BtDarkStyle.white : BtDarkStyle.textMain
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            // Changed since it was loaded.
                            Rectangle {
                                visible: row.modified
                                Layout.preferredWidth: 8
                                Layout.preferredHeight: 8
                                Layout.leftMargin: 8
                                radius: 4
                                color: row.added ? editorRoot.addedColor : BtDarkStyle.accentColor
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: strings.count === 0
                        text: "Double click a .str file in the workspace,\nor use File > Open String Table..."
                        horizontalAlignment: Text.AlignHCenter
                        color: BtDarkStyle.textDim
                        font.pixelSize: 14
                    }
                }
            }

            // ---- Editor panel ----------------------------------------------------------------------------
            Rectangle {
                id: editor
                SplitView.preferredWidth: 460
                SplitView.minimumWidth: 320
                color: BtDarkStyle.bgDark

                property bool loading: false

                // Shows the current row's key / text.
                function load() {
                    loading = true
                    keyField.text = strings.keyAt(editorRoot.currentRow)
                    textArea.text = strings.textAt(editorRoot.currentRow)
                    loading = false
                }

                // The list reports changes for the current row (revert / import): keep the panel in step.
                Connections {
                    target: strings
                    function onDataChanged(topLeft, bottomRight, roles) {
                        if (editorRoot.currentRow >= topLeft.row && editorRoot.currentRow <= bottomRight.row
                                && !textArea.activeFocus && !keyField.activeFocus)
                            editor.load()
                    }
                    function onRowsRemoved() { editor.load() }
                }

                readonly property bool hasRow: editorRoot.currentRow >= 0 && editorRoot.currentRow < strings.count
                readonly property bool rowChanged: hasRow && (strings.count, strings.modifiedCount,
                                                   strings.textAt(editorRoot.currentRow) !== strings.originalTextAt(editorRoot.currentRow))

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    enabled: editor.hasRow
                    opacity: editor.hasRow ? 1.0 : 0.5

                    RowLayout {
                        Layout.fillWidth: true
                        PanelLabel { text: "Key"; Layout.fillWidth: true }
                        Text {
                            text: editor.hasRow ? "Row " + (editorRoot.currentRow + 1) + " of " + strings.count : ""
                            color: BtDarkStyle.textDim
                            font.pixelSize: 11
                        }
                    }

                    TextField {
                        id: keyField
                        Layout.fillWidth: true
                        font.family: "Consolas"
                        font.pixelSize: 13
                        color: BtDarkStyle.accentColor
                        selectByMouse: true
                        selectionColor: BtDarkStyle.accentDim
                        leftPadding: 10
                        background: Rectangle {
                            implicitHeight: 34
                            color: BtDarkStyle.bgMedium
                            radius: 4
                            border.width: 1
                            border.color: keyField.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                        }
                        // Renaming a key is refused when it's empty or already used (the status bar says why).
                        onEditingFinished: {
                            if (!editor.loading && editor.hasRow && !strings.setKey(editorRoot.currentRow, text))
                                text = strings.keyAt(editorRoot.currentRow)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        PanelLabel { text: "Text"; Layout.fillWidth: true }
                        Text {
                            text: textArea.length + " characters" + (textArea.lineCount > 1 ? ", " + textArea.lineCount + " lines" : "")
                            color: BtDarkStyle.textDim
                            font.pixelSize: 11
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: BtDarkStyle.bgMedium
                        radius: 4
                        border.width: 1
                        border.color: textArea.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark

                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 1
                            clip: true

                            TextArea {
                                id: textArea
                                wrapMode: TextArea.Wrap
                                color: BtDarkStyle.textMain
                                selectionColor: BtDarkStyle.accentDim
                                selectedTextColor: BtDarkStyle.white
                                font.pixelSize: 14
                                selectByMouse: true
                                persistentSelection: true
                                padding: 10
                                background: null
                                // Every edit goes straight into the table (Save writes it).
                                onTextChanged: {
                                    if (!editor.loading && editor.hasRow)
                                        strings.setText(editorRoot.currentRow, text)
                                }
                            }
                        }
                    }

                    // Original text, while it differs.
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: editor.rowChanged && strings.originalTextAt(editorRoot.currentRow) !== ""
                        spacing: 4
                        PanelLabel { text: "Original" }
                        Text {
                            Layout.fillWidth: true
                            Layout.maximumHeight: 80
                            text: editor.hasRow ? strings.originalTextAt(editorRoot.currentRow) : ""
                            color: BtDarkStyle.textDim
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            elide: Text.ElideRight
                            clip: true
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Keep placeholders such as %s, %d and {name} as they are: the game fills them in."
                        color: BtDarkStyle.textDim
                        font.pixelSize: 11
                        font.italic: true
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        HeaderButton {
                            text: "Revert"
                            tip: "Put this string back the way it was loaded"
                            iconSource: editorRoot.iconDir + "reset_32.png"
                            implicitHeight: 34
                            enabled: editor.rowChanged
                            onClicked: {
                                strings.revert(editorRoot.currentRow)
                                editor.load()
                            }
                        }
                        Item { Layout.fillWidth: true }
                        HeaderButton {
                            text: "Add"
                            tip: "Add a new string after the last one"
                            iconSource: editorRoot.iconDir + "create_32.png"
                            implicitHeight: 34
                            enabled: strings.count > 0
                            onClicked: {
                                const row = strings.addEntry(strings.uniqueKey("NewString"), "")
                                if (row >= 0) {
                                    editorRoot.selectRow(row)
                                    keyField.forceActiveFocus()
                                    keyField.selectAll()
                                }
                            }
                        }
                        HeaderButton {
                            text: "Delete"
                            tip: "Remove this string from the table"
                            iconSource: editorRoot.iconDir + "close_32.png"
                            implicitHeight: 34
                            onClicked: deleteDialog.open()
                        }
                    }
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
                    text: "Status: " + (editorRoot.statusText !== "" ? editorRoot.statusText
                                                                     : "Ready | Select a string to edit its text")
                }
                Text {
                    text: strings.count + " strings" + (strings.modifiedCount > 0 ? "  ·  " + strings.modifiedCount + " changed"
                                                                                  : (strings.modified ? "  ·  unsaved changes" : ""))
                    color: strings.modified ? BtDarkStyle.accentColor : BtDarkStyle.textDim
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
    // Menus / dialogs
    // ------------------------------------------------------------------------------------------------
    Menu {
        id: fileMenu
        width: 240

        MenuItem {
            text: "Open String Table..."
            icon.source: editorRoot.iconDir + "open_32.png"
            icon.color: "transparent"
            onTriggered: editorRoot.guarded(() => fileDialog.start("open"))
        }
        MenuItem {
            text: "Save As..."
            icon.source: editorRoot.iconDir + "save_32.png"
            icon.color: "transparent"
            enabled: strings.count > 0
            onTriggered: fileDialog.start("saveAs")
        }
        MenuSeparator {}
        MenuItem {
            text: "Export CSV..."
            icon.source: editorRoot.iconDir + "extract_32.png"
            icon.color: "transparent"
            enabled: strings.count > 0
            onTriggered: fileDialog.start("exportCsv")
        }
        MenuItem {
            text: "Import CSV..."
            icon.source: editorRoot.iconDir + "modify_32.png"
            icon.color: "transparent"
            enabled: strings.count > 0
            onTriggered: fileDialog.start("importCsv")
        }
    }

    FileDialog {
        id: fileDialog
        property string mode: "open"

        function baseName() {
            let name = strings.sourceName.replace(/\\/g, "/")
            name = name.substring(name.lastIndexOf("/") + 1)
            const dot = name.lastIndexOf(".")
            return dot > 0 ? name.substring(0, dot) : (name !== "" ? name : "strings")
        }

        function start(m) {
            mode = m
            const folder = settings.lastFolder !== "" ? settings.lastFolder
                                                      : StandardPaths.writableLocation(StandardPaths.DocumentsLocation).toString()
            currentFolder = folder
            const csv = m === "exportCsv" || m === "importCsv"
            fileMode = (m === "open" || m === "importCsv") ? FileDialog.OpenFile : FileDialog.SaveFile
            nameFilters = csv ? ["CSV (*.csv)", "All files (*)"] : ["String tables (*.str)", "All files (*)"]
            defaultSuffix = csv ? "csv" : "str"
            title = m === "open" ? "Open a string table" : m === "saveAs" ? "Save the string table"
                  : m === "exportCsv" ? "Export the strings as CSV" : "Import strings from a CSV"
            if (fileMode === FileDialog.SaveFile)
                selectedFile = folder + "/" + encodeURIComponent(baseName() + (csv ? ".csv" : ".str"))
            open()
        }

        onAccepted: {
            const url = selectedFile.toString()
            settings.lastFolder = url.substring(0, url.lastIndexOf("/"))
            if (mode === "open")
                strings.loadFile(url)
            else if (mode === "saveAs")
                strings.saveAs(url)
            else if (mode === "exportCsv")
                strings.exportCsv(url)
            else if (mode === "importCsv") {
                strings.importCsv(url)
                editor.load()
                editorRoot.updateMatches()
            }
        }
    }

    component ThemedDialog: Dialog {
        id: themedDialog
        property string message: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 440
        padding: 20
        background: Rectangle {
            color: BtDarkStyle.bgDark
            border.color: BtDarkStyle.accentColor
            border.width: 1
            radius: 6
        }
        header: Text {
            text: themedDialog.title
            color: BtDarkStyle.accentColor
            font.bold: true
            font.pixelSize: 15
            leftPadding: 20
            topPadding: 16
        }
        contentItem: Text {
            text: themedDialog.message
            color: BtDarkStyle.textMain
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
    }

    ThemedDialog {
        id: deleteDialog
        title: "Delete string"
        message: "Remove \"" + strings.keyAt(editorRoot.currentRow) + "\" from the table?"
                 + "\n\nThe game shows the key instead of a text when something still asks for it."
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            HeaderButton {
                text: "Delete"
                accent: true
                implicitHeight: 34
                Layout.bottomMargin: 14
                onClicked: {
                    const row = editorRoot.currentRow
                    deleteDialog.close()
                    strings.removeEntry(row)
                    editorRoot.selectRow(Math.min(row, strings.count - 1))
                }
            }
            HeaderButton {
                text: "Cancel"
                implicitHeight: 34
                Layout.rightMargin: 16
                Layout.bottomMargin: 14
                onClicked: deleteDialog.close()
            }
        }
    }

    ThemedDialog {
        id: unsavedDialog
        title: "Unsaved changes"
        message: "The string table has unsaved changes. Save them first?"
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            HeaderButton {
                text: strings.workspacePath !== "" ? "Save to Mods" : "Save As..."
                accent: true
                implicitHeight: 34
                Layout.bottomMargin: 14
                onClicked: {
                    unsavedDialog.close()
                    const saved = editorRoot.saveToModsOrAs()
                    if (saved && editorRoot.pendingAction)
                        editorRoot.pendingAction()
                    editorRoot.pendingAction = null
                }
            }
            HeaderButton {
                text: "Discard"
                implicitHeight: 34
                Layout.bottomMargin: 14
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
