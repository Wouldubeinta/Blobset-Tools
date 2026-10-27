import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import com.wouldymods.blobset 1.0

// About window (MainForm.qml > About > Info): logo, name, version, then Credits / Info / License / Changelog tabs.
Popup {
    id: aboutWindow

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(620, parent ? parent.width - 40 : 620)
    height: Math.min(560, parent ? parent.height - 40 : 560)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property string appVersion: Qt.application.version !== "" ? Qt.application.version : "1.0.0.0"

    onOpened: {
        tabBar.currentIndex = 0
        const changelog = appOrchestrator.readAppTextFile("changelog.txt")
        changelogText = changelog !== "" ? changelog : builtInChangelog
    }

    Overlay.modal: Rectangle { color: "#b0000000" }

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 } }

    background: Rectangle {
        color: BtDarkStyle.bgDark
        border.color: BtDarkStyle.accentColor
        border.width: 1
        radius: 8
    }

    // ------------------------------------------------------------------------------------------------
    // Tab text (edit freely)
    // ------------------------------------------------------------------------------------------------
    readonly property string creditsText:
        "Blobset Tools\n" +
        "Written by Wouldubeinta (Wouldy Mods)\n\n" +
        "Thanks to everyone in the BigAnt modding community for testing, file research and feedback.\n\n" +
        "Libraries\n" +
        "  •  Qt 6 - The Qt Company (LGPL v3)\n" +
        "  •  DirectXTex - Microsoft (MIT)\n" +
        "  •  Zstandard - Meta Platforms (BSD)\n" +
        "  •  XZ Utils / liblzma - Lasse Collin and the Tukaani Project (public domain / 0BSD)\n" +
        "  •  vgmstream - vgmstream contributors (ISC)\n" +
        "  •  FFmpeg - FFmpeg developers (LGPL v2.1+)\n" +
        "  •  Ogg, Vorbis and Opus - Xiph.Org Foundation (BSD)\n"

    readonly property string infoText:
        "Description: Extract and Modify BigAnt games blobset files\n\n" +
        "Author: Wouldubeinta\n" +
        "Discord ID: Wouldubeinta\n\n" +
        "Supported blobset versions: V1, V2, V3 and V4 (PC, PS3 and Xbox 360 where the game was released on them).\n\n" +
        "Viewers: DDS, TXPK, M3MP, Mesh, BNK, FMOD, Hex\n" +
        "Creators: TXPK, M3MP\n" +
        "Tools: Extract, Modify, Create, File Mapping, Resolve File Names, Restore Backup Files, Blobset Mod Loader (V4)"

    readonly property string licenseText:
        "Blobset Tools\n" +
        "Copyright (C) 2026 Wouldy Mods.\n\n" +
        "This program is free software; you can redistribute it and/or modify it under the terms of the GNU " +
        "General Public License as published by the Free Software Foundation; either version 3 of the License, " +
        "or (at your option) any later version.\n\n" +
        "This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even " +
        "the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public " +
        "License for more details.\n\n" +
        "You should have received a copy of the GNU General Public License along with this program. " +
        "If not, see https://www.gnu.org/licenses/.\n\n" +
        "The libraries listed under Credits are covered by their own licenses."

    // changelog.txt next to Blobset-Tools.exe (edit it to update this tab; read each time the window opens).
    property string changelogText: builtInChangelog

    // Shown when changelog.txt is missing.
    readonly property string builtInChangelog:
        "v" + appVersion + "\n" +
        "  •  New Qt 6 interface with the dark theme\n" +
        "  •  Game Selection shows the game's image and GameInfo.ini details\n" +
        "  •  Mesh viewer: RLL3 / RLL4 meshes, V4 mesh fixes\n" +
        "  •  Middle mouse pan in the DDS, TXPK and Mesh viewers\n" +
        "  •  Asset search selects the folder and file instead of filtering\n" +
        "  •  Resolve File Names and new assets in Modify / Create\n" +
        "  •  Incremental V4 file mapping, mods folder at games\\<game>\\<platform>\\mods\n" +
        "  •  Blobset Mod Loader for V4 games (the game files stay untouched)\n" +
        "  •  Restore Backup Files now restores the originals\n" +
        "  •  Load Game and Validate Steam Files for Steam games\n" +
        "  •  TXPK / M3MP Creators rebuilt; Extract All writes the creator lists\n" +
        "  •  Skip Unknown Files removed\n"

    // ------------------------------------------------------------------------------------------------
    // Layout
    // ------------------------------------------------------------------------------------------------
    contentItem: ColumnLayout {
        spacing: 0

        // Title: logo, name, version
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 124
            color: "transparent"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: BtDarkStyle.borderDark
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 18

                Image {
                    source: "qrc:/qt/qml/com/wouldymods/blobset/assets/wouldy_mods.png"
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 96
                    sourceSize: Qt.size(192, 192)
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Blobset Tools"
                        color: BtDarkStyle.accentColor
                        font.pixelSize: 28
                        font.bold: true
                    }
                    Text {
                        text: "Version " + aboutWindow.appVersion
                        color: BtDarkStyle.textMain
                        font.pixelSize: 14
                    }
                    Text {
                        text: "Extract and Modify BigAnt games blobset files"
                        color: BtDarkStyle.textDim
                        font.pixelSize: 12
                    }
                }
            }
        }

        // Tabs
        TabBar {
            id: tabBar
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 12
            background: Rectangle { color: "transparent" }

            Repeater {
                model: ["Credits", "Info", "License", "Changelog"]

                TabButton {
                    id: tabButton
                    required property string modelData
                    text: modelData
                    hoverEnabled: true
                    implicitHeight: 36

                    contentItem: Text {
                        text: tabButton.text
                        color: tabButton.checked ? BtDarkStyle.accentColor
                                                 : (tabButton.hovered ? BtDarkStyle.textMain : BtDarkStyle.textDim)
                        font.pixelSize: 13
                        font.bold: tabButton.checked
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: tabButton.checked ? BtDarkStyle.bgMedium
                                                 : (tabButton.hovered ? BtDarkStyle.bgHover : "transparent")
                        radius: 4
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 2
                            color: tabButton.checked ? BtDarkStyle.accentColor : "transparent"
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            color: BtDarkStyle.bgDarkest
            border.color: BtDarkStyle.borderDark
            border.width: 1
            radius: 4

            StackLayout {
                anchors.fill: parent
                anchors.margins: 1
                currentIndex: tabBar.currentIndex

                Repeater {
                    model: [aboutWindow.creditsText, aboutWindow.infoText, aboutWindow.licenseText,
                            aboutWindow.changelogText]

                    ScrollView {
                        id: page
                        required property string modelData
                        required property int index
                        // The Changelog (changelog.txt) keeps its lines as written: scroll sideways for long ones.
                        readonly property bool noWrap: index === 3
                        clip: true
                        ScrollBar.horizontal.policy: noWrap ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff

                        TextArea {
                            text: page.modelData
                            readOnly: true
                            selectByMouse: true
                            wrapMode: page.noWrap ? TextArea.NoWrap : TextArea.Wrap
                            color: BtDarkStyle.textMain
                            selectionColor: BtDarkStyle.accentDim
                            selectedTextColor: BtDarkStyle.white
                            font.pixelSize: 13
                            leftPadding: 12
                            rightPadding: 12
                            topPadding: 10
                            bottomPadding: 10
                            background: null
                        }
                    }
                }
            }
        }

        // Footer: copyright + Close
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            spacing: 12

            Text {
                text: "Copyright: Wouldy Mods 2026"
                color: BtDarkStyle.textDim
                font.pixelSize: 12
                Layout.fillWidth: true
            }

            AbstractButton {
                BtToolTip { text: "Close this window" }
                id: closeButton
                text: "Close"
                hoverEnabled: true
                implicitWidth: 90
                implicitHeight: 34
                onClicked: aboutWindow.close()

                background: Rectangle {
                    color: closeButton.hovered ? BtDarkStyle.bgMedium : "transparent"
                    border.color: BtDarkStyle.accentColor
                    border.width: 1
                    radius: 4
                }
                contentItem: Text {
                    text: closeButton.text
                    color: BtDarkStyle.accentColor
                    font.bold: true
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
