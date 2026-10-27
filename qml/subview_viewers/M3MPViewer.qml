import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import com.wouldymods.blobset 1.0

// M3MP viewer: same layout and behaviour as TXPKViewer.qml. Images (DDS / PNG / JPEG / BMP) get the zoomable
// preview with Flip / Alpha, text files are shown as text, everything else as a hex dump.

Item {
    id: m3mpViewRoot
    anchors.fill: parent

    // Optional: open a standalone .m3mp from disk instead of one from the loaded blobset.
    property string targetFilePath: ""
    property bool isBigEndian: false

    // Path hash of the M3MP opened from the blobset (set by AppOrchestrator::openM3mpViewerByHash).
    property string activeHashString: ""

    readonly property string iconDir: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/"

    Rectangle {
        anchors.fill: parent
        color: BtDarkStyle.bgDarkest
        z: -1
    }

    // Header button (same look as the Extract / Modify / Create buttons of MainForm.qml).
    component HeaderButton: AbstractButton {
        id: headerButton
        property string tip: ""   // Tooltip text.
        BtToolTip { text: headerButton.tip }
        property url iconSource: ""

        implicitHeight: 40
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        leftPadding: 12
        rightPadding: 12
        hoverEnabled: true
        opacity: enabled ? 1.0 : 0.4

        // Highlighted while hovered, and while checked (Alpha toggle), same as the DDS viewer's toolbar.
        readonly property bool lit: (hovered && enabled) || checked

        background: Rectangle {
            color: headerButton.lit ? BtDarkStyle.bgMedium : "transparent"
            border.color: headerButton.lit ? BtDarkStyle.accentColor : "transparent"
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
                color: headerButton.lit ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                font.bold: true
                font.pixelSize: 14
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // List icon of a file inside the M3MP.
    function fileIcon(name) {
        var n = (name || "").toLowerCase()
        if (n.endsWith(".dds")) return "dds_32.png"
        if (n.endsWith(".png")) return "png_32.png"
        if (n.endsWith(".lua")) return "lua_32.png"
        if (n.endsWith(".wav")) return "wav_32.png"
        return "file_32.png"
    }

    // Shows the root folder (and, while filtering, every folder) and selects the root folder.
    function selectRootFolder() {
        if (m3mpManager.visibleCount <= 0)
            return

        if (m3mpManager.filterText !== "")
            folderTree.expandRecursively()
        else
            folderTree.expand(0)

        folderSelection.setCurrentIndex(m3mpManager.rootFolderIndex(), ItemSelectionModel.ClearAndSelect)
    }

    Connections {
        target: appOrchestrator

        // Raised by the orchestrator right after it switched to this view.
        function onOpenM3mpViewerByHash(hexHash) {
            m3mpViewRoot.activeHashString = hexHash
            m3mpManager.loadM3mpFromBlobset(hexHash)
        }
    }

    M3mpToolManager {
        id: m3mpManager

        // An M3MP was loaded or the filter changed: the folder tree has been rebuilt.
        onTreeRebuilt: Qt.callLater(m3mpViewRoot.selectRootFolder)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------------------------------------------------------
        // Header
        // ------------------------------------------------------------------------------------------------
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
                    tip: "Return to the main workspace"
                    text: "◀ Back"
                    onClicked: m3mpViewRoot.leave()
                }

                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }

                // Vertically flips the preview (and PNG extraction).
                HeaderButton {
                    tip: "Flip the image vertically (also used when extracting as PNG)"
                    text: "Flip Image"
                    iconSource: m3mpViewRoot.iconDir + "flip_image_32.png"
                    enabled: m3mpManager.previewKind === "image"
                    onClicked: m3mpManager.flipImage()
                }

                // Alpha channel visibility: on = as stored in the texture, off = fully opaque.
                HeaderButton {
                    tip: "Show or hide the alpha channel (transparency)"
                    text: m3mpManager.alphaEnabled ? "Alpha: On" : "Alpha: Off"
                    iconSource: m3mpViewRoot.iconDir + "alpha_32.png"
                    enabled: m3mpManager.previewKind === "image"
                    checkable: true
                    checked: m3mpManager.alphaEnabled
                    onToggled: m3mpManager.toggleAlpha(checked)
                }

                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }

                HeaderButton {
                    tip: "Extract the selected file to a folder"
                    id: extractSelectedButton
                    text: "Extract Selected"
                    iconSource: m3mpViewRoot.iconDir + "extract_32.png"
                    enabled: !m3mpManager.busy && m3mpManager.currentEntry >= 0
                    onClicked: extractDialog.start(false)
                }

                HeaderButton {
                    tip: "Extract every file to a folder, plus M3MP_List.xml for the M3MP Creator"
                    id: extractAllButton
                    text: "Extract All"
                    iconSource: m3mpViewRoot.iconDir + "extract_32.png"
                    enabled: !m3mpManager.busy && m3mpManager.entryCount > 0
                    onClicked: extractDialog.start(true)
                }

                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }

                // Replacements (drag files onto the list, or right click > Replace) are kept until Save Changes
                // rebuilds the M3MP into the mods folder.
                HeaderButton {
                    tip: m3mpManager.modsTarget !== ""
                         ? "Rebuild the M3MP with your replacements into " + m3mpManager.modsTarget + " (then Modify puts it into the game)"
                         : "Open the M3MP from the game's files (double click it in the workspace) to save changes"
                    text: m3mpManager.unsavedChanges ? "Save Changes (" + m3mpManager.replacedCount + ") •" : "Save Changes"
                    iconSource: m3mpViewRoot.iconDir + "save_32.png"
                    enabled: !m3mpManager.busy && m3mpManager.unsavedChanges && m3mpManager.modsTarget !== ""
                    checked: m3mpManager.unsavedChanges
                    onClicked: m3mpManager.saveChanges()
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: m3mpManager.sourceName !== "" ? "M3MP " + m3mpManager.sourceName : ""
                    color: BtDarkStyle.textDim
                    font.family: "Consolas"
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 220
                }

                // File filter (top right, where MainForm.qml has its asset search).
                Rectangle {
                    Layout.preferredWidth: 500
                    Layout.minimumWidth: 180
                    Layout.preferredHeight: 34
                    color: BtDarkStyle.bgMedium
                    border.width: 1
                    border.color: filterInput.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                    radius: 4
                    opacity: m3mpManager.entryCount > 0 ? 1.0 : 0.5

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6

                        Text { text: "🔍"; font.pixelSize: 12; color: BtDarkStyle.textDim }

                        TextInput {
                            id: filterInput
                            Layout.fillWidth: true
                            color: BtDarkStyle.textMain
                            font.pixelSize: 13
                            font.family: "Consolas"
                            selectByMouse: true
                            clip: true
                            enabled: m3mpManager.entryCount > 0

                            onTextChanged: m3mpManager.filterText = text
                            Keys.onEscapePressed: text = ""

                            Text {
                                text: "Filter files..."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 13
                                font.italic: true
                                visible: filterInput.text === "" && !filterInput.activeFocus
                            }
                        }

                        Text {
                            visible: m3mpManager.filterText !== ""
                            text: m3mpManager.visibleCount + " / " + m3mpManager.entryCount
                            color: BtDarkStyle.textDim
                            font.family: "Consolas"
                            font.pixelSize: 11
                        }

                        Text {
                            visible: filterInput.text !== ""
                            text: "✕"
                            color: clearArea.containsMouse ? BtDarkStyle.accentColor : BtDarkStyle.textDim
                            font.pixelSize: 12

                            MouseArea {
                                BtToolTip { text: "Clear the filter" }
                                id: clearArea
                                anchors.fill: parent
                                anchors.margins: -4
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: filterInput.text = ""
                            }
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------------------------------------------
        // Workspace: folders | files | preview
        // ------------------------------------------------------------------------------------------------
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: 4
                color: SplitHandle.hovered ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                Behavior on color { ColorAnimation { duration: 100 } }
            }

            // ---- Folders ------------------------------------------------------------------------------
            Rectangle {
                SplitView.preferredWidth: 250
                SplitView.minimumWidth: 150
                color: BtDarkStyle.bgDarkest
                focus: true
                onActiveFocusChanged: if (activeFocus) folderTree.forceActiveFocus()

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Text { text: "Folders"; color: BtDarkStyle.accentColor; font.bold: true; font.pointSize: 10 }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: BtDarkStyle.bgDark
                        border.width: 1
                        border.color: folderTree.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                        radius: 4
                        clip: true

                        TreeView {
                            id: folderTree
                            anchors.fill: parent
                            anchors.margins: 4
                            model: m3mpManager.treeModel
                            clip: true
                            focus: true
                            boundsBehavior: Flickable.StopAtBounds
                            columnWidthProvider: function(column) { return Math.max(folderTree.width, 100) }

                            selectionModel: ItemSelectionModel { id: folderSelection }

                            // The selected folder decides which files the file list shows.
                            Connections {
                                target: folderSelection
                                function onCurrentChanged(current, previous) {
                                    m3mpManager.selectFolder(current)
                                }
                            }

                            Keys.onPressed: (event) => {
                                if (event.key === Qt.Key_Tab) {
                                    fileListView.forceActiveFocus()
                                    event.accepted = true
                                    return
                                }
                                if (folderTree.rows <= 0)
                                    return

                                var row = folderTree.rowAtIndex(folderSelection.currentIndex)
                                if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                                    var target = row < 0 ? 0
                                                         : Math.max(0, Math.min(folderTree.rows - 1,
                                                                                row + (event.key === Qt.Key_Down ? 1 : -1)))
                                    folderSelection.setCurrentIndex(folderTree.modelIndex(Qt.point(0, target)),
                                                                    ItemSelectionModel.ClearAndSelect)
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Right && row >= 0) {
                                    folderTree.expand(row)
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Left && row >= 0) {
                                    folderTree.collapse(row)
                                    event.accepted = true
                                }
                            }

                            delegate: TreeViewDelegate {
                                id: treeDelegate
                                implicitHeight: 36
                                indicator: null

                                background: Rectangle {
                                    color: treeDelegate.current
                                           ? (folderTree.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.accentDim)
                                           : (treeDelegate.hovered ? BtDarkStyle.borderDark : "transparent")
                                    radius: 3
                                }

                                contentItem: RowLayout {
                                    anchors.fill: parent
                                    spacing: 8

                                    Item { implicitWidth: treeDelegate.depth * 15; Layout.fillHeight: true }

                                    Text {
                                        text: treeDelegate.isTreeNode && treeDelegate.hasChildren
                                              ? (treeDelegate.expanded ? "▼" : "▶") : " "
                                        font.pixelSize: 11
                                        Layout.alignment: Qt.AlignVCenter
                                        color: treeDelegate.current ? "#121214" : BtDarkStyle.textDim

                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: folderTree.toggleExpanded(row)
                                        }
                                    }

                                    Image {
                                        source: m3mpViewRoot.iconDir + "folder_32.png"
                                        Layout.preferredWidth: 16
                                        Layout.preferredHeight: 16
                                        Layout.alignment: Qt.AlignVCenter
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        cache: true
                                    }

                                    Label {
                                        text: model.fileName
                                        color: treeDelegate.current ? "#121214" : BtDarkStyle.textMain
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }

                                onClicked: {
                                    folderSelection.setCurrentIndex(folderTree.modelIndex(Qt.point(0, row)),
                                                                    ItemSelectionModel.ClearAndSelect)
                                    folderTree.forceActiveFocus()
                                }
                            }
                        }
                    }
                }
            }

            // ---- Files --------------------------------------------------------------------------------
            Rectangle {
                id: filesPanel
                SplitView.preferredWidth: 350
                SplitView.minimumWidth: 300
                color: BtDarkStyle.bgDarkest
                onActiveFocusChanged: if (activeFocus) fileListView.forceActiveFocus()

                readonly property int itemRowHeight: 22
                readonly property int gridColumnWidth: m3mpManager.maxFileItemWidth
                readonly property int rowsPerColumn: Math.max(1, Math.floor((fileListView.height - 16) / itemRowHeight))
                readonly property int totalColumns: Math.ceil(m3mpManager.currentFolderFiles.length / rowsPerColumn)

                Connections {
                    target: m3mpManager

                    // Keep the selected file in view (also covers keyboard navigation).
                    function onCurrentFileChanged() {
                        if (m3mpManager.currentFileIndex >= 0)
                            fileListView.positionViewAtIndex(
                                Math.floor(m3mpManager.currentFileIndex / filesPanel.rowsPerColumn), ListView.Contain)
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Text { text: "Files"; color: BtDarkStyle.accentColor; font.bold: true; font.pointSize: 10 }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: BtDarkStyle.bgDark
                        border.width: 1
                        border.color: listDrop.containsDrag || fileListView.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                        radius: 4
                        clip: true

                        // Dropped on the list (not on a file): each file replaces the one with the same name.
                        DropArea {
                            id: listDrop
                            anchors.fill: parent
                            onDropped: (drop) => {
                                if (!drop.hasUrls)
                                    return
                                const urls = []
                                for (let i = 0; i < drop.urls.length; ++i)
                                    urls.push(drop.urls[i].toString())
                                m3mpManager.replaceByName(urls)
                                drop.acceptProposedAction()
                            }
                        }

                        ListView {
                            id: fileListView
                            anchors.fill: parent
                            anchors.bottomMargin: 14
                            orientation: ListView.Horizontal
                            flickableDirection: Flickable.HorizontalFlick
                            spacing: 6
                            clip: true
                            leftMargin: 4
                            rightMargin: 4
                            model: filesPanel.totalColumns
                            focus: true

                            Keys.onPressed: (event) => {
                                if (event.key === Qt.Key_Tab) {
                                    folderTree.forceActiveFocus()
                                    event.accepted = true
                                    return
                                }
                                // All of the navigation rules live in M3mpToolManager::handleFileKey().
                                event.accepted = m3mpManager.handleFileKey(event.key, filesPanel.rowsPerColumn)
                            }

                            delegate: Column {
                                id: columnBlock
                                width: filesPanel.gridColumnWidth
                                spacing: 0

                                readonly property int startIdx: index * filesPanel.rowsPerColumn
                                readonly property int endIdx: Math.min(startIdx + filesPanel.rowsPerColumn,
                                                                       m3mpManager.currentFolderFiles.length)

                                Repeater {
                                    model: columnBlock.endIdx - columnBlock.startIdx

                                    delegate: ItemDelegate {
                                        id: fileDelegate
                                        readonly property int targetFileIndex: columnBlock.startIdx + index
                                        readonly property string currentFileName: m3mpManager.currentFolderFiles[targetFileIndex]
                                        readonly property bool isSelected: m3mpManager.currentFileIndex === targetFileIndex
                                        // Re-checked whenever the replacements change.
                                        readonly property bool replaced: m3mpManager.replacedCount >= 0 && m3mpManager.unsavedChanges !== undefined
                                                                         && m3mpManager.isReplacedAt(targetFileIndex)

                                        width: filesPanel.gridColumnWidth
                                        height: filesPanel.itemRowHeight
                                        leftPadding: 4
                                        rightPadding: 4
                                        highlighted: isSelected

                                        background: Rectangle {
                                            color: fileDelegate.isSelected
                                                   ? (fileListView.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.accentDim)
                                                   : (fileDelegate.hovered || itemDrop.containsDrag ? BtDarkStyle.bgHover : "transparent")
                                            border.width: itemDrop.containsDrag ? 1 : 0
                                            border.color: BtDarkStyle.accentColor
                                            radius: 2
                                        }

                                        // A file dropped on this one replaces it (whatever its name).
                                        DropArea {
                                            id: itemDrop
                                            anchors.fill: parent
                                            onDropped: (drop) => {
                                                if (!drop.hasUrls)
                                                    return
                                                if (drop.urls.length === 1) {
                                                    m3mpManager.replaceFileAt(fileDelegate.targetFileIndex, drop.urls[0].toString())
                                                } else {
                                                    const urls = []
                                                    for (let i = 0; i < drop.urls.length; ++i)
                                                        urls.push(drop.urls[i].toString())
                                                    m3mpManager.replaceByName(urls)
                                                }
                                                drop.acceptProposedAction()
                                            }
                                        }

                                        // Right click: Replace / Revert.
                                        TapHandler {
                                            acceptedButtons: Qt.RightButton
                                            onTapped: {
                                                fileListView.forceActiveFocus()
                                                m3mpManager.selectFile(fileDelegate.targetFileIndex)
                                                replaceMenu.fileIndex = fileDelegate.targetFileIndex
                                                replaceMenu.fileName = fileDelegate.currentFileName
                                                replaceMenu.replaced = fileDelegate.replaced
                                                replaceMenu.popup()
                                            }
                                        }

                                        contentItem: RowLayout {
                                            anchors.fill: parent;
                                            spacing: 6
                                            Image {
                                                source: m3mpViewRoot.iconDir + m3mpViewRoot.fileIcon(fileDelegate.currentFileName)
                                                Layout.preferredWidth: 16
                                                Layout.preferredHeight: 16
                                                Layout.alignment: Qt.AlignVCenter
                                                fillMode: Image.PreserveAspectFit
                                                asynchronous: true
                                                cache: true
                                            }
                                            Label {
                                                text: fileDelegate.currentFileName
                                                color: fileDelegate.isSelected ? "#121214"
                                                       : (fileDelegate.hovered ? BtDarkStyle.accentColor : BtDarkStyle.textMain)
                                                font.family: "Consolas"
                                                elide: Text.ElideNone
                                                font.pixelSize: 11
                                                Layout.fillWidth: true
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                            Rectangle {
                                                visible: fileDelegate.replaced
                                                Layout.alignment: Qt.AlignVCenter
                                                implicitWidth: replacedBadge.implicitWidth + 8
                                                implicitHeight: 14
                                                radius: 3
                                                color: fileDelegate.isSelected ? "#121214" : BtDarkStyle.accentDim
                                                Text {
                                                    id: replacedBadge
                                                    anchors.centerIn: parent
                                                    text: "MOD"
                                                    color: fileDelegate.isSelected ? BtDarkStyle.accentColor : "#121214"
                                                    font.pixelSize: 8
                                                    font.bold: true
                                                }
                                            }
                                        }

                                        onClicked: {
                                            fileListView.forceActiveFocus()
                                            m3mpManager.selectFile(targetFileIndex)
                                        }
                                    }
                                }
                            }

                            ScrollBar.horizontal: ScrollBar { active: true; policy: ScrollBar.AsNeeded }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: m3mpManager.currentFolderFiles.length === 0
                            color: BtDarkStyle.textDim
                            font.italic: true
                            text: m3mpManager.entryCount === 0
                                  ? (m3mpManager.sourceName !== "" ? "This M3MP holds no files." : "Waiting for an M3MP to load...")
                                  : (m3mpManager.visibleCount === 0 ? "No files match the filter."
                                                                    : "This folder has no files.")
                        }
                    }
                }
            }

            // ---- Preview ------------------------------------------------------------------------------
            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 250
                color: BtDarkStyle.bgDarkest

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Text { text: "Preview"; color: BtDarkStyle.accentColor; font.bold: true; font.pointSize: 10 }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: BtDarkStyle.bgDark
                        border.color: BtDarkStyle.borderDark
                        border.width: 1
                        radius: 4
                        clip: true

                        Image {
                            anchors.fill: parent
                            fillMode: Image.Tile
                            source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/grid_dark.bmp"
                        }

                        // Image canvas: the mouse wheel zooms around the cursor, dragging pans once the texture is bigger than the view.
                        Flickable {
                            id: previewFlick
                            visible: m3mpManager.previewKind === "image"
                            anchors.fill: parent
                            anchors.bottomMargin: 28   // Room for the info strip.
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            contentWidth: Math.max(width, previewImage.width + 40)
                            contentHeight: Math.max(height, previewImage.height + 40)
                            interactive: contentWidth > width || contentHeight > height

                            // 1.0 = fitted to the view (small textures are never blown up past their real size at 1.0).
                            property real zoom: 1.0
                            readonly property real minZoom: 0.1
                            readonly property real maxZoom: 32.0
                            readonly property real fitScale: previewImage.sourceSize.width > 0 && previewImage.sourceSize.height > 0
                                ? Math.min(1.0, Math.max(1, width - 40) / previewImage.sourceSize.width,
                                           Math.max(1, height - 40) / previewImage.sourceSize.height)
                                : 1.0
                            readonly property real displayScale: fitScale * zoom   // 1.0 = one texel per screen pixel.

                            // A different texture starts fitted again (flip / alpha keep the zoom).
                            property int resetKey: m3mpManager.currentEntry
                            onResetKeyChanged: resetZoom()

                            function resetZoom() {
                                zoom = 1.0
                                contentX = 0
                                contentY = 0
                            }

                            // Zooms by `factor` and keeps the texel under (px, py) (view coordinates) under the cursor.
                            function zoomAt(factor, px, py) {
                                var newZoom = Math.max(minZoom, Math.min(maxZoom, zoom * factor))
                                if (newZoom === zoom || previewImage.width <= 0 || previewImage.height <= 0)
                                    return
                                var u = (contentX + px - previewImage.x) / previewImage.width
                                var v = (contentY + py - previewImage.y) / previewImage.height
                                zoom = newZoom
                                contentX = Math.max(0, Math.min(contentWidth - width, previewImage.x + u * previewImage.width - px))
                                contentY = Math.max(0, Math.min(contentHeight - height, previewImage.y + v * previewImage.height - py))
                            }

                            Image {
                                id: previewImage
                                x: (previewFlick.contentWidth - width) / 2
                                y: (previewFlick.contentHeight - height) / 2
                                width: sourceSize.width * previewFlick.displayScale
                                height: sourceSize.height * previewFlick.displayScale
                                fillMode: Image.PreserveAspectFit
                                source: m3mpManager.previewKind === "image" ? m3mpManager.previewUrl : ""
                                smooth: previewFlick.displayScale < 2   // Crisp texels when zoomed in.
                                asynchronous: true
                                cache: false

                                BusyIndicator {
                                    anchors.centerIn: parent
                                    running: previewImage.status === Image.Loading
                                    visible: running
                                }
                            }
                        }

                        // Wheel only: dragging still goes to the Flickable underneath.
                        MouseArea {
                            anchors.fill: previewFlick
                            visible: previewFlick.visible
                            acceptedButtons: Qt.NoButton
                            onWheel: (wheel) => {
                                if (wheel.angleDelta.y === 0) {
                                    wheel.accepted = false
                                    return
                                }
                                previewFlick.zoomAt(Math.pow(1.0015, wheel.angleDelta.y), wheel.x, wheel.y)   // ~20% per notch.
                            }
                        }

                        // Zoom level (top right). Click to fit the texture again.
                        Rectangle {
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 8
                            width: zoomLabel.implicitWidth + 16
                            height: 24
                            radius: 4
                            color: zoomBadgeArea.containsMouse ? BtDarkStyle.bgMedium : BtDarkStyle.bgDark
                            border.width: 1
                            border.color: zoomBadgeArea.containsMouse ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                            opacity: 0.9
                            visible: previewImage.status === Image.Ready

                            Text {
                                id: zoomLabel
                                anchors.centerIn: parent
                                text: Math.round(previewFlick.displayScale * 100) + "%" + (previewFlick.zoom !== 1.0 ? "  ⟲" : "")
                                color: zoomBadgeArea.containsMouse ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                                font.family: "Consolas"
                                font.pixelSize: 12
                            }

                            MouseArea {
                                id: zoomBadgeArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: previewFlick.resetZoom()
                            }

                            ToolTip.visible: zoomBadgeArea.containsMouse
                            ToolTip.text: "Scroll to zoom, drag to pan. Click to fit."
                        }

                        // Text files as text, everything else as a hex dump.
                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 1
                            anchors.bottomMargin: 28   // Room for the info strip.
                            visible: m3mpManager.previewKind === "text" || m3mpManager.previewKind === "hex"
                            clip: true
                            background: Rectangle { color: BtDarkStyle.bgDark }

                            TextArea {
                                readOnly: true
                                selectByMouse: true
                                textFormat: TextEdit.PlainText
                                wrapMode: m3mpManager.previewKind === "text" ? TextEdit.Wrap : TextEdit.NoWrap
                                font.family: "Consolas"
                                font.pixelSize: 12
                                color: m3mpManager.previewKind === "hex" ? BtDarkStyle.accentDim : BtDarkStyle.textMain
                                text: m3mpManager.previewText
                                background: null
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: m3mpManager.previewKind === "" && previewImage.status !== Image.Loading
                            horizontalAlignment: Text.AlignHCenter
                            color: m3mpManager.statusOk ? BtDarkStyle.white : Material.color(Material.Red)
                            font.pixelSize: 18
                            font.bold: true
                            wrapMode: Text.WordWrap
                            width: parent.width - 80
                            text: m3mpManager.entryCount === 0
                                  ? (!m3mpManager.statusOk ? m3mpManager.statusText
                                     : m3mpManager.sourceName !== "" ? "This M3MP is empty (0 files)."
                                                                     : "Waiting for an M3MP to load...")
                                  : (m3mpManager.currentFileIndex < 0 ? "Select a file to preview it."
                                                                      : "This file can't be previewed.")
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: previewImage.status === Image.Error
                            color: Material.color(Material.Red)
                            font.pixelSize: 18
                            font.bold: true
                            text: "Failed to load the preview image."
                        }

                        // Info strip of the selected file
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 28
                            color: BtDarkStyle.bgMedium
                            visible: m3mpManager.infoLine !== ""

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideMiddle
                                color: BtDarkStyle.textMain
                                font.family: "Consolas"
                                font.pixelSize: 12
                                text: m3mpManager.infoLine
                            }
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------------------------------------------
        // Status bar (same as MainForm.qml: status text, progress %, progress bar, version)
        // ------------------------------------------------------------------------------------------------
        ToolBar {
            id: statusFooterBar
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

                Text {
                    id: statusOutput
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font.pointSize: 9
                    color: m3mpManager.statusOk ? BtDarkStyle.textDim : Material.color(Material.Red)
                    text: m3mpManager.statusText !== "" ? "Status: " + m3mpManager.statusText
                                                        : "Status: Ready | Blobset Tools M3MP Viewer"
                }

                RowLayout {
                    spacing: 10
                    Layout.alignment: Qt.AlignVCenter

                    Text {
                        id: statusProgress
                        text: m3mpManager.progress > 0 ? m3mpManager.progress + "%" : ""
                        color: BtDarkStyle.textDim
                        font.pointSize: 9
                    }

                    ProgressBar {
                        id: statusBarProgress
                        from: 0
                        to: 100
                        value: m3mpManager.progress
                        implicitWidth: 150
                        implicitHeight: 6
                        Layout.alignment: Qt.AlignVCenter

                        background: Rectangle {
                            implicitWidth: statusBarProgress.implicitWidth
                            implicitHeight: statusBarProgress.implicitHeight
                            color: BtDarkStyle.bgMedium
                            radius: 3
                        }
                        contentItem: Item {
                            implicitWidth: statusBarProgress.implicitWidth
                            implicitHeight: statusBarProgress.implicitHeight

                            Rectangle {
                                width: statusBarProgress.visualPosition * parent.width
                                height: parent.height
                                radius: 3
                                color: BtDarkStyle.accentColor
                            }
                        }
                    }

                    Text {
                        text: m3mpManager.appVersion
                        color: BtDarkStyle.accentColor
                        font.pointSize: 9
                    }
                }
            }
        }
    }

    FolderDialog {
        id: extractDialog
        property bool extractAll: false

        function start(all) {
            extractAll = all
            title = all ? "Extract All Files to Directory" : "Extract Selected File to Directory"
            currentFolder = appOrchestrator.workFolderUrl("m3mp")   // Documents\Blobset Tools\games\<game>\<platform>\m3mp
            open()
        }

        onAccepted: {
            if (extractAll)
                m3mpManager.extractAll(selectedFolder.toString())
            else
                m3mpManager.extractSelected(selectedFolder.toString())
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Replace / Save Changes
    // ------------------------------------------------------------------------------------------------
    function leave() {
        if (m3mpManager.unsavedChanges)
            unsavedChangesDialog.open()
        else
            appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml")
    }

    Connections {
        target: m3mpManager
        // Saved from the "unsaved changes" question on the way out: carry on back to the workspace.
        function onChangesSaved(target) {
            if (unsavedChangesDialog.leaveAfterSave) {
                unsavedChangesDialog.leaveAfterSave = false
                appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml")
            }
        }
        function onProcessComplete(success, message) {
            if (!success)
                unsavedChangesDialog.leaveAfterSave = false
        }
    }

    Menu {
        id: replaceMenu
        property int fileIndex: -1
        property string fileName: ""
        property bool replaced: false
        MenuItem {
            text: "Replace " + replaceMenu.fileName + "..."
            icon.source: m3mpViewRoot.iconDir + "modify_32.png"
            icon.color: "transparent"
            onTriggered: replaceDialog.start(replaceMenu.fileIndex, replaceMenu.fileName)
        }
        MenuItem {
            text: "Undo Replace"
            visible: replaceMenu.replaced
            height: visible ? implicitHeight : 0
            icon.source: m3mpViewRoot.iconDir + "reset_32.png"
            icon.color: "transparent"
            onTriggered: m3mpManager.revertAt(replaceMenu.fileIndex)
        }
    }

    FileDialog {
        id: replaceDialog
        property int fileIndex: -1
        fileMode: FileDialog.OpenFile

        function start(index, name) {
            fileIndex = index
            const dot = name.lastIndexOf(".")
            const ext = dot > 0 ? name.substring(dot + 1).toLowerCase() : ""
            title = "Replace " + name
            nameFilters = ext !== "" ? [ext.toUpperCase() + " file (*." + ext + ")"] : ["All files (*)"]
            open()
        }

        onAccepted: m3mpManager.replaceFileAt(fileIndex, selectedFile.toString())
    }

    MessageDialog {
        id: unsavedChangesDialog
        property bool leaveAfterSave: false
        title: "Unsaved Changes"
        text: "You replaced " + m3mpManager.replacedCount + " file(s) in this M3MP but haven't saved it."
        informativeText: "Save Changes rebuilds the M3MP into the mods folder. Save before going back?"
        buttons: MessageDialog.Save | MessageDialog.Discard | MessageDialog.Cancel
        onButtonClicked: function (button, role) {
            if (button === MessageDialog.Save) {
                leaveAfterSave = true
                m3mpManager.saveChanges()
            } else if (button === MessageDialog.Discard) {
                m3mpManager.discardChanges()
                appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml")
            }
        }
    }

    Component.onCompleted: {
        if (targetFilePath !== "")
            m3mpManager.loadM3mp(targetFilePath, isBigEndian)
    }
}
