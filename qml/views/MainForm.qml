import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Dialogs
import QtCore
import com.wouldymods.blobset 1.0

Item {
    id: mainFormView
    visible: true
    anchors.fill: parent

    property bool sidebarExpanded: false

    // View state (flip / alpha) and image://dds_provider source of the Image Preview
    DdsViewerBridge {
        id: previewBridge
    }

    // Shown when the selected game has no known location (SteamGameId = 0, or not found in Steam).
    FileDialog {
        id: gameLocationDialog
        title: "Locate " + appOrchestrator.currentGameName + " (" + appOrchestrator.expectedBlobsetName + ")"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Blobset (" + appOrchestrator.expectedBlobsetName + ")", "All files (*)"]

        onAccepted: {
            // Wrong file: ask again (the reason is already in the console log).
            if (!appOrchestrator.setGameLocation(selectedFile.toString()))
                Qt.callLater(gameLocationDialog.open)
        }
        onRejected: appOrchestrator.uiConsoleUpdate(
            "No game location set. Use 'Set Game Location' in the menu to choose it later.")
    }

    // Selection state is kept in AppOrchestrator (this view is destroyed whenever another view is loaded).
    property bool restorePending: true          // Restore the saved selection once the tree has rows.
    property bool restoringBrowserState: false  // True while the saved selection is being re-applied.
    property var searchResults: []              // Search bar matches (AppOrchestrator::searchAssets).
    property int searchPosition: -1             // The match that is selected.
    property string searchQuery: ""             // The query searchResults belong to.

    Rectangle {
        anchors.fill: parent
        color: BtDarkStyle.bgDarkest
        z: -1
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

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
                spacing: 20

                AbstractButton {
                    BtToolTip { text: "Show or hide the side menu" }
                    id: menuToggleBtn
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    hoverEnabled: true
                    background: Rectangle {
                        color: menuToggleBtn.hovered ? BtDarkStyle.bgHover : "transparent"
                        radius: 8
                        border.color: BtDarkStyle.accentColor
                        border.width: 1

                        Text {
                            anchors.centerIn: parent;
                            text: "BT";
                            color: BtDarkStyle.accentColor;
                            font.bold: true;
                            font.pointSize: 14
                        }
                    }
                    onClicked: mainFormView.sidebarExpanded = !mainFormView.sidebarExpanded
                }

                RowLayout {
                    spacing: 12
                    Layout.alignment: Qt.AlignVCenter

                    AbstractButton {
                        BtToolTip { text: "Extract the files of the loaded blobset to the game's folder" }
                        id: btnExtract
                        Layout.preferredWidth: 100
                        Layout.preferredHeight: 40
                        hoverEnabled: true
                        enabled: !appOrchestrator.workerRunning   // Only one task at a time.
                        opacity: enabled ? 1.0 : 0.4
                        background: Rectangle {
                            color: btnExtract.hovered ? BtDarkStyle.bgMedium : "transparent"
                            border.color: btnExtract.hovered ? BtDarkStyle.accentColor : "transparent"
                            border.width: 1
                            radius: 4
                        }
                        contentItem: RowLayout {
                            spacing: 2
                            anchors.centerIn: parent

                            Image {
                                source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/extract_32.png";
                                Layout.preferredWidth: 20;
                                Layout.preferredHeight: 20;
                                fillMode: Image.PreserveAspectFit;
                                Layout.leftMargin: 10
                            }

                            Text {
                                text: "Extract";
                                color: btnExtract.hovered ? BtDarkStyle.accentColor : BtDarkStyle.textMain;
                                font.bold: true;
                                font.pixelSize: 14
                            }
                        }
                        onClicked: appOrchestrator.extractBlobset()
                    }

                    AbstractButton {
                        BtToolTip { text: appOrchestrator.blobsetVersion === 3 && appOrchestrator.modLoaderMode ? "Build the mods folder and every enabled Mod Manager mod into <game>\\BlobsetMods for the Blobset Mod Loader (the game's files stay untouched)" : "Put the mods folder and every enabled Mod Manager mod into the game's blobset (the originals are backed up first)" }
                        id: btnModify
                        Layout.preferredWidth: 100
                        Layout.preferredHeight: 40
                        hoverEnabled: true
                        enabled: !appOrchestrator.workerRunning   // Only one task at a time.
                        opacity: enabled ? 1.0 : 0.4
                        background: Rectangle {
                            color: btnModify.hovered ? BtDarkStyle.bgMedium : "transparent"
                            border.color: btnModify.hovered ? BtDarkStyle.accentColor : "transparent"
                            border.width: 1
                            radius: 4
                        }
                        contentItem: RowLayout {
                            spacing: 2
                            anchors.centerIn: parent

                            Image {
                                source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/modify_32.png";
                                Layout.preferredWidth: 20;
                                Layout.preferredHeight: 20;
                                fillMode: Image.PreserveAspectFit;
                                Layout.leftMargin: 10
                            }

                            Text {
                                text: "Modify";
                                color: btnModify.hovered ? BtDarkStyle.accentColor : BtDarkStyle.textMain;
                                font.bold: true;
                                font.pixelSize: 14
                            }
                        }
                        onClicked: appOrchestrator.modifyBlobset()
                    }

                    AbstractButton {
                        BtToolTip { text: "Rebuild the whole blobset with the files from the mods folder (the original is backed up first)" }
                        id: btnCreate
                        Layout.preferredWidth: 100
                        Layout.preferredHeight: 40
                        hoverEnabled: true
                        enabled: !appOrchestrator.workerRunning   // Only one task at a time.
                        opacity: enabled ? 1.0 : 0.4
                        visible: appOrchestrator.blobsetVersion === 0   // Create only exists for V1 blobsets.
                        background: Rectangle {
                            color: btnCreate.hovered ? BtDarkStyle.bgMedium : "transparent"
                            border.color: btnCreate.hovered ? BtDarkStyle.accentColor : "transparent"
                            border.width: 1
                            radius: 4
                        }
                        contentItem: RowLayout {
                            spacing: 2
                            anchors.centerIn: parent

                            Image {
                                source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/create_32.png";
                                Layout.preferredWidth: 20;
                                Layout.preferredHeight: 20;
                                fillMode: Image.PreserveAspectFit;
                                Layout.leftMargin: 10
                            }

                            Text {
                                text: "Create";
                                color: btnCreate.hovered ? BtDarkStyle.accentColor : BtDarkStyle.textMain;
                                font.bold: true;
                                font.pixelSize: 14
                            }
                        }
                        onClicked: createConfirmDialog.open()
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                Rectangle {
                    Layout.preferredWidth: 500;
                    Layout.preferredHeight: 34;
                    color: BtDarkStyle.bgMedium;
                    border.width: 1;
                    radius: 4
                    border.color: mainFormView.searchQuery !== "" && mainFormView.searchResults.length === 0 ? "#ef5350"
                                : globalSearchInput.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark

                    RowLayout {
                        anchors.fill: parent;
                        anchors.leftMargin: 10;
                        anchors.rightMargin: 10;
                        spacing: 6

                        Text {
                            text: "🔍";
                            font.pixelSize: 12;
                            color: BtDarkStyle.textDim
                        }

                        TextInput {
                            id: globalSearchInput;
                            Layout.fillWidth: true;
                            color: BtDarkStyle.textMain;
                            font.pixelSize: 13;
                            font.family: "Consolas";
                            selectByMouse: true

                            Text {
                                text: "Search for file assets...  (Enter: next, Shift+Enter: previous)";
                                color: BtDarkStyle.textDim;
                                font.pixelSize: 13;
                                font.italic: true;
                                visible: globalSearchInput.text === "" && !globalSearchInput.activeFocus
                            }

                            // Typing jumps to the best match (after a short pause); Enter / Shift+Enter step through
                            // the other matches, Escape clears the search and goes back to the folder tree.
                            onTextChanged: searchDebounce.restart()
                            Keys.onPressed: function(event) {
                                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                    searchDebounce.stop();
                                    mainFormView.stepSearch((event.modifiers & Qt.ShiftModifier) ? -1 : 1);
                                    event.accepted = true;
                                } else if (event.key === Qt.Key_Escape) {
                                    globalSearchInput.text = "";
                                    packageTreeView.forceActiveFocus();
                                    event.accepted = true;
                                } else if (event.key === Qt.Key_Down) {
                                    mainFormView.stepSearch(1); event.accepted = true;
                                } else if (event.key === Qt.Key_Up) {
                                    mainFormView.stepSearch(-1); event.accepted = true;
                                }
                            }

                            Timer {
                                id: searchDebounce
                                interval: 300
                                onTriggered: mainFormView.runSearch(globalSearchInput.text, true)
                            }
                        }

                        Text {
                            visible: mainFormView.searchQuery !== ""
                            text: mainFormView.searchResults.length === 0 ? "No matches"
                                  : (mainFormView.searchPosition + 1) + " / " + mainFormView.searchResults.length
                                    + (mainFormView.searchResults.length >= 2000 ? "+" : "")
                            color: mainFormView.searchResults.length === 0 ? "#ef5350" : BtDarkStyle.textDim
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }

        RowLayout {
            id: workspaceMasterLayout
            Layout.fillWidth: true;
            Layout.fillHeight: true;
            spacing: 0

            Rectangle {
                id: dynamicHidingSidebar;
                Layout.fillHeight: true;
                color: BtDarkStyle.bgDark;
                clip: true
                Layout.preferredWidth: mainFormView.sidebarExpanded ? 220 : 0
                visible: Layout.preferredWidth > 0
                Behavior on Layout.preferredWidth {

                    NumberAnimation {
                        duration: 200;
                        easing.type: Easing.InOutQuad
                    }
                }

                Rectangle {
                    anchors.right: parent.right;
                    anchors.top: parent.top;
                    anchors.bottom: parent.bottom;
                    width: 1;
                    color: BtDarkStyle.borderDark
                }

                ScrollView {
                    anchors.fill: parent;
                    contentWidth: parent.width;
                    clip: true

                    ColumnLayout {
                        width: dynamicHidingSidebar.width;
                        spacing: 0;
                        anchors.topMargin: 15
                        component CategoryHeader: Text {
                            Layout.fillWidth: true;
                            Layout.leftMargin: 15;
                            Layout.topMargin: 12;
                            Layout.bottomMargin: 4;
                            color: BtDarkStyle.accentColor;
                            font.bold: true;
                            font.pointSize: 10
                        }
                        component NavigationItem: ItemDelegate {
                            id: navItem
                            property string tip: ""   // Tooltip text.
                            BtToolTip { text: navItem.tip }
                            property url iconSource: ""

                            Layout.fillWidth: true
                            implicitHeight: 32
                            leftPadding: 15
                            rightPadding: 10

                            background: Rectangle {
                                color: navItem.hovered ? BtDarkStyle.bgHover : "transparent"
                            }

                            contentItem: RowLayout {
                                spacing: 10

                                Image {
                                    source: navItem.iconSource
                                    sourceSize: Qt.size(32, 32)
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    Layout.alignment: Qt.AlignVCenter
                                    fillMode: Image.PreserveAspectFit
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: navItem.text
                                    color: navItem.hovered ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                                    font.pixelSize: 13
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        CategoryHeader { text: "File" }
                        NavigationItem {
                            tip: "Go back to the game selection screen"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/menu_32.png"
                            text: "Game Selection";
                            onClicked: {
                                appOrchestrator.clearBrowserState();
                                appOrchestrator.treeModel.clear();
                                appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/views/GameSelection.qml")
                            }
                        }
                        NavigationItem {
                            tip: "Close Blobset Tools"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/close_32.png"
                            text: "Exit";
                            onClicked: Qt.quit()
                        }

                        CategoryHeader { text: "Tools" }
                        NavigationItem {
                            tip: "Install, enable and order mods; Build puts them all into the game"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/mod_manager_32.png"
                            text: "Mod Manager";
                            onClicked: appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/subview_viewers/ModManager.qml")
                        }
                        NavigationItem {
                            tip: "Rebuild a TXPK texture pack from extracted .dds files"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/txpk_32.png"
                            text: "TXPK Creator";
                            onClicked: appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/subview_viewers/TXPKCreator.qml")
                        }
                        NavigationItem {
                            tip: "Rebuild an M3MP package from extracted files"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/m3mp_32.png"
                            text: "M3MP Creator";
                            onClicked: appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/subview_viewers/M3MPCreator.qml")
                        }
                        NavigationItem {
                            tip: "Rebuild a BSB (Flash UI screen) from its extracted movie and textures"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/bsb_32.png"
                            text: "BSB Creator";
                            visible: appOrchestrator.blobsetVersion === 0   // BSBs only exist in the Blobset V1 games.
                            onClicked: appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/subview_viewers/BSBCreator.qml")
                        }
                        NavigationItem {
                            tip: "View and edit the hash to file name mapping"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/mapping_32.png"
                            text: "File Mapping Editor";
                            onClicked: appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/subview_viewers/FileMappingEditor.qml")
                        }
                        NavigationItem {
                            tip: "Edit the game's teams and players (squads, lineups, jerseys, ratings, stadiums, ...)"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/database_editor_32.png"
                            text: "Database Editor";
                            // The games whose database the editor reads (DatabaseManager).
                            visible: ["Rugby League 26", "Rugby League Live 4", "Rugby League Live 3", "Rugby 25"].indexOf(appOrchestrator.currentGameName) >= 0
                            onClicked: appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/subview_viewers/DatabaseEditor.qml")
                        }
                        NavigationItem {
                            tip: "Edit the game's text (text\\strings\\*.str, one table per language)"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/str_32.png"
                            text: "String Editor";
                            onClicked: appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/subview_viewers/StringEditor.qml")
                        }

                        CategoryHeader { text: "Options" }
                        NavigationItem {
                            tip: "Rebuild the file mapping for the loaded blobset (V4: only adds missing hashes and removes deleted ones)"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/file_mapping_32.png"
                            text: "Update File Mapping Data";
                            onClicked: {
                                appOrchestrator.uiConsoleUpdate("Spawning back-end dataset regeneration indexing process...");
                                appOrchestrator.clearBrowserState(); appOrchestrator.treeModel.clear(); appOrchestrator.updateFileMapping()
                            }
                        }
                        NavigationItem {
                            tip: "Work out the names of unknown hashes from the known file names"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/mapping_32.png"
                            text: "Resolve File Names";
                            onClicked: {
                                appOrchestrator.uiConsoleUpdate("Matching names to unknown files by their hashes...");
                                appOrchestrator.resolveFileNames()
                            }
                        }
                        NavigationItem {
                            tip: "When on, the game starts automatically after Modify / Create succeeds"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/loading_32.png"
                            // On: the game starts through Steam after Modify / Create succeeded.
                            text: "Load Game: " + (appOrchestrator.loadGameMode ? "On" : "Off");
                            visible: appOrchestrator.steamGameId > 0   // Steam games only.
                            onClicked: appOrchestrator.loadGameMode = !appOrchestrator.loadGameMode
                        }
                        NavigationItem {
                            tip: "Ask Steam to verify the game's files and download any that changed"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/steam_32.png"
                            text: "Validate Steam Files";
                            visible: appOrchestrator.steamGameId > 0   // Steam games only.
                            onClicked: appOrchestrator.validateSteamFiles()
                        }
                        NavigationItem {
                            tip: "When on, Modify writes the mods to <game>\\BlobsetMods for the Blobset Mod Loader instead of changing the game's files, and the folders and files show that build (the modded files from <game>\\BlobsetMods\\data, the rest from the game)"
                            // V4 games: Modify builds the mods in <game>\BlobsetMods for the Blobset Mod Loader.
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/mod_loader_32.png"
                            text: "Mod Loader: " + (appOrchestrator.modLoaderMode ? "On" : "Off");
                            visible: appOrchestrator.blobsetVersion === 3
                            onClicked: appOrchestrator.modLoaderMode = !appOrchestrator.modLoaderMode
                        }
                        NavigationItem {
                            tip: "Open Documents\\Blobset Tools: settings, game profiles, mappings, mods, backups and extracted files"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/folder_32.png"
                            text: "Open Data Folder";
                            onClicked: Qt.openUrlExternally("file:///" + appOrchestrator.dataFolderPath().replace(/\\/g, "/"))
                        }
                        NavigationItem {
                            tip: "Put the original game files back from the backups"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/reset_32.png"
                            text: "Restore Backup Files";
                            enabled: !appOrchestrator.workerRunning
                            onClicked: restoreConfirmDialog.open()
                        }

                        CategoryHeader { text: "About" }
                        NavigationItem {
                            tip: "About Blobset Tools: credits, license and changelog"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/info_32.png"
                            text: "Info";
                            onClicked: aboutWindow.open()
                        }
                        NavigationItem {
                            tip: "See whether a newer Blobset Tools is on GitHub, and install it"
                            iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/check_updates_32.png"
                            text: updater.checking ? "Checking for Updates..." : "Check for Updates";
                            enabled: !updater.checking && !updater.downloading
                            onClicked: updater.checkNow()
                        }
                    }
                }
            }

            SplitView {
                id: mainSplitter;
                Layout.fillWidth: true;
                Layout.fillHeight: true;
                orientation: Qt.Horizontal
                handle: Rectangle {
                    implicitWidth: 4;
                    color: SplitHandle.hovered ? BtDarkStyle.accentColor : BtDarkStyle.borderDark;

                    Behavior on color {

                        ColorAnimation {
                            duration: 100
                        }
                    }
                }

                Rectangle {
                    id: foldersPanelWrapper;
                    SplitView.preferredWidth: 250;
                    SplitView.minimumWidth: 150;
                    color: BtDarkStyle.bgDarkest
                    onActiveFocusChanged: if (activeFocus) packageTreeView.forceActiveFocus()

                    ColumnLayout {
                        anchors.fill: parent;
                        anchors.margins: 10;
                        spacing: 8

                        Text {
                            text: "Folders";
                            color: BtDarkStyle.accentColor;
                            font.bold: true;
                            font.pointSize: 10
                        }

                        Rectangle {
                            Layout.fillWidth: true;
                            Layout.fillHeight: true;
                            color: BtDarkStyle.bgDark;
                            border.width: 1;
                            radius: 4;
                            clip: true
                            border.color: packageTreeView.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark

                            TreeView {
                                id: packageTreeView
                                anchors.fill: parent; anchors.margins: 4
                                model: appOrchestrator.treeModel
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                columnWidthProvider: function(column) { return Math.max(packageTreeView.width, 100) }
                                // Recycled rows could keep the indentation of the row they showed before, so a root folder
                                // looked like it sat inside the folder above it. Rows are cheap here: always create fresh ones.
                                reuseItems: false

                                // The selection model owns the current folder (a real QModelIndex that survives expand / collapse).
                                selectionModel: ItemSelectionModel { id: folderSelection }

                                Connections {
                                    target: folderSelection
                                    function onCurrentChanged(current, previous) {
                                        if (!current.valid) return;
                                        appOrchestrator.selectFolderIndex(current);   // Fills the file list (and clears the file selection).
                                        if (!mainFormView.restoringBrowserState) mainFormView.persistBrowserState("");
                                    }
                                }

                                // The tree can receive its rows after this view was created.
                                // An emptied tree (clear / rebuild) re-arms the initial selection for the next fill.
                                onRowsChanged: {
                                    if (packageTreeView.rows <= 0) mainFormView.restorePending = true;
                                    else mainFormView.restoreBrowserState();
                                }

                                Keys.onPressed: (event) => {
                                    if (event.key === Qt.Key_Tab) { fileMasterListView.forceActiveFocus(); event.accepted = true; return; }
                                    if (packageTreeView.rows <= 0) return;

                                    // NOTE: the *visual* row, not QModelIndex.row (that one is the row inside the parent folder).
                                    var row = packageTreeView.rowAtIndex(folderSelection.currentIndex);
                                    var lastRow = packageTreeView.rows - 1;
                                    var pageStep = Math.max(1, Math.floor(packageTreeView.height / 36) - 1);
                                    var target = -1;

                                    switch (event.key) {
                                    case Qt.Key_Down:     target = row < 0 ? 0 : Math.min(lastRow, row + 1); break;
                                    case Qt.Key_Up:       target = row < 0 ? 0 : Math.max(0, row - 1); break;
                                    case Qt.Key_PageDown: target = row < 0 ? 0 : Math.min(lastRow, row + pageStep); break;
                                    case Qt.Key_PageUp:   target = row < 0 ? 0 : Math.max(0, row - pageStep); break;
                                    case Qt.Key_Home:     target = 0; break;
                                    case Qt.Key_End:      target = lastRow; break;
                                    case Qt.Key_Right:
                                        if (row < 0) { target = 0; }
                                        else if (packageTreeView.hasChildren(row)) {
                                            if (!packageTreeView.isExpanded(row)) packageTreeView.expand(row);
                                            else target = row + 1;                       // Already open: step into the first child.
                                        }
                                        break;
                                    case Qt.Key_Left:
                                        if (row < 0) { target = 0; }
                                        else if (packageTreeView.hasChildren(row) && packageTreeView.isExpanded(row)) {
                                            packageTreeView.collapse(row);
                                        } else {                                         // Closed / leaf: step out to the parent folder.
                                            var depth = packageTreeView.depth(row);
                                            for (var r = row - 1; r >= 0; --r) {
                                                if (packageTreeView.depth(r) < depth) { target = r; break; }
                                            }
                                        }
                                        break;
                                    case Qt.Key_Space:
                                    case Qt.Key_Return:
                                    case Qt.Key_Enter:
                                        if (row >= 0) packageTreeView.toggleExpanded(row);
                                        break;
                                    default:
                                        return;                                          // Not ours - leave the event alone.
                                    }

                                    event.accepted = true;
                                    if (target >= 0) mainFormView.selectFolderRow(target);
                                }

                                delegate: TreeViewDelegate {
                                    id: treeDelegate;
                                    implicitHeight: 36;
                                    indicator: null
                                    // TreeViewDelegate indents the content itself (leftMargin + depth * indentation), so the
                                    // indentation always matches the row's depth.
                                    indentation: 15
                                    leftMargin: 4
                                    rightMargin: 4
                                    spacing: 0
                                    background: Rectangle {
                                        color: treeDelegate.current ? (packageTreeView.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.accentDim) : (treeDelegate.hovered ? BtDarkStyle.borderDark : "transparent");
                                        radius: 3
                                    }
                                    contentItem: RowLayout {
                                        spacing: 8;

                                        Text {
                                            text: treeDelegate.isTreeNode && treeDelegate.hasChildren ? (treeDelegate.expanded ? "\u25BC" : "\u25B6") : " "
                                            font.pixelSize: 11;
                                            Layout.alignment: Qt.AlignVCenter;
                                            color: treeDelegate.current ? "#121214" : BtDarkStyle.textDim

                                            MouseArea {
                                                anchors.fill: parent;
                                                onClicked: packageTreeView.toggleExpanded(row)
                                            }
                                        }

                                        Image {
                                            id: folderIcon;
                                            source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/folder_32.png";
                                            Layout.preferredWidth: 16;
                                            Layout.preferredHeight: 16;
                                            fillMode: Image.PreserveAspectFit;
                                            Layout.alignment: Qt.AlignVCenter;
                                            asynchronous: true;
                                            cache: true
                                        }

                                        Label {
                                            text: model.fileName;
                                            color: treeDelegate.current ? "#121214" : BtDarkStyle.textMain;
                                            font.pixelSize: 13;
                                            elide: Text.ElideRight;
                                            Layout.fillWidth: true;
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                    onClicked: {
                                        mainFormView.selectFolderRow(row); packageTreeView.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: filesPanelContainer;
                    SplitView.preferredWidth: 350;
                    SplitView.minimumWidth: 300;
                    color: BtDarkStyle.bgDarkest

                    function getFileExtensionIcon(fileName) {
                        var baseDir = "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/";
                        var dotIndex = fileName.lastIndexOf("."); if (dotIndex === -1) return baseDir + "file_32.png";
                        var ext = fileName.substring(dotIndex).toLowerCase();

                        switch (ext) {
                            case ".dds":
                                return baseDir + "dds_32.png";
                            case ".txpk":
                                return baseDir + "txpk_32.png";
                            case ".txpl":
                                return baseDir + "txpl_32.png";
                            case ".str":
                                return baseDir + "str_32.png";
                            case ".m3mp":
                                return baseDir + "m3mp_32.png";
                            case ".mesh":
                                return baseDir + "mesh_32.png";
                            case ".col":
                                return baseDir + "col_32.png";
                            case ".hjo":
                                return baseDir + "hjo_32.png";
                            case ".wem":
                                return baseDir + "wem_32.png";
                            case ".bnk":
                                return baseDir + "bnk_32.png";
                            case ".bank":
                                return baseDir + "bank_32.png";
                            case ".fsb":
                                return baseDir + "fsb_32.png";
                            case ".bsb":
                                return baseDir + "bsb_32.png";
                            case ".wav":
                                return baseDir + "wav_32.png";
                            case ".str":
                            case ".txt":
                                return baseDir + "txt_32.png";
                            case ".lua":
                                return baseDir + "lua_32.png";
                            case ".obj":
                                return baseDir + "obj_32.png";
                            case ".fev":
                                return baseDir + "fev1_32.png";
                            case ".mat":
                                return baseDir + "mat_32.png";
                            case ".dat":
                                return baseDir + "dat_32.png";
                            default:
                                return baseDir + "file_32.png"; }
                    }

                    readonly property int itemRowHeight: 22
                    readonly property int gridColumnWidth: appOrchestrator.maxFileItemWidth

                    property int rowsPerColumn: {
                        var calculatedRows = Math.floor((fileMasterListView.height - 16) / itemRowHeight);
                        return calculatedRows > 0 ? calculatedRows : 1;
                    }

                    property int totalColumnsNeeded: {
                        if (!appOrchestrator.currentFolderFiles || appOrchestrator.currentFolderFiles.length === 0)
                            return 0;
                        return Math.ceil(appOrchestrator.currentFolderFiles.length / filesPanelContainer.rowsPerColumn);
                    }
                    property int keyboardActiveFileIndex: -1

                    onActiveFocusChanged: if (activeFocus) fileMasterListView.forceActiveFocus()

                    // The rest of the Files panel (title, margins): the wheel scrolls the file columns too.
                    WheelHandler {
                        target: null
                        onWheel: (event) => {
                            fileMasterListView.scrollByWheel(event);
                            event.accepted = true;
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent;
                        anchors.margins: 10;
                        spacing: 8

                        RowLayout {
                            spacing: 8
                            Text {
                                text: "Files";
                                color: BtDarkStyle.accentColor;
                                font.bold: true;
                                font.pointSize: 10
                            }
                            // Mod Loader view: the files are read from the Mod Loader's build (<game>\BlobsetMods\data).
                            Rectangle {
                                visible: appOrchestrator.modLoaderView
                                implicitWidth: loaderViewLabel.implicitWidth + 12
                                implicitHeight: 16
                                radius: 3
                                color: BtDarkStyle.accentDim
                                Text {
                                    id: loaderViewLabel
                                    anchors.centerIn: parent
                                    text: "BLOBSETMODS"
                                    color: "#121214"
                                    font.pixelSize: 9
                                    font.bold: true
                                }
                                HoverHandler { id: loaderViewHover }
                                BtToolTip {
                                    text: "Mod Loader on: modded files are read from <game>\\BlobsetMods\\data, the rest from the game's own files (as the game sees them)"
                                    visible: loaderViewHover.hovered
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true;
                            Layout.fillHeight: true;
                            color: BtDarkStyle.bgDark;
                            border.width: 1;
                            radius: 4;
                            clip: true
                            border.color: filesDrop.containsDrag || fileMasterListView.activeFocus ? BtDarkStyle.accentColor
                                                                                                   : BtDarkStyle.borderDark

                            ListView {
                                id: fileMasterListView;
                                anchors.fill: parent;
                                anchors.bottomMargin: 14;
                                orientation: ListView.Horizontal;
                                flickableDirection: Flickable.HorizontalFlick;
                                spacing: 6;
                                clip: true;
                                leftMargin: 4;
                                rightMargin: 4;
                                model: filesPanelContainer.totalColumnsNeeded

                                // Mouse wheel anywhere in the Files panel: scroll the columns sideways (wheel up = left,
                                // wheel down = right). A wheel notch moves one column, a touchpad scrolls smoothly.
                                // (The handlers run before the list's own wheel handling, which they replace.)
                                WheelHandler {
                                    target: null
                                    onWheel: (event) => {
                                        fileMasterListView.scrollByWheel(event);
                                        event.accepted = true;
                                    }
                                }

                                function scrollByWheel(wheel) {
                                    const delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x;
                                    const pixels = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : wheel.pixelDelta.x;
                                    let step;
                                    if (pixels !== 0)
                                        step = -pixels;
                                    else
                                        step = -(delta / 120) * (filesPanelContainer.gridColumnWidth + spacing);
                                    if (step === 0)
                                        return;
                                    const minX = originX - leftMargin;
                                    const maxX = Math.max(minX, originX + contentWidth + rightMargin - width);
                                    contentX = Math.max(minX, Math.min(maxX, contentX + step));
                                }

                                Keys.onPressed: (event) => {
                                    if (event.key === Qt.Key_Tab) { packageTreeView.forceActiveFocus(); event.accepted = true; return; }
                                    if (!appOrchestrator.currentFolderFiles || appOrchestrator.currentFolderFiles.length === 0) return;
                                    var totalFiles = appOrchestrator.currentFolderFiles.length; var itemsPerCol = filesPanelContainer.rowsPerColumn; var currentIdx = filesPanelContainer.keyboardActiveFileIndex;
                                    if (currentIdx === -1) { if (event.key === Qt.Key_Down || event.key === Qt.Key_Up || event.key === Qt.Key_Left || event.key === Qt.Key_Right) { filesPanelContainer.keyboardActiveFileIndex = 0; triggerFileSelection(0); event.accepted = true; return; } }
                                    if (event.key === Qt.Key_Down) { if (currentIdx + 1 < totalFiles) { filesPanelContainer.keyboardActiveFileIndex = currentIdx + 1; triggerFileSelection(currentIdx + 1); } event.accepted = true; }
                                    else if (event.key === Qt.Key_Up) { if (currentIdx - 1 >= 0) { filesPanelContainer.keyboardActiveFileIndex = currentIdx - 1; triggerFileSelection(currentIdx - 1); } event.accepted = true; }
                                    else if (event.key === Qt.Key_Right) { if (currentIdx + itemsPerCol < totalFiles) { filesPanelContainer.keyboardActiveFileIndex = currentIdx + itemsPerCol; triggerFileSelection(currentIdx + itemsPerCol); } else { filesPanelContainer.keyboardActiveFileIndex = totalFiles - 1; triggerFileSelection(totalFiles - 1); } event.accepted = true; }
                                    else if (event.key === Qt.Key_Left) { if (currentIdx - itemsPerCol >= 0) { filesPanelContainer.keyboardActiveFileIndex = currentIdx - itemsPerCol; triggerFileSelection(currentIdx - itemsPerCol); } event.accepted = true; }
                                    else if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return) { if (currentIdx >= 0) triggerFileDoubleClicked(currentIdx); event.accepted = true; }
                                }

                                delegate: Column {
                                    id: columnBlock;
                                    width: filesPanelContainer.gridColumnWidth;
                                    spacing: 0
                                    property int startIdx: index * filesPanelContainer.rowsPerColumn;
                                    property int endIdx: Math.min(startIdx + filesPanelContainer.rowsPerColumn, appOrchestrator.currentFolderFiles.length)

                                    Repeater {
                                        model: columnBlock.endIdx - columnBlock.startIdx
                                        delegate: ItemDelegate {
                                            property int targetFileIndex: columnBlock.startIdx + index;
                                            property string currentFileName: appOrchestrator.currentFolderFiles[targetFileIndex];
                                            property bool isKeyboardSelected: filesPanelContainer.keyboardActiveFileIndex === targetFileIndex
                                            // A replacement is in the mods folder (Replace File / the TXPK / M3MP viewers).
                                            readonly property bool modded: appOrchestrator.modFilesRevision >= 0
                                                                           && appOrchestrator.hasModFile(mainFormView.filePathOf(currentFileName))
                                            width: filesPanelContainer.gridColumnWidth;
                                            height: filesPanelContainer.itemRowHeight;
                                            leftPadding: 4;
                                            rightPadding: 4;
                                            highlighted: isKeyboardSelected
                                            background: Rectangle {
                                                readonly property bool dropTarget: filesDrop.containsDrag && filesDrop.hoverIndex === targetFileIndex
                                                color: isKeyboardSelected ? (fileMasterListView.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.accentDim) : (hovered || dropTarget ? BtDarkStyle.bgHover : "transparent");
                                                border.width: dropTarget ? 1 : 0
                                                border.color: BtDarkStyle.accentColor
                                                radius: 2
                                            }
                                            contentItem: RowLayout {
                                                anchors.fill: parent;
                                                spacing: 6

                                                Image {
                                                    id: extIcon;
                                                    source: filesPanelContainer.getFileExtensionIcon(currentFileName);
                                                    Layout.preferredWidth: 16;
                                                    Layout.preferredHeight: 16;
                                                    fillMode: Image.PreserveAspectFit;
                                                    Layout.alignment: Qt.AlignVCenter;
                                                    asynchronous: true;
                                                    cache: true
                                                }

                                                Label {
                                                    text: currentFileName;
                                                    color: isKeyboardSelected ? "#121214" : (hovered ? BtDarkStyle.accentColor : BtDarkStyle.textMain);
                                                    font.family: "Consolas";
                                                    font.pixelSize: 11;
                                                    elide: Text.ElideNone;
                                                    Layout.fillWidth: true;
                                                    verticalAlignment: Text.AlignVCenter
                                                }

                                                Rectangle {
                                                    visible: modded
                                                    Layout.alignment: Qt.AlignVCenter
                                                    implicitWidth: modBadge.implicitWidth + 8
                                                    implicitHeight: 14
                                                    radius: 3
                                                    color: isKeyboardSelected ? "#121214" : BtDarkStyle.accentDim
                                                    Text {
                                                        id: modBadge
                                                        anchors.centerIn: parent
                                                        text: "MOD"
                                                        color: isKeyboardSelected ? BtDarkStyle.accentColor : "#121214"
                                                        font.pixelSize: 8
                                                        font.bold: true
                                                    }
                                                    BtToolTip { text: "Replaced: the mods folder has this file (Modify puts it into the game)" }
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent;
                                                hoverEnabled: true;
                                                acceptedButtons: Qt.LeftButton | Qt.RightButton;
                                                onClicked: (mouse) => {
                                                    if (mouse.button === Qt.RightButton) {
                                                        // Right click: highlight it and offer Save As (no preview / playback).
                                                        filesPanelContainer.keyboardActiveFileIndex = targetFileIndex;
                                                        fileMasterListView.forceActiveFocus();
                                                        saveAsMenu.openFor(targetFileIndex);
                                                        return;
                                                    }
                                                    filesPanelContainer.keyboardActiveFileIndex = targetFileIndex;
                                                    fileMasterListView.forceActiveFocus(); triggerFileSelection(targetFileIndex)
                                                }
                                                onDoubleClicked: triggerFileDoubleClicked(targetFileIndex)
                                            }
                                        }
                                    }
                                }
                                ScrollBar.horizontal: ScrollBar {
                                    active: true;
                                    policy: ScrollBar.AsNeeded
                                }
                            }

                            // Replace File by drag and drop: one drop area over the whole panel (on top of the file
                            // list, so drops always arrive here). Files and folders are matched by name / path:
                            // see AppOrchestrator::replaceDropped(). One file that matches nothing replaces the file
                            // it was dropped on.
                            DropArea {
                                id: filesDrop
                                anchors.fill: parent
                                property int hoverIndex: -1   // The file under the cursor while dragging.

                                function fileIndexAt(x, y) {
                                    const files = appOrchestrator.currentFolderFiles || []
                                    const p = fileMasterListView.mapFromItem(filesDrop, x, y)
                                    const column = fileMasterListView.indexAt(p.x + fileMasterListView.contentX,
                                                                              p.y + fileMasterListView.contentY)
                                    if (column < 0)
                                        return -1
                                    const row = Math.floor((p.y + fileMasterListView.contentY) / filesPanelContainer.itemRowHeight)
                                    if (row < 0 || row >= filesPanelContainer.rowsPerColumn)
                                        return -1
                                    const index = column * filesPanelContainer.rowsPerColumn + row
                                    return index < files.length ? index : -1
                                }

                                onPositionChanged: (drag) => hoverIndex = fileIndexAt(drag.x, drag.y)
                                onExited: hoverIndex = -1
                                onDropped: (drop) => {
                                    hoverIndex = -1
                                    if (!drop.hasUrls)
                                        return
                                    const urls = []
                                    for (let i = 0; i < drop.urls.length; ++i)
                                        urls.push(drop.urls[i].toString())
                                    const index = fileIndexAt(drop.x, drop.y)
                                    const target = index >= 0 ? mainFormView.describeFile(index) : null
                                    appOrchestrator.replaceDropped(urls, mainFormView.currentFolderPath(),
                                                                   target !== null ? target.path : "")
                                    drop.acceptProposedAction()
                                }
                            }

                            // While dragging: what a drop will do.
                            Rectangle {
                                visible: filesDrop.containsDrag
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 20
                                width: dropHint.implicitWidth + 20
                                height: 26
                                radius: 4
                                color: BtDarkStyle.bgMedium
                                border.color: BtDarkStyle.accentColor
                                Text {
                                    id: dropHint
                                    anchors.centerIn: parent
                                    text: "Drop files or folders to replace the matching files (copied into the mods folder)"
                                    color: BtDarkStyle.accentColor
                                    font.pixelSize: 11
                                }
                            }

                            Label {
                                text: "Select a folder to view files";
                                color: BtDarkStyle.textDim;
                                font.italic: true;
                                anchors.centerIn: parent;
                                visible: !appOrchestrator.currentFolderFiles || appOrchestrator.currentFolderFiles.length === 0 }
                        }
                    }
                }

                SplitView {
                    SplitView.preferredWidth: 580;
                    SplitView.minimumWidth: 250;
                    orientation: Qt.Vertical
                    handle: Rectangle {
                        implicitHeight: 4;
                        color: SplitHandle.hovered ? BtDarkStyle.accentColor : BtDarkStyle.borderDark;

                        Behavior on color {

                            ColorAnimation {
                                duration: 100
                            }
                        }
                    }

                    Rectangle {
                        SplitView.preferredHeight: 350;
                        SplitView.minimumHeight: 100;
                        color: BtDarkStyle.bgDarkest

                        ColumnLayout {
                            anchors.fill: parent;
                            anchors.margins: 10;
                            spacing: 8

                            Text {
                                text: "Console Log";
                                color: BtDarkStyle.accentColor;
                                font.bold: true;
                                font.pointSize: 10
                            }

                            Rectangle {
                                Layout.fillWidth: true;
                                Layout.fillHeight: true;
                                color: BtDarkStyle.bgDarkest;
                                border.color: BtDarkStyle.borderDark;
                                border.width: 1;
                                radius: 4;
                                clip: true

                                // One entry per log line (or per asset info block). A single big rich text area stopped
                                // repainting newly appended text until it was scrolled; a list draws every entry itself.
                                ListView {
                                    id: consoleOutput
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    clip: true
                                    boundsBehavior: Flickable.StopAtBounds
                                    flickableDirection: Flickable.AutoFlickIfNeeded
                                    contentWidth: Math.max(width, consoleWidest)
                                    model: ListModel { id: consoleModel }
                                    property real consoleWidest: 0

                                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                                    delegate: TextEdit {
                                        text: model.html
                                        textFormat: TextEdit.RichText
                                        wrapMode: TextEdit.NoWrap
                                        readOnly: true
                                        selectByMouse: true
                                        color: BtDarkStyle.accentDim
                                        selectionColor: BtDarkStyle.accentDim
                                        font.family: "Consolas"
                                        font.pointSize: 10
                                        onImplicitWidthChanged: if (implicitWidth > consoleOutput.consoleWidest) consoleOutput.consoleWidest = implicitWidth
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        SplitView.preferredHeight: 350;
                        SplitView.minimumHeight: 100;
                        color: BtDarkStyle.bgDarkest

                        ColumnLayout {
                            anchors.fill: parent;
                            anchors.margins: 10;
                            spacing: 8

                            Text {
                                text: "Image Preview";
                                color: BtDarkStyle.accentColor;
                                font.bold: true;
                                font.pointSize: 10
                            }
                            Rectangle {
                                Layout.fillWidth: true;
                                Layout.fillHeight: true;
                                color: BtDarkStyle.bgDark;
                                border.color: BtDarkStyle.borderDark;
                                border.width: 1;
                                radius: 4;
                                clip: true

                                Image {
                                    anchors.fill: parent;
                                    anchors.margins: 5;
                                    fillMode: Image.Tile;
                                    source: previewBridge.hasImage ? "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/grid_dark.bmp" : "";

                                    // Texture canvas: the mouse wheel zooms around the cursor, holding the middle mouse
                                    // button moves the texture (same as the DDS viewer). A new texture starts fitted.
                                    Item {
                                        id: previewCanvas
                                        anchors.fill: parent;
                                        anchors.margins: 5;
                                        clip: true

                                        property real zoom: 1.0          // 1.0 = fitted to the box.
                                        property real panX: 0            // Offset of the texture's centre from the box's centre.
                                        property real panY: 0
                                        readonly property real minZoom: 0.25
                                        readonly property real maxZoom: 32.0
                                        readonly property bool zoomable: previewBridge.hasImage && previewImage.status === Image.Ready
                                                                         && previewImage.sourceSize.width > 0 && previewImage.sourceSize.height > 0
                                        readonly property real fitScale: zoomable
                                            ? Math.min(width / previewImage.sourceSize.width, height / previewImage.sourceSize.height) : 1.0
                                        readonly property real displayScale: fitScale * zoom   // 1.0 = one texel per screen pixel.

                                        // Another texture (flip / alpha keep the view).
                                        property string resetKey: previewBridge.fileName + "|" + previewBridge.hasImage
                                        onResetKeyChanged: resetView()

                                        function resetView() {
                                            zoom = 1.0
                                            panX = 0
                                            panY = 0
                                        }

                                        // At least 40 px of the texture stays in the box.
                                        function clampPan() {
                                            const limitX = Math.max(0, (width + previewImage.width) / 2 - 40)
                                            const limitY = Math.max(0, (height + previewImage.height) / 2 - 40)
                                            panX = Math.max(-limitX, Math.min(limitX, panX))
                                            panY = Math.max(-limitY, Math.min(limitY, panY))
                                        }

                                        // Zooms by `factor`, keeping the texel under (px, py) under the cursor.
                                        function zoomAt(factor, px, py) {
                                            const newZoom = Math.max(minZoom, Math.min(maxZoom, zoom * factor))
                                            if (!zoomable || newZoom === zoom)
                                                return
                                            const texelX = (px - width / 2 - panX) / displayScale
                                            const texelY = (py - height / 2 - panY) / displayScale
                                            zoom = newZoom
                                            panX = px - width / 2 - texelX * displayScale
                                            panY = py - height / 2 - texelY * displayScale
                                            clampPan()
                                        }

                                        Image {
                                            id: previewImage
                                            // Zoomable texture: its own size times the zoom, centred plus the pan.
                                            // Otherwise (the game's picture / the logo) simply fitted into the box.
                                            width: previewCanvas.zoomable ? sourceSize.width * previewCanvas.displayScale : previewCanvas.width
                                            height: previewCanvas.zoomable ? sourceSize.height * previewCanvas.displayScale : previewCanvas.height
                                            x: (previewCanvas.width - width) / 2 + (previewCanvas.zoomable ? previewCanvas.panX : 0)
                                            y: (previewCanvas.height - height) / 2 + (previewCanvas.zoomable ? previewCanvas.panY : 0)
                                            fillMode: Image.PreserveAspectFit;
                                            smooth: !previewCanvas.zoomable || previewCanvas.displayScale < 2   // Crisp texels when zoomed in.
                                            // No texture selected: the selected game's picture (as on the game selection screen),
                                            // or the Blobset Tools logo when the game has none.
                                            source: previewBridge.hasImage ? previewBridge.imageSource
                                                  : appOrchestrator.currentGameImage !== "" ? appOrchestrator.currentGameImage
                                                  : "qrc:/qt/qml/com/wouldymods/blobset/assets/blobset_tools.png";
                                        }

                                        // Wheel: zoom. Middle button held: move the texture.
                                        MouseArea {
                                            anchors.fill: parent
                                            acceptedButtons: Qt.MiddleButton
                                            enabled: previewCanvas.zoomable
                                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                                            property real lastX: 0
                                            property real lastY: 0
                                            onPressed: (mouse) => { lastX = mouse.x; lastY = mouse.y }
                                            onPositionChanged: (mouse) => {
                                                previewCanvas.panX += mouse.x - lastX
                                                previewCanvas.panY += mouse.y - lastY
                                                previewCanvas.clampPan()
                                                lastX = mouse.x
                                                lastY = mouse.y
                                            }
                                            onWheel: (wheel) => {
                                                if (wheel.angleDelta.y === 0) {
                                                    wheel.accepted = false
                                                    return
                                                }
                                                previewCanvas.zoomAt(Math.pow(1.0015, wheel.angleDelta.y), wheel.x, wheel.y)   // ~20% per notch.
                                            }
                                        }

                                        // Zoom level (top right) once zoomed or moved. Click to fit the texture again.
                                        Rectangle {
                                            anchors.top: parent.top
                                            anchors.right: parent.right
                                            anchors.margins: 6
                                            width: previewZoomLabel.implicitWidth + 14
                                            height: 22
                                            radius: 4
                                            color: previewZoomArea.containsMouse ? BtDarkStyle.bgMedium : BtDarkStyle.bgDark
                                            border.width: 1
                                            border.color: previewZoomArea.containsMouse ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                                            opacity: 0.9
                                            visible: previewCanvas.zoomable
                                                     && (previewCanvas.zoom !== 1.0 || previewCanvas.panX !== 0 || previewCanvas.panY !== 0)

                                            Text {
                                                id: previewZoomLabel
                                                anchors.centerIn: parent
                                                text: Math.round(previewCanvas.displayScale * 100) + "%  ⟲"
                                                color: previewZoomArea.containsMouse ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                                                font.family: "Consolas"
                                                font.pixelSize: 11
                                            }

                                            MouseArea {
                                                id: previewZoomArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: previewCanvas.resetView()
                                            }

                                            ToolTip.visible: previewZoomArea.containsMouse
                                            ToolTip.text: "Scroll to zoom, hold the middle mouse button to move. Click to fit."
                                        }
                                    }

                                    Rectangle {
                                        anchors.fill: parent;
                                        anchors.margins: 5;
                                        color: "transparent";
                                        border.color: previewBridge.hasImage ? "transparent" : BtDarkStyle.borderDark;
                                        border.width: 1
                                    }
                                }

                                // Right click anywhere on the preview opens the image menu
                                MouseArea {
                                    anchors.fill: parent;
                                    acceptedButtons: Qt.RightButton;
                                    onClicked: previewMenu.popup()
                                }

                                Menu {
                                    id: previewMenu

                                    // 1. Change the width of the menu here
                                    width: 150

                                    // 2. Add an exterior drop shadow via the background property
                                    background: Rectangle {
                                        id: bgRect
                                        color: "#2d2d2d" // Matches your dark theme color
                                        border.color: "#3f3f3f"
                                        radius: 4

                                        layer.enabled: true
                                        layer.effect: MultiEffect {
                                            shadowEnabled: true
                                            shadowColor: "#80000000" // Semi-transparent black shadow
                                            shadowBlur: 0.6          // Softness of the shadow edge
                                            shadowVerticalOffset: 3  // Pushes shadow downward
                                            shadowHorizontalOffset: 0
                                        }
                                    }

                                    MenuItem {
                                        text: "Flip Image"
                                        enabled: previewBridge.hasImage
                                        onTriggered: previewBridge.flipImage()

                                        icon.source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/flip_image_32.png"
                                        icon.color: "transparent"   // Full color icon (Material tints icons by default).
                                        icon.width: 20
                                        icon.height: 20

                                        // 3. Set the exact height of this item to 25
                                        height: 40
                                    }

                                    MenuItem {
                                        text: "Alpha"
                                        enabled: previewBridge.hasImage
                                        checkable: true
                                        checked: previewBridge.alphaEnabled
                                        onToggled: previewBridge.toggleAlpha(checked)

                                        icon.source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/alpha_32.png"
                                        icon.color: "transparent"   // Full color icon (Material tints icons by default).
                                        icon.width: 20
                                        icon.height: 20

                                        // 3. Set the exact height of this item to 25
                                        height: 40
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        ToolBar {
            id: statusFooterBar;
            Layout.fillWidth: true;
            Layout.preferredHeight: 30
            background: Rectangle {
                color: BtDarkStyle.bgDark;

                Rectangle {
                    anchors.top: parent.top;
                    anchors.left: parent.left;
                    anchors.right: parent.right;
                    height: 1;
                    color: BtDarkStyle.borderDark
                }
            }

            RowLayout {
                anchors.fill: parent;
                anchors.leftMargin: 15;
                anchors.rightMargin: 15;
                height: 30

                Text {
                    id: statusOutput;
                    text: "Status: Ready | Blobset Tools Sanbox Mode";
                    color: BtDarkStyle.textDim;
                    font.pointSize: 9;
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: 10;
                    Layout.alignment: Qt.AlignVCenter

                    Text {
                        id: statusProgress; text: "";
                        color: BtDarkStyle.textDim;
                        font.pointSize: 9
                    }

                    ProgressBar {
                        id: statusBarProgress;
                        value: 0;
                        implicitWidth: 150;
                        implicitHeight: 6;
                        Layout.alignment: Qt.AlignVCenter;
                        background: Rectangle {
                            implicitWidth: statusBarProgress.implicitWidth;
                            implicitHeight: statusBarProgress.implicitHeight;
                            color: BtDarkStyle.bgMedium;
                            radius: 3
                        }

                        contentItem: Item {
                            implicitWidth: statusBarProgress.implicitWidth;
                            implicitHeight: statusBarProgress.implicitHeight;

                            Rectangle {
                                width: statusBarProgress.visualPosition * parent.width;
                                height: parent.height;
                                radius: 3;
                                color: BtDarkStyle.accentColor
                            }
                        }
                    }

                    Text {
                        text: "v" + Qt.application.version;
                        color: BtDarkStyle.accentColor;
                        font.pointSize: 9
                    }
                }
            }
        }
    }

    // No usable file mapping for the selected game (first time, or an empty / damaged one): nothing can be shown
    // until Update File Mapping Data has run, so offer to run it.
    MessageDialog {
        id: fileMappingDialog
        title: "Update File Mapping"
        text: "There's no file mapping for " + (appOrchestrator.currentGameName || "this game") + " yet."
        informativeText: "Blobset Tools needs the file mapping to show the game's files. Run Update File Mapping Data now?\n\n"
                         + "It can take a few minutes. You can also run it later from Options > Update File Mapping Data."
        buttons: MessageDialog.Yes | MessageDialog.No
        onButtonClicked: function (button, role) {
            if (button === MessageDialog.Yes) {
                appOrchestrator.clearBrowserState();   // Like Options > Update File Mapping Data.
                appOrchestrator.updateFileMapping()
            }
        }
    }

    // Steam games: an update is waiting in Steam (restore the original files before it installs), or the game was
    // updated since its file mapping was made (make the mapping again).
    MessageDialog {
        id: steamUpdateDialog
        property bool pending: false
        property string gameName: ""
        property string details: ""
        title: pending ? "Steam Update Waiting" : "Game Updated"
        text: pending ? "Steam has an update waiting for " + gameName + "."
                      : gameName + " has been updated by Steam."
        informativeText: (details !== "" ? details + "\n\n" : "")
                         + (pending ? "Before you let Steam install it, run Restore Backup Files so the update starts from the "
                                      + "game's original files. After the update, run Update File Mapping Data.\n\n"
                                      + "Restore Backup Files now?"
                                    : "The file mapping was made for the previous version. Run Update File Mapping Data "
                                      + "now? Then use Modify / Build to put your mods back in.\n\n"
                                      + "If the update failed, use Options > Validate Steam Files first.")
        buttons: MessageDialog.Yes | MessageDialog.No
        onButtonClicked: function (button, role) {
            if (button !== MessageDialog.Yes)
                return
            if (pending) {
                appOrchestrator.restoreBackupFiles()
            } else {
                appOrchestrator.clearBrowserState();
                appOrchestrator.updateFileMapping()
            }
        }
    }

    function promptFileMapping() {
        if (appOrchestrator.fileMappingMissing && !appOrchestrator.workerRunning && !fileMappingDialog.visible)
            fileMappingDialog.open()
    }

    // Restore replaces the game's files with the backups, so it asks first.
    MessageDialog {
        id: restoreConfirmDialog
        title: "Restore Backup Files"
        text: "Put the game's original files back from this game's backup folder?"
        informativeText: "Everything Modify / Create changed is undone (the Mod Loader mods of V4 games are removed too). "
                         + "Your files in the mods folder are kept. Make sure the game isn't running."
        buttons: MessageDialog.Yes | MessageDialog.Cancel
        onButtonClicked: function (button, role) {
            if (button === MessageDialog.Yes)
                appOrchestrator.restoreBackupFiles()
        }
    }

    // Create replaces the game's blobset, so it asks first.
    MessageDialog {
        id: createConfirmDialog
        title: "Create Blobset"
        text: "Create builds a new blobset from ONLY the files in this game's mods folder and replaces the game's "
              + appOrchestrator.expectedBlobsetName + " with it."
        informativeText: "The first time, the original blobset is copied to the game's backup folder before anything "
                         + "is replaced (this can take a while for large games). Continue?"
        buttons: MessageDialog.Yes | MessageDialog.Cancel
        onButtonClicked: function (button, role) {
            if (button === MessageDialog.Yes) {
                appOrchestrator.uiConsoleUpdate("Archive compiler initialization sequence triggered.")
                appOrchestrator.createBlobset()
            }
        }
    }

    Connections {
        target: appOrchestrator
        function onGameLocationRequested() { if (!gameLocationDialog.visible) gameLocationDialog.open() }
        function onSteamUpdateDetected(pending, gameName, details) {
            if (fileMappingDialog.visible || steamUpdateDialog.visible)
                return
            steamUpdateDialog.pending = pending
            steamUpdateDialog.gameName = gameName
            steamUpdateDialog.details = details
            Qt.callLater(steamUpdateDialog.open)
        }
        function onFileMappingMissingChanged() { if (appOrchestrator.fileMappingMissing) Qt.callLater(mainFormView.promptFileMapping) }
        function onUiConsoleUpdate(message) { mainFormView.logLine(message) }
        // The selected file's metadata replaces the console contents.
        function onAssetInfoReady(html) {
            consoleModel.clear();
            consoleOutput.consoleWidest = 0;
            consoleModel.append({ html: html });
            consoleOutput.positionViewAtBeginning();
        }
        function onUiStatusUpdate(message) { statusOutput.text = message }
        function onUiProgressUpdate(percentage) { statusBarProgress.value = percentage / 100.0; statusProgress.text = percentage + "%" }
        function onCurrentFolderFilesChanged() { filesPanelContainer.keyboardActiveFileIndex = -1; audioPlayer.stop(); }
    }

    // ---------------------------------------------------------------------------------------------------
    // Folder / file selection helpers
    // ---------------------------------------------------------------------------------------------------
    function currentFolderPath() {
        var idx = folderSelection.currentIndex;
        return idx.valid ? appOrchestrator.treeModel.getPathFromIndex(idx) : "";
    }

    // Selects the folder shown on visual row `row` of the tree and keeps it in view.
    function selectFolderRow(row) {
        folderSelection.setCurrentIndex(packageTreeView.modelIndex(Qt.point(0, row)), ItemSelectionModel.ClearAndSelect);
        packageTreeView.positionViewAtRow(row, TableView.Contain);
    }

    function scrollFileIntoView(fileIdx) {
        var colIndex = Math.floor(fileIdx / filesPanelContainer.rowsPerColumn);
        fileMasterListView.positionViewAtIndex(colIndex, ListView.Contain);
    }

    // { name, path, ext, hash } of file `fileIdx` of the selected folder, or null when nothing is selected.
    function describeFile(fileIdx) {
        var name = appOrchestrator.currentFolderFiles[fileIdx];
        if (name === undefined || !folderSelection.currentIndex.valid) return null;
        var folderPath = currentFolderPath();
        var path = folderPath ? (folderPath + "/" + name) : name;
        var dot = name.lastIndexOf(".");
        return { name: name, path: path, ext: dot !== -1 ? name.substring(dot + 1).toLowerCase() : "",
                 hash: appOrchestrator.getHashFromPath(path) };
    }

    // The blobset path of `name` in the selected folder.
    function filePathOf(name) {
        const folderPath = currentFolderPath()
        return folderPath ? (folderPath + "/" + name) : name
    }

    // Remembers the selection in AppOrchestrator so it outlives this view.
    function persistBrowserState(fileName) {
        appOrchestrator.saveBrowserState(currentFolderPath(), fileName);
    }

    // Shows `f` in the Image Preview when it is a DDS texture, otherwise falls back to the logo.
    function updateDdsPreview(f) {
        if (f !== null && f.ext === "dds") previewBridge.loadHash(f.hash);
        else previewBridge.clear();
    }

    // Appends one "> message" log line to the Console Log (escaped, the console shows rich text).
    function logLine(message) {
        var escaped = String(message).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        // Follows new lines while the view is at the bottom; scrolled up, it stays where it is.
        var follow = consoleModel.count === 0 || consoleOutput.atYEnd;
        consoleModel.append({ html: "<span style=\"color:" + BtDarkStyle.accentDim + ";\">&gt; " + escaped + "</span>" });
        if (consoleModel.count > 2000)
            consoleModel.remove(0, consoleModel.count - 2000);   // Keep the log light.
        if (follow)
            Qt.callLater(consoleOutput.positionViewAtEnd);
    }

    function triggerFileSelection(fileIdx) {
        var f = describeFile(fileIdx);
        if (f === null) return;
        appOrchestrator.requestAssetInfo(f.hash, f.path);   // Refreshes the Console Log with its metadata.
        updateDdsPreview(f);
        if (f.ext === "wem") audioPlayer.playWemByHash(f.hash);
        else if (f.ext === "wav") audioPlayer.playWavByHash(f.hash, f.path);
        else audioPlayer.stop();
        scrollFileIntoView(fileIdx);
        persistBrowserState(f.name);
    }

    function triggerFileDoubleClicked(fileIdx) {
        var f = describeFile(fileIdx);
        if (f === null) return;
        persistBrowserState(f.name);                       // This view is about to be replaced.
        appOrchestrator.openWorkspaceFileByHash(f.hash, f.ext, f.path);
    }

    // ---------------------------------------------------------------------------------------------------
    // Search bar: finds assets / folders and selects them in the tree and file list (nothing is filtered).
    // ---------------------------------------------------------------------------------------------------
    function runSearch(query, selectFirst) {
        searchQuery = query.trim();
        searchResults = searchQuery === "" ? [] : appOrchestrator.searchAssets(searchQuery);
        searchPosition = -1;
        if (searchQuery === "") return;
        if (searchResults.length === 0) { logLine("No assets match \"" + searchQuery + "\"."); return; }
        if (selectFirst) showSearchResult(0);
    }

    // direction: 1 = next match, -1 = previous (wraps around).
    function stepSearch(direction) {
        if (globalSearchInput.text.trim() !== searchQuery) { runSearch(globalSearchInput.text, true); return; }
        if (searchResults.length === 0) return;
        var next = searchPosition < 0 ? 0 : (searchPosition + direction + searchResults.length) % searchResults.length;
        showSearchResult(next);
    }

    function showSearchResult(i) {
        var r = searchResults[i];
        if (r === undefined) return;
        searchPosition = i;

        var folderIdx = appOrchestrator.treeModel.indexFromPath(r.folder);
        if (!folderIdx.valid) { logLine("Can't show " + r.path + " (it isn't in the folder tree)."); return; }

        packageTreeView.expandToIndex(folderIdx);          // Open every ancestor so the folder has a row.
        folderSelection.setCurrentIndex(folderIdx, ItemSelectionModel.ClearAndSelect);   // Refills the file list.

        var fileIdx = r.file !== "" ? appOrchestrator.currentFolderFiles.indexOf(r.file) : -1;
        filesPanelContainer.keyboardActiveFileIndex = fileIdx;
        if (fileIdx >= 0) triggerFileSelection(fileIdx);   // Preview, Console info and audio, like a click.
        else { updateDdsPreview(null); audioPlayer.stop(); persistBrowserState(""); }
        restoreScrollTimer.restart();                      // Scroll the folder row and the file into view.
    }

    // Re-applies the folder + file that were selected before this view was destroyed.
    function restoreBrowserState() {
        if (!restorePending || packageTreeView.rows <= 0) return;
        restorePending = false;

        var folderPath = appOrchestrator.savedFolderPath();
        var fileName = appOrchestrator.savedFileName();
        var folderIdx = folderPath !== "" ? appOrchestrator.treeModel.indexFromPath(folderPath) : null;
        if (folderIdx === null || !folderIdx.valid) {
            // Nothing (valid) remembered, e.g. coming from Game Selection: start on the first folder.
            selectFolderRow(0);
            packageTreeView.forceActiveFocus();
            return;
        }

        restoringBrowserState = true;
        packageTreeView.expandToIndex(folderIdx);          // Open every ancestor so the folder has a row.
        folderSelection.setCurrentIndex(folderIdx, ItemSelectionModel.ClearAndSelect);   // Refills the file list.

        var fileIdx = fileName !== "" ? appOrchestrator.currentFolderFiles.indexOf(fileName) : -1;
        if (fileIdx >= 0) {
            filesPanelContainer.keyboardActiveFileIndex = fileIdx;
            var f = describeFile(fileIdx);                  // Preview only - never auto play audio on return.
            updateDdsPreview(f);
            if (f !== null) appOrchestrator.requestAssetInfo(f.hash, f.path);
            fileMasterListView.forceActiveFocus();
        } else {
            packageTreeView.forceActiveFocus();
        }
        restoringBrowserState = false;
        restoreScrollTimer.restart();                      // Scroll once the views have their final size.
    }

    Timer {
        id: restoreScrollTimer
        interval: 60
        onTriggered: {
            var idx = folderSelection.currentIndex;
            if (idx.valid) packageTreeView.positionViewAtRow(packageTreeView.rowAtIndex(idx), TableView.Contain);
            if (filesPanelContainer.keyboardActiveFileIndex >= 0) mainFormView.scrollFileIntoView(filesPanelContainer.keyboardActiveFileIndex);
        }
    }

    Component.onCompleted: {
        logLine("Blobset Tools Modern Engine Initialized...")
        // Data folder setup at startup (first run, files moved from an older version).
        const startup = appOrchestrator.takeStartupMessages()
        for (let i = 0; i < startup.length; ++i)
            logLine(startup[i])
        packageTreeView.forceActiveFocus()
        restoreBrowserState()
        if (appOrchestrator.gameLocationRequired)
            Qt.callLater(gameLocationDialog.open)
        else if (appOrchestrator.fileMappingMissing)
            Qt.callLater(promptFileMapping)
    }

    Component.onDestruction: {
        audioPlayer.stop()
    }

    // ---------------------------------------------------------------------------------------------------
    // Save As (right click on a file)
    //   .dds: DDS or PNG   .wem: WEM or WAV   .txpk / .m3mp: none (open them in their viewer)   others: as stored
    // ---------------------------------------------------------------------------------------------------
    Settings {
        id: saveAsSettings
        category: "SaveAs"
        property string lastFolder: ""     // file:/// URL of the folder last saved to.
        property string lastReplaceFolder: ""   // file:/// URL of the folder Replace File last picked from.
    }

    Menu {
        id: saveAsMenu
        width: 200

        property var file: null            // describeFile() of the clicked file.
        readonly property string ext: file ? file.ext : ""

        // .txpk / .m3mp aren't saved from here (open them in their viewer) but can be replaced.
        readonly property bool canSave: ext !== "txpk" && ext !== "m3mp"
        property bool modded: false

        function openFor(fileIdx) {
            const f = mainFormView.describeFile(fileIdx)
            if (f === null)
                return
            file = f
            modded = appOrchestrator.hasModFile(f.path)
            popup()
        }

        background: Rectangle {
            color: "#2d2d2d"
            border.color: "#3f3f3f"
            radius: 4
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#80000000"
                shadowBlur: 0.6
                shadowVerticalOffset: 3
                shadowHorizontalOffset: 0
            }
        }

        MenuItem {
            text: "Save as DDS..."
            visible: saveAsMenu.ext === "dds"
            height: visible ? 40 : 0
            icon.source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/dds_32.png"
            icon.color: "transparent"
            icon.width: 20
            icon.height: 20
            onTriggered: saveAsDialog.start(saveAsMenu.file, "dds", "dds", "DDS texture (*.dds)")
        }
        MenuItem {
            text: "Save as PNG..."
            visible: saveAsMenu.ext === "dds"
            height: visible ? 40 : 0
            icon.source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/png_32.png"
            icon.color: "transparent"
            icon.width: 20
            icon.height: 20
            onTriggered: saveAsDialog.start(saveAsMenu.file, "png", "png", "PNG image (*.png)")
        }
        MenuItem {
            text: "Save as WEM..."
            visible: saveAsMenu.ext === "wem"
            height: visible ? 40 : 0
            icon.source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/wem_32.png"
            icon.color: "transparent"
            icon.width: 20
            icon.height: 20
            onTriggered: saveAsDialog.start(saveAsMenu.file, "wem", "wem", "Wwise audio (*.wem)")
        }
        MenuItem {
            text: "Save as WAV..."
            visible: saveAsMenu.ext === "wem"
            height: visible ? 40 : 0
            icon.source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/wav_32.png"
            icon.color: "transparent"
            icon.width: 20
            icon.height: 20
            onTriggered: saveAsDialog.start(saveAsMenu.file, "wav", "wav", "WAV audio (*.wav)")
        }
        MenuItem {
            text: "Save As..."
            visible: saveAsMenu.canSave && saveAsMenu.ext !== "dds" && saveAsMenu.ext !== "wem"
            height: visible ? 40 : 0
            icon.source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/save_32.png"
            icon.color: "transparent"
            icon.width: 20
            icon.height: 20
            onTriggered: {
                const ext = saveAsMenu.ext
                saveAsDialog.start(saveAsMenu.file, "raw", ext,
                                   ext !== "" ? ext.toUpperCase() + " file (*." + ext + ")" : "All files (*)")
            }
        }

        MenuSeparator { visible: saveAsMenu.canSave; height: visible ? implicitHeight : 0 }

        MenuItem {
            text: "Replace File..."
            height: 40
            icon.source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/modify_32.png"
            icon.color: "transparent"
            icon.width: 20
            icon.height: 20
            onTriggered: replaceDialog.start(saveAsMenu.file)
        }
        MenuItem {
            text: "Remove Replacement"
            visible: saveAsMenu.modded
            height: visible ? 40 : 0
            icon.source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/close_32.png"
            icon.color: "transparent"
            icon.width: 20
            icon.height: 20
            onTriggered: appOrchestrator.removeModFile(saveAsMenu.file.path)
        }
        MenuItem {
            text: "Open in Mods Folder"
            visible: saveAsMenu.modded
            height: visible ? 40 : 0
            icon.source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/folder_32.png"
            icon.color: "transparent"
            icon.width: 20
            icon.height: 20
            onTriggered: appOrchestrator.showModFile(saveAsMenu.file.path)
        }
    }

    // Replace File...: picks the replacement (same extension) and copies it into the mods folder.
    FileDialog {
        id: replaceDialog
        fileMode: FileDialog.OpenFile
        property var file: null

        function start(f) {
            file = f
            title = "Replace " + f.name
            // A .wem can also be replaced by a .wav (converted to a PCM WEM with the original's header).
            nameFilters = f.ext === "wem" ? ["Wwise audio or WAV (*.wem *.wav)", "WAV (*.wav)", "Wwise audio (*.wem)"]
                        : f.ext !== "" ? [f.ext.toUpperCase() + " file (*." + f.ext + ")"] : ["All files (*)"]
            if (saveAsSettings.lastReplaceFolder !== "")
                currentFolder = saveAsSettings.lastReplaceFolder
            open()
        }

        onAccepted: {
            const url = selectedFile.toString()
            saveAsSettings.lastReplaceFolder = url.substring(0, url.lastIndexOf("/"))
            appOrchestrator.replaceAsset(file.path, url)
        }
    }

    FileDialog {
        id: saveAsDialog
        fileMode: FileDialog.SaveFile

        property var file: null
        property string format: "raw"

        // f: describeFile(), extension: of the saved file, filter: "Name (*.ext)".
        function start(f, fmt, extension, filter) {
            file = f
            format = fmt
            const dot = f.name.lastIndexOf(".")
            const base = dot > 0 ? f.name.substring(0, dot) : f.name
            title = "Save " + f.name + (fmt === "raw" ? "" : " as " + fmt.toUpperCase())
            nameFilters = extension !== "" ? [filter, "All files (*)"] : ["All files (*)"]
            defaultSuffix = extension
            const folder = saveAsSettings.lastFolder !== "" ? saveAsSettings.lastFolder : StandardPaths.writableLocation(StandardPaths.DocumentsLocation).toString()
            currentFolder = folder
            selectedFile = folder + "/" + encodeURIComponent(base + (extension !== "" ? "." + extension : ""))
            open()
        }

        onAccepted: {
            const url = selectedFile.toString()
            saveAsSettings.lastFolder = url.substring(0, url.lastIndexOf("/"))
            appOrchestrator.saveAssetAs(file.hash, file.path, url, format,
                                        previewBridge.flipped, previewBridge.alphaEnabled)
        }
    }

    // About window (qml/subview_viewers/About.qml).
    About {
        id: aboutWindow
    }
}
