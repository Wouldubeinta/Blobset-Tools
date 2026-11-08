import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import com.wouldymods.blobset 1.0

// Save Editor: the custom players and teams in the Academy save (AcademyData000.SAV), laid out like the Database
// Editor. Rugby League 26: SaveEditorManager; Rugby League Live 4: Rll4SaveEditorManager (a port of Wouldy's RLL4
// Save Editor); Rugby League Live 3: Rll3SaveEditorManager. They do the file work and hold the mapped fields, with
// the same API. CareerEditor.qml is this in career mode (careerMode): Rugby League Live 4's career save
// (Rll4CareerEditorManager) or Rugby League Live 3's (Rll3CareerEditorManager), with a Career list (settings,
// player status, contracts, retirements, competitions).
Item {
    id: editorRoot
    anchors.fill: parent

    property bool careerMode: false

    readonly property string iconDir: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/"
    readonly property color errorColor: Material.color(Material.Red)

    // What the right hand side shows: "", "team" or "player".
    property string kind: ""
    property int recordIndex: -1
    property var fieldsModel: []
    property var groupsModel: []
    property var rawModel: []
    property var lookModel: []          // A player's appearance fields (PaSerial items).
    property var rosterModel: []        // A team's roster (save.rosterRows).
    property int rosterSelected: -1
    property var kitInfo: ({})          // A team's jerseys (save.jerseys).
    property int kitSelected: 0
    property var kitFields: []          // The selected jersey's fields (PaSerial items).
    property string kitGroup: ""
    // The detail tabs for what's open; the pages are found by name (pageIndex), so teams and players share them.
    readonly property var tabNames: kind === "career" ? ((careerItemsModel[recordIndex] || {}).tabs || [])
                                  : isLive ? (kind === "team" ? ["Details", "Stadiums", "Roster", "Lineup", "Jerseys", "Raw"]
                                                              : (isRll4 || isCareer ? ["Identity", "Ratings", "Appearance", "Stats", "Raw"]
                                                                        : ["Identity", "Ratings", "Appearance", "Raw"]))
                                           : (kind === "team" ? (hasLineupApi ? ["Details", "Stadiums", "Feeder Clubs", "Roster", "Lineup", "Jerseys", "Raw"]
                                                                              : ["Details", "Stadiums", "Feeder Clubs", "Roster", "Jerseys", "Raw"])
                                                              : ["Identity", "Ratings", "Appearance", "Raw"])
    readonly property string page: tabNames[detailTabs.currentIndex] || ""
    function pageIndex(name) {
        return ({ "Details": 0, "Identity": 0, "Stadiums": 1, "Ratings": 1, "Feeder Clubs": 2, "Appearance": 2,
                  "Raw": 3, "Roster": 4, "Jerseys": 5, "Lineup": 6, "Stats": 7, "Settings": 0, "Selection": 0,
                  "Player Status": 8, "Contracts": 9, "Retirements": 10, "Ladder": 11, "Fixtures": 12 })[name] || 0
    }
    property string lookGroup: ""
    property var teamRowsModel: []
    property var playerRowsModel: []
    // The career (career mode): its pages, and what they show.
    property var careerItemsModel: []
    property var statusModel: []
    property int statusSelected: -1
    property var contractModel: []
    property int contractSelected: -1
    property var contractFieldsModel: []
    property var retirementModel: []
    property int retirementSelected: -1
    property int ladderComp: 0
    property var ladderModel: []
    property int fixtureStage: 1
    property int fixtureRound: 0
    property var fixtureModel: []
    property var ladderCompsModel: []
    property var fixtureStagesModel: []
    property var fixtureRoundsModel: []

    Rectangle {
        anchors.fill: parent
        color: BtDarkStyle.bgDarkest
        z: -1
    }

    // The game's save: Rugby League 26 (SaveEditorManager), Rugby League Live 4 (Rll4SaveEditorManager) or Rugby League
    // Live 3 (Rll3SaveEditorManager). They have the same API; RLL3 / RLL4 add adding / deleting / importing records
    // (and RLL4 stats).
    readonly property bool isRll4: appOrchestrator.currentGameName === "Rugby League Live 4"
    readonly property bool isRll3: appOrchestrator.currentGameName === "Rugby League Live 3"
    readonly property bool isRl26: !isRll3 && !isRll4
    readonly property bool isRl26Career: careerMode && isRl26   // Rugby League 26's career save (Rl26CareerEditorManager).
    readonly property bool isLive: isRll3 || isRll4 || isRl26Career   // The editors of all teams and players.
    readonly property bool isCareer: careerMode && isLive
    // The career editors' icons (the C# editors'): assets/editors/<name>.png.
    readonly property string editorIconDir: "qrc:/qt/qml/com/wouldymods/blobset/assets/editors/"
    function careerIcon(key) {
        const name = ({ "settings": "career_settings", "selection": "team_selection", "status": "player_status",
                        "contracts": "player_contracts", "retirements": "retired", "competitions": "comp_ladder" })[key]
        return name ? editorIconDir + name + ".png" : ""
    }
    function tabIcon(tab) {
        if (!isCareer)
            return ""
        const name = ({ "Settings": "career_settings", "Selection": "team_selection", "Player Status": "player_status",
                        "Contracts": "player_contracts", "Retirements": "retired", "Ladder": "comp_ladder",
                        "Fixtures": "team_scores", "Stats": "plus_minus", "Raw": "raw_database", "Roster": "player_selection",
                        "Lineup": "team_selection" })[tab]
        return name ? editorIconDir + name + ".png" : ""
    }
    // Player status (0 available, injured, suspended, retired, NSW / QLD Origin camp).
    function statusIcon(reason) {
        const name = ["available", "injured", "suspended", "retired", "nsw_camp", "qld_camp"][reason]
        return name ? editorIconDir + name + ".png" : ""
    }
    // Form (the C# editor's arrows): 0-29 red, 30-44 orange, 45-54 blue, 55-69 light blue, 70+ green.
    function formIcon(form) {
        const name = form < 30 ? "red_arrow" : form < 45 ? "orange_arrow" : form < 55 ? "blue_arrow"
                   : form < 70 ? "light_blue_arrow" : "green_arrow"
        return editorIconDir + name + ".png"
    }
    // A career page's badge (by its key).
    function careerBadge(key) {
        return ({ "settings": "SET", "selection": "SEL", "status": "ST", "contracts": "CON", "retirements": "RET",
                  "competitions": "COMP" })[key] || ""
    }
    readonly property string gameTitle: isRll4 ? "Rugby League Live 4" : isRll3 ? "Rugby League Live 3" : "Rugby League 26"
    readonly property var save: isCareer ? (isRll4 ? rll4Career : isRll3 ? rll3Career : rl26Career) : isRll4 ? rll4Save : isRll3 ? rll3Save : rl26Save
    // The save's file name and what's in it (for the texts).
    readonly property string saveFileName: isCareer ? (isRll3 ? "CAR000.SAV" : "CareerData.SAV") : isRll3 ? "FED000.SAV" : "AcademyData000.SAV"
    readonly property string saveContents: isCareer ? "The career save: every team and player, the career's settings, contracts and competitions"
                                         : isRll3 ? "The save's teams and players"
                                                  : "Custom players and teams in the Academy save"
    // The manager has the Database Editor's lineup functions (RLL4; RL26 once its save's lineups are mapped).
    readonly property bool hasLineupApi: typeof save.matchLists === "function"
    SaveEditorManager { id: rl26Save }
    Rll4SaveEditorManager { id: rll4Save }
    Rll3SaveEditorManager { id: rll3Save }
    Rll4CareerEditorManager { id: rll4Career }
    Rll3CareerEditorManager { id: rll3Career }
    Rl26CareerEditorManager { id: rl26Career }
    Connections {
        target: editorRoot.save
        function onRevisionChanged() { refreshTimer.restart() }
        function onLoadedChanged() {
            editorRoot.reloadLists()
            if (editorRoot.kind === "" || editorRoot.recordIndex < 0) {
                if (editorRoot.teamRowsModel.length > 0)
                    editorRoot.openRecord("team", editorRoot.teamRowsModel[0].index)
                else if (editorRoot.playerRowsModel.length > 0)
                    editorRoot.openRecord("player", editorRoot.playerRowsModel[0].index)
            }
        }
    }

    Settings {
        id: settings
        category: "SaveEditor"
        property string lastFile: ""
        property string lastFileRll4: ""
        property string lastFileRll3: ""
        property string lastFileCareer: ""
        property string lastFileCareer3: ""
        property string lastFileCareer26: ""
        property real splitWidth: 420
    }
    function lastFile() { return isCareer ? (isRll4 ? settings.lastFileCareer : isRll3 ? settings.lastFileCareer3 : settings.lastFileCareer26) : isRll4 ? settings.lastFileRll4 : isRll3 ? settings.lastFileRll3 : settings.lastFile }
    function rememberFile(path) {
        if (isCareer && isRll4)
            settings.lastFileCareer = path
        else if (isCareer && isRll3)
            settings.lastFileCareer3 = path
        else if (isCareer)
            settings.lastFileCareer26 = path
        else if (isRll4)
            settings.lastFileRll4 = path
        else if (isRll3)
            settings.lastFileRll3 = path
        else
            settings.lastFile = path
    }

    Component.onCompleted: editorRoot.save.load(editorRoot.lastFile())

    // ------------------------------------------------------------------------------------------------
    // Data helpers
    // ------------------------------------------------------------------------------------------------
    function openRecord(k, index) {
        if (k !== kind || index !== recordIndex) {
            rosterSelected = -1
            kitSelected = 0
            statusSelected = -1
            contractSelected = -1
            retirementSelected = -1
            if (k === "career")
                detailTabs.currentIndex = 0     // Each career page has its own tabs.
        }
        if (k !== kind) {
            const was = page
            kind = k
            const keep = tabNames.indexOf(was)
            detailTabs.currentIndex = keep >= 0 ? keep : 0
        }
        recordIndex = index
        groupsModel = index >= 0 && k !== "career" ? editorRoot.save.groups(k) : []
        refreshRecord()
        listTabs.currentIndex = k === "team" ? 0 : k === "player" ? 1 : 2
    }
    function refreshRecord() {
        if (kind === "career") {
            refreshCareer()
            return
        }
        fieldsModel = recordIndex >= 0 ? editorRoot.save.fields(kind, recordIndex) : []
        rawModel = recordIndex >= 0 && (editorRoot.tabNames[detailTabs.currentIndex] || "") === "Raw" ? editorRoot.save.rawRows(kind, recordIndex) : []
        rosterModel = recordIndex >= 0 && kind === "team" ? editorRoot.save.rosterRows(recordIndex) : []
        if (rosterSelected >= rosterModel.length)
            rosterSelected = rosterModel.length - 1
        refreshKit()
        // Appearance: the same items as the Database Editor's Appearance tab, marked so edits go to the look.
        lookModel = recordIndex >= 0 && kind === "player"
                ? editorRoot.save.appearanceFields(recordIndex).map((f) => Object.assign({ appearance: true, min: 0, max: 255, offset: "" }, f)) : []
        if (lookModel.length > 0 && lookGroups.indexOf(lookGroup) < 0)
            lookGroup = lookGroups[0]
    }
    // The career page that's showing.
    function refreshCareer() {
        // The tab's name from the tab bar itself: this runs from the tab bar's index change, which can come before the
        // page binding has caught up (it then loaded the old tab and blanked the new one).
        const page = editorRoot.tabNames[detailTabs.currentIndex] || ""
        fieldsModel = recordIndex === 0 || recordIndex === 1 ? editorRoot.save.fields("career", recordIndex) : []
        rawModel = []
        rosterModel = []
        lookModel = []
        kitInfo = ({})
        kitFields = []
        statusModel = page === "Player Status" ? editorRoot.save.statusRows() : []
        if (statusSelected >= statusModel.length)
            statusSelected = statusModel.length - 1
        contractModel = page === "Contracts" ? editorRoot.save.contractRows() : []
        if (contractSelected >= contractModel.length)
            contractSelected = contractModel.length - 1
        contractFieldsModel = contractSelected >= 0 && page === "Contracts"
                ? editorRoot.save.fields("contract", contractSelected).map((f) => Object.assign({ contract: true }, f)) : []
        retirementModel = page === "Retirements" ? editorRoot.save.retirementRows() : []
        if (retirementSelected >= retirementModel.length)
            retirementSelected = retirementModel.length - 1
        // The ladder and the fixtures are both kept loaded, so going between the two tabs never shows an empty list.
        ladderCompsModel = editorRoot.save.ladderComps()
        // A competition that isn't in this save's list (another save was loaded) would leave the box blank.
        if (ladderCompsModel.length > 0 && !ladderCompsModel.some((c) => c.index === ladderComp))
            ladderComp = ladderCompsModel[0].index
        ladderModel = editorRoot.save.ladderRows(ladderComp)
        fixtureStagesModel = editorRoot.save.fixtureStages()
        if (fixtureStagesModel.length > 0 && !fixtureStagesModel.some((c) => c.index === fixtureStage)) {
            fixtureStage = fixtureStagesModel[0].index
            fixtureRound = 0
        }
        fixtureRoundsModel = editorRoot.save.fixtureRounds(fixtureStage)
        if (fixtureRound >= fixtureRoundsModel.length)
            fixtureRound = 0
        fixtureModel = editorRoot.save.fixtureRows(fixtureStage, fixtureRound)
    }
    function refreshKit() {
        kitInfo = recordIndex >= 0 && kind === "team" ? editorRoot.save.jerseys(recordIndex) : ({})
        if (kitInfo.ok !== true) {
            kitFields = []
            return
        }
        kitSelected = Math.max(0, Math.min(kitSelected, kitInfo.count - 1))
        kitFields = editorRoot.save.jerseyFields(recordIndex, kitSelected).map((f) => Object.assign({ jersey: true, min: 0, offset: "" }, f))
        if (kitGroups.indexOf(kitGroup) < 0)
            kitGroup = kitGroups[0] || ""
    }
    readonly property var kitGroups: {
        const out = []
        for (const f of kitFields)
            if (out.indexOf(f.group) < 0)
                out.push(f.group)
        return out
    }
    readonly property var lookGroups: {
        const out = []
        for (const f of lookModel)
            if (out.indexOf(f.group) < 0)
                out.push(f.group)
        return out
    }
    function setField(key, value) {
        if (recordIndex >= 0)
            editorRoot.save.setField(kind, recordIndex, key, value)
    }
    // A field editor's commit: the record's own fields, or the player's appearance.
    function commitField(field, value) {
        if (recordIndex < 0)
            return
        if (field.contract === true)
            editorRoot.save.setField("contract", contractSelected, field.key, value)
        else if (field.appearance === true)
            editorRoot.save.setAppearanceField(recordIndex, field.key, value)
        else if (field.jersey === true)
            editorRoot.save.setJerseyField(recordIndex, field.key, value)
        else
            editorRoot.save.setField(kind, recordIndex, field.key, value)
    }
    // The groups each tab shows (the manager's group names).
    function tabGroups(tab) {
        if (kind === "career")
            return tab === 0 ? editorRoot.save.pageGroups("career", page) : []
        if (isLive) {
            const page = (kind === "team" ? ["Details", "Stadiums"] : ["Identity", "Ratings"])[tab] || (tab === 7 ? "Stats" : "")
            return page !== "" ? editorRoot.save.pageGroups(kind, page) : []
        }
        if (kind === "team")
            return [["Names", "Details", "Colours", "Commentary", "Record"],
                    ["Home Ground 1", "Home Ground 2", "Home Ground 3", "Grand Final Ground"],
                    ["Feeder Clubs"]][tab] || []
        if (kind === "player")
            return [["Name", "Details", "Roles", "Representative", "Record"], ["Attributes", "Skills"], []][tab] || []
        return []
    }
    function componentFor(field) {
        if ((field.slider === true || field.group === "Attributes" || field.group === "Skills") && field.kind === "number"
                && field.min === 0 && field.max === 99)
            return sliderComp
        return field.kind === "text" ? textComp
             : field.kind === "bool" ? boolComp
             : field.kind === "colour" ? colourComp
             : field.kind === "palette" ? paletteComp
             : field.kind === "choice" ? choiceComp
             : field.kind === "big" || field.readOnly ? bigComp : numberComp
    }
    function ratingColor(r) {
        return r >= 85 ? "#4caf50" : r >= 75 ? "#9ccc65" : r >= 65 ? BtDarkStyle.accentColor : r >= 50 ? "#ff9800" : "#e53935"
    }
    function roleShort(name) {
        const map = { "Fullback": "FB", "Winger": "WG", "Centre": "C", "Five-Eighth": "FE", "Halfback": "HB",
                      "Prop": "FR", "Hooker": "HK", "Second Row": "SR", "Lock": "L" }
        return map[name] || "?"
    }
    // Rugby League 26's field names -> Rugby League Live 4's / 3's (for the banners).
    function gameKey(key) {
        if (isRll3)
            return ({ "logoNumber": "logo", "hudName": "abbreviatedName", "clubType": "type", "preferredRolePrimary": "primaryRole",
                      "firstname": "firstName", "surname": "lastName", "age": "dob.year" })[key] || key
        if (!isRll4)
            return key
        return ({ "primaryRGBColour": "primaryColour", "secondaryRGBColour": "secondaryColour", "logoNumber": "@logo",
                  "hudName": "abbreviatedName", "sex": "gender", "clubType": "type", "preferredRolePrimary": "primaryRole",
                  "firstname": "firstName", "surname": "lastName", "age": "dob.year" })[key] || key
    }
    function choiceName(key) {
        key = gameKey(key)
        const f = fieldsModel.find((x) => x.key === key)
        if (!f || !f.choices)
            return ""
        const c = f.choices.find((x) => x.value === f.value)
        return c ? c.name : ""
    }
    readonly property int currentRating: {
        editorRoot.save.revision
        return kind === "player" && recordIndex >= 0 ? editorRoot.save.playerRating(recordIndex) : 0
    }
    function fieldValue(key) {
        if (isRll3 && kind === "team") {
            // RLL3 teams: one colour (an index into the team palette), the name in two parts.
            if (key === "primaryRGBColour") {
                const set = fieldsModel.find((x) => x.key === "colourSet")
                const c = set && set.choices ? set.choices.find((x) => x.value === set.value) : null
                return c ? c.colour : ""
            }
            if (key === "secondaryRGBColour")
                return ""
            if (key === "fullName")
                return ((fieldValue("locationName") || "") + " " + (fieldValue("clubName") || "")).trim()
        }
        key = gameKey(key)
        const f = fieldsModel.find((x) => x.key === key)
        return f ? f.value : ""
    }
    function reloadLists() {
        teamRowsModel = editorRoot.save.teamRows(teamSearch.text)
        playerRowsModel = editorRoot.save.playerRows(playerSearch.text)
        careerItemsModel = isCareer ? editorRoot.save.careerItems() : []
    }

    property string localStatus: ""
    property bool localStatusOk: true
    function setStatus(ok, text) {
        localStatusOk = ok
        localStatus = text
    }
    Connections {
        target: editorRoot.save
        function onStatusChanged() { editorRoot.localStatus = "" }
    }

    Timer {
        id: refreshTimer
        interval: 80
        onTriggered: {
            editorRoot.refreshRecord()
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
    function guarded(action) {
        if (editorRoot.save.unsavedChanges) {
            pendingAction = action
            unsavedDialog.open()
        } else {
            action()
        }
    }
    function goBack() {
        appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml")
    }
    function loadFile(pathOrUrl) {
        kind = ""
        recordIndex = -1
        if (editorRoot.save.load(pathOrUrl))
            editorRoot.rememberFile(editorRoot.save.filePath)
    }
    function addRecord(k, female) {
        const index = editorRoot.save.addRecord(k, female)
        if (index >= 0) {
            reloadLists()
            openRecord(k, index)
        }
    }
    function folderUrl(path) {
        const p = path.replace(/\\/g, "/")
        return "file:///" + p.substring(0, p.lastIndexOf("/"))
    }

    Shortcut { sequences: [StandardKey.Save]; enabled: editorRoot.save.loaded && !rawView.visible; onActivated: editorRoot.save.save() }
    Shortcut {
        sequences: [StandardKey.Find]
        enabled: !rawView.visible
        onActivated: {
            const field = listTabs.currentIndex === 0 ? teamSearch : playerSearch
            field.forceActiveFocus()
            field.selectAll()
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Look (the Database Editor's)
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
        Layout.preferredWidth: 170
        Layout.alignment: Qt.AlignVCenter
    }

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

    component OffsetText: Text {
        color: BtDarkStyle.textDim
        font.family: "Consolas"
        font.pixelSize: 11
        Layout.preferredWidth: 52
    }

    // Text edit: commit(text) when editing finishes.
    component TextFieldEdit: RowLayout {
        id: textEdit
        property string label: ""
        property string value: ""
        property int maxLength: 255
        property string offset: ""
        signal commit(string text)
        spacing: 10
        Layout.fillWidth: true
        FieldLabel { text: textEdit.label }
        TextField {
            id: textField
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            text: textEdit.value
            maximumLength: textEdit.maxLength
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
        Text {
            text: textEdit.value.length + "/" + textEdit.maxLength
            color: BtDarkStyle.textDim
            font.pixelSize: 11
            Layout.preferredWidth: 40
        }
        OffsetText { text: textEdit.offset }
    }

    // Whole number edit.
    component NumberEdit: RowLayout {
        id: numberEdit
        property string label: ""
        property int value: 0
        property int from: 0
        property int to: 99
        property string offset: ""
        signal commit(int value)
        spacing: 10
        Layout.fillWidth: true
        FieldLabel { text: numberEdit.label }
        SpinBox {
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
        Item { Layout.fillWidth: true }
        OffsetText { text: numberEdit.offset }
    }

    // A big whole number (hashes, ids): typed in, decimal or 0x hex.
    component BigNumberEdit: RowLayout {
        id: bigEdit
        property string label: ""
        property double value: 0
        property bool readOnly: false
        property string offset: ""
        signal commit(double value)
        spacing: 10
        Layout.fillWidth: true
        FieldLabel { text: bigEdit.label }
        TextField {
            id: bigField
            Layout.preferredWidth: 150
            Layout.preferredHeight: 34
            text: String(bigEdit.value)
            readOnly: bigEdit.readOnly
            color: bigEdit.readOnly ? BtDarkStyle.textDim : BtDarkStyle.textMain
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
        OffsetText { text: bigEdit.offset }
    }

    // A drop down: choices [{name, value}]; pick(value) when the user chooses one.
    component ChoiceEdit: RowLayout {
        id: choiceEdit
        property string label: ""
        property int labelWidth: 170
        property var choices: []
        property var value
        property string offset: ""
        signal pick(var value)
        spacing: 10
        Layout.fillWidth: true
        // The box's index is set here rather than bound: a binding stops updating once the user picks from the box, and a
        // model reset puts it back to 0 / -1, which left the competition and round boxes blank or on the wrong item.
        function sync() {
            const list = choiceEdit.choices || []
            let i = -1
            for (let k = 0; k < list.length; ++k) {
                if (list[k].value === choiceEdit.value || String(list[k].value) === String(choiceEdit.value)) { i = k; break }
            }
            if (combo.currentIndex !== i)
                combo.currentIndex = i
        }
        onChoicesChanged: { combo.model = (choiceEdit.choices || []).map((c) => c.name); sync() }
        onValueChanged: sync()
        Component.onCompleted: { combo.model = (choiceEdit.choices || []).map((c) => c.name); sync() }
        FieldLabel { text: choiceEdit.label; Layout.preferredWidth: choiceEdit.labelWidth }
        ComboBox {
            id: combo
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            font.pixelSize: 13
            // The shown text comes from the value, not the box's index, so it's right even while the list is reloading.
            displayText: {
                const list = choiceEdit.choices || []
                for (let k = 0; k < list.length; ++k) {
                    if (list[k].value === choiceEdit.value || String(list[k].value) === String(choiceEdit.value))
                        return list[k].name
                }
                return ""
            }
            onActivated: (i) => {
                choiceEdit.pick(choiceEdit.choices[i].value)
                Qt.callLater(choiceEdit.sync)
            }
            onCountChanged: Qt.callLater(choiceEdit.sync)
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
        OffsetText { text: choiceEdit.offset }
    }

    // A colour swatch that opens a colour picker; commit("#rrggbb").
    component ColourEdit: RowLayout {
        id: colourEdit
        property string label: ""
        property string value: "#000000"
        property string offset: ""
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
        OffsetText { text: colourEdit.offset }
        ColorDialog {
            id: colourDialog
            title: colourEdit.label
            onAccepted: colourEdit.commit(selectedColor.toString().substring(0, 7))
        }
    }

    // A colour from the game's palette (RLL3): the swatch opens a grid of the palette's colours; pick(index).
    component PaletteEdit: RowLayout {
        id: paletteEdit
        property string label: ""
        property var choices: []        // {name, value, colour}
        property var value
        property string offset: ""
        readonly property var current: choices.find((c) => c.value === value) || null
        signal pick(var value)
        spacing: 10
        Layout.fillWidth: true
        FieldLabel { text: paletteEdit.label }
        Rectangle {
            id: paletteSwatch
            Layout.preferredWidth: 90
            Layout.preferredHeight: 28
            radius: 4
            color: paletteEdit.current ? paletteEdit.current.colour : "transparent"
            border.color: paletteArea.containsMouse || palettePopup.opened ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
            border.width: 2
            MouseArea {
                id: paletteArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: palettePopup.opened ? palettePopup.close() : palettePopup.open()
            }
            Popup {
                id: palettePopup
                y: parent.height + 4
                padding: 8
                readonly property int columns: Math.min(16, paletteEdit.choices.length)
                width: columns * 22 + 16
                background: Rectangle {
                    color: BtDarkStyle.bgDark
                    radius: 6
                    border.color: BtDarkStyle.borderDark
                }
                contentItem: Grid {
                    columns: palettePopup.columns
                    spacing: 2
                    Repeater {
                        model: paletteEdit.choices
                        Rectangle {
                            required property var modelData
                            width: 20
                            height: 20
                            radius: 3
                            color: modelData.colour || "transparent"
                            border.width: modelData.value === paletteEdit.value || cellArea.containsMouse ? 2 : 1
                            border.color: modelData.value === paletteEdit.value ? BtDarkStyle.accentColor
                                        : cellArea.containsMouse ? BtDarkStyle.textMain : BtDarkStyle.borderDark
                            ToolTip.visible: cellArea.containsMouse
                            ToolTip.text: modelData.name
                            ToolTip.delay: 300
                            MouseArea {
                                id: cellArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    palettePopup.close()
                                    paletteEdit.pick(parent.modelData.value)
                                }
                            }
                        }
                    }
                }
            }
        }
        Text {
            text: paletteEdit.current ? paletteEdit.current.name : String(paletteEdit.value)
            color: BtDarkStyle.textDim
            font.family: "Consolas"
            font.pixelSize: 12
        }
        Item { Layout.fillWidth: true }
        OffsetText { text: paletteEdit.offset }
    }

    // A fixture's team: its logo (or abbreviation in its colour) and name; mirrored: the away side.
    // (Not "right": Item has a FINAL right.)
    component TeamSide: RowLayout {
        id: side
        property string teamName: ""
        property string logoUrl: ""
        property string abbreviation: ""
        property string colour: ""
        property bool mirrored: false
        layoutDirection: mirrored ? Qt.RightToLeft : Qt.LeftToRight
        spacing: 8
        Image {
            id: sideLogo
            visible: status === Image.Ready
            source: side.logoUrl
            asynchronous: true
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            sourceSize: Qt.size(72, 72)
            fillMode: Image.PreserveAspectFit
        }
        Rectangle {
            visible: !sideLogo.visible
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            radius: 18
            color: side.colour || BtDarkStyle.bgMedium
            border.color: BtDarkStyle.borderDark
            Text {
                anchors.centerIn: parent
                text: side.abbreviation
                color: BtDarkStyle.white
                font.pixelSize: 10
                font.bold: true
                style: Text.Outline
                styleColor: "#80000000"
            }
        }
        Text {
            text: side.teamName
            color: BtDarkStyle.textMain
            font.pixelSize: 13
            font.bold: true
            elide: Text.ElideRight
            horizontalAlignment: side.mirrored ? Text.AlignRight : Text.AlignLeft
            Layout.fillWidth: true
            Layout.minimumWidth: 40
        }
    }

    // A team's logo, or its abbreviation in its colour when it has none.
    component TeamBadge: Item {
        id: badge
        property string logoUrl: ""
        property string abbreviation: ""
        property string colour: ""
        property int size: 64
        implicitWidth: size
        implicitHeight: size
        Image {
            id: badgeLogo
            anchors.fill: parent
            visible: status === Image.Ready
            source: badge.logoUrl
            asynchronous: true
            sourceSize: Qt.size(badge.size * 2, badge.size * 2)
            fillMode: Image.PreserveAspectFit
        }
        Rectangle {
            visible: !badgeLogo.visible
            anchors.centerIn: parent
            width: badge.size * 0.9
            height: width
            radius: width / 2
            color: badge.colour || BtDarkStyle.bgMedium
            border.color: BtDarkStyle.borderDark
            Text {
                anchors.centerIn: parent
                text: badge.abbreviation
                color: BtDarkStyle.white
                font.pixelSize: Math.max(9, badge.size / 4)
                font.bold: true
                style: Text.Outline
                styleColor: "#80000000"
            }
        }
    }

    // The match view's scorer box (Tries, Goals, ...): each scorer and how many.
    component ScorerBox: Rectangle {
        id: scorerBox
        property string title: ""
        property var scorers: []      // [{name, count}]
        property bool mirrored: false
        Layout.fillWidth: true
        implicitHeight: scorerColumn.implicitHeight + 16
        color: BtDarkStyle.bgDark
        border.color: BtDarkStyle.borderDark
        radius: 6
        ColumnLayout {
            id: scorerColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 4
            Text {
                text: scorerBox.title
                color: BtDarkStyle.accentColor
                font.pixelSize: 13
                font.bold: true
                Layout.fillWidth: true
                horizontalAlignment: scorerBox.mirrored ? Text.AlignRight : Text.AlignLeft
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: BtDarkStyle.borderDark }
            Text {
                visible: scorerBox.scorers.length === 0
                text: "-"
                color: BtDarkStyle.textDim
                font.pixelSize: 12
                Layout.fillWidth: true
                horizontalAlignment: scorerBox.mirrored ? Text.AlignRight : Text.AlignLeft
            }
            Repeater {
                model: scorerBox.scorers
                RowLayout {
                    id: scorerLine
                    required property var modelData
                    Layout.fillWidth: true
                    layoutDirection: scorerBox.mirrored ? Qt.RightToLeft : Qt.LeftToRight
                    spacing: 8
                    Text {
                        text: scorerLine.modelData.name
                        color: BtDarkStyle.textMain
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        horizontalAlignment: scorerBox.mirrored ? Text.AlignRight : Text.AlignLeft
                        Layout.fillWidth: true
                    }
                    Text {
                        text: scorerLine.modelData.count
                        color: BtDarkStyle.white
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
            }
        }
    }

    // A ring filled to a percentage in a team's colour, the percentage in the middle and a detail ("16/18") under it.
    component Donut: ColumnLayout {
        id: donut
        property real percent: 0
        property string colour: ""
        property string detail: ""
        spacing: 2
        Item {
            Layout.preferredWidth: 92
            Layout.preferredHeight: 92
            Layout.alignment: Qt.AlignHCenter
            Canvas {
                id: donutCanvas
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const r = Math.min(width, height) / 2 - 7
                    ctx.lineWidth = 11
                    ctx.lineCap = "butt"
                    ctx.strokeStyle = String(BtDarkStyle.bgMedium)
                    ctx.beginPath()
                    ctx.arc(width / 2, height / 2, r, 0, 2 * Math.PI)
                    ctx.stroke()
                    const p = Math.max(0, Math.min(100, donut.percent)) / 100
                    if (p > 0) {
                        ctx.strokeStyle = donut.colour ? String(donut.colour) : String(BtDarkStyle.accentColor)
                        ctx.beginPath()
                        ctx.arc(width / 2, height / 2, r, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * p)
                        ctx.stroke()
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                text: Math.round(donut.percent) + "%"
                color: BtDarkStyle.white
                font.pixelSize: 16
                font.bold: true
            }
        }
        Text {
            text: donut.detail
            color: BtDarkStyle.textMain
            font.pixelSize: 12
            Layout.alignment: Qt.AlignHCenter
        }
        onPercentChanged: donutCanvas.requestPaint()
        onColourChanged: donutCanvas.requestPaint()
    }

    // The match view's page of one side's players, with a way back.
    component MatchPlayersView: ColumnLayout {
        id: playersView
        property var team: ({})
        property var players: []
        signal back()
        signal openPlayer(int playerIndex)
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            SmallButton { text: "Back"; onClicked: playersView.back() }
            TeamBadge {
                size: 32
                logoUrl: playersView.team.logoUrl || ""
                abbreviation: playersView.team.abbreviation || ""
                colour: playersView.team.colour || ""
            }
            Text {
                text: (playersView.team.name || "") + " - Player Stats / Ratings"
                color: BtDarkStyle.accentColor
                font.pixelSize: 15
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: "Double-click a player to open them."
                color: BtDarkStyle.textDim
                font.pixelSize: 11
                font.italic: true
            }
        }
        MatchPlayers {
            Layout.fillWidth: true
            Layout.fillHeight: true
            players: playersView.players
            onOpenPlayer: (playerIndex) => playersView.openPlayer(playerIndex)
        }
    }

    // One side's players in the match view: shirt, name, match rating and their stats.
    component MatchPlayers: ColumnLayout {
        id: matchPlayers
        property var players: []
        signal openPlayer(int playerIndex)
        readonly property var columns: [["#", "shirt", 34], ["Player", "name", -1], ["Rating", "rating", 56], ["T", "tries", 34],
                                        ["TA", "tryAssists", 34], ["Goals", "goals", 50], ["Runs", "runs", 44],
                                        ["Metres", "metres", 54], ["LB", "lineBreaks", 34], ["Off", "offloads", 36],
                                        ["Tkl", "tackles", 38], ["Miss", "missed", 40], ["Err", "errors", 36],
                                        ["Mins", "minutes", 42]]
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 28
            color: BtDarkStyle.bgMedium
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 6
                Repeater {
                    model: matchPlayers.columns
                    Text {
                        required property var modelData
                        text: modelData[0]
                        color: BtDarkStyle.textDim
                        font.pixelSize: 11
                        font.bold: true
                        horizontalAlignment: modelData[2] < 0 ? Text.AlignLeft : Text.AlignHCenter
                        Layout.fillWidth: modelData[2] < 0
                        Layout.preferredWidth: modelData[2] < 0 ? -1 : modelData[2]
                    }
                }
            }
        }
        ListView {
            id: matchPlayerList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: matchPlayers.players
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                id: matchPlayerRow
                required property var modelData
                required property int index
                width: matchPlayerList.width
                height: 32
                color: matchPlayerMouse.containsMouse ? BtDarkStyle.bgHover : (index % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
                MouseArea {
                    id: matchPlayerMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onDoubleClicked: {
                        if (matchPlayerRow.modelData.playerIndex >= 0)
                            matchPlayers.openPlayer(matchPlayerRow.modelData.playerIndex)
                    }
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 6
                    Repeater {
                        model: matchPlayers.columns
                        Text {
                            required property var modelData
                            text: matchPlayerRow.modelData[modelData[1]]
                            color: modelData[1] === "rating" ? editorRoot.ratingColor(Math.min(99, matchPlayerRow.modelData.rating / 1.5))
                                 : modelData[1] === "name" ? BtDarkStyle.textMain : BtDarkStyle.textMain
                            font.pixelSize: 12
                            font.bold: modelData[1] === "rating" || modelData[1] === "name"
                            elide: Text.ElideRight
                            horizontalAlignment: modelData[2] < 0 ? Text.AlignLeft : Text.AlignHCenter
                            Layout.fillWidth: modelData[2] < 0
                            Layout.preferredWidth: modelData[2] < 0 ? -1 : modelData[2]
                        }
                    }
                }
            }
        }
    }

    component CheckEdit: RowLayout {
        id: checkEdit
        property string label: ""
        property bool value: false
        property string offset: ""
        signal commit(bool on)
        spacing: 10
        Layout.fillWidth: true
        FieldLabel { text: checkEdit.label }
        CheckBox {
            checked: checkEdit.value
            font.pixelSize: 13
            onToggled: checkEdit.commit(checked)
        }
        Item { Layout.fillWidth: true }
        OffsetText { text: checkEdit.offset }
    }

    // A rating bar like the game's: label, coloured bar, number. commit() when the drag ends.
    component StatSlider: RowLayout {
        id: stat
        property string label: ""
        property int value: 0
        property int to: 99
        property string offset: ""
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
        OffsetText { text: stat.offset }
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
        Layout.fillWidth: true
        background: Rectangle { color: BtDarkStyle.bgDark }
    }
    component EditorTab: TabButton {
        id: tabButton
        property string iconUrl: ""
        width: implicitWidth
        leftPadding: 16
        rightPadding: 16
        contentItem: Row {
            spacing: 6
            Image {
                visible: tabButton.iconUrl !== ""
                source: tabButton.iconUrl
                width: 16
                height: 16
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: tabButton.text
                color: tabButton.checked ? BtDarkStyle.accentColor : BtDarkStyle.textDim
                font.pixelSize: 13
                font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

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

    component SearchField: TextField {
        id: search
        Layout.fillWidth: true
        Layout.preferredHeight: 34
        color: BtDarkStyle.textMain
        font.pixelSize: 13
        leftPadding: 8
        topPadding: 0
        bottomPadding: 0
        verticalAlignment: TextInput.AlignVCenter
        background: Rectangle {
            color: BtDarkStyle.bgMedium
            radius: 4
            border.color: search.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
        }
    }

    // A row of the team / player lists.
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

    component ListRow: Rectangle {
        id: listRow
        property string title: ""
        property string subtitle: ""
        property bool current: false
        property int rowIndex: 0
        property color stripe: "transparent"
        property string badgeText: ""
        property string iconUrl: ""         // A small icon in the badge (instead of its text).
        property string trailingIconUrl: "" // A small icon before the rating.
        property string imageUrl: ""
        property color badgeColour: BtDarkStyle.bgMedium
        property color badgeTextColour: BtDarkStyle.textMain
        property int rating: -1
        signal clicked()
        signal doubleClicked()
        height: 44
        color: current ? BtDarkStyle.bgMedium
                       : rowMouse.containsMouse ? BtDarkStyle.bgHover
                                                : (rowIndex % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
        Rectangle { width: 4; height: parent.height; color: listRow.stripe }
        Rectangle { visible: listRow.current; anchors.right: parent.right; width: 3; height: parent.height; color: BtDarkStyle.accentColor }
        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: listRow.clicked()
            onDoubleClicked: listRow.doubleClicked()
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 12
            spacing: 10
            Image {
                id: rowImage
                visible: listRow.imageUrl !== "" && status === Image.Ready
                source: listRow.imageUrl
                asynchronous: true
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                sourceSize: Qt.size(68, 68)
                fillMode: Image.PreserveAspectFit
            }
            Rectangle {
                visible: (listRow.badgeText !== "" || listRow.iconUrl !== "") && !rowImage.visible
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                radius: 17
                color: listRow.badgeColour
                border.color: BtDarkStyle.borderDark
                Image {
                    visible: listRow.iconUrl !== ""
                    anchors.centerIn: parent
                    source: listRow.iconUrl
                    width: 20
                    height: 20
                }
                Text {
                    visible: listRow.iconUrl === ""
                    anchors.centerIn: parent
                    text: listRow.badgeText
                    color: listRow.badgeTextColour
                    font.pixelSize: listRow.badgeText.length > 2 ? 10 : 12
                    font.bold: true
                }
            }
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Text {
                    text: listRow.title
                    color: BtDarkStyle.textMain
                    font.pixelSize: 13
                    font.bold: listRow.current
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: listRow.subtitle
                    color: BtDarkStyle.textDim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            Image {
                visible: listRow.trailingIconUrl !== ""
                source: listRow.trailingIconUrl
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
            }
            RatingBadge { visible: listRow.rating > 0; rating: Math.max(0, listRow.rating) }
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
                    source: editorRoot.iconDir + (editorRoot.isCareer ? "career_editor_32.png" : "save_editor_32.png")
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    fillMode: Image.PreserveAspectFit
                }
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
                    Text {
                        text: (editorRoot.isCareer ? "Career Editor  ·  " : "Save Editor  ·  ") + editorRoot.gameTitle
                              + (editorRoot.isRll4 && editorRoot.save.demo ? " (demo)" : "")
                        color: BtDarkStyle.accentColor
                        font.bold: true
                        font.pixelSize: 16
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: editorRoot.save.loaded ? editorRoot.save.teamCount + (editorRoot.isLive ? " teams  ·  " : " custom teams  ·  ")
                                                       + editorRoot.save.playerCount + (editorRoot.isLive ? " players  ·  " : " custom players  ·  ") + editorRoot.save.filePath
                                          : editorRoot.saveContents + " (" + editorRoot.saveFileName + ")"
                        color: BtDarkStyle.textDim
                        font.pixelSize: 11
                    }
                }
                HeaderButton {
                    text: "Raw Databases"
                    tip: "Every " + (editorRoot.isLive ? "" : "custom ") + "player and team as a table (players, skills, teams, rosters, lineups"
                         + (editorRoot.isCareer ? ", leagues, player status" : "") + "): edit the values in place and export / import them as CSV, like the C# editors' Raw Databases"
                    iconSource: editorRoot.editorIconDir + "raw_database.png"
                    enabled: editorRoot.save.loaded && typeof editorRoot.save.rawTables === "function"
                    onClicked: rawView.open()
                }
                HeaderButton {
                    id: fileButton
                    text: "Save File ▾"
                    tip: "Open another save, reload, save as, export the decoded data"
                    iconSource: editorRoot.iconDir + "open_32.png"
                    onClicked: fileMenu.popup(fileButton, 0, fileButton.height)
                }
                HeaderButton {
                    text: "Save"
                    tip: "Write the changes back to " + (editorRoot.save.fileName || "the save") + " (Ctrl+S). The first save keeps the original as .bak."
                    iconSource: editorRoot.iconDir + "save_32.png"
                    accent: editorRoot.save.unsavedChanges
                    enabled: editorRoot.save.loaded
                    onClicked: editorRoot.save.save()
                }
            }
        }

        // Nothing loaded: where to find the save.
        Item {
            visible: !editorRoot.save.loaded
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 60, 640)
                spacing: 14
                PanelLabel { text: "No save loaded" }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: BtDarkStyle.textMain
                    font.pixelSize: 13
                    text: (editorRoot.isCareer ? editorRoot.gameTitle + " keeps a career (every team and player, and the career's "
                                                 + "settings, contracts and competitions) in " + editorRoot.saveFileName
                                               : editorRoot.isRll3 ? editorRoot.gameTitle + " keeps its teams and players in " + editorRoot.saveFileName
                                                                   : editorRoot.gameTitle + " keeps the Academy's custom players and teams in " + editorRoot.saveFileName)
                          + ", in " + editorRoot.save.defaultFolder() + ". Open it (or a copy) to edit them. Close the game first: it "
                          + "rewrites the file when it saves."
                }
                RowLayout {
                    spacing: 8
                    SmallButton { text: "Open " + editorRoot.saveFileName; accent: true; onClicked: editorRoot.loadFile("") }
                    SmallButton { text: "Open a save..."; onClicked: openDialog.start() }
                }
            }
        }

        // Body
        SplitView {
            visible: editorRoot.save.loaded
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 4
                color: SplitHandle.hovered ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
            }

            // ---- Teams / players lists -----------------------------------------------------------------
            ColumnLayout {
                SplitView.preferredWidth: settings.splitWidth
                SplitView.minimumWidth: 300
                onWidthChanged: if (width > 0) settings.splitWidth = width
                spacing: 0

                EditorTabs {
                    id: listTabs
                    EditorTab { text: "Teams (" + editorRoot.teamRowsModel.length + ")" }
                    EditorTab { text: "Players (" + editorRoot.playerRowsModel.length + ")" }
                    Repeater {
                        model: editorRoot.isCareer ? ["Career"] : []
                        EditorTab {
                            required property var modelData
                            text: modelData
                            iconUrl: editorRoot.editorIconDir + "career_settings.png"
                        }
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    clip: true
                    currentIndex: listTabs.currentIndex

                    // Teams
                    ColumnLayout {
                        spacing: 0
                        SearchField {
                            id: teamSearch
                            Layout.margins: 8
                            onTextEdited: searchDelay.restart()
                            placeholderText: "Search teams (name or id)..."
                        }
                        // RLL4: new / delete / import / export (the C# editor's .tem / .ply files).
                        Flow {
                            visible: editorRoot.isLive
                            Layout.fillWidth: true
                            Layout.leftMargin: 8
                            Layout.rightMargin: 8
                            Layout.bottomMargin: 6
                            spacing: 6
                            SmallButton { text: "New"; tip: editorRoot.isRll3 ? "Add a new team" : "Add a new team (male)"; visible: editorRoot.save.canAddRecords === true; onClicked: editorRoot.addRecord("team", false) }
                            SmallButton { text: "New Female"; tip: "Add a new female team"; visible: !editorRoot.isRll3 && editorRoot.save.canAddRecords === true; enabled: !editorRoot.save.demo; onClicked: editorRoot.addRecord("team", true) }
                            SmallButton {
                                text: "Delete"
                                visible: editorRoot.save.canAddRecords === true
                                tip: "Delete the selected team"
                                enabled: editorRoot.kind === "team" && editorRoot.recordIndex >= 0
                                onClicked: deleteDialog.ask("team", editorRoot.recordIndex)
                            }
                            SmallButton {
                                text: "Import..."
                                tip: editorRoot.isCareer ? "Replace the selected team with a .tem file from the Academy editor or exported here (keeps its id)"
                                                         : "Replace the selected team with an exported .tem file (keeps its id)"
                                enabled: editorRoot.kind === "team" && editorRoot.recordIndex >= 0
                                onClicked: exportDialog.start("import")
                            }
                            SmallButton {
                                text: "Export..."
                                tip: "Save the selected team as a .tem file"
                                enabled: editorRoot.kind === "team" && editorRoot.recordIndex >= 0
                                onClicked: exportDialog.start("tem")
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
                            delegate: ListRow {
                                required property var modelData
                                required property int index
                                width: teamList.width
                                rowIndex: index
                                stripe: modelData.primaryColour
                                badgeText: modelData.abbreviation
                                imageUrl: modelData.logoUrl || ""
                                badgeColour: modelData.primaryColour
                                badgeTextColour: modelData.secondaryColour
                                current: editorRoot.kind === "team" && modelData.index === editorRoot.recordIndex
                                title: modelData.fullName !== "" ? modelData.fullName : "(no name)"
                                subtitle: "ID " + modelData.id + (modelData.shortName !== "" ? "  ·  " + modelData.shortName : "")
                                          + (modelData.female ? "  ·  Women" : "")
                                onClicked: editorRoot.openRecord("team", modelData.index)
                            }
                        }
                        Text {
                            visible: editorRoot.teamRowsModel.length === 0
                            Layout.fillWidth: true
                            Layout.margins: 12
                            wrapMode: Text.WordWrap
                            color: BtDarkStyle.textDim
                            font.pixelSize: 12
                            text: editorRoot.isLive ? "No teams in this save." : "No custom teams in this save (make them in the game's Academy)."
                        }
                    }

                    // Players
                    ColumnLayout {
                        spacing: 0
                        SearchField {
                            id: playerSearch
                            Layout.margins: 8
                            onTextEdited: searchDelay.restart()
                            placeholderText: "Search players (name or id)..."
                        }
                        // RLL4: new / delete / import / export (the C# editor's .tem / .ply files).
                        Flow {
                            visible: editorRoot.isLive
                            Layout.fillWidth: true
                            Layout.leftMargin: 8
                            Layout.rightMargin: 8
                            Layout.bottomMargin: 6
                            spacing: 6
                            SmallButton { text: "New"; tip: editorRoot.isRll3 ? "Add a new player" : "Add a new player (male)"; visible: editorRoot.save.canAddRecords === true; onClicked: editorRoot.addRecord("player", false) }
                            SmallButton { text: "New Female"; tip: "Add a new female player"; visible: !editorRoot.isRll3 && editorRoot.save.canAddRecords === true; enabled: !editorRoot.save.demo; onClicked: editorRoot.addRecord("player", true) }
                            SmallButton {
                                text: "Delete"
                                visible: editorRoot.save.canAddRecords === true
                                tip: "Delete the selected player"
                                enabled: editorRoot.kind === "player" && editorRoot.recordIndex >= 0
                                onClicked: deleteDialog.ask("player", editorRoot.recordIndex)
                            }
                            SmallButton {
                                text: "Import..."
                                tip: editorRoot.isCareer ? "Replace the selected player with a .ply file from the Academy editor or exported here (keeps their id and career data)"
                                                         : "Replace the selected player with an exported .ply file (keeps its id)"
                                enabled: editorRoot.kind === "player" && editorRoot.recordIndex >= 0
                                onClicked: exportDialog.start("import")
                            }
                            SmallButton {
                                text: "Export..."
                                tip: "Save the selected player as a .ply file"
                                enabled: editorRoot.kind === "player" && editorRoot.recordIndex >= 0
                                onClicked: exportDialog.start("tem")
                            }
                        }
                        ListView {
                            id: playerList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: editorRoot.playerRowsModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: ListRow {
                                required property var modelData
                                required property int index
                                width: playerList.width
                                rowIndex: index
                                stripe: modelData.female ? "#e91e63" : "#2196f3"
                                trailingIconUrl: editorRoot.isCareer ? editorRoot.editorIconDir + (modelData.female ? "female.png" : "male.png") : ""
                                badgeText: editorRoot.roleShort(modelData.role)
                                badgeTextColour: BtDarkStyle.accentColor
                                rating: modelData.rating
                                current: editorRoot.kind === "player" && modelData.index === editorRoot.recordIndex
                                title: (modelData.first + " " + modelData.last).trim() || "(no name)"
                                subtitle: "ID " + modelData.id + "  ·  " + modelData.role + (modelData.age > 0 ? "  ·  age " + modelData.age : "")
                                          + (modelData.originalId > 0 ? "  ·  replaces " + modelData.originalId : "")
                                          + (modelData.female ? "  ·  Female" : "")
                                onClicked: editorRoot.openRecord("player", modelData.index)
                            }
                        }
                        Text {
                            visible: editorRoot.playerRowsModel.length === 0
                            Layout.fillWidth: true
                            Layout.margins: 12
                            wrapMode: Text.WordWrap
                            color: BtDarkStyle.textDim
                            font.pixelSize: 12
                            text: editorRoot.isLive ? "No players in this save." : "No custom players in this save (make them in the game's Academy)."
                        }
                    }

                    // Career (career mode): its settings, player status, contracts, retirements, competitions.
                    ColumnLayout {
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            Layout.margins: 10
                            wrapMode: Text.WordWrap
                            color: BtDarkStyle.textDim
                            font.pixelSize: 11
                            text: editorRoot.save.careerInfo || ""
                            visible: text !== ""
                        }
                        ListView {
                            id: careerList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: editorRoot.careerItemsModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: ListRow {
                                required property var modelData
                                required property int index
                                width: careerList.width
                                rowIndex: index
                                stripe: BtDarkStyle.accentColor
                                badgeText: editorRoot.careerBadge(modelData.key)
                                iconUrl: editorRoot.careerIcon(modelData.key)
                                badgeTextColour: BtDarkStyle.accentColor
                                current: editorRoot.kind === "career" && index === editorRoot.recordIndex
                                title: modelData.title
                                subtitle: modelData.subtitle
                                onClicked: editorRoot.openRecord("career", index)
                            }
                        }
                    }
                }
            }

            // ---- One team / player --------------------------------------------------------------------------
            ColumnLayout {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 420
                spacing: 0

                Text {
                    visible: editorRoot.recordIndex < 0
                    text: "Pick a team or a player."
                    color: BtDarkStyle.textDim
                    font.pixelSize: 13
                    Layout.margins: 16
                }

                // Team banner (the team's colours, like the Database Editor)
                Rectangle {
                    id: teamBanner
                    visible: editorRoot.kind === "team" && editorRoot.recordIndex >= 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96
                    readonly property string primary: editorRoot.fieldValue("primaryRGBColour") || BtDarkStyle.bgDark
                    readonly property string secondary: editorRoot.fieldValue("secondaryRGBColour") || BtDarkStyle.borderDark
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: teamBanner.primary }
                        GradientStop { position: 0.55; color: BtDarkStyle.bgDark }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 3; color: teamBanner.secondary }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 16
                        // The team's emblem (its club icon from the loaded blobset); the abbreviation in the team's
                        // colours when it's not a club icon or the blobset isn't Rugby League 26's.
                        Image {
                            id: bannerLogo
                            visible: status === Image.Ready
                            // fieldValue() makes it follow logo edits.
                            source: editorRoot.kind === "team" && editorRoot.recordIndex >= 0 && editorRoot.fieldValue("logoNumber") !== undefined
                                    ? editorRoot.save.teamLogoUrl(editorRoot.recordIndex) : ""
                            asynchronous: true
                            Layout.preferredWidth: 76
                            Layout.preferredHeight: 76
                            sourceSize: Qt.size(152, 152)
                            fillMode: Image.PreserveAspectFit
                        }
                        Rectangle {
                            visible: !bannerLogo.visible
                            Layout.preferredWidth: 72
                            Layout.preferredHeight: 72
                            radius: 36
                            color: teamBanner.primary
                            border.color: teamBanner.secondary
                            border.width: 3
                            Text {
                                anchors.centerIn: parent
                                text: editorRoot.fieldValue("hudName") || "?"
                                color: teamBanner.secondary
                                font.pixelSize: 20
                                font.bold: true
                                style: Text.Outline
                                styleColor: "#80000000"
                            }
                        }
                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            Text {
                                text: editorRoot.fieldValue("fullName") || "(no name)"
                                color: BtDarkStyle.white
                                font.pixelSize: 22
                                font.bold: true
                                style: Text.Outline
                                styleColor: "#80000000"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: "Team ID " + editorRoot.fieldValue("id") + (editorRoot.isRll3 ? "" : "  ·  " + (editorRoot.fieldValue("sex") === true ? "Women" : "Men"))
                                      + (editorRoot.choiceName("logoNumber") !== "" ? "  ·  " + editorRoot.choiceName("logoNumber") : "")
                                      + (editorRoot.choiceName("clubType") !== "" ? "  ·  " + editorRoot.choiceName("clubType") : "")
                                      + (editorRoot.isLive ? "  ·  OVR " + (editorRoot.save.revision, editorRoot.save.teamRating(editorRoot.recordIndex)) : "")
                                      + "  ·  data offset " + editorRoot.save.recordOffset(editorRoot.kind, editorRoot.recordIndex)
                                color: BtDarkStyle.textMain
                                font.pixelSize: 12
                            }
                        }
                    }
                }

                // Player banner
                Rectangle {
                    visible: editorRoot.kind === "player" && editorRoot.recordIndex >= 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96
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
                                text: editorRoot.roleShort(editorRoot.choiceName("preferredRolePrimary"))
                                color: BtDarkStyle.accentColor
                                font.pixelSize: 20
                                font.bold: true
                            }
                        }
                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            Text {
                                text: ((editorRoot.fieldValue("firstname") || "") + " " + (editorRoot.fieldValue("surname") || "")).trim() || "(no name)"
                                color: BtDarkStyle.white
                                font.pixelSize: 22
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: "ID " + editorRoot.fieldValue("id") + (editorRoot.isLive && !editorRoot.isRl26 ? "  ·  born " : "  ·  age ") + editorRoot.fieldValue("age")
                                      + "  ·  " + editorRoot.choiceName("preferredRolePrimary")
                                      + (editorRoot.fieldValue("sex") === true ? "  ·  Female" : "")
                                      + "  ·  data offset " + editorRoot.save.recordOffset(editorRoot.kind, editorRoot.recordIndex)
                                color: BtDarkStyle.textDim
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        // Career: the player's form (the C# editor's arrows) and status.
                        ColumnLayout {
                            visible: editorRoot.isCareer
                            spacing: 2
                            Text { text: "FORM"; color: BtDarkStyle.textDim; font.pixelSize: 11; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                            RowLayout {
                                spacing: 4
                                Layout.alignment: Qt.AlignHCenter
                                Image {
                                    source: editorRoot.isCareer ? editorRoot.formIcon(Number(editorRoot.fieldValue("attributes.form")) || 0) : ""
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                }
                                Text {
                                    text: editorRoot.fieldValue("attributes.form")
                                    color: BtDarkStyle.textMain
                                    font.pixelSize: 14
                                    font.bold: true
                                }
                            }
                        }
                        ColumnLayout {
                            visible: editorRoot.isCareer
                            spacing: 2
                            Text { text: "STATUS"; color: BtDarkStyle.textDim; font.pixelSize: 11; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                            RowLayout {
                                spacing: 4
                                Layout.alignment: Qt.AlignHCenter
                                Image {
                                    source: editorRoot.isCareer ? editorRoot.statusIcon(Number(editorRoot.fieldValue("@status")) || 0) : ""
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                }
                                Text {
                                    text: editorRoot.choiceName("@status")
                                    color: BtDarkStyle.textMain
                                    font.pixelSize: 12
                                }
                            }
                        }
                        Item { visible: editorRoot.isCareer; Layout.preferredWidth: 8 }
                        ColumnLayout {
                            spacing: 0
                            Text { text: "OVR"; color: BtDarkStyle.textDim; font.pixelSize: 11; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                            Text {
                                text: editorRoot.currentRating
                                color: editorRoot.ratingColor(editorRoot.currentRating)
                                font.pixelSize: 30
                                font.bold: true
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }
                    }
                }

                // Career banner
                Rectangle {
                    visible: editorRoot.kind === "career" && editorRoot.recordIndex >= 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96
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
                            Image {
                                anchors.centerIn: parent
                                source: editorRoot.careerIcon((editorRoot.careerItemsModel[editorRoot.recordIndex] || {}).key)
                                width: 32
                                height: 32
                            }
                            Text {
                                visible: false
                                anchors.centerIn: parent
                                text: editorRoot.careerBadge((editorRoot.careerItemsModel[editorRoot.recordIndex] || {}).key)
                                color: BtDarkStyle.accentColor
                                font.pixelSize: 16
                                font.bold: true
                            }
                        }
                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            Text {
                                text: (editorRoot.careerItemsModel[editorRoot.recordIndex] || {}).title || ""
                                color: BtDarkStyle.white
                                font.pixelSize: 22
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: editorRoot.save.careerInfo || ""
                                color: BtDarkStyle.textDim
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }
                }

                EditorTabs {
                    id: detailTabs
                    visible: editorRoot.recordIndex >= 0
                    Repeater {
                        model: editorRoot.tabNames
                        EditorTab {
                            required property var modelData
                            text: modelData
                            iconUrl: editorRoot.tabIcon(modelData)
                        }
                    }
                    onCurrentIndexChanged: editorRoot.refreshRecord()
                }

                StackLayout {
                    visible: editorRoot.recordIndex >= 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    clip: true
                    currentIndex: editorRoot.pageIndex(editorRoot.page)

                    // Tab 1: Details / Identity
                    Page_ {
                        GridLayout {
                            Layout.fillWidth: true
                            columns: width > 980 ? 2 : 1
                            columnSpacing: 12
                            rowSpacing: 12
                            Repeater { model: editorRoot.tabGroups(0); delegate: groupCardComp }
                        }
                    }

                    // Tab 2: Stadiums / Ratings
                    Page_ {
                        GridLayout {
                            Layout.fillWidth: true
                            columns: width > 980 ? 2 : 1
                            columnSpacing: 12
                            rowSpacing: 12
                            Repeater { model: editorRoot.tabGroups(1); delegate: groupCardComp }
                        }
                    }

                    // Tab 3: Feeder Clubs / Appearance
                    StackLayout {
                        currentIndex: editorRoot.kind === "player" ? 1 : 0
                        Page_ {
                            GridLayout {
                                Layout.fillWidth: true
                                columns: width > 980 ? 2 : 1
                                columnSpacing: 12
                                rowSpacing: 12
                                Repeater { model: editorRoot.tabGroups(2); delegate: groupCardComp }
                            }
                        }
                        // The player's look (paCharacterVisualConfig), grouped like the Database Editor's.
                        ColumnLayout {
                            spacing: 0
                            Text {
                                visible: editorRoot.lookModel.length === 0
                                text: "No appearance data for this player."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 13
                                Layout.margins: 16
                            }
                            Flow {
                                Layout.fillWidth: true
                                Layout.margins: 8
                                spacing: 4
                                Repeater {
                                    model: editorRoot.lookGroups
                                    SmallButton {
                                        required property var modelData
                                        text: modelData
                                        accent: editorRoot.lookGroup === modelData
                                        onClicked: editorRoot.lookGroup = modelData
                                    }
                                }
                            }
                            Page_ {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width > 900 ? 2 : 1
                                    columnSpacing: 20
                                    rowSpacing: 4
                                    Repeater {
                                        model: editorRoot.lookModel.filter((f) => f.group === editorRoot.lookGroup)
                                        Loader {
                                            required property var modelData
                                            property var field: modelData
                                            Layout.fillWidth: true
                                            sourceComponent: editorRoot.componentFor(field)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Tab 4: Raw: the record's bytes, mapped ones highlighted. Click a byte to change it.
                    ColumnLayout {
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: 10
                            spacing: 10
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: BtDarkStyle.textDim
                                font.pixelSize: 11
                                text: editorRoot.recordIndex < 0 ? "" :
                                      "Record at " + editorRoot.save.recordOffset(editorRoot.kind, editorRoot.recordIndex) + " in the decoded data, 0x"
                                      + editorRoot.save.recordSize(editorRoot.kind).toString(16).toUpperCase() + " bytes. Highlighted bytes are mapped "
                                      + "fields. Click a byte to change it."
                            }
                            SmallButton {
                                text: "Export record..."
                                tip: "Save this record's bytes to a file (to compare in a hex editor)"
                                enabled: editorRoot.recordIndex >= 0
                                onClicked: exportDialog.start("record")
                            }
                        }
                        ListView {
                            id: rawList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: editorRoot.rawModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: RowLayout {
                                id: rawRow
                                required property var modelData
                                required property int index
                                width: rawList.width
                                height: 22
                                spacing: 0
                                Text {
                                    text: "+" + rawRow.modelData.offset.toString(16).toUpperCase().padStart(4, "0")
                                    color: BtDarkStyle.accentColor
                                    font.family: "Consolas"
                                    font.pixelSize: 12
                                    Layout.preferredWidth: 64
                                    Layout.leftMargin: 10
                                }
                                Repeater {
                                    model: rawRow.modelData.hex.length
                                    Rectangle {
                                        id: cell
                                        required property int index
                                        readonly property bool known: rawRow.modelData.known[index]
                                        readonly property int offset: rawRow.modelData.offset + index
                                        Layout.preferredWidth: 26
                                        Layout.preferredHeight: 20
                                        Layout.rightMargin: index === 7 ? 8 : 0
                                        radius: 2
                                        color: cellMouse.containsMouse ? BtDarkStyle.bgHover : cell.known ? BtDarkStyle.bgMedium : "transparent"
                                        Text {
                                            anchors.centerIn: parent
                                            text: rawRow.modelData.hex[cell.index]
                                            color: cell.known ? BtDarkStyle.accentColor
                                                 : rawRow.modelData.hex[cell.index] === "00" ? BtDarkStyle.textDim : BtDarkStyle.textMain
                                            font.family: "Consolas"
                                            font.pixelSize: 12
                                        }
                                        MouseArea {
                                            id: cellMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: byteDialog.edit(cell.offset, rawRow.modelData.hex[cell.index])
                                        }
                                    }
                                }
                                Text {
                                    text: rawRow.modelData.text
                                    color: BtDarkStyle.textDim
                                    font.family: "Consolas"
                                    font.pixelSize: 12
                                    Layout.leftMargin: 12
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }

                    // Tab 5: Roster (team): the players the team picks from, in order.
                    ColumnLayout {
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: 10
                            spacing: 8
                            PanelLabel { text: "Roster  ·  " + editorRoot.rosterModel.length + " / " + editorRoot.save.rosterMax() }
                            Text {
                                visible: editorRoot.rosterModel.length < editorRoot.save.rosterMin()
                                text: "The game needs at least " + editorRoot.save.rosterMin() + " players to play this team."
                                color: Material.color(Material.Orange)
                                font.pixelSize: 12
                            }
                            Item { Layout.fillWidth: true }
                            SmallButton {
                                text: "Add Player"
                                tip: "Add a player from the game's database or this save's Academy players"
                                accent: true
                                enabled: editorRoot.rosterModel.length < editorRoot.save.rosterMax()
                                onClicked: playerPicker.start()
                            }
                            SmallButton {
                                text: "Remove"
                                enabled: editorRoot.rosterSelected >= 0
                                onClicked: editorRoot.save.removeRosterPlayer(editorRoot.recordIndex, editorRoot.rosterSelected)
                            }
                            SmallButton {
                                text: "Up"
                                enabled: editorRoot.rosterSelected > 0
                                onClicked: {
                                    if (editorRoot.save.moveRosterPlayer(editorRoot.recordIndex, editorRoot.rosterSelected, -1))
                                        editorRoot.rosterSelected--
                                }
                            }
                            SmallButton {
                                text: "Down"
                                enabled: editorRoot.rosterSelected >= 0 && editorRoot.rosterSelected < editorRoot.rosterModel.length - 1
                                onClicked: {
                                    if (editorRoot.save.moveRosterPlayer(editorRoot.recordIndex, editorRoot.rosterSelected, 1))
                                        editorRoot.rosterSelected++
                                }
                            }
                            SmallButton {
                                visible: editorRoot.isLive
                                text: "Sort A-Z"
                                tip: "Sort the roster by last name"
                                enabled: editorRoot.rosterModel.length > 1
                                onClicked: editorRoot.save.sortRoster(editorRoot.recordIndex)
                            }
                        }
                        Text {
                            visible: editorRoot.kind === "team" && !editorRoot.save.databasePlayersAvailable()
                            Layout.fillWidth: true
                            Layout.leftMargin: 10
                            Layout.rightMargin: 10
                            wrapMode: Text.WordWrap
                            color: BtDarkStyle.textDim
                            font.pixelSize: 11
                            text: "Load " + editorRoot.gameTitle + "'s blobset to see the game's players' names (and to add them)."
                        }
                        ListView {
                            id: rosterList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: editorRoot.rosterModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: ListRow {
                                required property var modelData
                                required property int index
                                width: rosterList.width
                                rowIndex: index
                                current: editorRoot.rosterSelected === index
                                badgeText: modelData.role || "?"
                                rating: modelData.rating || -1
                                title: (index + 1) + ".  " + modelData.name
                                subtitle: "ID " + modelData.id + (modelData.club ? "  ·  " + modelData.club : "")
                                          + (modelData.age > 0 ? "  ·  Age " + modelData.age : "")
                                onClicked: editorRoot.rosterSelected = index
                                onDoubleClicked: {
                                    if (modelData.playerIndex !== undefined)
                                        editorRoot.openRecord("player", modelData.playerIndex)
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.margins: 10
                            wrapMode: Text.WordWrap
                            color: BtDarkStyle.textDim
                            font.pixelSize: 11
                            font.italic: true
                            text: "Double-click " + (editorRoot.isLive ? "a player from this save" : "an Academy player") + " to open them. The game wants " + editorRoot.save.rosterMin() + " to "
                                  + editorRoot.save.rosterMax() + " players."
                        }
                    }

                    // Tab 6: Jerseys (team): like the Database Editor's Jerseys tab.
                    ColumnLayout {
                        spacing: 0
                        Text {
                            visible: editorRoot.kitInfo.ok !== true
                            text: editorRoot.kitInfo.error || ""
                            color: BtDarkStyle.textDim
                            font.pixelSize: 13
                            Layout.margins: 16
                        }
                        // The jersey list | the jersey's fields, with a splitter between them (drag it to widen the list).
                        SplitView {
                            visible: editorRoot.kitInfo.ok === true
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            orientation: Qt.Horizontal
                            handle: Rectangle {
                                implicitWidth: 4
                                color: kitHandleHover.hovered ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                                HoverHandler { id: kitHandleHover; cursorShape: Qt.SplitHCursor }
                            }

                            // The jersey list and which jersey the game uses where.
                            ScrollView {
                                id: kitSide
                                SplitView.preferredWidth: 360
                                SplitView.minimumWidth: 240
                                SplitView.maximumWidth: 700
                                clip: true
                                contentWidth: availableWidth
                                contentHeight: kitSideColumn.implicitHeight
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                ColumnLayout {
                                    id: kitSideColumn
                                    width: kitSide.availableWidth
                                    spacing: 6
                                    PanelLabel {
                                        text: "Jerseys  ·  " + (editorRoot.kitInfo.count || 0) + " / " + (editorRoot.kitInfo.max || 0)
                                        Layout.margins: 10
                                    }
                                    Repeater {
                                        model: editorRoot.kitInfo.uniforms || []
                                        Rectangle {
                                            id: kitRow
                                            required property var modelData
                                            required property int index
                                            Layout.fillWidth: true
                                            implicitHeight: 34
                                            color: editorRoot.kitSelected === index ? BtDarkStyle.bgMedium
                                                 : kitMouse.containsMouse ? BtDarkStyle.bgHover
                                                 : (index % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
                                            Rectangle { visible: editorRoot.kitSelected === kitRow.index; width: 3; height: parent.height; color: BtDarkStyle.accentColor }
                                            MouseArea {
                                                id: kitMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                onClicked: { editorRoot.kitSelected = kitRow.index; editorRoot.refreshKit() }
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
                                                    text: kitRow.modelData.hidden ? "Hidden" : ""
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
                                            enabled: (editorRoot.kitInfo.count || 0) < (editorRoot.kitInfo.max || 0)
                                            onClicked: {
                                                const added = editorRoot.save.addJersey(editorRoot.recordIndex, editorRoot.kitSelected)
                                                if (added >= 0)
                                                    editorRoot.kitSelected = added
                                            }
                                        }
                                        SmallButton {
                                            text: "Remove"
                                            tip: "Remove the selected jersey"
                                            enabled: (editorRoot.kitInfo.count || 0) > 1
                                            onClicked: editorRoot.save.removeJersey(editorRoot.recordIndex, editorRoot.kitSelected)
                                        }
                                    }
                                    PanelLabel {
                                        text: "Used For"
                                        Layout.leftMargin: 10
                                        Layout.topMargin: 10
                                        visible: (editorRoot.kitInfo.slots || []).length > 0
                                    }
                                    Repeater {
                                        model: editorRoot.kitInfo.slots || []
                                        ChoiceEdit {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            Layout.leftMargin: 10
                                            Layout.rightMargin: 10
                                            // The label takes at most 45% so the box always has room (it was squeezed to
                                            // nothing in the narrow list).
                                            labelWidth: Math.min(170, Math.max(90, Math.round((kitSide.availableWidth - 30) * 0.45)))
                                            label: modelData.label
                                            choices: (editorRoot.kitInfo.uniforms || []).map((u) => ({ name: (u.index + 1) + ". " + u.name, value: u.index }))
                                            value: modelData.uniform
                                            onPick: (v) => editorRoot.save.setJerseySlot(editorRoot.recordIndex, modelData.slot, v)
                                        }
                                    }
                                    Item { Layout.preferredHeight: 10 }
                                }
                            }

                            // The selected jersey's fields, by group (the Database Editor's groups).
                            ColumnLayout {
                                SplitView.fillWidth: true
                                SplitView.minimumWidth: 300
                                spacing: 0
                                Flow {
                                    Layout.fillWidth: true
                                    Layout.margins: 8
                                    spacing: 4
                                    Repeater {
                                        model: editorRoot.kitGroups
                                        SmallButton {
                                            required property var modelData
                                            text: modelData
                                            accent: editorRoot.kitGroup === modelData
                                            onClicked: editorRoot.kitGroup = modelData
                                        }
                                    }
                                }
                                Page_ {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: width > 900 ? 2 : 1
                                        columnSpacing: 20
                                        rowSpacing: 4
                                        Repeater {
                                            model: editorRoot.kitFields.filter((f) => f.group === editorRoot.kitGroup)
                                            Loader {
                                                required property var modelData
                                                property var field: modelData
                                                Layout.fillWidth: true
                                                sourceComponent: editorRoot.componentFor(field)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Tab 7: Lineup (like the Database Editor's Lineups tab): the team's lineups, one at a time, and
                    // their roles. Click a row to pick the player.
                    ColumnLayout {
                        id: lineupPage
                        spacing: 0
                        property var lists: editorRoot.save.loaded && editorRoot.hasLineupApi && editorRoot.kind === "team"
                                            && editorRoot.save.revision >= 0 ? editorRoot.save.matchLists(editorRoot.recordIndex) : []
                        // RL26: the team's own lineups (named, up to 8): rename, add a copy.
                        readonly property bool namedLineups: typeof editorRoot.save.addLineup === "function"
                        readonly property var current: lists.length > 0 ? lists[Math.min(lineupKind.currentIndex, lists.length - 1)] : null
                        readonly property string list: current ? current.list : ""
                        property var rows: editorRoot.kind === "team" && editorRoot.recordIndex >= 0 && list !== "" && editorRoot.save.revision >= 0
                                           ? editorRoot.save.lineupRows(editorRoot.recordIndex, list) : []
                        property var roles: editorRoot.kind === "team" && editorRoot.recordIndex >= 0 && list !== "" && editorRoot.save.revision >= 0
                                            ? editorRoot.save.teamRoles(editorRoot.recordIndex, list) : []
                        readonly property bool present: editorRoot.kind === "team" && editorRoot.recordIndex >= 0 && list !== ""
                                                        && editorRoot.save.revision >= 0 && editorRoot.save.listPresent(editorRoot.recordIndex, list)
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
                            CheckBox {
                                text: lineupPage.namedLineups ? "Keep this lineup" : "The team has this lineup"
                                checked: lineupPage.present
                                font.pixelSize: 13
                                onToggled: {
                                    editorRoot.save.setListPresent(editorRoot.recordIndex, lineupPage.list, checked)
                                    checked = Qt.binding(() => lineupPage.present)
                                }
                            }
                            TextField {
                                id: lineupNameField
                                visible: lineupPage.namedLineups && lineupPage.list !== ""
                                Layout.preferredWidth: 160
                                Layout.preferredHeight: 34
                                maximumLength: 15
                                text: lineupPage.namedLineups && lineupPage.list !== "" && editorRoot.save.revision >= 0
                                      ? editorRoot.save.lineupName(editorRoot.recordIndex, lineupPage.list) : ""
                                color: BtDarkStyle.textMain
                                font.pixelSize: 13
                                leftPadding: 8
                                topPadding: 0
                                bottomPadding: 0
                                verticalAlignment: TextInput.AlignVCenter
                                background: Rectangle { color: BtDarkStyle.bgMedium; radius: 4; border.color: lineupNameField.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark }
                                onEditingFinished: editorRoot.save.setLineupName(editorRoot.recordIndex, lineupPage.list, text)
                                BtToolTip { text: "The lineup's name" }
                            }
                            SmallButton {
                                visible: lineupPage.namedLineups
                                text: "New Lineup"
                                tip: "Add a lineup, a copy of the first (as the game's New Lineup does)"
                                enabled: lineupPage.lists.length > 0 && lineupPage.lists.length < 8
                                onClicked: {
                                    const added = editorRoot.save.addLineup(editorRoot.recordIndex)
                                    if (added >= 0)
                                        Qt.callLater(() => lineupKind.currentIndex = added)
                                }
                            }
                            SmallButton {
                                visible: editorRoot.isLive
                                text: "Generate Best Lineup"
                                tip: editorRoot.isRll3 ? "Pick the lineup from the roster by rating and roles (captain: best rated, goal kicker and play makers: best footwork)"
                                                      : "Pick the lineups from the roster by rating and roles (captain: best rated, goal kicker: best place kick, play makers: best punt kicks)"
                                enabled: editorRoot.rosterModel.length > 0
                                onClicked: {
                                    const warning = editorRoot.save.generateBestLineup(editorRoot.recordIndex)
                                    editorRoot.setStatus(warning === "", warning === "" ? "Generated the best lineups." : warning)
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "Click a row to pick the player."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 11
                                font.italic: true
                                wrapMode: Text.WordWrap
                                Layout.maximumWidth: 330
                            }
                        }
                        Text {
                            visible: lineupPage.lists.length === 0
                            Layout.margins: 16
                            text: "This team has no lineups yet: make one in the game (the team's Lineups in the Academy), then reload the save."
                            color: BtDarkStyle.textDim
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
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
                                            onClicked: lineupPicker.pick(lineupRow.modelData.position, editorRoot.recordIndex,
                                                                         (id) => editorRoot.save.setLineupSlot(editorRoot.recordIndex, lineupPage.list, lineupRow.modelData.key, id))
                                            onDoubleClicked: if (lineupRow.modelData.playerIndex !== undefined) editorRoot.openRecord("player", lineupRow.modelData.playerIndex)
                                        }
                                        SpinBox {
                                            visible: lineupPage.current !== null && lineupPage.current.shirts
                                            Layout.preferredWidth: 110
                                            Layout.preferredHeight: 30
                                            from: 0
                                            to: 99
                                            editable: true
                                            enabled: lineupRow.modelData.present
                                            value: lineupRow.modelData.shirt || 0
                                            font.pixelSize: 12
                                            onValueModified: editorRoot.save.setLineupShirt(editorRoot.recordIndex, lineupPage.list, lineupRow.modelData.key, value)
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
                                        onClicked: lineupPicker.pick(modelData.name, editorRoot.recordIndex,
                                                                     (id) => editorRoot.save.setLineupSlot(editorRoot.recordIndex, lineupPage.list, modelData.key, id))
                                        onDoubleClicked: if (modelData.playerIndex !== undefined) editorRoot.openRecord("player", modelData.playerIndex)
                                    }
                                }
                            }
                        }
                    }

                    // Tab 8: Stats (RLL4 player): standard and Nines career stats.
                    Page_ {
                        GridLayout {
                            Layout.fillWidth: true
                            columns: width > 980 ? 2 : 1
                            columnSpacing: 12
                            rowSpacing: 12
                            Repeater { model: editorRoot.tabGroups(7); delegate: groupCardComp }
                        }
                    }

                    // Tab 9: Player Status (career): who's injured, suspended, retired or in an Origin camp.
                    ColumnLayout {
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: 10
                            spacing: 8
                            PanelLabel { text: "Player Status  ·  " + editorRoot.statusModel.length }
                            Item { Layout.fillWidth: true }
                            SmallButton {
                                text: "Open Player"
                                enabled: editorRoot.statusSelected >= 0 && (editorRoot.statusModel[editorRoot.statusSelected] || {}).playerIndex >= 0
                                onClicked: editorRoot.openRecord("player", editorRoot.statusModel[editorRoot.statusSelected].playerIndex)
                            }
                            SmallButton {
                                text: "Make Available"
                                tip: "Take the player off the list (fit, not suspended, not retired)"
                                enabled: editorRoot.statusSelected >= 0
                                onClicked: editorRoot.save.removeStatus(editorRoot.statusSelected)
                            }
                        }
                        ListView {
                            id: statusList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: editorRoot.statusModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: ListRow {
                                required property var modelData
                                required property int index
                                width: statusList.width
                                rowIndex: index
                                current: editorRoot.statusSelected === index
                                stripe: ["transparent", "#e53935", "#ff9800", "#757575", "#2196f3", "#8e24aa"][modelData.reason] || "transparent"
                                badgeText: modelData.role || "?"
                                iconUrl: editorRoot.statusIcon(modelData.reason)
                                rating: modelData.rating || -1
                                title: modelData.name
                                subtitle: modelData.reasonName + (modelData.unit !== "" ? "  ·  " + modelData.duration + " " + modelData.unit : "")
                                          + (modelData.detail !== "" ? "  ·  " + modelData.detail : "") + "  ·  ID " + modelData.id
                                onClicked: editorRoot.statusSelected = index
                                onDoubleClicked: {
                                    if (modelData.playerIndex >= 0)
                                        editorRoot.openRecord("player", modelData.playerIndex)
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.margins: 10
                            wrapMode: Text.WordWrap
                            color: BtDarkStyle.textDim
                            font.pixelSize: 11
                            font.italic: true
                            text: "Double-click a player to open them. A player's status (and how long, the injury) is on their Identity tab."
                        }
                    }

                    // Tab 10: Contracts (career): the contract changes waiting for the off-season.
                    ColumnLayout {
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: 10
                            spacing: 8
                            PanelLabel { text: "Player Contracts  ·  " + editorRoot.contractModel.length }
                            Item { Layout.fillWidth: true }
                            SmallButton {
                                text: "Open Player"
                                enabled: editorRoot.contractSelected >= 0 && (editorRoot.contractModel[editorRoot.contractSelected] || {}).playerIndex >= 0
                                onClicked: editorRoot.openRecord("player", editorRoot.contractModel[editorRoot.contractSelected].playerIndex)
                            }
                            SmallButton {
                                text: "Remove"
                                enabled: editorRoot.contractSelected >= 0
                                onClicked: editorRoot.save.removeContract(editorRoot.contractSelected)
                            }
                        }
                        Text {
                            visible: editorRoot.contractModel.length === 0
                            Layout.fillWidth: true
                            Layout.margins: 10
                            color: BtDarkStyle.textDim
                            font.pixelSize: 12
                            text: "No contract changes in this career yet (they're made in the off-season)."
                        }
                        ListView {
                            id: contractList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: editorRoot.contractModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: ListRow {
                                required property var modelData
                                required property int index
                                width: contractList.width
                                rowIndex: index
                                current: editorRoot.contractSelected === index
                                badgeText: modelData.role || "?"
                                rating: modelData.rating || -1
                                title: modelData.name
                                subtitle: (modelData.currentTeamName || "-") + "  →  " + (modelData.newTeamName || "-")
                                          + "  ·  salary " + modelData.newSalary
                                onClicked: {
                                    editorRoot.contractSelected = index
                                    editorRoot.refreshCareer()
                                }
                                onDoubleClicked: {
                                    if (modelData.playerIndex >= 0)
                                        editorRoot.openRecord("player", modelData.playerIndex)
                                }
                            }
                        }
                        Card {
                            visible: editorRoot.contractFieldsModel.length > 0
                            Layout.margins: 10
                            title: "Contract"
                            Repeater {
                                model: editorRoot.contractFieldsModel
                                Loader {
                                    required property var modelData
                                    property var field: modelData
                                    Layout.fillWidth: true
                                    sourceComponent: editorRoot.componentFor(field)
                                }
                            }
                        }
                    }

                    // Tab 11: Retirements (career): who retired at the end of the season.
                    ColumnLayout {
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: 10
                            spacing: 8
                            PanelLabel {
                                readonly property var date: (editorRoot.save.revision, editorRoot.kind === "career" ? editorRoot.save.retirementDate() : ({}))
                                text: "Retirements  ·  " + editorRoot.retirementModel.length
                                      + (date.year > 0 ? "  ·  " + date.day + "/" + date.month + "/" + date.year : "")
                            }
                            Item { Layout.fillWidth: true }
                            NumberEdit {
                                visible: editorRoot.retirementSelected >= 0
                                Layout.fillWidth: false
                                Layout.preferredWidth: 260
                                label: "Reason"
                                value: (editorRoot.retirementModel[editorRoot.retirementSelected] || {}).reason || 0
                                from: 0
                                to: 255
                                onCommit: (v) => editorRoot.save.setRetirementReason(editorRoot.retirementSelected, v)
                            }
                            SmallButton {
                                text: "Open Player"
                                enabled: editorRoot.retirementSelected >= 0 && (editorRoot.retirementModel[editorRoot.retirementSelected] || {}).playerIndex >= 0
                                onClicked: editorRoot.openRecord("player", editorRoot.retirementModel[editorRoot.retirementSelected].playerIndex)
                            }
                            SmallButton {
                                text: "Remove"
                                tip: "Take the player off the retirement list"
                                enabled: editorRoot.retirementSelected >= 0
                                onClicked: editorRoot.save.removeRetirement(editorRoot.retirementSelected)
                            }
                        }
                        ListView {
                            id: retirementList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: editorRoot.retirementModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: ListRow {
                                required property var modelData
                                required property int index
                                width: retirementList.width
                                rowIndex: index
                                current: editorRoot.retirementSelected === index
                                badgeText: modelData.role || "?"
                                rating: modelData.rating || -1
                                title: modelData.name
                                subtitle: "ID " + modelData.id + (modelData.age > 0 ? "  ·  Age " + modelData.age : "") + "  ·  reason " + modelData.reason
                                onClicked: editorRoot.retirementSelected = index
                                onDoubleClicked: {
                                    if (modelData.playerIndex >= 0)
                                        editorRoot.openRecord("player", modelData.playerIndex)
                                }
                            }
                        }
                    }

                    // Tab 12: Ladder (career): a competition's table from the season's results (the C# editor's).
                    ColumnLayout {
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: 10
                            spacing: 8
                            Image {
                                readonly property var comp: editorRoot.ladderCompsModel[editorRoot.ladderComp] || ({})
                                visible: status === Image.Ready
                                source: comp.logoUrl || ""
                                asynchronous: true
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                                sourceSize: Qt.size(80, 80)
                                fillMode: Image.PreserveAspectFit
                            }
                            ChoiceEdit {
                                Layout.fillWidth: false
                                Layout.preferredWidth: 480
                                label: "Competition"
                                labelWidth: 90
                                choices: editorRoot.ladderCompsModel.map((c) => ({ name: c.name, value: c.index }))
                                value: editorRoot.ladderComp
                                onPick: (v) => {
                                    editorRoot.ladderComp = v
                                    editorRoot.refreshCareer()
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }
                        // Header
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 28
                            color: BtDarkStyle.bgMedium
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 12
                                spacing: 6
                                Repeater {
                                    model: [["#", 28], ["Team", -1], ["P", 34], ["W", 34], ["D", 34], ["L", 34], ["B", 34], ["For", 44],
                                            ["Ag", 44], ["+/-", 48], ["Home", 50], ["Away", 50], ["Pts", 40]]
                                    Text {
                                        required property var modelData
                                        text: modelData[0]
                                        color: BtDarkStyle.textDim
                                        font.pixelSize: 11
                                        font.bold: true
                                        horizontalAlignment: modelData[1] < 0 ? Text.AlignLeft : Text.AlignHCenter
                                        Layout.fillWidth: modelData[1] < 0
                                        Layout.preferredWidth: modelData[1] < 0 ? -1 : modelData[1]
                                    }
                                }
                            }
                        }
                        ListView {
                            id: ladderList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: editorRoot.ladderModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                id: ladderRow
                                required property var modelData
                                required property int index
                                width: ladderList.width
                                height: 40
                                color: ladderMouse.containsMouse ? BtDarkStyle.bgHover : (index % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
                                Rectangle { width: 4; height: parent.height; color: ladderRow.modelData.finals ? BtDarkStyle.accentColor : "transparent" }
                                MouseArea {
                                    id: ladderMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onDoubleClicked: {
                                        if (ladderRow.modelData.teamIndex >= 0)
                                            editorRoot.openRecord("team", ladderRow.modelData.teamIndex)
                                    }
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 12
                                    spacing: 6
                                    Text {
                                        text: ladderRow.modelData.pos
                                        color: BtDarkStyle.textDim
                                        font.pixelSize: 12
                                        horizontalAlignment: Text.AlignHCenter
                                        Layout.preferredWidth: 28
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Image {
                                            id: ladderLogo
                                            visible: status === Image.Ready
                                            source: ladderRow.modelData.logoUrl || ""
                                            asynchronous: true
                                            Layout.preferredWidth: 28
                                            Layout.preferredHeight: 28
                                            sourceSize: Qt.size(56, 56)
                                            fillMode: Image.PreserveAspectFit
                                        }
                                        Rectangle {
                                            visible: !ladderLogo.visible
                                            Layout.preferredWidth: 28
                                            Layout.preferredHeight: 28
                                            radius: 14
                                            color: ladderRow.modelData.colour || BtDarkStyle.bgMedium
                                            border.color: BtDarkStyle.borderDark
                                            Text {
                                                anchors.centerIn: parent
                                                text: ladderRow.modelData.abbreviation || ""
                                                color: BtDarkStyle.white
                                                font.pixelSize: 9
                                                font.bold: true
                                                style: Text.Outline
                                                styleColor: "#80000000"
                                            }
                                        }
                                        Text {
                                            text: ladderRow.modelData.name
                                            color: BtDarkStyle.textMain
                                            font.pixelSize: 13
                                            font.bold: ladderRow.modelData.finals
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }
                                    Repeater {
                                        model: [["played", 34], ["wins", 34], ["draws", 34], ["lost", 34], ["byes", 34], ["for", 44],
                                                ["against", 44], ["diff", 48], ["home", 50], ["away", 50], ["points", 40]]
                                        Text {
                                            required property var modelData
                                            text: ladderRow.modelData[modelData[0]]
                                            color: modelData[0] === "points" ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                                            font.pixelSize: 12
                                            font.bold: modelData[0] === "points"
                                            horizontalAlignment: Text.AlignHCenter
                                            Layout.preferredWidth: modelData[1]
                                        }
                                    }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.margins: 10
                            wrapMode: Text.WordWrap
                            color: BtDarkStyle.textDim
                            font.pixelSize: 11
                            font.italic: true
                            text: "Worked out from the season's results so far (a bye is worth 2 points). The marked teams make the finals. Double-click a team to open it."
                        }
                    }

                    // Tab 13: Fixtures (career): a competition stage's matches, round by round.
                    ColumnLayout {
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.margins: 10
                            spacing: 8
                            Image {
                                readonly property var stage: editorRoot.fixtureStagesModel[editorRoot.fixtureStage] || ({})
                                visible: status === Image.Ready
                                source: stage.logoUrl || ""
                                asynchronous: true
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                                sourceSize: Qt.size(80, 80)
                                fillMode: Image.PreserveAspectFit
                            }
                            ChoiceEdit {
                                Layout.fillWidth: false
                                Layout.preferredWidth: 520
                                label: "Competition"
                                labelWidth: 90
                                choices: editorRoot.fixtureStagesModel.map((c) => ({ name: c.name, value: c.index }))
                                value: editorRoot.fixtureStage
                                onPick: (v) => {
                                    editorRoot.fixtureStage = v
                                    editorRoot.fixtureRound = 0
                                    editorRoot.refreshCareer()
                                }
                            }
                            ChoiceEdit {
                                Layout.fillWidth: false
                                Layout.preferredWidth: 280
                                label: "Round"
                                labelWidth: 50
                                choices: editorRoot.fixtureRoundsModel.map((r, i) => ({ name: r, value: i }))
                                value: editorRoot.fixtureRound
                                onPick: (v) => {
                                    editorRoot.fixtureRound = v
                                    editorRoot.refreshCareer()
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Text {
                            visible: editorRoot.fixtureModel.length === 0
                            Layout.fillWidth: true
                            Layout.margins: 10
                            color: BtDarkStyle.textDim
                            font.pixelSize: 12
                            text: "No matches drawn for this round yet."
                        }
                        Text {
                            visible: editorRoot.fixtureModel.length > 0
                            Layout.fillWidth: true
                            Layout.leftMargin: 10
                            Layout.bottomMargin: 4
                            color: BtDarkStyle.textDim
                            font.pixelSize: 11
                            font.italic: true
                            text: "Click or double-click a match for its match statistics and the players' ratings."
                        }
                        ListView {
                            id: fixtureList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: editorRoot.fixtureModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                id: fixtureRow
                                required property var modelData
                                required property int index
                                width: fixtureList.width
                                height: 56
                                color: fixtureMouse.containsMouse ? BtDarkStyle.bgHover : (index % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.bgDark)
                                MouseArea {
                                    id: fixtureMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: matchPopup.show(editorRoot.fixtureStage, fixtureRow.modelData.match)
                                    onDoubleClicked: if (!matchPopup.opened) matchPopup.show(editorRoot.fixtureStage, fixtureRow.modelData.match)
                                }
                                // Fixed width middle and the two teams anchored either side of it, so the names get all the room
                                // left (a layout let the stadium text widen the middle and squeeze the names).
                                TeamSide {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 14
                                    anchors.right: fixtureCentre.left
                                    anchors.rightMargin: 12
                                    anchors.verticalCenter: parent.verticalCenter
                                    teamName: fixtureRow.modelData.home || ""
                                    logoUrl: fixtureRow.modelData.homeLogoUrl || ""
                                    abbreviation: fixtureRow.modelData.homeAbbreviation || ""
                                    colour: fixtureRow.modelData.homeColour || ""
                                }
                                Column {
                                    id: fixtureCentre
                                    anchors.centerIn: parent
                                    width: Math.min(300, fixtureRow.width * 0.34)
                                    spacing: 1
                                    Text {
                                        width: parent.width
                                        text: fixtureRow.modelData.played ? fixtureRow.modelData.homeScore + "  -  " + fixtureRow.modelData.awayScore : "v"
                                        color: fixtureRow.modelData.played ? BtDarkStyle.white : BtDarkStyle.textDim
                                        font.pixelSize: fixtureRow.modelData.played ? 18 : 14
                                        font.bold: true
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                    Text {
                                        width: parent.width
                                        text: [fixtureRow.modelData.stadium, (fixtureRow.modelData.date + " " + fixtureRow.modelData.time).trim()]
                                              .filter((t) => t).join("  ·  ")
                                        color: BtDarkStyle.textDim
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                        horizontalAlignment: Text.AlignHCenter
                                    }
                                }
                                TeamSide {
                                    anchors.left: fixtureCentre.right
                                    anchors.leftMargin: 12
                                    anchors.right: parent.right
                                    anchors.rightMargin: 14
                                    anchors.verticalCenter: parent.verticalCenter
                                    mirrored: true
                                    teamName: fixtureRow.modelData.away || ""
                                    logoUrl: fixtureRow.modelData.awayLogoUrl || ""
                                    abbreviation: fixtureRow.modelData.awayAbbreviation || ""
                                    colour: fixtureRow.modelData.awayColour || ""
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
                    readonly property bool ok: editorRoot.localStatus !== "" ? editorRoot.localStatusOk : editorRoot.save.statusOk
                    color: ok ? BtDarkStyle.textDim : editorRoot.errorColor
                    text: "Status: " + (editorRoot.localStatus !== "" ? editorRoot.localStatus
                                        : editorRoot.save.statusText !== "" ? editorRoot.save.statusText : "Ready")
                }
                Text {
                    text: editorRoot.save.unsavedChanges ? "Unsaved changes" : ""
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
    // Field editors (Details tab)
    // ------------------------------------------------------------------------------------------------
    Component {
        id: textComp
        TextFieldEdit {
            label: parent ? parent.field.label : ""
            value: parent ? parent.field.value : ""
            maxLength: parent ? parent.field.max : 31
            offset: parent ? parent.field.offset : ""
            onCommit: (t) => editorRoot.commitField(parent.field, t)
        }
    }
    Component {
        id: numberComp
        NumberEdit {
            label: parent ? parent.field.label : ""
            value: parent ? parent.field.value : 0
            from: parent ? parent.field.min : 0
            to: parent ? Math.min(parent.field.max, 2147483647) : 255
            offset: parent ? parent.field.offset : ""
            onCommit: (v) => editorRoot.commitField(parent.field, v)
        }
    }
    Component {
        id: bigComp
        BigNumberEdit {
            label: parent ? parent.field.label : ""
            value: parent ? parent.field.value : 0
            readOnly: parent ? parent.field.readOnly : false
            offset: parent ? parent.field.offset : ""
            onCommit: (v) => editorRoot.commitField(parent.field, v)
        }
    }
    // A card of one field group: its fields with the editor each needs.
    Component {
        id: groupCardComp
        Card {
            id: groupCard
            required property var modelData
            readonly property var items: editorRoot.fieldsModel.filter((f) => f.group === groupCard.modelData)
            visible: items.length > 0
            title: modelData
            Layout.alignment: Qt.AlignTop
            Repeater {
                model: groupCard.items
                Loader {
                    required property var modelData
                    property var field: modelData
                    Layout.fillWidth: true
                    sourceComponent: editorRoot.componentFor(field)
                }
            }
        }
    }
    Component {
        id: sliderComp
        StatSlider {
            label: parent ? parent.field.label : ""
            value: parent ? parent.field.value : 0
            to: parent ? parent.field.max : 99
            offset: parent ? parent.field.offset : ""
            onCommit: (v) => editorRoot.commitField(parent.field, v)
        }
    }
    Component {
        id: choiceComp
        ChoiceEdit {
            label: parent ? parent.field.label : ""
            choices: parent ? parent.field.choices : []
            value: parent ? parent.field.value : 0
            offset: parent ? parent.field.offset : ""
            onPick: (v) => editorRoot.commitField(parent.field, v)
        }
    }
    Component {
        id: colourComp
        ColourEdit {
            label: parent ? parent.field.label : ""
            value: parent ? parent.field.value : "#000000"
            offset: parent ? parent.field.offset : ""
            onCommit: (c) => editorRoot.commitField(parent.field, c)
        }
    }
    Component {
        id: paletteComp
        PaletteEdit {
            label: parent ? parent.field.label : ""
            choices: parent ? parent.field.choices : []
            value: parent ? parent.field.value : 0
            offset: parent ? parent.field.offset : ""
            onPick: (v) => editorRoot.commitField(parent.field, v)
        }
    }
    Component {
        id: boolComp
        CheckEdit {
            label: parent ? parent.field.label : ""
            value: parent ? parent.field.value === true : false
            offset: parent ? parent.field.offset : ""
            onCommit: (on) => editorRoot.commitField(parent.field, on)
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Career: a fixture's match statistics (the C# career editors' Match Statistics): team v team, the scorers and
    // man of the match, and each side's players with their match ratings.
    // ------------------------------------------------------------------------------------------------
    Popup {
        id: matchPopup
        property var stats: ({})
        readonly property var home: stats.home || ({})
        readonly property var away: stats.away || ({})
        readonly property var motm: stats.motm || ({})
        property int view: 0          // 0 = the match, 1 = home's players, 2 = away's players.
        function show(stage, match) {
            stats = editorRoot.save.matchStats(stage, match)
            view = 0
            open()
        }
        // A side's scorers of a stat: [{name, count}], most first.
        function scorers(players, key) {
            return (players || []).filter((p) => p[key] > 0).sort((a, b) => b[key] - a[key]).map((p) => ({ name: p.name, count: p[key] }))
        }
        // The big and small halves of a team's name ("Cronulla-Sutherland" over "Sharks"), as the C# editor shows it.
        function clubOf(t) { return t.club || t.name || "" }
        function locationOf(t) { return t.club ? (t.location || "") : "" }
        readonly property var scorerKinds: [["Tries", "tries"], ["Goals", "conversions"], ["Penalty Goals", "penaltyGoals"], ["Field Goals", "fieldGoals"]]
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(editorRoot.width - 40, 1240)
        height: Math.min(editorRoot.height - 40, 920)
        modal: true
        focus: true
        padding: 12
        background: Rectangle {
            color: BtDarkStyle.bgDarkest
            border.color: BtDarkStyle.borderDark
            radius: 8
        }
        contentItem: ColumnLayout {
            spacing: 10
            // The competition, stadium, date and round; the competition's logo in the middle.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 92
                color: BtDarkStyle.bgDark
                border.color: BtDarkStyle.borderDark
                radius: 6
                ColumnLayout {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width / 2 - 70
                    spacing: 2
                    Text {
                        text: matchPopup.stats.competition || ""
                        color: BtDarkStyle.white
                        font.pixelSize: 16
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: matchPopup.stats.stadium || ""
                        color: BtDarkStyle.textMain
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: ((matchPopup.stats.date || "") + "  " + (matchPopup.stats.time || "")).trim()
                        color: BtDarkStyle.textMain
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                    Text {
                        text: matchPopup.stats.round || ""
                        color: BtDarkStyle.textDim
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                }
                Image {
                    anchors.centerIn: parent
                    width: 76
                    height: 76
                    visible: status === Image.Ready
                    source: matchPopup.stats.competitionLogoUrl || ""
                    asynchronous: true
                    sourceSize: Qt.size(152, 152)
                    fillMode: Image.PreserveAspectFit
                }
                SmallButton {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    text: "Close"
                    onClicked: matchPopup.close()
                }
            }
            // The teams and the score.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 100
                color: BtDarkStyle.bgDark
                border.color: BtDarkStyle.borderDark
                radius: 6
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 12
                    TeamBadge {
                        size: 72
                        logoUrl: matchPopup.home.logoUrl || ""
                        abbreviation: matchPopup.home.abbreviation || ""
                        colour: matchPopup.home.colour || ""
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 0
                        Text {
                            visible: text !== ""
                            text: matchPopup.locationOf(matchPopup.home)
                            color: BtDarkStyle.textMain
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: matchPopup.clubOf(matchPopup.home)
                            color: BtDarkStyle.white
                            font.pixelSize: 24
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Text {
                        text: matchPopup.stats.played ? String(matchPopup.home.score) : ""
                        color: BtDarkStyle.white
                        font.pixelSize: 44
                        font.bold: true
                    }
                    Text {
                        text: matchPopup.stats.ok === false ? "Unreadable" : matchPopup.stats.played ? "Full Time" : "Not Played"
                        color: matchPopup.stats.played ? BtDarkStyle.accentColor : BtDarkStyle.textDim
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        Layout.preferredWidth: 110
                    }
                    Text {
                        text: matchPopup.stats.played ? String(matchPopup.away.score) : ""
                        color: BtDarkStyle.white
                        font.pixelSize: 44
                        font.bold: true
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 0
                        Text {
                            visible: text !== ""
                            text: matchPopup.locationOf(matchPopup.away)
                            color: BtDarkStyle.textMain
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: matchPopup.clubOf(matchPopup.away)
                            color: BtDarkStyle.white
                            font.pixelSize: 24
                            font.bold: true
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignRight
                            Layout.fillWidth: true
                        }
                    }
                    TeamBadge {
                        size: 72
                        logoUrl: matchPopup.away.logoUrl || ""
                        abbreviation: matchPopup.away.abbreviation || ""
                        colour: matchPopup.away.colour || ""
                    }
                }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: matchPopup.view
                // The match: scorers either side of the team stats, then the player buttons and man of the match.
                ColumnLayout {
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 10
                        // Home's scorers.
                        Flickable {
                            id: homeScorersFlick
                            Layout.preferredWidth: 250
                            Layout.fillHeight: true
                            contentWidth: width
                            contentHeight: homeScorers.implicitHeight
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            ColumnLayout {
                                id: homeScorers
                                width: homeScorersFlick.width - 10
                                spacing: 8
                                Repeater {
                                    model: matchPopup.scorerKinds
                                    ScorerBox {
                                        required property var modelData
                                        title: modelData[0]
                                        scorers: matchPopup.scorers(matchPopup.stats.homePlayers, modelData[1])
                                    }
                                }
                            }
                        }
                        // Team stats.
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: BtDarkStyle.bgDark
                            border.color: BtDarkStyle.borderDark
                            radius: 6
                            Text {
                                id: teamStatsTitle
                                anchors.top: parent.top
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.topMargin: 8
                                text: "Team Stats"
                                color: BtDarkStyle.accentColor
                                font.pixelSize: 15
                                font.bold: true
                            }
                            Text {
                                visible: matchPopup.stats.played !== true
                                anchors.centerIn: parent
                                text: matchPopup.stats.ok === false ? "This match couldn't be read." : "This match hasn't been played yet."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 13
                            }
                            ListView {
                                id: teamStatList
                                visible: matchPopup.stats.played === true
                                anchors.fill: parent
                                anchors.topMargin: teamStatsTitle.height + 16
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                anchors.bottomMargin: 8
                                clip: true
                                spacing: 10
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                model: (matchPopup.stats.rows || []).filter((r) => r.label !== "Score")
                                delegate: ColumnLayout {
                                    id: statRow
                                    required property var modelData
                                    readonly property string style: modelData.style || ""
                                    width: teamStatList.width - 12
                                    spacing: 4
                                    // Plain: home's figure, the stat, away's figure, and a thin bar.
                                    RowLayout {
                                        Layout.fillWidth: true
                                        visible: statRow.style !== "donut"
                                        Text {
                                            text: statRow.modelData.home
                                            color: statRow.modelData.share > 0.5 ? BtDarkStyle.white : BtDarkStyle.textMain
                                            font.pixelSize: 14
                                            font.bold: statRow.modelData.share > 0.5
                                            Layout.preferredWidth: 110
                                        }
                                        Text {
                                            text: statRow.modelData.label
                                            color: BtDarkStyle.textDim
                                            font.pixelSize: 13
                                            font.bold: true
                                            horizontalAlignment: Text.AlignHCenter
                                            Layout.fillWidth: true
                                        }
                                        Text {
                                            text: statRow.modelData.away
                                            color: statRow.modelData.share < 0.5 ? BtDarkStyle.white : BtDarkStyle.textMain
                                            font.pixelSize: 14
                                            font.bold: statRow.modelData.share < 0.5
                                            horizontalAlignment: Text.AlignRight
                                            Layout.preferredWidth: 110
                                        }
                                    }
                                    Rectangle {
                                        visible: statRow.style === ""
                                        Layout.fillWidth: true
                                        implicitHeight: 5
                                        radius: 2
                                        color: matchPopup.away.colour || BtDarkStyle.borderDark
                                        Rectangle {
                                            width: parent.width * Math.max(0, Math.min(1, statRow.modelData.share))
                                            height: parent.height
                                            radius: 2
                                            color: matchPopup.home.colour || BtDarkStyle.accentColor
                                        }
                                    }
                                    // Time in possession: one bar in the two colours.
                                    Rectangle {
                                        visible: statRow.style === "split"
                                        Layout.fillWidth: true
                                        implicitHeight: 18
                                        radius: 4
                                        clip: true
                                        color: matchPopup.away.colour || BtDarkStyle.borderDark
                                        Rectangle {
                                            width: parent.width * Math.max(0, Math.min(1, statRow.modelData.share))
                                            height: parent.height
                                            color: matchPopup.home.colour || BtDarkStyle.accentColor
                                        }
                                    }
                                    // Completions: a ring per team.
                                    Text {
                                        visible: statRow.style === "donut"
                                        text: statRow.modelData.label
                                        color: BtDarkStyle.textDim
                                        font.pixelSize: 13
                                        font.bold: true
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    RowLayout {
                                        visible: statRow.style === "donut"
                                        Layout.fillWidth: true
                                        Item { Layout.fillWidth: true }
                                        Donut {
                                            percent: statRow.modelData.homePct || 0
                                            colour: matchPopup.home.colour || ""
                                            detail: statRow.modelData.homeDetail || ""
                                        }
                                        Item { Layout.fillWidth: true }
                                        Donut {
                                            percent: statRow.modelData.awayPct || 0
                                            colour: matchPopup.away.colour || ""
                                            detail: statRow.modelData.awayDetail || ""
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                }
                            }
                        }
                        // Away's scorers.
                        Flickable {
                            id: awayScorersFlick
                            Layout.preferredWidth: 250
                            Layout.fillHeight: true
                            contentWidth: width
                            contentHeight: awayScorers.implicitHeight
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            ColumnLayout {
                                id: awayScorers
                                width: awayScorersFlick.width - 10
                                spacing: 8
                                Repeater {
                                    model: matchPopup.scorerKinds
                                    ScorerBox {
                                        required property var modelData
                                        mirrored: true
                                        title: modelData[0]
                                        scorers: matchPopup.scorers(matchPopup.stats.awayPlayers, modelData[1])
                                    }
                                }
                            }
                        }
                    }
                    // The player stat buttons and man of the match.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        HeaderButton {
                            Layout.preferredWidth: 250
                            Layout.preferredHeight: 56
                            enabled: matchPopup.stats.played === true
                            accent: true
                            text: (matchPopup.home.name || "Home") + " - Player Stats / Ratings"
                            contentItem: Text {
                                text: (matchPopup.home.name || "Home") + "\nPlayer Stats / Ratings"
                                color: BtDarkStyle.accentColor
                                font.pixelSize: 13
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            onClicked: matchPopup.view = 1
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 56
                            color: BtDarkStyle.bgDark
                            border.color: BtDarkStyle.borderDark
                            radius: 6
                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 12
                                Image {
                                    source: editorRoot.editorIconDir + "good.png"
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 20
                                }
                                ColumnLayout {
                                    spacing: 0
                                    Text {
                                        text: "Man Of The Match"
                                        color: BtDarkStyle.accentColor
                                        font.pixelSize: 12
                                        font.bold: true
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                    Text {
                                        text: matchPopup.motm.name ? (matchPopup.motm.shirt ? matchPopup.motm.shirt + " - " : "") + matchPopup.motm.name : "-"
                                        color: BtDarkStyle.white
                                        font.pixelSize: 16
                                        font.bold: true
                                        Layout.alignment: Qt.AlignHCenter
                                    }
                                }
                                TeamBadge {
                                    visible: !!matchPopup.motm.name
                                    size: 40
                                    logoUrl: matchPopup.motm.logoUrl || ""
                                    abbreviation: matchPopup.motm.abbreviation || ""
                                    colour: matchPopup.motm.colour || ""
                                }
                            }
                        }
                        HeaderButton {
                            Layout.preferredWidth: 250
                            Layout.preferredHeight: 56
                            enabled: matchPopup.stats.played === true
                            accent: true
                            text: (matchPopup.away.name || "Away") + " - Player Stats / Ratings"
                            contentItem: Text {
                                text: (matchPopup.away.name || "Away") + "\nPlayer Stats / Ratings"
                                color: BtDarkStyle.accentColor
                                font.pixelSize: 13
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            onClicked: matchPopup.view = 2
                        }
                    }
                }
                // A side's players (double-click one to open them).
                MatchPlayersView {
                    team: matchPopup.home
                    players: matchPopup.stats.homePlayers || []
                    onBack: matchPopup.view = 0
                    onOpenPlayer: (playerIndex) => {
                        matchPopup.close()
                        editorRoot.openRecord("player", playerIndex)
                    }
                }
                MatchPlayersView {
                    team: matchPopup.away
                    players: matchPopup.stats.awayPlayers || []
                    onBack: matchPopup.view = 0
                    onOpenPlayer: (playerIndex) => {
                        matchPopup.close()
                        editorRoot.openRecord("player", playerIndex)
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Menus / dialogs
    // ------------------------------------------------------------------------------------------------
    Menu {
        id: fileMenu
        width: 280
        MenuItem {
            text: "Open " + editorRoot.saveFileName
            icon.source: editorRoot.iconDir + "open_32.png"
            icon.color: "transparent"
            onTriggered: editorRoot.guarded(() => editorRoot.loadFile(""))
        }
        MenuItem {
            text: "Open a Save..."
            icon.source: editorRoot.iconDir + "open_32.png"
            icon.color: "transparent"
            onTriggered: editorRoot.guarded(() => openDialog.start())
        }
        MenuItem {
            text: "Reload"
            icon.source: editorRoot.iconDir + "reset_32.png"
            icon.color: "transparent"
            enabled: editorRoot.save.loaded
            onTriggered: editorRoot.guarded(() => editorRoot.loadFile(editorRoot.save.filePath))
        }
        MenuSeparator {}
        MenuItem {
            text: "Save As..."
            icon.source: editorRoot.iconDir + "save_32.png"
            icon.color: "transparent"
            enabled: editorRoot.save.loaded
            onTriggered: exportDialog.start("saveAs")
        }
        MenuItem {
            text: "Export Decoded Data..."
            icon.source: editorRoot.iconDir + "extract_32.png"
            icon.color: "transparent"
            enabled: editorRoot.save.loaded
            onTriggered: exportDialog.start("decoded")
        }
    }

    FileDialog {
        id: openDialog
        function start() {
            currentFolder = editorRoot.save.loaded ? editorRoot.folderUrl(editorRoot.save.filePath) : editorRoot.folderUrl(editorRoot.save.defaultFile())
            fileMode = FileDialog.OpenFile
            nameFilters = [editorRoot.gameTitle + " saves (*.SAV)", "All files (*)"]
            title = "Open a " + editorRoot.gameTitle + " save"
            open()
        }
        onAccepted: editorRoot.loadFile(selectedFile.toString())
    }

    FileDialog {
        id: exportDialog
        property string mode: "saveAs"
        function start(m) {
            mode = m
            const folder = editorRoot.folderUrl(editorRoot.save.filePath)
            currentFolder = folder
            fileMode = FileDialog.SaveFile
            const ext = editorRoot.kind === "team" ? "tem" : "ply"
            const what = editorRoot.kind === "team" ? "Team" : "Player"
            if (m === "import" || m === "tem") {
                fileMode = m === "import" ? FileDialog.OpenFile : FileDialog.SaveFile
                nameFilters = [what + " files (*." + ext + ")", "All files (*)"]
                defaultSuffix = ext
                title = m === "import" ? "Import a " + what.toLowerCase() + " over the selected one" : "Export this " + what.toLowerCase()
                selectedFile = m === "import" ? "" : folder + "/" + encodeURIComponent(editorRoot.save.recordFileName(editorRoot.kind, editorRoot.recordIndex))
            } else if (m === "saveAs") {
                nameFilters = [editorRoot.gameTitle + " saves (*.SAV)", "All files (*)"]
                defaultSuffix = "SAV"
                title = "Save the save as"
                selectedFile = folder + "/" + encodeURIComponent(editorRoot.save.fileName)
            } else if (m === "decoded") {
                nameFilters = ["Binary (*.bin)", "All files (*)"]
                defaultSuffix = "bin"
                title = "Export the decoded data"
                selectedFile = folder + "/" + encodeURIComponent(editorRoot.save.fileName.replace(/\.SAV$/i, "") + ".decoded.bin")
            } else {
                nameFilters = ["Binary (*.bin)", "All files (*)"]
                defaultSuffix = "bin"
                title = "Export this record"
                const name = editorRoot.kind === "team" ? editorRoot.fieldValue("fullName")
                                                        : editorRoot.fieldValue("firstname") + " " + editorRoot.fieldValue("surname")
                selectedFile = folder + "/" + encodeURIComponent((editorRoot.kind + " " + editorRoot.fieldValue("id") + " " + name)
                                                                 .trim().replace(/[\\/:*?"<>|]/g, "_") + ".bin")
            }
            open()
        }
        onAccepted: {
            const url = selectedFile.toString()
            const file = decodeURIComponent(url.replace("file:///", ""))
            if (mode === "saveAs") {
                if (editorRoot.save.saveAs(url))
                    editorRoot.rememberFile(editorRoot.save.filePath)
                return
            }
            if (mode === "import") {
                const failed = editorRoot.save.importRecord(editorRoot.kind, editorRoot.recordIndex, url)
                if (failed !== "")
                    editorRoot.setStatus(false, failed)
                return
            }
            const error = mode === "decoded" ? editorRoot.save.exportDecoded(url)
                                             : editorRoot.save.exportRecord(editorRoot.kind, editorRoot.recordIndex, url)
            editorRoot.setStatus(error === "", error === "" ? "Exported to " + file : error)
        }
    }

    // RLL4: delete a team / player.
    Dialog {
        id: deleteDialog
        property string what: ""
        property int index: -1
        function ask(k, i) {
            what = k
            index = i
            open()
        }
        title: "Delete this " + what + "?"
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
            text: deleteDialog.title
            color: BtDarkStyle.accentColor
            font.bold: true
            font.pixelSize: 15
            leftPadding: 20
            topPadding: 16
        }
        contentItem: Text {
            wrapMode: Text.WordWrap
            color: BtDarkStyle.textMain
            font.pixelSize: 13
            text: deleteDialog.what === "team"
                  ? "The team is removed from the save (when you save). Competitions that use it may need it again."
                  : "The player is removed from the save (when you save). Teams that have them in their roster or lineups still list the player's id: check them after."
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
                    deleteDialog.close()
                    if (editorRoot.save.deleteRecord(deleteDialog.what, deleteDialog.index)) {
                        const count = deleteDialog.what === "team" ? editorRoot.save.teamCount : editorRoot.save.playerCount
                        editorRoot.reloadLists()
                        editorRoot.openRecord(deleteDialog.what, Math.min(deleteDialog.index, count - 1))
                    }
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

    // Lineups: pick a player (the Database Editor's player picker: the squad, or everyone).
    Popup {
        id: lineupPicker
        property string title: ""
        property int team: -1
        property var callback: null
        property var rows: []
        function pick(heading, teamIndex, onPicked) {
            title = heading
            team = teamIndex
            callback = onPicked
            lineupPickerSquad.checked = teamIndex >= 0
            lineupPickerSearch.text = ""
            refresh()
            open()
            lineupPickerSearch.forceActiveFocus()
        }
        function refresh() {
            rows = editorRoot.save.pickerRows(lineupPickerSearch.text, team, lineupPickerSquad.checked)
        }
        function choose(id) {
            close()
            if (callback)
                callback(id)
            callback = null
        }
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
        contentItem: ColumnLayout {
            spacing: 8
            Text { text: lineupPicker.title; color: BtDarkStyle.accentColor; font.bold: true; font.pixelSize: 15 }
            RowLayout {
                spacing: 8
                TextField {
                    id: lineupPickerSearch
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
                    onTextEdited: lineupPickerDelay.restart()
                    Keys.onReturnPressed: if (lineupPicker.rows.length > 0) lineupPicker.choose(lineupPicker.rows[0].id)
                    Timer { id: lineupPickerDelay; interval: 200; onTriggered: lineupPicker.refresh() }
                }
                CheckBox {
                    id: lineupPickerSquad
                    text: "Squad only"
                    visible: lineupPicker.team >= 0
                    onToggled: lineupPicker.refresh()
                }
            }
            ListView {
                id: lineupPickerList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: lineupPicker.rows
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: PlayerRow {
                    required property var modelData
                    required property int index
                    width: lineupPickerList.width
                    rowIndex: index
                    leading: modelData.clubName
                    row: ({ id: modelData.id, first: modelData.first, last: modelData.last, known: true,
                            roles: modelData.roleName + (modelData.age > 0 ? "  ·  age " + modelData.age : ""), rating: modelData.rating })
                    onClicked: lineupPicker.choose(modelData.id)
                }
            }
            RowLayout {
                Text {
                    text: lineupPicker.rows.length + " players  ·  click one to pick it"
                    color: BtDarkStyle.textDim
                    font.pixelSize: 11
                    Layout.fillWidth: true
                }
                SmallButton { text: "Cancel"; onClicked: lineupPicker.close() }
            }
        }
    }

    // Roster: pick a player to add (the game's database players and this save's Academy players).
    Dialog {
        id: playerPicker
        property var rows: []
        property int selected: -1
        function start() {
            pickSearch.text = ""
            reload()
            open()
            pickSearch.forceActiveFocus()
        }
        function reload() {
            rows = editorRoot.save.pickablePlayers(pickSearch.text)
            selected = rows.length > 0 ? 0 : -1
        }
        function add() {
            if (selected < 0 || selected >= rows.length)
                return
            if (editorRoot.save.addRosterPlayer(editorRoot.recordIndex, rows[selected].id))
                editorRoot.rosterSelected = editorRoot.rosterModel.length
        }
        title: "Add a player to the roster"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(620, parent ? parent.width - 40 : 620)
        height: Math.min(640, parent ? parent.height - 40 : 640)
        padding: 12
        background: Rectangle {
            color: BtDarkStyle.bgDark
            border.color: BtDarkStyle.accentColor
            border.width: 1
            radius: 6
        }
        header: Text {
            text: playerPicker.title + "  ·  " + editorRoot.rosterModel.length + " / " + editorRoot.save.rosterMax()
            color: BtDarkStyle.accentColor
            font.bold: true
            font.pixelSize: 15
            leftPadding: 20
            topPadding: 16
        }
        contentItem: ColumnLayout {
            spacing: 8
            SearchField {
                id: pickSearch
                Layout.fillWidth: true
                placeholderText: "Search players (name, club or id)..."
                onTextEdited: pickDelay.restart()
                Keys.onReturnPressed: playerPicker.add()
                Keys.onEnterPressed: playerPicker.add()
                Timer {
                    id: pickDelay
                    interval: 200
                    onTriggered: playerPicker.reload()
                }
            }
            Text {
                visible: !editorRoot.save.databasePlayersAvailable()
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: BtDarkStyle.textDim
                font.pixelSize: 11
                text: "Only this save's players are listed: load " + editorRoot.gameTitle + "'s blobset for the game's players."
            }
            ListView {
                id: pickList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: playerPicker.rows
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: ListRow {
                    required property var modelData
                    required property int index
                    width: pickList.width
                    rowIndex: index
                    current: playerPicker.selected === index
                    badgeText: modelData.role || "?"
                    rating: modelData.rating || -1
                    title: modelData.name
                    subtitle: "ID " + modelData.id + (modelData.club ? "  ·  " + modelData.club : "")
                              + (modelData.age > 0 ? "  ·  Age " + modelData.age : "")
                    onClicked: playerPicker.selected = index
                    onDoubleClicked: { playerPicker.selected = index; playerPicker.add() }
                }
            }
        }
        footer: RowLayout {
            spacing: 8
            Text {
                Layout.leftMargin: 16
                Layout.bottomMargin: 14
                text: "Double-click to add. Stays open to add more."
                color: BtDarkStyle.textDim
                font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            HeaderButton {
                text: "Add"
                accent: true
                implicitHeight: 34
                Layout.bottomMargin: 14
                enabled: playerPicker.selected >= 0 && editorRoot.rosterModel.length < editorRoot.save.rosterMax()
                onClicked: playerPicker.add()
            }
            HeaderButton {
                text: "Close"
                implicitHeight: 34
                Layout.rightMargin: 16
                Layout.bottomMargin: 14
                onClicked: playerPicker.close()
            }
        }
    }

    // One byte of the Raw tab.
    Dialog {
        id: byteDialog
        property int offset: 0
        function edit(off, hex) {
            offset = off
            byteField.text = hex
            open()
            byteField.forceActiveFocus()
            byteField.selectAll()
        }
        function apply() {
            const v = parseInt(byteField.text, 16)
            if (!isNaN(v) && v >= 0 && v <= 255 && editorRoot.save.setRawByte(editorRoot.kind, editorRoot.recordIndex, offset, v))
                close()
        }
        title: "Byte +0x" + offset.toString(16).toUpperCase()
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 320
        padding: 20
        background: Rectangle {
            color: BtDarkStyle.bgDark
            border.color: BtDarkStyle.accentColor
            border.width: 1
            radius: 6
        }
        header: Text {
            text: byteDialog.title
            color: BtDarkStyle.accentColor
            font.bold: true
            font.pixelSize: 15
            leftPadding: 20
            topPadding: 16
        }
        contentItem: RowLayout {
            spacing: 10
            Text { text: "Hex value"; color: BtDarkStyle.textDim; font.pixelSize: 12 }
            TextField {
                id: byteField
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                color: BtDarkStyle.textMain
                font.family: "Consolas"
                font.pixelSize: 14
                maximumLength: 2
                validator: RegularExpressionValidator { regularExpression: /^[0-9a-fA-F]{1,2}$/ }
                background: Rectangle { color: BtDarkStyle.bgMedium; radius: 4; border.color: BtDarkStyle.accentColor }
                Keys.onReturnPressed: byteDialog.apply()
                Keys.onEnterPressed: byteDialog.apply()
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            HeaderButton {
                text: "Set"
                accent: true
                implicitHeight: 34
                Layout.bottomMargin: 14
                onClicked: byteDialog.apply()
            }
            HeaderButton {
                text: "Cancel"
                implicitHeight: 34
                Layout.rightMargin: 16
                Layout.bottomMargin: 14
                onClicked: byteDialog.close()
            }
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
            text: "The save has unsaved changes. Save them first?"
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
                Layout.bottomMargin: 14
                onClicked: {
                    unsavedDialog.close()
                    const action = editorRoot.pendingAction
                    editorRoot.pendingAction = null
                    if (editorRoot.save.save() && action)
                        action()
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

    // The C# editors' Raw Databases (over the whole editor while open).
    RawDatabaseView {
        id: rawView
        anchors.fill: parent
        manager: editorRoot.save
        filePrefix: editorRoot.gameTitle + (editorRoot.isCareer ? " Career" : " Save")
    }
}
