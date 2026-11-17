import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick3D
import QtQuick3D.Helpers
import com.wouldymods.blobset 1.0

// Rugby League 26 appearance editor: the player built in 3D from their appearance, with the appearance fields beside
// it (sliders, the game's lists of heads / hair styles / beards / boots ...). It covers the Database, Save and Career
// editors (their Appearance tab's "3D Appearance Editor" button opens it); edits go through that editor
// (Rl26AppearanceManager), so they're saved with it. Back (or Esc) returns to the editor.
Rectangle {
    id: root
    color: BtDarkStyle.bgDarkest
    visible: false
    z: 1000

    property var editor: null
    property int player: -1
    property string title: ""
    property string group: ""
    signal closed()

    function open(ed, index, name) {
        editor = ed
        player = index
        title = name || ("Player " + (index + 1))
        visible = true
        forceActiveFocus()
        look.open(ed, index)
        if (look.groups.indexOf(group) < 0)
            group = look.groups.indexOf("Face") >= 0 ? "Face" : (look.groups[0] || "")
    }
    function back() {
        look.close()
        visible = false
        closed()
    }
    // An edit: the fields are read again afterwards, so the list keeps its scroll position across it.
    function commit(key, value) {
        const y = fieldList.contentY
        look.setField(key, value)
        Qt.callLater(() => fieldList.contentY = Math.min(y, Math.max(0, fieldList.contentHeight - fieldList.height)))
    }
    function resetView() {
        orbitOrigin.position = look.center
        orbitOrigin.eulerRotation = Qt.vector3d(-5, 20, 0)
        camera.position = Qt.vector3d(0, 0, look.radius * 2.4)
    }
    function faceView() {
        orbitOrigin.position = look.headCenter
        orbitOrigin.eulerRotation = Qt.vector3d(-3, 0, 0)
        camera.position = Qt.vector3d(0, 0, look.headRadius * 3.4)
    }

    Keys.onEscapePressed: back()

    // Nothing under the page reacts while it's open.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    Rl26AppearanceManager {
        id: look
        onViewReset: root.resetView()
    }

    component HeaderButton: AbstractButton {
        id: hb
        property string tip: ""
        BtToolTip { text: hb.tip }
        implicitHeight: 38
        implicitWidth: label.implicitWidth + 24
        hoverEnabled: true
        opacity: enabled ? 1 : 0.4
        readonly property bool lit: (hovered && enabled) || checked
        background: Rectangle {
            radius: 4
            color: hb.lit ? BtDarkStyle.bgMedium : "transparent"
            border.color: hb.lit ? BtDarkStyle.accentColor : "transparent"
        }
        contentItem: Text {
            id: label
            text: hb.text
            color: hb.lit ? BtDarkStyle.accentColor : BtDarkStyle.textMain
            font.bold: true
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component GroupButton: AbstractButton {
        id: gb
        property bool accent: false
        implicitHeight: 28
        implicitWidth: gbText.implicitWidth + 18
        hoverEnabled: true
        background: Rectangle {
            radius: 4
            color: gb.down ? BtDarkStyle.bgDarkest : gb.hovered ? BtDarkStyle.bgHover : BtDarkStyle.bgMedium
            border.color: gb.hovered || gb.accent ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
        }
        contentItem: Text {
            id: gbText
            text: gb.text
            color: gb.hovered || gb.accent ? BtDarkStyle.accentColor : BtDarkStyle.textMain
            font.pixelSize: 12
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: BtDarkStyle.bgDark
            border.color: BtDarkStyle.borderDark
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10
                HeaderButton {
                    text: "◀ Back"
                    tip: "Back to the editor (Esc). Your changes are in the editor: save there."
                    onClicked: root.back()
                }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 28; color: BtDarkStyle.borderDark }
                Text {
                    text: root.title
                    color: BtDarkStyle.white
                    font.pixelSize: 17
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.maximumWidth: 360
                }
                Item { Layout.fillWidth: true }
                HeaderButton { text: "Whole Body"; tip: "Show the whole player"; enabled: look.hasModel; onClicked: root.resetView() }
                HeaderButton { text: "Face"; tip: "Close up on the face"; enabled: look.hasModel; onClicked: root.faceView() }
                HeaderButton {
                    id: texturesButton
                    text: checked ? "Textures: On" : "Textures: Off"
                    tip: "Skin, eyes, hair and beard textures, built like the game's character creator does"
                    checkable: true
                    checked: true
                    enabled: look.hasModel
                }
                HeaderButton {
                    id: wireButton
                    text: checked ? "Wireframe: On" : "Wireframe: Off"
                    checkable: true
                    enabled: look.hasModel
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- 3D view
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 300

                View3D {
                    id: view3d
                    anchors.fill: parent
                    anchors.bottomMargin: 26
                    visible: look.hasModel
                    // Lit like the game's character screens: a studio light probe (soft boxes, what the skin and hair
                    // reflect), filmic tone mapping, and a key / rim light.
                    environment: SceneEnvironment {
                        backgroundMode: SceneEnvironment.Color
                        clearColor: BtDarkStyle.bgDarkest
                        antialiasingMode: SceneEnvironment.MSAA
                        antialiasingQuality: SceneEnvironment.High
                        specularAAEnabled: true
                        lightProbe: Texture {
                            source: look.environmentUrl
                            mappingMode: Texture.LightProbe
                        }
                        probeExposure: 0.9
                        debugSettings: DebugSettings { wireframeEnabled: wireButton.checked }
                    }
                    Node {
                        id: orbitOrigin
                        position: look.center
                        PerspectiveCamera {
                            id: camera
                            clipNear: Math.max(0.001, Math.abs(camera.z) * 0.01)
                            clipFar: Math.abs(camera.z) + look.radius * 6
                            DirectionalLight { brightness: 0.9; eulerRotation: Qt.vector3d(-15, -20, 0); color: "#fff4e8" }
                        }
                    }
                    DirectionalLight { eulerRotation: Qt.vector3d(-70, 0, 0); brightness: 0.2 }
                    DirectionalLight { eulerRotation: Qt.vector3d(-15, 160, 0); brightness: 0.45; color: "#e6eeff" }   // Rim.
                    // One model per material of each part; its colour texture once the textures are built.
                    Repeater3D {
                        model: look.parts
                        delegate: Model {
                            id: partModel
                            required property var modelData
                            readonly property var tex: look.textures[modelData.key]
                            readonly property bool textured: texturesButton.checked && tex !== undefined
                            geometry: modelData.geometry
                            readonly property bool hasNormal: textured && tex.normal !== undefined && tex.normal !== ""
                            readonly property bool hasRough: textured && tex.rough !== undefined && tex.rough !== ""
                            readonly property bool strands: textured && tex.alpha === 1
                            readonly property bool skin: modelData.style === "skin"
                            Texture {
                                id: partTexture
                                source: partModel.tex !== undefined ? partModel.tex.url : ""
                                generateMipmaps: true
                                mipFilter: Texture.Linear
                            }
                            Texture {
                                id: normalTexture
                                source: partModel.hasNormal ? partModel.tex.normal : ""
                                generateMipmaps: true
                                mipFilter: Texture.Linear
                            }
                            Texture {
                                id: roughTexture
                                source: partModel.hasRough ? partModel.tex.rough : ""
                                generateMipmaps: true
                                mipFilter: Texture.Linear
                            }
                            materials: PrincipledMaterial {
                                baseColor: partModel.textured ? "white" : partModel.modelData.colour
                                baseColorMap: partModel.textured ? partTexture : null
                                // Strands (hair, beard, eyelashes) blended, with a depth pre-pass for their solid
                                // middles so they still sort against each other; caps blended over the skin.
                                alphaMode: !partModel.textured ? PrincipledMaterial.Opaque
                                           : partModel.tex.alpha === 0 ? PrincipledMaterial.Opaque : PrincipledMaterial.Blend
                                depthDrawMode: partModel.strands ? Material.OpaquePrePassDepthDraw : Material.OpaqueOnlyDepthDraw
                                normalMap: partModel.hasNormal ? normalTexture : null
                                normalStrength: 1.0
                                // The game's "mat" maps: roughness (R) and occlusion (G); else the material's own.
                                roughnessMap: partModel.hasRough ? roughTexture : null
                                roughnessChannel: Material.R
                                // With a map this scales it: the eyes stay wet, the skin satin.
                                roughness: partModel.hasRough ? (partModel.modelData.style === "eyes" ? 0.35
                                                                 : partModel.modelData.slot === "body" ? 0.85
                                                                 : partModel.modelData.style === "boots" ? 0.7 : 1.0)
                                                              : partModel.modelData.roughness
                                // Occlusion only where the "mat" map has it (skin; the eyes' and hair's B is empty).
                                occlusionMap: partModel.hasRough && partModel.skin ? roughTexture : null
                                occlusionChannel: Material.G
                                occlusionAmount: 1.0
                                specularAmount: partModel.skin ? 0.3 : partModel.strands ? 0.35
                                                : partModel.modelData.style === "boots" ? 0.6 : 0.5
                                vertexColorsEnabled: partModel.modelData.vertexColours === true
                                metalness: 0
                                cullMode: PrincipledMaterial.NoCulling
                                lighting: PrincipledMaterial.FragmentLighting
                            }
                        }
                    }
                }

                OrbitCameraController {
                    anchors.fill: view3d
                    origin: orbitOrigin
                    camera: camera
                    enabled: look.hasModel
                    xSpeed: 0.3
                    ySpeed: 0.3
                    panEnabled: false
                }
                // Middle drag moves the player; the wheel zooms.
                MouseArea {
                    anchors.fill: view3d
                    enabled: look.hasModel
                    acceptedButtons: Qt.MiddleButton
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                    property real lastX: 0
                    property real lastY: 0
                    onPressed: (mouse) => { lastX = mouse.x; lastY = mouse.y }
                    onWheel: (wheel) => {
                        const steps = wheel.angleDelta.y / 120
                        if (steps !== 0)
                            camera.z = Math.min(Math.max(camera.z * Math.pow(0.87, steps), 0.05), look.radius * 12)
                    }
                    onPositionChanged: (mouse) => {
                        const perPixel = 2 * camera.position.length() * Math.tan(camera.fieldOfView * Math.PI / 360) / Math.max(1, view3d.height)
                        orbitOrigin.position = orbitOrigin.position.minus(camera.right.times((mouse.x - lastX) * perPixel))
                                                                   .plus(camera.up.times((mouse.y - lastY) * perPixel))
                        lastX = mouse.x
                        lastY = mouse.y
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 10
                    visible: look.hasModel
                    text: "Left drag: orbit   |   Middle drag: move   |   Wheel: zoom"
                    color: BtDarkStyle.textDim
                    font.pixelSize: 11
                }
                RowLayout {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    visible: look.loading || look.texturing
                    Text {
                        text: look.loading ? "Loading models..." : "Building textures..."
                        color: BtDarkStyle.textDim
                        font.pixelSize: 11
                    }
                    BusyIndicator {
                        running: parent.visible
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: !look.hasModel
                    width: parent.width - 80
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font.pixelSize: 17
                    font.bold: true
                    color: look.statusOk ? BtDarkStyle.white : "#ef5350"
                    text: look.loading ? "Loading the player's models..." : look.statusText
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 26
                    color: BtDarkStyle.bgMedium
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        font.family: "Consolas"
                        font.pixelSize: 11
                        color: look.statusOk ? BtDarkStyle.textMain : "#ef5350"
                        text: look.statusOk ? look.modelsInfo : look.statusText
                    }
                }
            }

            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: BtDarkStyle.borderDark }

            // ---- Fields
            // A fixed width: a layout takes on its children's fillWidth, which squeezed the 3D view to nothing.
            ColumnLayout {
                Layout.fillWidth: false
                Layout.preferredWidth: Math.min(460, root.width * 0.4)
                Layout.maximumWidth: Math.min(460, root.width * 0.4)
                Layout.fillHeight: true
                spacing: 0

                Flow {
                    Layout.fillWidth: true
                    Layout.margins: 8
                    spacing: 4
                    Repeater {
                        model: look.groups
                        GroupButton {
                            required property var modelData
                            text: modelData
                            accent: root.group === modelData
                            onClicked: root.group = modelData
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: BtDarkStyle.borderDark }

                ListView {
                    id: fieldList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    // Model-changing fields first (the parts), then the rest.
                    model: {
                        const list = look.fields.filter((f) => f.group === root.group)
                        return list.filter((f) => f.model).concat(list.filter((f) => !f.model))
                    }

                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        width: fieldList.width - 12
                        x: 6
                        height: 46
                        radius: 3
                        color: modelData.model ? Qt.rgba(1, 1, 1, 0.03) : "transparent"

                        Text {
                            id: rowLabel
                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            width: 150
                            text: row.modelData.label
                            color: row.modelData.model ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            BtToolTip { text: row.modelData.name || row.modelData.label }
                        }
                        Loader {
                            anchors.left: rowLabel.right
                            anchors.leftMargin: 8
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            property var field: row.modelData
                            sourceComponent: field.control === "slider" ? sliderEdit
                                           : field.control === "choice" ? choiceEdit
                                           : field.control === "bool" ? boolEdit
                                           : field.control === "colour" ? colourEdit
                                           : field.control === "text" ? textEdit : numberEdit
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.margins: 8
                    wrapMode: Text.WordWrap
                    color: BtDarkStyle.textDim
                    font.pixelSize: 11
                    text: "Highlighted fields pick the models. Face sliders only show on created players' heads (Head: Created player); real players' scanned heads keep their own face. Edits go to the editor: save there."
                }
            }
        }
    }

    // ---- Field editors (each Loader gives `field`)
    Component {
        id: sliderEdit
        RowLayout {
            spacing: 6
            Slider {
                id: slider
                Layout.fillWidth: true
                from: 0
                to: field.max || 255
                stepSize: 1
                value: field.value
                onMoved: look.previewField(field.name, value)
                onPressedChanged: if (!pressed && value !== field.value) root.commit(field.key, Math.round(value))
            }
            Text {
                Layout.preferredWidth: 34
                horizontalAlignment: Text.AlignRight
                text: Math.round(slider.value)
                color: BtDarkStyle.textDim
                font.pixelSize: 11
            }
        }
    }
    Component {
        id: choiceEdit
        BtComboBox {
            id: choiceBox
            model: field.choices
            textRole: "text"
            valueRole: "value"
            // Hair / beard / eyebrow colours are the game's swatches ("#rrggbb"): shown as colours.
            readonly property bool swatches: field.choices.length > 0 && /^#[0-9a-f]{6}$/.test(field.choices[field.choices.length - 1].text)
            function isColour(t) { return /^#[0-9a-f]{6}$/.test(t) }
            contentItem: Row {
                leftPadding: 10
                spacing: 8
                Rectangle {
                    visible: choiceBox.swatches && choiceBox.isColour(choiceBox.displayText)
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40; height: 18; radius: 3
                    color: visible ? choiceBox.displayText : "transparent"
                    border.color: BtDarkStyle.borderDark
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: choiceBox.displayText
                    color: BtDarkStyle.textMain
                    font: choiceBox.font
                    elide: Text.ElideRight
                    width: choiceBox.width - 90
                }
            }
            delegate: ItemDelegate {
                required property int index
                required property var modelData
                width: choiceBox.popup.width - 12
                highlighted: choiceBox.highlightedIndex === index
                contentItem: Row {
                    spacing: 8
                    Rectangle {
                        visible: choiceBox.isColour(modelData.text)
                        anchors.verticalCenter: parent.verticalCenter
                        width: 40; height: 18; radius: 3
                        color: visible ? modelData.text : "transparent"
                        border.color: BtDarkStyle.borderDark
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.text
                        color: highlighted ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                        font: choiceBox.font
                    }
                }
            }
            currentIndex: {
                for (let i = 0; i < field.choices.length; ++i)
                    if (field.choices[i].value === field.value)
                        return i
                return -1
            }
            onActivated: (index) => root.commit(field.key, field.choices[index].value)
        }
    }
    Component {
        id: boolEdit
        CheckBox {
            checked: field.value === true
            onToggled: root.commit(field.key, checked)
        }
    }
    Component {
        id: colourEdit
        RowLayout {
            spacing: 6
            Rectangle {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 22
                radius: 3
                color: colourText.acceptableInput ? colourText.text : field.value
                border.color: BtDarkStyle.borderDark
            }
            TextField {
                id: colourText
                Layout.fillWidth: true
                text: field.value
                validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{6}/ }
                onEditingFinished: if (text !== field.value) root.commit(field.key, text.startsWith("#") ? text : "#" + text)
            }
        }
    }
    Component {
        id: numberEdit
        SpinBox {
            from: 0
            to: Math.min(field.max === undefined ? 65535 : field.max, 2147483647)
            value: field.value
            editable: true
            onValueModified: root.commit(field.key, value)
        }
    }
    Component {
        id: textEdit
        TextField {
            text: field.value
            onEditingFinished: if (text !== field.value) root.commit(field.key, text)
        }
    }
}
