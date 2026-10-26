import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import com.wouldymods.blobset 1.0

Item {
    id: ddsViewRoot
    anchors.fill: parent

    readonly property string iconPath: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/"

    // Tracks the active numeric path hash identifier string context
    property string activeHashString: ""

    // Owns the view state (flip / alpha) and the image://dds_provider source of the texture
    DdsViewerBridge {
        id: bridge
    }

    Connections {
        target: appOrchestrator

        // Catches full-screen workspace loading event requests dispatched from the backend
        function onOpenDdsViewerByHash(hexHash, displayName) {
            ddsViewRoot.activeHashString = hexHash;
            bridge.loadHash(hexHash, displayName);
        }
    }

    // Toolbar button shared by Back / Flip / Alpha / Extract
    component ToolbarButton: AbstractButton {
        id: tb
        property string tip: ""   // Tooltip text.
        BtToolTip { text: tb.tip }
        property string label: ""
        property url iconSource: ""

        Layout.preferredHeight: 34
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        leftPadding: 10
        rightPadding: 10
        hoverEnabled: true

        readonly property bool lit: (hovered && enabled) || checked

        background: Rectangle {
            color: tb.lit ? BtDarkStyle.bgMedium : "transparent"
            border.color: tb.lit ? BtDarkStyle.accentColor : "transparent"
            border.width: 1
            radius: 4
        }
        contentItem: RowLayout {
            spacing: 6
            opacity: tb.enabled ? 1.0 : 0.4

            Image {
                visible: tb.iconSource.toString() !== ""
                source: tb.iconSource
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
            Text {
                text: tb.label
                color: tb.lit ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                font.bold: true
                font.pixelSize: 14
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Custom Visual Toolbar header matching MainForm styling frameworks
        ToolBar {
            Layout.fillWidth: true
            Material.background: BtDarkStyle.bgDark

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 4

                ToolbarButton {
                    tip: "Return to the main workspace"
                    label: "◀ Back"
                    onClicked: appOrchestrator.viewChanged("qrc:/qt/qml/com/wouldymods/blobset/qml/views/MainForm.qml")
                }

                ToolSeparator {}

                // Vertically flips the image canvas
                ToolbarButton {
                    tip: "Flip the image vertically (also used when extracting as PNG)"
                    label: "Flip Image"
                    iconSource: ddsViewRoot.iconPath + "flip_image_32.png"
                    enabled: bridge.hasImage
                    onClicked: bridge.flipImage()
                }

                // Alpha channel visibility: on = as stored in the texture, off = fully opaque
                ToolbarButton {
                    tip: "Show or hide the alpha channel (transparency)"
                    label: bridge.alphaEnabled ? "Alpha: On" : "Alpha: Off"
                    iconSource: ddsViewRoot.iconPath + "alpha_32.png"
                    enabled: bridge.hasImage
                    checkable: true
                    checked: bridge.alphaEnabled
                    onToggled: bridge.toggleAlpha(checked)
                }

                ToolSeparator {}

                // Extract as DDS (original texture) or PNG (what is shown)
                ToolbarButton {
                    tip: "Extract the texture as DDS or PNG"
                    id: btnExtract
                    label: "Extract ▾"
                    iconSource: ddsViewRoot.iconPath + "extract_32.png"
                    enabled: bridge.hasImage
                    onClicked: extractMenu.popup(btnExtract, 0, btnExtract.height)
                }

                Item { Layout.fillWidth: true } // Responsive layout spacer anchor
            }
        }

        // Texture Rendering Canvas Layer
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: BtDarkStyle.bgDarkest

            Image {
                anchors.fill: parent
                fillMode: Image.Tile
                source: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/grid_dark.bmp"
            }

            // Texture canvas: the mouse wheel zooms around the cursor, dragging pans once the texture is bigger than the view.
            Flickable {
                id: ddsFlick
                anchors.fill: parent
                anchors.bottomMargin: 28   // Room for the info strip.
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: Math.max(width, ddsImage.width + 40)
                contentHeight: Math.max(height, ddsImage.height + 40)
                interactive: contentWidth > width || contentHeight > height

                // 1.0 = fitted to the view (small textures are never blown up past their real size at 1.0).
                property real zoom: 1.0
                readonly property real minZoom: 0.1
                readonly property real maxZoom: 32.0
                readonly property real fitScale: ddsImage.sourceSize.width > 0 && ddsImage.sourceSize.height > 0
                    ? Math.min(1.0, Math.max(1, width - 40) / ddsImage.sourceSize.width,
                               Math.max(1, height - 40) / ddsImage.sourceSize.height)
                    : 1.0
                readonly property real displayScale: fitScale * zoom   // 1.0 = one texel per screen pixel.

                // A different texture starts fitted again (flip / alpha keep the zoom).
                property string resetKey: bridge.fileName + "|" + ddsViewRoot.activeHashString
                onResetKeyChanged: resetZoom()

                // Free offset of the texture (middle mouse drag), on top of the scrolling.
                property real panX: 0
                property real panY: 0

                // Moves the texture by (dx, dy) pixels; at least 40 px of it stays in view.
                function panBy(dx, dy) {
                    var limitX = (width + ddsImage.width) / 2 - 40
                    var limitY = (height + ddsImage.height) / 2 - 40
                    panX = Math.max(-limitX, Math.min(limitX, panX + dx))
                    panY = Math.max(-limitY, Math.min(limitY, panY + dy))
                }

                function resetZoom() {
                    zoom = 1.0
                    panX = 0
                    panY = 0
                    contentX = 0
                    contentY = 0
                }

                // Zooms by `factor` and keeps the texel under (px, py) (view coordinates) under the cursor.
                function zoomAt(factor, px, py) {
                    var newZoom = Math.max(minZoom, Math.min(maxZoom, zoom * factor))
                    if (newZoom === zoom || ddsImage.width <= 0 || ddsImage.height <= 0)
                        return
                    var u = (contentX + px - ddsImage.x) / ddsImage.width
                    var v = (contentY + py - ddsImage.y) / ddsImage.height
                    zoom = newZoom
                    contentX = Math.max(0, Math.min(contentWidth - width, ddsImage.x + u * ddsImage.width - px))
                    contentY = Math.max(0, Math.min(contentHeight - height, ddsImage.y + v * ddsImage.height - py))
                }

                Image {
                    id: ddsImage
                    x: (ddsFlick.contentWidth - width) / 2 + ddsFlick.panX
                    y: (ddsFlick.contentHeight - height) / 2 + ddsFlick.panY
                    width: sourceSize.width * ddsFlick.displayScale
                    height: sourceSize.height * ddsFlick.displayScale
                    fillMode: Image.PreserveAspectFit
                    source: bridge.imageSource
                    smooth: ddsFlick.displayScale < 2   // Crisp texels when zoomed in.
                    asynchronous: true
                    cache: false

                    BusyIndicator {
                        anchors.centerIn: parent
                        running: ddsImage.status === Image.Loading
                        visible: running
                    }
                }
            }

            // Wheel zoom and middle button panning; left dragging still goes to the Flickable underneath.
            MouseArea {
                anchors.fill: ddsFlick
                acceptedButtons: Qt.MiddleButton
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                property real lastX: 0
                property real lastY: 0
                onPressed: (mouse) => { lastX = mouse.x; lastY = mouse.y }
                onPositionChanged: (mouse) => {
                    ddsFlick.panBy(mouse.x - lastX, mouse.y - lastY)
                    lastX = mouse.x
                    lastY = mouse.y
                }
                onWheel: (wheel) => {
                    if (wheel.angleDelta.y === 0) {
                        wheel.accepted = false
                        return
                    }
                    ddsFlick.zoomAt(Math.pow(1.0015, wheel.angleDelta.y), wheel.x, wheel.y)   // ~20% per notch.
                }
            }

            // Zoom level (top right). Click to fit the texture again.
            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 8
                width: zoomLabel.implicitWidth + 16
                height: 24
                radius: 4
                color: zoomBadgeArea.containsMouse ? BtDarkStyle.bgMedium : BtDarkStyle.bgDark
                border.width: 1
                border.color: zoomBadgeArea.containsMouse ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
                opacity: 0.9
                visible: ddsImage.status === Image.Ready

                Text {
                    id: zoomLabel
                    anchors.centerIn: parent
                    text: Math.round(ddsFlick.displayScale * 100) + "%" + (ddsFlick.zoom !== 1.0 ? "  ⟲" : "")
                    color: zoomBadgeArea.containsMouse ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                    font.family: "Consolas"
                    font.pixelSize: 12
                }

                MouseArea {
                    id: zoomBadgeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: ddsFlick.resetZoom()
                }

                ToolTip.visible: zoomBadgeArea.containsMouse
                ToolTip.text: "Scroll to zoom, drag or hold the middle mouse button to move. Click to fit."
            }

            Text {
                anchors.centerIn: parent
                visible: ddsImage.status === Image.Error || (!bridge.hasImage && ddsImage.status !== Image.Loading)
                horizontalAlignment: Text.AlignHCenter
                color: bridge.hasImage ? Material.color(Material.Red) : BtDarkStyle.white
                font.pixelSize: 18
                font.bold: true
                wrapMode: Text.WordWrap
                width: parent.width - 80
                text: bridge.hasImage ? "Failed to load the preview image." : "Waiting for a texture to load..."
            }

            // Info strip of the texture (same as the TXPK viewer)
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 28
                color: BtDarkStyle.bgMedium
                visible: infoText.text !== ""

                Text {
                    id: infoText
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideMiddle
                    color: BtDarkStyle.textMain
                    font.family: "Consolas"
                    font.pixelSize: 12
                    text: bridge.hasImage ? bridge.infoLine : ""
                }
            }
        }

        // Output Data Information Status Bar Footer bar
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
                height: 30

                // Same layout as the TXPK viewer: last extraction result, otherwise the texture details.
                Text {
                    id: statusOutput
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font.pointSize: 9
                    color: bridge.statusOk ? BtDarkStyle.textDim : Material.color(Material.Red)
                    text: bridge.statusText !== "" ? "Status: " + bridge.statusText
                        : bridge.hasImage ? "Status: " + bridge.infoLine
                                            + "  |  0x" + ddsViewRoot.activeHashString.toUpperCase()
                                            + (bridge.flipped ? "  |  Flipped" : "")
                                            + (bridge.alphaEnabled ? "" : "  |  Alpha off")
                          : "Status: Ready | Blobset Tools DDS Viewer"
                }

                Text {
                    text: "v" + Qt.application.version
                    color: BtDarkStyle.accentColor
                    font.pointSize: 9
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }
    }

    Menu {
        id: extractMenu

        MenuItem {
            text: "As DDS..."
            icon.source: ddsViewRoot.iconPath + "dds_32.png"
            icon.color: "transparent"   // Full color icon (Material tints icons by default).
            onTriggered: extractDialog.start("dds")
        }
        MenuItem {
            text: "As PNG..."
            icon.source: ddsViewRoot.iconPath + "png_32.png"
            icon.color: "transparent"   // Full color icon (Material tints icons by default).
            onTriggered: extractDialog.start("png")
        }
    }

    FolderDialog {
        id: extractDialog
        property string format: "dds"

        function start(fmt) {
            format = fmt
            title = "Extract Texture as " + fmt.toUpperCase() + " to Directory"
            open()
        }

        onAccepted: bridge.extractTexture(selectedFolder.toString(), format)
    }
}
