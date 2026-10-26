import QtQuick
import QtQuick.Controls.Material
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
