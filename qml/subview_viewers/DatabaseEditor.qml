import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import com.wouldymods.blobset 1.0

Item {
    id: editorRoot
    anchors.fill: parent

    readonly property string iconDir: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/"
    readonly property color errorColor: Material.color(Material.Red)
    readonly property color goodColor: "#5cb85c"

    // What the right hand side shows: "", "team" or "player".
    property string mode: ""
    property int teamIndex: -1
    property int playerIndex: -1
    property var teamData: ({})
    property var playerData: ({})
    property var teamRowsModel: []
    property var playerRowsModel: []

    Rectangle {
        anchors.fill: parent
        color: BtDarkStyle.bgDarkest
        z: -1
    }

    DatabaseManager {
        id: db
        onRevisionChanged: refreshTimer.restart()
        onLoadedChanged: {
            editorRoot.mode = ""
            editorRoot.teamIndex = -1
            editorRoot.playerIndex = -1
            editorRoot.reloadLists()
            if (db.loaded && editorRoot.teamRowsModel.length > 0)
                editorRoot.openTeam(editorRoot.teamRowsModel[0].index)
        }
        onFinished: (ok, message) => {
            if (editorRoot.pendingAfterSave && ok) {
                const action = editorRoot.pendingAfterSave
                editorRoot.pendingAfterSave = null
                action()
            } else {
                editorRoot.pendingAfterSave = null
            }
        }
    }

    Settings {
        id: settings
        category: "DatabaseEditor"
        property string lastFolder: ""
        property real splitWidth: 520
    }

    Component.onCompleted: db.load()

    // ------------------------------------------------------------------------------------------------
    // Data helpers (bindings read teamData / playerData, edits go through db and refresh them)
    // ------------------------------------------------------------------------------------------------
    function hasT(key) { return db.teamFieldTypes[key] !== undefined }
    function typeT(key) { return db.teamFieldTypes[key] || "" }
    function typeP(key) { return db.playerFieldTypes[key] || "" }
    function hasP(key) { return db.playerFieldTypes[key] !== undefined }
    readonly property bool isRl26: db.game === "rl26"
    readonly property bool isRll3: db.game === "rll3"
    readonly property bool isRll4: db.game === "rll4"
    // The game's own lists (they change with the game loaded).
    function gameList(name) { return db.game === "" ? [] : db.list(name) }
    function gameChoices(name) { return db.game === "" ? [] : db.choices(name) }
    readonly property bool isLeague: db.game !== "r25"

    function tv(field) { const v = teamData[field]; return v === undefined ? "" : v }
    function pv(field) { const v = playerData[field]; return v === undefined ? "" : v }
    function tn(field) { const v = teamData[field]; return v === undefined ? 0 : v }
    function pn(field) { const v = playerData[field]; return v === undefined ? 0 : v }

    function setT(field, value) {
        if (teamIndex >= 0 && db.setTeamField(teamIndex, field, value))
            teamData = db.team(teamIndex)
    }
    function setP(field, value) {
        if (playerIndex >= 0 && db.setPlayerField(playerIndex, field, value))
            playerData = db.player(playerIndex)
    }
    function presentT(field, on) {
        if (teamIndex >= 0 && db.setTeamPresent(teamIndex, field, on))
            teamData = db.team(teamIndex)
    }
    function presentP(field, on) {
        if (playerIndex >= 0 && db.setPlayerPresent(playerIndex, field, on))
            playerData = db.player(playerIndex)
    }

    function openTeam(index) {
        teamIndex = index
        teamData = db.team(index)
        mode = index >= 0 ? "team" : ""
        listTabs.currentIndex = 0
        if (squadOnly.checked)
            reloadLists()
    }
    function openPlayer(index) {
        if (index < 0)
            return
        playerIndex = index
        playerData = db.player(index)
        mode = "player"
    }
    function openPlayerById(id) {
        const index = db.playerIndexById(id)
        if (index < 0) {
            setStatus(false, "There's no player with id " + id + ".")
            return
        }
        openPlayer(index)
        listTabs.currentIndex = 1
    }

    property string localStatus: ""
    property bool localStatusOk: true
    function setStatus(ok, text) {
        localStatusOk = ok
        localStatus = text
    }
    Connections {
        target: db
        function onStatusChanged() { editorRoot.localStatus = "" }
    }

    function reloadLists() {
        const ty = teamList.contentY
        teamRowsModel = db.teamRows(teamSearch.text, teamGender.currentIndex - 1)
        teamList.contentY = ty
        const py = playerList.contentY
        playerRowsModel = db.playerRows(playerSearch.text, playerGender.currentIndex - 1,
                                        squadOnly.checked ? teamIndex : -1, playerRole.currentIndex - 1)
        playerList.contentY = py
    }

    Timer {
        id: refreshTimer
        interval: 120
        onTriggered: {
            if (editorRoot.teamIndex >= 0)
                editorRoot.teamData = db.team(editorRoot.teamIndex)
            if (editorRoot.playerIndex >= 0)
                editorRoot.playerData = db.player(editorRoot.playerIndex)
            editorRoot.reloadLists()
        }
    }
    Timer {
        id: searchDelay
        interval: 200
        onTriggered: editorRoot.reloadLists()
    }

    // Unsaved changes guard
    property var pendingAction: null
    property var pendingAfterSave: null
    function guarded(action) {
        if (db.unsavedChanges) {
            pendingAction = action
            unsavedDialog.open()
        } else {
            action()
        }
    }
    function goBack() {
        appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml")
    }
    function openAsM3mp() {
        appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/subview_viewers/M3MPViewer.qml")
        appOrchestrator.openM3mpViewerByHash(db.databaseHash.toLowerCase())
    }

    Shortcut { sequences: [StandardKey.Save]; enabled: db.loaded && !db.busy; onActivated: db.save() }
    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: {
            const field = listTabs.currentIndex === 0 ? teamSearch : playerSearch
            field.forceActiveFocus()
            field.selectAll()
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Look
    // ------------------------------------------------------------------------------------------------
    function ratingColor(r) {
        return r >= 85 ? "#4caf50" : r >= 75 ? "#9ccc65" : r >= 65 ? BtDarkStyle.accentColor : r >= 50 ? "#ff9800" : "#e53935"
    }

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
        implicitHeight: 30
        implicitWidth: Math.max(30, smallText.implicitWidth + 20)
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
        color: BtDarkStyle.accentColor
        font.pixelSize: 12
        font.bold: true
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
    }

    component FieldLabel: Text {
        color: BtDarkStyle.textDim
        font.pixelSize: 12
        elide: Text.ElideRight
        Layout.preferredWidth: 150
        Layout.alignment: Qt.AlignVCenter
    }

    // A card that groups fields, like the game's editor panels.
    component Card: Rectangle {
        id: card
        default property alias content: cardBody.data
        property string title: ""
        color: BtDarkStyle.bgDark
        border.color: BtDarkStyle.borderDark
        border.width: 1
        radius: 6
        implicitHeight: cardColumn.implicitHeight + 24
        Layout.fillWidth: true
        ColumnLayout {
            id: cardColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            spacing: 10
            PanelLabel {
                visible: card.title !== ""
                text: card.title
            }
            ColumnLayout {
                id: cardBody
                Layout.fillWidth: true
                spacing: 6
            }
        }
    }

    // Text edit: commit(text) when editing finishes.
    component TextFieldEdit: RowLayout {
        id: textEdit
        property string label: ""
        property string value: ""
        property int maxLength: 255
        property string placeholder: ""
        signal commit(string text)
        spacing: 10
        Layout.fillWidth: true
        FieldLabel { text: textEdit.label; visible: textEdit.label !== "" }
        TextField {
            id: textField
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            text: textEdit.value
            maximumLength: textEdit.maxLength
            placeholderText: textEdit.placeholder
            color: BtDarkStyle.textMain
            font.pixelSize: 13
            selectByMouse: true
            leftPadding: 8
            topPadding: 0
            bottomPadding: 0
            verticalAlignment: TextInput.AlignVCenter
            background: Rectangle {
                color: BtDarkStyle.bgMedium
                radius: 4
                border.color: textField.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
            }
            onEditingFinished: if (text !== textEdit.value) textEdit.commit(text)
        }
    }

    // Whole number edit.
    component NumberEdit: RowLayout {
        id: numberEdit
        property string label: ""
        property int value: 0
        property int from: 0
        property int to: 99
        property string suffix: ""
        signal commit(int value)
        spacing: 10
        Layout.fillWidth: true
        FieldLabel { text: numberEdit.label; visible: numberEdit.label !== "" }
        SpinBox {
            id: spin
            Layout.preferredWidth: 150
            Layout.preferredHeight: 36
            from: numberEdit.from
            to: numberEdit.to
            value: numberEdit.value
            editable: true
            font.pixelSize: 13
            textFromValue: (v) => String(v)
            valueFromText: (t) => parseInt(t) || 0
            onValueModified: numberEdit.commit(value)
        }
        Text {
            visible: numberEdit.suffix !== ""
            text: numberEdit.suffix
            color: BtDarkStyle.textDim
            font.pixelSize: 12
        }
        Item { Layout.fillWidth: true }
    }

    // Drop down: commit(index) when the user picks.
    component ComboEdit: RowLayout {
        id: comboEdit
        property string label: ""
        property var items: []
        property int index: 0
        property string unknownText: ""   // Shown when index is -1 (a value that isn't in the list).
        signal commit(int index)
        spacing: 10
        Layout.fillWidth: true
        FieldLabel { text: comboEdit.label; visible: comboEdit.label !== "" }
        ComboBox {
            id: combo
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            model: comboEdit.items
            currentIndex: comboEdit.index
            font.pixelSize: 13
            onActivated: (i) => comboEdit.commit(i)
            displayText: currentIndex < 0 && comboEdit.unknownText !== "" ? comboEdit.unknownText : currentText
            background: Rectangle {
                color: BtDarkStyle.bgMedium
                radius: 4
                border.color: combo.hovered ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
            }
            delegate: ItemDelegate {
                required property int index
                required property var modelData
                width: combo.width
                implicitHeight: 30
                highlighted: combo.highlightedIndex === index
                contentItem: Text {
                    text: modelData
                    color: parent.highlighted ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                    font.pixelSize: 13
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle { color: parent.highlighted ? BtDarkStyle.bgHover : "transparent" }
            }
        }
    }

    // A big whole number (hashes, ids): typed in.
    component BigNumberEdit: RowLayout {
        id: bigEdit
        property string label: ""
        property double value: 0
        signal commit(double value)
        spacing: 10
        Layout.fillWidth: true
        FieldLabel { text: bigEdit.label; visible: bigEdit.label !== "" }
        TextField {
            id: bigField
            Layout.preferredWidth: 150
            Layout.preferredHeight: 34
            text: String(bigEdit.value)
            color: BtDarkStyle.textMain
            font.pixelSize: 13
            font.family: "Consolas"
            selectByMouse: true
            leftPadding: 8
            topPadding: 0
            bottomPadding: 0
            verticalAlignment: TextInput.AlignVCenter
            validator: RegularExpressionValidator { regularExpression: /^(0x[0-9a-fA-F]{1,8}|[0-9]{1,10})$/ }
            background: Rectangle {
                color: BtDarkStyle.bgMedium
                radius: 4
                border.color: bigField.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
            }
            onEditingFinished: {
                const v = text.startsWith("0x") ? parseInt(text.substring(2), 16) : parseInt(text)
                if (!isNaN(v) && v !== bigEdit.value && v <= 4294967295)
                    bigEdit.commit(v)
            }
        }
        Text {
            text: "0x" + Math.floor(bigEdit.value).toString(16).toUpperCase()
            color: BtDarkStyle.textDim
            font.family: "Consolas"
            font.pixelSize: 11
        }
        Item { Layout.fillWidth: true }
    }

    // Generic fields from a paSerial blob (jerseys, appearance): [{key, label, group, kind, value, max}].
    // commit(key, value) when one is edited; the field shows the new value straight away.
    component FieldGrid: GridLayout {
        id: grid
        property var items: []
        property string group: ""        // Only this group ("" = all).
        signal commit(string key, var value)
        Layout.fillWidth: true
        columns: width > 900 ? 2 : 1
        columnSpacing: 20
        rowSpacing: 4
        Repeater {
            model: grid.group === "" ? grid.items : grid.items.filter((i) => i.group === grid.group)
            Loader {
                id: fieldLoader
                required property var modelData
                Layout.fillWidth: true
                property var field: modelData
                function edited(value) {
                    grid.commit(field.key, value)
                    const copy = Object.assign({}, field)
                    copy.value = value
                    field = copy
                }
                sourceComponent: modelData.kind === "colour" ? colourField
                               : modelData.kind === "choice" ? choiceField
                               : modelData.kind === "bool" ? boolField
                               : modelData.kind === "text" ? textField
                               : modelData.max > 65535 ? bigField : numberField
            }
        }
        Component {
            id: numberField
            NumberEdit {
                label: parent ? parent.field.label : ""
                value: parent ? parent.field.value : 0
                to: parent ? Math.min(parent.field.max, 2147483647) : 255
                onCommit: (v) => parent.edited(v)
            }
        }
        Component {
            id: bigField
            BigNumberEdit {
                label: parent ? parent.field.label : ""
                value: parent ? parent.field.value : 0
                onCommit: (v) => parent.edited(v)
            }
        }
        Component {
            id: boolField
            RowLayout {
                FieldLabel { text: parent && parent.parent ? parent.parent.field.label : "" }
                CheckEdit {
                    value: parent && parent.parent ? parent.parent.field.value === true : false
                    onCommit: (on) => parent.parent.edited(on)
                }
                Item { Layout.fillWidth: true }
            }
        }
        Component {
            id: textField
            TextFieldEdit {
                label: parent ? parent.field.label : ""
                value: parent ? parent.field.value : ""
                maxLength: parent ? parent.field.max : 24
                onCommit: (t) => parent.edited(t)
            }
        }
        Component {
            id: choiceField
            ChoiceEdit {
                label: parent ? parent.field.label : ""
                choices: parent ? parent.field.choices : []
                value: parent ? parent.field.value : 0
                onPick: (v) => parent.edited(v)
            }
        }
        Component {
            id: colourField
            ColourEdit {
                label: parent ? parent.field.label : ""
                value: parent ? parent.field.value : "#000000"
                onCommit: (c) => parent.edited(c)
            }
        }
    }

    // A drop down where the stored value isn't the position: choices [{name, value}]; pick(value) when one is chosen.
    // A value that isn't in the list is shown as it is.
    component ChoiceEdit: ComboEdit {
        id: choiceEdit
        property var choices: []
        property var value
        signal pick(var value)
        items: choices.map((c) => c.name)
        index: choices.findIndex((c) => c.value === value)
        unknownText: value === undefined || value === null ? "" : String(value)
        onCommit: (i) => choiceEdit.pick(choices[i].value)
    }

    component CheckEdit: CheckBox {
        id: checkEdit
        property bool value: false
        signal commit(bool on)
        checked: value
        font.pixelSize: 13
        onToggled: commit(checked)
    }

    // A rating bar like the game's: label, coloured bar, number. commit() when the drag ends.
    component StatSlider: RowLayout {
        id: stat
        property string label: ""
        property int value: 0
        property int to: 99
        signal commit(int value)
        function ratingColor(r) {
            return r >= 85 ? "#4caf50" : r >= 75 ? "#9ccc65" : r >= 65 ? BtDarkStyle.accentColor : r >= 50 ? "#ff9800" : "#e53935"
        }
        spacing: 10
        Layout.fillWidth: true
        FieldLabel { text: stat.label }
        Slider {
            id: slider
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            from: 0
            to: stat.to
            stepSize: 1
            snapMode: Slider.SnapAlways
            value: stat.value
            onPressedChanged: if (!pressed && Math.round(value) !== stat.value) stat.commit(Math.round(value))
            Keys.onReleased: if (Math.round(value) !== stat.value) stat.commit(Math.round(value))
            background: Rectangle {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: slider.availableWidth
                height: 8
                radius: 4
                color: BtDarkStyle.bgMedium
                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    radius: 4
                    color: stat.ratingColor(Math.round(slider.value))
                }
            }
            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: 14
                height: 14
                radius: 7
                color: slider.pressed ? BtDarkStyle.accentColor : BtDarkStyle.textMain
            }
        }
        Text {
            text: Math.round(slider.value)
            color: stat.ratingColor(Math.round(slider.value))
            font.pixelSize: 15
            font.bold: true
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 30
        }
    }

    // A colour swatch that opens a colour picker.
    component ColourEdit: RowLayout {
        id: colourEdit
        property string label: ""
        property string value: "#000000"
        signal commit(string colour)
        spacing: 10
        Layout.fillWidth: true
        FieldLabel { text: colourEdit.label }
        Rectangle {
            Layout.preferredWidth: 90
            Layout.preferredHeight: 28
            radius: 4
            color: colourEdit.value
            border.color: swatchArea.containsMouse ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
            border.width: 2
            MouseArea {
                id: swatchArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    colourDialog.selectedColor = colourEdit.value
                    colourDialog.open()
                }
            }
        }
        Text {
            text: colourEdit.value.toUpperCase()
            color: BtDarkStyle.textDim
            font.family: "Consolas"
            font.pixelSize: 12
        }
        Item { Layout.fillWidth: true }
        ColorDialog {
            id: colourDialog
            title: colourEdit.label
            onAccepted: colourEdit.commit(selectedColor.toString().substring(0, 7))
        }
    }

    component RatingBadge: Rectangle {
        id: badge
        property int rating: 0
        function ratingColor(r) {
            return r >= 85 ? "#4caf50" : r >= 75 ? "#9ccc65" : r >= 65 ? BtDarkStyle.accentColor : r >= 50 ? "#ff9800" : "#e53935"
        }
        implicitWidth: 34
        implicitHeight: 22
        radius: 4
        color: "transparent"
        border.color: badge.ratingColor(badge.rating)
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: badge.rating
            color: badge.ratingColor(badge.rating)
            font.bold: true
            font.pixelSize: 12
        }
    }

    component EditorTabs: TabBar {
        id: tabs
        Layout.fillWidth: true
        background: Rectangle { color: BtDarkStyle.bgDark }
    }
    component EditorTab: TabButton {
        id: tabButton
        width: implicitWidth
        leftPadding: 16
        rightPadding: 16
        contentItem: Text {
            text: tabButton.text
            color: tabButton.checked ? BtDarkStyle.accentColor : BtDarkStyle.textDim
            font.pixelSize: 13
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // Scrolling page for an editor tab.
    component Page_: ScrollView {
        id: page
        default property alias content: pageColumn.data
        clip: true
        contentWidth: availableWidth
        contentHeight: pageColumn.implicitHeight + 24
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: pageColumn
            width: page.availableWidth - 24
            x: 12
            y: 12
            spacing: 12
        }
    }

    // A row of a player table (squad / lineup / roles).
    component PlayerRow: Rectangle {
        id: playerRow
        property int rowIndex: 0
        property string leading: ""
        property var row: ({})
        property bool selected: false
        signal clicked()
        signal doubleClicked()
        implicitHeight: 30
        Layout.fillWidth: true
        color: selected ? BtDarkStyle.bgMedium
                        : rowMouse.containsMouse ? BtDarkStyle.bgHover
                                                 : (rowIndex % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
        Rectangle { visible: playerRow.selected; width: 3; height: parent.height; color: BtDarkStyle.accentColor }
        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: playerRow.clicked()
            onDoubleClicked: playerRow.doubleClicked()
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 10
            Text {
                text: playerRow.leading
                color: BtDarkStyle.textDim
                font.pixelSize: 12
                Layout.preferredWidth: 150
                elide: Text.ElideRight
                visible: playerRow.leading !== ""
            }
            Text {
                text: playerRow.row.id !== undefined && playerRow.row.id !== 0 ? playerRow.row.id : ""
                color: BtDarkStyle.textDim
                font.family: "Consolas"
                font.pixelSize: 12
                Layout.preferredWidth: 50
            }
            Text {
                text: ((playerRow.row.first || "") + " " + (playerRow.row.last || "")).trim()
                color: playerRow.row.known === false ? BtDarkStyle.textDim : BtDarkStyle.textMain
                font.pixelSize: 13
                font.italic: playerRow.row.known === false
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                text: playerRow.row.roles || ""
                color: BtDarkStyle.textDim
                font.pixelSize: 12
                Layout.preferredWidth: 110
            }
            RatingBadge {
                rating: playerRow.row.rating || 0
                visible: playerRow.row.known === true
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
                    source: editorRoot.iconDir + "database_editor_32.png"
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    fillMode: Image.PreserveAspectFit
                }
                ColumnLayout {
                    spacing: 0
                    Text {
                        text: "Database Editor" + (db.gameName !== "" ? "  ·  " + db.gameName : "")
                        color: BtDarkStyle.accentColor
                        font.bold: true
                        font.pixelSize: 16
                    }
                    Text {
                        text: db.loaded ? db.teamCount + " teams  ·  " + db.playerCount + " players  ·  " + db.sourceText
                                        : "Teams and players (Rugby League 26, Rugby League Live 4, Rugby 25)"
                        color: BtDarkStyle.textDim
                        font.pixelSize: 11
                    }
                }
                Item { Layout.fillWidth: true }
                BusyIndicator {
                    running: db.busy
                    visible: db.busy
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                }
                HeaderButton {
                    id: moreButton
                    text: "Database ▾"
                    tip: "Reload, export the lists as CSV, open the M3MP"
                    iconSource: editorRoot.iconDir + "open_32.png"
                    onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
                }
                HeaderButton {
                    text: "Save to Mods"
                    tip: db.modsTarget !== "" ? "Save the database into the mods folder (" + db.modsTarget + "), ready for Modify (Ctrl+S)"
                                              : "Save the database into the mods folder, ready for Modify (Ctrl+S)"
                    iconSource: editorRoot.iconDir + "save_32.png"
                    accent: db.unsavedChanges
                    enabled: db.loaded && !db.busy
                    onClicked: db.save()
                }
            }
        }

        // Body
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            enabled: !db.busy

            handle: Rectangle {
                implicitWidth: 4
                color: SplitHandle.hovered ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
            }

            // ---- Teams / players lists -----------------------------------------------------------------
            ColumnLayout {
                SplitView.preferredWidth: settings.splitWidth
                SplitView.minimumWidth: 380
                onWidthChanged: if (width > 0) settings.splitWidth = width
                spacing: 0

                EditorTabs {
                    id: listTabs
                    EditorTab { text: "Teams (" + editorRoot.teamRowsModel.length + ")" }
                    EditorTab { text: "Players (" + editorRoot.playerRowsModel.length + ")" }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // A page's minimum height (its fixed size rows) mustn't make the panel taller than the window
                    // (small screens): the page scrolls instead.
                    Layout.minimumHeight: 0
                    clip: true
                    currentIndex: listTabs.currentIndex

                    // Teams
                    ColumnLayout {
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: 8
                            spacing: 8
                            TextField {
                                id: teamSearch
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                placeholderText: "Search teams (name or id)..."
                                color: BtDarkStyle.textMain
                                font.pixelSize: 13
                                leftPadding: 8
                                topPadding: 0
                                bottomPadding: 0
                                verticalAlignment: TextInput.AlignVCenter
                                background: Rectangle {
                                    color: BtDarkStyle.bgMedium
                                    radius: 4
                                    border.color: teamSearch.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                                }
                                onTextEdited: searchDelay.restart()
                            }
                            ComboBox {
                                id: teamGender
                                Layout.preferredWidth: 120
                                Layout.preferredHeight: 36
                                model: ["All", "Men", "Women"]
                                onActivated: editorRoot.reloadLists()
                            }
                        }
                        ListView {
                            id: teamList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: editorRoot.teamRowsModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                id: teamRow
                                required property var modelData
                                required property int index
                                readonly property bool isCurrent: editorRoot.mode === "team" && modelData.index === editorRoot.teamIndex
                                width: teamList.width
                                height: 44
                                color: isCurrent ? BtDarkStyle.bgMedium
                                                 : teamMouse.containsMouse ? BtDarkStyle.bgHover
                                                                           : (index % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
                                Rectangle { width: 4; height: parent.height; color: teamRow.modelData.primaryColour }
                                Rectangle { visible: teamRow.isCurrent; anchors.right: parent.right; width: 3; height: parent.height; color: BtDarkStyle.accentColor }
                                MouseArea {
                                    id: teamMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: editorRoot.openTeam(teamRow.modelData.index)
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 10
                                    Image {
                                        source: teamRow.modelData.logoUrl
                                        Layout.preferredWidth: 34
                                        Layout.preferredHeight: 34
                                        sourceSize: Qt.size(68, 68)
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                    }
                                    ColumnLayout {
                                        spacing: 0
                                        Layout.fillWidth: true
                                        Text {
                                            text: teamRow.modelData.fullName
                                            color: BtDarkStyle.textMain
                                            font.pixelSize: 13
                                            font.bold: teamRow.isCurrent
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            text: "ID " + teamRow.modelData.id + "  ·  " + teamRow.modelData.players + " players"
                                                  + (teamRow.modelData.gender === 1 ? "  ·  Women" : "")
                                            color: BtDarkStyle.textDim
                                            font.pixelSize: 11
                                        }
                                    }
                                    RatingBadge { rating: teamRow.modelData.rating; visible: teamRow.modelData.rating > 0 }
                                }
                            }
                        }
                    }

                    // Players
                    ColumnLayout {
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 8
                            Layout.rightMargin: 8
                            Layout.topMargin: 8
                            spacing: 8
                            TextField {
                                id: playerSearch
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                placeholderText: "Search players (name or id)..."
                                color: BtDarkStyle.textMain
                                font.pixelSize: 13
                                leftPadding: 8
                                topPadding: 0
                                bottomPadding: 0
                                verticalAlignment: TextInput.AlignVCenter
                                background: Rectangle {
                                    color: BtDarkStyle.bgMedium
                                    radius: 4
                                    border.color: playerSearch.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                                }
                                onTextEdited: searchDelay.restart()
                            }
                            ComboBox {
                                id: playerGender
                                Layout.preferredWidth: 110
                                Layout.preferredHeight: 36
                                model: ["All", "Men", "Women"]
                                onActivated: editorRoot.reloadLists()
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 8
                            Layout.rightMargin: 8
                            spacing: 8
                            ComboBox {
                                id: playerRole
                                visible: editorRoot.isLeague
                                Layout.preferredWidth: 160
                                Layout.preferredHeight: 36
                                model: ["All positions"].concat(Array.from(db.list("roles")))
                                onActivated: editorRoot.reloadLists()
                            }
                            CheckBox {
                                id: squadOnly
                                text: editorRoot.teamIndex >= 0 ? "Only " + (editorRoot.teamData.abbreviatedName || "the team") + "'s squad"
                                                                : "Only the team's squad"
                                enabled: editorRoot.teamIndex >= 0
                                onToggled: editorRoot.reloadLists()
                            }
                            Item { Layout.fillWidth: true }
                        }
                        // Column titles
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 28
                            color: BtDarkStyle.bgDark
                            border.color: BtDarkStyle.borderDark
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 8
                                Text { text: "ID"; color: BtDarkStyle.accentColor; font.bold: true; font.pixelSize: 12; Layout.preferredWidth: 50 }
                                Text { text: "Name"; color: BtDarkStyle.accentColor; font.bold: true; font.pixelSize: 12; Layout.fillWidth: true }
                                Text { text: "Pos"; color: BtDarkStyle.accentColor; font.bold: true; font.pixelSize: 12; Layout.preferredWidth: 30 }
                                Text { text: "Age"; color: BtDarkStyle.accentColor; font.bold: true; font.pixelSize: 12; Layout.preferredWidth: 30 }
                                Text { text: "OVR"; color: BtDarkStyle.accentColor; font.bold: true; font.pixelSize: 12; Layout.preferredWidth: 34 }
                            }
                        }
                        ListView {
                            id: playerList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: editorRoot.playerRowsModel
                            boundsBehavior: Flickable.StopAtBounds
                            reuseItems: true
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                id: playerListRow
                                required property var modelData
                                required property int index
                                readonly property bool isCurrent: editorRoot.mode === "player" && modelData.index === editorRoot.playerIndex
                                width: playerList.width
                                height: 32
                                color: isCurrent ? BtDarkStyle.bgMedium
                                                 : playerMouse.containsMouse ? BtDarkStyle.bgHover
                                                                             : (index % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
                                Rectangle { visible: playerListRow.isCurrent; width: 3; height: parent.height; color: BtDarkStyle.accentColor }
                                MouseArea {
                                    id: playerMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: editorRoot.openPlayer(playerListRow.modelData.index)
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 8
                                    Text {
                                        text: playerListRow.modelData.id
                                        color: BtDarkStyle.textDim
                                        font.family: "Consolas"
                                        font.pixelSize: 12
                                        Layout.preferredWidth: 50
                                    }
                                    ColumnLayout {
                                        spacing: -2
                                        Layout.fillWidth: true
                                        Text {
                                            text: (playerListRow.modelData.gender === 1 ? "♀ " : "") + playerListRow.modelData.first + " " + playerListRow.modelData.last
                                            color: BtDarkStyle.textMain
                                            font.pixelSize: 13
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            text: playerListRow.modelData.clubName
                                            color: BtDarkStyle.textDim
                                            font.pixelSize: 10
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }
                                    Text { text: playerListRow.modelData.roleName; color: BtDarkStyle.textDim; font.pixelSize: 12; Layout.preferredWidth: 30 }
                                    Text { text: playerListRow.modelData.age; color: BtDarkStyle.textDim; font.pixelSize: 12; Layout.preferredWidth: 30 }
                                    RatingBadge { rating: playerListRow.modelData.rating }
                                }
                            }
                        }
                    }
                }
            }

            // ---- Editor --------------------------------------------------------------------------------
            StackLayout {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 560
                currentIndex: editorRoot.mode === "team" ? 1 : editorRoot.mode === "player" ? 2 : 0

                // Nothing selected / not loaded.
                Item {
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 80
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: db.loaded ? "Pick a team or a player on the left."
                                        : (db.statusText !== "" ? db.statusText : "Loading the database...")
                        color: db.loaded || db.statusOk ? BtDarkStyle.textDim : editorRoot.errorColor
                        font.pixelSize: 15
                    }
                }

                // ---------------- Team editor ----------------
                ColumnLayout {
                    spacing: 0

                    // Team banner
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: editorRoot.tv("primaryRGBColour") || BtDarkStyle.bgDark }
                            GradientStop { position: 0.55; color: BtDarkStyle.bgDark }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 3; color: editorRoot.tv("secondaryRGBColour") || BtDarkStyle.borderDark }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 16
                            Image {
                                visible: source.toString() !== ""
                                source: editorRoot.tv("_logoUrl")
                                Layout.preferredWidth: 76
                                Layout.preferredHeight: 76
                                sourceSize: Qt.size(152, 152)
                                fillMode: Image.PreserveAspectFit
                            }
                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true
                                Text {
                                    text: editorRoot.tv("fullName")
                                    color: BtDarkStyle.white
                                    font.pixelSize: 22
                                    font.bold: true
                                    style: Text.Outline
                                    styleColor: "#80000000"
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: "Team " + editorRoot.tv("_index") + "  ·  ID " + editorRoot.tv("id") + "  ·  " + editorRoot.tv("_players") + " players"
                                          + "  ·  " + (editorRoot.tn("sex") === 1 ? "Women" : "Men")
                                    color: BtDarkStyle.textMain
                                    font.pixelSize: 12
                                }
                            }
                            ColumnLayout {
                                spacing: 0
                                Text { text: "OVR"; color: BtDarkStyle.textDim; font.pixelSize: 11; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                                Text {
                                    text: editorRoot.tn("_rating")
                                    color: editorRoot.ratingColor(editorRoot.tn("_rating"))
                                    font.pixelSize: 30
                                    font.bold: true
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }
                    }

                    EditorTabs {
                        id: teamTabs
                        EditorTab { text: "Details" }
                        EditorTab { text: "Stadiums" }
                        EditorTab { text: "Squad" }
                        EditorTab { text: "Lineups" }
                        EditorTab { text: "Jerseys" }
                        EditorTab { text: "Feeder Clubs" }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        // A tab's minimum height (its fixed size rows, e.g. the Jerseys list) mustn't make the panel
                        // taller than the window on small screens: that pushed the bottom of every tab (the team
                        // colours) under the status bar. The tabs scroll instead.
                        Layout.minimumHeight: 0
                        clip: true
                        currentIndex: teamTabs.currentIndex

                        // ---- Details
                        Page_ {
                            Card {
                                title: "Names"
                                TextFieldEdit { visible: editorRoot.hasT("fullName"); label: "Full Name"; value: editorRoot.tv("fullName"); onCommit: (t) => editorRoot.setT("fullName", t) }
                                TextFieldEdit { visible: editorRoot.hasT("shortName"); label: "Location Name"; value: editorRoot.tv("shortName"); onCommit: (t) => editorRoot.setT("shortName", t) }
                                TextFieldEdit { visible: editorRoot.hasT("nickname"); label: "Club Name"; value: editorRoot.tv("nickname"); onCommit: (t) => editorRoot.setT("nickname", t) }
                                TextFieldEdit { visible: editorRoot.hasT("hudName"); label: "Abbreviation"; value: editorRoot.tv("hudName"); maxLength: 8; onCommit: (t) => editorRoot.setT("hudName", t) }
                                TextFieldEdit { visible: editorRoot.hasT("shirtName"); label: "Jersey Name"; value: editorRoot.tv("shirtName"); onCommit: (t) => editorRoot.setT("shirtName", t) }
                                NumberEdit { visible: editorRoot.hasT("id"); label: "Team ID"; value: editorRoot.tn("id"); to: 999999; onCommit: (v) => editorRoot.setT("id", v) }
                            }
                            Card {
                                title: "Team"
                                ComboEdit { visible: editorRoot.hasT("clubType") && editorRoot.isRl26; label: "Team Type"; items: db.list("teamTypes"); index: editorRoot.tn("clubType"); onCommit: (i) => editorRoot.setT("clubType", i) }
                                NumberEdit { visible: editorRoot.hasT("clubType") && !editorRoot.isRl26; label: "Team Type"; value: editorRoot.tn("clubType"); to: 255; onCommit: (v) => editorRoot.setT("clubType", v) }
                                NumberEdit { visible: editorRoot.hasT("clubTypeAdditional"); label: "Team Type (extra)"; value: editorRoot.tn("clubTypeAdditional"); to: 255; onCommit: (v) => editorRoot.setT("clubTypeAdditional", v) }
                                ComboEdit { visible: editorRoot.hasT("affiliations") && editorRoot.isRl26; label: "Affiliation"; items: db.list("affiliations"); index: editorRoot.tn("affiliations"); onCommit: (i) => editorRoot.setT("affiliations", i) }
                                NumberEdit { visible: editorRoot.hasT("affiliations") && !editorRoot.isRl26; label: "Affiliation"; value: editorRoot.tn("affiliations"); to: 999; onCommit: (v) => editorRoot.setT("affiliations", v) }
                                NumberEdit { visible: editorRoot.hasT("affiliation"); label: "Affiliation"; value: editorRoot.tn("affiliation"); to: 999; onCommit: (v) => editorRoot.setT("affiliation", v) }
                                ComboEdit { visible: editorRoot.hasT("sex"); label: "Gender"; items: db.list("genders"); index: editorRoot.tn("sex"); onCommit: (i) => editorRoot.setT("sex", i) }
                                ComboEdit { visible: editorRoot.hasT("supporters") && (editorRoot.isRl26 || editorRoot.isRll4); label: "Supporters"; items: editorRoot.gameList("supporters"); index: editorRoot.tn("supporters"); onCommit: (i) => editorRoot.setT("supporters", i) }
                                ChoiceEdit { visible: editorRoot.hasT("supporters") && editorRoot.isRll3; label: "Supporters"; choices: editorRoot.gameChoices("supporters"); value: editorRoot.tn("supporters"); onPick: (v) => editorRoot.setT("supporters", v) }
                                NumberEdit { visible: editorRoot.hasT("supporters") && !editorRoot.isRl26 && !editorRoot.isRll3 && !editorRoot.isRll4; label: "Supporters"; value: editorRoot.tn("supporters"); to: 999; onCommit: (v) => editorRoot.setT("supporters", v) }
                                ComboEdit {
                                    visible: editorRoot.hasT("icon") && editorRoot.isRl26
                                    label: "Logo"
                                    items: db.list("logos")
                                    index: items.indexOf(editorRoot.tv("icon"))
                                    unknownText: editorRoot.tv("icon")
                                    onCommit: (i) => editorRoot.setT("icon", items[i])
                                }
                                ChoiceEdit { visible: editorRoot.hasT("icon") && (editorRoot.isRll3 || editorRoot.isRll4); label: "Logo"; choices: editorRoot.gameChoices("logos"); value: editorRoot.tv("icon"); onPick: (v) => editorRoot.setT("icon", v) }
                                TextFieldEdit { visible: editorRoot.hasT("icon") && !editorRoot.isRl26 && !editorRoot.isRll3 && !editorRoot.isRll4; label: "Logo"; value: editorRoot.tv("icon"); onCommit: (t) => editorRoot.setT("icon", t) }
                                ChoiceEdit { visible: editorRoot.hasT("worldCupIcon") && editorRoot.isRll4; label: "World Cup Logo"; choices: editorRoot.gameChoices("logos"); value: editorRoot.tv("worldCupIcon"); onPick: (v) => editorRoot.setT("worldCupIcon", v) }
                                TextFieldEdit { visible: editorRoot.hasT("worldCupIcon") && !editorRoot.isRl26 && !editorRoot.isRll4; label: "World Cup Logo"; value: editorRoot.tv("worldCupIcon"); onCommit: (t) => editorRoot.setT("worldCupIcon", t) }
                                ComboEdit {
                                    visible: editorRoot.hasT("commentaryTeamLocationHash") && items.length > 0
                                    label: "Commentary Location"
                                    items: db.commentaryNames("location")
                                    index: db.commentaryIndex("location", editorRoot.tn("commentaryTeamLocationHash"))
                                    unknownText: "0x" + editorRoot.tn("commentaryTeamLocationHash").toString(16).toUpperCase()
                                    onCommit: (i) => editorRoot.setT("commentaryTeamLocationHash", db.commentaryHash("location", i))
                                }
                                ComboEdit {
                                    visible: editorRoot.hasT("commentaryTeamMascotHash") && items.length > 0
                                    label: "Commentary Mascot"
                                    items: db.commentaryNames("mascot")
                                    index: db.commentaryIndex("mascot", editorRoot.tn("commentaryTeamMascotHash"))
                                    unknownText: "0x" + editorRoot.tn("commentaryTeamMascotHash").toString(16).toUpperCase()
                                    onCommit: (i) => editorRoot.setT("commentaryTeamMascotHash", db.commentaryHash("mascot", i))
                                }
                                // RLL3 stores the commentary name as text, RLL4 as a number, Rugby 25 as a hash (sound M3MP lists).
                                ComboEdit {
                                    visible: editorRoot.typeT("commentary") === "string" && items.length > 0
                                    label: "Commentary"
                                    items: db.loaded ? db.commentaryNames("teams") : []
                                    index: db.commentaryIndexOf("teams", editorRoot.tv("commentary"))
                                    unknownText: editorRoot.tv("commentary")
                                    onCommit: (i) => editorRoot.setT("commentary", db.commentaryValue("teams", i))
                                }
                                TextFieldEdit { visible: editorRoot.typeT("commentary") === "string" && db.commentaryNames("teams").length === 0; label: "Commentary"; value: editorRoot.tv("commentary"); onCommit: (t) => editorRoot.setT("commentary", t) }
                                ComboEdit { visible: editorRoot.typeT("commentary") === "int" && editorRoot.isRll4; label: "Commentary"; items: editorRoot.gameList("commentaryTeams"); index: editorRoot.tn("commentary"); unknownText: "Commentary " + editorRoot.tn("commentary"); onCommit: (i) => editorRoot.setT("commentary", i) }
                                NumberEdit { visible: editorRoot.typeT("commentary") === "int" && !editorRoot.isRll4; label: "Commentary"; value: editorRoot.tn("commentary"); to: 99999; onCommit: (v) => editorRoot.setT("commentary", v) }
                                ComboEdit {
                                    visible: editorRoot.hasT("commentaryTeamHash") && items.length > 0
                                    label: "Commentary"
                                    items: db.loaded ? db.commentaryNames("teams") : []
                                    index: db.commentaryIndex("teams", editorRoot.tn("commentaryTeamHash"))
                                    unknownText: "0x" + editorRoot.tn("commentaryTeamHash").toString(16).toUpperCase()
                                    onCommit: (i) => editorRoot.setT("commentaryTeamHash", db.commentaryHash("teams", i))
                                }
                                BigNumberEdit { visible: editorRoot.hasT("commentaryTeamHash") && db.commentaryNames("teams").length === 0; label: "Commentary Hash"; value: editorRoot.tn("commentaryTeamHash"); onCommit: (v) => editorRoot.setT("commentaryTeamHash", v) }
                                NumberEdit { visible: editorRoot.hasT("colourSet"); label: "Colour Set"; value: editorRoot.tn("colourSet"); to: 255; onCommit: (v) => editorRoot.setT("colourSet", v) }
                                NumberEdit { visible: editorRoot.hasT("formation"); label: "Formation"; value: editorRoot.tn("formation"); to: 65535; onCommit: (v) => editorRoot.setT("formation", v) }
                                NumberEdit { visible: editorRoot.hasT("defaultLineupCategory"); label: "Default Lineup Category"; value: editorRoot.tn("defaultLineupCategory"); to: 255; onCommit: (v) => editorRoot.setT("defaultLineupCategory", v) }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 16
                                    CheckEdit { visible: editorRoot.hasT("licensed"); text: "Licensed"; value: editorRoot.tv("licensed") === true; onCommit: (on) => editorRoot.setT("licensed", on) }
                                    CheckEdit { visible: editorRoot.hasT("deleted"); text: "Deleted"; value: editorRoot.tv("deleted") === true; onCommit: (on) => editorRoot.setT("deleted", on) }
                                    CheckEdit { visible: editorRoot.hasT("frontendVisible"); text: "Shown in the menus"; value: editorRoot.tv("frontendVisible") === true; onCommit: (on) => editorRoot.setT("frontendVisible", on) }
                                    CheckEdit {
                                        visible: editorRoot.hasT("isWorldCup")
                                        text: "World Cup team"
                                        value: editorRoot.tv("isWorldCup") === true
                                        onCommit: (on) => {
                                            editorRoot.setT("isWorldCup", on)
                                            // The C# editor: a World Cup team has a World Cup logo (the same logo).
                                            if (on && editorRoot.hasT("worldCupIcon") && !editorRoot.tv("worldCupIcon?"))
                                                editorRoot.setT("worldCupIcon", editorRoot.tv("icon"))
                                        }
                                    }
                                    CheckEdit { visible: editorRoot.hasT("alternateNumbering"); text: "Alternate numbering"; value: editorRoot.tv("alternateNumbering") === true; onCommit: (on) => editorRoot.setT("alternateNumbering", on) }
                                }
                                NumberEdit { visible: editorRoot.hasT("alternativeNumberingSystem"); label: "Numbering System"; value: editorRoot.tn("alternativeNumberingSystem"); to: 99; onCommit: (v) => editorRoot.setT("alternativeNumberingSystem", v) }
                            }
                            Card {
                                title: "Colours"
                                ColourEdit { visible: editorRoot.hasT("primaryRGBColour"); label: "Primary"; value: editorRoot.tv("primaryRGBColour") || "#000000"; onCommit: (c) => editorRoot.setT("primaryRGBColour", c) }
                                ColourEdit { visible: editorRoot.hasT("secondaryRGBColour"); label: "Secondary"; value: editorRoot.tv("secondaryRGBColour") || "#000000"; onCommit: (c) => editorRoot.setT("secondaryRGBColour", c) }
                                ColourEdit { visible: editorRoot.hasT("hudTextRGBColour"); label: "HUD Text"; value: editorRoot.tv("hudTextRGBColour") || "#000000"; onCommit: (c) => editorRoot.setT("hudTextRGBColour", c) }
                            }
                        }

                        // ---- Stadiums
                        Page_ {
                            Card {
                                title: "Grounds"
                                visible: db.game === "rll3"
                                Text {
                                    text: "Rugby League Live 3 keeps the team's grounds as a list: the last one is the Grand Final ground (Old Trafford for Super League teams). Clear a ground's name to take it out."
                                    color: BtDarkStyle.textDim
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                            Card {
                                title: "Home Grounds"
                                visible: editorRoot.hasT("homeGroundCount")
                                NumberEdit {
                                    label: "Grounds used"
                                    value: Math.max(1, editorRoot.tn("homeGroundCount"))
                                    from: 1
                                    to: 3
                                    onCommit: (v) => editorRoot.setT("homeGroundCount", v)
                                }
                            }
                            Repeater {
                                model: [{ key: "homeGrounds#0:", title: "Primary Ground", n: 1 },
                                        { key: "homeGrounds#1:", title: "Secondary Ground", n: 2 },
                                        { key: "homeGrounds#2:", title: "Tertiary Ground", n: 3 },
                                        { key: "homeGrounds#3:", title: "Ground 4", n: 4 },
                                        { key: "homeGroundFinal:", title: "Final Ground", n: 0 }]
                                Card {
                                    id: groundCard
                                    required property var modelData
                                    readonly property string k: modelData.key
                                    // RLL3 keeps a list of grounds (the last one is the Grand Final ground).
                                    title: db.game === "rll3" ? "Ground " + modelData.n : modelData.title
                                    visible: editorRoot.hasT(k + "stadium")
                                    opacity: !editorRoot.hasT("homeGroundCount") || modelData.n === 0
                                             || modelData.n <= Math.max(1, editorRoot.tn("homeGroundCount")) ? 1.0 : 0.45
                                    ComboEdit {
                                        visible: editorRoot.isRl26 || editorRoot.isRll4
                                        label: "Stadium"
                                        items: editorRoot.gameList("stadiums")
                                        index: editorRoot.tn(groundCard.k + "stadium") - 5
                                        unknownText: "Stadium id " + editorRoot.tn(groundCard.k + "stadium")
                                        onCommit: (i) => editorRoot.setT(groundCard.k + "stadium", i + 5)
                                    }
                                    TextFieldEdit {
                                        visible: editorRoot.typeT(groundCard.k + "stadium") === "string"
                                        label: "Stadium"
                                        value: editorRoot.tv(groundCard.k + "stadium")
                                        placeholder: groundCard.modelData.n > 1 ? "Empty = not used" : ""
                                        onCommit: (t) => {
                                            if (t === "" && groundCard.modelData.n > 1)
                                                editorRoot.presentT(groundCard.k + "stadium", false)
                                            else
                                                editorRoot.setT(groundCard.k + "stadium", t)
                                        }
                                    }
                                    NumberEdit {
                                        visible: !editorRoot.isRl26 && !editorRoot.isRll4 && editorRoot.typeT(groundCard.k + "stadium") === "int"
                                        label: "Stadium id"
                                        value: editorRoot.tn(groundCard.k + "stadium")
                                        to: 9999
                                        onCommit: (v) => editorRoot.setT(groundCard.k + "stadium", v)
                                    }
                                    RowLayout {
                                        visible: editorRoot.hasT(groundCard.k + "wantCustomName")
                                        spacing: 10
                                        CheckEdit {
                                            text: "Custom name"
                                            value: editorRoot.tv(groundCard.k + "wantCustomName") === true
                                            onCommit: (on) => {
                                                editorRoot.setT(groundCard.k + "wantCustomName", on)
                                                if (on && editorRoot.tv(groundCard.k + "customName") === "") {
                                                    const names = editorRoot.isRl26 || editorRoot.isRll4 ? editorRoot.gameList("stadiums") : []
                                                    editorRoot.setT(groundCard.k + "customName", names[editorRoot.tn(groundCard.k + "stadium") - 5] || "Stadium")
                                                } else if (!on) {
                                                    editorRoot.presentT(groundCard.k + "customName", false)
                                                }
                                            }
                                        }
                                        TextFieldEdit {
                                            enabled: editorRoot.tv(groundCard.k + "wantCustomName") === true
                                            value: editorRoot.tv(groundCard.k + "customName")
                                            placeholder: "Name shown in the game"
                                            onCommit: (t) => editorRoot.setT(groundCard.k + "customName", t)
                                        }
                                    }
                                }
                            }
                        }

                        // ---- Squad
                        ColumnLayout {
                            id: squadPage
                            spacing: 0
                            property int selected: -1
                            property var rows: editorRoot.mode === "team" && db.revision >= 0 ? db.roster(editorRoot.teamIndex) : []
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.margins: 10
                                spacing: 8
                                PanelLabel { text: "Squad  ·  " + squadPage.rows.length + " / 64" }
                                Item { Layout.fillWidth: true }
                                SmallButton {
                                    text: "Add Player"
                                    tip: "Pick a player to add to the squad (Insert)"
                                    accent: true
                                    enabled: squadPage.rows.length < 64
                                    onClicked: playerPicker.pick("Add a player to the squad", -1, (id) => db.addRosterPlayer(editorRoot.teamIndex, id))
                                }
                                SmallButton {
                                    text: "Replace"
                                    tip: "Swap the selected player for another"
                                    enabled: squadPage.selected >= 0 && squadPage.selected < squadPage.rows.length
                                    onClicked: {
                                        const slot = squadPage.selected
                                        playerPicker.pick("Replace " + squadPage.rows[slot].first + " " + squadPage.rows[slot].last, -1,
                                                          (id) => db.setRosterPlayer(editorRoot.teamIndex, slot, id))
                                    }
                                }
                                SmallButton {
                                    text: "Remove"
                                    tip: "Take the selected player out of the squad (Delete)"
                                    enabled: squadPage.selected >= 0 && squadPage.selected < squadPage.rows.length
                                    onClicked: {
                                        db.removeRosterPlayer(editorRoot.teamIndex, squadPage.selected)
                                        squadPage.selected = Math.min(squadPage.selected, squadPage.rows.length - 2)
                                    }
                                }
                                SmallButton {
                                    text: "Sort A-Z"
                                    tip: "Sort the squad by last name"
                                    enabled: squadPage.rows.length > 1
                                    onClicked: db.sortRoster(editorRoot.teamIndex)
                                }
                                SmallButton {
                                    text: "Edit Player"
                                    tip: "Open the selected player (or double click a row)"
                                    enabled: squadPage.selected >= 0 && squadPage.selected < squadPage.rows.length
                                    onClicked: editorRoot.openPlayerById(squadPage.rows[squadPage.selected].id)
                                }
                            }
                            ListView {
                                id: squadList
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: squadPage.rows
                                focus: true
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                Keys.onDeletePressed: if (squadPage.selected >= 0) db.removeRosterPlayer(editorRoot.teamIndex, squadPage.selected)
                                Keys.onPressed: (event) => {
                                    if (event.key === Qt.Key_Insert && squadPage.rows.length < 64) {
                                        playerPicker.pick("Add a player to the squad", -1, (id) => db.addRosterPlayer(editorRoot.teamIndex, id))
                                        event.accepted = true
                                    }
                                }
                                delegate: PlayerRow {
                                    required property var modelData
                                    required property int index
                                    width: squadList.width
                                    rowIndex: index
                                    leading: (index + 1) + "."
                                    row: modelData
                                    selected: squadPage.selected === index
                                    onClicked: { squadPage.selected = index; squadList.forceActiveFocus() }
                                    onDoubleClicked: editorRoot.openPlayerById(modelData.id)
                                }
                            }
                        }

                        // ---- Lineups (every match list the database has: NRL 2026, League, Nines, Union, ...)
                        ColumnLayout {
                            id: lineupPage
                            spacing: 0
                            property var lists: db.loaded ? db.matchLists() : []
                            readonly property var current: lists.length > 0 ? lists[Math.min(lineupKind.currentIndex, lists.length - 1)] : null
                            readonly property string list: current ? current.list : ""
                            property var rows: editorRoot.mode === "team" && list !== "" && db.revision >= 0 ? db.lineup(editorRoot.teamIndex, list) : []
                            property var roles: editorRoot.mode === "team" && list !== "" && db.revision >= 0 ? db.teamRoles(editorRoot.teamIndex, list) : []
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.margins: 10
                                spacing: 10
                                PanelLabel { text: "Lineup" }
                                ComboBox {
                                    id: lineupKind
                                    Layout.preferredWidth: 200
                                    Layout.preferredHeight: 36
                                    model: lineupPage.lists.map((l) => l.title)
                                }
                                CheckEdit {
                                    text: "The team has this lineup"
                                    value: editorRoot.tv(lineupPage.list + "!") === true
                                    onCommit: (on) => editorRoot.presentT(lineupPage.list, on)
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: lineupPage.list === "NRL2026Match" ? "Click a row to pick the player. NRL 2026 changes are copied to the League lineup."
                                                                              : "Click a row to pick the player."
                                    color: BtDarkStyle.textDim
                                    font.pixelSize: 11
                                    font.italic: true
                                    wrapMode: Text.WordWrap
                                    Layout.maximumWidth: 330
                                }
                            }
                            ScrollView {
                                id: lineupScroll
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                contentWidth: availableWidth
                                ColumnLayout {
                                    width: lineupScroll.availableWidth
                                    spacing: 0
                                    Repeater {
                                        model: lineupPage.rows
                                        RowLayout {
                                            id: lineupRow
                                            required property var modelData
                                            required property int index
                                            Layout.fillWidth: true
                                            spacing: 0
                                            PlayerRow {
                                                Layout.fillWidth: true
                                                rowIndex: lineupRow.index
                                                leading: lineupRow.modelData.position
                                                row: lineupRow.modelData
                                                onClicked: playerPicker.pick(lineupRow.modelData.position, editorRoot.teamIndex,
                                                                             (id) => db.setLineupPlayer(editorRoot.teamIndex, lineupPage.list, lineupRow.modelData.key, id))
                                                onDoubleClicked: if (lineupRow.modelData.known) editorRoot.openPlayerById(lineupRow.modelData.id)
                                            }
                                            SpinBox {
                                                visible: lineupPage.current !== null && lineupPage.current.shirts
                                                Layout.preferredWidth: 110
                                                Layout.preferredHeight: 30
                                                from: 0
                                                to: 99
                                                editable: true
                                                enabled: lineupRow.modelData.present
                                                value: lineupRow.modelData.shirt
                                                font.pixelSize: 12
                                                onValueModified: db.setLineupShirt(editorRoot.teamIndex, lineupPage.list, lineupRow.modelData.key, value)
                                                BtToolTip { text: "Shirt number" }
                                            }
                                        }
                                    }
                                    PanelLabel { text: "Team Roles"; Layout.margins: 10; visible: lineupPage.roles.length > 0 }
                                    Repeater {
                                        model: lineupPage.roles
                                        PlayerRow {
                                            required property var modelData
                                            required property int index
                                            Layout.fillWidth: true
                                            rowIndex: index
                                            leading: modelData.name
                                            row: modelData
                                            onClicked: playerPicker.pick(modelData.name, editorRoot.teamIndex,
                                                                         (id) => db.setLineupPlayer(editorRoot.teamIndex, lineupPage.list, modelData.key, id))
                                        }
                                    }
                                }
                            }
                        }

                        // ---- Jerseys (the team's uniforms)
                        ColumnLayout {
                            id: kitPage
                            spacing: 0
                            property var info: ({})
                            property int selected: 0
                            property var fields: []
                            property string group: ""
                            readonly property var groups: {
                                const out = []
                                for (const f of fields)
                                    if (out.indexOf(f.group) < 0)
                                        out.push(f.group)
                                return out
                            }
                            function refresh() {
                                if (editorRoot.teamIndex < 0) {
                                    info = ({})
                                    fields = []
                                    return
                                }
                                info = db.jerseys(editorRoot.teamIndex)
                                if (!info.ok) {
                                    fields = []
                                    return
                                }
                                selected = Math.max(0, Math.min(selected, info.count - 1))
                                fields = db.jerseyFields(editorRoot.teamIndex, selected)
                                if (groups.indexOf(group) < 0)
                                    group = groups[0] || ""   // "Uniform": name, hidden, Nines.
                            }
                            Connections {
                                target: editorRoot
                                function onTeamIndexChanged() { kitPage.selected = 0; kitPage.refresh() }
                            }
                            Connections {
                                target: db
                                function onLoadedChanged() { kitPage.refresh() }
                            }
                            Component.onCompleted: refresh()

                            Text {
                                visible: kitPage.info.ok !== true
                                text: kitPage.info.error || ""
                                color: BtDarkStyle.textDim
                                font.pixelSize: 13
                                Layout.margins: 16
                            }
                            RowLayout {
                                visible: kitPage.info.ok === true
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 0

                                // Uniform list + which uniform the game uses where (scrolls on small screens).
                                ScrollView {
                                    id: kitSide
                                    Layout.preferredWidth: 280
                                    Layout.fillHeight: true
                                    Layout.minimumHeight: 0
                                    clip: true
                                    contentWidth: availableWidth
                                    contentHeight: kitSideColumn.implicitHeight
                                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                ColumnLayout {
                                    id: kitSideColumn
                                    width: kitSide.availableWidth
                                    spacing: 6
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.margins: 10
                                        PanelLabel { text: "Jerseys  ·  " + (kitPage.info.count || 0) + " / " + (kitPage.info.max || 0) }
                                    }
                                    ListView {
                                        id: kitList
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: Math.min(contentHeight, 320)
                                        clip: true
                                        model: kitPage.info.uniforms || []
                                        delegate: Rectangle {
                                            id: kitRow
                                            required property var modelData
                                            required property int index
                                            width: kitList.width
                                            height: 34
                                            color: kitPage.selected === index ? BtDarkStyle.bgMedium
                                                                              : kitMouse.containsMouse ? BtDarkStyle.bgHover
                                                                                                       : (index % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
                                            Rectangle { visible: kitPage.selected === kitRow.index; width: 3; height: parent.height; color: BtDarkStyle.accentColor }
                                            MouseArea {
                                                id: kitMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                onClicked: { kitPage.selected = kitRow.index; kitPage.refresh() }
                                            }
                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.leftMargin: 12
                                                anchors.rightMargin: 10
                                                Text { text: (kitRow.index + 1) + "."; color: BtDarkStyle.textDim; font.pixelSize: 12; Layout.preferredWidth: 22 }
                                                Text {
                                                    text: kitRow.modelData.name !== "" ? kitRow.modelData.name : "(no name)"
                                                    color: BtDarkStyle.textMain
                                                    font.pixelSize: 13
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideRight
                                                }
                                                Text {
                                                    text: (kitRow.modelData.nines ? "Nines " : "") + (kitRow.modelData.hidden ? "Hidden" : "")
                                                    color: BtDarkStyle.textDim
                                                    font.pixelSize: 10
                                                }
                                            }
                                        }
                                    }
                                    Flow {
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 10
                                        Layout.rightMargin: 10
                                        spacing: 6
                                        SmallButton {
                                            text: "Add Copy"
                                            tip: "Add a new jersey, a copy of the selected one"
                                            accent: true
                                            enabled: (kitPage.info.count || 0) < (kitPage.info.max || 0)
                                            onClicked: {
                                                const added = db.addJersey(editorRoot.teamIndex, kitPage.selected)
                                                if (added >= 0)
                                                    kitPage.selected = added
                                                kitPage.refresh()
                                            }
                                        }
                                        SmallButton {
                                            text: "Remove"
                                            tip: "Remove the selected jersey"
                                            enabled: (kitPage.info.count || 0) > 1
                                            onClicked: {
                                                db.removeJersey(editorRoot.teamIndex, kitPage.selected)
                                                kitPage.refresh()
                                            }
                                        }
                                        SmallButton {
                                            text: "Export..."
                                            tip: "Save this team's jerseys to a file, to import them into another team"
                                            onClicked: kitDialog.start("export")
                                        }
                                        SmallButton {
                                            text: "Import..."
                                            tip: "Replace this team's jerseys with ones exported from another team (same game)"
                                            onClicked: kitDialog.start("import")
                                        }
                                    }
                                    PanelLabel { text: "Used For"; Layout.leftMargin: 10; Layout.topMargin: 10; visible: (kitPage.info.slots || []).length > 0 }
                                    Repeater {
                                        model: kitPage.info.slots || []
                                        ComboEdit {
                                            required property var modelData
                                            Layout.leftMargin: 10
                                            Layout.rightMargin: 10
                                            label: modelData.label
                                            items: (kitPage.info.uniforms || []).map((u) => (u.index + 1) + ". " + u.name)
                                            index: modelData.uniform
                                            unknownText: "Jersey " + (modelData.uniform + 1)
                                            onCommit: (i) => { db.setJerseySlot(editorRoot.teamIndex, modelData.slot, i); kitPage.refresh() }
                                        }
                                    }
                                    Item { Layout.preferredHeight: 10 }
                                }
                                }
                                Rectangle { width: 1; Layout.fillHeight: true; color: BtDarkStyle.borderDark }

                                // The selected jersey's fields.
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: 0
                                    Flow {
                                        Layout.fillWidth: true
                                        Layout.margins: 8
                                        spacing: 4
                                        Repeater {
                                            model: kitPage.groups
                                            SmallButton {
                                                required property var modelData
                                                text: modelData
                                                accent: kitPage.group === modelData
                                                onClicked: kitPage.group = modelData
                                            }
                                        }
                                    }
                                    Page_ {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        FieldGrid {
                                            items: kitPage.fields
                                            group: kitPage.group
                                            onCommit: (key, value) => {
                                                db.setJerseyField(editorRoot.teamIndex, kitPage.selected, key, value)
                                                if (key.startsWith("t:"))
                                                    kitPage.info = db.jerseys(editorRoot.teamIndex)   // The name in the list.
                                                else if (kitPage.fields.some((f) => f.key === key && f.refresh))
                                                    kitPage.refresh()   // RLL3: a new manufacturer has its own licensed jerseys.
                                            }
                                        }
                                        Text {
                                            text: "Names the games' files didn't give away are shown as \"Unknown 0x...\"."
                                            color: BtDarkStyle.textDim
                                            font.pixelSize: 11
                                            font.italic: true
                                        }
                                    }
                                }
                            }
                        }

                        // ---- Feeder clubs
                        Page_ {
                            Repeater {
                                model: [{ kind: "feeder", title: "Feeder Clubs", tip: "Clubs this team takes players from" },
                                        { kind: "fedFrom", title: "Fed From Clubs", tip: "Clubs this team supplies players to" }]
                                Card {
                                    id: linkCard
                                    required property var modelData
                                    property int selected: -1
                                    property var rows: editorRoot.mode === "team" && db.revision >= 0 ? db.clubLinks(editorRoot.teamIndex, modelData.kind) : []
                                    title: modelData.title + "  ·  " + rows.length + " / 7"
                                    RowLayout {
                                        spacing: 8
                                        SmallButton {
                                            text: "Add Team"
                                            tip: linkCard.modelData.tip
                                            accent: true
                                            enabled: linkCard.rows.length < 7
                                            onClicked: teamPicker.pick(linkCard.modelData.title, (id) => db.addClubLink(editorRoot.teamIndex, linkCard.modelData.kind, id))
                                        }
                                        SmallButton {
                                            text: "Replace"
                                            enabled: linkCard.selected >= 0 && linkCard.selected < linkCard.rows.length
                                            onClicked: {
                                                const slot = linkCard.selected
                                                teamPicker.pick(linkCard.modelData.title, (id) => db.setClubLink(editorRoot.teamIndex, linkCard.modelData.kind, slot, id))
                                            }
                                        }
                                        SmallButton {
                                            text: "Remove"
                                            enabled: linkCard.selected >= 0 && linkCard.selected < linkCard.rows.length
                                            onClicked: {
                                                db.removeClubLink(editorRoot.teamIndex, linkCard.modelData.kind, linkCard.selected)
                                                linkCard.selected = -1
                                            }
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                    Repeater {
                                        model: linkCard.rows
                                        Rectangle {
                                            id: linkRow
                                            required property var modelData
                                            required property int index
                                            Layout.fillWidth: true
                                            implicitHeight: 34
                                            color: linkCard.selected === index ? BtDarkStyle.bgMedium
                                                                               : linkMouse.containsMouse ? BtDarkStyle.bgHover : BtDarkStyle.bgDarkest
                                            radius: 3
                                            MouseArea {
                                                id: linkMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                onClicked: linkCard.selected = linkRow.index
                                                onDoubleClicked: {
                                                    const t = db.teamIndexById(linkRow.modelData.id)
                                                    if (t >= 0) editorRoot.openTeam(t)
                                                }
                                            }
                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.leftMargin: 10
                                                anchors.rightMargin: 10
                                                spacing: 10
                                                Image {
                                                    visible: source.toString() !== ""
                                                    source: linkRow.modelData.logoUrl
                                                    Layout.preferredWidth: 26
                                                    Layout.preferredHeight: 26
                                                    sourceSize: Qt.size(52, 52)
                                                    fillMode: Image.PreserveAspectFit
                                                }
                                                Text { text: linkRow.modelData.id; color: BtDarkStyle.textDim; font.family: "Consolas"; font.pixelSize: 12; Layout.preferredWidth: 40 }
                                                Text {
                                                    text: linkRow.modelData.fullName
                                                    color: linkRow.modelData.known ? BtDarkStyle.textMain : BtDarkStyle.textDim
                                                    font.pixelSize: 13
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }
                                    }
                                    Text {
                                        visible: linkCard.rows.length === 0
                                        text: "None"
                                        color: BtDarkStyle.textDim
                                        font.italic: true
                                        font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }
                }

                // ---------------- Player editor ----------------
                ColumnLayout {
                    spacing: 0

                    // Player banner
                    Rectangle {
                        id: playerBanner
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
                        readonly property string clubName: editorRoot.pv("_clubName")
                        color: BtDarkStyle.bgDark
                        border.color: BtDarkStyle.borderDark
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 16
                            Rectangle {
                                Layout.preferredWidth: 64
                                Layout.preferredHeight: 64
                                radius: 32
                                color: BtDarkStyle.bgMedium
                                border.color: BtDarkStyle.accentColor
                                Text {
                                    anchors.centerIn: parent
                                    text: editorRoot.isLeague ? (db.list("roleShort")[editorRoot.pn("preferredRolePrimary")] || "?")
                                                              : String(editorRoot.pn("preferredRolePrimary"))
                                    color: BtDarkStyle.accentColor
                                    font.pixelSize: 20
                                    font.bold: true
                                }
                            }
                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true
                                Text {
                                    text: (editorRoot.pv("firstname") + " " + editorRoot.pv("surname")).trim()
                                    color: BtDarkStyle.white
                                    font.pixelSize: 22
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: "Player " + editorRoot.pv("_index") + "  ·  ID " + editorRoot.pv("id") + "  ·  age " + editorRoot.pv("_age")
                                          + (editorRoot.isLeague ? "  ·  " + (db.list("roles")[editorRoot.pn("preferredRolePrimary")] || "") : "")
                                          + (playerBanner.clubName !== "" ? "  ·  " + playerBanner.clubName : "")
                                          + (editorRoot.pn("sex") === 1 ? "  ·  Female" : "")
                                    color: BtDarkStyle.textDim
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                            ColumnLayout {
                                spacing: 0
                                Text { text: "OVR"; color: BtDarkStyle.textDim; font.pixelSize: 11; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                                Text {
                                    text: editorRoot.pn("_rating")
                                    color: editorRoot.ratingColor(editorRoot.pn("_rating"))
                                    font.pixelSize: 30
                                    font.bold: true
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }
                    }

                    EditorTabs {
                        id: playerTabs
                        EditorTab { text: "Identity" }
                        EditorTab { text: "Ratings" }
                        EditorTab { text: "Career Stats" }
                        EditorTab { text: "Appearance" }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        // A tab's minimum height (its fixed size rows, e.g. the Jerseys list) mustn't make the panel
                        // taller than the window on small screens: that pushed the bottom of every tab (the team
                        // colours) under the status bar. The tabs scroll instead.
                        Layout.minimumHeight: 0
                        clip: true
                        currentIndex: playerTabs.currentIndex

                        // ---- Identity
                        Page_ {
                            GridLayout {
                                Layout.fillWidth: true
                                columns: width > 980 ? 2 : 1
                                columnSpacing: 12
                                rowSpacing: 12

                                Card {
                                    title: "Name"
                                    Layout.alignment: Qt.AlignTop
                                    TextFieldEdit { visible: editorRoot.hasP("firstname"); label: "First Name"; value: editorRoot.pv("firstname"); onCommit: (t) => editorRoot.setP("firstname", t) }
                                    TextFieldEdit { visible: editorRoot.hasP("surname"); label: "Last Name"; value: editorRoot.pv("surname"); onCommit: (t) => editorRoot.setP("surname", t) }
                                    TextFieldEdit { visible: editorRoot.hasP("shirtName"); label: "Jersey Name"; value: editorRoot.pv("shirtName"); maxLength: 20; onCommit: (t) => editorRoot.setP("shirtName", t) }
                                    ComboEdit {
                                        visible: editorRoot.hasP("commentaryNameHash") && items.length > 0
                                        label: "Commentary Name"
                                        items: db.commentaryNames("surnames")
                                        index: db.commentaryIndex("surnames", editorRoot.pn("commentaryNameHash"))
                                        unknownText: "0x" + editorRoot.pn("commentaryNameHash").toString(16).toUpperCase()
                                        onCommit: (i) => editorRoot.setP("commentaryNameHash", db.commentaryHash("surnames", i))
                                    }
                                    // RLL3: numbers (0 = the game picks it); the name shown is that entry of the sound M3MP's list.
                                    Repeater {
                                        model: [["commentarySurname", "Commentary Surname"], ["commentaryFullName", "Commentary Full Name"]]
                                        RowLayout {
                                            id: commentaryRow
                                            required property var modelData
                                            visible: editorRoot.hasP(modelData[0])
                                            Layout.fillWidth: true
                                            NumberEdit {
                                                Layout.fillWidth: false
                                                label: commentaryRow.modelData[1]
                                                value: editorRoot.pn(commentaryRow.modelData[0])
                                                to: 65535
                                                onCommit: (v) => editorRoot.setP(commentaryRow.modelData[0], v)
                                            }
                                            Text {
                                                text: db.commentaryNames("surnames")[editorRoot.pn(commentaryRow.modelData[0])] || ""
                                                color: BtDarkStyle.textDim
                                                font.pixelSize: 12
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                    BigNumberEdit { visible: editorRoot.hasP("commentaryNameHash") && db.commentaryNames("surnames").length === 0; label: "Commentary Hash"; value: editorRoot.pn("commentaryNameHash"); onCommit: (v) => editorRoot.setP("commentaryNameHash", v) }
                                    NumberEdit { visible: editorRoot.hasP("id"); label: "Player ID"; value: editorRoot.pn("id"); to: 999999; onCommit: (v) => editorRoot.setP("id", v) }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 16
                                        CheckEdit { visible: editorRoot.hasP("licensed"); text: "Licensed"; value: editorRoot.pv("licensed") === true; onCommit: (on) => editorRoot.setP("licensed", on) }
                                        CheckEdit { visible: editorRoot.hasP("hidden"); text: "Hidden"; value: editorRoot.pv("hidden") === true; onCommit: (on) => editorRoot.setP("hidden", on) }
                                        CheckEdit { visible: editorRoot.hasP("isWorldCup"); text: "World Cup"; value: editorRoot.pv("isWorldCup") === true; onCommit: (on) => editorRoot.setP("isWorldCup", on) }
                                    }
                                }

                                Card {
                                    title: "Club & Positions"
                                    Layout.alignment: Qt.AlignTop
                                    ComboEdit { visible: editorRoot.hasP("club"); label: "Club"; items: db.revision >= 0 ? db.teamNames() : []; index: editorRoot.pn("club"); onCommit: (i) => editorRoot.setP("club", i) }
                                    Repeater {
                                        model: [["preferredRolePrimary", "Primary Role"], ["preferredRoleSecondary", "Secondary Role"], ["preferredRoleTertiary", "Tertiary Role"]]
                                        ColumnLayout {
                                            id: roleBox
                                            required property var modelData
                                            Layout.fillWidth: true
                                            visible: editorRoot.hasP(modelData[0])
                                            ComboEdit { visible: editorRoot.isLeague; label: roleBox.modelData[1]; items: db.list("roles"); index: editorRoot.pn(roleBox.modelData[0]); onCommit: (i) => editorRoot.setP(roleBox.modelData[0], i) }
                                            NumberEdit { visible: !editorRoot.isLeague; label: roleBox.modelData[1]; value: editorRoot.pn(roleBox.modelData[0]); to: 99; onCommit: (v) => editorRoot.setP(roleBox.modelData[0], v) }
                                        }
                                    }
                                    RowLayout {
                                        visible: editorRoot.hasP("shirtNumber")
                                        spacing: 10
                                        NumberEdit { label: "Jersey Number"; value: editorRoot.pn("shirtNumber"); from: 0; to: 99; onCommit: (v) => editorRoot.setP("shirtNumber", v) }
                                        CheckEdit {
                                            text: "Use it"
                                            value: editorRoot.pv("shirtNumber?") === true
                                            onCommit: (on) => editorRoot.presentP("shirtNumber", on)
                                        }
                                    }
                                    ComboEdit {
                                        visible: editorRoot.hasP("contractExpiry") && editorRoot.isRl26
                                        label: "Contract Expiry"
                                        items: db.list("contract")
                                        index: editorRoot.pn("contractExpiry") >= 2025 ? editorRoot.pn("contractExpiry") - 2024 : 0
                                        unknownText: String(editorRoot.pn("contractExpiry"))
                                        onCommit: (i) => editorRoot.setP("contractExpiry", i === 0 ? 0 : 2024 + i)
                                    }
                                    NumberEdit { visible: editorRoot.hasP("contractExpiry") && !editorRoot.isRl26; label: "Contract Expiry"; value: editorRoot.pn("contractExpiry"); to: 2100; onCommit: (v) => editorRoot.setP("contractExpiry", v) }
                                }

                                Card {
                                    title: "Personal"
                                    Layout.alignment: Qt.AlignTop
                                    ComboEdit { visible: editorRoot.hasP("sex"); label: "Gender"; items: db.list("genders"); index: editorRoot.pn("sex"); onCommit: (i) => editorRoot.setP("sex", i) }
                                    RowLayout {
                                        visible: editorRoot.hasP("birthYear")
                                        spacing: 6
                                        FieldLabel { text: "Date of Birth" }
                                        SpinBox {
                                            Layout.preferredWidth: 110; Layout.preferredHeight: 36; from: 1; to: 31; editable: true
                                            value: editorRoot.pn("birthDay")
                                            onValueModified: editorRoot.setP("birthDay", value)
                                            BtToolTip { text: "Day" }
                                        }
                                        SpinBox {
                                            Layout.preferredWidth: 110; Layout.preferredHeight: 36; from: 1; to: 12; editable: true
                                            value: editorRoot.pn("birthMonth")
                                            onValueModified: editorRoot.setP("birthMonth", value)
                                            BtToolTip { text: "Month" }
                                        }
                                        SpinBox {
                                            Layout.preferredWidth: 130; Layout.preferredHeight: 36; from: 1940; to: 2020; editable: true
                                            value: editorRoot.pn("birthYear")
                                            textFromValue: (v) => String(v)
                                            valueFromText: (t) => parseInt(t)
                                            onValueModified: editorRoot.setP("birthYear", value)
                                            BtToolTip { text: "Year" }
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                    NumberEdit { visible: editorRoot.hasP("age"); label: "Age (stored)"; value: editorRoot.pn("age"); to: 99; onCommit: (v) => editorRoot.setP("age", v) }
                                    NumberEdit { visible: editorRoot.hasP("height"); label: "Height"; value: editorRoot.pn("height"); from: 100; to: 230; suffix: "cm"; onCommit: (v) => editorRoot.setP("height", v) }
                                    NumberEdit { visible: editorRoot.hasP("weight"); label: "Weight"; value: editorRoot.pn("weight"); from: 40; to: 180; suffix: "kg"; onCommit: (v) => editorRoot.setP("weight", v) }
                                    ComboEdit { visible: editorRoot.hasP("preferredHand"); label: "Preferred Hand"; items: db.list("hands"); index: editorRoot.pn("preferredHand"); onCommit: (i) => editorRoot.setP("preferredHand", i) }
                                    ComboEdit { visible: editorRoot.hasP("preferredFoot"); label: "Preferred Foot"; items: db.list("hands"); index: editorRoot.pn("preferredFoot"); onCommit: (i) => editorRoot.setP("preferredFoot", i) }
                                    NumberEdit { visible: editorRoot.hasP("weakFootRating"); label: "Weak Foot Rating"; value: editorRoot.pn("weakFootRating"); to: 255; onCommit: (v) => editorRoot.setP("weakFootRating", v) }
                                    ComboEdit { visible: editorRoot.hasP("rightHanded"); label: "Preferred Hand"; items: db.list("hands"); index: editorRoot.pv("rightHanded") === true ? 1 : 0; onCommit: (i) => editorRoot.setP("rightHanded", i === 1) }
                                    ComboEdit { visible: editorRoot.hasP("rightFooted"); label: "Preferred Foot"; items: db.list("hands"); index: editorRoot.pv("rightFooted") === true ? 1 : 0; onCommit: (i) => editorRoot.setP("rightFooted", i === 1) }
                                }

                                Card {
                                    title: "Representative"
                                    Layout.alignment: Qt.AlignTop
                                    ComboEdit { visible: editorRoot.hasP("countryOfBirth") && (editorRoot.isRl26 || editorRoot.isRll4); label: "Country of Birth"; items: editorRoot.gameList("countryOfBirth"); index: editorRoot.pn("countryOfBirth"); unknownText: String(editorRoot.pn("countryOfBirth")); onCommit: (i) => editorRoot.setP("countryOfBirth", i) }
                                    ChoiceEdit { visible: editorRoot.hasP("countryOfBirth") && editorRoot.isRll3; label: "Country of Birth"; choices: editorRoot.gameChoices("countries"); value: editorRoot.pn("countryOfBirth"); onPick: (v) => editorRoot.setP("countryOfBirth", v) }
                                    NumberEdit { visible: editorRoot.hasP("countryOfBirth") && !editorRoot.isRl26 && !editorRoot.isRll3 && !editorRoot.isRll4; label: "Country of Birth"; value: editorRoot.pn("countryOfBirth"); to: 999; onCommit: (v) => editorRoot.setP("countryOfBirth", v) }
                                    ComboEdit { visible: editorRoot.hasP("representativeCountry") && (editorRoot.isRl26 || editorRoot.isRll4); label: "Representative Country"; items: editorRoot.gameList("repCountry"); index: editorRoot.pn("representativeCountry"); unknownText: String(editorRoot.pn("representativeCountry")); onCommit: (i) => editorRoot.setP("representativeCountry", i) }
                                    ChoiceEdit { visible: editorRoot.hasP("representativeCountry") && editorRoot.isRll3; label: "Representative Country"; choices: editorRoot.gameChoices("countries"); value: editorRoot.pn("representativeCountry"); onPick: (v) => editorRoot.setP("representativeCountry", v) }
                                    NumberEdit { visible: editorRoot.hasP("representativeCountry") && !editorRoot.isRl26 && !editorRoot.isRll3 && !editorRoot.isRll4; label: "Representative Country"; value: editorRoot.pn("representativeCountry"); to: 999; onCommit: (v) => editorRoot.setP("representativeCountry", v) }
                                    ComboEdit {
                                        visible: editorRoot.hasP("stateOfOrigin") && editorRoot.isRl26
                                        label: "State of Origin"
                                        items: db.list("soo")
                                        index: editorRoot.pn("stateOfOrigin") === 197 ? 1 : editorRoot.pn("stateOfOrigin") === 198 ? 2 : 0
                                        onCommit: (i) => editorRoot.setP("stateOfOrigin", 196 + i)
                                    }
                                    NumberEdit { visible: editorRoot.hasP("stateOfOrigin") && !editorRoot.isRl26; label: "State of Origin"; value: editorRoot.pn("stateOfOrigin"); to: 999; onCommit: (v) => editorRoot.setP("stateOfOrigin", v) }
                                    NumberEdit { visible: editorRoot.hasP("originRepNumber"); label: "Origin Rep Number"; value: editorRoot.pn("originRepNumber"); to: 50000; onCommit: (v) => editorRoot.setP("originRepNumber", v) }
                                    NumberEdit { visible: editorRoot.hasP("originOtherNumber"); label: "Origin Other Number"; value: editorRoot.pn("originOtherNumber"); to: 50000; onCommit: (v) => editorRoot.setP("originOtherNumber", v) }
                                    ComboEdit {
                                        visible: editorRoot.hasP("cityVsCountry") && editorRoot.isRl26
                                        label: "City v Country"
                                        items: db.list("cvc")
                                        index: editorRoot.pn("cityVsCountry") === 200 ? 1 : editorRoot.pn("cityVsCountry") === 201 ? 2 : 0
                                        onCommit: (i) => editorRoot.setP("cityVsCountry", 199 + i)
                                    }
                                    NumberEdit { visible: editorRoot.hasP("cityVsCountry") && !editorRoot.isRl26; label: "City v Country"; value: editorRoot.pn("cityVsCountry"); to: 999; onCommit: (v) => editorRoot.setP("cityVsCountry", v) }
                                    ComboEdit {
                                        visible: editorRoot.hasP("allStars") && editorRoot.isRl26
                                        label: "All Stars"
                                        items: db.list("allStars")
                                        index: editorRoot.pn("allStars") === 203 ? 1 : editorRoot.pn("allStars") === 204 ? 2 : 0
                                        onCommit: (i) => editorRoot.setP("allStars", 202 + i)
                                    }
                                    NumberEdit { visible: editorRoot.hasP("allStars") && !editorRoot.isRl26; label: "All Stars"; value: editorRoot.pn("allStars"); to: 999; onCommit: (v) => editorRoot.setP("allStars", v) }
                                }

                                Card {
                                    title: "Other"
                                    Layout.alignment: Qt.AlignTop
                                    visible: otherRepeater.count > 0
                                    Repeater {
                                        id: otherRepeater
                                        model: ["head", "hairTop", "movementProfile", "temperament", "conversionStyle", "photogrammetryStatus"]
                                                   .filter((k) => editorRoot.hasP(k))
                                        NumberEdit {
                                            required property var modelData
                                            label: db.prettyLabel(modelData)
                                            value: editorRoot.pn(modelData)
                                            to: 99999
                                            onCommit: (v) => editorRoot.setP(modelData, v)
                                        }
                                    }
                                }
                            }
                        }

                        // ---- Ratings (the database's own list: RL26 / RLL4 / Rugby 25 differ)
                        Page_ {
                            id: ratingsPage
                            readonly property var fields: db.loaded ? db.playerSkillFields() : []
                            readonly property var groups: {
                                const out = []
                                for (const f of fields)
                                    if (out.indexOf(f.group) < 0)
                                        out.push(f.group)
                                return out
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: width > 980 ? 2 : 1
                                columnSpacing: 12
                                rowSpacing: 12
                                Repeater {
                                    model: ratingsPage.groups
                                    Card {
                                        id: ratingCard
                                        required property var modelData
                                        title: modelData
                                        Layout.alignment: Qt.AlignTop
                                        Repeater {
                                            model: ratingsPage.fields.filter((f) => f.group === ratingCard.modelData)
                                            StatSlider {
                                                required property var modelData
                                                label: modelData.label
                                                to: 100
                                                value: editorRoot.pn(modelData.key)
                                                onCommit: (v) => editorRoot.setP(modelData.key, v)
                                            }
                                        }
                                    }
                                }
                            }
                            Card {
                                title: "Career Perk"
                                visible: editorRoot.hasP("perk")
                                ComboEdit { visible: editorRoot.isRl26; label: "Perk"; items: db.list("perks"); index: editorRoot.pn("perk"); onCommit: (i) => editorRoot.setP("perk", i) }
                                NumberEdit { visible: !editorRoot.isRl26; label: "Perk"; value: editorRoot.pn("perk"); to: 99; onCommit: (v) => editorRoot.setP("perk", v) }
                            }
                            Card {
                                title: "Randomize Stats"
                                visible: db.canRandomize()
                                Text {
                                    text: "New main stats and skills for a " + (db.list("roles")[editorRoot.pn("preferredRolePrimary")] || "player") + ", like the game's editor."
                                    color: BtDarkStyle.textDim
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                                RowLayout {
                                    spacing: 10
                                    ComboBox {
                                        id: randomLevel
                                        Layout.preferredWidth: 200
                                        Layout.preferredHeight: 36
                                        model: db.list("randomLevels")
                                        currentIndex: editorRoot.pn("_rating") >= 83 ? 0 : editorRoot.pn("_rating") >= 70 ? 1 : 2
                                    }
                                    SmallButton {
                                        text: "Randomize"
                                        accent: true
                                        tip: "Roll new stats (Reload throws away all unsaved edits)"
                                        onClicked: db.randomizeStats(editorRoot.playerIndex, randomLevel.currentIndex)
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                            }
                        }

                        // ---- Career stats
                        Page_ {
                            id: careerPage
                            readonly property var careerFields: db.loaded ? db.playerCareerFields() : []
                            readonly property var statLists: db.loaded ? db.playerStatLists() : []
                            Card {
                                title: "Career"
                                visible: careerPage.careerFields.length > 0
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: Math.max(1, Math.floor(width / 330))
                                    columnSpacing: 16
                                    rowSpacing: 4
                                    Repeater {
                                        model: careerPage.careerFields
                                        NumberEdit {
                                            required property var modelData
                                            label: modelData.label
                                            to: 50000
                                            value: editorRoot.pn(modelData.key)
                                            onCommit: (v) => editorRoot.setP(modelData.key, v)
                                        }
                                    }
                                }
                            }
                            Repeater {
                                model: careerPage.statLists
                                Card {
                                    id: matchCard
                                    required property var modelData
                                    readonly property bool hasStats: editorRoot.pv(modelData.list + "!") === true
                                    title: modelData.title
                                    CheckEdit {
                                        text: "Has " + matchCard.modelData.title.toLowerCase()
                                        value: matchCard.hasStats
                                        onCommit: (on) => editorRoot.presentP(matchCard.modelData.list, on)
                                    }
                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: Math.max(1, Math.floor(width / 330))
                                        columnSpacing: 16
                                        rowSpacing: 4
                                        enabled: matchCard.hasStats
                                        opacity: matchCard.hasStats ? 1.0 : 0.4
                                        Repeater {
                                            model: matchCard.modelData.fields
                                            NumberEdit {
                                                required property var modelData
                                                label: modelData.label
                                                to: 50000
                                                value: editorRoot.pn(modelData.key)
                                                onCommit: (v) => editorRoot.setP(modelData.key, v)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // ---- Appearance (the player's look)
                        ColumnLayout {
                            id: lookPage
                            spacing: 0
                            property var fields: []
                            property string group: ""
                            readonly property var groups: {
                                const out = []
                                for (const f of fields)
                                    if (out.indexOf(f.group) < 0)
                                        out.push(f.group)
                                return out
                            }
                            function refresh() {
                                fields = editorRoot.playerIndex >= 0 ? db.appearanceFields(editorRoot.playerIndex) : []
                                if (groups.indexOf(group) < 0)
                                    group = groups[0] || ""
                            }
                            Connections {
                                target: editorRoot
                                function onPlayerIndexChanged() { lookPage.refresh() }
                            }
                            Connections {
                                target: db
                                function onLoadedChanged() { lookPage.refresh() }
                            }
                            Component.onCompleted: refresh()
                            Text {
                                visible: lookPage.fields.length === 0
                                text: "This player has no appearance data."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 13
                                Layout.margins: 16
                            }
                            Flow {
                                Layout.fillWidth: true
                                Layout.margins: 8
                                spacing: 4
                                Repeater {
                                    model: lookPage.groups
                                    SmallButton {
                                        required property var modelData
                                        text: modelData
                                        accent: lookPage.group === modelData
                                        onClicked: lookPage.group = modelData
                                    }
                                }
                            }
                            Page_ {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                FieldGrid {
                                    items: lookPage.fields
                                    group: lookPage.group
                                    onCommit: (key, value) => db.setAppearanceField(editorRoot.playerIndex, key, value)
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
                    readonly property bool ok: editorRoot.localStatus !== "" ? editorRoot.localStatusOk : db.statusOk
                    color: ok ? BtDarkStyle.textDim : editorRoot.errorColor
                    text: "Status: " + (editorRoot.localStatus !== "" ? editorRoot.localStatus
                                        : db.statusText !== "" ? db.statusText : "Ready")
                }
                Text {
                    text: db.unsavedChanges ? "Unsaved changes" : ""
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
    // Pickers
    // ------------------------------------------------------------------------------------------------
    component PickerPopup: Popup {
        id: picker
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(640, parent ? parent.width - 80 : 640)
        height: Math.min(620, parent ? parent.height - 80 : 620)
        padding: 14
        background: Rectangle {
            color: BtDarkStyle.bgDark
            border.color: BtDarkStyle.accentColor
            border.width: 1
            radius: 6
        }
    }

    PickerPopup {
        id: playerPicker
        property string title: ""
        property int team: -1          // >= 0: start with that team's squad.
        property var callback: null
        property var rows: []
        function pick(heading, teamIndex, onPicked) {
            title = heading
            team = teamIndex
            callback = onPicked
            pickerSquad.checked = teamIndex >= 0
            pickerSearch.text = ""
            refresh()
            open()
            pickerSearch.forceActiveFocus()
        }
        function refresh() {
            rows = db.playerRows(pickerSearch.text, -1, pickerSquad.checked ? team : -1, -1)
        }
        function choose(id) {
            close()
            if (callback)
                callback(id)
            callback = null
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text { text: playerPicker.title; color: BtDarkStyle.accentColor; font.bold: true; font.pixelSize: 15 }
            RowLayout {
                spacing: 8
                TextField {
                    id: pickerSearch
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    placeholderText: "Search players (name or id)..."
                    color: BtDarkStyle.textMain
                    font.pixelSize: 13
                    leftPadding: 8
                    topPadding: 0
                    bottomPadding: 0
                    verticalAlignment: TextInput.AlignVCenter
                    background: Rectangle { color: BtDarkStyle.bgMedium; radius: 4; border.color: BtDarkStyle.borderDark }
                    onTextEdited: pickerDelay.restart()
                    Keys.onReturnPressed: if (playerPicker.rows.length > 0) playerPicker.choose(playerPicker.rows[0].id)
                    Timer { id: pickerDelay; interval: 200; onTriggered: playerPicker.refresh() }
                }
                CheckBox {
                    id: pickerSquad
                    text: "Squad only"
                    visible: playerPicker.team >= 0
                    onToggled: playerPicker.refresh()
                }
            }
            ListView {
                id: pickerList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: playerPicker.rows
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: PlayerRow {
                    required property var modelData
                    required property int index
                    width: pickerList.width
                    rowIndex: index
                    leading: modelData.clubName
                    row: ({ id: modelData.id, first: modelData.first, last: modelData.last, known: true,
                            roles: modelData.roleName + "  ·  age " + modelData.age, rating: modelData.rating })
                    onClicked: playerPicker.choose(modelData.id)
                }
            }
            RowLayout {
                Text {
                    text: playerPicker.rows.length + " players  ·  click one to pick it"
                    color: BtDarkStyle.textDim
                    font.pixelSize: 11
                    Layout.fillWidth: true
                }
                SmallButton { text: "Cancel"; onClicked: playerPicker.close() }
            }
        }
    }

    PickerPopup {
        id: teamPicker
        property string title: ""
        property var callback: null
        property var rows: []
        function pick(heading, onPicked) {
            title = heading
            callback = onPicked
            teamPickerSearch.text = ""
            rows = db.teamRows("", -1)
            open()
            teamPickerSearch.forceActiveFocus()
        }
        function choose(id) {
            close()
            if (callback)
                callback(id)
            callback = null
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text { text: teamPicker.title; color: BtDarkStyle.accentColor; font.bold: true; font.pixelSize: 15 }
            TextField {
                id: teamPickerSearch
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                placeholderText: "Search teams..."
                color: BtDarkStyle.textMain
                font.pixelSize: 13
                leftPadding: 8
                topPadding: 0
                bottomPadding: 0
                verticalAlignment: TextInput.AlignVCenter
                background: Rectangle { color: BtDarkStyle.bgMedium; radius: 4; border.color: BtDarkStyle.borderDark }
                onTextEdited: teamPicker.rows = db.teamRows(text, -1)
            }
            ListView {
                id: teamPickerList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: teamPicker.rows
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    id: pickTeamRow
                    required property var modelData
                    required property int index
                    width: teamPickerList.width
                    height: 34
                    color: pickTeamMouse.containsMouse ? BtDarkStyle.bgHover : (index % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
                    MouseArea {
                        id: pickTeamMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: teamPicker.choose(pickTeamRow.modelData.id)
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10
                        Image {
                            source: pickTeamRow.modelData.logoUrl
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 26
                            sourceSize: Qt.size(52, 52)
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }
                        Text { text: pickTeamRow.modelData.id; color: BtDarkStyle.textDim; font.family: "Consolas"; font.pixelSize: 12; Layout.preferredWidth: 40 }
                        Text { text: pickTeamRow.modelData.fullName; color: BtDarkStyle.textMain; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                }
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                SmallButton { text: "Cancel"; onClicked: teamPicker.close() }
            }
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Menus / dialogs
    // ------------------------------------------------------------------------------------------------
    Menu {
        id: moreMenu
        width: 260
        MenuItem {
            text: "Reload"
            icon.source: editorRoot.iconDir + "reset_32.png"
            icon.color: "transparent"
            enabled: !db.busy
            onTriggered: editorRoot.guarded(() => db.load())
        }
        MenuSeparator {}
        MenuItem {
            text: "Export Team List (CSV)..."
            icon.source: editorRoot.iconDir + "extract_32.png"
            icon.color: "transparent"
            enabled: db.loaded
            onTriggered: csvDialog.start("teams")
        }
        MenuItem {
            text: "Export Player List (CSV)..."
            icon.source: editorRoot.iconDir + "extract_32.png"
            icon.color: "transparent"
            enabled: db.loaded
            onTriggered: csvDialog.start("players")
        }
        MenuSeparator {}
        MenuItem {
            text: "Open as M3MP"
            icon.source: editorRoot.iconDir + "m3mp_32.png"
            icon.color: "transparent"
            enabled: db.loaded
            onTriggered: editorRoot.guarded(editorRoot.openAsM3mp)
        }
    }

    FileDialog {
        id: csvDialog
        property string kind: "teams"
        function start(k) {
            kind = k
            const folder = settings.lastFolder !== "" ? settings.lastFolder
                                                      : StandardPaths.writableLocation(StandardPaths.DocumentsLocation).toString()
            currentFolder = folder
            fileMode = FileDialog.SaveFile
            nameFilters = ["CSV (*.csv)", "All files (*)"]
            defaultSuffix = "csv"
            title = k === "teams" ? "Export the team list" : "Export the player list"
            selectedFile = folder + "/" + encodeURIComponent((db.gameName || "Database") + (k === "teams" ? " Teams.csv" : " Players.csv"))
            open()
        }
        onAccepted: {
            const url = selectedFile.toString()
            settings.lastFolder = url.substring(0, url.lastIndexOf("/"))
            const error = db.exportCsv(kind, url)
            editorRoot.setStatus(error === "", error === "" ? "Exported the " + kind + " to " + decodeURIComponent(url.replace("file:///", "")) : error)
        }
    }

    // A team's jerseys to / from a file (.bajersey: the team's whole jersey data, for the same game).
    FileDialog {
        id: kitDialog
        property string mode: "export"
        function start(m) {
            mode = m
            const folder = settings.lastFolder !== "" ? settings.lastFolder
                                                      : StandardPaths.writableLocation(StandardPaths.DocumentsLocation).toString()
            currentFolder = folder
            fileMode = m === "export" ? FileDialog.SaveFile : FileDialog.OpenFile
            nameFilters = ["Jerseys (*.bajersey)", "All files (*)"]
            defaultSuffix = "bajersey"
            title = m === "export" ? "Export this team's jerseys" : "Import jerseys into this team"
            if (m === "export") {
                const name = (editorRoot.tv("fullName") || "team").replace(/[\\/:*?"<>|]/g, "_")
                selectedFile = folder + "/" + encodeURIComponent(name + ".bajersey")
            }
            open()
        }
        onAccepted: {
            const url = selectedFile.toString()
            settings.lastFolder = url.substring(0, url.lastIndexOf("/"))
            const file = decodeURIComponent(url.replace("file:///", ""))
            const error = mode === "export" ? db.exportJerseys(editorRoot.teamIndex, url) : db.importJerseys(editorRoot.teamIndex, url)
            editorRoot.setStatus(error === "", error !== "" ? error
                                 : mode === "export" ? "Saved the jerseys to " + file : "Imported the jerseys from " + file)
            if (mode === "import" && error === "")
                kitPage.refresh()
        }
    }

    Dialog {
        id: unsavedDialog
        title: "Unsaved changes"
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
            text: unsavedDialog.title
            color: BtDarkStyle.accentColor
            font.bold: true
            font.pixelSize: 15
            leftPadding: 20
            topPadding: 16
        }
        contentItem: Text {
            text: "The database has unsaved changes. Save them to the mods folder first?"
            color: BtDarkStyle.textMain
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            HeaderButton {
                text: "Save to Mods"
                accent: true
                implicitHeight: 34
                Layout.bottomMargin: 14
                onClicked: {
                    unsavedDialog.close()
                    editorRoot.pendingAfterSave = editorRoot.pendingAction
                    editorRoot.pendingAction = null
                    db.save()
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
