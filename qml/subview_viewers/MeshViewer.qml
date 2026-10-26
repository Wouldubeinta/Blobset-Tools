import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick3D
import QtQuick3D.Helpers
import com.wouldymods.blobset 1.0

// Mesh viewer: a static mesh (.mesh) of the loaded blobset in 3D. Left drag orbits, the mouse wheel zooms,
// Reset View brings the camera back. Same header / info strip / status bar as the TXPK and DDS viewers.
Item {
    id: meshViewRoot
    anchors.fill: parent

    property string activeHashString: ""
    readonly property string iconDir: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/"

    Rectangle {
        anchors.fill: parent
        color: BtDarkStyle.bgDarkest
        z: -1
    }

    MeshToolManager {
        id: meshManager
        onMeshChanged: meshViewRoot.resetView()
    }

    Connections {
        target: appOrchestrator
        function onOpenMeshViewerByHash(hexHash, displayName) {
            meshViewRoot.activeHashString = hexHash
            meshManager.loadMeshFromBlobset(hexHash, displayName)
        }
    }

    // Puts the camera back in front of the whole mesh.
    function resetView() {
        orbitOrigin.position = meshManager.center   // Undoes middle button panning.
        orbitOrigin.eulerRotation = Qt.vector3d(-20, 35, 0)
        camera.position = Qt.vector3d(0, 0, meshManager.radius * 2.6)
    }

    // Header button (same look as the TXPK viewer).
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

                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 28; Layout.alignment: Qt.AlignVCenter; color: BtDarkStyle.borderDark }

                HeaderButton {
                    tip: "Reset the camera so the whole mesh fits the view"
                    text: "Reset View"
                    iconSource: meshViewRoot.iconDir + "reset_32.png"
                    enabled: meshManager.loaded
                    onClicked: meshViewRoot.resetView()
                }

                HeaderButton {
                    tip: "Show the mesh as a wireframe"
                    id: wireButton
                    text: checked ? "Wireframe: On" : "Wireframe: Off"
                    iconSource: meshViewRoot.iconDir + "wireframe_32.png"
                    enabled: meshManager.loaded
                    checkable: true
                }

                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 28; Layout.alignment: Qt.AlignVCenter; color: BtDarkStyle.borderDark }

                HeaderButton {
                    tip: "Extract the mesh as the original .mesh or as a Wavefront .obj"
                    id: extractButton
                    text: "Extract ▾"
                    iconSource: meshViewRoot.iconDir + "extract_32.png"
                    enabled: meshManager.loaded
                    onClicked: extractMenu.popup(extractButton, 0, extractButton.height)
                }

                // Keeps the buttons together on the left (like the TXPK / DDS viewers).
                Item { Layout.fillWidth: true }

                Text {
                    text: meshManager.sourceName
                    color: BtDarkStyle.textDim
                    font.family: "Consolas"
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 420
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }

        // ------------------------------------------------------------------------------------------------
        // 3D view
        // ------------------------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: BtDarkStyle.bgDarkest
            clip: true

            View3D {
                id: view3d
                anchors.fill: parent
                anchors.bottomMargin: 28   // Room for the info strip.
                visible: meshManager.loaded

                environment: SceneEnvironment {
                    backgroundMode: SceneEnvironment.Color
                    clearColor: BtDarkStyle.bgDarkest
                    antialiasingMode: SceneEnvironment.MSAA
                    antialiasingQuality: SceneEnvironment.High
                    debugSettings: DebugSettings {
                        wireframeEnabled: wireButton.checked
                    }
                }

                // The camera orbits around the centre of the mesh.
                Node {
                    id: orbitOrigin
                    position: meshManager.center

                    PerspectiveCamera {
                        id: camera
                        clipNear: Math.max(0.001, meshManager.radius * 0.005)
                        clipFar: meshManager.radius * 100

                        // Key light from the viewer, so the visible side is always lit.
                        DirectionalLight {
                            brightness: 1.0
                            eulerRotation: Qt.vector3d(-10, -10, 0)
                        }
                    }
                }

                // Soft fill light from above.
                DirectionalLight {
                    eulerRotation: Qt.vector3d(-70, 0, 0)
                    brightness: 0.35
                }

                Model {
                    geometry: meshManager.geometry
                    materials: PrincipledMaterial {
                        baseColor: "#c9c9cf"
                        roughness: 0.75
                        metalness: 0.0
                        cullMode: PrincipledMaterial.NoCulling
                        lighting: PrincipledMaterial.FragmentLighting
                    }
                }

            }

            OrbitCameraController {
                anchors.fill: view3d
                origin: orbitOrigin
                camera: camera
                enabled: meshManager.loaded
                xSpeed: 0.3
                ySpeed: 0.3
                panEnabled: false   // Panning is the middle mouse button (below).
            }

            // Middle mouse button: moves the mesh with the cursor (pans the orbit centre in the view plane).
            MouseArea {
                anchors.fill: view3d
                enabled: meshManager.loaded
                acceptedButtons: Qt.MiddleButton
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                property real lastX: 0
                property real lastY: 0
                onPressed: (mouse) => { lastX = mouse.x; lastY = mouse.y }
                onPositionChanged: (mouse) => {
                    // World units per screen pixel at the orbit centre's distance.
                    var distance = camera.position.length()
                    var perPixel = 2 * distance * Math.tan(camera.fieldOfView * Math.PI / 360) / Math.max(1, view3d.height)
                    var dx = (mouse.x - lastX) * perPixel
                    var dy = (mouse.y - lastY) * perPixel
                    orbitOrigin.position = orbitOrigin.position.minus(camera.right.times(dx)).plus(camera.up.times(dy))
                    lastX = mouse.x
                    lastY = mouse.y
                }
            }

            // Messages while nothing is shown.
            Text {
                anchors.centerIn: parent
                visible: !meshManager.loaded
                width: parent.width - 80
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.pixelSize: 18
                font.bold: true
                color: meshManager.statusOk ? BtDarkStyle.white : "#ef5350"
                text: meshManager.statusOk || meshManager.statusText === "" ? "Waiting for a mesh to load..."
                                                                           : meshManager.statusText
            }

            // Hint (top left).
            Text {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 10
                visible: meshManager.loaded
                text: "Left drag: orbit   |   Middle drag: move   |   Wheel: zoom"
                color: BtDarkStyle.textDim
                font.pixelSize: 11
            }

            // Info strip (same as the other viewers).
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 28
                color: BtDarkStyle.bgMedium
                visible: meshManager.infoLine !== ""

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideMiddle
                    color: BtDarkStyle.textMain
                    font.family: "Consolas"
                    font.pixelSize: 12
                    text: meshManager.infoLine
                }
            }
        }

        // ------------------------------------------------------------------------------------------------
        // Status bar
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

                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font.pointSize: 9
                    color: meshManager.statusOk ? BtDarkStyle.textDim : "#ef5350"
                    text: "Status: Ready | Blobset Tools Mesh Viewer"
                }

                Text {
                    text: meshManager.appVersion
                    color: BtDarkStyle.accentColor
                    font.pointSize: 9
                }
            }
        }
    }

    Menu {
        id: extractMenu

        MenuItem {
            text: "Original .mesh..."
            icon.source: meshViewRoot.iconDir + "mesh_32.png"
            icon.color: "transparent"
            onTriggered: extractDialog.start("mesh")
        }
        MenuItem {
            text: "Wavefront .obj..."
            icon.source: meshViewRoot.iconDir + "obj_32.png"
            icon.color: "transparent"
            onTriggered: extractDialog.start("obj")
        }
    }

    FolderDialog {
        id: extractDialog
        property string format: "obj"

        function start(fmt) {
            format = fmt
            title = "Extract Mesh as " + (fmt === "obj" ? "OBJ" : "original .mesh") + " to Directory"
            open()
        }

        onAccepted: meshManager.extract(selectedFolder.toString(), format)
    }
}
