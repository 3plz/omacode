import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import QtQuick.Window
import Omawrite 1.0
import "EditorMutations.js" as EditorMutations

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    minimumWidth: 720
    minimumHeight: 520
    visible: true
    title: (backend.modified ? "* " : "") + backend.fileName + " - Omawrite"

    readonly property bool darkMode: backend.darkMode
    readonly property color pageColor: backend.themeBackground
    readonly property color textColor: backend.themeForeground
    readonly property color strongTextColor: backend.themeForeground
    readonly property color mutedColor: darkMode ? "#909191" : "#aeb1b5"
    readonly property color selectionFill: backend.themeSelection
    // The desktop's text size knob (GNOME's text-scaling-factor, which
    // `omarchy display text size` drives) anchored so its 12px default leaves
    // the app at the sizes it was designed around.
    readonly property real textScale: backend.textScale
    readonly property int editorFontPixelSize: scaledSize(20)
    readonly property int editorWidth: Math.min(
        Math.round(writerFontMetrics.averageCharacterWidth * 65),
        Math.max(360, width - Math.round(writerFontMetrics.averageCharacterWidth * 20)))
    readonly property int gutterWidth: Math.max(
        scaledSize(44),
        Math.round((String(editor.lineCount).length + 2) * writerFontMetrics.averageCharacterWidth))
    property bool closeConfirmed: false
    property bool searchOpen: false
    property bool searchUpdating: false
    property var searchMatches: []
    property int searchMatchIndex: -1
    property url pendingOpenUrl
    property string pendingAction: ""
    property bool replaceOpen: false
    property bool awaitingPendingSave: false
    property bool terminalOpen: false
    property real terminalHeight: scaledSize(220)

    Material.theme: darkMode ? Material.Dark : Material.Light
    Material.accent: backend.themeAccent
    color: pageColor

    onClosing: function(close) {
        if (closeConfirmed || !backend.modified)
            return;

        close.accepted = false;
        pendingAction = "close";
        if (!unsavedChangesDialog.opened)
            unsavedChangesDialog.open();
    }

    function requestOpen(url) {
        if (!backend.modified) {
            backend.open(url);
            return;
        }
        pendingOpenUrl = url;
        pendingAction = "open";
        unsavedChangesDialog.open();
    }

    function completePendingAction() {
        var action = pendingAction;
        pendingAction = "";
        if (action === "close") {
            closeConfirmed = true;
            close();
        } else if (action === "open") {
            backend.open(pendingOpenUrl);
        }
    }

    FontMetrics {
        id: writerFontMetrics
        font.family: "iA Writer Mono S"
        font.pixelSize: win.editorFontPixelSize
    }

    // Every hardcoded size in the interface is expressed at text scale 1.
    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * win.textScale));
    }

    function toggleFullScreen() {
        win.visibility = win.visibility === Window.FullScreen
            ? Window.Windowed
            : Window.FullScreen;
    }

    function toggleTerminal() {
        terminalOpen = !terminalOpen;
        if (terminalOpen) {
            terminalInput.forceActiveFocus();
        } else {
            editor.forceActiveFocus();
        }
    }

    function updateSearch() {
        var matches = [];
        var query = searchField.text;
        if (query.length > 0) {
            var haystack = editor.text.toLocaleLowerCase();
            var needle = query.toLocaleLowerCase();
            var position = 0;
            while ((position = haystack.indexOf(needle, position)) !== -1) {
                matches.push(position);
                position += Math.max(1, needle.length);
            }
        }
        searchMatches = matches;
        searchMatchIndex = matches.length > 0 ? 0 : -1;
        showSearchMatch();
    }

    function showSearchMatch() {
        var start = searchMatchIndex >= 0 ? searchMatches[searchMatchIndex] : -1;
        searchUpdating = true;
        backend.setSearchHighlight(searchField.text, start);
        if (start >= 0) {
            editor.select(start, start + searchField.text.length);
            editorFlick.ensureCursorVisible();
        }
        searchUpdating = false;
    }

    function moveSearch(direction) {
        if (searchMatches.length === 0)
            return;
        searchMatchIndex = (searchMatchIndex + direction + searchMatches.length)
                           % searchMatches.length;
        showSearchMatch();
    }

    function closeSearch() {
        searchOpen = false;
        searchUpdating = true;
        backend.setSearchHighlight("", -1);
        editor.deselect();
        searchUpdating = false;
        replaceOpen = false;
        editor.forceActiveFocus();
    }

    Shortcut {
        sequence: "Ctrl+S"
        context: Qt.ApplicationShortcut
        onActivated: backend.save()
    }

    Shortcut {
        sequence: "Ctrl+H"
        context: Qt.ApplicationShortcut
        onActivated: {
            searchOpen = true;
            replaceOpen = true;
            searchField.forceActiveFocus();
            searchField.selectAll();
        }
    }

    Shortcut {
        sequence: "Ctrl+B"
        context: Qt.WindowShortcut
        onActivated: editor.wrapSelection("**", "**")
    }

    Shortcut {
        sequence: "Ctrl+I"
        context: Qt.WindowShortcut
        onActivated: editor.wrapSelection("*", "*")
    }

    Shortcut {
        sequence: "Ctrl+K"
        context: Qt.WindowShortcut
        onActivated: editor.insertLink()
    }

    Shortcut {
        sequence: "Ctrl+?"
        context: Qt.ApplicationShortcut
        onActivated: shortcutsDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+O"
        context: Qt.ApplicationShortcut
        onActivated: backend.openDialog()
    }

    Shortcut {
        sequence: "Ctrl+N"
        context: Qt.ApplicationShortcut
        onActivated: backend.newWindow()
    }

    Shortcut {
        sequence: "Ctrl+Shift+S"
        context: Qt.ApplicationShortcut
        onActivated: backend.saveAsDialog()
    }

    Shortcut {
        sequence: "Ctrl+P"
        context: Qt.ApplicationShortcut
        onActivated: backend.printDocument()
    }

    Shortcut {
        sequences: ["Meta+F", "F11"]
        context: Qt.ApplicationShortcut
        onActivated: toggleFullScreen()
    }

    Shortcut {
        sequence: "Ctrl+Z"
        context: Qt.WindowShortcut
        onActivated: editor.undo()
    }

    Shortcut {
        sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]
        context: Qt.WindowShortcut
        onActivated: editor.redo()
    }

    Shortcut {
        sequence: "Ctrl+F"
        context: Qt.ApplicationShortcut
        onActivated: {
            searchOpen = true;
            searchField.forceActiveFocus();
            searchField.selectAll();
        }
    }

    Shortcut {
        sequence: "Ctrl+G"
        context: Qt.ApplicationShortcut
        enabled: win.searchOpen
        onActivated: win.moveSearch(1)
    }

    Shortcut {
        sequence: "Ctrl+Space"
        context: Qt.WindowShortcut
        onActivated: {
            if (backend.lspActive)
                backend.requestCompletion(editor.cursorPosition);
        }
    }

    Shortcut {
        sequence: "F12"
        context: Qt.WindowShortcut
        onActivated: {
            if (backend.lspActive)
                backend.requestDefinition(editor.cursorPosition);
        }
    }

    Shortcut {
        sequence: "Ctrl+T"
        context: Qt.ApplicationShortcut
        onActivated: win.toggleTerminal()
    }

    Connections {
        target: backend

        function onOpenDialogRequested() {
            openFileDialog.open();
        }

        function onSaveDialogRequested(suggestedUrl) {
            saveFileDialog.selectedFile = suggestedUrl;
            saveFileDialog.open();
        }

        function onCloseAfterSave() {
            win.closeConfirmed = true;
            win.close();
        }

        function onSaveSucceeded() {
            win.awaitingPendingSave = false;
            if (win.pendingAction !== "")
                win.completePendingAction();
        }

        function onExternalChangeDetected(deleted, locallyModified) {
            externalChangeDialog.deleted = deleted;
            externalChangeDialog.locallyModified = locallyModified;
            externalChangeDialog.open();
        }

        function onJumpToPositionRequested(pos) {
            editor.cursorPosition = pos;
            editorFlick.ensureCursorVisible();
            editor.forceActiveFocus();
        }

        function onCompletionsChanged() {
            if (backend.hasCompletions) {
                completionList.currentIndex = 0;
                completionPopup.open();
            } else {
                completionPopup.close();
            }
        }
    }

    Dialogs.FileDialog {
        id: openFileDialog
        title: "Open File"
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: ["All supported files (*.md *.markdown *.cpp *.c *.h *.hpp *.py *.rs *.go *.js *.ts *.qml *.sh)", "Markdown files (*.md *.markdown)", "Source code files (*.cpp *.c *.h *.hpp *.py *.rs *.go *.js *.ts *.qml *.sh)", "All files (*)"]
        onAccepted: win.requestOpen(selectedFile)
    }

    Dialogs.FileDialog {
        id: saveFileDialog
        title: "Save File"
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: ["Markdown files (*.md *.markdown)", "Source code files (*.cpp *.c *.h *.hpp *.py *.rs *.go *.js *.ts *.qml *.sh)", "All files (*)"]
        onAccepted: backend.saveAs(selectedFile)
        onRejected: {
            backend.fileDialogCanceled();
            win.awaitingPendingSave = false;
            win.pendingAction = "";
        }
    }

    UnsavedChangesDialog {
        id: unsavedChangesDialog
        fileName: backend.fileName
        darkMode: win.darkMode
        textScale: win.textScale
        textColor: win.textColor
        strongTextColor: win.strongTextColor
        activeButtonColor: backend.themeAccent
        containerWidth: win.width
        containerHeight: win.height

        onDiscardRequested: {
            backend.discardRecovery();
            win.completePendingAction();
        }

        onSaveRequested: {
            win.awaitingPendingSave = true;
            backend.save();
        }
        onCancelRequested: win.pendingAction = ""
    }

    ExternalChangeDialog {
        id: externalChangeDialog
        darkMode: win.darkMode
        textScale: win.textScale
        textColor: win.textColor
        strongTextColor: win.strongTextColor
        containerWidth: win.width
        containerHeight: win.height

        onKeepRequested: backend.keepExternalVersion()
        onReloadRequested: backend.reloadFromDisk()
    }

    Dialog {
        id: shortcutsDialog
        modal: true
        title: "Keyboard shortcuts"
        standardButtons: Dialog.Close
        anchors.centerIn: parent
        contentItem: Label {
            text: "Ctrl+S  Save\nCtrl+Shift+S  Save As\nCtrl+O  Open\nCtrl+N  New Window\nCtrl+T  Terminal\nCtrl+F  Find\nCtrl+H  Find and Replace\nCtrl+Space  Complete\nF12  Definition\nCtrl+B  Bold\nCtrl+I  Italic\nCtrl+K  Link\nCtrl+P  Print\nF11 / Super+F  Fullscreen\nCtrl+?  Shortcuts"
            lineHeight: 1.5
        }
    }

    Item {
        anchors.fill: parent

        LineNumberGutter {
            id: gutter
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: win.terminalOpen ? terminalPanel.top : parent.bottom
            width: win.gutterWidth
            textDocument: editor.textDocument
            contentY: editorFlick.contentY
            textOffsetY: editor.y
            currentLine: (backend.positionToLineCol(editor.cursorPosition).line || 0) + 1
            font: editor.font
            textColor: win.mutedColor
            currentLineColor: backend.themeAccent
            separatorColor: win.darkMode ? "#2b2d30" : "#e0e0e0"
            backgroundColor: win.pageColor
            onLineClicked: function(pos) {
                editor.cursorPosition = pos;
                editor.forceActiveFocus();
            }
        }

        Flickable {
            id: editorFlick
            anchors.left: gutter.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: win.terminalOpen ? terminalPanel.top : parent.bottom
            anchors.leftMargin: win.scaledSize(14)
            anchors.rightMargin: win.scaledSize(20)
            clip: true
            contentWidth: Math.max(width, editor.implicitWidth + win.scaledSize(32))
            contentHeight: Math.max(height, editor.y + editor.implicitHeight + win.scaledSize(160))
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                // Wheel scrolling moves contentY directly rather than
                // flicking the Flickable, so the bar has to be told about
                // that activity; linger briefly after the last event.
                active: hovered || pressed || wheelScroll.running || scrollLinger.running
                // Stop above the footer strip so the bar doesn't overlap
                // the word count in the bottom-right corner. Padding and
                // inset, not anchors: the attached-ScrollBar layout overrides
                // anchors. Padding stops the thumb, the inset the track.
                bottomPadding: win.scaledSize(32)
                bottomInset: win.scaledSize(32)
            }
            ScrollBar.horizontal: ScrollBar {
                policy: ScrollBar.AsNeeded
                active: hovered || pressed
                rightPadding: win.scaledSize(32)
                rightInset: win.scaledSize(32)
            }

            Timer {
                id: scrollLinger
                interval: 600
            }

            // Flickable turns a wheel notch into a flick sized by the small
            // application font, which crawls next to a browser. Reproduce
            // Chromium's wheel physics instead (cc::ScrollOffsetAnimationCurve):
            // each notch moves 3 lines of 40px towards a running target, the
            // animation gets shorter as the outstanding distance grows, and a
            // notch landing mid-animation carries the current velocity into
            // the new curve, so sustained spinning keeps picking up speed.
            readonly property real wheelStep: win.scaledSize(120)

            FrameAnimation {
                id: wheelScroll
                running: false

                property real startY: 0
                property real targetY: 0
                property real duration: 0.2
                // Cubic bezier easing; ease-in-out (0.42, 0, 0.58, 1) for a
                // fresh scroll, with y1 tilted on retarget so the curve's
                // initial slope matches the velocity it inherits.
                property real cx1: 0.42
                property real cy1: 0
                readonly property real cx2: 0.58
                readonly property real cy2: 1

                onTriggered: {
                    var x = elapsedTime / duration;
                    if (x >= 1) {
                        editorFlick.contentY = editorFlick.snapToPixel(targetY);
                        stop();
                        return;
                    }
                    editorFlick.contentY = editorFlick.snapToPixel(
                        startY + (targetY - startY) * curveY(solveCurve(x)));
                }

                function begin(from, to, dur, slope) {
                    startY = from;
                    targetY = to;
                    duration = dur;
                    cx1 = 0.42;
                    cy1 = 0.42 * Math.max(-1000, Math.min(1000, slope));
                    restart();
                }

                function retarget(newTarget) {
                    var s = solveCurve(Math.min(1, elapsedTime / duration));
                    var pos = startY + (targetY - startY) * curveY(s);
                    var delta = newTarget - pos;
                    if (Math.abs(delta) < 0.5) {
                        editorFlick.contentY = newTarget;
                        stop();
                        return;
                    }

                    var velocity = curveDY(s) / Math.max(1e-6, curveDX(s))
                        * (targetY - startY) / duration;
                    var dur = editorFlick.wheelDuration(delta);
                    // When already moving faster than the eased curve would,
                    // bound the duration by the time to target at the current
                    // velocity; the 2.5x covers the ease-out tail.
                    if (velocity !== 0 && delta / velocity > 0)
                        dur = Math.min(dur, delta / velocity * 2.5);
                    begin(pos, newTarget, dur, velocity * dur / delta);
                }

                // Cubic bezier through (0,0), (cx1,cy1), (cx2,cy2), (1,1),
                // evaluated by Newton-solving the curve parameter from x.
                function curveX(s) { return 3 * s * (1 - s) * ((1 - s) * cx1 + s * cx2) + s * s * s; }
                function curveY(s) { return 3 * s * (1 - s) * ((1 - s) * cy1 + s * cy2) + s * s * s; }
                function curveDX(s) { return 3 * (1 - s) * (1 - s) * cx1 + 6 * (1 - s) * s * (cx2 - cx1) + 3 * s * s * (1 - cx2); }
                function curveDY(s) { return 3 * (1 - s) * (1 - s) * cy1 + 6 * (1 - s) * s * (cy2 - cy1) + 3 * s * s * (1 - cy2); }

                function solveCurve(x) {
                    var s = x;
                    for (var i = 0; i < 8; ++i) {
                        var error = curveX(s) - x;
                        if (Math.abs(error) < 0.001)
                            break;
                        var d = curveDX(s);
                        if (Math.abs(d) < 1e-6)
                            break;
                        s = Math.max(0, Math.min(1, s - error / d));
                    }
                    return s;
                }
            }

            WheelHandler {
                // Wayland compositors route every pointer's scroll through
                // one seat device that Qt classifies as a touchpad, so the
                // device type cannot tell a mouse wheel from two-finger
                // scrolling. Distinguish by event shape instead: discrete
                // wheel notches arrive with only angleDelta set, while
                // finger scrolling carries pixel-precise pixelDelta.
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: function(wheel) {
                    if (wheel.pixelDelta.x !== 0 || wheel.angleDelta.x !== 0) {
                        var dx = wheel.pixelDelta.x !== 0 ? wheel.pixelDelta.x : (wheel.angleDelta.x / 120 * editorFlick.wheelStep);
                        editorFlick.contentX = Math.max(0, Math.min(Math.max(0, editorFlick.contentWidth - editorFlick.width), editorFlick.contentX - dx));
                        wheel.accepted = true;
                        return;
                    }
                    if (wheel.modifiers & Qt.ShiftModifier) {
                        var dxShift = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : (wheel.angleDelta.y / 120 * editorFlick.wheelStep);
                        editorFlick.contentX = Math.max(0, Math.min(Math.max(0, editorFlick.contentWidth - editorFlick.width), editorFlick.contentX - dxShift));
                        wheel.accepted = true;
                        return;
                    }
                    scrollLinger.restart();
                    if (wheel.pixelDelta.y !== 0)
                        editorFlick.scrollTo(editorFlick.clampContentY(editorFlick.contentY - wheel.pixelDelta.y));
                    else
                        editorFlick.scrollByWheel(wheel);
                    wheel.accepted = true;
                }
            }

            onMovementStarted: wheelScroll.stop()

            function scrollByWheel(wheel) {
                // High-resolution wheels report fractional notches; feed
                // those through the same animated path, like Chromium does
                // for every wheel-source event.
                var notches = wheel.angleDelta.y / 120;
                if (notches === 0)
                    return;

                if (wheelScroll.running) {
                    wheelScroll.retarget(clampContentY(wheelScroll.targetY - notches * wheelStep));
                    return;
                }

                var target = clampContentY(contentY - notches * wheelStep);
                if (target !== contentY)
                    wheelScroll.begin(contentY, target, wheelDuration(target - contentY), 0);
            }

            // Chromium's inverse-delta duration: 200ms for a single notch,
            // ramping down to 100ms once 480px are outstanding.
            function wheelDuration(delta) {
                var pixels = Math.abs(delta) / win.textScale;
                return Math.max(6, Math.min(12, 14 - pixels / 60)) / 60;
            }

            function clampContentY(y) {
                return Math.max(0, Math.min(Math.max(0, contentHeight - height), y));
            }

            // Whole device pixels keep natively hinted glyphs from
            // re-rasterizing mid-animation, which reads as shimmer.
            function snapToPixel(y) {
                return Math.round(y * Screen.devicePixelRatio) / Screen.devicePixelRatio;
            }

            // Jump to a position, abandoning any wheel animation still running.
            function scrollTo(y) {
                wheelScroll.stop();
                contentY = snapToPixel(y);
            }

            // Keep the editing caret within the viewport so writing past the
            // bottom edge scrolls the page along with the text.
            function ensureCursorVisible() {
                var vMargin = win.editorFontPixelSize * 2;
                var cursorTop = editor.y + editor.cursorRectangle.y;
                var cursorBottom = cursorTop + editor.cursorRectangle.height;
                var maxContentY = Math.max(0, contentHeight - height);

                if (cursorBottom + vMargin > contentY + height)
                    scrollTo(Math.min(maxContentY, cursorBottom + vMargin - height));
                else if (cursorTop - vMargin < contentY)
                    scrollTo(Math.max(0, cursorTop - vMargin));

                var cursorLeft = editor.x + editor.cursorRectangle.x;
                var cursorRight = cursorLeft + editor.cursorRectangle.width;
                var hMargin = win.editorFontPixelSize * 3;
                var maxContentX = Math.max(0, contentWidth - width);

                if (cursorRight + hMargin > contentX + width)
                    contentX = Math.min(maxContentX, cursorRight + hMargin - width);
                else if (cursorLeft - hMargin < contentX)
                    contentX = Math.max(0, cursorLeft - hMargin);
            }

            TextEdit {
                id: editor
                objectName: "sourceEditor"
                x: 0
                y: win.scaledSize(20)
                width: backend.isCodeDocument
                    ? Math.max(editorFlick.width - win.scaledSize(32), implicitWidth + win.scaledSize(32))
                    : Math.max(win.editorWidth, editorFlick.width - win.scaledSize(32))
                height: Math.max(editorFlick.height - y - win.scaledSize(60), implicitHeight + win.scaledSize(20))
                text: ""
                textFormat: TextEdit.PlainText
                wrapMode: backend.isCodeDocument ? TextEdit.NoWrap : TextEdit.Wrap
                selectByMouse: true
                persistentSelection: true
                activeFocusOnPress: true
                color: win.textColor
                selectedTextColor: win.strongTextColor
                selectionColor: win.selectionFill
                font.family: "iA Writer Mono S"
                font.pixelSize: win.editorFontPixelSize
                font.weight: Font.Normal
                // Native rendering hints glyphs to the pixel grid, which is
                // crispest at whole scale factors but misplaces and unevenly
                // rasterizes glyphs at fractional ones (and goes stale when
                // the compositor delivers the fractional scale after the
                // first frame). Fall back to Qt's scalable renderer there.
                renderType: Screen.devicePixelRatio % 1 === 0 ? TextEdit.NativeRendering : TextEdit.QtRendering
                cursorDelegate: Rectangle {
                    width: 1
                    color: win.strongTextColor
                }
                onCursorRectangleChanged: editorFlick.ensureCursorVisible()

                function replaceSelectionWith(replacement) {
                    var start = Math.min(selectionStart, selectionEnd);
                    var end = Math.max(selectionStart, selectionEnd);
                    EditorMutations.replaceRange(editor, start, end, replacement);
                }

                function wrapSelection(before, after) {
                    forceActiveFocus();
                    var start = Math.min(selectionStart, selectionEnd);
                    var end = Math.max(selectionStart, selectionEnd);
                    var selected = text.slice(start, end);
                    EditorMutations.replaceRange(editor, start, end,
                                                 before + selected + after,
                                                 before.length,
                                                 before.length + selected.length);
                }

                function insertLink() {
                    var start = Math.min(selectionStart, selectionEnd);
                    var end = Math.max(selectionStart, selectionEnd);
                    var selected = text.slice(start, end);
                    var url = backend.clipboardUrl();
                    var label = selected.length > 0 ? selected : "link text";
                    var destination = url.length > 0 ? url : "https://";
                    var escapedLabel = escapeMarkdownLinkText(label);
                    var markdown = "[" + escapedLabel + "](" + escapeMarkdownLinkDestination(destination) + ")";
                    if (selected.length === 0) {
                        EditorMutations.replaceRange(editor, start, end, markdown,
                                                     1, 1 + escapedLabel.length);
                    } else if (url.length === 0) {
                        EditorMutations.replaceRange(editor, start, end, markdown,
                                                     escapedLabel.length + 3,
                                                     markdown.length - 1);
                    } else {
                        EditorMutations.replaceRange(editor, start, end, markdown);
                    }
                }

                function smartReturn(softBreak) {
                    if (softBreak) {
                        replaceSelectionWith("\n");
                        return;
                    }
                    if (backend.isCodeDocument) {
                        var lineStart = text.lastIndexOf("\n", cursorPosition - 1) + 1;
                        var currentLine = text.slice(lineStart, cursorPosition);
                        var indentMatch = currentLine.match(/^([ \t]*)/);
                        var indent = indentMatch ? indentMatch[1] : "";
                        var trimmed = currentLine.trim();
                        if (trimmed.endsWith("{") || trimmed.endsWith(":") || trimmed.endsWith("(")) {
                            indent += "    ";
                        }
                        replaceSelectionWith("\n" + indent);
                        return;
                    }
                    var lineStart = text.lastIndexOf("\n", cursorPosition - 1) + 1;
                    var line = text.slice(lineStart, cursorPosition);
                    var before = text.slice(0, cursorPosition);
                    var fences = (before.match(/^\s*```/gm) || []).length;
                    if ((fences % 2) === 1) {
                        replaceSelectionWith("\n");
                        return;
                    }
                    var match = line.match(/^(\s*)([-+*]|\d+[.)]|>+)\s+(.*)$/);
                    if (match) {
                        if (match[3].length === 0) {
                            EditorMutations.replaceRange(editor, lineStart,
                                                         cursorPosition, "\n");
                        } else {
                            var marker = match[2];
                            if (/^\d/.test(marker))
                                marker = (parseInt(marker) + 1) + marker.slice(-1);
                            replaceSelectionWith("\n" + match[1] + marker + " ");
                        }
                        return;
                    }
                    replaceSelectionWith("\n\n");
                }

                function escapeMarkdownLinkText(linkText) {
                    return linkText.replace(/\\/g, "\\\\")
                                   .replace(/\[/g, "\\[")
                                   .replace(/\]/g, "\\]");
                }

                function escapeMarkdownLinkDestination(linkUrl) {
                    return linkUrl.replace(/\\/g, "\\\\")
                                  .replace(/\(/g, "\\(")
                                  .replace(/\)/g, "\\)");
                }

                function pasteClipboardUrlAsMarkdownLink() {
                    var start = Math.min(selectionStart, selectionEnd);
                    var end = Math.max(selectionStart, selectionEnd);
                    if (start === end)
                        return false;

                    var url = backend.clipboardUrl();
                    if (url === "")
                        return false;

                    var selected = text.slice(start, end);
                    var leading = selected.match(/^\s*/)[0];
                    var trailing = selected.match(/\s*$/)[0];
                    var linkText = selected.slice(leading.length,
                                                  selected.length - trailing.length);
                    if (linkText === "")
                        return false;

                    replaceSelectionWith(leading + "[" + escapeMarkdownLinkText(linkText) + "]("
                                         + escapeMarkdownLinkDestination(url) + ")" + trailing);
                    return true;
                }

                function pasteClipboardAsPlainText() {
                    var pastedText = backend.clipboardText();
                    if (pastedText.length > 0)
                        replaceSelectionWith(pastedText);
                }

                function skipHiddenForward(position) {
                    var pos = position;
                    var ranges = backend.hiddenRangesAt(pos);
                    for (var i = 0; i < ranges.length; i++) {
                        if (pos >= ranges[i].start && pos < ranges[i].end) {
                            pos = ranges[i].end;
                            i = -1;
                        }
                    }
                    return pos;
                }

                function skipHiddenBackward(position) {
                    var pos = position;
                    var ranges = backend.hiddenRangesAt(pos);
                    for (var i = ranges.length - 1; i >= 0; i--) {
                        if (pos > ranges[i].start && pos <= ranges[i].end) {
                            pos = ranges[i].start;
                            i = ranges.length;
                        }
                    }
                    return pos;
                }

                function moveCursorVisibly(direction) {
                    if (selectionStart !== selectionEnd) {
                        cursorPosition = direction > 0
                            ? Math.max(selectionStart, selectionEnd)
                            : Math.min(selectionStart, selectionEnd);
                        return;
                    }

                    var pos = Math.max(0, Math.min(text.length, cursorPosition + direction));
                    cursorPosition = direction > 0
                        ? skipHiddenForward(pos)
                        : skipHiddenBackward(pos);
                }

                function movePage(direction, extendSelection) {
                    var pageStep = Math.max(win.editorFontPixelSize,
                                            editorFlick.height - win.editorFontPixelSize * 2);
                    var rect = cursorRectangle;
                    var targetY = rect.y + rect.height / 2 + direction * pageStep;
                    var target = positionAt(rect.x, Math.max(0, targetY));
                    if (extendSelection)
                        moveCursorSelection(target, TextEdit.SelectCharacters);
                    else
                        cursorPosition = target;
                }

                function deleteParagraphBreakBehindCursor() {
                    if (selectionStart !== selectionEnd || cursorPosition < 2)
                        return false;

                    if (text.slice(cursorPosition - 2, cursorPosition) !== "\n\n")
                        return false;

                    var start = cursorPosition - 2;
                    remove(start, cursorPosition);
                    cursorPosition = start;
                    return true;
                }

                function acceptCompletion() {
                    if (!backend.hasCompletions)
                        return;
                    var item = backend.completions[completionList.currentIndex];
                    if (!item)
                        return;
                    var insert = item.insertText || item.label;
                    var pos = cursorPosition;
                    var start = pos;
                    while (start > 0 && /\w/.test(text.charAt(start - 1))) {
                        start--;
                    }
                    EditorMutations.replaceRange(editor, start, pos, insert);
                    backend.clearCompletions();
                    completionPopup.close();
                    forceActiveFocus();
                }

                Keys.priority: Keys.BeforeItem
                Keys.onPressed: function(event) {
                    if (completionPopup.opened) {
                        if (event.key === Qt.Key_Down) {
                            completionList.incrementCurrentIndex();
                            event.accepted = true;
                            return;
                        }
                        if (event.key === Qt.Key_Up) {
                            completionList.decrementCurrentIndex();
                            event.accepted = true;
                            return;
                        }
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Tab) {
                            acceptCompletion();
                            event.accepted = true;
                            return;
                        }
                        if (event.key === Qt.Key_Escape) {
                            completionPopup.close();
                            backend.clearCompletions();
                            event.accepted = true;
                            return;
                        }
                    }

                    var pasteKey = (event.key === Qt.Key_V)
                        && (event.modifiers & Qt.ControlModifier)
                        && !(event.modifiers & (Qt.AltModifier | Qt.MetaModifier | Qt.ShiftModifier));
                    var shiftInsert = (event.key === Qt.Key_Insert)
                        && (event.modifiers & Qt.ShiftModifier)
                        && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier));
                    if (pasteKey || shiftInsert) {
                        if (!pasteClipboardUrlAsMarkdownLink())
                            pasteClipboardAsPlainText();
                        event.accepted = true;
                        return;
                    }

                    var returnKey = event.key === Qt.Key_Return || event.key === Qt.Key_Enter;
                    var commandModifier = event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier);
                    if (returnKey && !commandModifier) {
                        smartReturn(event.modifiers & Qt.ShiftModifier);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Tab && !commandModifier) {
                        replaceSelectionWith("    ");
                        event.accepted = true;
                    } else if (!commandModifier && event.key === Qt.Key_Backspace
                               && deleteParagraphBreakBehindCursor()) {
                        event.accepted = true;
                    } else if (!commandModifier && !(event.modifiers & Qt.ShiftModifier)
                               && event.key === Qt.Key_Right) {
                        moveCursorVisibly(1);
                        event.accepted = true;
                    } else if (!commandModifier && !(event.modifiers & Qt.ShiftModifier)
                               && event.key === Qt.Key_Left) {
                        moveCursorVisibly(-1);
                        event.accepted = true;
                    } else if (!commandModifier
                               && (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp)) {
                        movePage(event.key === Qt.Key_PageDown ? 1 : -1,
                                 event.modifiers & Qt.ShiftModifier);
                        event.accepted = true;
                    }
                }

                onCursorPositionChanged: {
                    backend.updateCursorPosition(cursorPosition);
                }

                onTextChanged: {
                    if (win.searchUpdating)
                        return;
                    var contentChanged = backend.editorTextChanged();
                    if (win.searchOpen && contentChanged)
                        win.updateSearch();

                    if (backend.isCodeDocument && backend.lspActive && cursorPosition > 0) {
                        var charJustTyped = text.charAt(cursorPosition - 1);
                        if (charJustTyped === "." || charJustTyped === ">" || charJustTyped === ":") {
                            backend.requestCompletion(cursorPosition);
                        }
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    text: backend.isCodeDocument ? "// Start coding" : "# Start writing"
                    visible: editor.text.length === 0 && !editor.activeFocus
                    color: win.mutedColor
                    font.family: editor.font.family
                    font.pixelSize: editor.font.pixelSize
                    font.weight: editor.font.weight
                }

                Component.onCompleted: {
                    backend.attachDocument(textDocument);
                    forceActiveFocus();
                }
            }
        }

        Rectangle {
            id: terminalPanel
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: win.scaledSize(32)
            height: win.terminalHeight
            visible: win.terminalOpen
            color: win.darkMode ? "#181a1f" : "#f4f4f4"
            clip: true
            z: 5

            Rectangle {
                id: terminalSplitter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: win.scaledSize(3)
                color: win.darkMode ? "#282c34" : "#d8d8d8"

                MouseArea {
                    anchors.fill: parent
                    anchors.topMargin: -win.scaledSize(4)
                    anchors.bottomMargin: -win.scaledSize(4)
                    cursorShape: Qt.SplitVCursor
                    property real startY: 0
                    property real startH: 0
                    onPressed: function(mouse) {
                        startY = mouse.y;
                        startH = win.terminalHeight;
                    }
                    onPositionChanged: function(mouse) {
                        var delta = mouse.y - startY;
                        win.terminalHeight = Math.max(win.scaledSize(100), Math.min(win.height * 0.75, startH - delta));
                    }
                }
            }

            Rectangle {
                id: terminalHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: terminalSplitter.bottom
                height: win.scaledSize(26)
                color: win.darkMode ? "#21252b" : "#ebebeb"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: win.scaledSize(10)
                    anchors.rightMargin: win.scaledSize(8)
                    spacing: 8

                    Label {
                        text: "Terminal"
                        font.family: "iA Writer Mono S"
                        font.pixelSize: win.scaledSize(11)
                        font.bold: true
                        color: win.darkMode ? "#abb2bf" : "#495162"
                    }

                    Label {
                        text: backend.terminal.workingDirectoryShort
                        font.family: "iA Writer Mono S"
                        font.pixelSize: win.scaledSize(11)
                        color: win.mutedColor
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    Label {
                        text: backend.terminal.isRunning ? "Running..." : ""
                        font.family: "iA Writer Mono S"
                        font.pixelSize: win.scaledSize(11)
                        color: backend.themeAccent
                        visible: backend.terminal.isRunning
                    }

                    Label {
                        text: "Clear"
                        font.family: "iA Writer Mono S"
                        font.pixelSize: win.scaledSize(11)
                        color: clearMouse.containsMouse ? win.strongTextColor : win.mutedColor
                        MouseArea {
                            id: clearMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: backend.terminal.clear()
                        }
                    }

                    Label {
                        text: "✕"
                        font.pixelSize: win.scaledSize(12)
                        color: closeMouse.containsMouse ? win.strongTextColor : win.mutedColor
                        MouseArea {
                            id: closeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.toggleTerminal()
                        }
                    }
                }
            }

            Flickable {
                id: termFlick
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: terminalHeader.bottom
                anchors.bottom: termInputRow.top
                anchors.leftMargin: win.scaledSize(10)
                anchors.rightMargin: win.scaledSize(6)
                anchors.topMargin: win.scaledSize(4)
                anchors.bottomMargin: win.scaledSize(4)
                clip: true
                contentWidth: termOutput.width
                contentHeight: termOutput.height
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    active: hovered || pressed
                }

                TextEdit {
                    id: termOutput
                    width: termFlick.width
                    height: Math.max(termFlick.height, implicitHeight)
                    readOnly: true
                    selectByMouse: true
                    text: backend.terminal.output
                    textFormat: TextEdit.PlainText
                    font.family: "iA Writer Mono S"
                    font.pixelSize: win.scaledSize(12)
                    color: win.textColor
                    selectionColor: win.selectionFill
                    selectedTextColor: win.strongTextColor
                    wrapMode: TextEdit.Wrap

                    onTextChanged: {
                        Qt.callLater(function() {
                            if (termFlick.contentHeight > termFlick.height)
                                termFlick.contentY = termFlick.contentHeight - termFlick.height;
                        });
                    }
                }
            }

            Rectangle {
                id: termInputRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: win.scaledSize(28)
                color: win.darkMode ? "#1b1d23" : "#f0f0f0"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: win.scaledSize(10)
                    anchors.rightMargin: win.scaledSize(8)
                    spacing: win.scaledSize(6)

                    Label {
                        text: "$ "
                        font.family: "iA Writer Mono S"
                        font.pixelSize: win.scaledSize(12)
                        font.bold: true
                        color: backend.themeAccent
                    }

                    TextInput {
                        id: terminalInput
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        verticalAlignment: TextInput.AlignVCenter
                        font.family: "iA Writer Mono S"
                        font.pixelSize: win.scaledSize(12)
                        color: win.textColor
                        selectionColor: win.selectionFill
                        selectedTextColor: win.strongTextColor
                        selectByMouse: true
                        clip: true

                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                var cmd = text;
                                text = "";
                                backend.terminal.sendInput(cmd);
                                event.accepted = true;
                                return;
                            }
                            if (event.key === Qt.Key_Up) {
                                text = backend.terminal.historyUp(text);
                                cursorPosition = text.length;
                                event.accepted = true;
                                return;
                            }
                            if (event.key === Qt.Key_Down) {
                                text = backend.terminal.historyDown();
                                cursorPosition = text.length;
                                event.accepted = true;
                                return;
                            }
                            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_C) {
                                if (backend.terminal.isRunning) {
                                    backend.terminal.cancel();
                                    event.accepted = true;
                                    return;
                                }
                            }
                            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_L) {
                                backend.terminal.clear();
                                event.accepted = true;
                                return;
                            }
                            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_T) {
                                win.toggleTerminal();
                                event.accepted = true;
                                return;
                            }
                            if (event.key === Qt.Key_Escape) {
                                win.toggleTerminal();
                                event.accepted = true;
                                return;
                            }
                        }
                    }
                }
            }
        }

        Row {
            id: footerStatus
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: 12
            anchors.bottomMargin: 10
            spacing: 12
            opacity: 0.55

            FooterIconButton {
                objectName: "saveButton"
                iconName: "save"
                iconColor: win.mutedColor
                tooltip: "Save"
                onClicked: backend.save()
            }

            FooterIconButton {
                objectName: "openButton"
                iconName: "open"
                iconColor: win.mutedColor
                tooltip: "Open"
                onClicked: backend.openDialog()
            }

            FooterIconButton {
                objectName: "terminalButton"
                iconName: "terminal"
                iconColor: win.terminalOpen ? backend.themeAccent : win.mutedColor
                tooltip: "Terminal (Ctrl+T)"
                onClicked: win.toggleTerminal()
            }

            Label {
                text: backend.currentDiagnostic !== ""
                    ? backend.currentDiagnostic
                    : (backend.lspStatus !== "" ? backend.lspStatus : backend.status)
                color: backend.currentDiagnostic !== ""
                    ? (win.darkMode ? "#e06c75" : "#e45649")
                    : win.mutedColor
                font.family: "iA Writer Mono S"
                font.pixelSize: win.scaledSize(11)
                visible: text !== ""
                elide: Text.ElideRight
                width: Math.min(520, win.width / 2)
                height: win.scaledSize(16)
                verticalAlignment: Text.AlignVCenter
            }
        }

        Label {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 12
            anchors.bottomMargin: 10
            text: {
                var lc = backend.positionToLineCol(editor.cursorPosition);
                var posStr = "Ln " + ((lc.line || 0) + 1) + ", Col " + ((lc.character || 0) + 1);
                if (backend.isCodeDocument)
                    return posStr;
                return posStr + "  •  " + backend.wordCount + (backend.wordCount === 1 ? " Word" : " Words");
            }
            color: win.mutedColor
            opacity: 0.75
            font.family: "iA Writer Mono S"
            font.pixelSize: win.scaledSize(11)
        }

    Popup {
        id: completionPopup
        parent: win.contentItem
        x: {
            var pt = editor.mapToItem(win.contentItem, editor.cursorRectangle.x, editor.cursorRectangle.y);
            return Math.min(win.width - width - 16, Math.max(16, pt.x));
        }
        y: {
            var pt = editor.mapToItem(win.contentItem, editor.cursorRectangle.x, editor.cursorRectangle.y);
            var targetY = pt.y + editor.cursorRectangle.height + 4;
            if (targetY + height > win.height - 40)
                return Math.max(16, pt.y - height - 4);
            return targetY;
        }
        width: 320
        height: Math.min(win.scaledSize(220), completionList.contentHeight + 10)
        padding: 4
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: win.darkMode ? "#21252b" : "#ffffff"
            border.color: win.darkMode ? "#3a3f4b" : "#d0d4dc"
            border.width: 1
            radius: 6
        }

        ListView {
            id: completionList
            anchors.fill: parent
            clip: true
            model: backend.completions
            delegate: Rectangle {
                width: ListView.view.width
                height: win.scaledSize(24)
                color: ListView.isCurrentItem
                    ? (win.darkMode ? "#2c313a" : "#e8edf5")
                    : "transparent"
                radius: 4

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    Label {
                        text: modelData.label
                        color: ListView.isCurrentItem ? win.strongTextColor : win.textColor
                        font.family: "iA Writer Mono S"
                        font.pixelSize: win.scaledSize(12)
                        font.weight: ListView.isCurrentItem ? Font.Bold : Font.Normal
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    Label {
                        text: modelData.detail || ""
                        color: win.mutedColor
                        font.family: "iA Writer Mono S"
                        font.pixelSize: win.scaledSize(10)
                        visible: text !== ""
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        completionList.currentIndex = index;
                        editor.acceptCompletion();
                    }
                }
            }
        }
    }


        Pane {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 12
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            height: win.scaledSize(win.replaceOpen ? 104 : 56)
            visible: win.searchOpen
            z: 10
            leftPadding: 16
            rightPadding: 8
            topPadding: 0
            bottomPadding: 0
            Material.elevation: 8

            background: Rectangle {
                radius: 9
                color: win.darkMode ? "#22221f" : "#fffef2"
            }

            RowLayout {
                anchors.fill: parent
                spacing: 8

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    TextInput {
                        id: searchField
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: win.replaceOpen ? parent.height / 2 : parent.height
                        verticalAlignment: TextInput.AlignVCenter
                        selectByMouse: true
                        color: win.textColor
                        selectionColor: win.selectionFill
                        selectedTextColor: win.strongTextColor
                        font.pixelSize: win.scaledSize(17)
                        clip: true
                        onTextChanged: win.updateSearch()
                        Keys.onReturnPressed: function(event) {
                            win.moveSearch((event.modifiers & Qt.ShiftModifier) ? -1 : 1);
                            event.accepted = true;
                        }
                        Keys.onEscapePressed: function(event) {
                            win.closeSearch();
                            event.accepted = true;
                        }
                    }

                    TextInput {
                        id: replaceField
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: parent.height / 2
                        visible: win.replaceOpen
                        verticalAlignment: TextInput.AlignVCenter
                        color: win.textColor
                        selectionColor: win.selectionFill
                        selectedTextColor: win.strongTextColor
                        font.pixelSize: win.scaledSize(17)
                        Keys.onReturnPressed: replaceCurrentButton.clicked()
                    }

                    Label {
                        anchors.verticalCenter: replaceField.verticalCenter
                        text: "Replace with"
                        visible: win.replaceOpen && replaceField.text.length === 0
                        color: win.mutedColor
                        font.pixelSize: win.scaledSize(17)
                    }

                    Label {
                        anchors.verticalCenter: searchField.verticalCenter
                        text: "Find"
                        visible: searchField.text.length === 0
                        color: win.mutedColor
                        font.pixelSize: win.scaledSize(17)
                    }
                }

                Label {
                    Layout.preferredWidth: win.scaledSize(58)
                    Layout.fillHeight: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: win.searchMatches.length === 0
                        ? "0/0"
                        : (win.searchMatchIndex + 1) + "/" + win.searchMatches.length
                    color: win.darkMode ? win.textColor : "#62635f"
                    font.pixelSize: win.scaledSize(16)
                }

                Button {
                    id: replaceCurrentButton
                    visible: win.replaceOpen
                    text: "Replace"
                    onClicked: {
                        if (win.searchMatchIndex < 0) return;
                        var start = win.searchMatches[win.searchMatchIndex];
                        EditorMutations.replaceRange(editor, start,
                                                     start + searchField.text.length,
                                                     replaceField.text);
                        win.updateSearch();
                    }
                }

                Button {
                    visible: win.replaceOpen
                    text: "All"
                    onClicked: {
                        if (searchField.text.length === 0) return;
                        for (var i = win.searchMatches.length - 1; i >= 0; --i) {
                            var start = win.searchMatches[i];
                            EditorMutations.replaceRange(editor, start,
                                                         start + searchField.text.length,
                                                         replaceField.text);
                        }
                        win.updateSearch();
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 34
                    color: win.darkMode ? "#6f6f62" : "#d5d56e"
                }

                SearchIconButton {
                    iconName: "up"
                    iconColor: win.darkMode ? win.textColor : "#62635f"
                    onClicked: win.moveSearch(-1)
                }

                SearchIconButton {
                    iconName: "down"
                    iconColor: win.darkMode ? win.textColor : "#62635f"
                    onClicked: win.moveSearch(1)
                }

                SearchIconButton {
                    iconName: "close"
                    iconColor: win.darkMode ? win.textColor : "#62635f"
                    onClicked: win.closeSearch()
                }
            }
        }
    }

    Component.onCompleted: {
        var geometry = backend.windowGeometry();
        if (geometry.x >= 0) x = geometry.x;
        if (geometry.y >= 0) y = geometry.y;
        width = geometry.width;
        height = geometry.height;
        if (geometry.maximized) showMaximized();
    }

    Component.onDestruction: backend.saveWindowGeometry(x, y, width, height, visibility === Window.Maximized)

}
