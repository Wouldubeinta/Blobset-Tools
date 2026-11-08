import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import com.wouldymods.blobset 1.0

// Dialogue Editor: the names the commentary can say (DialogueEditorManager), in sound\dialogue.mempack.m3mp -
// surnames, team locations and team mascots. Each name's hash is what the database stores for a player / team; the
// commentary says it with the lines of its surname_n_ / surname_e_ (or team_) categories. Save to Mods writes the
// M3MP into the mods folder, ready for Modify.
Item {
    id: editorRoot
    anchors.fill: parent

    readonly property string iconDir: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/"
    readonly property color errorColor: Material.color(Material.Red)
    readonly property color addedColor: "#5cb85c"
    readonly property color okColor: "#5cb85c"

    property var listsModel: []
    property var rowsModel: []
    property var voicesModel: []
    property var unlistedModel: []
    property int list: 0
    property int selected: -1          // Index of the name in the list (not the row).
    property string show: "all"

    Rectangle {
        anchors.fill: parent
        color: BtDarkStyle.bgDarkest
        z: -1
    }

    DialogueEditorManager {
        id: dm
        onLoadedChanged: Qt.callLater(editorRoot.reloadAll)
        onSaved: (ok, message) => editorRoot.refresh()
    }

    Settings {
        id: settings
        category: "DialogueEditor"
        property string lastFolder: ""
    }

    Component.onCompleted: Qt.callLater(() => dm.load())

    // ------------------------------------------------------------------------------------------------
    // Models (refreshed from the manager)
    // ------------------------------------------------------------------------------------------------
    function reloadAll() {
        listsModel = dm.lists()
        if (list >= listsModel.length)
            list = 0
        selected = -1
        refresh()
    }

    function refresh() {
        listsModel = dm.lists()
        voicesModel = dm.voices(list)
        const y = namesView.contentY
        rowsModel = dm.rows(list, filterInput.text, show)
        namesView.contentY = y
        unlistedModel = dm.unlisted(list)
    }

    function selectList(i) {
        list = i
        selected = -1
        filterInput.text = ""
        refresh()
        namesView.positionViewAtBeginning()
    }

    function addTyped() {
        const at = dm.addName(list, addInput.text)
        if (at < 0)
            return
        addInput.text = ""
        filterInput.text = ""
        show = "all"
        showCombo.currentIndex = 0
        refresh()
        showName(at)
    }

    // Selects name `at` (an index in the list) and scrolls its row into view: names go in alphabetical order.
    function showName(at) {
        selected = at
        const row = rowsModel.findIndex((r) => r.index === at)
        if (row >= 0)
            namesView.positionViewAtIndex(row, ListView.Center)
    }

    property string localStatus: ""
    property bool localStatusOk: true
    Connections {
        target: dm
        function onStatusChanged() { editorRoot.localStatus = "" }
    }

    // ------------------------------------------------------------------------------------------------
    // Unsaved changes guard
    // ------------------------------------------------------------------------------------------------
    property var pendingAction: null
    function guarded(action) {
        if (dm.modified) {
            pendingAction = action
            unsavedDialog.open()
        } else {
            action()
        }
    }
    function goBack() {
        appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml")
    }

    Shortcut { sequences: [StandardKey.Save]; enabled: dm.loaded && !dm.busy; onActivated: dm.save() }
    Shortcut { sequences: [StandardKey.Find]; onActivated: { filterInput.forceActiveFocus(); filterInput.selectAll() } }

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
        rightPadding: 8
        topPadding: 0
        bottomPadding: 0
        topInset: 0
        bottomInset: 0
        verticalAlignment: TextInput.AlignVCenter
        implicitHeight: 32
        placeholderTextColor: BtDarkStyle.textDim
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

    component ThemedDialog: Dialog {
        id: themedDialog
        property string message: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 480
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
                    source: editorRoot.iconDir + "dialogue_editor_32.png"
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    fillMode: Image.PreserveAspectFit
                }
                ColumnLayout {
                    spacing: 0
                    Layout.maximumWidth: 420
                    Text {
                        text: "Dialogue Editor"
                        color: BtDarkStyle.accentColor
                        font.bold: true
                        font.pixelSize: 16
                    }
                    Text {
                        Layout.fillWidth: true
                        text: dm.loaded ? "sound\\dialogue.mempack.m3mp  ·  " + dm.sourceText : "sound\\dialogue.mempack.m3mp"
                        color: BtDarkStyle.textDim
                        font.pixelSize: 11
                        font.family: "Consolas"
                        elide: Text.ElideMiddle
                    }
                }
                Item { Layout.fillWidth: true }

                HeaderButton {
                    id: filesButton
                    text: "Files ▾"
                    tip: "Export / import the list, reload, revert"
                    iconSource: editorRoot.iconDir + "open_32.png"
                    enabled: dm.loaded && !dm.busy
                    onClicked: filesMenu.popup(filesButton, 0, filesButton.height)
                }
                HeaderButton {
                    text: dm.busy ? "Saving..." : "Save to Mods"
                    tip: dm.modsTarget !== "" ? "Save the M3MP into the mods folder (" + dm.modsTarget + "), ready for Modify (Ctrl+S)" : ""
                    iconSource: editorRoot.iconDir + "save_32.png"
                    accent: dm.modified
                    enabled: dm.loaded && !dm.busy
                    onClicked: dm.save()
                }
            }
        }

        // Not loaded
        Item {
            visible: !dm.loaded
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 60, 560)
                spacing: 12
                Text {
                    text: "sound\\dialogue.mempack isn't loaded"
                    color: BtDarkStyle.accentColor
                    font.pixelSize: 18
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: dm.statusText !== "" ? dm.statusText : "Load Rugby League 26 (or Rugby 25) in the main workspace, then open the Dialogue Editor."
                    color: BtDarkStyle.textDim
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                HeaderButton {
                    text: "Try Again"
                    accent: true
                    implicitHeight: 34
                    onClicked: dm.load()
                }
            }
        }

        RowLayout {
            visible: dm.loaded
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Lists
            Rectangle {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: BtDarkStyle.bgDark
                border.color: BtDarkStyle.borderDark

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    PanelLabel { text: "Name Lists" }
                    Repeater {
                        model: editorRoot.listsModel
                        Rectangle {
                            id: listCard
                            required property var modelData
                            readonly property bool isCurrent: modelData.index === editorRoot.list
                            Layout.fillWidth: true
                            implicitHeight: listColumn.implicitHeight + 18
                            radius: 6
                            color: isCurrent ? BtDarkStyle.bgMedium : cardArea.containsMouse ? BtDarkStyle.bgHover : BtDarkStyle.bgDarkest
                            border.color: isCurrent ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                            MouseArea {
                                id: cardArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: editorRoot.selectList(listCard.modelData.index)
                            }
                            ColumnLayout {
                                id: listColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: listCard.modelData.title
                                        color: listCard.isCurrent ? BtDarkStyle.white : BtDarkStyle.textMain
                                        font.pixelSize: 14
                                        font.bold: true
                                        Layout.fillWidth: true
                                    }
                                    Rectangle {
                                        visible: listCard.modelData.modified
                                        Layout.preferredWidth: 8
                                        Layout.preferredHeight: 8
                                        radius: 4
                                        color: BtDarkStyle.accentColor
                                    }
                                    Text {
                                        text: listCard.modelData.count
                                        color: BtDarkStyle.textDim
                                        font.pixelSize: 12
                                        font.family: "Consolas"
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: listCard.modelData.hint
                                    color: BtDarkStyle.textDim
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }

                    PanelLabel { text: "Show"; Layout.topMargin: 8 }
                    DarkCombo {
                        id: showCombo
                        Layout.fillWidth: true
                        textRole: "label"
                        model: [{ label: "Every name", key: "all" }, { label: "Names a commentator says", key: "with" },
                                { label: "Names no commentator says", key: "without" }]
                        onActivated: (i) => {
                            editorRoot.show = model[i].key
                            editorRoot.refresh()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        implicitHeight: unlistedColumn.implicitHeight + 18
                        radius: 6
                        color: BtDarkStyle.bgDarkest
                        border.color: editorRoot.unlistedModel.length > 0 ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                        visible: dm.hasCommentary
                        ColumnLayout {
                            id: unlistedColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 6
                            Text {
                                Layout.fillWidth: true
                                text: editorRoot.unlistedModel.length > 0
                                      ? editorRoot.unlistedModel.length + " name(s) have commentary but aren't in this list"
                                      : "Every name the commentary has is in this list."
                                color: editorRoot.unlistedModel.length > 0 ? BtDarkStyle.accentColor : BtDarkStyle.textDim
                                font.pixelSize: 12
                                font.bold: editorRoot.unlistedModel.length > 0
                                wrapMode: Text.WordWrap
                            }
                            SmallButton {
                                visible: editorRoot.unlistedModel.length > 0
                                text: "Show them..."
                                onClicked: unlistedDialog.open()
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Text {
                        Layout.fillWidth: true
                        text: "A player's / team's commentary name is stored in the database as its hash, so renaming or removing "
                              + "a name here unlinks the players and teams that use it (pick it again in the Database Editor)."
                        color: BtDarkStyle.textDim
                        font.pixelSize: 11
                        font.italic: true
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // Names
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Add / filter
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 92
                    color: BtDarkStyle.bgDark
                    border.color: BtDarkStyle.borderDark
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        anchors.topMargin: 10
                        anchors.bottomMargin: 8
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            DarkField {
                                id: filterInput
                                Layout.preferredWidth: 260
                                placeholderText: "Filter by name or hash..."
                                onTextEdited: filterDelay.restart()
                                Timer { id: filterDelay; interval: 250; onTriggered: editorRoot.refresh() }
                            }
                            Text {
                                text: editorRoot.rowsModel.length + " shown"
                                color: BtDarkStyle.textDim
                                font.pixelSize: 11
                            }
                            Item { Layout.fillWidth: true }
                            DarkField {
                                id: addInput
                                Layout.preferredWidth: 240
                                placeholderText: "New name (e.g. Smith)..."
                                onAccepted: editorRoot.addTyped()
                            }
                            Text {
                                text: dm.hashOf(addInput.text)
                                color: BtDarkStyle.textDim
                                font.pixelSize: 11
                                font.family: "Consolas"
                                Layout.preferredWidth: 80
                            }
                            SmallButton {
                                text: "Add"
                                accent: addInput.text.trim() !== "" && addCheck.text === ""
                                enabled: addInput.text.trim() !== "" && addCheck.text === ""
                                tip: "Add the name to the end of the list"
                                onClicked: editorRoot.addTyped()
                            }
                        }
                        Text {
                            id: addCheck
                            Layout.fillWidth: true
                            readonly property string problem: addInput.text.trim() === "" ? "" : dm.checkName(editorRoot.list, addInput.text)
                            text: problem
                            visible: true
                            color: problem !== "" ? editorRoot.errorColor : BtDarkStyle.textDim
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Text {
                                anchors.fill: parent
                                visible: addCheck.problem === ""
                                text: "Names are lower case letters (\"A Hall\" becomes \"ahall\"). The commentary says a name with the lines "
                                      + "of its categories in the Commentary Editor (e.g. surname_n_ahall)."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 11
                                font.italic: true
                                elide: Text.ElideRight
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
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 10
                        Text { text: "#"; color: BtDarkStyle.textDim; font.bold: true; font.pixelSize: 11; Layout.preferredWidth: 44 }
                        Text { text: "Name"; color: BtDarkStyle.accentColor; font.bold: true; font.pixelSize: 12; Layout.preferredWidth: 220 }
                        Text { text: "Hash"; color: BtDarkStyle.textDim; font.bold: true; font.pixelSize: 11; Layout.preferredWidth: 100 }
                        Repeater {
                            model: editorRoot.voicesModel
                            Text {
                                required property var modelData
                                text: modelData
                                color: BtDarkStyle.textDim
                                font.bold: true
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                Layout.preferredWidth: 96
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Text { text: ""; Layout.preferredWidth: 150 }
                    }
                }

                ListView {
                    id: namesView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: editorRoot.rowsModel
                    boundsBehavior: Flickable.StopAtBounds
                    reuseItems: true
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        id: nameRow
                        required property var modelData
                        required property int index
                        readonly property bool isCurrent: modelData.index === editorRoot.selected
                        property bool editing: false
                        width: namesView.width
                        height: 34
                        color: isCurrent ? BtDarkStyle.bgMedium : rowArea.containsMouse ? BtDarkStyle.bgHover
                                                                                        : (index % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
                        onModelDataChanged: editing = false
                        Rectangle { visible: nameRow.isCurrent; width: 3; height: parent.height; color: BtDarkStyle.accentColor }
                        MouseArea {
                            id: rowArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: editorRoot.selected = nameRow.modelData.index
                            onDoubleClicked: {
                                editorRoot.selected = nameRow.modelData.index
                                if (!nameRow.modelData.isNone) {
                                    nameRow.editing = true
                                    renameField.text = nameRow.modelData.name
                                    renameField.forceActiveFocus()
                                    renameField.selectAll()
                                }
                            }
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 10
                            Text {
                                text: nameRow.modelData.index
                                color: BtDarkStyle.textDim
                                font.pixelSize: 11
                                font.family: "Consolas"
                                Layout.preferredWidth: 44
                            }
                            Item {
                                Layout.preferredWidth: 220
                                Layout.fillHeight: true
                                Text {
                                    visible: !nameRow.editing
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width
                                    text: nameRow.modelData.name
                                    color: nameRow.modelData.added ? editorRoot.addedColor
                                         : nameRow.modelData.isNone ? BtDarkStyle.textDim : nameRow.isCurrent ? BtDarkStyle.white : BtDarkStyle.textMain
                                    font.pixelSize: 13
                                    font.family: "Consolas"
                                    font.italic: nameRow.modelData.isNone
                                    elide: Text.ElideRight
                                }
                                DarkField {
                                    id: renameField
                                    visible: nameRow.editing
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width
                                    implicitHeight: 26
                                    font.family: "Consolas"
                                    onAccepted: {
                                        const at = nameRow.modelData.index
                                        const typed = text
                                        nameRow.editing = false
                                        if (dm.renameName(editorRoot.list, at, typed)) {
                                            editorRoot.refresh()
                                            editorRoot.showName(dm.indexOfName(editorRoot.list, typed))
                                        }
                                    }
                                    Keys.onEscapePressed: nameRow.editing = false
                                    onActiveFocusChanged: if (!activeFocus) nameRow.editing = false
                                }
                            }
                            Text {
                                text: nameRow.modelData.hash
                                color: BtDarkStyle.textDim
                                font.pixelSize: 11
                                font.family: "Consolas"
                                Layout.preferredWidth: 100
                            }
                            Repeater {
                                model: editorRoot.voicesModel
                                Text {
                                    required property var modelData
                                    readonly property bool says: (", " + nameRow.modelData.voiceNames + ", ").indexOf(", " + modelData + ", ") >= 0
                                    text: nameRow.modelData.isNone ? "" : says ? "✓" : "–"
                                    color: says ? editorRoot.okColor : BtDarkStyle.textDim
                                    font.pixelSize: 13
                                    font.bold: says
                                    horizontalAlignment: Text.AlignHCenter
                                    Layout.preferredWidth: 96
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                Layout.preferredWidth: 150
                                horizontalAlignment: Text.AlignRight
                                text: nameRow.modelData.added ? "added"
                                    : nameRow.modelData.renamedFrom !== "" ? "was " + nameRow.modelData.renamedFrom : ""
                                color: nameRow.modelData.added ? editorRoot.addedColor : BtDarkStyle.accentColor
                                font.pixelSize: 11
                                font.italic: true
                                elide: Text.ElideRight
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: editorRoot.rowsModel.length === 0
                        text: "No names match."
                        color: BtDarkStyle.textDim
                        font.pixelSize: 13
                    }
                }

                // Selected name's actions
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: BtDarkStyle.bgDark
                    border.color: BtDarkStyle.borderDark
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 8
                        readonly property var row: editorRoot.rowsModel.find((r) => r.index === editorRoot.selected)
                        Text {
                            Layout.fillWidth: true
                            text: parent.row ? parent.row.name + "  ·  " + parent.row.hash
                                               + (dm.hasCommentary ? "  ·  " + (parent.row.voices > 0 ? "said by " + parent.row.voiceNames : "no commentator says it") : "")
                                             : "Double-click a name to rename it."
                            color: parent.row ? BtDarkStyle.textMain : BtDarkStyle.textDim
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        SmallButton {
                            text: "Revert"
                            tip: "Put back the name it had"
                            enabled: !!parent.row && parent.row.renamedFrom !== ""
                            onClicked: {
                                const name = parent.row.renamedFrom
                                if (dm.revertName(editorRoot.list, editorRoot.selected)) {
                                    editorRoot.refresh()
                                    editorRoot.showName(dm.indexOfName(editorRoot.list, name))
                                }
                            }
                        }
                        SmallButton {
                            text: "Delete"
                            tip: "Remove the name from the list"
                            enabled: !!parent.row && !parent.row.isNone
                            onClicked: {
                                if (dm.removeName(editorRoot.list, editorRoot.selected)) {
                                    editorRoot.selected = -1
                                    editorRoot.refresh()
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
                    color: dm.statusOk ? BtDarkStyle.textDim : editorRoot.errorColor
                    text: "Status: " + (dm.statusText !== "" ? dm.statusText : "Ready")
                }
                Text {
                    text: dm.modified ? "Unsaved changes" : ""
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

    // ------------------------------------------------------------------------------------------------
    // Menus / dialogs
    // ------------------------------------------------------------------------------------------------
    Menu {
        id: filesMenu
        width: 300
        MenuItem {
            text: "Export This List..."
            icon.source: editorRoot.iconDir + "extract_32.png"
            icon.color: "transparent"
            onTriggered: fileDialog.start("export")
        }
        MenuItem {
            text: "Import Names into This List..."
            icon.source: editorRoot.iconDir + "modify_32.png"
            icon.color: "transparent"
            onTriggered: fileDialog.start("import")
        }
        MenuSeparator {}
        MenuItem {
            text: "Revert All Changes"
            icon.source: editorRoot.iconDir + "reset_32.png"
            icon.color: "transparent"
            enabled: dm.modified
            onTriggered: {
                dm.revertAll()
                editorRoot.refresh()
            }
        }
        MenuItem {
            text: "Reload"
            icon.source: editorRoot.iconDir + "open_32.png"
            icon.color: "transparent"
            onTriggered: editorRoot.guarded(() => dm.load())
        }
    }

    FileDialog {
        id: fileDialog
        property string mode: "export"
        function start(m) {
            mode = m
            const folder = settings.lastFolder !== "" ? settings.lastFolder
                                                      : StandardPaths.writableLocation(StandardPaths.DocumentsLocation).toString()
            currentFolder = folder
            fileMode = m === "import" ? FileDialog.OpenFile : FileDialog.SaveFile
            nameFilters = ["Text (*.txt)", "All files (*)"]
            defaultSuffix = "txt"
            const l = editorRoot.listsModel[editorRoot.list] || {}
            title = m === "import" ? "Import names (one per line)" : "Export the names"
            if (fileMode === FileDialog.SaveFile)
                selectedFile = folder + "/" + encodeURIComponent("dialogue_" + (l.key || "names") + ".txt")
            open()
        }
        onAccepted: {
            const url = selectedFile.toString()
            settings.lastFolder = url.substring(0, url.lastIndexOf("/"))
            if (mode === "export")
                dm.exportList(editorRoot.list, url)
            else {
                dm.importList(editorRoot.list, url)
                editorRoot.refresh()
            }
        }
    }

    // Names the commentary has categories for that the list doesn't have.
    ThemedDialog {
        id: unlistedDialog
        title: "Names with commentary that aren't listed"
        width: 560
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: "The commentary has lines for these, but players / teams can't be given them until they're in the list."
                color: BtDarkStyle.textDim
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            ListView {
                id: unlistedView
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                clip: true
                model: editorRoot.unlistedModel
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: RowLayout {
                    required property var modelData
                    width: unlistedView.width - 12
                    spacing: 10
                    Text {
                        text: modelData.name
                        color: BtDarkStyle.textMain
                        font.pixelSize: 13
                        font.family: "Consolas"
                        Layout.preferredWidth: 200
                        elide: Text.ElideRight
                    }
                    Text {
                        text: modelData.voiceNames
                        color: BtDarkStyle.textDim
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    SmallButton {
                        text: "Add"
                        onClicked: {
                            dm.addName(editorRoot.list, modelData.name)
                            editorRoot.refresh()
                        }
                    }
                }
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            HeaderButton {
                text: "Add All"
                accent: true
                implicitHeight: 34
                Layout.bottomMargin: 14
                enabled: editorRoot.unlistedModel.length > 0
                onClicked: {
                    dm.addNames(editorRoot.list, editorRoot.unlistedModel.map((u) => u.name))
                    editorRoot.refresh()
                    unlistedDialog.close()
                }
            }
            HeaderButton { text: "Close"; implicitHeight: 34; Layout.rightMargin: 16; Layout.bottomMargin: 14; onClicked: unlistedDialog.close() }
        }
    }

    ThemedDialog {
        id: unsavedDialog
        title: "Unsaved changes"
        message: "The name lists have unsaved changes. Leave without saving them?"
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
                text: "Discard"
                accent: true
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
