import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import com.wouldymods.blobset 1.0

// Lua script editor (M3MPViewer.qml): LuaJIT scripts are decompiled into it (M3mpToolManager::openLuaAt), and
// Save compiles the script back to bytecode, puts it into the M3MP and rebuilds the M3MP into the mods folder
// (M3mpToolManager::saveLua). Covers the viewer while it's open.

Rectangle {
    id: editor
    color: BtDarkStyle.bgDarkest
    visible: false

    required property var manager      // The viewer's M3mpToolManager.

    property int entry: -1
    property string path: ""
    property bool writable: false
    property string note: ""
    property bool loading: false
    property string loadError: ""
    property bool dirty: false
    property bool settingText: false   // Text set by load(), not typed.
    property int errorLine: 0
    property string errorText: ""
    property string message: ""
    property bool messageOk: true
    property bool savePending: false   // saveLua() ran, waiting for the M3MP rebuild.
    property var afterClose: null      // Called once the editor has closed (the viewer's Back).

    readonly property string fileName: path.substring(path.lastIndexOf("/") + 1)
    // Every line is as tall (one font, no wrapping): the line numbers and highlights use this.
    readonly property real lineHeight: codeArea.lineCount > 0 && codeArea.contentHeight > 0
                                       ? codeArea.contentHeight / codeArea.lineCount
                                       : fontMetrics.lineSpacing

    // --- Opening / closing ---------------------------------------------------------------------------------
    function request(fileIndex, fileName) {
        entry = -1
        path = fileName
        loading = true
        loadError = ""
        errorLine = 0
        errorText = ""
        message = ""
        visible = true
        manager.openLuaAt(fileIndex)
    }

    function load(newEntry, newPath, source, canWrite, why) {
        entry = newEntry
        path = newPath
        writable = canWrite
        note = why
        loading = false
        loadError = ""
        settingText = true
        codeArea.text = source
        settingText = false
        dirty = false
        errorLine = 0
        errorText = ""
        codeArea.cursorPosition = 0
        codeFlick.contentY = 0
        codeFlick.contentX = 0
        codeArea.forceActiveFocus()
    }

    function close(onClosed) {
        afterClose = onClosed || null
        if (dirty && writable) {
            closeDialog.open()
            return
        }
        finishClose()
    }

    function finishClose() {
        visible = false
        dirty = false
        savePending = false
        settingText = true
        codeArea.text = ""
        settingText = false
        const next = afterClose
        afterClose = null
        if (next)
            next()
    }

    // --- Save / check --------------------------------------------------------------------------------------
    function save() {
        if (!writable || loading || manager.busy)
            return
        errorLine = 0
        errorText = ""
        if (manager.saveLua(entry, codeArea.text, true)) {
            dirty = false
            savePending = true
            message = "Compiled - rebuilding the M3MP into the mods folder..."
            messageOk = true
        }
    }

    function check() {
        const result = manager.checkLua(entry, codeArea.text)
        if (result.ok) {
            errorLine = 0
            errorText = ""
            message = "No errors: the script compiles."
            messageOk = true
        } else {
            showError(result.line, result.error)
        }
    }

    function showError(line, text) {
        errorLine = line
        errorText = text
        message = ""
        if (line > 0)
            goToLine(line)
    }

    // --- Navigation ----------------------------------------------------------------------------------------
    function lineStart(line) {
        const text = codeArea.text
        let at = 0
        for (let i = 1; i < line && at >= 0; ++i) {
            at = text.indexOf("\n", at)
            if (at >= 0)
                ++at
        }
        return at < 0 ? text.length : at
    }

    // Scrolls a line (y, h in the code's coordinates) into view, a third of the way down, when it's off screen.
    function ensureVisible(y, h) {
        if (y >= codeFlick.contentY && y + h <= codeFlick.contentY + codeFlick.height)
            return
        codeFlick.contentY = Math.max(0, Math.min(y - codeFlick.height / 3, codeFlick.contentHeight - codeFlick.height))
    }

    function goToLine(line) {
        if (line < 1)
            return
        codeArea.cursorPosition = lineStart(line)
        ensureVisible(codeArea.topPadding + (line - 1) * lineHeight, lineHeight)
        codeArea.forceActiveFocus()
    }

    function find(forward) {
        const query = findField.text
        if (query === "")
            return
        const text = codeArea.text.toLowerCase()
        const q = query.toLowerCase()
        let at
        if (forward) {
            at = text.indexOf(q, codeArea.selectionEnd)
            if (at < 0)
                at = text.indexOf(q)          // Wrap around.
        } else {
            at = text.lastIndexOf(q, Math.max(0, codeArea.selectionStart - 1))
            if (at < 0)
                at = text.lastIndexOf(q)
        }
        if (at < 0) {
            message = "\"" + query + "\" isn't in the script."
            messageOk = false
            return
        }
        message = ""
        codeArea.select(at, at + q.length)
        const rect = codeArea.positionToRectangle(at)
        ensureVisible(rect.y, rect.height)
        if (rect.x < codeFlick.contentX || rect.x > codeFlick.contentX + codeFlick.width - 40)
            codeFlick.contentX = Math.max(0, rect.x - codeFlick.width / 3)
    }

    Connections {
        target: editor.manager
        function onLuaOpened(entry, path, source, writable, note) {
            if (editor.visible && editor.loading)
                editor.load(entry, path, source, writable, note)
        }
        function onLuaOpenFailed(path, error) {
            if (editor.visible && editor.loading) {
                editor.loading = false
                editor.loadError = error
            }
        }
        function onLuaCompileFailed(line, error) {
            if (editor.visible)
                editor.showError(line, error)
        }
        function onChangesSaved(target) {
            if (editor.visible && editor.savePending) {
                editor.savePending = false
                editor.message = "Saved to " + target + " - click Modify in the workspace to put it into the game."
                editor.messageOk = true
            }
        }
        function onProcessComplete(success, message) {
            if (editor.visible && editor.savePending && !success) {
                editor.savePending = false
                editor.dirty = true
                editor.message = message
                editor.messageOk = false
            }
        }
    }

    Shortcut { sequences: [StandardKey.Save]; enabled: editor.visible; onActivated: editor.save() }
    Shortcut { sequences: [StandardKey.Find]; enabled: editor.visible; onActivated: { findField.forceActiveFocus(); findField.selectAll() } }
    Shortcut { sequence: "Ctrl+G"; enabled: editor.visible; onActivated: { lineField.forceActiveFocus(); lineField.selectAll() } }
    Shortcut { sequence: "F3"; enabled: editor.visible; onActivated: editor.find(true) }
    Shortcut { sequence: "Shift+F3"; enabled: editor.visible; onActivated: editor.find(false) }
    Shortcut { sequence: "F7"; enabled: editor.visible && editor.writable; onActivated: editor.check() }

    FontMetrics {
        id: fontMetrics
        font: codeArea.font
    }

    // A toolbar button (the viewer's HeaderButton look).
    component EditorButton: AbstractButton {
        id: button
        property string tip: ""
        BtToolTip { text: button.tip }
        property url iconSource: ""
        implicitHeight: 36
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        leftPadding: 10
        rightPadding: 10
        hoverEnabled: true
        opacity: enabled ? 1.0 : 0.4
        readonly property bool lit: (hovered && enabled) || checked
        background: Rectangle {
            color: button.lit ? BtDarkStyle.bgMedium : "transparent"
            border.color: button.lit ? BtDarkStyle.accentColor : "transparent"
            border.width: 1
            radius: 4
        }
        contentItem: RowLayout {
            spacing: 6
            Image {
                visible: button.iconSource.toString() !== ""
                source: button.iconSource
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                fillMode: Image.PreserveAspectFit
            }
            Text {
                text: button.text
                color: button.lit ? BtDarkStyle.accentColor : BtDarkStyle.textMain
                font.bold: true
                font.pixelSize: 13
            }
        }
    }

    component SmallField: TextField {
        Layout.preferredHeight: 30
        font.pixelSize: 12
        color: BtDarkStyle.textMain
        placeholderTextColor: BtDarkStyle.textDim
        leftPadding: 8
        topPadding: 4
        bottomPadding: 4
        background: Rectangle {
            color: BtDarkStyle.bgMedium
            radius: 4
            border.width: 1
            border.color: parent.activeFocus ? BtDarkStyle.accentColor : BtDarkStyle.borderDark
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Toolbar --------------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: BtDarkStyle.bgDark
            border.color: BtDarkStyle.borderDark
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                EditorButton {
                    text: "◀ Close"
                    tip: "Back to the M3MP's files (Esc)"
                    onClicked: editor.close()
                }
                Rectangle { width: 1; Layout.preferredHeight: 26; color: BtDarkStyle.borderDark }
                EditorButton {
                    text: editor.dirty ? "Save to Mods •" : "Save to Mods"
                    iconSource: "qrc:/qt/qml/com/wouldymods/blobset/assets/ext/save_32.png"
                    tip: "Compile the script, put it into the M3MP and rebuild the M3MP into the mods folder (Ctrl+S)"
                    enabled: editor.writable && !editor.loading && editor.loadError === "" && !editor.manager.busy
                    checked: editor.dirty
                    onClicked: editor.save()
                }
                EditorButton {
                    text: "Check"
                    tip: "Compile the script without saving it, to find mistakes (F7)"
                    enabled: editor.writable && !editor.loading && editor.loadError === ""
                    onClicked: editor.check()
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: editor.fileName
                    color: BtDarkStyle.accentColor
                    font.family: "Consolas"
                    font.pixelSize: 13
                    font.bold: true
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 320
                    BtToolTip { text: editor.path; visible: pathHover.hovered }
                    HoverHandler { id: pathHover }
                }
                Rectangle {
                    visible: !editor.writable && !editor.loading && editor.loadError === ""
                    implicitWidth: roLabel.implicitWidth + 12
                    implicitHeight: 18
                    radius: 3
                    color: BtDarkStyle.bgMedium
                    border.color: BtDarkStyle.borderDark
                    Text { id: roLabel; anchors.centerIn: parent; text: "READ ONLY"; color: BtDarkStyle.textDim; font.pixelSize: 9; font.bold: true }
                }

                Rectangle { width: 1; Layout.preferredHeight: 26; color: BtDarkStyle.borderDark }

                SmallField {
                    id: findField
                    Layout.preferredWidth: 200
                    placeholderText: "Find (Ctrl+F)"
                    onAccepted: editor.find(true)
                    Keys.onEscapePressed: codeArea.forceActiveFocus()
                }
                EditorButton { text: "▲"; tip: "Previous (Shift+F3)"; leftPadding: 6; rightPadding: 6; onClicked: editor.find(false) }
                EditorButton { text: "▼"; tip: "Next (F3)"; leftPadding: 6; rightPadding: 6; onClicked: editor.find(true) }
                SmallField {
                    id: lineField
                    Layout.preferredWidth: 90
                    placeholderText: "Line (Ctrl+G)"
                    validator: IntValidator { bottom: 1 }
                    onAccepted: editor.goToLine(parseInt(text))
                    Keys.onEscapePressed: codeArea.forceActiveFocus()
                }
            }
        }

        // ---- Notes / errors -------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? noteText.implicitHeight + 12 : 0
            visible: editor.note !== "" && !editor.loading
            color: "#2a2614"
            Text {
                id: noteText
                anchors.fill: parent
                anchors.margins: 6
                anchors.leftMargin: 14
                text: editor.note
                color: BtDarkStyle.accentColor
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                verticalAlignment: Text.AlignVCenter
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? errorRow.implicitHeight + 12 : 0
            visible: editor.errorText !== ""
            color: "#3a1717"
            RowLayout {
                id: errorRow
                anchors.fill: parent
                anchors.margins: 6
                anchors.leftMargin: 14
                Text {
                    text: "✖ " + editor.errorText
                    color: "#ff8a80"
                    font.pixelSize: 12
                    font.family: "Consolas"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                EditorButton {
                    visible: editor.errorLine > 0
                    implicitHeight: 24
                    text: "Go to line " + editor.errorLine
                    onClicked: editor.goToLine(editor.errorLine)
                }
            }
        }

        // ---- Code -----------------------------------------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Line numbers, scrolled with the code.
            Rectangle {
                id: gutter
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                width: Math.max(44, fontMetrics.advanceWidth(String(Math.max(1, codeArea.lineCount))) + 22)
                color: BtDarkStyle.bgDark
                clip: true

                ListView {
                    id: lineNumbers
                    anchors.fill: parent
                    interactive: false
                    contentY: codeFlick.contentY - codeArea.topPadding
                    model: editor.loading || editor.loadError !== "" ? 0 : codeArea.lineCount
                    reuseItems: true
                    delegate: Text {
                        required property int index
                        width: lineNumbers.width - 10
                        height: editor.lineHeight
                        text: index + 1
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        font: codeArea.font
                        color: index + 1 === editor.errorLine ? "#ff8a80"
                               : (index === currentLine.lineIndex ? BtDarkStyle.accentColor : BtDarkStyle.textDim)
                    }
                }
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: BtDarkStyle.borderDark }
            }

            Flickable {
                id: codeFlick
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.left: gutter.right
                anchors.right: parent.right
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: Math.max(width, codeArea.implicitWidth)
                contentHeight: Math.max(height, codeArea.implicitHeight)
                visible: !editor.loading && editor.loadError === ""

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                // Current line and error line.
                Rectangle {
                    id: currentLine
                    readonly property int lineIndex: editor.lineHeight > 0
                                                     ? Math.floor((codeArea.cursorRectangle.y - codeArea.topPadding + 1) / editor.lineHeight) : 0
                    x: 0
                    width: codeFlick.contentWidth
                    y: codeArea.topPadding + lineIndex * editor.lineHeight
                    height: editor.lineHeight
                    color: BtDarkStyle.bgDark
                    visible: codeArea.activeFocus
                }
                Rectangle {
                    x: 0
                    width: codeFlick.contentWidth
                    y: codeArea.topPadding + (editor.errorLine - 1) * editor.lineHeight
                    height: editor.lineHeight
                    color: "#3a1717"
                    visible: editor.errorLine > 0
                }

                TextArea.flickable: TextArea {
                    id: codeArea
                    readOnly: !editor.writable
                    selectByMouse: true
                    persistentSelection: true
                    wrapMode: TextEdit.NoWrap
                    textFormat: TextEdit.PlainText
                    font.family: "Consolas"
                    font.pixelSize: 13
                    color: BtDarkStyle.textMain
                    selectionColor: BtDarkStyle.accentDim
                    selectedTextColor: "#121214"
                    tabStopDistance: fontMetrics.advanceWidth(" ") * 4
                    leftPadding: 10
                    topPadding: 6
                    bottomPadding: 6
                    background: null

                    onTextChanged: if (!editor.settingText) editor.dirty = true
                    Keys.onEscapePressed: editor.close()
                }

                // Colours (off for very big scripts, where it would slow typing down).
                LuaHighlighter {
                    document: codeArea.textDocument
                    enabled: codeArea.length < 600000
                }
            }

            // Loading / failed.
            Column {
                anchors.centerIn: parent
                spacing: 10
                visible: editor.loading || editor.loadError !== ""
                width: Math.min(parent.width - 60, 640)
                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    running: editor.loading
                    visible: editor.loading
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: editor.loading ? "Decompiling " + editor.fileName + "..." : editor.loadError
                    color: editor.loading ? BtDarkStyle.textDim : "#ff8a80"
                    font.pixelSize: 13
                }
            }
        }

        // ---- Status ---------------------------------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            color: BtDarkStyle.bgDark
            border.color: BtDarkStyle.borderDark
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 16
                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.pixelSize: 11
                    text: editor.manager.busy ? editor.manager.statusText : editor.message
                    color: editor.manager.busy || editor.messageOk ? BtDarkStyle.textMain : "#ff8a80"
                }
                Text {
                    visible: !editor.loading && editor.loadError === ""
                    text: "Ln " + (currentLine.lineIndex + 1) + "   Lines " + codeArea.lineCount + (editor.dirty ? "   Modified" : "")
                    color: BtDarkStyle.textDim
                    font.pixelSize: 11
                    font.family: "Consolas"
                }
            }
        }
    }

    MessageDialog {
        id: closeDialog
        title: "Unsaved Script"
        text: "You changed " + editor.fileName + " but haven't saved it."
        informativeText: "Save compiles it and rebuilds the M3MP into the mods folder."
        buttons: MessageDialog.Save | MessageDialog.Discard | MessageDialog.Cancel
        onButtonClicked: function (button, role) {
            if (button === MessageDialog.Save) {
                editor.save()
                if (!editor.dirty)
                    editor.finishClose()
            } else if (button === MessageDialog.Discard) {
                editor.finishClose()
            } else {
                editor.afterClose = null
            }
        }
    }
}
