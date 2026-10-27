import QtQuick
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import com.wouldymods.blobset 1.0

ApplicationWindow {
    id: rootWindow
    visible: true
    width: 1400
    height: 800
    title: "Blobset Tools"

    Material.theme: Material.Dark
    Material.accent: "#f5c518"

    // This loader handles everything smoothly inside this single outer window
    Loader {
        id: viewLoader
        anchors.fill: parent
        source: "qrc:/qt/qml/com/wouldymods/blobset/qml/views/GameSelection.qml"
    }

    Connections {
        target: appOrchestrator
        function onViewChanged(viewName) {
            viewLoader.source = viewName;
        }
    }

    // ---------------------------------------------------------------------------------------------------------
    // Updates (UpdateChecker): a quiet check a few seconds after startup (at most once a day), and
    // Options > Check for Updates. The window lives here, so it shows over any view.
    // ---------------------------------------------------------------------------------------------------------
    Timer {
        interval: 4000
        running: true
        onTriggered: updater.checkOnStartup()
    }

    Connections {
        target: updater
        function onUpdateAvailable(version, notes) {
            updateDialog.errorText = ""
            updateDialog.open()
        }
        function onUpToDate(version) {
            updateInfoDialog.title = "No Update Available"
            updateInfoDialog.text = "Blobset Tools " + version + " is the latest version."
            updateInfoDialog.open()
        }
        function onCheckFailed(message) {
            updateInfoDialog.title = "Check for Updates"
            updateInfoDialog.text = "Couldn't check for updates.\n\n" + message
            updateInfoDialog.open()
        }
        function onDownloadFailed(message) {
            updateDialog.errorText = message
            if (!updateDialog.visible)
                updateDialog.open()
        }
    }

    MessageDialog {
        id: updateInfoDialog
        buttons: MessageDialog.Ok
    }

    Dialog {
        id: updateDialog
        property string errorText: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(rootWindow.width - 80, 640)
        modal: true
        closePolicy: updater.downloading ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
        padding: 20
        title: "Update Available"

        background: Rectangle {
            color: BtDarkStyle.bgDark
            border.color: BtDarkStyle.accentColor
            border.width: 1
            radius: 6
        }
        header: Text {
            text: updateDialog.title
            color: BtDarkStyle.accentColor
            font.bold: true
            font.pixelSize: 16
            leftPadding: 20
            topPadding: 16
        }

        contentItem: ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: "Blobset Tools <b>" + updater.latestVersion + "</b> is available. You have " + updater.currentVersion + "."
                      + (updater.portable ? "<br>This is a portable copy: download the new version from the release page." : "")
                color: BtDarkStyle.textMain
                font.pixelSize: 14
                wrapMode: Text.WordWrap
                textFormat: Text.StyledText
            }
            // The release's notes from GitHub (Markdown).
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 260
                visible: updater.releaseNotes !== ""
                color: BtDarkStyle.bgDarkest
                border.color: BtDarkStyle.borderDark
                radius: 4
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 10
                    clip: true
                    Text {
                        width: updateDialog.availableWidth - 30
                        text: updater.releaseNotes
                        textFormat: Text.MarkdownText
                        wrapMode: Text.WordWrap
                        color: BtDarkStyle.textMain
                        linkColor: BtDarkStyle.accentColor
                        font.pixelSize: 13
                        onLinkActivated: (link) => Qt.openUrlExternally(link)
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: updater.downloading
                spacing: 4
                Text {
                    text: updater.progress >= 0 ? "Downloading... " + Math.round(updater.progress * 100) + "%" : "Downloading..."
                    color: BtDarkStyle.textDim
                    font.pixelSize: 12
                }
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: Math.max(0, updater.progress)
                    indeterminate: updater.progress < 0
                }
            }
            Text {
                Layout.fillWidth: true
                visible: updateDialog.errorText !== ""
                text: updateDialog.errorText
                color: "#ef5350"
                font.pixelSize: 12
                wrapMode: Text.WrapAnywhere
            }
            Text {
                Layout.fillWidth: true
                visible: !updater.downloading && !updater.portable
                text: "Blobset Tools closes and the setup starts. Your settings, mods and games are kept."
                color: BtDarkStyle.textDim
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        footer: RowLayout {
            spacing: 8
            Button {
                Layout.leftMargin: 16
                Layout.bottomMargin: 14
                text: "Release Page"
                flat: true
                onClicked: updater.openReleasePage()
            }
            Item { Layout.fillWidth: true }
            Button {
                Layout.bottomMargin: 14
                visible: !updater.downloading
                text: "Skip This Version"
                flat: true
                onClicked: {
                    updater.skipThisVersion()
                    updateDialog.close()
                }
            }
            Button {
                Layout.bottomMargin: 14
                text: updater.downloading ? "Cancel" : "Later"
                flat: true
                onClicked: {
                    if (updater.downloading)
                        updater.cancelDownload()
                    else
                        updateDialog.close()
                }
            }
            Button {
                Layout.rightMargin: 16
                Layout.bottomMargin: 14
                visible: !updater.downloading
                highlighted: true
                // Not while Extract / Modify / ... runs: the setup closes Blobset Tools.
                enabled: updater.portable || !appOrchestrator.workerRunning
                text: updater.portable ? "Open Release Page" : "Download and Install"
                onClicked: {
                    updateDialog.errorText = ""
                    updater.downloadAndInstall()
                }
            }
        }
    }

    // A view that fails to load (e.g. a QML module missing next to the exe) would otherwise leave an empty window
    // with no way back. Show what went wrong and a way back to the workspace instead.
    Rectangle {
        anchors.fill: parent
        visible: viewLoader.status === Loader.Error
        color: BtDarkStyle.bgDarkest

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - 80, 700)
            spacing: 16

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: "#ef5350"
                font.pixelSize: 18
                font.bold: true
                text: "This view couldn't be loaded."
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WrapAnywhere
                color: BtDarkStyle.textDim
                font.family: "Consolas"
                font.pixelSize: 12
                text: viewLoader.source + "\nIf it says a module \"is not installed\", rebuild so windeployqt deploys it next to the exe."
            }
            Button {
                BtToolTip { text: "Return to the main workspace" }
                anchors.horizontalCenter: parent.horizontalCenter
                text: "◀ Back to Workspace"
                onClicked: viewLoader.source = "qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml"
            }
        }
    }
}
