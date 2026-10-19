import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: window
    width: 1160; height: 820
    minimumWidth: 640; minimumHeight: 620
    title: chat.title + " — Kllama"
    property bool compact: width < 850
    property string feedbackText: ""
    property int editingRow: -1
    property string previewId: ""
    property url previewImageUrl: ""
    property var previewSources: []
    property string previewWarning: ""
    function openSettings() {
        if (chat.busy || chat.attachments.busy) return
        address.text = ollama.endpoint
        location.currentIndex = ollama.local ? 0 : 1
        settingsError.text = ""
        settings.open()
    }
    function openAbout() { settings.close(); about.open() }
    function statusCaption(value) {
        switch (value) {
        case "streaming": return i18n("In progress")
        case "stopped": return i18n("Stopped")
        case "interrupted": return i18n("Interrupted")
        case "error": return i18n("Failed")
        default: return ""
        }
    }
    function sendMessage() { chat.send(); draft.forceActiveFocus() }
    function previewAttachment(id, name) {
        window.previewId = id
        window.previewImageUrl = chat.attachments.imageUrl(id)
        window.previewSources = chat.attachments.sources(id)
        const attachment = chat.attachments.allEntries.find(entry => entry.id === id)
        window.previewWarning = attachment ? attachment.warning || "" : ""
        sourceChoice.currentIndex = 0
        previewDialog.title = name
        previewText.text = chat.attachments.preview(id)
        previewDialog.open()
    }
    function editMessage(row) {
        editingRow = row
        editedText.text = chat.messageText(row)
        editDialog.open()
    }
    Connections {
        target: chat
        function onFeedback(text) { window.feedbackText = text; feedbackTimer.restart() }
        function onDraftChanged() { if (draft.text !== chat.draft) draft.text = chat.draft }
        function onConversationChanged() { transcript.following = true; Qt.callLater(() => transcript.positionViewAtEnd()) }
    }
    Timer { id: feedbackTimer; interval: 2500; onTriggered: window.feedbackText = "" }
    Shortcut { sequence: "Ctrl+,"; enabled: !chat.busy && !chat.attachments.busy; onActivated: window.openSettings() }
    Shortcut { sequence: "Ctrl+N"; enabled: !chat.busy && !chat.attachments.busy; onActivated: chat.newConversation() }
    Shortcut { sequence: "F5"; enabled: !chat.busy && !chat.attachments.busy; onActivated: ollama.refresh() }

    // Keep application controls alive instead of creating asynchronous page-action delegates.
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None
    header: Controls.ToolBar {
        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing
            Controls.ToolButton {
                visible: window.compact
                text: i18n("History"); icon.name: "view-history"
                onClicked: historyDrawer.open()
            }
            Controls.Label { textFormat: Text.PlainText; text: i18n("Kllama"); Layout.leftMargin: Kirigami.Units.largeSpacing; Layout.fillWidth: true }
            Controls.ToolButton {
                text: i18n("Connection settings"); icon.name: "configure"
                display: window.compact ? Controls.AbstractButton.IconOnly : Controls.AbstractButton.TextBesideIcon
                enabled: !chat.busy && !chat.attachments.busy; onClicked: window.openSettings()
                Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered
            }
            Controls.ToolButton {
                text: i18n("Refresh models"); icon.name: "view-refresh"
                display: window.compact ? Controls.AbstractButton.IconOnly : Controls.AbstractButton.TextBesideIcon
                enabled: !ollama.busy && !chat.busy; onClicked: { ollama.refresh(); chat.capabilities.refresh() }
                Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered
            }
            Controls.ToolButton {
                text: i18n("Export Markdown"); icon.name: "document-export"
                display: window.compact ? Controls.AbstractButton.IconOnly : Controls.AbstractButton.TextBesideIcon
                enabled: !chat.busy; onClicked: exportDialog.open()
                Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered
            }
            Controls.ToolButton {
                text: i18n("About Kllama"); icon.name: "help-about"
                display: window.compact ? Controls.AbstractButton.IconOnly : Controls.AbstractButton.TextBesideIcon
                onClicked: window.openAbout()
                Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered
            }
        }
    }

    component UsageBar: ColumnLayout {
        required property string label
        required property real fraction
        required property color tint
        spacing: 3
        RowLayout {
            Layout.fillWidth: true
            Controls.Label { textFormat: Text.PlainText; text: label; font: Kirigami.Theme.smallFont; Layout.fillWidth: true }
            Controls.Label { textFormat: Text.PlainText; text: Math.round(fraction * 100) + "%"; font: Kirigami.Theme.smallFont }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 7; radius: 3.5
            color: Qt.alpha(tint, 0.14)
            Rectangle {
                height: parent.height; radius: parent.radius
                width: parent.width * fraction
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Qt.alpha(tint, 0.65) }
                    GradientStop { position: 1; color: tint }
                }
                Behavior on width { enabled: ollama.animations; NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
            }
        }
        Accessible.role: Accessible.ProgressBar
        Accessible.name: label + " " + Math.round(fraction * 100) + "%"
    }
    component HistoryPane: ColumnLayout {
        spacing: Kirigami.Units.largeSpacing
        RowLayout {
            Kirigami.Icon { source: "qrc:/kllama.svg"; Layout.preferredWidth: 36; Layout.preferredHeight: 36 }
            Kirigami.Heading { textFormat: Text.PlainText; text: i18n("Kllama"); level: 2 }
        }
        Controls.Button {
            text: i18n("New conversation"); icon.name: "list-add"
            Layout.fillWidth: true; enabled: !chat.busy && !chat.attachments.busy
            onClicked: { chat.newConversation(); historyDrawer.close(); draft.forceActiveFocus() }
        }
        Controls.TextField {
            id: historySearch
            Layout.fillWidth: true
            placeholderText: i18n("Find a conversation…")
            Accessible.name: i18n("Search conversation titles")
        }
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: 3
            model: chat.history
            Controls.ScrollBar.vertical: Controls.ScrollBar { id: historyScrollBar }
            delegate: Controls.ItemDelegate {
                required property var modelData
                width: Math.max(0, ListView.view.width - historyScrollBar.width - Kirigami.Units.smallSpacing)
                text: modelData.title
                highlighted: modelData.id === chat.conversationId
                enabled: !chat.busy && !chat.attachments.busy
                visible: historySearch.text.length === 0 || text.toLowerCase().includes(historySearch.text.toLowerCase())
                height: visible ? implicitHeight : 0
                onClicked: { chat.openConversation(modelData.id); historyDrawer.close() }
                Controls.ToolTip.visible: hovered
                Controls.ToolTip.text: text
            }
        }
        Kirigami.Separator { Layout.fillWidth: true }
        Controls.Label { textFormat: Text.PlainText; text: ollama.local ? i18n("Local Ollama") : i18n("Remote Ollama") }
        Controls.Label { textFormat: Text.PlainText; text: ollama.endpoint; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
        Controls.Label { textFormat: Text.PlainText; text: ollama.statistics; font: Kirigami.Theme.smallFont; Layout.fillWidth: true; wrapMode: Text.NoWrap; elide: Text.ElideRight; fontSizeMode: Text.HorizontalFit; minimumPixelSize: 10; opacity: 0.75; Controls.ToolTip.visible: statisticsHover.hovered; Controls.ToolTip.text: text; HoverHandler { id: statisticsHover } }
        Controls.Label {
            textFormat: Text.PlainText; text: i18n("Model placement")
            font: Kirigami.Theme.smallFont
            Controls.ToolTip.visible: placementHover.hovered
            Controls.ToolTip.text: i18n("Model memory split between CPU and GPU, as reported by Ollama. These are not processor activity percentages. Updates every five seconds, locally or remotely.")
            HoverHandler { id: placementHover }
        }
        Controls.Label {
            visible: ollama.runtimeStatus.length > 0
            textFormat: Text.PlainText; text: ollama.runtimeStatus
            font: Kirigami.Theme.smallFont; opacity: 0.75
            Layout.fillWidth: true; elide: Text.ElideRight
        }
        Controls.ScrollView {
            id: runtimeScroll
            visible: ollama.runningModels.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(runtimeList.implicitHeight, 190)
            contentWidth: availableWidth
            rightPadding: Kirigami.Units.largeSpacing
            ColumnLayout {
                id: runtimeList
                width: runtimeScroll.availableWidth
                spacing: Kirigami.Units.largeSpacing
                Repeater {
                    model: ollama.runningModels
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true; spacing: Kirigami.Units.smallSpacing
                        Controls.Label {
                            textFormat: Text.PlainText; text: modelData.name
                            font: Kirigami.Theme.smallFont; Layout.fillWidth: true; elide: Text.ElideRight
                            Controls.ToolTip.visible: modelHover.hovered
                            Controls.ToolTip.text: text
                            HoverHandler { id: modelHover }
                        }
                        UsageBar { label: i18n("CPU"); fraction: modelData.cpu; tint: Kirigami.Theme.linkColor; Layout.fillWidth: true }
                        UsageBar { label: i18n("GPU"); fraction: modelData.gpu; tint: Kirigami.Theme.positiveTextColor; Layout.fillWidth: true }
                    }
                }
            }
        }
        Controls.Button { text: i18n("Connection settings"); icon.name: "configure"; enabled: !chat.busy && !chat.attachments.busy; Layout.fillWidth: true; onClicked: window.openSettings() }
        Controls.Button { text: i18n("About Kllama"); icon.name: "help-about"; Layout.fillWidth: true; onClicked: window.openAbout() }
    }
    Controls.Drawer {
        id: historyDrawer
        width: 280; height: window.height
        edge: Qt.LeftEdge
        HistoryPane { anchors.fill: parent; anchors.margins: Kirigami.Units.largeSpacing }
    }
    pageStack.initialPage: Kirigami.Page {
        padding: 0
        title: i18n("Kllama")
        globalToolBarStyle: Kirigami.ApplicationHeaderStyle.None
        DropArea {
            anchors.fill: parent; z: 5
            enabled: !chat.busy
            onDropped: drop => { if (drop.hasUrls) { chat.attachments.addFiles(drop.urls); drop.acceptProposedAction() } }
            Rectangle {
                anchors.fill: parent
                visible: opacity > 0
                color: Kirigami.Theme.backgroundColor; opacity: parent.containsDrag ? 0.94 : 0
                Behavior on opacity { enabled: ollama.animations; NumberAnimation { duration: 120 } }
                border.color: Kirigami.Theme.highlightColor; border.width: 2; radius: 8
                Controls.Label { textFormat: Text.PlainText; anchors.centerIn: parent; text: i18n("Drop documents, text or supported images to attach") }
            }
        }
        RowLayout {
            anchors.fill: parent; spacing: 0
            Rectangle {
                visible: !window.compact
                Layout.preferredWidth: 240; Layout.fillHeight: true
                color: Kirigami.Theme.alternateBackgroundColor
                HistoryPane { anchors.fill: parent; anchors.margins: Kirigami.Units.largeSpacing }
            }
            Kirigami.Separator { visible: !window.compact; Layout.fillHeight: true }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                RowLayout {
                    Layout.fillWidth: true; Layout.margins: Kirigami.Units.largeSpacing
                    Kirigami.Heading { textFormat: Text.PlainText; text: chat.title; level: 2; Layout.fillWidth: true; elide: Text.ElideRight }
                    Controls.ToolButton {
                        icon.name: "edit-rename"; text: i18n("Rename"); display: Controls.AbstractButton.IconOnly
                        enabled: !chat.busy && !chat.attachments.busy
                        onClicked: { renameText.text = chat.title; renameDialog.open() }
                        Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered
                    }
                    Controls.ToolButton {
                        icon.name: "edit-delete"; text: i18n("Delete conversation"); display: Controls.AbstractButton.IconOnly
                        enabled: !chat.busy && !chat.attachments.busy; onClicked: deleteDialog.open()
                        Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered
                    }
                    Controls.ComboBox {
                        Layout.preferredWidth: window.compact ? 155 : 210
                        model: ollama.models; enabled: ollama.state === "ready" && !chat.busy
                        currentIndex: ollama.models.indexOf(chat.model)
                        displayText: currentIndex >= 0 ? currentText : chat.model.length ? chat.model + i18n(" (unavailable)") : i18n("Select model")
                        onActivated: chat.model = currentText
                        Accessible.name: i18n("Selected Ollama model")
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.leftMargin: Kirigami.Units.largeSpacing; Layout.rightMargin: Kirigami.Units.largeSpacing
                    Controls.Label { textFormat: Text.PlainText;
                        text: chat.capabilities.summary
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; opacity: 0.8
                        Controls.ToolTip.text: chat.capabilities.detail; Controls.ToolTip.visible: capabilityHover.hovered
                        HoverHandler { id: capabilityHover }
                    }
                    Controls.ToolButton {
                        text: i18n("Model details"); icon.name: "dialog-information"
                        display: Controls.AbstractButton.IconOnly
                        onClicked: capabilityDialog.open()
                        Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered
                    }
                    Controls.ToolButton {
                        text: i18n("Inspect model"); icon.name: "view-refresh"
                        display: Controls.AbstractButton.IconOnly
                        enabled: !chat.busy && chat.model.length > 0 && chat.capabilities.state !== "loading"
                        onClicked: chat.capabilities.refresh()
                        Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered
                    }
                }
                Kirigami.Separator { Layout.fillWidth: true }
                Kirigami.InlineMessage {
                    Layout.fillWidth: true; Layout.margins: Kirigami.Units.largeSpacing
                    visible: chat.storageError.length > 0
                    type: Kirigami.MessageType.Error
                    text: i18n("Conversation storage is unavailable: %1", chat.storageError)
                }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ListView {
                        id: transcript
                        objectName: "transcript"
                        anchors.fill: parent; anchors.margins: Kirigami.Units.largeSpacing
                        clip: true; spacing: 18
                        model: chat.messages
                        property bool following: true
                        onMovementStarted: following = false
                        onMovementEnded: following = atYEnd
                        onContentHeightChanged: if (following) Qt.callLater(() => { if (following) positionViewAtEnd() })
                        Controls.ScrollBar.vertical: Controls.ScrollBar {
                            id: transcriptScrollBar
                            objectName: "transcriptScrollBar"
                            onPressedChanged: { if (pressed) transcript.following = false; else if (transcript.atYEnd) transcript.following = true }
                        }
                        delegate: Item {
                            id: messageDelegate
                            required property int index
                            required property string speaker
                            required property string body
                            required property string messageState
                            required property var files
                            required property double tokenCount
                            required property string reasoning
                            property bool reasoningExpanded: false
                            required property string responseModel
                            width: Math.max(0, ListView.view.width - transcriptScrollBar.width - Kirigami.Units.smallSpacing)
                            height: bubble.implicitHeight + (bubbleTokens.visible ? bubbleTokens.implicitHeight + Kirigami.Units.smallSpacing : 0)
                            function updateText() {
                                if (!messageText) return
                                const start = messageText.selectionStart
                                const selected = messageText.selectedText
                                const oldPosition = transcript.contentY
                                messageText.text = speaker === "assistant" ? markdown.render(body) : body
                                if (start >= 0 && selected.length) {
                                    const plain = messageText.getText(0, messageText.length)
                                    let position = plain.indexOf(selected)
                                    let nearest = position
                                    while (position >= 0) {
                                        if (Math.abs(position - start) < Math.abs(nearest - start)) nearest = position
                                        position = plain.indexOf(selected, position + 1)
                                    }
                                    if (nearest >= 0) messageText.select(nearest, nearest + selected.length)
                                    transcript.following = false
                                    transcript.contentY = oldPosition
                                }
                            }
                            Timer { id: renderTimer; interval: 80; onTriggered: messageDelegate.updateText() }
                            onBodyChanged: {
                                if (messageState === "streaming") { if (!renderTimer.running) renderTimer.start() }
                                else updateText()
                            }
                            onMessageStateChanged: if (messageState !== "streaming") { renderTimer.stop(); updateText() }
                            Component.onCompleted: updateText()
                            Controls.Label {
                                id: bubbleTokens
                                objectName: "bubbleTokens_" + messageDelegate.index
                                anchors.top: bubble.bottom; anchors.topMargin: Kirigami.Units.smallSpacing
                                anchors.left: messageDelegate.speaker === "assistant" ? bubble.left : undefined
                                anchors.right: messageDelegate.speaker === "user" ? bubble.right : undefined
                                visible: messageDelegate.tokenCount >= 0
                                textFormat: Text.PlainText
                                text: messageDelegate.speaker === "user" ? i18n("%1 input tokens", messageDelegate.tokenCount) : i18n("%1 output tokens", messageDelegate.tokenCount)
                                font: Kirigami.Theme.smallFont; opacity: 0.65
                                Controls.ToolTip.visible: tokenHover.hovered
                                Controls.ToolTip.text: messageDelegate.speaker === "user" ? i18n("Input reported by Ollama for this request, including history and attachments.") : i18n("Generated tokens reported by Ollama, including reasoning when enabled.")
                                HoverHandler { id: tokenHover }
                            }
                            Controls.Frame {
                                id: bubble
                                objectName: "chatBubble_" + messageDelegate.index
                                anchors.right: messageDelegate.speaker === "user" ? parent.right : undefined
                                width: messageDelegate.speaker === "user" ? parent.width * 0.9 : parent.width
                                background: Rectangle {
                                    color: messageDelegate.speaker === "user" ? Kirigami.Theme.alternateBackgroundColor : Kirigami.Theme.backgroundColor
                                    radius: 10
                                    border.color: Kirigami.Theme.alternateBackgroundColor
                                }
                                ColumnLayout {
                                    anchors.left: parent.left; anchors.right: parent.right
                                    RowLayout {
                                        Controls.Label { textFormat: Text.PlainText; text: messageDelegate.speaker === "user" ? i18n("You") : i18n("Kllama"); font.bold: true }
                                        Controls.Label { textFormat: Text.PlainText; text: messageDelegate.responseModel; opacity: 0.7; Layout.fillWidth: true; elide: Text.ElideRight }
                                        Controls.Label { textFormat: Text.PlainText; text: window.statusCaption(messageDelegate.messageState); opacity: 0.75 }
                                    }
                                    Flow {
                                        id: sentFiles
                                        visible: messageDelegate.files.length > 0
                                        Layout.fillWidth: true; spacing: Kirigami.Units.smallSpacing
                                        Repeater {
                                            model: messageDelegate.files
                                            delegate: RowLayout {
                                                required property var modelData
                                                readonly property var info: chat.attachments.allEntries.find(file => file.id === modelData.id) || ({})
                                                width: Math.min(implicitWidth, sentFiles.width)
                                                spacing: 2
                                                Controls.Button {
                                                    objectName: "sentAttachment_" + messageDelegate.index + "_" + modelData.id
                                                    text: modelData.name; icon.name: "mail-attachment"
                                                    Layout.fillWidth: true; Layout.minimumWidth: 0
                                                    onClicked: {
                                                        if (parent.info.state === "ready") window.previewAttachment(modelData.id, modelData.name)
                                                        else { window.feedbackText = parent.info.error || i18n("Preparing attachment…"); feedbackTimer.restart() }
                                                    }
                                                    Controls.ToolTip.visible: hovered
                                                    Controls.ToolTip.text: parent.info.error || i18n("Attachment sent with this message. Click to preview.")
                                                }
                                                Controls.ToolButton {
                                                    visible: parent.info.state === "error" || parent.info.state === "cancelled"
                                                    text: i18n("Retry"); icon.name: "view-refresh"; display: Controls.AbstractButton.IconOnly
                                                    enabled: !chat.busy && !chat.attachments.busy
                                                    onClicked: chat.attachments.retry(modelData.id)
                                                    Controls.ToolTip.visible: hovered; Controls.ToolTip.text: text
                                                }
                                                Controls.ToolButton {
                                                    text: i18n("Remove attachment"); icon.name: "list-remove"; display: Controls.AbstractButton.IconOnly
                                                    enabled: !chat.busy && !chat.attachments.busy
                                                    onClicked: chat.removeMessageAttachment(messageDelegate.index, modelData.id)
                                                    Controls.ToolTip.visible: hovered; Controls.ToolTip.text: text
                                                }
                                            }
                                        }
                                    }
                                    Controls.Frame {
                                        objectName: "modelActivity_" + messageDelegate.index
                                        readonly property bool thinking: chat.state === "thinking"
                                        visible: chat.busy && messageDelegate.speaker === "assistant" && messageDelegate.messageState === "streaming" && messageDelegate.index === transcript.count - 1 && (messageDelegate.body.length === 0 || thinking)
                                        Layout.fillWidth: true
                                        background: Rectangle {
                                            radius: 8
                                            color: Qt.alpha(Kirigami.Theme.highlightColor, 0.08)
                                            border.color: Qt.alpha(Kirigami.Theme.highlightColor, 0.3)
                                        }
                                        RowLayout {
                                            anchors.left: parent.left; anchors.right: parent.right
                                            spacing: Kirigami.Units.largeSpacing
                                            Row {
                                                spacing: 5
                                                Repeater {
                                                    model: 3
                                                    delegate: Rectangle {
                                                        id: activityDot
                                                        required property int index
                                                        width: 7; height: 7; radius: 3.5
                                                        color: Kirigami.Theme.highlightColor
                                                        opacity: 0.6
                                                        SequentialAnimation on opacity {
                                                            running: activityDot.visible && chat.busy && ollama.animations
                                                            loops: Animation.Infinite
                                                            PauseAnimation { duration: activityDot.index * 140 }
                                                            NumberAnimation { to: 1; duration: 280 }
                                                            NumberAnimation { to: 0.3; duration: 420 }
                                                            PauseAnimation { duration: (2 - activityDot.index) * 140 }
                                                        }
                                                    }
                                                }
                                            }
                                            Controls.Label {
                                                objectName: "modelActivityLabel_" + messageDelegate.index
                                                textFormat: Text.PlainText
                                                text: chat.state === "thinking" ? i18n("Thinking…") : i18n("Waiting for model…")
                                                color: Kirigami.Theme.textColor
                                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                                Accessible.name: text
                                            }
                                        }
                                    }
                                    RowLayout {
                                        visible: chat.showReasoning && messageDelegate.reasoning.length > 0
                                        Layout.fillWidth: true
                                        Controls.Button {
                                            objectName: "reasoningToggle_" + messageDelegate.index
                                            text: messageDelegate.reasoningExpanded ? i18n("Hide reasoning") : i18n("Show reasoning")
                                            icon.name: messageDelegate.reasoningExpanded ? "arrow-up" : "arrow-down"
                                            checkable: true; checked: messageDelegate.reasoningExpanded
                                            onToggled: messageDelegate.reasoningExpanded = checked
                                        }
                                        Controls.Label { textFormat: Text.PlainText; text: i18n("Experimental"); font: Kirigami.Theme.smallFont; opacity: 0.65 }
                                        Controls.ToolButton {
                                            visible: messageDelegate.reasoningExpanded
                                            text: i18n("Copy reasoning"); icon.name: "edit-copy"
                                            onClicked: chat.copy(messageDelegate.reasoning)
                                        }
                                    }
                                    Controls.ScrollView {
                                        id: reasoningScroll
                                        visible: chat.showReasoning && messageDelegate.reasoningExpanded && messageDelegate.reasoning.length > 0
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: Math.min(180, reasoningText.implicitHeight)
                                        contentWidth: availableWidth
                                        Controls.TextArea {
                                            id: reasoningText
                                            objectName: "reasoningText_" + messageDelegate.index
                                            text: messageDelegate.reasoning
                                            readOnly: true; selectByMouse: true
                                            textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
                                            rightPadding: reasoningScroll.Controls.ScrollBar.vertical.width + Kirigami.Units.largeSpacing
                                            Accessible.name: i18n("Reasoning supplied by the model")
                                        }
                                    }
                                    Controls.TextArea {
                                        id: messageText
                                        objectName: "messageText_" + messageDelegate.index
                                        Layout.fillWidth: true
                                        readOnly: true; selectByMouse: true; textFormat: messageDelegate.speaker === "assistant" ? TextEdit.RichText : TextEdit.PlainText
                                        wrapMode: TextEdit.Wrap
                                        background: null; padding: 2
                                        persistentSelection: true
                                        onLinkActivated: link => markdown.openLink(link)
                                        Accessible.name: messageDelegate.speaker === "user" ? i18n("Your message") : i18n("Assistant response")
                                        onSelectedTextChanged: if (selectedText.length) transcript.following = false
                                        TapHandler { acceptedButtons: Qt.RightButton; onTapped: messageMenu.popup() }
                                        Controls.Menu {
                                            id: messageMenu
                                            Controls.MenuItem { text: i18n("Copy selection"); enabled: messageText.selectedText.length > 0; onTriggered: messageText.copy() }
                                            Controls.MenuItem { text: i18n("Copy message"); onTriggered: chat.copy(messageDelegate.body) }
                                            Controls.MenuItem { text: i18n("Select all"); onTriggered: { messageText.forceActiveFocus(); messageText.selectAll() } }
                                        }
                                    }
                                    Flow {
                                        Layout.fillWidth: true; spacing: 6
                                        Controls.Button { text: messageDelegate.speaker === "assistant" ? i18n("Copy Markdown") : i18n("Copy message"); icon.name: "edit-copy"; enabled: messageDelegate.body.length > 0; onClicked: chat.copy(messageDelegate.body) }
                                        Controls.Button {
                                            visible: messageDelegate.speaker === "user"
                                            text: i18n("Edit and resend"); icon.name: "document-edit"; enabled: !chat.busy && !chat.attachments.busy
                                            onClicked: window.editMessage(messageDelegate.index)
                                        }
                                        Repeater {
                                            model: messageDelegate.messageState === "streaming" ? [] : chat.codeBlocks(messageDelegate.index)
                                            delegate: Controls.Button {
                                                required property var modelData
                                                required property int index
                                                text: i18n("Copy code %1", index + 1) + (modelData.language.length ? " · " + modelData.language : "")
                                                onClicked: chat.copy(modelData.text)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        visible: transcript.count === 0
                        anchors.centerIn: parent; width: Math.min(parent.width - 48, 500)
                        spacing: Kirigami.Units.largeSpacing
                        Kirigami.Icon { source: "qrc:/kllama.svg"; Layout.preferredWidth: 80; Layout.preferredHeight: 80; Layout.alignment: Qt.AlignHCenter }
                        Kirigami.Heading { textFormat: Text.PlainText; text: ollama.state === "ready" ? i18n("What would you like to explore?") : i18n("Welcome to Kllama"); level: 1; Layout.fillWidth: true; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }
                        Controls.Label { textFormat: Text.PlainText; text: ollama.message; Layout.fillWidth: true; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }
                        Controls.Button {
                            text: ollama.busy ? i18n("Cancel connection check") : i18n("Refresh models")
                            Layout.alignment: Qt.AlignHCenter
                            onClicked: ollama.busy ? ollama.cancel() : ollama.refresh()
                        }
                    }
                    Controls.Button {
                        visible: !transcript.following && !transcript.atYEnd
                        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
                        text: i18n("Jump to latest ↓")
                        onClicked: { transcript.following = true; transcript.positionViewAtEnd() }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.margins: Kirigami.Units.largeSpacing * 2
                    RowLayout {
                        Layout.fillWidth: true
                        Controls.Label {
                            visible: chat.state === "error" || chat.state === "stopped" || chat.state === "interrupted"
                            textFormat: Text.PlainText; text: chat.status
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            Accessible.name: text
                        }
                        Item { Layout.fillWidth: true }
                        Controls.Button { visible: chat.busy; text: i18n("Stop"); icon.name: "process-stop"; onClicked: chat.stop() }
                        Controls.Button {
                            visible: chat.canRetry
                            text: chat.state === "complete" || chat.state === "idle" ? i18n("Regenerate") : i18n("Retry")
                            enabled: ollama.models.indexOf(chat.model) >= 0 && chat.storageError.length === 0 && !chat.attachments.busy
                            onClicked: chat.retry()
                        }
                    }
                    Controls.ScrollView {
                        id: attachmentScroll
                        visible: chat.attachments.entries.length > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(window.height < 750 ? 85 : 150, attachmentColumn.implicitHeight)
                        contentWidth: availableWidth
                        ColumnLayout {
                            id: attachmentColumn
                            width: attachmentScroll.availableWidth
                            Repeater {
                                model: chat.attachments.entries
                                delegate: Controls.Frame {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    ColumnLayout {
                                        anchors.left: parent.left; anchors.right: parent.right
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Controls.Label { textFormat: Text.PlainText; text: modelData.name + " · " + Math.ceil(modelData.size / 1024) + " KiB"; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                            Controls.Label { textFormat: Text.PlainText; text: modelData.state === "ready" ? i18n("Ready ✓") : modelData.state === "extracting" || modelData.state === "queued" ? (modelData.stage || i18n("Preparing…")) : modelData.state === "cancelled" ? i18n("Cancelled") : i18n("Failed") }
                                            Controls.ToolButton { text: i18n("Preview"); icon.name: "document-preview"; enabled: modelData.state === "ready"; onClicked: window.previewAttachment(modelData.id, modelData.name) }
                                            Controls.ToolButton { visible: modelData.state === "extracting" || modelData.state === "queued"; text: i18n("Cancel"); enabled: !chat.busy; onClicked: chat.attachments.cancel(modelData.id) }
                                            Controls.ToolButton { visible: modelData.state === "error" || modelData.state === "cancelled"; text: i18n("Retry"); enabled: !chat.busy; onClicked: chat.attachments.retry(modelData.id) }
                                            Controls.ToolButton { text: i18n("Remove"); icon.name: "list-remove"; display: Controls.AbstractButton.IconOnly; enabled: !chat.busy; onClicked: chat.attachments.remove(modelData.id); Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered }
                                        }
                                        Controls.ProgressBar { visible: modelData.state === "extracting"; Layout.fillWidth: true; from: 0; to: 100; value: modelData.progress; Accessible.name: i18n("File preparation progress") }
                                        Controls.Label { textFormat: Text.PlainText; visible: (modelData.warning || "").length > 0; text: modelData.warning || ""; Layout.fillWidth: true; wrapMode: Text.WordWrap; opacity: 0.8 }
                                        Controls.Label { textFormat: Text.PlainText; visible: modelData.error.length > 0; text: modelData.error; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        visible: chat.contextIssue.length > 0
                        Layout.fillWidth: true
                        Controls.Label { textFormat: Text.PlainText; text: i18n("Review model, attachments or context."); Layout.fillWidth: true; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight; color: Kirigami.Theme.neutralTextColor }
                        Controls.ToolButton { text: i18n("Details"); onClicked: contextDialog.open() }
                    }
                    Controls.Frame {
                        Layout.fillWidth: true
                        ColumnLayout {
                            anchors.left: parent.left; anchors.right: parent.right
                            Controls.ScrollView {
                                id: composerScrollView
                                objectName: "composerScrollView"
                                contentWidth: availableWidth
                                // Retain the platform style's scrollbar positioning.
                                readonly property var verticalBar: Controls.ScrollBar.vertical
                                Component.onCompleted: verticalBar.objectName = "composerScrollBar"
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.min(130, Math.max(65, draft.implicitHeight))
                                Controls.TextArea {
                                    id: draft
                                    objectName: "draftEditor"
                                    rightPadding: composerScrollView.verticalBar.width + Kirigami.Units.largeSpacing
                                    text: chat.draft
                                    placeholderText: chat.busy ? i18n("Draft your next message…") : i18n("Message Kllama…")
                                    textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
                                    selectByMouse: true
                                    Accessible.name: i18n("Message draft")
                                    onTextChanged: if (text !== chat.draft) chat.draft = text
                                    function handleEnter(event) {
                                        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
                                        if (shift !== chat.enterSends) {
                                            event.accepted = true
                                            if (!chat.busy && ollama.models.indexOf(chat.model) >= 0) window.sendMessage()
                                        } else event.accepted = false
                                    }
                                    Keys.onPressed: event => {
                                        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_V && chat.attachments.pasteFiles(!chat.busy)) event.accepted = true
                                        else event.accepted = false
                                    }
                                    Keys.onReturnPressed: event => handleEnter(event)
                                    Keys.onEnterPressed: event => handleEnter(event)
                                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: draftMenu.popup() }
                                    Controls.Menu {
                                        id: draftMenu
                                        Controls.MenuItem { text: i18n("Cut"); enabled: draft.selectedText.length > 0; onTriggered: draft.cut() }
                                        Controls.MenuItem { text: i18n("Copy"); enabled: draft.selectedText.length > 0; onTriggered: draft.copy() }
                                        Controls.MenuItem { text: i18n("Paste"); enabled: draft.canPaste || chat.attachments.visionAllowed; onTriggered: { if (!chat.attachments.pasteFiles(!chat.busy)) draft.paste() } }
                                        Controls.MenuItem { text: i18n("Select all"); onTriggered: draft.selectAll() }
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Controls.Button { text: i18n("Attach file"); icon.name: "mail-attachment"; enabled: !chat.busy; onClicked: fileDialog.open(); Controls.ToolTip.text: i18n("Attach up to 8 files: UTF-8 text (512 KiB), PDF/DOCX (20 MiB, extracted text up to 512 KiB), or up to 4 PNG/JPEG images (10 MiB each, 20 MiB combined) with a vision model."); Controls.ToolTip.visible: hovered }
                                Controls.ToolButton { text: i18n("Paste attachment"); icon.name: "edit-paste"; enabled: !chat.busy; onClicked: { if (!chat.attachments.pasteFiles()) { window.feedbackText = i18n("No files or image on the clipboard"); feedbackTimer.restart() } } }
                                Item { Layout.fillWidth: true }
                                Controls.Button {
                                    text: i18n("Send"); icon.name: "document-send"
                                    enabled: !chat.busy && !chat.attachments.busy && draft.text.trim().length > 0 && ollama.models.indexOf(chat.model) >= 0 && chat.storageError.length === 0 && chat.contextIssue.length === 0
                                    onClicked: window.sendMessage()
                                }
                            }
                        }
                    }
                    Controls.Label { textFormat: Text.PlainText;
                        Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; opacity: 0.75
                        text: window.feedbackText.length ? "✓ " + window.feedbackText : (ollama.local ? i18n("Local Ollama") : i18n("Content is sent to %1", ollama.endpoint)) + " · " + (chat.enterSends ? i18n("Shift+Enter for a new line") : i18n("Shift+Enter to send"))
                    }
                    Controls.Label { textFormat: Text.PlainText; visible: window.compact; text: ollama.statistics; font: Kirigami.Theme.smallFont; Layout.fillWidth: true; wrapMode: Text.NoWrap; elide: Text.ElideRight; fontSizeMode: Text.HorizontalFit; minimumPixelSize: 10; opacity: 0.75; Controls.ToolTip.visible: compactStatisticsHover.hovered; Controls.ToolTip.text: text; HoverHandler { id: compactStatisticsHover } }
                }
            }
        }
    }
    Controls.Dialog {
        id: settings
        objectName: "settingsDialog"
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width - 40, 570)
        title: i18n("Ollama connection")
        modal: true
        padding: Kirigami.Units.largeSpacing * 2
        standardButtons: Controls.Dialog.Cancel
        contentItem: Controls.ScrollView {
            id: settingsScroll
            rightPadding: Kirigami.Units.largeSpacing
            contentWidth: availableWidth
            implicitHeight: Math.min(settingsForm.implicitHeight, window.height - 170)
            ColumnLayout {
                id: settingsForm
                width: settingsScroll.availableWidth

            spacing: Kirigami.Units.largeSpacing
            Controls.Label { textFormat: Text.PlainText; text: i18n("Location") }
            Controls.ComboBox {
                id: location
                Layout.fillWidth: true
                model: [i18n("This computer"), i18n("Remote server")]
                onActivated: {
                    if (currentIndex === 0) address.text = "http://localhost:11434"
                }
            }
            Controls.Label { textFormat: Text.PlainText; text: i18n("Server URL or IP address") }
            Controls.TextField {
                id: address
                Layout.fillWidth: true
                placeholderText: "http://192.168.1.50:11434"
                Accessible.name: i18n("Ollama server URL")
                onAccepted: applyConnection.clicked()
            }
            Controls.Label { textFormat: Text.PlainText;
                text: i18n("Include http:// or https:// and an optional port. Use a bare server address without /api.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Controls.Label { textFormat: Text.PlainText;
                visible: location.currentIndex === 1
                text: i18n("Messages will be sent to this server. Prefer HTTPS beyond a trusted local network.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Controls.Label { textFormat: Text.PlainText; text: i18n("Context tokens (limited by reported model maximum)"); Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Controls.CheckBox {
                text: i18n("Use model maximum automatically")
                checked: chat.autoContext
                onToggled: chat.autoContext = checked
            }
            Controls.SpinBox { visible: !chat.autoContext; from: 1024; to: 262144; stepSize: 1024; editable: true; value: chat.contextTokens; onValueModified: chat.contextTokens = value; Accessible.name: i18n("Context tokens") }
            Controls.Label {
                visible: chat.autoContext
                textFormat: Text.PlainText
                text: chat.capabilities.maximumContext > 0 ? i18n("Context: %1 tokens", chat.contextTokens) : i18n("Model maximum unknown; using %1 tokens until available.", chat.contextTokens)
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Controls.Label { textFormat: Text.PlainText; text: i18n("Larger contexts use more memory. Kllama reserves 1,024 output tokens and 256 safety tokens, and never removes input to make it fit."); Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Controls.CheckBox {
                text: i18n("Enter sends; Shift+Enter inserts a new line")
                checked: chat.enterSends
                onToggled: chat.enterSends = checked
            }
            Controls.CheckBox {
                text: i18n("Allow viewing model reasoning (experimental)")
                checked: chat.showReasoning
                onToggled: chat.showReasoning = checked
            }
            Controls.Label {
                textFormat: Text.PlainText
                text: i18n("Requests thinking from models that report support. Available reasoning can be expanded in each reply and is saved with the conversation.")
                Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
            Controls.CheckBox {
                text: i18n("Animate activity indicators")
                checked: ollama.animations
                onToggled: ollama.animations = checked
            }
            Kirigami.InlineMessage {
                id: settingsError
                Layout.fillWidth: true
                visible: text.length > 0
                type: Kirigami.MessageType.Error
            }
            Controls.Button {
                id: applyConnection
                text: i18n("Save and test connection")
                icon.name: "network-connect"
                Layout.alignment: Qt.AlignRight
                onClicked: {
                    const error = ollama.configure(address.text, location.currentIndex === 0)
                    if (error.length > 0) settingsError.text = error
                    else settings.close()
                }
            }
            }
        }
    }    Controls.Dialog {
        id: about
        objectName: "aboutDialog"
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width - 40, 510)
        title: i18n("About Kllama")
        modal: true
        standardButtons: Controls.Dialog.Close
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.largeSpacing
            Kirigami.Icon { source: "qrc:/kllama.svg"; Layout.preferredWidth: 72; Layout.preferredHeight: 72; Layout.alignment: Qt.AlignHCenter }
            Kirigami.Heading { textFormat: Text.PlainText; text: "Kllama " + appInfo.version; level: 1; Layout.alignment: Qt.AlignHCenter }
            Controls.Label { textFormat: Text.PlainText; text: i18n("A native desktop frontend for Ollama, built with Qt and Kirigami."); Layout.fillWidth: true; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }
            Kirigami.Separator { Layout.fillWidth: true }
            Controls.Label { textFormat: Text.PlainText; text: i18n("Qt %1 · KDE Frameworks (KI18n) %2", appInfo.qtVersion, appInfo.frameworksVersion); Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Controls.Label { textFormat: Text.PlainText; text: i18n("Conversation storage"); font.bold: true }
            Controls.Label { textFormat: Text.PlainText; text: appInfo.dataDirectory; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
            Controls.Label { textFormat: Text.PlainText; text: i18n("Ollama runs separately. Kllama connects to the server you configure."); Layout.fillWidth: true; wrapMode: Text.WordWrap; opacity: 0.75 }
        }
    }
    Controls.Dialog {
        id: editDialog
        parent: Controls.Overlay.overlay; anchors.centerIn: parent
        width: Math.min(window.width - 40, 640)
        title: i18n("Edit and resend")
        modal: true; standardButtons: Controls.Dialog.Cancel
        contentItem: ColumnLayout {
            Controls.Label { textFormat: Text.PlainText; text: i18n("Resending replaces this message and all following messages in this conversation."); Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Controls.ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: 220
                Controls.TextArea { id: editedText; wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText; selectByMouse: true; Accessible.name: i18n("Edited message") }
            }
            Controls.Button { text: i18n("Replace following messages and send"); enabled: editedText.text.trim().length > 0; onClicked: { chat.editAndResend(window.editingRow, editedText.text); editDialog.close() } }
        }
    }
    Controls.Dialog {
        id: renameDialog
        parent: Controls.Overlay.overlay; anchors.centerIn: parent
        title: i18n("Rename conversation"); modal: true
        standardButtons: Controls.Dialog.Ok | Controls.Dialog.Cancel
        contentItem: Controls.TextField { id: renameText; implicitWidth: 350; Accessible.name: i18n("Conversation title") }
        onAccepted: chat.renameConversation(renameText.text)
    }
    Controls.Dialog {
        id: deleteDialog
        parent: Controls.Overlay.overlay; anchors.centerIn: parent
        width: Math.min(window.width - 40, 450)
        title: i18n("Delete conversation?"); modal: true
        standardButtons: Controls.Dialog.Yes | Controls.Dialog.No
        contentItem: Controls.Label { textFormat: Text.PlainText; text: i18n("This permanently deletes the current conversation and its draft."); wrapMode: Text.WordWrap }
        onAccepted: chat.deleteConversation()
    }
    Dialogs.FileDialog {
        id: fileDialog
        title: i18n("Attach documents, text or images")
        fileMode: Dialogs.FileDialog.OpenFiles
        nameFilters: [i18n("Documents and text (*.pdf *.docx *.txt *.md *.markdown *.cpp *.h *.c *.py *.js *.ts *.rs *.go *.json *.csv *.yaml *.yml *.toml *.sh *.log *.cmake)")].concat(chat.attachments.visionAllowed ? [i18n("Images (*.png *.jpg *.jpeg)")] : []).concat([i18n("All files (*)")])
        onAccepted: chat.attachments.addFiles(selectedFiles)
    }
    Dialogs.FileDialog {
        id: exportDialog
        title: i18n("Export conversation as Markdown")
        fileMode: Dialogs.FileDialog.SaveFile
        defaultSuffix: "md"
        nameFilters: [i18n("Markdown files (*.md)")]
        onAccepted: chat.exportMarkdown(selectedFile)
    }
    Controls.Dialog {
        id: previewDialog
        objectName: "attachmentPreviewDialog"
        padding: Kirigami.Units.largeSpacing * 2
        parent: Controls.Overlay.overlay; anchors.centerIn: parent
        width: Math.min(window.width - 40, 760)
        height: Math.min(window.height - 80, 580)
        modal: true; standardButtons: Controls.Dialog.Close
        contentItem: ColumnLayout {
            Controls.Label {
                visible: window.previewWarning.length > 0
                textFormat: Text.PlainText; text: window.previewWarning
                Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
            Controls.Label {
                visible: window.previewImageUrl.toString().length === 0 && window.previewSources.length > 0
                textFormat: Text.PlainText
                text: i18n("Verified source excerpts from this attachment. References in model answers are not independently verified.")
                Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
            RowLayout {
                visible: window.previewSources.length > 0
                Layout.fillWidth: true
                Controls.ComboBox {
                    id: sourceChoice
                    objectName: "sourceChoice"
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    model: [i18n("All extracted text")].concat(window.previewSources.map(row => row.label))
                    onActivated: previewText.text = currentIndex === 0 ? chat.attachments.preview(window.previewId) : chat.attachments.sourceText(window.previewId, currentIndex - 1)
                    Accessible.name: i18n("Source page or paragraph")
                }
                Controls.Button {
                    text: i18n("Open source"); enabled: sourceChoice.currentIndex > 0
                    onClicked: chat.attachments.openSource(window.previewId, sourceChoice.currentIndex - 1)
                    Controls.ToolTip.text: i18n("Open the managed original. PDF viewers may navigate to the selected page; DOCX viewers open the document.")
                    Controls.ToolTip.visible: hovered
                }
            }
            Controls.Label {
                visible: window.previewImageUrl.toString().length > 0
                textFormat: Text.PlainText; text: previewText.text
                Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
            Image {
                visible: window.previewImageUrl.toString().length > 0
                source: window.previewImageUrl
                Layout.fillWidth: true; Layout.fillHeight: true
                fillMode: Image.PreserveAspectFit; asynchronous: true
                sourceSize.width: 1200; sourceSize.height: 1200
                Accessible.name: i18n("Attached image preview")
            }
            Controls.ScrollView {
                id: previewScroll
                visible: window.previewImageUrl.toString().length === 0
                Layout.fillWidth: true; Layout.fillHeight: true
                contentWidth: availableWidth
                Controls.TextArea {
                    id: previewText
                    objectName: "attachmentPreviewText"
                    readOnly: true; selectByMouse: true; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap
                    rightPadding: previewScroll.Controls.ScrollBar.vertical.width + Kirigami.Units.largeSpacing
                    Accessible.name: i18n("Extracted file text included in context")
                }
            }
        }
    }

    Controls.Dialog {
        id: capabilityDialog
        parent: Controls.Overlay.overlay; anchors.centerIn: parent
        width: Math.min(window.width - 40, 560)
        title: i18n("Model capabilities"); modal: true; standardButtons: Controls.Dialog.Close
        contentItem: ColumnLayout {
            Controls.Label { textFormat: Text.PlainText; text: chat.model; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }
            Controls.Label { textFormat: Text.PlainText; text: chat.capabilities.summary; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Controls.Label { textFormat: Text.PlainText; text: chat.capabilities.detail; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Controls.Label { textFormat: Text.PlainText; text: i18n("Attach UTF-8 text or extract PDF/DOCX body text with any chat model. PNG/JPEG images require confirmed vision support. Audio, video and OCR are not implemented."); Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Controls.Button { text: i18n("Refresh capabilities"); enabled: !chat.busy && chat.capabilities.state !== "loading"; onClicked: chat.capabilities.refresh() }
        }
    }
    Controls.Dialog {
        id: contextDialog
        parent: Controls.Overlay.overlay; anchors.centerIn: parent
        width: Math.min(window.width - 40, 560)
        title: i18n("Context budget"); modal: true; standardButtons: Controls.Dialog.Close
        contentItem: ColumnLayout {
            Controls.Label { textFormat: Text.PlainText; text: chat.contextSummary; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Controls.Label { textFormat: Text.PlainText; text: chat.contextIssue; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Controls.Label { textFormat: Text.PlainText; text: i18n("The estimate uses one token per UTF-8 byte plus message overhead. It is deliberately conservative, not an exact tokenizer count. Images use a 4,096-token allowance each; actual vision token usage varies by model and image. Configure context size in Connection settings; larger contexts use more memory."); Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
    }

}
