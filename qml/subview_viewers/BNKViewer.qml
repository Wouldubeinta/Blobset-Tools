import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import com.wouldymods.blobset 1.0

Item {
    id: bnkViewRoot
    anchors.fill: parent

    // Optional: open a standalone .bnk from disk instead of one from the loaded blobset.
    property string targetFilePath: ""

    // Path hash of the bank opened from the blobset (set by AppOrchestrator::openBnkViewerByHash).
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

        background: Rectangle {
            color: headerButton.hovered && headerButton.enabled ? BtDarkStyle.bgMedium : "transparent"
            border.color: headerButton.hovered && headerButton.enabled ? BtDarkStyle.accentColor : "transparent"
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
                color: headerButton.hovered && headerButton.enabled ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                font.bold: true
                font.pixelSize: 14
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }

    // Panel title (same as "Folders" / "Files" / "Console Log" in MainForm.qml).
    component PanelTitle: Text {
        color: BtDarkStyle.accentColor
        font.bold: true
        font.pointSize: 10
    }

    // One "label: value" line of the metadata panel.
    component InfoRow: RowLayout {
        id: infoRow
        property string label: ""
        property string value: ""

        Layout.fillWidth: true
        spacing: 12

        Text {
            text: infoRow.label
            color: BtDarkStyle.textDim
            font.pixelSize: 13
            Layout.preferredWidth: 150
            Layout.alignment: Qt.AlignTop
            elide: Text.ElideRight
        }

        TextEdit {
            text: infoRow.value
            color: BtDarkStyle.textMain
            font.family: "Consolas"
            font.pixelSize: 13
            readOnly: true
            selectByMouse: true
            selectionColor: BtDarkStyle.accentDim
            wrapMode: TextEdit.Wrap
            Layout.fillWidth: true
        }
    }

    function playCurrent() {
        if (bnkManager.currentEntry < 0)
            return
        audioPlayer.playWemData(bnkManager.wemBytes(bnkManager.currentEntry), bnkManager.currentName)
    }

    function togglePlayback() {
        if (audioPlayer.playing)
            audioPlayer.stop()
        else
            playCurrent()
    }

    Connections {
        target: appOrchestrator

        // Raised by the orchestrator right after it switched to this view.
        function onOpenBnkViewerByHash(hexHash) {
            bnkViewRoot.activeHashString = hexHash
            audioPlayer.stop()
            bnkManager.loadBnkFromBlobset(hexHash)
        }
    }

    Connections {
        target: audioPlayer
        function onMessage(text) { bnkManager.reportMessage(text) }
    }

    BnkToolManager {
        id: bnkManager

        // A bank was loaded or the filter changed: select the first audio file.
        onFilterApplied: {
            fileList.currentIndex = bnkManager.visibleCount > 0 ? 0 : -1
            bnkManager.selectRow(fileList.currentIndex)
        }
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
                    onClicked: {
                        audioPlayer.stop()
                        appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml")
                    }
                }

                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }

                HeaderButton {
                    tip: "Play or stop the selected WEM sound"
                    text: audioPlayer.playing ? "Stop" : "Play"
                    iconSource: bnkViewRoot.iconDir + "wem_32.png"
                    enabled: audioPlayer.playing || bnkManager.currentEntry >= 0
                    onClicked: bnkViewRoot.togglePlayback()
                }

                HeaderButton {
                    tip: "Extract the selected WEM sound to a folder"
                    text: "Extract Selected"
                    iconSource: bnkViewRoot.iconDir + "extract_32.png"
                    enabled: !bnkManager.busy && bnkManager.currentEntry >= 0
                    onClicked: {
                        extractOneDialog.currentFolder = appOrchestrator.workFolderUrl("audio")
                        extractOneDialog.open()
                    }
                }

                HeaderButton {
                    tip: "Extract every WEM sound in this sound bank to a folder"
                    text: "Extract All WEM Files"
                    iconSource: bnkViewRoot.iconDir + "extract_32.png"
                    enabled: !bnkManager.busy && bnkManager.entryCount > 0
                    onClicked: {
                        extractAllDialog.currentFolder = appOrchestrator.workFolderUrl("audio")
                        extractAllDialog.open()
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: bnkManager.sourceName !== "" ? "BNK " + bnkManager.sourceName : ""
                    color: BtDarkStyle.textDim
                    font.family: "Consolas"
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 220
                }

                // Audio filter (top right, where MainForm.qml has its asset search).
                Rectangle {
                    Layout.preferredWidth: 500
                    Layout.minimumWidth: 180
                    Layout.preferredHeight: 34
                    color: BtDarkStyle.bgMedium
                    border.width: 1
                    border.color: filterInput.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                    radius: 4
                    opacity: bnkManager.entryCount > 0 ? 1.0 : 0.5

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
                            enabled: bnkManager.entryCount > 0

                            onTextChanged: bnkManager.filterText = text
                            Keys.onEscapePressed: text = ""

                            Text {
                                text: "Filter audio by Wwise ID..."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 13
                                font.italic: true
                                visible: filterInput.text === "" && !filterInput.activeFocus
                            }
                        }

                        Text {
                            visible: bnkManager.filterText !== ""
                            text: bnkManager.visibleCount + " / " + bnkManager.entryCount
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
        // Workspace: audio files | metadata
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

            // ---- Audio files --------------------------------------------------------------------------
            Rectangle {
                SplitView.preferredWidth: 380
                SplitView.minimumWidth: 250
                color: BtDarkStyle.bgDarkest
                onActiveFocusChanged: if (activeFocus) fileList.forceActiveFocus()

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        PanelTitle { text: "Audio Files" }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: bnkManager.entryCount > 0
                            text: bnkManager.entryCount + " WEM"
                            color: BtDarkStyle.textDim
                            font.family: "Consolas"
                            font.pixelSize: 11
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: BtDarkStyle.bgDark
                        border.width: 1
                        border.color: fileList.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                        radius: 4
                        clip: true

                        ListView {
                            id: fileList
                            anchors.fill: parent
                            anchors.margins: 4
                            clip: true
                            focus: true
                            model: bnkManager.model
                            currentIndex: -1
                            keyNavigationEnabled: true
                            highlightMoveDuration: 0
                            boundsBehavior: Flickable.StopAtBounds

                            onCurrentIndexChanged: {
                                audioPlayer.stop()
                                bnkManager.selectRow(currentIndex)
                            }

                            Keys.onPressed: (event) => {
                                if (count <= 0)
                                    return

                                const pageStep = Math.max(1, Math.floor(height / 26) - 1)
                                switch (event.key) {
                                case Qt.Key_Return:
                                case Qt.Key_Enter:
                                    bnkViewRoot.playCurrent()
                                    break
                                case Qt.Key_Space:
                                    bnkViewRoot.togglePlayback()
                                    break
                                case Qt.Key_Home:
                                    currentIndex = 0
                                    break
                                case Qt.Key_End:
                                    currentIndex = count - 1
                                    break
                                case Qt.Key_PageDown:
                                    currentIndex = Math.min(count - 1, currentIndex + pageStep)
                                    break
                                case Qt.Key_PageUp:
                                    currentIndex = Math.max(0, currentIndex - pageStep)
                                    break
                                default:
                                    return // Up / Down are handled by the ListView itself.
                                }
                                event.accepted = true
                            }

                            delegate: ItemDelegate {
                                id: fileDelegate
                                required property int index
                                required property string name
                                required property string sizeText

                                readonly property bool isSelected: ListView.isCurrentItem

                                width: ListView.view ? ListView.view.width : 0
                                height: 26
                                leftPadding: 8
                                rightPadding: 8

                                background: Rectangle {
                                    color: fileDelegate.isSelected
                                           ? (fileList.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.accentDim)
                                           : (fileDelegate.hovered ? BtDarkStyle.bgHover : "transparent")
                                    radius: 2
                                }

                                contentItem: RowLayout {
                                    spacing: 6

                                    Image {
                                        source: bnkViewRoot.iconDir + "wem_32.png"
                                        Layout.preferredWidth: 16
                                        Layout.preferredHeight: 16
                                        Layout.alignment: Qt.AlignVCenter
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        cache: true
                                    }
                                    Label {
                                        text: fileDelegate.name
                                        color: fileDelegate.isSelected ? "#121214"
                                               : (fileDelegate.hovered ? BtDarkStyle.accentColor : BtDarkStyle.textMain)
                                        font.family: "Consolas"
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    Label {
                                        text: fileDelegate.sizeText
                                        color: fileDelegate.isSelected ? "#121214" : BtDarkStyle.textDim
                                        font.family: "Consolas"
                                        font.pixelSize: 11
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }

                                onClicked: {
                                    fileList.currentIndex = index
                                    fileList.forceActiveFocus()
                                }
                                onDoubleClicked: {
                                    fileList.currentIndex = index
                                    bnkViewRoot.playCurrent()
                                }
                            }

                            ScrollBar.vertical: ScrollBar { active: true; policy: ScrollBar.AsNeeded }
                        }

                        Label {
                            anchors.centerIn: parent
                            width: parent.width - 40
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            visible: fileList.count === 0
                            color: bnkManager.statusOk ? BtDarkStyle.textDim : Material.color(Material.Red)
                            font.italic: true
                            text: !bnkManager.statusOk && bnkManager.entryCount === 0 ? bnkManager.statusText
                                  : bnkManager.entryCount === 0
                                    ? (bnkManager.sourceName === "" ? "Waiting for a sound bank to load..."
                                                                    : "This bank has no embedded audio.")
                                    : "No audio files match the filter."
                        }
                    }
                }
            }

            // ---- Metadata -----------------------------------------------------------------------------
            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 300
                color: BtDarkStyle.bgDarkest

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    PanelTitle { text: "Metadata" }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: BtDarkStyle.bgDark
                        border.color: BtDarkStyle.borderDark
                        border.width: 1
                        radius: 4
                        clip: true

                        ScrollView {
                            id: metaScroll
                            anchors.fill: parent
                            anchors.margins: 14
                            contentWidth: availableWidth
                            clip: true

                            ColumnLayout {
                                width: metaScroll.availableWidth
                                spacing: 6

                                PanelTitle { text: "Sound Bank" }

                                Repeater {
                                    model: bnkManager.bankInfo
                                    delegate: InfoRow {
                                        required property var modelData
                                        label: modelData.label
                                        value: modelData.value
                                    }
                                }

                                Text {
                                    visible: bnkManager.bankInfo.length === 0
                                    text: "No sound bank loaded."
                                    color: BtDarkStyle.textDim
                                    font.italic: true
                                    font.pixelSize: 13
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 10
                                    Layout.bottomMargin: 6
                                    height: 1
                                    color: BtDarkStyle.borderDark
                                }

                                PanelTitle { text: "Selected Audio" }

                                Repeater {
                                    model: bnkManager.entryInfo
                                    delegate: InfoRow {
                                        required property var modelData
                                        label: modelData.label
                                        value: modelData.value
                                    }
                                }

                                Text {
                                    visible: bnkManager.entryInfo.length === 0
                                    text: bnkManager.entryCount > 0 ? "Select an audio file to see its details."
                                                                    : "Nothing to show."
                                    color: BtDarkStyle.textDim
                                    font.italic: true
                                    font.pixelSize: 13
                                }
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
                    color: bnkManager.statusOk ? BtDarkStyle.textDim : Material.color(Material.Red)
                    text: bnkManager.statusText !== "" ? "Status: " + bnkManager.statusText
                                                       : "Status: Ready | Blobset Tools Sound Bank Viewer"
                }

                RowLayout {
                    spacing: 10
                    Layout.alignment: Qt.AlignVCenter

                    Text {
                        id: statusProgress
                        text: bnkManager.progress > 0 ? bnkManager.progress + "%" : ""
                        color: BtDarkStyle.textDim
                        font.pointSize: 9
                    }

                    ProgressBar {
                        id: statusBarProgress
                        from: 0
                        to: 100
                        value: bnkManager.progress
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
                        text: bnkManager.appVersion
                        color: BtDarkStyle.accentColor
                        font.pointSize: 9
                    }
                }
            }
        }
    }

    FolderDialog {
        id: extractAllDialog
        title: "Extract Sound Bank Audio to Directory"
        onAccepted: bnkManager.extractAll(selectedFolder.toString())
    }

    FolderDialog {
        id: extractOneDialog
        title: "Extract Selected Audio to Directory"
        onAccepted: bnkManager.extractSelected(selectedFolder.toString())
    }

    Component.onCompleted: {
        if (targetFilePath !== "")
            bnkManager.loadBnk(targetFilePath)
        fileList.forceActiveFocus()
    }

    Component.onDestruction: audioPlayer.stop()
}
