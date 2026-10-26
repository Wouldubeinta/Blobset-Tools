import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Dialogs
import com.wouldymods.blobset 1.0

Item {
    id: gameSelectionView
    anchors.fill: parent

    // Declare the UI warning dialogue component
    MessageDialog {
        id: metadataErrorDialog
        buttons: MessageDialog.Ok
    }

    // Attach a listener to your AppOrchestrator object context
    Connections {
        target: appOrchestrator

        function onMissingMetadataError(title, message) {
            metadataErrorDialog.title = title;
            metadataErrorDialog.text = message;
            metadataErrorDialog.open(); // Display the warning overlay modally
        }
    }

    Image {
        id: backgroundImage
        anchors.fill: parent
        source: "qrc:/qt/qml/com/wouldymods/blobset/assets/blackorchid.png"
        fillMode: Image.Tile
        horizontalAlignment: Image.AlignLeft
        verticalAlignment: Image.AlignTop
        asynchronous: true
        cache: true
        opacity: 0.45
    }

    Rectangle {
        anchors.fill: parent
        opacity: 0.6
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: "#000000" } // Darkens edges/bottom
        }
    }

    Component.onCompleted: {
        appOrchestrator.loadGameProfile()
    }

    // =======================================================================
    // 🔄 REACTION HOOK (Handling the Signal emitted from C++)
    // Connects to AppOrchestrator's profileLoaded(...) signal to update the dropdowns
    // =======================================================================
    Connections {
        target: appOrchestrator

        function onProfileLoaded(gameId, platformId) {
            gameDropdown.currentIndex = gameId ?? 0
            platformDropdown.selectPlatform(platformId ?? 0)   // By id: console-only games list PS3 first.
        }

        function onGameSelectionInfoLoaded(gameSelectedInfo) {
            gameDropdown.model = gameSelectedInfo
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        id: selectionLayout
        width: Math.min(parent.width - 80, 760)
        spacing: 22

        // Selected game entry (GameSelectionInfo.xml) and its GameInfo.ini summary for the chosen platform.
        readonly property var selectedGame: gameDropdown.currentIndex >= 0 && gameDropdown.model
                                            ? gameDropdown.model[gameDropdown.currentIndex] : null
        readonly property var gameInfo: selectedGame
                                        ? appOrchestrator.gameInfoPreview(selectedGame.modFolder, platformDropdown.platformId)
                                        : ({})

        RowLayout {
            Layout.fillWidth: true
            spacing: 20

        // Dynamic Workspace Logo Display Box
        Rectangle {
            Layout.preferredWidth: 300
            Layout.preferredHeight: 300
            Layout.alignment: Qt.AlignTop
            color: BtDarkStyle.bgMedium
            border.color: BtDarkStyle.borderDark
            radius: 8
            clip: true

            Text {
                anchors.centerIn: parent
                text: "🎮"
                font.pixelSize: 72
                visible: !gameImagePreview.visible
            }

            Image {
                id: gameImagePreview
                anchors.fill: parent
                anchors.margins: 10
                fillMode: Image.PreserveAspectFit
                source: gameDropdown.currentIndex !== -1
                    ? "file:///" + applicationDirPath + "/assets/games/" + gameDropdown.model[gameDropdown.currentIndex].image
                    : ""
                onStatusChanged: if(status === Image.Error) visible = false
            }
			
            // Dark vignette overlay loop
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: BtDarkStyle.darkVignette }
                    GradientStop { position: 0.4; color: "transparent" }
                    GradientStop { position: 1.0; color: BtDarkStyle.darkVignette }
                }
            }
        }

        // GameInfo.ini metadata card
        Rectangle {
            id: infoCard
            Layout.fillWidth: true
            Layout.preferredHeight: 300
            color: BtDarkStyle.bgDark
            border.color: BtDarkStyle.borderDark
            radius: 8

            readonly property var info: selectionLayout.gameInfo

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                Label {
                    Layout.fillWidth: true
                    text: infoCard.info.gameName || (gameDropdown.currentText || "No game selected")
                    color: BtDarkStyle.accentColor
                    font.pixelSize: 18
                    font.bold: true
                    elide: Text.ElideRight
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: BtDarkStyle.borderDark }

                Label {
                    visible: infoCard.info.found !== true
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "No GameInfo.ini found for " + (infoCard.info.platform || "this platform") + "."
                    color: BtDarkStyle.textDim
                    font.pixelSize: 13
                }

                GridLayout {
                    visible: infoCard.info.found === true
                    Layout.fillWidth: true
                    columns: 1
                    rowSpacing: 7

                    Repeater {
                        model: [
                            ["Platform",       infoCard.info.platform],
                            ["Game ID",        infoCard.info.gameId],
                            ["Version",        infoCard.info.gameVersion],
                            ["Steam App ID",   infoCard.info.steamGameId > 0 ? infoCard.info.steamGameId : "None (console / non-Steam)"],
                            ["Blobset",        (infoCard.info.blobsetVersion || "") + "  ·  " + (infoCard.info.compression || "")],
                            ["Blobset Size",   infoCard.info.locationExists ? infoCard.info.blobsetSize : "Not found"],
                            ["File Mapping",   infoCard.info.mappingExists ? "Mapped  ·  " + infoCard.info.mappingDate : "Not mapped yet"],
                            ["Mod Files",      infoCard.info.modCount ?? 0]
                        ]
                        delegate: Item {
                            Layout.fillWidth: true
                            implicitHeight: valueLabel.implicitHeight
                            Label {
                                id: keyLabel
                                width: 110
                                text: modelData[0]
                                color: BtDarkStyle.textDim
                                font.pixelSize: 13
                            }
                            Label {
                                id: valueLabel
                                anchors.left: keyLabel.right
                                anchors.right: parent.right
                                text: String(modelData[1] ?? "")
                                color: (modelData[0] === "Blobset Size" && !infoCard.info.locationExists)
                                       || (modelData[0] === "File Mapping" && !infoCard.info.mappingExists)
                                       ? "#ef5350" : BtDarkStyle.textMain
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
        }   // RowLayout

        // Form Fields
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            Label { text: "Select System Platform:"; font.pixelSize: 12; font.bold: true; color: BtDarkStyle.textDim }
            ComboBox {
                id: platformDropdown
                Layout.fillWidth: true
                Layout.preferredHeight: 44

                // The list only holds the platforms the game supports, so its index isn't the platform id
                // (console-only games list "Playstation 3" first). Map by name to CoreDefs::Platforms.
                readonly property var platformNames: ["Windows", "Playstation 3", "Xbox 360"]
                readonly property int platformId: Math.max(0, platformNames.indexOf(currentText))

                function selectPlatform(id) {
                    currentIndex = Math.max(0, find(platformNames[id] ?? ""))
                }

                // Evaluates if the current game is restricted to Windows, console-only, or multi-platform
                model: {
                    if (gameDropdown.currentIndex === -1 || !gameDropdown.model || !gameDropdown.model[gameDropdown.currentIndex]) {
                        return ["Windows"];
                    }

                    var currentGame = gameDropdown.model[gameDropdown.currentIndex];

                    if (currentGame.ps3 === false || currentGame.xbox360 === false) {
                        return ["Windows"];
                    } else if (currentGame.pc === false) {
                        return ["Playstation 3", "Xbox 360"];
                    } else {
                        return ["Windows", "Playstation 3", "Xbox 360"];
                    }
                }

                delegate: ItemDelegate {
                    width: platformDropdown.width
                    implicitHeight: 34 // <--- Changes item height

                    contentItem: Label {
                        text: modelData
                        color: (hovered || highlighted) ? BtDarkStyle.accentColor : BtDarkStyle.textMain // Changes color on hover/selection
                        font.pixelSize: 15
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 10
                    }

                    background: Rectangle {
                        color: highlighted ? BtDarkStyle.accentColor : (hovered ? BtDarkStyle.bgHover : "transparent")
                    }
                }

                background: Rectangle { color: BtDarkStyle.bgMedium; border.color: BtDarkStyle.borderDark; radius: 4 }
                contentItem: Label { text: platformDropdown.currentText; color: BtDarkStyle.textMain; font.pixelSize: 15; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            Label { text: "Select Your Game..."; font.pixelSize: 12; font.bold: true; color: BtDarkStyle.textDim }
            ComboBox {
                id: gameDropdown
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                textRole: "name"

                delegate: ItemDelegate {
                    width: gameDropdown.width
                    implicitHeight: 34 // <--- Changes item height

                    contentItem: Label {
                        text: modelData.name ?? ""
                        color: (hovered || highlighted) ? BtDarkStyle.accentColor : BtDarkStyle.textMain // Changes color on hover/selection
                        font.pixelSize: 15
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 10
                    }

                    background: Rectangle {
                        color: highlighted ? BtDarkStyle.accentColor : (hovered ? BtDarkStyle.bgHover : "transparent")
                    }
                }

                background: Rectangle { color: BtDarkStyle.bgMedium; border.color: BtDarkStyle.borderDark; radius: 4 }
                contentItem: Label { text: gameDropdown.currentText; color: BtDarkStyle.textMain; font.pixelSize: 15; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
            }
        }

        Button {
            BtToolTip { text: "Open the selected game in Blobset Tools" }
            id: loadWorkspaceButton
            text: "Load Blobset Tools"
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            Layout.topMargin: 10

            background: Rectangle {
                color: loadWorkspaceButton.hovered ? BtDarkStyle.accentColor : BtDarkStyle.bgMedium
                border.color: BtDarkStyle.accentColor
                radius: 4
            }
            contentItem: Label {
                text: loadWorkspaceButton.text
                color: loadWorkspaceButton.hovered ? BtDarkStyle.bgMedium : BtDarkStyle.textMain
                font.pixelSize: 14
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: {
                const game = gameDropdown.model[gameDropdown.currentIndex]
                if (!game)
                    return
                appOrchestrator.selectGameAndLoad(gameDropdown.currentIndex, platformDropdown.platformId,
                                                  game.modFolder, game.folderName)
            }
        }
    }
}
