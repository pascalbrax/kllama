# Kllama roadmap

Kllama is a native KDE desktop frontend for Ollama: chat with local models, copy and paste comfortably, read Markdown responses, and ask questions about attached files.

Status: M1–M4 are implemented and built; automated and native Wayland checks pass, with cross-application Wayland clipboard transfer still awaiting manual acceptance. See VALIDATION.md for results and remaining manual checks. Milestones are ordered by dependency rather than committed dates.

## Primary development and acceptance target

Build first for the user's HP ZBook Firefly 16 inch G11 Mobile Workstation PC, with this user-reported configuration:

| Component | Target |
| --- | --- |
| Operating system | Fedora Linux 44, x86_64 |
| Desktop | KDE Plasma 6.7.5 |
| KDE Frameworks | 6.31.0 |
| Qt | 6.11.2 |
| Kernel | 7.2.11-200.fc44.x86_64 |
| Display session | Wayland |
| CPU | Intel Core Ultra 7 155H, 22 logical processors |
| Memory | 64 GiB installed, 62.2 GiB usable |
| Graphics | Intel Arc |

These versions define the initial acceptance environment, rather than mandatory exact-version dependencies. Select minimum dependency versions from APIs actually used and verify against the installed development packages when implementation begins.

- Prioritize a native Fedora build and RPM packaging for this machine; add Flatpak after the native workflow works reliably.
- Validate clipboard, selection clipboard, drag-and-drop, file dialogs, focus, notifications, and scaling in the actual Plasma Wayland session.
- Use Qt Quick's supported graphics path and test visual effects on this Intel Arc GPU. Keep effects lightweight and provide reduced-motion behavior; do not require a discrete GPU.
- Keep extraction, indexing, and networking off the UI thread. Use bounded background concurrency with conservative defaults and configurable limits rather than consuming every logical processor.
- Show available model metadata and measured runtime statistics when supplied by Ollama. Do not infer a safe model size or context length from system RAM alone; leave memory headroom for Plasma, attachments, and other applications.
- Treat inference acceleration as Ollama's responsibility. Confirm the installed Ollama backend and actual Intel GPU support before advertising GPU acceleration; Kllama must remain usable with CPU inference.
- Include long conversations, multiple attachments, streaming during ingestion, and model loading in on-machine responsiveness checks. Record first-response latency and throughput separately from UI responsiveness.
- Document the Fedora package prerequisites, native launch steps, and tested runtime versions as implementation progresses. Wider distributions and mobile layouts are secondary to this desktop target.

## Product and UI

- Sidebar: new chat, searchable conversation history, rename and delete actions.
- Main area: conversation title, model selector, connection status, selectable messages, and message actions.
- Composer: multiline text, attachment button, attachment chips, Send and Stop. Enter sends; Shift+Enter inserts a newline, with a setting to reverse this behavior.
- Optional details panel: attached files, extracted-text preview, context usage, and generation settings.
- Settings: Ollama address, default model, appearance, storage, and keyboard shortcuts.
- Add a discoverable connection settings menu with local/remote mode, editable URL or hostname/IP and port, validation, connection test, and saved endpoint. Default to `http://localhost:11434`.
- Show connection statistics instead of OS/desktop/session labels in the GUI. For local Ollama, show measured process RAM when accessible and identify its scope; report unavailable measurements explicitly and keep model-residency estimates separate. For remote Ollama, show Kllama's sent/received application bytes per connection session and recent transfer rate, with a defined reset when saving a connection or restarting the app; do not imply these include all network overhead or other clients. Never display remote process RAM without a supported measurement source.
- Follow Plasma colors, fonts, scaling, light/dark themes, and accessibility conventions.

## Visual polish and processing feedback

Make activity understandable at a glance, with a calm, polished KDE appearance. Visual feedback is part of each milestone, starting with the first working chat.

- Give Kllama a distinctive llama icon, welcoming empty states, generous spacing, readable typography, softly rounded message and attachment surfaces, and restrained Plasma accent colors.
- Add subtle hover/press feedback, attachment insertion/removal transitions, sidebar transitions, and a brief success checkmark for copy and completed tasks. Keep streaming text stable instead of animating every token.
- Show a compact activity strip near the composer with the current operation, elapsed time, and Stop/Cancel when available. Expand it into task details for concurrent or multi-step work.
- Use explicit states: queued, running, waiting for user, completed, failed, and cancelled. Pair icons and colors with text; keep failures visible with a useful Retry or Details action.
- Name observable work: “Connecting to Ollama”, “Waiting for model response”, “Generating response”, “Extracting report.pdf”, “Embedding chunks 12 of 40”, or “Searching attached files”. Show model-loading or thinking states only when the backend actually reports them; never invent internal reasoning or progress.
- Use determinate progress for known totals such as download bytes, processed pages, or embedding chunks. Otherwise use a quiet busy indicator and elapsed time; avoid fabricated percentages and unsupported time estimates.
- Keep per-file progress and errors on attachment chips/cards. Let users inspect stages such as queued → extracting → preparing context → ready, with removal or cancellation where supported.
- For future MCP/tool tasks, show a collapsible timeline with server/tool name, argument summary, waiting-for-approval state, execution status, duration, and result/error details. Clearly distinguish model generation from external tool execution.
- Show connection loss, prolonged waiting, and interrupted work explicitly. State what is known and offer recovery actions without claiming a slow task has failed.
- Keep background jobs visible in a compact task indicator while the user switches chats. Offer opt-in desktop completion notifications when the window is unfocused; avoid duplicate in-app alerts.
- Respect reduced-motion preferences and provide an animation toggle. Avoid continuous decorative animation, maintain contrast, and announce meaningful status changes to screen readers without announcing every token.

**Acceptance:** for every long-running action, the user can identify what is happening, whether progress is measurable, whether input is required, and how to cancel or recover. Effects remain smooth with long chats and multiple files.

## Technical direction

- Qt 6, QML/Qt Quick, KDE Frameworks 6 and Kirigami for the UI; C++ for application services; CMake/ECM for builds.
- Asynchronous Qt networking for Ollama; incrementally parse streamed JSON records, including records split across network reads.
- SQLite for conversations and attachment metadata; application data directory for managed attachment copies and extracted text.
- Keep UI, Ollama transport, conversation storage, Markdown presentation, and ingestion behind separate interfaces.
- Expose a shared task model to QML with operation ID, owning chat/file, state, stage, optional completed/total units, timestamps, cancellation capability, and error/recovery details. Drive all progress UI from actual service events and throttle frequent updates to keep rendering responsive.
- Default to `http://localhost:11434`; make remote endpoints an explicit setting and show the destination before sending attached content.
- Ollama supplies inference. Kllama performs document extraction, context assembly, and later retrieval; attaching a PDF does not mean sending a PDF directly to the chat API.

## Milestones

### 1. Application foundation — v0.1

- [x] Create the CMake project, application metadata, desktop entry, icon placeholder, and translation setup.
- [x] Verify the target machine's Qt/KF development packages and establish a reproducible Fedora 44 native build.
- [x] Build the Kirigami shell with sidebar, transcript, composer, and settings.
- [x] Configure the Ollama endpoint, check connectivity, and list installed models using `GET /api/tags`.
- [x] Provide useful empty states for unavailable Ollama and no installed models.
- [x] Establish visual tokens, Kllama branding, reusable status/progress components, and reduced-motion behavior.

**Done when:** Kllama launches on Plasma, follows its theme, and lets the user select an installed model or understand a connection failure.

### 2. Everyday chat and copy/paste — v0.2

- [x] Add an About Kllama dialog with application version, framework versions, and the conversation-storage location.

- [x] Send conversation history to `POST /api/chat` and display streaming responses.
- [x] Add Stop, retry failed requests, regenerate, and edit-and-resend; editing an earlier message explicitly replaces its subsequent conversation path.
- [x] Persist conversations, selected model, drafts, and interrupted-response status across restarts.
- [x] Support mouse selection, Ctrl+C/Ctrl+V, context menus, and ordinary multiline/Unicode text without altering whitespace.
- [x] Add Copy message and Copy code actions, with brief confirmation; copy code without Markdown fences.
- [ ] Handle Linux selection clipboard where supported and verify behavior on Wayland. Support is implemented; synthetic Qt events cannot validate compositor clipboard ownership, so cross-application transfer and middle-click paste remain a manual acceptance check.
- [x] Keep selection and scroll position stable during streaming; auto-scroll only while already following the latest message.
- [x] Add the activity strip with response-waiting/generation states, elapsed time, Stop, and clear completed/interrupted/error feedback.

**Done when:** a user can chat, stop generation, copy a response, paste multiline code, and reopen the conversation without losing content.

### 3. Markdown and text-file ingestion — v0.3 MVP

- [x] Discover capabilities for the selected model through Ollama's `POST /api/show` metadata, including `capabilities`, rather than asking the model in a chat prompt or guessing from its name. Track confirmed, unsupported, and unknown capabilities; show inspection progress and a retryable failure state. Qwen and other families must be evaluated per installed model variant and endpoint.
- [x] Cache capability results by endpoint and model identity/digest, invalidate after model changes or pulls, and discard stale responses when switching models or endpoints. Inspect on selection and allow an explicit refresh.
- [x] Show supported input types beside the model selector and in attachment controls. Distinguish model-native inputs from files Kllama can extract into text; a text-only model can still use extracted text/code files. Advertised vision support does not imply arbitrary PDF, DOCX, audio, or video support, nor does it specify every image encoding or size limit.
- [x] Gate native attachment submission on confirmed model capability, an implemented Ollama API input path, and Kllama's supported formats/limits. When support is unknown or absent, explain whether text extraction, a compatible model, or removal is available; do not silently drop or convert attachments. Revalidate existing attachments when the model changes.
- [x] Render headings, emphasis, lists, links, quotes, tables, inline code, and fenced code blocks with syntax highlighting.
- [x] Offer raw Markdown copying and export conversations as Markdown.
- [x] Keep incomplete Markdown usable during streaming and preserve selectable text.
- [x] Disable active HTML and automatic remote-image fetching; open links only through an explicit user action.
- [x] Attach UTF-8 text, Markdown, source code, JSON, and CSV through a file picker, drag-and-drop, or pasted file URLs. Paste ordinary text into the composer as text.
- [x] Show filename, size, ingestion status, preview, and Remove on each attachment.
- [x] Extract and prepare content in background jobs with cancellation, encoding errors, duplicate detection, and file-size limits.
- [x] Delimit each file in the model context with its filename; treat file content as reference data.
- [x] Budget space for history, files, and output using model metadata plus a conservative token estimate. If content will not fit, explain the limit and let the user remove or shorten it; do not silently truncate.
- [x] Define attachment lifetime: retain managed copies with the conversation and remove them when the conversation is deleted.
- [x] Add animated drop targets, attachment readiness feedback, and per-file extraction/preparation progress with actionable errors.

**Done when:** a user can inspect the selected model's reported input capabilities, attach a text/code file, inspect what will be included, ask about it, read a formatted answer, and copy its code accurately. Missing capability metadata never appears as confirmed attachment support.

### 4. Document support — v0.4 (implemented)

- [x] Add PDF text extraction with page references and DOCX extraction with section/paragraph references.
- [x] Preserve useful document structure and surface extraction failures, encrypted files, and empty documents.
- [x] Explain that scanned PDFs need OCR; defer OCR to a later milestone.
- [x] Use M3 capability discovery to enable image picking, paste, and drop only for confirmed vision-capable models, including compatible Qwen variants. Validate actual file content, supported encodings, dimensions, and size; send images using Ollama's documented chat `images` input and explain incompatibility before sending.
- [x] For other model-native file modalities, inspect the available metadata and endpoint contract before adding a handler. Expose only input paths implemented and tested in Kllama; keep PDF/DOCX text extraction available independently of native document support. Audio/video handlers remain deferred unless explicitly brought into scope.
- [x] Offer source excerpts and file/page links for included context; distinguish generated references from verified source locations.

**Done when:** representative text PDFs and DOCX files produce inspectable text and source locations without freezing the UI.

### 5. Large-document retrieval — v0.5

- [ ] Split extracted documents into overlapping chunks with stable source locations and content hashes.
- [ ] Generate embeddings through `POST /api/embed` using a user-selected embedding model.
- [ ] Store vectors locally, retrieve relevant chunks, and display the excerpts supplied to the chat model.
- [ ] Reindex when content, chunking rules, or embedding models change; remove indexes when attachments are deleted.
- [ ] Keep direct attachment context available for small files; explain when retrieval supplies only selected excerpts.
- [ ] Show indexing stages, chunk counts, cancellation, and retrieval activity through the shared task model.

**Done when:** questions about a document larger than the context window retrieve relevant passages with inspectable references, and deletion removes its stored index.

### 6. Stable desktop release — v1.0

- [ ] Add model pull progress, cancellation, model details, and deletion with confirmation.
- [ ] Complete keyboard navigation, screen-reader labels, high-DPI behavior, localization, and shortcut configuration.
- [ ] Add history search, export/import, storage controls, and documented data migrations.
- [ ] Ship and validate a native Fedora 44 RPM first, then add a Flatpak with file-portal integration and document Ollama setup. Resolve local MCP process access separately if MCP support is introduced.
- [ ] Validate remote-endpoint configuration and credential storage if authentication is supported; use KWallet for secrets.
- [ ] Publish installation instructions, supported formats, known limits, and troubleshooting guidance.
- [ ] Polish transitions and empty states, add the background task view and optional completion notifications, and verify animation performance.

**Done when:** a fresh install can connect to Ollama, complete the MVP workflow, survive restarts and network failures, and manage its stored data predictably.

## Verification gates

- Transport: split/combined stream records, cancellation, HTTP errors, disconnects, malformed responses, and model changes.
- Clipboard: Unicode, indentation, multiline paste, code-only copy, and Wayland selection behavior.
- Rendering: incomplete code fences, long tables/lines, selection during streaming, and no automatic external resource loads.
- Ingestion: oversized/binary/invalid-encoding files, cancelled extraction, scanned/encrypted PDFs, context overflow, and complete deletion.
- Capabilities: text-only and vision-capable variants (including Qwen examples), missing/unknown metadata, failed inspection, endpoint/model switches during inspection, cache invalidation after model replacement, and attachments that become incompatible after a model switch. Test that unknown flags never enable an unimplemented file-input path.
- Persistence: interrupted generation, draft recovery, schema migration, and attachment references after restart.
- Desktop: keyboard-only workflow, screen-reader inspection, Plasma light/dark themes, scaling, and a clean packaged installation.
- Primary target: perform acceptance on the specified Fedora 44 / Plasma Wayland / Intel Arc machine, including native RPM installation and measured UI responsiveness under chat and ingestion workloads.
- Feedback: verify every task state, unknown totals, simultaneous tasks across chats, cancellation races, disconnections, reduced motion, non-color status cues, and screen-reader announcements. Check that animations and progress updates do not disrupt typing, selection, or scrolling.

## Deferred scope

OCR, folder watching, voice chat, autonomous tools, web browsing, plugins, cloud account integration, and cross-device sync come after the core desktop workflow is reliable.

## Reference documentation

- KDE Kirigami: https://develop.kde.org/docs/getting-started/kirigami/
- KDE C++ application setup: https://develop.kde.org/docs/getting-started/kirigami/setup-cpp/
- Ollama chat and streaming: https://docs.ollama.com/api/chat
- Ollama installed models: https://docs.ollama.com/api/tags
- Ollama model details and capabilities: https://docs.ollama.com/api-reference/show-model-details
- Ollama vision inputs: https://docs.ollama.com/capabilities/vision
- Ollama embeddings: https://docs.ollama.com/api/embed
