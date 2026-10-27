import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import com.wouldymods.blobset 1.0

// BSB Creator (Blobset V1 games): rebuilds a BSB from the BSB_List.xml the BSB Viewer writes on Extract All, the
// (edited) Flash movie (.swf) and the (edited) .dds textures next to it. Writes <name>.bsb (main + vram, the way
// Extract writes BSBs, which Modify / Create split again by the BSB header) plus <name>.xml.
Item {
    id: creatorRoot
    anchors.fill: parent

    readonly property string iconDir: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/"
    readonly property string kind: "BSB"
    readonly property string listName: "BSB_List.xml"
    readonly property color okColor: "#5cb85c"
    readonly property color errorColor: Material.color(Material.Red)

    property string xmlPath: ""
    property string sourceDir: ""        // Empty = the list's folder.
    property string outputPath: ""
    property bool outputEdited: false     // Chosen by hand: no longer follows the list.
    property var info: ({})               // BsbToolManager.inspectBsbList()

    readonly property bool listOk: info.ok === true
    readonly property bool canCreate: listOk && info.missing === 0 && outputPath !== "" && !creator.busy

    Rectangle {
        anchors.fill: parent
        color: BtDarkStyle.bgDarkest
        z: -1
    }

    // "file:///C:/x" -> "C:/x", "file:///home/x" -> "/home/x"
    function toLocal(url) {
        var s = url.toString()
        if (/^file:\/\/\/[A-Za-z]:/.test(s))
            s = s.substring(8)
        else if (s.startsWith("file://"))
            s = s.substring(7)
        return decodeURIComponent(s)
    }

    function folderOf(path) {
        var i = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        return i > 0 ? path.substring(0, i) : path
    }

    function toUrl(path) {
        if (path === "")
            return ""
        return (/^[A-Za-z]:/.test(path) ? "file:///" : "file://") + path
    }

    function humanSize(bytes) {
        if (bytes >= 1048576) return (bytes / 1048576).toFixed(2) + " MB"
        if (bytes >= 1024) return (bytes / 1024).toFixed(1) + " KB"
        return bytes + " B"
    }

    function refresh() {
        info = xmlPath === "" ? ({}) : creator.inspectBsbList(xmlPath, sourceDir)
        if (listOk && !outputEdited)
            outputPath = info.suggestedOutput
    }

    function log(message, ok) {
        logModel.append({ message: message, ok: ok, time: Qt.formatTime(new Date(), "hh:mm:ss") })
        logView.positionViewAtEnd()
    }

    BsbToolManager {
        id: creator
        onCreatorLog: (message, ok) => creatorRoot.log(message, ok)
        onProcessComplete: (success, message) => {
            creatorRoot.log(message, success)
            creatorRoot.refresh()
        }
    }

    ListModel { id: logModel }

    // ------------------------------------------------------------------------------------------------
    // Components
    // ------------------------------------------------------------------------------------------------

    // Same as the viewers' header buttons.
    component HeaderButton: AbstractButton {
        id: headerButton
        property string tip: ""   // Tooltip text.
        BtToolTip { text: headerButton.tip }
        property url iconSource: ""
        property bool accent: false

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

    // A titled panel.
    component Card: Rectangle {
        id: card
        property string title: ""
        property string step: ""
        default property alias content: cardBody.data

        color: BtDarkStyle.bgDark
        border.color: BtDarkStyle.borderDark
        border.width: 1
        radius: 6
        implicitHeight: cardColumn.implicitHeight + 28

        ColumnLayout {
            id: cardColumn
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            RowLayout {
                spacing: 8
                Layout.fillWidth: true

                Rectangle {
                    visible: card.step !== ""
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    radius: 11
                    color: BtDarkStyle.accentColor
                    Text {
                        anchors.centerIn: parent
                        text: card.step
                        color: BtDarkStyle.bgDarkest
                        font.bold: true
                        font.pixelSize: 12
                    }
                }
                Text {
                    text: card.title
                    color: BtDarkStyle.textMain
                    font.bold: true
                    font.pixelSize: 14
                    Layout.fillWidth: true
                }
            }

            ColumnLayout {
                id: cardBody
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
            }
        }
    }

    // Editable path box + browse button + hint.
    component PathRow: ColumnLayout {
        id: pathRow
        property alias text: pathInput.text
        property string placeholder: ""
        property string hint: ""
        property string buttonText: "Browse..."
        property string buttonTip: ""
        property bool valid: true
        property bool busy: false
        signal browse()
        signal edited()

        Layout.fillWidth: true
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                color: BtDarkStyle.bgMedium
                radius: 4
                border.width: 1
                border.color: pathInput.activeFocus ? BtDarkStyle.accentColor
                                                    : (pathRow.valid ? BtDarkStyle.borderDark : Material.color(Material.Red))

                TextInput {
                    id: pathInput
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    color: BtDarkStyle.textMain
                    font.family: "Consolas"
                    font.pixelSize: 12
                    selectByMouse: true
                    clip: true
                    onEditingFinished: pathRow.edited()

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        visible: pathInput.text === "" && !pathInput.activeFocus
                        text: pathRow.placeholder
                        color: BtDarkStyle.textDim
                        font: pathInput.font
                        elide: Text.ElideRight
                    }
                }
            }

            HeaderButton {
                tip: pathRow.buttonTip
                text: pathRow.buttonText
                iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/open_32.png"
                implicitHeight: 34
                enabled: !pathRow.busy
                onClicked: pathRow.browse()
            }
        }

        Text {
            visible: pathRow.hint !== ""
            text: pathRow.hint
            color: BtDarkStyle.textDim
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    component InfoRow: RowLayout {
        property string label: ""
        property string value: ""
        property color valueColor: BtDarkStyle.textMain
        Layout.fillWidth: true
        spacing: 10
        Text {
            text: parent.label
            color: BtDarkStyle.textDim
            font.pixelSize: 12
            Layout.preferredWidth: 110
        }
        Text {
            text: parent.value
            color: parent.valueColor
            font.pixelSize: 12
            font.family: "Consolas"
            elide: Text.ElideMiddle
            Layout.fillWidth: true
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
                    tip: "Return to the main workspace"
                    text: "◀ Back"
                    enabled: !creator.busy
                    onClicked: appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml")
                }

                Rectangle { width: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }

                Image {
                    source: creatorRoot.iconDir + "bsb_32.png"
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    fillMode: Image.PreserveAspectFit
                }

                ColumnLayout {
                    spacing: 0
                    Text {
                        text: "BSB Creator"
                        color: BtDarkStyle.accentColor
                        font.bold: true
                        font.pixelSize: 16
                    }
                    Text {
                        text: "Rebuild a Flash UI screen (BSB) from its movie and textures"
                        color: BtDarkStyle.textDim
                        font.pixelSize: 11
                    }
                }

                Item { Layout.fillWidth: true }

                HeaderButton {
                    tip: "Clear the log"
                    text: "Clear Log"
                    enabled: logModel.count > 0
                    onClicked: logModel.clear()
                }

                HeaderButton {
                    tip: "Build the BSB (and its .xml) from the list, the movie and the textures"
                    text: creator.busy ? "Creating..." : "Create BSB"
                    iconSource: creatorRoot.iconDir + "create_32.png"
                    accent: creatorRoot.canCreate
                    enabled: creatorRoot.canCreate
                    onClicked: {
                        creatorRoot.log("Creating " + creatorRoot.outputPath + " ...", true)
                        creator.createBsb(creatorRoot.xmlPath, creatorRoot.sourceDir, creatorRoot.outputPath)
                    }
                }
            }
        }

        // Body
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            spacing: 16

            // Left: inputs + summary
            ScrollView {
                id: leftScroll
                Layout.preferredWidth: Math.min(560, creatorRoot.width * 0.45)
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    width: leftScroll.availableWidth
                    spacing: 12

                    Card {
                        Layout.fillWidth: true
                        step: "1"
                        title: "Creator List"

                        PathRow {
                            busy: creator.busy
                            text: creatorRoot.xmlPath
                            placeholder: "Choose " + creatorRoot.listName + "..."
                            valid: creatorRoot.xmlPath === "" || creatorRoot.listOk
                            hint: creatorRoot.listName + " is written by the BSB Viewer when you Extract All. "
                                  + "It names the movie and keeps the texture order, the paths the movie uses and the blobset entry it came from."
                            buttonTip: "Choose the BSB_List.xml written by the BSB Viewer's Extract All"
                            onBrowse: listDialog.open()
                            onEdited: {
                                if (text !== creatorRoot.xmlPath) {
                                    creatorRoot.xmlPath = text
                                    creatorRoot.refresh()
                                }
                            }
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        step: "2"
                        title: "Source Folder"

                        PathRow {
                            busy: creator.busy
                            text: creatorRoot.sourceDir !== "" ? creatorRoot.sourceDir
                                                                : (creatorRoot.listOk ? creatorRoot.info.sourceDir : "")
                            placeholder: "The list's folder"
                            hint: "The folder with the (edited) .swf movie and .dds textures. Keep the file names and folders Extract All created."
                            buttonTip: "Choose the folder with the extracted (edited) files"
                            onBrowse: folderDialog.open()
                            onEdited: {
                                creatorRoot.sourceDir = text
                                creatorRoot.refresh()
                            }
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        step: "3"
                        title: "Output"

                        PathRow {
                            busy: creator.busy
                            text: creatorRoot.outputPath
                            placeholder: "Where to save the new .bsb"
                            buttonText: "Save As..."
                            hint: "A matching <name>.xml is written next to it. Put both in your mods folder to use them with Modify / Create."
                            buttonTip: "Choose where to save the new .bsb"
                            onBrowse: saveDialog.open()
                            onEdited: {
                                creatorRoot.outputPath = text
                                creatorRoot.outputEdited = text !== ""
                            }
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        title: "Summary"

                        Text {
                            visible: !creatorRoot.listOk
                            text: creatorRoot.xmlPath === "" ? "Choose a creator list to begin."
                                                             : (creatorRoot.info.error || "")
                            color: creatorRoot.xmlPath === "" ? BtDarkStyle.textDim : creatorRoot.errorColor
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        ColumnLayout {
                            visible: creatorRoot.listOk
                            Layout.fillWidth: true
                            spacing: 6

                            InfoRow {
                                label: "Movie"
                                value: creatorRoot.listOk ? (creatorRoot.info.movie || "(not named)")
                                                            + (creatorRoot.info.movieFound ? "  ·  " + creatorRoot.humanSize(creatorRoot.info.movieSize)
                                                                                           : "  (missing)")
                                                          : ""
                                valueColor: creatorRoot.info.movieFound ? creatorRoot.okColor : creatorRoot.errorColor
                            }
                            InfoRow { label: "Textures"; value: creatorRoot.info.count || 0 }
                            InfoRow {
                                readonly property int texturesMissing: creatorRoot.listOk
                                    ? creatorRoot.info.missing - (creatorRoot.info.movieFound ? 0 : 1) : 0
                                label: "Found"
                                value: creatorRoot.listOk ? (creatorRoot.info.count - texturesMissing) + " of "
                                                            + creatorRoot.info.count
                                                            + (texturesMissing > 0 ? "  (" + texturesMissing + " missing)" : "")
                                                          : ""
                                valueColor: texturesMissing > 0 ? creatorRoot.errorColor : creatorRoot.okColor
                            }
                            Text {
                                visible: !creator.isV1Game()
                                Layout.fillWidth: true
                                text: "BSBs belong to the Blobset V1 games: load one of them to use the new BSB with Modify / Create."
                                color: BtDarkStyle.accentDim
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                            InfoRow { label: "Total size"; value: creatorRoot.humanSize(creatorRoot.info.totalSize || 0) }
                            InfoRow { label: "Platform"; value: creatorRoot.info.platform || "" }
                            InfoRow {
                                label: "Blobset entry"
                                value: creatorRoot.info.hasTarget ? "Index " + creatorRoot.info.index
                                                                  : "Unknown (set it in the output .xml)"
                                valueColor: creatorRoot.info.hasTarget ? BtDarkStyle.textMain : BtDarkStyle.accentDim
                            }
                            InfoRow {
                                visible: creatorRoot.info.folderHash !== "" || creatorRoot.info.fileNameHash !== ""
                                label: "Hashes"
                                value: (creatorRoot.info.folderHash || "-") + " / " + (creatorRoot.info.fileNameHash || "-")
                            }
                        }
                    }
                }
            }

            // Right: texture list + log
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: "Textures" + (creatorRoot.listOk ? "  (" + creatorRoot.info.count + ")" : "")

                    ListView {
                        id: entryView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: creatorRoot.listOk ? creatorRoot.info.entries : []
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: entryView.width
                            height: 26
                            color: rowArea.containsMouse ? BtDarkStyle.bgHover
                                                         : (index % 2 === 0 ? "transparent" : BtDarkStyle.bgDarkest)
                            radius: 3

                            MouseArea { id: rowArea; anchors.fill: parent; hoverEnabled: true }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 12
                                spacing: 8

                                Text {
                                    text: (index + 1).toString()
                                    color: BtDarkStyle.textDim
                                    font.pixelSize: 11
                                    font.family: "Consolas"
                                    horizontalAlignment: Text.AlignRight
                                    Layout.preferredWidth: 34
                                }
                                Rectangle {
                                    Layout.preferredWidth: 8
                                    Layout.preferredHeight: 8
                                    radius: 4
                                    color: modelData.found ? creatorRoot.okColor : creatorRoot.errorColor
                                }
                                Text {
                                    text: modelData.file
                                    color: modelData.found ? BtDarkStyle.textMain : creatorRoot.errorColor
                                    font.pixelSize: 12
                                    font.family: "Consolas"
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: modelData.found ? creatorRoot.humanSize(modelData.size) : "missing"
                                    color: modelData.found ? BtDarkStyle.textDim : creatorRoot.errorColor
                                    font.pixelSize: 11
                                    font.family: "Consolas"
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: entryView.count === 0
                            text: "No list loaded"
                            color: BtDarkStyle.textDim
                            font.pixelSize: 13
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    title: "Log"

                    ListView {
                        id: logView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: logModel
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Text {
                            required property string message
                            required property bool ok
                            required property string time
                            width: logView.width
                            text: "[" + time + "]  " + message
                            color: ok ? BtDarkStyle.textMain : creatorRoot.errorColor
                            font.pixelSize: 12
                            font.family: "Consolas"
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }

        // Status bar (same as the viewers)
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
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font.pointSize: 9
                    color: creator.statusOk ? BtDarkStyle.textDim : creatorRoot.errorColor
                    text: creator.statusText !== "" ? "Status: " + creator.statusText
                                                    : "Status: Ready | Blobset Tools BSB Creator"
                }

                RowLayout {
                    spacing: 10
                    Layout.alignment: Qt.AlignVCenter

                    Text {
                        text: creator.progress > 0 ? creator.progress + "%" : ""
                        color: BtDarkStyle.textDim
                        font.pointSize: 9
                    }

                    ProgressBar {
                        id: statusBarProgress
                        from: 0
                        to: 100
                        value: creator.progress
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
                        text: creator.appVersion
                        color: BtDarkStyle.accentColor
                        font.pointSize: 9
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Dialogs
    // ------------------------------------------------------------------------------------------------
    FileDialog {
        id: listDialog
        title: "Choose the " + creatorRoot.listName
        nameFilters: ["Creator list (" + creatorRoot.listName + " *.xml)", "All files (*)"]
        currentFolder: creatorRoot.xmlPath !== "" ? creatorRoot.toUrl(creatorRoot.folderOf(creatorRoot.xmlPath))
                                                  : appOrchestrator.workFolderUrl("bsb")
        onAccepted: {
            creatorRoot.xmlPath = creatorRoot.toLocal(selectedFile)
            creatorRoot.sourceDir = ""
            creatorRoot.outputEdited = false
            creatorRoot.refresh()
            if (creatorRoot.listOk)
                creatorRoot.log("Loaded " + creatorRoot.listName + ": " + creatorRoot.info.count + " texture(s)"
                                + (creatorRoot.info.missing > 0 ? ", " + creatorRoot.info.missing + " missing." : "."),
                                creatorRoot.info.missing === 0)
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Choose the folder with the movie and the textures"
        currentFolder: creatorRoot.listOk ? creatorRoot.toUrl(creatorRoot.info.sourceDir) : appOrchestrator.workFolderUrl("bsb")
        onAccepted: {
            creatorRoot.sourceDir = creatorRoot.toLocal(selectedFolder)
            creatorRoot.refresh()
        }
    }

    FileDialog {
        id: saveDialog
        title: "Save the new BSB"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "bsb"
        nameFilters: ["BSB files (*.bsb)"]
        currentFolder: creatorRoot.outputPath !== "" ? creatorRoot.toUrl(creatorRoot.folderOf(creatorRoot.outputPath)) : ""
        selectedFile: creatorRoot.toUrl(creatorRoot.outputPath)
        onAccepted: {
            creatorRoot.outputPath = creatorRoot.toLocal(selectedFile)
            creatorRoot.outputEdited = true
        }
    }
}
