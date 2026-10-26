import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import com.wouldymods.blobset 1.0

// Mod Manager: the loaded game's installed mods and their load order. Mods lower in the list win when two of them
// have the same file. Build (= Modify) puts the workspace mods folder and every enabled mod into the game.
// Drop .zip / .7z / .rar archives or folders anywhere on the page to install them.
Item {
    id: managerRoot
    anchors.fill: parent

    readonly property string iconDir: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/"
    property int selectedIndex: -1
    readonly property var selectedMod: selectedIndex >= 0 && selectedIndex < modManager.mods.length
                                       ? modManager.mods[selectedIndex] : null
    property string statusText: ""
    property bool statusOk: true
    property var selectedConflicts: []

    Rectangle {
        anchors.fill: parent
        color: BtDarkStyle.bgDarkest
        z: -1
    }

    function setStatus(ok, text) {
        statusOk = ok
        statusText = text
    }

    function selectMod(index) {
        selectedIndex = index
        refreshDetails()
    }

    function refreshDetails() {
        if (selectedMod === null) {
            selectedConflicts = []
            filesList.model = []
            return
        }
        selectedConflicts = modManager.conflictsFor(selectedMod.id)
        filesList.model = modManager.filesOf(selectedMod.id, 2000)
        nameField.text = selectedMod.name
        authorField.text = selectedMod.author
        versionField.text = selectedMod.version
        descriptionField.text = selectedMod.description
    }

    function selectById(id) {
        for (let i = 0; i < modManager.mods.length; ++i) {
            if (modManager.mods[i].id === id) {
                selectMod(i)
                return
            }
        }
        selectMod(-1)
    }

    Connections {
        target: modManager
        function onMessage(ok, text) { managerRoot.setStatus(ok, text) }
        function onModsChanged() {
            // Keep the same mod selected after a reorder / install.
            const id = managerRoot.selectedMod ? managerRoot.selectedMod.id : ""
            if (managerRoot.selectedIndex >= modManager.mods.length || id === "")
                managerRoot.selectMod(Math.min(managerRoot.selectedIndex, modManager.mods.length - 1))
            else
                managerRoot.refreshDetails()
        }
    }

    Connections {
        target: appOrchestrator
        function onTaskCompleted(success, message) {
            managerRoot.setStatus(success, success ? "Build finished: " + message : message)
            modManager.reload()
        }
    }

    Component.onCompleted: {
        modManager.reload()
        if (modManager.mods.length > 0)
            selectMod(modManager.mods.length - 1)
        setStatus(true, "Lower in the list wins. Drop mod archives or folders here to install them.")
    }

    function moveSelected(delta) {
        const to = selectedIndex + delta
        if (selectedIndex < 0 || to < 0 || to >= modManager.mods.length)
            return
        const id = selectedMod.id
        modManager.move(selectedIndex, to)
        selectById(id)
    }

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

    component SmallButton: AbstractButton {
        id: smallButton
        property string tip: ""
        property bool danger: false
        BtToolTip { text: smallButton.tip }
        implicitHeight: 30
        implicitWidth: Math.max(30, implicitContentWidth + 20)
        hoverEnabled: true
        opacity: enabled ? 1.0 : 0.35
        background: Rectangle {
            radius: 4
            color: smallButton.hovered && smallButton.enabled ? BtDarkStyle.bgHover : BtDarkStyle.bgMedium
            border.color: smallButton.hovered && smallButton.enabled
                          ? (smallButton.danger ? "#ef5350" : BtDarkStyle.accentColor) : BtDarkStyle.borderDark
            border.width: 1
        }
        contentItem: Text {
            text: smallButton.text
            color: smallButton.hovered && smallButton.enabled
                   ? (smallButton.danger ? "#ef5350" : BtDarkStyle.accentColor) : BtDarkStyle.textMain
            font.pixelSize: 13
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component FieldLabel: Text {
        color: BtDarkStyle.textDim
        font.pixelSize: 12
        font.bold: true
    }

    component DarkField: TextField {
        Layout.fillWidth: true
        color: BtDarkStyle.textMain
        selectByMouse: true
        font.pixelSize: 13
        leftPadding: 8
        background: Rectangle {
            color: BtDarkStyle.bgDarkest
            radius: 4
            border.color: parent.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
            border.width: 1
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
                    onClicked: appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml")
                }

                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }

                Image {
                    source: managerRoot.iconDir + "modify_32.png"
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    fillMode: Image.PreserveAspectFit
                }

                ColumnLayout {
                    spacing: 0
                    Text {
                        text: "Mod Manager"
                        color: BtDarkStyle.textMain
                        font.bold: true
                        font.pixelSize: 16
                    }
                    Text {
                        text: appOrchestrator.currentGameName
                        color: BtDarkStyle.textDim
                        font.pixelSize: 12
                    }
                }

                Item { Layout.fillWidth: true }

                HeaderButton {
                    text: "Install Archive…"
                    iconSource: managerRoot.iconDir + "open_32.png"
                    tip: "Install a mod from a .zip, .7z or .rar archive (7-Zip is used when it's installed)"
                    enabled: !modManager.busy
                    onClicked: archiveDialog.open()
                }
                HeaderButton {
                    text: "Install Folder…"
                    iconSource: managerRoot.iconDir + "folder_32.png"
                    tip: "Install a mod from a folder"
                    enabled: !modManager.busy
                    onClicked: folderDialog.open()
                }
                HeaderButton {
                    text: "Open Library"
                    iconSource: managerRoot.iconDir + "folder_32.png"
                    tip: "Open the folder the installed mods are kept in"
                    onClicked: modManager.openLibraryFolder()
                }

                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }

                HeaderButton {
                    text: "Build"
                    accent: true
                    iconSource: managerRoot.iconDir + "modify_32.png"
                    tip: "Put the workspace mods folder and every enabled mod into the game (the same as Modify). "
                         + "Mods you disabled or removed are taken out again."
                    enabled: !appOrchestrator.workerRunning && !modManager.busy
                    onClicked: {
                        managerRoot.setStatus(true, "Building... (see the Console Log in the workspace for details)")
                        modManager.build()
                    }
                }
            }
        }

        // Body: load order (left) and the selected mod (right)
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 4
                color: SplitHandle.hovered || SplitHandle.pressed ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
            }

            // ---- Load order ----
            Rectangle {
                SplitView.preferredWidth: managerRoot.width * 0.55
                SplitView.minimumWidth: 420
                color: BtDarkStyle.bgDarkest

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Load order"
                            color: BtDarkStyle.textMain
                            font.bold: true
                            font.pixelSize: 14
                        }
                        Text {
                            text: "— lower in the list wins"
                            color: BtDarkStyle.textDim
                            font.pixelSize: 12
                        }
                        Item { Layout.fillWidth: true }
                        SmallButton {
                            text: "▲"
                            tip: "Move up (lower priority)"
                            enabled: managerRoot.selectedIndex > 0
                            onClicked: managerRoot.moveSelected(-1)
                        }
                        SmallButton {
                            text: "▼"
                            tip: "Move down (higher priority)"
                            enabled: managerRoot.selectedIndex >= 0 && managerRoot.selectedIndex < modManager.mods.length - 1
                            onClicked: managerRoot.moveSelected(1)
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: BtDarkStyle.bgDark
                        border.color: BtDarkStyle.borderDark
                        radius: 4

                        ListView {
                            id: modList
                            anchors.fill: parent
                            anchors.margins: 1
                            clip: true
                            model: modManager.mods
                            currentIndex: managerRoot.selectedIndex
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            delegate: Rectangle {
                                id: row
                                required property var modelData
                                required property int index
                                width: ListView.view.width
                                height: 54
                                color: index === managerRoot.selectedIndex ? BtDarkStyle.bgMedium
                                       : rowMouse.containsMouse ? BtDarkStyle.bgHover : "transparent"

                                Rectangle {
                                    anchors.left: parent.left
                                    width: 3
                                    height: parent.height
                                    color: BtDarkStyle.accentColor
                                    visible: row.index === managerRoot.selectedIndex
                                }

                                MouseArea {
                                    id: rowMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: managerRoot.selectMod(row.index)
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 12
                                    spacing: 10

                                    Text {
                                        text: (row.index + 1) + "."
                                        color: BtDarkStyle.textDim
                                        font.pixelSize: 12
                                        Layout.preferredWidth: 24
                                    }

                                    CheckBox {
                                        checked: row.modelData.enabled
                                        BtToolTip { text: row.modelData.enabled ? "Enabled: Build puts it into the game" : "Disabled: Build leaves it out" }
                                        onToggled: modManager.setEnabled(row.modelData.id, checked)
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            Layout.fillWidth: true
                                            text: row.modelData.name + (row.modelData.version !== "" ? "  " + row.modelData.version : "")
                                            color: row.modelData.enabled ? BtDarkStyle.textMain : BtDarkStyle.textDim
                                            font.pixelSize: 14
                                            font.bold: true
                                            font.italic: row.modelData.builtIn
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: row.modelData.fileCount + " file(s)"
                                                  + (row.modelData.author !== "" ? "  ·  by " + row.modelData.author : "")
                                                  + (row.modelData.builtIn ? "  ·  built in" : "")
                                            color: BtDarkStyle.textDim
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }

                                    // Conflict badges: files it takes over (wins) / loses to a mod lower in the list.
                                    Rectangle {
                                        visible: row.modelData.enabled && row.modelData.wins > 0
                                        radius: 3
                                        color: "#1b5e20"
                                        implicitWidth: winsText.implicitWidth + 12
                                        implicitHeight: 20
                                        BtToolTip { text: row.modelData.wins + " file(s) this mod overrides from mods above it" }
                                        Text {
                                            id: winsText
                                            anchors.centerIn: parent
                                            text: "+" + row.modelData.wins
                                            color: "#c8e6c9"
                                            font.pixelSize: 11
                                            font.bold: true
                                        }
                                    }
                                    Rectangle {
                                        visible: row.modelData.enabled && row.modelData.loses > 0
                                        radius: 3
                                        color: "#7f1d1d"
                                        implicitWidth: losesText.implicitWidth + 12
                                        implicitHeight: 20
                                        BtToolTip { text: row.modelData.loses + " file(s) of this mod are replaced by mods below it" }
                                        Text {
                                            id: losesText
                                            anchors.centerIn: parent
                                            text: "−" + row.modelData.loses
                                            color: "#ffcdd2"
                                            font.pixelSize: 11
                                            font.bold: true
                                        }
                                    }
                                }

                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 1
                                    color: BtDarkStyle.borderDark
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: modList.count === 0
                                text: "No game loaded."
                                color: BtDarkStyle.textDim
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: modManager.enabledFileCount + " file(s) will be put into the game"
                              + (modManager.conflictCount > 0 ? "  ·  " + modManager.conflictCount + " file(s) are in more than one enabled mod" : "")
                        color: BtDarkStyle.textDim
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
            }

            // ---- Selected mod ----
            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 360
                color: BtDarkStyle.bgDarkest

                Text {
                    anchors.centerIn: parent
                    visible: managerRoot.selectedMod === null
                    text: "Select a mod to see its details."
                    color: BtDarkStyle.textDim
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    visible: managerRoot.selectedMod !== null

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 6
                        enabled: managerRoot.selectedMod !== null && !managerRoot.selectedMod.builtIn

                        FieldLabel { text: "Name" }
                        DarkField { id: nameField }
                        FieldLabel { text: "Author" }
                        DarkField { id: authorField }
                        FieldLabel { text: "Version" }
                        DarkField { id: versionField }
                        FieldLabel { text: "Description"; Layout.alignment: Qt.AlignTop }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 70
                            color: BtDarkStyle.bgDarkest
                            radius: 4
                            border.color: descriptionField.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                            ScrollView {
                                anchors.fill: parent
                                TextArea {
                                    id: descriptionField
                                    color: BtDarkStyle.textMain
                                    wrapMode: TextEdit.Wrap
                                    selectByMouse: true
                                    font.pixelSize: 13
                                    background: null
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: managerRoot.selectedMod !== null && managerRoot.selectedMod.builtIn
                        text: managerRoot.selectedMod ? managerRoot.selectedMod.description : ""
                        color: BtDarkStyle.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: managerRoot.selectedMod
                              ? managerRoot.selectedMod.path + (managerRoot.selectedMod.installed !== "" ? "\nInstalled " + managerRoot.selectedMod.installed : "")
                              : ""
                        color: BtDarkStyle.textDim
                        font.family: "Consolas"
                        font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        SmallButton {
                            text: "Save Details"
                            tip: "Save the name, author, version and description"
                            visible: managerRoot.selectedMod !== null && !managerRoot.selectedMod.builtIn
                            onClicked: modManager.saveInfo(managerRoot.selectedMod.id, nameField.text, authorField.text,
                                                           versionField.text, descriptionField.text)
                        }
                        SmallButton {
                            text: "Open Folder"
                            tip: "Open the mod's folder (you can add or change its files there)"
                            onClicked: modManager.openFolder(managerRoot.selectedMod.id)
                        }
                        Item { Layout.fillWidth: true }
                        SmallButton {
                            text: "Remove"
                            danger: true
                            tip: "Remove the mod (its folder goes to the Recycle Bin)"
                            visible: managerRoot.selectedMod !== null && !managerRoot.selectedMod.builtIn
                            onClicked: removeDialog.open()
                        }
                    }

                    TabBar {
                        id: detailTabs
                        Layout.fillWidth: true
                        background: Rectangle { color: BtDarkStyle.bgDark }
                        TabButton { text: "Files (" + filesList.count + ")" }
                        TabButton { text: "Conflicts (" + managerRoot.selectedConflicts.length + ")" }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: detailTabs.currentIndex

                        Rectangle {
                            color: BtDarkStyle.bgDark
                            border.color: BtDarkStyle.borderDark
                            radius: 4
                            ListView {
                                id: filesList
                                anchors.fill: parent
                                anchors.margins: 6
                                clip: true
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                delegate: Text {
                                    required property var modelData
                                    width: ListView.view.width
                                    text: modelData
                                    color: BtDarkStyle.textMain
                                    font.family: "Consolas"
                                    font.pixelSize: 12
                                    elide: Text.ElideMiddle
                                }
                            }
                        }

                        Rectangle {
                            color: BtDarkStyle.bgDark
                            border.color: BtDarkStyle.borderDark
                            radius: 4
                            ListView {
                                anchors.fill: parent
                                anchors.margins: 6
                                clip: true
                                model: managerRoot.selectedConflicts
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                delegate: ColumnLayout {
                                    required property var modelData
                                    width: ListView.view.width
                                    spacing: 0
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.path
                                        color: modelData.wins ? "#a5d6a7" : "#ef9a9a"
                                        font.family: "Consolas"
                                        font.pixelSize: 12
                                        elide: Text.ElideMiddle
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        Layout.bottomMargin: 6
                                        text: (modelData.wins ? "Used from this mod" : "Replaced by " + modelData.winner)
                                              + "  ·  also in: " + modelData.others
                                        color: BtDarkStyle.textDim
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: managerRoot.selectedConflicts.length === 0
                                    text: "No other enabled mod has these files."
                                    color: BtDarkStyle.textDim
                                }
                            }
                        }
                    }
                }
            }
        }

        // Status bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: BtDarkStyle.bgDark
            border.color: BtDarkStyle.borderDark

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                BusyIndicator {
                    running: modManager.busy || appOrchestrator.workerRunning
                    visible: running
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                }
                Text {
                    Layout.fillWidth: true
                    text: managerRoot.statusText
                    color: managerRoot.statusOk ? BtDarkStyle.textMain : "#ef5350"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                SmallButton {
                    text: "Set Up Vortex"
                    implicitHeight: 24
                    tip: "Install the Blobset Tools game extensions into Vortex (restart Vortex afterwards). "
                         + "Vortex then deploys mods into <game>\\BlobsetToolsMods and runs Blobset Tools to put them into the game."
                    onClicked: modManager.installVortexExtensions()
                }
            }
        }
    }

    // Drop archives / folders anywhere to install them.
    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (drop) => {
            if (!drop.hasUrls)
                return
            for (let i = 0; i < drop.urls.length; ++i)
                managerRoot.pendingInstalls.push(drop.urls[i].toString())
            managerRoot.installNext()
            drop.accept(Qt.CopyAction)
        }
        Rectangle {
            anchors.fill: parent
            visible: dropArea.containsDrag
            color: "#33f5c518"
            border.color: BtDarkStyle.accentColor
            border.width: 2
            Text {
                anchors.centerIn: parent
                text: "Drop to install"
                color: BtDarkStyle.accentColor
                font.pixelSize: 22
                font.bold: true
            }
        }
    }

    // Several dropped items install one after the other (one install runs at a time).
    property var pendingInstalls: []
    function installNext() {
        if (modManager.busy || managerRoot.pendingInstalls.length === 0)
            return
        const next = managerRoot.pendingInstalls.shift()
        modManager.install(next)
    }
    Connections {
        target: modManager
        function onBusyChanged() {
            if (!modManager.busy)
                managerRoot.installNext()
        }
    }

    FileDialog {
        id: archiveDialog
        title: "Install a mod archive"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Mod archives (*.zip *.7z *.rar)", "All files (*)"]
        onAccepted: {
            for (let i = 0; i < selectedFiles.length; ++i)
                managerRoot.pendingInstalls.push(selectedFiles[i].toString())
            managerRoot.installNext()
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Install a mod folder"
        onAccepted: {
            managerRoot.pendingInstalls.push(selectedFolder.toString())
            managerRoot.installNext()
        }
    }

    Dialog {
        id: removeDialog
        anchors.centerIn: parent
        modal: true
        title: "Remove mod"
        standardButtons: Dialog.Yes | Dialog.No
        Text {
            width: 420
            wrapMode: Text.WordWrap
            color: BtDarkStyle.textMain
            text: managerRoot.selectedMod
                  ? "Remove " + managerRoot.selectedMod.name + "? Its folder goes to the Recycle Bin. "
                    + "Press Build afterwards to take it out of the game."
                  : ""
        }
        onAccepted: modManager.uninstall(managerRoot.selectedMod.id)
    }
}
