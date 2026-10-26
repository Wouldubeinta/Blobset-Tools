import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import com.wouldymods.blobset 1.0

Item {
    id: hexViewerRoot
    anchors.fill: parent

    // Optional: inspect a byte range of a file on disk instead of a blobset asset.
    property string targetFilePath: ""
    property string targetFileName: ""
    property int targetOffset: 0
    property int targetSize: 0

    // Path hash of the asset opened from the blobset (set by AppOrchestrator::openHexViewerByHash).
    property string activeHashString: ""

    readonly property string iconDir: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/"
    readonly property int rowHeight: 22
    readonly property int addressColWidth: 110
    readonly property int hexColWidth: 400
    readonly property int asciiColWidth: 160

    Rectangle {
        anchors.fill: parent
        color: BtDarkStyle.bgDarkest
        z: -1
    }

    // Header button (same look as the other viewers).
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

    // Column title of the dump header.
    component ColumnTitle: Text {
        height: parent.height
        verticalAlignment: Text.AlignVCenter
        color: "#90CAF9"
        font.bold: true
        font.pixelSize: 13
    }

    Connections {
        target: appOrchestrator

        // Raised by the orchestrator right after it switched to this view.
        function onOpenHexViewerByHash(hexHash, displayName) {
            hexViewerRoot.activeHashString = hexHash
            hexViewerRoot.targetFileName = displayName
            hexModel.loadFromBlobset(hexHash, displayName)
        }
    }

    HexViewerModel {
        id: hexModel

        onContentChanged: {
            hexList.currentIndex = hexModel.dataSize > 0 ? 0 : -1
            hexList.positionViewAtBeginning()
        }
    }

    function goToOffset(text) {
        var clean = text.trim().replace(/^0x/i, "")
        if (clean === "" || !/^[0-9a-fA-F]+$/.test(clean)) {
            hexModel.reportMessage("Enter the offset as hexadecimal, e.g. 1A0 or 0x1A0.", false)
            return
        }

        var row = hexModel.rowForOffset(parseInt(clean, 16))
        if (row < 0) {
            hexModel.reportMessage("Offset 0x" + clean.toUpperCase() + " is outside of this asset.", false)
            return
        }

        hexList.currentIndex = row
        hexList.positionViewAtIndex(row, ListView.Center)
        hexList.forceActiveFocus()
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

                Image {
                    source: hexViewerRoot.iconDir + "dat_32.png"
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    fillMode: Image.PreserveAspectFit
                }

                Text {
                    text: "Hex Inspector - " + (hexModel.sourceName !== "" ? hexModel.sourceName
                                                                          : (hexViewerRoot.targetFileName !== "" ? hexViewerRoot.targetFileName : "No file"))
                    color: BtDarkStyle.textMain
                    font.bold: true
                    font.pixelSize: 15
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                Text {
                    visible: hexModel.mainSize >= 0 && hexModel.mainSize < hexModel.dataSize
                    text: "Gold address = VRAM section"
                    color: BtDarkStyle.textDim
                    font.pixelSize: 11
                    font.italic: true
                }

                // Go to offset (top right, where the other views have their search / filter).
                Rectangle {
                    Layout.preferredWidth: 260
                    Layout.minimumWidth: 160
                    Layout.preferredHeight: 34
                    color: BtDarkStyle.bgMedium
                    border.width: 1
                    border.color: offsetInput.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                    radius: 4
                    opacity: hexModel.dataSize > 0 ? 1.0 : 0.5

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6

                        Text { text: "⇥"; font.pixelSize: 13; color: BtDarkStyle.textDim }

                        TextInput {
                            id: offsetInput
                            Layout.fillWidth: true
                            color: BtDarkStyle.textMain
                            font.pixelSize: 13
                            font.family: "Consolas"
                            selectByMouse: true
                            clip: true
                            enabled: hexModel.dataSize > 0

                            onAccepted: hexViewerRoot.goToOffset(text)
                            Keys.onEscapePressed: text = ""

                            Text {
                                text: "Go to offset (hex)..."
                                color: BtDarkStyle.textDim
                                font.pixelSize: 13
                                font.italic: true
                                visible: offsetInput.text === "" && !offsetInput.activeFocus
                            }
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------------------------------------------
        // Dump
        // ------------------------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 10
            color: BtDarkStyle.bgDark
            border.width: 1
            border.color: hexList.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
            radius: 4
            clip: true

            // Column titles
            Rectangle {
                id: columnHeader
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 1
                height: 30
                color: BtDarkStyle.bgMedium

                Row {
                    anchors.fill: parent
                    leftPadding: 10

                    ColumnTitle { width: hexViewerRoot.addressColWidth; text: "Offset" }
                    ColumnTitle {
                        width: hexViewerRoot.hexColWidth
                        text: "00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F"
                        font.family: "Consolas"
                    }
                    ColumnTitle { width: hexViewerRoot.asciiColWidth; text: "ASCII" }
                }
            }

            ListView {
                id: hexList
                anchors.top: columnHeader.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 1
                clip: true
                focus: true
                model: hexModel
                currentIndex: -1
                keyNavigationEnabled: true
                highlightMoveDuration: 0
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true

                Keys.onPressed: (event) => {
                    if (count <= 0)
                        return

                    const pageStep = Math.max(1, Math.floor(height / hexViewerRoot.rowHeight) - 1)
                    switch (event.key) {
                    case Qt.Key_Home:     currentIndex = 0; break
                    case Qt.Key_End:      currentIndex = count - 1; break
                    case Qt.Key_PageDown: currentIndex = Math.min(count - 1, currentIndex + pageStep); break
                    case Qt.Key_PageUp:   currentIndex = Math.max(0, currentIndex - pageStep); break
                    default:              return // Up / Down are handled by the ListView itself.
                    }
                    event.accepted = true
                }

                delegate: Rectangle {
                    id: hexRow
                    required property int index
                    required property string address
                    required property string hexData
                    required property string asciiData
                    required property bool isVram

                    readonly property bool isSelected: ListView.isCurrentItem

                    width: ListView.view ? ListView.view.width : 0
                    height: hexViewerRoot.rowHeight
                    color: isSelected ? (hexList.activeFocus ? BtDarkStyle.accentDim : BtDarkStyle.bgHover)
                                      : (index % 2 === 0 ? BtDarkStyle.bgDarkest : BtDarkStyle.darkVignette)

                    Row {
                        anchors.fill: parent
                        leftPadding: 10

                        Text {
                            width: hexViewerRoot.addressColWidth
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            text: hexRow.address
                            color: hexRow.isSelected ? "#121214"
                                                     : (hexRow.isVram ? BtDarkStyle.accentColor : BtDarkStyle.textDim)
                            font.family: "Consolas"
                            font.pixelSize: 13
                        }
                        Text {
                            width: hexViewerRoot.hexColWidth
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            text: hexRow.hexData
                            color: hexRow.isSelected ? "#121214" : "#81C784"
                            font.family: "Consolas"
                            font.pixelSize: 13
                        }
                        Text {
                            width: hexViewerRoot.asciiColWidth
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            text: hexRow.asciiData
                            color: hexRow.isSelected ? "#121214" : "#FFCA28"
                            font.family: "Consolas"
                            font.pixelSize: 13
                        }
                    }

                    TapHandler {
                        onTapped: {
                            hexList.currentIndex = hexRow.index
                            hexList.forceActiveFocus()
                        }
                    }
                }

                ScrollBar.vertical: ScrollBar { active: true; policy: ScrollBar.AsNeeded }
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: hexModel.busy
                visible: running
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: !hexModel.busy && hexModel.dataSize === 0
                color: hexModel.statusOk ? BtDarkStyle.textDim : Material.color(Material.Red)
                font.italic: true
                text: !hexModel.statusOk ? hexModel.statusText
                      : (hexModel.sourceName !== "" ? "This asset is empty." : "Waiting for an asset to load...")
            }
        }

        // ------------------------------------------------------------------------------------------------
        // Status bar (same as the other views: status text, selected offset, version)
        // ------------------------------------------------------------------------------------------------
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
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font.pointSize: 9
                    color: hexModel.statusOk ? BtDarkStyle.textDim : Material.color(Material.Red)
                    text: hexModel.statusText !== "" ? "Status: " + hexModel.statusText
                                                     : "Status: Ready | Blobset Tools Hex Inspector"
                }

                Text {
                    visible: hexList.currentIndex >= 0
                    text: "Row: 0x" + (hexModel.baseOffset + hexList.currentIndex * 16).toString(16).toUpperCase()
                    color: BtDarkStyle.textDim
                    font.family: "Consolas"
                    font.pointSize: 9
                }

                Text {
                    text: "v" + Qt.application.version
                    color: BtDarkStyle.accentColor
                    font.pointSize: 9
                }
            }
        }
    }

    Component.onCompleted: {
        if (targetFilePath !== "")
            hexModel.loadData(targetFilePath, targetOffset, targetSize)
        hexList.forceActiveFocus()
    }
}
