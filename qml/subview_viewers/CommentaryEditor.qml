import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import com.wouldymods.blobset 1.0

// Commentary Editor: Rugby League 26's commentary and referee dialogue (CommentaryEditorManager).
//   sound\lines.commentary.cmtry / sound\lines.referee.dat: each commentator's / referee's lines by category - the
//     subtitle text, the sound it plays (its id and parts), who it's said about (he / she) and its sub-categories.
//   sound\categoryinfo.commentary.cmtry / categoryinfo.referee.cmtry: the settings of each category.
// Opened by double clicking one of them in the workspace, or from the Commentary Editor menu item (pick a file).
// Save to Mods writes the file into games\<game>\<platform>\mods at its blobset path, ready for Modify.
Item {
    id: editorRoot
    anchors.fill: parent

    readonly property string iconDir: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/"
    readonly property color errorColor: Material.color(Material.Red)
    readonly property color addedColor: "#5cb85c"
    // Read from the manager every time (not a binding): a handler of the manager's signal can run before a binding
    // on it has caught up, which left the lists empty when a file was opened by a double click.
    readonly property bool isLines: cm.kind === "lines"
    readonly property bool isInfo: cm.kind === "categories"
    function linesLoaded() { return cm.kind === "lines" }
    function infoLoaded() { return cm.kind === "categories" }

    property var speakersModel: []
    property var categoriesModel: []
    property var infoModel: []
    property var lineData: ({})
    property int speaker: 0
    property int category: -1        // Index in the speaker's categories (not in the filtered list).
    property int lineIndex: -1
    property int matchCount: 0
    property int infoSelected: -1
    property var filesModel: []

    readonly property var genders: ["They / them (any player)", "He / him", "She / her"]
    readonly property var infoValueNames: ["Value 1", "Value 2", "Value 3", "Value 4", "Value 5", "Value 6"]
    readonly property var infoTypes: ["0 - Sequence", "1 - Event", "2 - Reply", "3 - Sub-category"]

    Rectangle {
        anchors.fill: parent
        color: BtDarkStyle.bgDarkest
        z: -1
    }

    CommentaryEditorManager {
        id: cm
        onLoadedChanged: Qt.callLater(editorRoot.reloadAll)
    }

    Settings {
        id: settings
        category: "CommentaryEditor"
        property string lastFolder: ""
    }

    Connections {
        target: appOrchestrator
        function onOpenCommentaryEditorByHash(hexHash, displayName) {
            cm.loadFromBlobset(hexHash, displayName)
            // WAVs dropped into a sound\\commentary\\<speaker> folder on MainForm: a new line for each.
            const pending = appOrchestrator.takePendingCommentaryWavs()
            if (pending.wavs && pending.wavs.length > 0)
                Qt.callLater(() => editorRoot.startWavLines(pending.wavs, pending.folder || ""))
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Models (refreshed from the manager: bindings on its functions don't follow its edits)
    // ------------------------------------------------------------------------------------------------
    function reloadAll() {
        speakersModel = linesLoaded() ? cm.speakers() : []
        if (speaker >= speakersModel.length)
            speaker = 0
        speakerCombo.currentIndex = speaker
        category = -1
        lineIndex = -1
        refreshCategories()
        refreshInfo()
        updateMatches()
    }

    function refreshCategories() {
        categoriesModel = linesLoaded() ? cm.categories(speaker, categoryFilter.text) : []
        if (category < 0 && categoriesModel.length > 0)
            category = categoriesModel[0].index
        refreshLines()
    }

    // The lines of the category, kept in a ListModel so typing a text only updates its row.
    function refreshLines() {
        const rows = linesLoaded() && category >= 0 ? cm.lines(speaker, category) : []
        linesModel.clear()
        for (let i = 0; i < rows.length; ++i)
            linesModel.append(rows[i])
        if (lineIndex >= rows.length)
            lineIndex = rows.length - 1
        if (lineIndex < 0 && rows.length > 0)
            lineIndex = 0
        refreshLine()
    }

    function refreshLine() {
        lineData = linesLoaded() && lineIndex >= 0 ? cm.line(speaker, category, lineIndex) : ({})
        lineEditor.load()
    }

    // After an edit of the current line: its row in the list and the panel's extras (not the text being typed).
    function refreshLineRow() {
        if (lineIndex < 0 || lineIndex >= linesModel.count)
            return
        const rows = cm.lines(speaker, category)
        if (lineIndex < rows.length) {
            const r = rows[lineIndex]
            linesModel.set(lineIndex, r)
        }
        lineData = cm.line(speaker, category, lineIndex)
    }

    function refreshInfo() {
        infoModel = infoLoaded() ? cm.infoRows(infoFilter.text) : []
    }

    function selectSpeaker(s) {
        speaker = s
        speakerCombo.currentIndex = s
        category = -1
        lineIndex = -1
        refreshCategories()
    }

    function selectCategory(c) {
        if (c === category)
            return
        category = c
        lineIndex = -1
        refreshLines()
    }

    function selectLine(l) {
        lineIndex = l
        linesView.currentIndex = l
        linesView.positionViewAtIndex(l, ListView.Contain)
        refreshLine()
    }

    // ------------------------------------------------------------------------------------------------
    // Search (every line's text, parts and id)
    // ------------------------------------------------------------------------------------------------
    function updateMatches() {
        matchCount = linesLoaded() && searchInput.text.trim() !== "" ? cm.countMatches(searchInput.text) : 0
    }

    function find(backward) {
        const query = searchInput.text.trim()
        if (query === "" || !linesLoaded())
            return
        const hit = cm.find(query, speaker, category, lineIndex, backward)
        if (hit.speaker === undefined) {
            setStatus(false, "No line contains \"" + query + "\"")
            return
        }
        if (hit.speaker !== speaker) {
            speaker = hit.speaker
            speakerCombo.currentIndex = hit.speaker
            categoryFilter.text = ""
            categoriesModel = cm.categories(speaker, "")
        } else if (!categoriesModel.some((c) => c.index === hit.category)) {
            categoryFilter.text = ""
            categoriesModel = cm.categories(speaker, "")
        }
        category = hit.category
        const row = categoriesModel.findIndex((c) => c.index === hit.category)
        if (row >= 0)
            categoryList.positionViewAtIndex(row, ListView.Contain)
        lineIndex = hit.line
        refreshLines()
        selectLine(hit.line)
    }

    Timer {
        id: searchDelay
        interval: 300
        onTriggered: editorRoot.updateMatches()
    }

    property string localStatus: ""
    property bool localStatusOk: true
    function setStatus(ok, text) {
        localStatusOk = ok
        localStatus = text
        localStatusTimer.restart()
    }
    Timer { id: localStatusTimer; interval: 6000; onTriggered: editorRoot.localStatus = "" }
    Connections {
        target: cm
        function onStatusChanged() { editorRoot.localStatus = "" }
    }

    // ------------------------------------------------------------------------------------------------
    // Unsaved changes guard
    // ------------------------------------------------------------------------------------------------
    property var pendingAction: null

    function guarded(action) {
        if (cm.modified) {
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
        if (cm.workspacePath !== "")
            return cm.saveToMods() !== ""
        fileDialog.start("saveAs")
        return false
    }

    Shortcut { sequences: [StandardKey.Find]; enabled: editorRoot.isLines; onActivated: { searchInput.forceActiveFocus(); searchInput.selectAll() } }
    Shortcut { sequences: ["F3"]; enabled: editorRoot.isLines; onActivated: editorRoot.find(false) }
    Shortcut { sequences: ["Shift+F3"]; enabled: editorRoot.isLines; onActivated: editorRoot.find(true) }
    Shortcut { sequences: [StandardKey.Save]; enabled: cm.loaded; onActivated: editorRoot.saveToModsOrAs() }

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

    component SmallButton: AbstractButton {
        id: smallButton
        property string tip: ""
        property bool accent: false
        BtToolTip { text: smallButton.tip }
        implicitHeight: 28
        implicitWidth: Math.max(28, smallText.implicitWidth + 18)
        hoverEnabled: true
        opacity: enabled ? 1.0 : 0.35
        background: Rectangle {
            radius: 4
            color: smallButton.down ? BtDarkStyle.bgDarkest : smallButton.hovered ? BtDarkStyle.bgHover : BtDarkStyle.bgMedium
            border.color: smallButton.hovered || smallButton.accent ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
            border.width: 1
        }
        contentItem: Text {
            id: smallText
            text: smallButton.text
            color: smallButton.hovered || smallButton.accent ? BtDarkStyle.accentColor : BtDarkStyle.textMain
            font.pixelSize: 12
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
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
            color: searchButton.hovered && searchButton.enabled ? BtDarkStyle.bgHover : "transparent"
        }
        contentItem: Text {
            text: searchButton.text
            color: searchButton.hovered && searchButton.enabled ? BtDarkStyle.accentColor : BtDarkStyle.textDim
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

    component DarkField: TextField {
        id: darkField
        color: BtDarkStyle.textMain
        selectByMouse: true
        selectionColor: BtDarkStyle.accentDim
        font.pixelSize: 13
        leftPadding: 8
        topPadding: 0
        bottomPadding: 0
        verticalAlignment: TextInput.AlignVCenter
        implicitHeight: 32
        background: Rectangle {
            color: BtDarkStyle.bgMedium
            radius: 4
            border.width: 1
            border.color: darkField.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
        }
    }

    component DarkCombo: ComboBox {
        id: darkCombo
        implicitHeight: 32
        font.pixelSize: 13
        background: Rectangle {
            color: BtDarkStyle.bgMedium
            radius: 4
            border.color: darkCombo.hovered ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
        }
        delegate: ItemDelegate {
            required property int index
            required property var modelData
            width: darkCombo.width
            implicitHeight: 30
            highlighted: darkCombo.highlightedIndex === index
            contentItem: Text {
                text: typeof modelData === "object" && modelData !== null ? modelData[darkCombo.textRole] : modelData
                color: parent.highlighted ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle { color: parent.highlighted ? BtDarkStyle.bgHover : "transparent" }
        }
    }

    component DarkCheck: CheckBox {
        id: darkCheck
        font.pixelSize: 12
        padding: 2
        contentItem: Text {
            text: darkCheck.text
            leftPadding: darkCheck.indicator.width + 6
            color: BtDarkStyle.textMain
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    // A row of the empty page: one of the four files.
    component FileCard: Rectangle {
        id: fileCard
        property var file: ({})
        signal open()
        Layout.fillWidth: true
        implicitHeight: 64
        radius: 6
        color: cardArea.containsMouse && fileCard.file.available ? BtDarkStyle.bgHover : BtDarkStyle.bgDark
        border.color: cardArea.containsMouse && fileCard.file.available ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
        opacity: fileCard.file.available ? 1.0 : 0.45
        MouseArea {
            id: cardArea
            anchors.fill: parent
            hoverEnabled: true
            enabled: fileCard.file.available === true
            cursorShape: Qt.PointingHandCursor
            onClicked: fileCard.open()
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 14
            Image {
                source: editorRoot.iconDir + "commentary_editor_32.png"
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
            }
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Text {
                    text: fileCard.file.name || ""
                    color: BtDarkStyle.white
                    font.pixelSize: 14
                    font.bold: true
                }
                Text {
                    text: (fileCard.file.path || "") + "   ·   0x" + String(fileCard.file.hash || "").toUpperCase()
                          + (fileCard.file.available ? "" : "   ·   not in the loaded blobset")
                    color: BtDarkStyle.textDim
                    font.pixelSize: 11
                    font.family: "Consolas"
                }
            }
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
                spacing: 10

                HeaderButton {
                    text: "◀ Back"
                    tip: "Return to the main workspace"
                    onClicked: editorRoot.guarded(editorRoot.goBack)
                }

                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }

                Image {
                    source: editorRoot.iconDir + "commentary_editor_32.png"
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    fillMode: Image.PreserveAspectFit
                }

                ColumnLayout {
                    spacing: 0
                    Layout.maximumWidth: 320
                    Text {
                        text: "Commentary Editor"
                        color: BtDarkStyle.accentColor
                        font.bold: true
                        font.pixelSize: 16
                    }
                    Text {
                        Layout.fillWidth: true
                        text: cm.loaded ? cm.sourceName : "No commentary file loaded"
                        color: BtDarkStyle.textDim
                        font.pixelSize: 11
                        font.family: "Consolas"
                        elide: Text.ElideMiddle
                    }
                }

                Item { Layout.fillWidth: true; Layout.minimumWidth: 6 }

                // Search (lines)
                Rectangle {
                    visible: editorRoot.isLines
                    Layout.preferredWidth: 460
                    Layout.minimumWidth: 220
                    Layout.preferredHeight: 34
                    color: BtDarkStyle.bgMedium
                    border.width: 1
                    border.color: searchInput.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                    radius: 4

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
                            verticalAlignment: TextInput.AlignVCenter
                            onTextEdited: searchDelay.restart()
                            Keys.onReturnPressed: (event) => editorRoot.find((event.modifiers & Qt.ShiftModifier) !== 0)
                            Keys.onEnterPressed: (event) => editorRoot.find((event.modifiers & Qt.ShiftModifier) !== 0)
                            Keys.onEscapePressed: {
                                text = ""
                                editorRoot.updateMatches()
                            }
                            Text {
                                anchors.fill: parent
                                verticalAlignment: Text.AlignVCenter
                                text: "Search every line's text, sound and id..."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 13
                                font.italic: true
                                visible: searchInput.text === "" && !searchInput.activeFocus
                            }
                        }
                        Text {
                            visible: searchInput.text.trim() !== ""
                            text: editorRoot.matchCount === 0 ? "No matches" : editorRoot.matchCount + " lines"
                            color: editorRoot.matchCount === 0 ? editorRoot.errorColor : BtDarkStyle.textDim
                            font.pixelSize: 11
                            font.family: "Consolas"
                            Layout.rightMargin: 2
                        }
                        SearchButton {
                            text: "▲"
                            tip: "Previous match (Shift+Enter / Shift+F3)"
                            enabled: editorRoot.matchCount > 0
                            onClicked: editorRoot.find(true)
                        }
                        SearchButton {
                            text: "▼"
                            tip: "Next match (Enter / F3)"
                            enabled: editorRoot.matchCount > 0
                            onClicked: editorRoot.find(false)
                        }
                        SearchButton {
                            text: "✕"
                            tip: "Clear the search"
                            visible: searchInput.text !== ""
                            onClicked: {
                                searchInput.text = ""
                                editorRoot.updateMatches()
                            }
                        }
                    }
                }

                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }

                HeaderButton {
                    id: openButton
                    text: "Files ▾"
                    tip: "Open one of the commentary files from the blobset, or a file on disk"
                    iconSource: editorRoot.iconDir + "open_32.png"
                    onClicked: openMenu.popup(openButton, 0, openButton.height)
                }

                HeaderButton {
                    text: "Save to Mods"
                    tip: cm.workspacePath !== "" ? "Save into the mods folder as " + cm.workspacePath + ", ready for Modify (Ctrl+S)"
                                                 : "Opened from a file: use Files > Save As"
                    iconSource: editorRoot.iconDir + "save_32.png"
                    accent: cm.modified
                    enabled: cm.loaded && cm.workspacePath !== ""
                    onClicked: cm.saveToMods()
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: editorRoot.isLines ? 1 : editorRoot.isInfo ? 2 : 0

            // ---- Nothing loaded: pick a file ------------------------------------------------------------------
            Item {
                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 60, 640)
                    spacing: 10
                    Text {
                        text: "Open a commentary file"
                        color: BtDarkStyle.accentColor
                        font.pixelSize: 18
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Rugby League 26's commentary and referee dialogue. The lines files hold each commentator's and referee's "
                              + "lines (subtitle text, the sound each plays, he / she, sub-categories); the category info files hold "
                              + "each category's settings."
                        color: BtDarkStyle.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.bottomMargin: 6
                    }
                    Repeater {
                        id: fileCards
                        model: editorRoot.filesModel
                        FileCard {
                            required property var modelData
                            file: modelData
                            onOpen: cm.loadFromBlobset(modelData.hash, modelData.path)
                        }
                    }
                    RowLayout {
                        Layout.topMargin: 6
                        spacing: 10
                        HeaderButton {
                            text: "Open a File..."
                            iconSource: editorRoot.iconDir + "open_32.png"
                            implicitHeight: 34
                            onClicked: fileDialog.start("open")
                        }
                        Text {
                            text: "A blobset needs to be loaded to open the game's own files."
                            color: BtDarkStyle.textDim
                            font.pixelSize: 11
                            font.italic: true
                        }
                    }
                    Component.onCompleted: editorRoot.filesModel = cm.blobsetFiles()
                }
            }

            // ---- Lines ------------------------------------------------------------------------------------------
            SplitView {
                orientation: Qt.Horizontal
                handle: Rectangle {
                    implicitWidth: 4
                    color: SplitHandle.hovered ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                }

                // Speakers and categories
                Rectangle {
                    SplitView.preferredWidth: 310
                    SplitView.minimumWidth: 230
                    color: BtDarkStyle.bgDark

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        PanelLabel { text: cm.title === "RefereeDialogue" ? "Referee" : "Commentator" }
                        DarkCombo {
                            id: speakerCombo
                            Layout.fillWidth: true
                            textRole: "label"
                            model: editorRoot.speakersModel.map((s) => ({ label: s.name + "  (" + s.lines + " lines)" }))
                            currentIndex: editorRoot.speaker
                            onActivated: (i) => editorRoot.selectSpeaker(i)
                        }
                        Text {
                            Layout.fillWidth: true
                            readonly property var sp: editorRoot.speakersModel[editorRoot.speaker] || ({})
                            text: (sp.folder || "") + "  ·  id " + (sp.id !== undefined ? sp.id : "") + "\n" + (sp.bank || "")
                            color: BtDarkStyle.textDim
                            font.pixelSize: 10
                            font.family: "Consolas"
                            elide: Text.ElideRight
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            PanelLabel { text: "Categories"; Layout.fillWidth: true }
                            Text {
                                text: editorRoot.categoriesModel.length + (categoryFilter.text !== "" ? " found" : "")
                                color: BtDarkStyle.textDim
                                font.pixelSize: 11
                            }
                        }
                        DarkField {
                            id: categoryFilter
                            Layout.fillWidth: true
                            placeholderText: "Filter by name or line text..."
                            placeholderTextColor: BtDarkStyle.textDim
                            onTextEdited: categoryDelay.restart()
                            Timer {
                                id: categoryDelay
                                interval: 300
                                onTriggered: {
                                    editorRoot.categoriesModel = cm.categories(editorRoot.speaker, categoryFilter.text)
                                    if (editorRoot.categoriesModel.length > 0
                                            && !editorRoot.categoriesModel.some((c) => c.index === editorRoot.category))
                                        editorRoot.selectCategory(editorRoot.categoriesModel[0].index)
                                }
                            }
                        }

                        ListView {
                            id: categoryList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: editorRoot.categoriesModel
                            boundsBehavior: Flickable.StopAtBounds
                            reuseItems: true
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                id: catRow
                                required property var modelData
                                required property int index
                                readonly property bool isCurrent: modelData.index === editorRoot.category
                                width: categoryList.width
                                height: 28
                                color: isCurrent ? BtDarkStyle.bgMedium : catArea.containsMouse ? BtDarkStyle.bgHover
                                                                                               : (index % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
                                Rectangle { visible: catRow.isCurrent; width: 3; height: parent.height; color: BtDarkStyle.accentColor }
                                MouseArea {
                                    id: catArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: editorRoot.selectCategory(catRow.modelData.index)
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 8
                                    spacing: 6
                                    Text {
                                        text: catRow.modelData.name
                                        color: catRow.isCurrent ? BtDarkStyle.white : BtDarkStyle.textMain
                                        font.pixelSize: 12
                                        font.family: "Consolas"
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        visible: catRow.modelData.match > 0
                                        text: catRow.modelData.match + " ✓"
                                        color: BtDarkStyle.accentColor
                                        font.pixelSize: 10
                                    }
                                    Text {
                                        text: catRow.modelData.lines
                                        color: BtDarkStyle.textDim
                                        font.pixelSize: 11
                                        font.family: "Consolas"
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            SmallButton {
                                text: "Add"
                                tip: "Add a category to this speaker"
                                onClicked: nameDialog.ask("addCategory", "Add a category", "")
                            }
                            SmallButton {
                                text: "Rename"
                                tip: "Rename this category (lines in other categories that play it as c:<name> need changing too)"
                                enabled: editorRoot.category >= 0
                                onClicked: {
                                    const c = editorRoot.categoriesModel.find((x) => x.index === editorRoot.category)
                                    nameDialog.ask("renameCategory", "Rename the category", c ? c.name : "")
                                }
                            }
                            SmallButton {
                                text: "Sub-categories"
                                tip: "Edit the sub-categories lines can be marked with"
                                enabled: editorRoot.category >= 0
                                onClicked: subsDialog.start()
                            }
                            Item { Layout.fillWidth: true }
                            SmallButton {
                                text: "Delete"
                                tip: "Remove this category and all its lines"
                                enabled: editorRoot.category >= 0
                                onClicked: confirmDialog.ask("removeCategory", "Delete category",
                                                             "Remove this category and its " + linesModel.count + " lines from " + speakerCombo.currentText + "?")
                            }
                        }
                    }
                }

                // Lines of the category
                ColumnLayout {
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 360
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        color: BtDarkStyle.bgDark
                        border.color: BtDarkStyle.borderDark
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 8
                            spacing: 10
                            Text {
                                readonly property var c: editorRoot.categoriesModel.find((x) => x.index === editorRoot.category)
                                text: c ? c.name : ""
                                color: BtDarkStyle.accentColor
                                font.pixelSize: 13
                                font.bold: true
                                font.family: "Consolas"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: linesModel.count + " lines"
                                color: BtDarkStyle.textDim
                                font.pixelSize: 11
                            }
                            SmallButton {
                                text: "▲"
                                tip: "Move the line up"
                                enabled: editorRoot.lineIndex > 0
                                onClicked: {
                                    if (cm.moveLine(editorRoot.speaker, editorRoot.category, editorRoot.lineIndex, -1)) {
                                        const to = editorRoot.lineIndex - 1
                                        editorRoot.refreshLines()
                                        editorRoot.selectLine(to)
                                    }
                                }
                            }
                            SmallButton {
                                text: "▼"
                                tip: "Move the line down"
                                enabled: editorRoot.lineIndex >= 0 && editorRoot.lineIndex < linesModel.count - 1
                                onClicked: {
                                    if (cm.moveLine(editorRoot.speaker, editorRoot.category, editorRoot.lineIndex, 1)) {
                                        const to = editorRoot.lineIndex + 1
                                        editorRoot.refreshLines()
                                        editorRoot.selectLine(to)
                                    }
                                }
                            }
                            SmallButton {
                                text: "Add"
                                tip: "Add a blank line to this category"
                                enabled: editorRoot.category >= 0
                                onClicked: editorRoot.addLine(-1)
                            }
                            SmallButton {
                                text: "From WAV"
                                tip: "New line from WAV: makes a new sound (a WEM in the mods folder) from a .wav and a line in this category that plays it"
                                enabled: editorRoot.category >= 0 && cm.workspacePath !== ""
                                onClicked: fileDialog.start("wav")
                            }
                            SmallButton {
                                text: "Duplicate"
                                tip: "Copy this line to the end of the category"
                                enabled: editorRoot.lineIndex >= 0
                                onClicked: editorRoot.addLine(editorRoot.lineIndex)
                            }
                            SmallButton {
                                text: "Delete"
                                tip: "Remove this line"
                                enabled: editorRoot.lineIndex >= 0
                                onClicked: {
                                    const at = editorRoot.lineIndex
                                    if (cm.removeLine(editorRoot.speaker, editorRoot.category, at)) {
                                        editorRoot.refreshLines()
                                        if (linesModel.count > 0)
                                            editorRoot.selectLine(Math.min(at, linesModel.count - 1))
                                        editorRoot.refreshCategoryCounts()
                                    }
                                }
                            }
                        }
                    }

                    ListModel { id: linesModel }

                    ListView {
                        id: linesView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: linesModel
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: Rectangle {
                            id: lineRow
                            required property int index
                            required property string text
                            required property string parts
                            required property int gender
                            required property string subNames
                            required property bool modified
                            required property bool added
                            readonly property bool isCurrent: index === editorRoot.lineIndex
                            width: linesView.width
                            height: lineColumn.implicitHeight + 12
                            color: isCurrent ? BtDarkStyle.bgMedium : lineArea.containsMouse ? BtDarkStyle.bgHover
                                                                                           : (index % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
                            Rectangle { visible: lineRow.isCurrent; width: 3; height: parent.height; color: BtDarkStyle.accentColor }
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: BtDarkStyle.borderDark }
                            MouseArea {
                                id: lineArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: editorRoot.selectLine(lineRow.index)
                                onDoubleClicked: {
                                    editorRoot.selectLine(lineRow.index)
                                    lineText.forceActiveFocus()
                                }
                            }
                            RowLayout {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 10
                                Text {
                                    text: lineRow.index + 1
                                    color: lineRow.isCurrent ? BtDarkStyle.accentColor : BtDarkStyle.textDim
                                    font.pixelSize: 11
                                    font.family: "Consolas"
                                    Layout.preferredWidth: 30
                                    Layout.alignment: Qt.AlignTop
                                }
                                ColumnLayout {
                                    id: lineColumn
                                    spacing: 2
                                    Layout.fillWidth: true
                                    Text {
                                        text: lineRow.text !== "" ? lineRow.text : "(no text)"
                                        color: lineRow.added ? editorRoot.addedColor : lineRow.isCurrent ? BtDarkStyle.white : BtDarkStyle.textMain
                                        font.pixelSize: 13
                                        font.italic: lineRow.text === ""
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: lineRow.parts + (lineRow.subNames !== "" ? "   ·   " + lineRow.subNames : "")
                                        color: BtDarkStyle.textDim
                                        font.pixelSize: 10
                                        font.family: "Consolas"
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                                Text {
                                    visible: lineRow.gender > 0
                                    text: lineRow.gender === 1 ? "he" : lineRow.gender === 2 ? "she" : String(lineRow.gender)
                                    color: lineRow.gender === 2 ? "#e48ad0" : "#7fb2f0"
                                    font.pixelSize: 11
                                    font.bold: true
                                    Layout.alignment: Qt.AlignTop
                                }
                                Rectangle {
                                    visible: lineRow.modified
                                    Layout.preferredWidth: 8
                                    Layout.preferredHeight: 8
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: 4
                                    radius: 4
                                    color: lineRow.added ? editorRoot.addedColor : BtDarkStyle.accentColor
                                }
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: linesModel.count === 0
                            text: editorRoot.category >= 0 ? "No lines in this category: Add one." : "Pick a category."
                            color: BtDarkStyle.textDim
                            font.pixelSize: 13
                        }
                    }
                }

                // The line
                Rectangle {
                    id: lineEditor
                    SplitView.preferredWidth: 440
                    SplitView.minimumWidth: 340
                    color: BtDarkStyle.bgDark

                    property bool loading: false
                    readonly property bool hasLine: editorRoot.lineIndex >= 0 && editorRoot.lineData.id !== undefined

                    function load() {
                        loading = true
                        lineText.text = hasLine ? editorRoot.lineData.text : ""
                        lineParts.text = hasLine ? editorRoot.lineData.parts.join("\n") : ""
                        lineId.text = hasLine ? editorRoot.lineData.id : ""
                        genderCombo.currentIndex = hasLine ? Math.min(2, editorRoot.lineData.gender || 0) : 0
                        phraseCheck.checked = hasLine && editorRoot.lineData.flag === true
                        loading = false
                    }

                    ScrollView {
                        anchors.fill: parent
                        contentWidth: availableWidth
                        clip: true

                        ColumnLayout {
                            width: parent.width
                            enabled: lineEditor.hasLine
                            opacity: lineEditor.hasLine ? 1.0 : 0.5
                            spacing: 8

                            Item { Layout.preferredHeight: 6 }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                PanelLabel { text: "Subtitle Text"; Layout.fillWidth: true }
                                Text {
                                    text: lineEditor.hasLine ? "Line " + (editorRoot.lineIndex + 1) + " of " + linesModel.count : ""
                                    color: BtDarkStyle.textDim
                                    font.pixelSize: 11
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                Layout.preferredHeight: 110
                                color: BtDarkStyle.bgMedium
                                radius: 4
                                border.color: lineText.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                                ScrollView {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    clip: true
                                    TextArea {
                                        id: lineText
                                        wrapMode: TextArea.Wrap
                                        color: BtDarkStyle.textMain
                                        selectionColor: BtDarkStyle.accentDim
                                        font.pixelSize: 14
                                        selectByMouse: true
                                        // Material sets its own paddings / insets (they beat `padding`): set each one so the text lines up.
                                        leftPadding: 10
                                        rightPadding: 10
                                        topPadding: 8
                                        bottomPadding: 8
                                        topInset: 0
                                        bottomInset: 0
                                        leftInset: 0
                                        rightInset: 0
                                        background: null
                                        onTextChanged: {
                                            if (lineEditor.loading || !lineEditor.hasLine)
                                                return
                                            if (cm.setLineText(editorRoot.speaker, editorRoot.category, editorRoot.lineIndex, text)) {
                                                linesModel.setProperty(editorRoot.lineIndex, "text", text)
                                                linesModel.setProperty(editorRoot.lineIndex, "modified", true)
                                            }
                                        }
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                text: "What the subtitles show. Changing it doesn't change what's heard: that's the sound below. "
                                      + "Keep tags like <winning_team> as they are."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 11
                                font.italic: true
                                wrapMode: Text.WordWrap
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                visible: lineEditor.hasLine && !editorRoot.lineData.added
                                         && editorRoot.lineData.originalText !== lineText.text
                                Text {
                                    Layout.fillWidth: true
                                    text: "Was: " + (editorRoot.lineData.originalText || "")
                                    color: BtDarkStyle.textDim
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                }
                                SmallButton {
                                    text: "Revert"
                                    onClicked: {
                                        cm.revertLineText(editorRoot.speaker, editorRoot.category, editorRoot.lineIndex)
                                        editorRoot.refreshLineRow()
                                        lineEditor.load()
                                    }
                                }
                            }

                            PanelLabel { text: "Sound"; Layout.leftMargin: 16; Layout.topMargin: 6 }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                spacing: 8
                                Text { text: "Id"; color: BtDarkStyle.textDim; font.pixelSize: 12; Layout.preferredWidth: 60 }
                                DarkField {
                                    id: lineId
                                    Layout.fillWidth: true
                                    font.family: "Consolas"
                                    validator: RegularExpressionValidator { regularExpression: /^\d{0,10}$/ }
                                    onEditingFinished: {
                                        if (lineEditor.loading || !lineEditor.hasLine)
                                            return
                                        if (!cm.setLineId(editorRoot.speaker, editorRoot.category, editorRoot.lineIndex, text))
                                            text = editorRoot.lineData.id
                                        editorRoot.refreshLineRow()
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                spacing: 8
                                Text {
                                    text: "Parts"
                                    color: BtDarkStyle.textDim
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 60
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: 9
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Math.max(60, lineParts.contentHeight + 18)
                                    clip: true
                                    color: BtDarkStyle.bgMedium
                                    radius: 4
                                    border.color: lineParts.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                                    TextArea {
                                        id: lineParts
                                        anchors.fill: parent
                                        color: BtDarkStyle.textMain
                                        selectionColor: BtDarkStyle.accentDim
                                        font.pixelSize: 13
                                        font.family: "Consolas"
                                        selectByMouse: true
                                        // Material sets its own paddings / insets (they beat `padding`): set each one so the text lines up.
                                        leftPadding: 10
                                        rightPadding: 10
                                        topPadding: 8
                                        bottomPadding: 8
                                        topInset: 0
                                        bottomInset: 0
                                        leftInset: 0
                                        rightInset: 0
                                        background: null
                                        onActiveFocusChanged: {
                                            if (activeFocus || lineEditor.loading || !lineEditor.hasLine)
                                                return
                                            cm.setLineParts(editorRoot.speaker, editorRoot.category, editorRoot.lineIndex, text.split("\n"))
                                            editorRoot.refreshLineRow()
                                            lineEditor.load()
                                        }
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                Layout.leftMargin: 84
                                Layout.rightMargin: 16
                                text: "One per line, played in order: a sound name (e.g. id1_try_27), or c:<category> to play a line "
                                      + "from that category (c:winning_team, c:surname_n_...)."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 11
                                font.italic: true
                                wrapMode: Text.WordWrap
                            }

                            PanelLabel { text: "When It's Used"; Layout.leftMargin: 16; Layout.topMargin: 6 }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                spacing: 8
                                Text { text: "Player"; color: BtDarkStyle.textDim; font.pixelSize: 12; Layout.preferredWidth: 60 }
                                DarkCombo {
                                    id: genderCombo
                                    Layout.fillWidth: true
                                    model: editorRoot.genders
                                    onActivated: (i) => {
                                        cm.setLineGender(editorRoot.speaker, editorRoot.category, editorRoot.lineIndex, i)
                                        editorRoot.refreshLineRow()
                                    }
                                }
                            }
                            DarkCheck {
                                id: phraseCheck
                                Layout.leftMargin: 80
                                text: "Whole phrase (off for the names and teams spliced into other lines)"
                                onToggled: {
                                    cm.setLineFlag(editorRoot.speaker, editorRoot.category, editorRoot.lineIndex, checked)
                                    editorRoot.refreshLineRow()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                Layout.topMargin: 6
                                PanelLabel { text: "Sub-categories"; Layout.fillWidth: true }
                                SmallButton { text: "Edit list..."; onClicked: subsDialog.start() }
                            }
                            Text {
                                visible: (editorRoot.lineData.subcategories || []).length === 0
                                Layout.leftMargin: 16
                                text: "This category has none."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 11
                                font.italic: true
                            }
                            Text {
                                visible: (editorRoot.lineData.subcategories || []).length > 0
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                text: "The situations the line suits (it's picked when they apply)."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 11
                                font.italic: true
                                wrapMode: Text.WordWrap
                            }
                            Repeater {
                                model: editorRoot.lineData.subcategories || []
                                DarkCheck {
                                    required property var modelData
                                    required property int index
                                    Layout.leftMargin: 16
                                    Layout.fillWidth: true
                                    text: modelData
                                    font.family: "Consolas"
                                    checked: (editorRoot.lineData.subFlags || [])[index] === true
                                    onToggled: {
                                        cm.setLineSubcategory(editorRoot.speaker, editorRoot.category, editorRoot.lineIndex, index, checked)
                                        editorRoot.refreshLineRow()
                                    }
                                }
                            }

                            // Raw masks (the 5th is the sub-categories above).
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                Layout.topMargin: 6
                                PanelLabel { text: "Conditions"; Layout.fillWidth: true }
                                SmallButton {
                                    id: conditionsToggle
                                    checkable: true
                                    text: checked ? "Hide" : "Show"
                                }
                            }
                            Text {
                                visible: conditionsToggle.checked
                                Layout.fillWidth: true
                                Layout.leftMargin: 16
                                Layout.rightMargin: 16
                                text: "The line's other requirements, as the game stores them (64 bit masks). Change them only to copy "
                                      + "another line's."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 11
                                font.italic: true
                                wrapMode: Text.WordWrap
                            }
                            Repeater {
                                model: conditionsToggle.checked ? (editorRoot.lineData.conditions || []) : []
                                RowLayout {
                                    id: condRow
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 16
                                    Layout.rightMargin: 16
                                    spacing: 8
                                    Text {
                                        text: "Mask " + (condRow.index + 1)
                                        color: BtDarkStyle.textDim
                                        font.pixelSize: 12
                                        Layout.preferredWidth: 60
                                    }
                                    DarkField {
                                        Layout.fillWidth: true
                                        font.family: "Consolas"
                                        text: condRow.modelData
                                        readOnly: condRow.index === 4
                                        color: condRow.index === 4 ? BtDarkStyle.textDim : BtDarkStyle.textMain
                                        onEditingFinished: {
                                            if (condRow.index === 4 || text === condRow.modelData)
                                                return
                                            if (!cm.setLineCondition(editorRoot.speaker, editorRoot.category, editorRoot.lineIndex, condRow.index, text))
                                                text = condRow.modelData
                                            editorRoot.refreshLineRow()
                                        }
                                    }
                                    Text {
                                        visible: condRow.index === 4
                                        text: "sub-categories"
                                        color: BtDarkStyle.textDim
                                        font.pixelSize: 10
                                        font.italic: true
                                    }
                                }
                            }
                            Item { Layout.preferredHeight: 16 }
                        }
                    }
                }
            }

            // ---- Category info --------------------------------------------------------------------------------
            ColumnLayout {
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    color: BtDarkStyle.bgDark
                    border.color: BtDarkStyle.borderDark
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 10
                        DarkField {
                            id: infoFilter
                            Layout.preferredWidth: 280
                            placeholderText: "Filter categories..."
                            placeholderTextColor: BtDarkStyle.textDim
                            onTextEdited: editorRoot.refreshInfo()
                        }
                        Text {
                            text: editorRoot.infoModel.length + " categories"
                            color: BtDarkStyle.textDim
                            font.pixelSize: 11
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: "Every category in the lines files can have settings here; one without uses \"default\"."
                            color: BtDarkStyle.textDim
                            font.pixelSize: 11
                            font.italic: true
                            elide: Text.ElideRight
                            Layout.maximumWidth: 520
                        }
                        SmallButton {
                            text: "Add"
                            tip: "Add a category (with the selected one's settings)"
                            onClicked: nameDialog.ask("addInfo", "Add a category", "")
                        }
                        SmallButton {
                            text: "Delete"
                            tip: "Remove the selected category"
                            enabled: editorRoot.infoSelected >= 2
                            onClicked: {
                                if (cm.removeInfoCategory(editorRoot.infoSelected)) {
                                    editorRoot.infoSelected = -1
                                    editorRoot.refreshInfo()
                                }
                            }
                        }
                    }
                }

                // Column titles
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    color: BtDarkStyle.bgMedium
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 6
                        Text { text: "Category"; color: BtDarkStyle.accentColor; font.bold: true; font.pixelSize: 12; Layout.fillWidth: true; Layout.minimumWidth: 200 }
                        Text { text: "Hash"; color: BtDarkStyle.textDim; font.bold: true; font.pixelSize: 11; Layout.preferredWidth: 90 }
                        Repeater {
                            model: editorRoot.infoValueNames
                            Text {
                                required property var modelData
                                text: modelData
                                color: BtDarkStyle.textDim
                                font.bold: true
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignHCenter
                                Layout.preferredWidth: 74
                            }
                        }
                        Text { text: "Type"; color: BtDarkStyle.textDim; font.bold: true; font.pixelSize: 11; Layout.preferredWidth: 150 }
                        Text { text: "Flag"; color: BtDarkStyle.textDim; font.bold: true; font.pixelSize: 11; Layout.preferredWidth: 40 }
                    }
                }

                ListView {
                    id: infoList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: editorRoot.infoModel
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        id: infoRow
                        required property var modelData
                        required property int index
                        readonly property bool isCurrent: modelData.index === editorRoot.infoSelected
                        width: infoList.width
                        height: 38
                        color: isCurrent ? BtDarkStyle.bgMedium : (index % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
                        Rectangle { visible: infoRow.isCurrent; width: 3; height: parent.height; color: BtDarkStyle.accentColor }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: editorRoot.infoSelected = infoRow.modelData.index
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 6
                            Text {
                                text: infoRow.modelData.name
                                color: infoRow.isCurrent ? BtDarkStyle.white : BtDarkStyle.textMain
                                font.pixelSize: 12
                                font.family: "Consolas"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                Layout.minimumWidth: 200
                            }
                            Text {
                                text: infoRow.modelData.hash
                                color: BtDarkStyle.textDim
                                font.pixelSize: 10
                                font.family: "Consolas"
                                Layout.preferredWidth: 90
                            }
                            Repeater {
                                model: 6
                                DarkField {
                                    id: valueField
                                    required property int index
                                    Layout.preferredWidth: 74
                                    implicitHeight: 28
                                    horizontalAlignment: TextInput.AlignHCenter
                                    font.family: "Consolas"
                                    font.pixelSize: 12
                                    text: String(infoRow.modelData.values[index])
                                    validator: DoubleValidator { notation: DoubleValidator.StandardNotation }
                                    onActiveFocusChanged: if (activeFocus) editorRoot.infoSelected = infoRow.modelData.index
                                    onEditingFinished: {
                                        const v = parseFloat(text)
                                        if (isNaN(v)) {
                                            text = String(infoRow.modelData.values[index])
                                            return
                                        }
                                        if (cm.setInfoValue(infoRow.modelData.index, index, v))
                                            infoRow.modelData.values[index] = v
                                    }
                                }
                            }
                            DarkCombo {
                                Layout.preferredWidth: 150
                                implicitHeight: 28
                                model: editorRoot.infoTypes
                                currentIndex: Math.min(3, infoRow.modelData.type)
                                onActivated: (i) => {
                                    editorRoot.infoSelected = infoRow.modelData.index
                                    cm.setInfoByte(infoRow.modelData.index, 0, i)
                                }
                            }
                            DarkCheck {
                                Layout.preferredWidth: 40
                                checked: infoRow.modelData.flag !== 0
                                onToggled: {
                                    editorRoot.infoSelected = infoRow.modelData.index
                                    cm.setInfoByte(infoRow.modelData.index, 1, checked ? 1 : 0)
                                }
                            }
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
                    readonly property bool isLocal: editorRoot.localStatus !== ""
                    color: (isLocal ? editorRoot.localStatusOk : cm.statusOk) ? BtDarkStyle.textDim : editorRoot.errorColor
                    text: "Status: " + (isLocal ? editorRoot.localStatus : cm.statusText !== "" ? cm.statusText : "Ready | Open a commentary file")
                }
                Text {
                    text: cm.modified ? "Unsaved changes" : ""
                    color: BtDarkStyle.accentColor
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

    function addLine(from) {
        const at = cm.addLine(speaker, category, from)
        if (at < 0)
            return
        refreshLines()
        selectLine(at)
        refreshCategoryCounts()
        if (from < 0)
            lineId.forceActiveFocus()
    }

    // The category list's line counts, keeping the list where it is.
    function refreshCategoryCounts() {
        const y = categoryList.contentY
        categoriesModel = cm.categories(speaker, categoryFilter.text)
        speakersModel = cm.speakers()
        speakerCombo.currentIndex = speaker
        categoryList.contentY = y
    }

    // ------------------------------------------------------------------------------------------------
    // Menus / dialogs
    // ------------------------------------------------------------------------------------------------
    Menu {
        id: openMenu
        width: 320
        onAboutToShow: editorRoot.filesModel = cm.blobsetFiles()
        MenuItem {
            readonly property var file: editorRoot.filesModel[0] || ({})
            text: file.name || ""
            enabled: file.available === true
            icon.source: editorRoot.iconDir + "commentary_editor_32.png"
            icon.color: "transparent"
            onTriggered: editorRoot.guarded(() => cm.loadFromBlobset(file.hash, file.path))
        }
        MenuItem {
            readonly property var file: editorRoot.filesModel[1] || ({})
            text: file.name || ""
            enabled: file.available === true
            icon.source: editorRoot.iconDir + "commentary_editor_32.png"
            icon.color: "transparent"
            onTriggered: editorRoot.guarded(() => cm.loadFromBlobset(file.hash, file.path))
        }
        MenuItem {
            readonly property var file: editorRoot.filesModel[2] || ({})
            text: file.name || ""
            enabled: file.available === true
            icon.source: editorRoot.iconDir + "commentary_editor_32.png"
            icon.color: "transparent"
            onTriggered: editorRoot.guarded(() => cm.loadFromBlobset(file.hash, file.path))
        }
        MenuItem {
            readonly property var file: editorRoot.filesModel[3] || ({})
            text: file.name || ""
            enabled: file.available === true
            icon.source: editorRoot.iconDir + "commentary_editor_32.png"
            icon.color: "transparent"
            onTriggered: editorRoot.guarded(() => cm.loadFromBlobset(file.hash, file.path))
        }
        MenuSeparator {}
        MenuItem {
            text: "Open a File..."
            icon.source: editorRoot.iconDir + "open_32.png"
            icon.color: "transparent"
            onTriggered: editorRoot.guarded(() => fileDialog.start("open"))
        }
        MenuItem {
            text: "Save As..."
            icon.source: editorRoot.iconDir + "save_32.png"
            icon.color: "transparent"
            enabled: cm.loaded
            onTriggered: fileDialog.start("saveAs")
        }
        MenuSeparator {}
        MenuItem {
            text: "Export Lines as CSV..."
            icon.source: editorRoot.iconDir + "extract_32.png"
            icon.color: "transparent"
            enabled: editorRoot.isLines
            onTriggered: fileDialog.start("exportCsv")
        }
        MenuItem {
            text: "Import Lines from CSV..."
            icon.source: editorRoot.iconDir + "modify_32.png"
            icon.color: "transparent"
            enabled: editorRoot.isLines
            onTriggered: fileDialog.start("importCsv")
        }
    }

    FileDialog {
        id: fileDialog
        property string mode: "open"

        function baseName() {
            let name = cm.fileName !== "" ? cm.fileName : "commentary"
            const dot = name.lastIndexOf(".")
            return dot > 0 ? name.substring(0, dot) : name
        }
        function extension() {
            const name = cm.fileName
            const dot = name.lastIndexOf(".")
            return dot > 0 ? name.substring(dot + 1) : "cmtry"
        }

        function start(m) {
            mode = m
            const folder = settings.lastFolder !== "" ? settings.lastFolder
                                                      : StandardPaths.writableLocation(StandardPaths.DocumentsLocation).toString()
            currentFolder = folder
            const csv = m === "exportCsv" || m === "importCsv"
            fileMode = m === "wav" ? FileDialog.OpenFiles : (m === "open" || m === "importCsv") ? FileDialog.OpenFile : FileDialog.SaveFile
            nameFilters = m === "wav" ? ["Audio (*.wav *.wem)", "All files (*)"]
                        : csv ? ["CSV (*.csv)", "All files (*)"] : ["Commentary files (*.cmtry *.dat)", "All files (*)"]
            defaultSuffix = csv ? "csv" : extension()
            title = m === "wav" ? "New lines from WAV / WEM files" : m === "open" ? "Open a commentary file" : m === "saveAs" ? "Save the commentary file"
                  : m === "exportCsv" ? "Export the lines as CSV" : "Import lines from a CSV"
            if (fileMode === FileDialog.SaveFile)
                selectedFile = folder + "/" + encodeURIComponent(baseName() + (csv ? ".csv" : "." + extension()))
            open()
        }

        onAccepted: {
            const url = selectedFile.toString()
            settings.lastFolder = url.substring(0, url.lastIndexOf("/"))
            if (mode === "wav") {
                const files = []
                for (let i = 0; i < selectedFiles.length; ++i)
                    files.push(selectedFiles[i].toString())
                editorRoot.startWavLines(files, "")
            } else if (mode === "open")
                cm.loadFile(url)
            else if (mode === "saveAs")
                cm.saveAs(url)
            else if (mode === "exportCsv")
                cm.exportCsv(url)
            else if (mode === "importCsv") {
                cm.importCsv(url)
                editorRoot.refreshLines()
            }
        }
    }

    component ThemedDialog: Dialog {
        id: themedDialog
        property string message: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 460
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
    }

    // A name: add / rename a category, add a category info entry.
    ThemedDialog {
        id: nameDialog
        property string mode: ""
        function ask(m, t, value) {
            mode = m
            title = t
            nameField.text = value
            open()
            nameField.forceActiveFocus()
            nameField.selectAll()
        }
        function accept_() {
            const name = nameField.text.trim()
            if (name === "")
                return
            if (mode === "addCategory") {
                const at = cm.addCategory(editorRoot.speaker, name)
                if (at < 0)
                    return
                categoryFilter.text = ""
                editorRoot.category = at
                editorRoot.lineIndex = -1
                editorRoot.refreshCategoryCounts()
                editorRoot.refreshLines()
                categoryList.positionViewAtEnd()
            } else if (mode === "renameCategory") {
                if (!cm.renameCategory(editorRoot.speaker, editorRoot.category, name))
                    return
                editorRoot.refreshCategoryCounts()
            } else if (mode === "addInfo") {
                const at = cm.addInfoCategory(name, editorRoot.infoSelected)
                if (at < 0)
                    return
                infoFilter.text = ""
                editorRoot.refreshInfo()
                editorRoot.infoSelected = at
                infoList.positionViewAtIndex(at, ListView.Center)
            }
            close()
        }
        contentItem: ColumnLayout {
            spacing: 8
            DarkField {
                id: nameField
                Layout.fillWidth: true
                font.family: "Consolas"
                onAccepted: nameDialog.accept_()
            }
            Text {
                Layout.fillWidth: true
                text: nameDialog.mode === "addInfo"
                      ? "Use the category's name in the lines files (e.g. try_reply). It gets the selected category's settings."
                      : "Lower case with _ like the game's (e.g. try_reply). Other lines play a category as c:<name>."
                color: BtDarkStyle.textDim
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            HeaderButton { text: "OK"; accent: true; implicitHeight: 34; Layout.bottomMargin: 14; onClicked: nameDialog.accept_() }
            HeaderButton { text: "Cancel"; implicitHeight: 34; Layout.rightMargin: 16; Layout.bottomMargin: 14; onClicked: nameDialog.close() }
        }
    }

    // The category's sub-categories, one per line.
    ThemedDialog {
        id: subsDialog
        title: "Sub-categories"
        width: 520
        function start() {
            subsText.text = cm.subcategories(editorRoot.speaker, editorRoot.category).join("\n")
            open()
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: "One per line (up to 64). Each line of the category is ticked for the ones it suits; renaming one here "
                      + "unticks it on every line, reordering keeps the ticks."
                color: BtDarkStyle.textDim
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 240
                color: BtDarkStyle.bgMedium
                radius: 4
                border.color: subsText.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 1
                    TextArea {
                        id: subsText
                        color: BtDarkStyle.textMain
                        font.family: "Consolas"
                        font.pixelSize: 12
                        selectByMouse: true
                        // Material sets its own paddings / insets (they beat `padding`): set each one so the text lines up.
                        leftPadding: 10
                        rightPadding: 10
                        topPadding: 8
                        bottomPadding: 8
                        topInset: 0
                        bottomInset: 0
                        leftInset: 0
                        rightInset: 0
                        background: null
                    }
                }
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            HeaderButton {
                text: "Apply"
                accent: true
                implicitHeight: 34
                Layout.bottomMargin: 14
                onClicked: {
                    const names = subsText.text.split("\n").map((s) => s.trim()).filter((s) => s !== "")
                    const before = cm.subcategories(editorRoot.speaker, editorRoot.category)
                    if (JSON.stringify(names) === JSON.stringify(before) || cm.setSubcategories(editorRoot.speaker, editorRoot.category, names)) {
                        subsDialog.close()
                        const at = editorRoot.lineIndex
                        editorRoot.refreshLines()
                        if (at >= 0)
                            editorRoot.selectLine(at)
                        editorRoot.refreshCategoryCounts()
                    }
                }
            }
            HeaderButton { text: "Cancel"; implicitHeight: 34; Layout.rightMargin: 16; Layout.bottomMargin: 14; onClicked: subsDialog.close() }
        }
    }

    // ------------------------------------------------------------------------------------------------
    // New line from WAV: one dialog per WAV (from the From WAV button, or WAVs dropped into a
    // sound\\commentary\\<speaker> folder on MainForm).
    // ------------------------------------------------------------------------------------------------
    property var wavQueue: []

    function startWavLines(files, folder) {
        if (!linesLoaded())
            return
        if (folder !== "") {
            const s = cm.speakerForFolder(folder)
            if (s >= 0 && s !== speaker)
                selectSpeaker(s)
        }
        wavQueue = files.slice()
        nextWavLine()
    }

    function nextWavLine() {
        if (wavQueue.length === 0)
            return
        const wav = wavQueue[0]
        wavQueue = wavQueue.slice(1)
        wavDialog.start(wav)
    }

    ThemedDialog {
        id: wavDialog
        title: "New Line From WAV"
        width: 600
        property string wav: ""
        property int speakerIndex: 0
        property var cats: []
        property string nameError: ""
        property var info: ({})

        function wavName() {
            const u = decodeURIComponent(wav.toString())
            return u.substring(Math.max(u.lastIndexOf("/"), u.lastIndexOf("\\")) + 1)
        }
        function start(file) {
            wav = file
            speakerIndex = editorRoot.speaker
            wavSpeakerCombo.currentIndex = speakerIndex
            loadSpeaker()
            wavText.text = ""
            wavGender.currentIndex = 0
            wavHash.text = cm.defaultWemHash(file)
            open()
            wavNameField.forceActiveFocus()
            wavNameField.selectAll()
        }
        // The category: a new one (the first entry, the default) or one of the speaker's.
        readonly property bool newCategory: wavCategory.currentIndex === 0
        property string categoryError: ""
        // The category the line goes in (the new one's name, or the picked one's).
        readonly property string categoryName: newCategory ? wavCategoryName.text.trim()
                                                           : (wavCategory.currentIndex > 0 ? cats[wavCategory.currentIndex - 1].name : "")
        // Names spliced into sentences (surname_n_ / surname_e_ / team_...) aren't whole sentences.
        onCategoryNameChanged: wavPhrase.checked = !cm.isNameCategory(categoryName)

        function loadSpeaker() {
            cats = cm.categories(speakerIndex, "")
            wavCategory.model = ["+ New category"].concat(cats.map((c) => c.name))
            wavCategory.currentIndex = 0
            wavNameField.text = cm.suggestSoundName(speakerIndex, wav)
            // The new category's name: the sound's name without the speaker's prefix ("id1_my_try" -> "my_try").
            const n = wavNameField.text
            const prefix = n.indexOf("_") > 0 && /^id\d+$/.test(n.substring(0, n.indexOf("_"))) ? n.indexOf("_") + 1 : 0
            wavCategoryName.text = n.substring(prefix)
            check()
            wavPhrase.checked = !cm.isNameCategory(categoryName)
        }
        function check() {
            nameError = cm.checkSoundName(speakerIndex, wavNameField.text.trim())
            info = cm.newSoundInfo(speakerIndex, wavNameField.text.trim())
            const c = wavCategoryName.text.trim()
            categoryError = !newCategory ? ""
                          : !/^[A-Za-z0-9_]+$/.test(c) ? "A category name is letters, digits and _ (e.g. my_try_call)."
                          : cats.some((x) => x.name === c) ? "The speaker already has a " + c + " category: pick it from the list."
                          : ""
        }
        function create() {
            check()
            if (nameError !== "" || categoryError !== "" || wavCategory.currentIndex < 0)
                return
            let cat
            let added = false
            if (newCategory) {
                cat = cm.addCategory(speakerIndex, wavCategoryName.text.trim())
                if (cat < 0)
                    return   // The status bar says why.
                added = true
            } else {
                cat = cats[wavCategory.currentIndex - 1].index
            }
            const at = cm.addLineFromWav(speakerIndex, cat, wav, wavNameField.text.trim(), wavText.text,
                                         wavGender.currentIndex, wavPhrase.checked, wavHash.text.trim())
            if (at < 0) {
                if (added)
                    cm.removeCategory(speakerIndex, cat)   // Not left behind empty.
                return   // The status bar says why.
            }
            close()
            if (speakerIndex !== editorRoot.speaker)
                editorRoot.selectSpeaker(speakerIndex)
            categoryFilter.text = ""
            editorRoot.category = cat
            editorRoot.refreshCategoryCounts()
            editorRoot.refreshLines()
            editorRoot.selectLine(at)
            Qt.callLater(editorRoot.nextWavLine)
        }

        contentItem: GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 8

            PanelLabel { text: "WAV" }
            Text {
                Layout.fillWidth: true
                text: wavDialog.wavName() + (editorRoot.wavQueue.length > 0 ? "   (" + editorRoot.wavQueue.length + " more after this)" : "")
                color: BtDarkStyle.textMain
                font.pixelSize: 13
                elide: Text.ElideMiddle
            }

            PanelLabel { text: "Speaker" }
            DarkCombo {
                id: wavSpeakerCombo
                Layout.fillWidth: true
                textRole: "name"
                model: editorRoot.speakersModel
                onActivated: (i) => { wavDialog.speakerIndex = i; wavDialog.loadSpeaker() }
            }

            PanelLabel { text: "Category" }
            DarkCombo {
                id: wavCategory
                Layout.fillWidth: true
                onActivated: wavDialog.check()
            }

            PanelLabel { text: "New category"; visible: wavDialog.newCategory }
            DarkField {
                id: wavCategoryName
                visible: wavDialog.newCategory
                Layout.fillWidth: true
                font.family: "Consolas"
                onTextChanged: wavDialog.check()
            }

            Item { implicitWidth: 1; visible: wavDialog.newCategory }
            Text {
                visible: wavDialog.newCategory
                Layout.fillWidth: true
                text: wavDialog.categoryError !== "" ? wavDialog.categoryError
                      : "A new category is only heard when the game asks for it or another line plays it as c:"
                        + wavCategoryName.text.trim() + " (add that as a part). Give it settings in the category info file if it needs them."
                color: wavDialog.categoryError !== "" ? editorRoot.errorColor : BtDarkStyle.textDim
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            PanelLabel { text: "Sound name" }
            DarkField {
                id: wavNameField
                Layout.fillWidth: true
                font.family: "Consolas"
                onTextChanged: wavDialog.check()
                onAccepted: wavDialog.create()
            }

            Item { implicitWidth: 1 }
            Text {
                Layout.fillWidth: true
                text: wavDialog.nameError !== "" ? wavDialog.nameError
                      : (wavDialog.info.path || "") + "\npath hash " + (wavDialog.info.pathHash || "")
                color: wavDialog.nameError !== "" ? editorRoot.errorColor : BtDarkStyle.textDim
                font.pixelSize: 11
                font.family: wavDialog.nameError !== "" ? "" : "Consolas"
                wrapMode: Text.WrapAnywhere
            }

            PanelLabel { text: "Subtitle text" }
            DarkField {
                id: wavText
                Layout.fillWidth: true
                placeholderText: "What the commentator says"
            }

            PanelLabel { text: "Pronoun" }
            DarkCombo {
                id: wavGender
                Layout.fillWidth: true
                model: editorRoot.genders
            }

            PanelLabel { text: "Sentence" }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                DarkCheck {
                    id: wavPhrase
                    text: "A whole sentence (untick for a name spliced into other lines)"
                    checked: true
                }
                Text {
                    Layout.fillWidth: true
                    visible: /^surname_[ne]_/.test(wavDialog.categoryName)
                    text: "A surname needs both readings for each commentator: surname_n_" + wavDialog.categoryName.substring(10)
                          + " (plain, what the menus and most lines use) and surname_e_" + wavDialog.categoryName.substring(10)
                          + " (excited). Add a WAV for each, for every commentator that should say it."
                    color: BtDarkStyle.accentColor
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            PanelLabel { text: "WEM hash" }
            DarkField {
                id: wavHash
                Layout.fillWidth: true
                font.family: "Consolas"
                placeholderText: "32 hex digits (empty: made from the WAV)"
            }

            Item { implicitWidth: 1 }
            Text {
                Layout.fillWidth: true
                text: "A .wav becomes a Vorbis WEM encoded like the game's own commentary (mono, 44100 Hz), a .wem is used as it is, in the mods folder under the sound's name (the game finds commentary "
                      + "sounds by name: no Wwise bank changes), and a line that plays it goes at the end of the category "
                      + "(with the conditions of its first line, if it has one). The lines file is saved to the mods folder; "
                      + "click Modify to put both into the game."
                color: BtDarkStyle.textDim
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            HeaderButton {
                text: "Create"
                accent: true
                enabled: wavDialog.nameError === "" && wavDialog.categoryError === "" && wavCategory.currentIndex >= 0
                implicitHeight: 34
                Layout.bottomMargin: 14
                onClicked: wavDialog.create()
            }
            HeaderButton {
                text: editorRoot.wavQueue.length > 0 ? "Skip" : "Cancel"
                implicitHeight: 34
                Layout.bottomMargin: 14
                onClicked: { wavDialog.close(); Qt.callLater(editorRoot.nextWavLine) }
            }
            HeaderButton {
                visible: editorRoot.wavQueue.length > 0
                text: "Cancel All"
                implicitHeight: 34
                Layout.rightMargin: 16
                Layout.bottomMargin: 14
                onClicked: { editorRoot.wavQueue = []; wavDialog.close() }
            }
            Item { visible: editorRoot.wavQueue.length === 0; implicitWidth: 8 }
        }
    }

    ThemedDialog {
        id: confirmDialog
        property string mode: ""
        function ask(m, t, text) {
            mode = m
            title = t
            message = text
            open()
        }
        contentItem: Text {
            text: confirmDialog.message
            color: BtDarkStyle.textMain
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            HeaderButton {
                text: "Delete"
                accent: true
                implicitHeight: 34
                Layout.bottomMargin: 14
                onClicked: {
                    confirmDialog.close()
                    if (confirmDialog.mode === "removeCategory" && cm.removeCategory(editorRoot.speaker, editorRoot.category)) {
                        editorRoot.category = -1
                        editorRoot.lineIndex = -1
                        editorRoot.refreshCategoryCounts()
                        if (editorRoot.categoriesModel.length > 0)
                            editorRoot.category = editorRoot.categoriesModel[0].index
                        editorRoot.refreshLines()
                    }
                }
            }
            HeaderButton { text: "Cancel"; implicitHeight: 34; Layout.rightMargin: 16; Layout.bottomMargin: 14; onClicked: confirmDialog.close() }
        }
    }

    ThemedDialog {
        id: unsavedDialog
        title: "Unsaved changes"
        message: "The commentary file has unsaved changes. Save them first?"
        contentItem: Text {
            text: unsavedDialog.message
            color: BtDarkStyle.textMain
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            HeaderButton {
                text: cm.workspacePath !== "" ? "Save to Mods" : "Save As..."
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
