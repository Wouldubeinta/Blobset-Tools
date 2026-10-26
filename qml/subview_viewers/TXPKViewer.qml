import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import com.wouldymods.blobset 1.0

Item {
    id: txpkViewRoot
    anchors.fill: parent

    // Optional: open a standalone .txpk from disk instead of one from the loaded blobset.
    property string targetFilePath: ""
    property bool isBigEndian: false

    // Path hash of the TXPK opened from the blobset (set by AppOrchestrator::openTxpkViewerByHash).
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

    // Shows the root folder (and, while filtering, every folder) and selects the root folder.
    function selectRootFolder() {
        if (txpkManager.visibleCount <= 0)
            return

        if (txpkManager.filterText !== "")
            folderTree.expandRecursively()
        else
            folderTree.expand(0)

        folderSelection.setCurrentIndex(txpkManager.rootFolderIndex(), ItemSelectionModel.ClearAndSelect)
    }

    Connections {
        target: appOrchestrator

        // Raised by the orchestrator right after it switched to this view.
        function onOpenTxpkViewerByHash(hexHash) {
            txpkViewRoot.activeHashString = hexHash
            txpkManager.loadTxpkFromBlobset(hexHash)
        }
    }

    TxpkToolManager {
        id: txpkManager

        // A TXPK was loaded or the filter changed: the folder tree has been rebuilt.
        onTreeRebuilt: Qt.callLater(txpkViewRoot.selectRootFolder)
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
                    onClicked: appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml")
                }

                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }

                // Vertically flips the preview (and PNG extraction).
                HeaderButton {
                    tip: "Flip the image vertically (also used when extracting as PNG)"
                    text: "Flip Image"
                    iconSource: txpkViewRoot.iconDir + "flip_image_32.png"
                    enabled: txpkManager.currentEntry >= 0
                    onClicked: txpkManager.flipImage()
                }

                // Alpha channel visibility: on = as stored in the texture, off = fully opaque.
                HeaderButton {
                    tip: "Show or hide the alpha channel (transparency)"
                    text: txpkManager.alphaEnabled ? "Alpha: On" : "Alpha: Off"
                    iconSource: txpkViewRoot.iconDir + "alpha_32.png"
                    enabled: txpkManager.currentEntry >= 0
                    checkable: true
                    checked: txpkManager.alphaEnabled
                    onToggled: txpkManager.toggleAlpha(checked)
                }

                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }

                HeaderButton {
                    tip: "Extract the selected texture as DDS or PNG"
                    id: extractSelectedButton
                    text: "Extract Selected ▾"
                    iconSource: txpkViewRoot.iconDir + "extract_32.png"
                    enabled: !txpkManager.busy && txpkManager.currentEntry >= 0
                    onClicked: extractMenu.openFor(extractSelectedButton, false)
                }

                HeaderButton {
                    tip: "Extract every texture as DDS (plus TXPK_List.xml for the TXPK Creator) or as PNG"
                    id: extractAllButton
                    text: "Extract All ▾"
                    iconSource: txpkViewRoot.iconDir + "extract_32.png"
                    enabled: !txpkManager.busy && txpkManager.entryCount > 0
                    onClicked: extractMenu.openFor(extractAllButton, true)
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: txpkManager.sourceName !== "" ? "TXPK " + txpkManager.sourceName : ""
                    color: BtDarkStyle.textDim
                    font.family: "Consolas"
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 220
                }

                // Texture filter (top right, where MainForm.qml has its asset search).
                Rectangle {
                    Layout.preferredWidth: 500
                    Layout.minimumWidth: 180
                    Layout.preferredHeight: 34
                    color: BtDarkStyle.bgMedium
                    border.width: 1
                    border.color: filterInput.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                    radius: 4
                    opacity: txpkManager.entryCount > 0 ? 1.0 : 0.5

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
                            enabled: txpkManager.entryCount > 0

                            onTextChanged: txpkManager.filterText = text
                            Keys.onEscapePressed: text = ""

                            Text {
                                text: "Filter textures..."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 13
                                font.italic: true
                                visible: filterInput.text === "" && !filterInput.activeFocus
                            }
                        }

                        Text {
                            visible: txpkManager.filterText !== ""
                            text: txpkManager.visibleCount + " / " + txpkManager.entryCount
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
                            model: txpkManager.treeModel
                            clip: true
                            focus: true
                            boundsBehavior: Flickable.StopAtBounds
                            columnWidthProvider: function(column) { return Math.max(folderTree.width, 100) }

                            selectionModel: ItemSelectionModel { id: folderSelection }

                            // The selected folder decides which textures the file list shows.
                            Connections {
                                target: folderSelection
                                function onCurrentChanged(current, previous) {
                                    txpkManager.selectFolder(current)
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
                                        source: txpkViewRoot.iconDir + "folder_32.png"
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
                readonly property int gridColumnWidth: txpkManager.maxFileItemWidth
                readonly property int rowsPerColumn: Math.max(1, Math.floor((fileListView.height - 16) / itemRowHeight))
                readonly property int totalColumns: Math.ceil(txpkManager.currentFolderFiles.length / rowsPerColumn)

                Connections {
                    target: txpkManager

                    // Keep the selected texture in view (also covers keyboard navigation).
                    function onCurrentFileChanged() {
                        if (txpkManager.currentFileIndex >= 0)
                            fileListView.positionViewAtIndex(
                                Math.floor(txpkManager.currentFileIndex / filesPanel.rowsPerColumn), ListView.Contain)
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
                        border.color: fileListView.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                        radius: 4
                        clip: true

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
                                // All of the navigation rules live in TxpkToolManager::handleFileKey().
                                event.accepted = txpkManager.handleFileKey(event.key, filesPanel.rowsPerColumn)
                            }

                            delegate: Column {
                                id: columnBlock
                                width: filesPanel.gridColumnWidth
                                spacing: 0

                                readonly property int startIdx: index * filesPanel.rowsPerColumn
                                readonly property int endIdx: Math.min(startIdx + filesPanel.rowsPerColumn,
                                                                       txpkManager.currentFolderFiles.length)

                                Repeater {
                                    model: columnBlock.endIdx - columnBlock.startIdx

                                    delegate: ItemDelegate {
                                        id: fileDelegate
                                        readonly property int targetFileIndex: columnBlock.startIdx + index
                                        readonly property string currentFileName: txpkManager.currentFolderFiles[targetFileIndex]
                                        readonly property bool isSelected: txpkManager.currentFileIndex === targetFileIndex

                                        width: filesPanel.gridColumnWidth
                                        height: filesPanel.itemRowHeight
                                        leftPadding: 4
                                        rightPadding: 4
                                        highlighted: isSelected

                                        background: Rectangle {
                                            color: fileDelegate.isSelected
                                                   ? (fileListView.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.accentDim)
                                                   : (fileDelegate.hovered ? BtDarkStyle.bgHover : "transparent")
                                            radius: 2
                                        }

                                        contentItem: RowLayout {
                                            anchors.fill: parent;
                                            spacing: 6
                                            Image {
                                                source: txpkViewRoot.iconDir + "dds_32.png"
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
                                        }

                                        onClicked: {
                                            fileListView.forceActiveFocus()
                                            txpkManager.selectFile(targetFileIndex)
                                        }
                                    }
                                }
                            }

                            ScrollBar.horizontal: ScrollBar { active: true; policy: ScrollBar.AsNeeded }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: txpkManager.currentFolderFiles.length === 0
                            color: BtDarkStyle.textDim
                            font.italic: true
                            text: txpkManager.entryCount === 0
                                  ? (txpkManager.sourceName !== "" ? "This TXPK holds no textures." : "Waiting for a TXPK to load...")
                                  : (txpkManager.visibleCount === 0 ? "No textures match the filter."
                                                                    : "This folder has no textures.")
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

                    Text { text: "Image Preview"; color: BtDarkStyle.accentColor; font.bold: true; font.pointSize: 10 }

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

                        // Texture canvas: the mouse wheel zooms around the cursor, dragging pans once the texture is bigger than the view.
                        Flickable {
                            id: previewFlick
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
                            property int resetKey: txpkManager.currentEntry
                            onResetKeyChanged: resetZoom()

                            // Free offset of the texture (middle mouse drag), on top of the scrolling.
                            property real panX: 0
                            property real panY: 0

                            // Moves the texture by (dx, dy) pixels; at least 40 px of it stays in view.
                            function panBy(dx, dy) {
                                var limitX = (width + previewImage.width) / 2 - 40
                                var limitY = (height + previewImage.height) / 2 - 40
                                panX = Math.max(-limitX, Math.min(limitX, panX + dx))
                                panY = Math.max(-limitY, Math.min(limitY, panY + dy))
                            }

                            function resetZoom() {
                                zoom = 1.0
                                panX = 0
                                panY = 0
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
                                x: (previewFlick.contentWidth - width) / 2 + previewFlick.panX
                                y: (previewFlick.contentHeight - height) / 2 + previewFlick.panY
                                width: sourceSize.width * previewFlick.displayScale
                                height: sourceSize.height * previewFlick.displayScale
                                fillMode: Image.PreserveAspectFit
                                source: txpkManager.previewUrl
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

                        // Wheel zoom and middle button panning; left dragging still goes to the Flickable underneath.
                        MouseArea {
                            anchors.fill: previewFlick
                            acceptedButtons: Qt.MiddleButton
                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                            property real lastX: 0
                            property real lastY: 0
                            onPressed: (mouse) => { lastX = mouse.x; lastY = mouse.y }
                            onPositionChanged: (mouse) => {
                                previewFlick.panBy(mouse.x - lastX, mouse.y - lastY)
                                lastX = mouse.x
                                lastY = mouse.y
                            }
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
                            ToolTip.text: "Scroll to zoom, drag or hold the middle mouse button to move. Click to fit."
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: txpkManager.previewUrl === "" && previewImage.status !== Image.Loading
                            horizontalAlignment: Text.AlignHCenter
                            color: txpkManager.statusOk ? BtDarkStyle.white : Material.color(Material.Red)
                            font.pixelSize: 18
                            font.bold: true
                            wrapMode: Text.WordWrap
                            width: parent.width - 80
                            text: txpkManager.entryCount === 0
                                  ? (!txpkManager.statusOk ? txpkManager.statusText
                                     : txpkManager.sourceName !== "" ? "This TXPK is empty (0 textures)."
                                                                     : "Waiting for a TXPK to load...")
                                  : (txpkManager.currentFileIndex < 0 ? "Select a texture to preview it."
                                                                      : "This texture can't be previewed.")
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: previewImage.status === Image.Error
                            color: Material.color(Material.Red)
                            font.pixelSize: 18
                            font.bold: true
                            text: "Failed to load the preview image."
                        }

                        // Info strip of the selected texture
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 28
                            color: BtDarkStyle.bgMedium
                            visible: txpkManager.infoLine !== ""

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideMiddle
                                color: BtDarkStyle.textMain
                                font.family: "Consolas"
                                font.pixelSize: 12
                                text: txpkManager.infoLine
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
                    color: txpkManager.statusOk ? BtDarkStyle.textDim : Material.color(Material.Red)
                    text: txpkManager.statusText !== "" ? "Status: " + txpkManager.statusText
                                                        : "Status: Ready | Blobset Tools TXPK Viewer"
                }

                RowLayout {
                    spacing: 10
                    Layout.alignment: Qt.AlignVCenter

                    Text {
                        id: statusProgress
                        text: txpkManager.progress > 0 ? txpkManager.progress + "%" : ""
                        color: BtDarkStyle.textDim
                        font.pointSize: 9
                    }

                    ProgressBar {
                        id: statusBarProgress
                        from: 0
                        to: 100
                        value: txpkManager.progress
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
                        text: txpkManager.appVersion
                        color: BtDarkStyle.accentColor
                        font.pointSize: 9
                    }
                }
            }
        }
    }

    Menu {
        id: extractMenu
        property bool extractAll: false

        function openFor(button, all) {
            extractAll = all
            popup(button, 0, button.height)
        }

        MenuItem {
            // Extract All as DDS also writes TXPK_List.xml, which the TXPK Creator rebuilds the TXPK from.
            text: extractMenu.extractAll ? "As DDS (+ creator list)..." : "As DDS..."
            icon.source: txpkViewRoot.iconDir + "dds_32.png"
            icon.color: "transparent"   // Full color icon (Material tints icons by default).
            onTriggered: extractDialog.start(extractMenu.extractAll, "dds")
        }
        MenuItem {
            text: "As PNG..."
            icon.source: txpkViewRoot.iconDir + "png_32.png"
            icon.color: "transparent"   // Full color icon (Material tints icons by default).
            onTriggered: extractDialog.start(extractMenu.extractAll, "png")
        }
    }

    FolderDialog {
        id: extractDialog
        property bool extractAll: false
        property string format: "dds"

        function start(all, fmt) {
            extractAll = all
            format = fmt
            title = (all ? "Extract All Textures" : "Extract Selected Texture")
                    + " as " + fmt.toUpperCase() + " to Directory"
            open()
        }

        onAccepted: {
            if (extractAll)
                txpkManager.extractAll(selectedFolder.toString(), format)
            else
                txpkManager.extractSelected(selectedFolder.toString(), format)
        }
    }

    Component.onCompleted: {
        if (targetFilePath !== "")
            txpkManager.loadTxpk(targetFilePath, isBigEndian)
    }
}
