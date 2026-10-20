# Kllama

A native KDE frontend for Ollama, built with C++20, Qt 6, and Kirigami.

![Kllama showing Markdown chat, PDF and image attachments, and Ollama statistics](docs/images/kllama.png)

*Native KDE interface with demonstration content.*

## M4: Documents and images

Kllama now includes an About dialog, streaming chat, full conversation history in requests, Stop, retry/regenerate, and edit-and-resend with explicit replacement of later messages. Conversations, per-chat models, and drafts are stored in SQLite; interrupted responses retain partial text. The sidebar supports title search, rename, and deletion. Only one app instance may write to a data directory at a time.

Assistant responses render selectable Markdown: headings, emphasis, lists, quotes, tables, links, inline code, and fenced code with basic keyword/string/comment highlighting. User messages remain plain text. Copy Markdown preserves the raw response, and code copy excludes fences. Export Markdown saves the conversation and attached filenames; it does not bundle the attachment contents. Enter sends and Shift+Enter inserts a newline; reverse this in Connection settings. Copy/paste context menus and standard keyboard shortcuts work in the composer and transcript. History navigation, connection/model changes, and edits are disabled during generation; Stop first. You can draft your next message while waiting.

Local statistics sample accessible Ollama process RSS, including model runners and other descendants, every five seconds off the UI thread. The sum includes other Ollama sessions, may double-count shared pages, and is not GPU memory. Remote statistics count outgoing chat and capability-inspection JSON payloads, received API payloads, and their combined recent transfer rate, excluding HTTP/TLS overhead and other clients. Counters reset when saving a connection or restarting Kllama; Refresh reuses the current counter session.

The sidebar shows animated CPU/GPU **model placement** bars for each loaded model, refreshed every five seconds through `GET /api/ps`. Percentages follow the model memory split reported by Ollama (`size` and `size_vram`), as in `ollama ps`; they do not measure processor activity or total hardware capacity. This works with local and remote endpoints without installing an agent. No loaded models and unavailable/unsupported metrics are shown explicitly. Remote payload counters include these polling responses. True remote CPU/GPU activity and host RAM monitoring would require a separate authenticated host telemetry service; the standard Ollama endpoint does not provide those measurements.

Requests time out after three minutes without incoming bytes. Stream records are capped at 2 MiB and total responses at 16 MiB. Partial output is checkpointed approximately once per second and saved on Stop/normal exit. Reasoning is optional: enable **Connection settings → Allow viewing model reasoning (experimental)** to request thinking from models that report support. Available reasoning appears behind a per-reply **Show reasoning** button and can be expanded, selected or copied during processing and afterward. It is stored separately with the conversation, capped at 1 MiB per reply, and excluded from subsequent model history and Markdown answer exports. It shares the existing 1,024-token generation budget with the answer. Turning the option off hides stored reasoning and stops capturing new reasoning; existing saved reasoning remains until the conversation is deleted. Models that do not supply reasoning cannot display it.

With this option off (the default), Kllama requests non-thinking output and does not save reasoning. If Ollama sends thinking events anyway, the pulsing-dot card reports the phase without showing the text. Before such events it says “Waiting for model…”. Disabling animations leaves a static indicator. Tools are not executed yet.

## Attach files and inspect models

Use **Attach file**, drop local files onto the chat, or paste copied file URLs and clipboard images with Ctrl+V or **Paste attachment**. Ordinary clipboard text continues to paste into the composer. Preparation runs on two background workers with observable stages, cancellation, retry and preview.

Up to eight attachments are allowed per conversation:

| Input | Limits and behavior |
|---|---|
| UTF-8 text, code, Markdown, JSON, CSV | 512 KiB per file |
| PDF | 20 MiB input, 200 pages, 512 KiB extracted text; page references |
| DOCX | 20 MiB input, 512 KiB extracted text; section and paragraph references |
| PNG/JPEG | Confirmed vision model required; up to four images, 10 MiB each and 20 MiB combined; maximum 8,192 pixels per side and 12 megapixels |

**Preview** shows exactly the extracted text or the attached image. PDF/DOCX previews let you choose a source excerpt and open the managed original; PDF page navigation depends on the viewer. These source locations come from extraction. Locations generated in assistant answers are not independently verified.

PDF extraction uses `pdfinfo` and `pdftotext` from **poppler-utils**. Scanned/image-only PDFs need OCR, which is deferred; partially scanned PDFs show which pages lack text. Encrypted PDFs are rejected. DOCX extraction uses **unzip** and reads body paragraphs and table text from `word/document.xml`; headers, footers, footnotes, endnotes, comments, deleted text, text boxes and embedded images are excluded and the preview reports this limitation. Paragraph numbers follow document-body XML order (including table paragraphs), rather than Word list numbering. ZIP archives are never unpacked into the filesystem. Archive XML is capped at 8 MiB, entity declarations are rejected, and each extraction subprocess has a 45-second time limit. Missing tools produce an actionable error. Existing text chat remains available.

PNG/JPEG contents and dimensions are validated before use. Models must explicitly report `vision`; a model name such as Qwen is not sufficient. Images are sent as base64 in Ollama's chat `images` array, and remain available in follow-up questions. Switching to a text-only or unknown model blocks sending until images are removed or a vision model is selected. Image previews only open the managed local attachment; Markdown responses still cannot fetch images. Audio, video, OCR and other native file formats are deferred.

Attachments are staged in the composer until Send succeeds locally, then clear from that area and appear on their original user message. Click a sent attachment to preview it; retry preparation or remove it from that message when needed. Follow-up questions include its content once at the original point in chat history, rather than attaching it again to each new message. Ollama still receives the full history on each request, so earlier file content can be transmitted again as history. Regenerate/retry uses the original message attachments and leaves staged draft files alone. Editing an earlier message preserves its attachments and discards copies belonging only to the removed later messages.

Kllama retains an owner-readable/writable managed original and extracted text, so removing the external source does not break a reopened conversation. Removing an attachment deletes its managed copy when no message uses it; deleting the conversation deletes all its managed attachments. Original source files are never changed. Older conversation-wide attachments without ownership metadata are associated with the earliest user message when reopened, preserving their history context.

Model inspection uses `POST /api/show`, not a chat prompt. The summary and Model details report metadata from the current endpoint/model. Results are cached in memory by endpoint, model, and digest (or modification identity), and refreshed after identity changes; explicit inspection refresh discards cached results. Late replies from a previous selection are ignored. Failed or missing metadata is shown as unknown, never as confirmed native attachment support. Reported vision support enables the implemented PNG/JPEG path. PDF/DOCX extraction works independently with any text-generation model. Other reported modalities do not enable unimplemented input paths.

## Context budget and Markdown resources

The main chat hides timers and the context-budget summary. Completed requests show small server-reported input/output token counts beneath the corresponding user/assistant bubbles; older or incomplete replies without metrics show no count. Input counts cover the full request (including history and attachments); output counts include reasoning when enabled. Counts are saved with the conversation and excluded from subsequent model messages. Context controls remain in Connection settings; a compact attention notice opens details when a send is blocked.

The context setting defaults to 4,096 tokens and is capped by the model maximum when metadata reports one. Kllama supplies this explicit `num_ctx` and a 1,024-token output cap to Ollama. The input estimate conservatively counts UTF-8 bytes plus message/file overhead, reserves 1,024 output tokens and 256 safety tokens, and blocks sending when the estimate will not fit. Details explains how to remove/shorten content, start a new conversation, or increase context in Connection settings. Drafts and history remain intact on a blocked send. Images add a 4,096-token allowance each, which is an estimate rather than a guaranteed upper bound: actual image tokens vary by model, image size and tiling. Increase context (or enable automatic maximum context) if needed. Model maxima may be unknown and the estimate is not a tokenizer count; Kllama never truncates input itself.

Connection settings → **Use model maximum automatically** selects the context limit reported by the current model. If unavailable, Kllama uses the saved manual limit until discovery succeeds. Turning it off restores the manual limit. Maximum context can substantially increase memory use.

Markdown is parsed with HTML disabled. Image resources, including local/data URLs, are removed before presentation; there is no automatic remote-image loading. Only explicit user activation opens HTTP/HTTPS links. Incomplete fenced blocks remain readable during streaming. Rendering updates are throttled, delegates stay in place, and selected text is restored where the same text remains after reformatting.

## Build on Fedora 44

```bash
sudo dnf install cmake gcc-c++ ninja-build extra-cmake-modules \
  qt6-qtbase-devel qt6-qtdeclarative-devel kf6-kirigami-devel \
  kf6-ki18n-devel kf6-qqc2-desktop-style poppler-utils unzip
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target register-desktop
./build/bin/kllama
```

Minimum declared versions: Qt 6.6 and KDE Frameworks 6.8. The primary acceptance environment is the Fedora 44 / Plasma 6 / Wayland machine described in [ROADMAP.md](ROADMAP.md). Exact installed development-package versions and validation results are recorded in [VALIDATION.md](VALIDATION.md).

### Desktop integration for development builds

`register-desktop` installs a per-user launcher and icon under `${XDG_DATA_HOME:-~/.local/share}`. Its launcher points to this build's absolute executable path. This also lets the host desktop portal resolve `org.kllama.Kllama` when launching `build/bin/kllama` directly. Re-run the target after moving the build. Registration is explicit; ordinary builds and tests do not modify your desktop entries.

If you see `Could not register app ID: App info not found`, run `cmake --build build --target register-desktop`, then restart Kllama. No sudo is needed. A normal installed package supplies the entry instead. To remove this development registration, delete only `applications/org.kllama.Kllama.desktop` and `icons/hicolor/scalable/apps/org.kllama.Kllama.svg` beneath your user data directory.

Optional staged install (no root needed; staging does not register with your running desktop):

```bash
DESTDIR="$PWD/build/stage" cmake --install build --prefix /usr
```

The build installs a desktop entry, scalable icon, and AppStream metadata. RPM packaging is planned for the stable-release milestone. `org.kllama.Kllama` is a provisional application identifier, not a claim of domain ownership or KDE project membership.

## Connect

Run Ollama separately (`ollama serve` when no service is already running). Kllama defaults to `http://localhost:11434`. Open Connection settings for a remote hostname or IP, including scheme and port. Local mode only accepts `localhost`, `127.0.0.1`, or `[::1]` to avoid showing this computer's RAM for a remote server. No credentials are stored in URLs, redirects are not followed, and remote authentication is not implemented.

Use **Download model…** in the sidebar to enter a model name such as `qwen3.8:latest` (availability depends on the registry). The dialog first sends an empty-name request to check that the download API is accessible without downloading anything. This is an endpoint-access check; the actual request can still fail because of private-model authorization, unavailable names, or server storage permissions. Denied or unverifiable access disables Download until a successful recheck.

Downloads run on the configured local or remote Ollama server, using its storage and network. The dialog shows server status and current-layer byte progress; Cancel request aborts this client's request, and Ollama may retain partial data for a retry. Choose **Minimize** (or close the dialog) to keep downloading while you chat. A compact sidebar progress bar remains visible; **Show download…** reopens the details, including during generation. Completion and errors remain visible on the sidebar button. Downloads continue while Kllama is running; exiting the app ends its request. Completion refreshes the model list. Remote traffic statistics count API messages, not model weights transferred by the server. Remote authentication is not implemented. Kllama never starts a server or downloads models automatically.

For an explicit PDF-plus-image inference check (choose an installed vision model):

```bash
KLLAMA_LIVE_TEST=1 KLLAMA_LIVE_MODEL=gemma3:12b \
  QT_QPA_PLATFORM=offscreen ./build/bin/m4test liveDocumentAndVision
```

## Translation

User-facing strings use KI18n in C++ and QML. `po/Messages.sh` is the extraction entry point for KDE translation tooling (`XGETTEXT`/`podir`); `ki18n_install(po)` installs language catalogs when supplied. No translated catalogs are included yet.

## Stored data

M4 keeps schema version 2: attachment metadata is extended without changing the database layout. Extracted text and source maps are rebuilt from managed originals; base64 image data is not stored in conversation messages or metadata.

The About dialog shows the data directory. Its `conversations.sqlite` file contains unencrypted chats and drafts with owner-only database permissions. Schema version 2 stores attachment metadata and recovers in-progress messages as interrupted on startup. M2 schema-1 databases migrate automatically after a consistent `conversations-v1-backup.sqlite` backup is made; newer unknown schemas and invalid message data are preserved and reported as storage errors. Delete a conversation to remove its active records and managed attachments; this is logical deletion, not guaranteed forensic erasure from SQLite free pages or backups. Migration backups remain until you explicitly remove them.

## Development checks

The QtTest suites use isolated settings/data directories and loopback HTTP fixtures. They cover model discovery, split/coalesced UTF-8 stream records, malformed/truncated responses, HTTP errors, cancellation, history assembly, regeneration, edits, storage recovery, code copying, keyboard sending/newlines, clipboard round trips, and selection retention during streaming. M3 tests also cover capability caching and invalidation, missing metadata, stale replies, file validation/cancellation/duplicates, managed-copy recovery/deletion, schema migration, context blocking, Markdown isolation/highlighting, file-URL paste/drop, and file preview. M4 tests generate text/scanned/encrypted PDFs, DOCX archives and PNG/JPEG images, and cover source excerpts, limits, missing tools, cancellation, capability gating, clipboard images, request encoding and restoration. `qpdf` is an optional test dependency for the encrypted-PDF fixture. Offscreen smoke tests open settings and About. Native Wayland acceptance is recorded in [VALIDATION.md](VALIDATION.md).

Normal tests never contact the user's Ollama server. An explicit local inference check is available:

```bash
KLLAMA_LIVE_TEST=1 KLLAMA_LIVE_MODEL=gemma4:e4b \
  QT_QPA_PLATFORM=offscreen ./build/bin/m3test liveAttachment
```

`--smoke-test` opens settings and About then exits. `--screenshot <absolute-path.png>` captures the window and exits. `KLLAMA_NO_AUTOCONNECT=1` skips the initial API request. Use isolated `XDG_DATA_HOME` and `XDG_CONFIG_HOME` directories for manual UI test runs.
