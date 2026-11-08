import QtQuick
import QtQuick.Controls.Material
import com.wouldymods.blobset 1.0

// Career Editor: a Rugby League Live 4 career (Career000\CareerData.SAV) or Rugby League Live 3 career (CAR000.SAV),
// in the Save Editor's layout. Ports of Wouldy's RLL4 / RLL3 Career Save Editors (Rll4CareerEditorManager,
// Rll3CareerEditorManager): every team and player, plus the career's settings, player status, contracts, retirements
// (RLL4) and competitions (the Career list). It's SaveEditor.qml in career mode
// (careerMode, set before it loads the save).
Item {
    id: careerRoot
    anchors.fill: parent

    readonly property url editorUrl: Qt.resolvedUrl("SaveEditor.qml")

    Loader {
        id: editorLoader
        anchors.fill: parent
        Component.onCompleted: setSource(careerRoot.editorUrl, { careerMode: true })
    }

    // If the editor can't be made, say why (the QML errors).
    Text {
        visible: editorLoader.status === Loader.Error
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 900)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WrapAnywhere
        color: "#ef5350"
        font.family: "Consolas"
        font.pixelSize: 12
        text: visible ? "The Career Editor couldn't be loaded:\n" + Qt.createComponent(careerRoot.editorUrl).errorString() : ""
    }
}
