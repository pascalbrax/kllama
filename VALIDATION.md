# M1 validation

## Target and toolchain

Validated on the Fedora Linux 44 x86_64 machine on 2026-10-18:

- Qt base 6.11.2-3.fc44; Qt declarative 6.11.2-2.fc44.
- Kirigami, KI18n, and ECM 6.31.0-1.fc44.
- GCC 16.2.1; CMake/Ninja Debug build.
- Native Wayland window using the installed KDE controls style.

## Passed

- Native CMake configuration and C++/QML compilation, with the deprecated translation API replaced and QML resources embedded.
- `ctest --test-dir build --output-on-failure`: all three test targets pass (AppStream test, connection QtTest suite, QML shell/settings smoke test).
- Connection suite: endpoint validation, sorted/deduplicated models, empty and malformed responses, selection/endpoint persistence in isolated settings, cancellation/replacement, and connection refusal. Loopback fixtures require network permission outside the execution sandbox.
- Offscreen startup and settings popup load without runtime diagnostics. Screenshots visually inspected for shell and dialog layout. Secondary text contrast improved after inspection.
- Native Wayland screenshot: actual application connects to `http://localhost:11434`, lists two installed models (`gemma3:12b`, `gemma4:e4b`), and displays measured local Ollama process RSS. No inference or model download performed.
- Staged install at `build/stage/usr`: executable, desktop entry, icon, and AppStream metadata present.
- Desktop entry passes `desktop-file-validate`; SVG and AppStream XML pass `xmllint`; translation extraction script passes `bash -n`.

## Limits and remaining manual acceptance

- Interactive keyboard/screen-reader inspection, theme changes, fractional scaling, resizing at all supported sizes, and clipboard behavior still need hands-on acceptance. The QML smoke test checks shell/settings startup, not those interactions.
- Remote statistics are covered through the loopback response fixture, not a separately hosted remote Ollama service. RSS is summed across accessible local Ollama processes, may double-count shared pages, and is not GPU memory.
- `qmllint` reports unqualified context-property references for injected `ollama` and KI18n functions; these resolve at runtime. Native startup emits no corresponding errors.
- Manual AppStream validation warns that no homepage is supplied and notes missing developer/content-rating fields. The CMake AppStream test passes; publishing metadata still needs completion. No homepage, developer identity, or distribution license has been invented.
- Chat, ingestion, persistent conversation history, and RPM packaging remain in their later roadmap milestones.

## Commands

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 6
ctest --test-dir build --output-on-failure
./build/bin/kllama
```

Screenshots are local build outputs: `build/m1-wayland.png` and `build/m1-settings.png`. `--smoke-test` briefly opens settings and exits; `--screenshot <absolute-path.png>` captures the application window and exits. Set `KLLAMA_NO_AUTOCONNECT=1` for UI-only inspection.

## Development desktop registration fix

The direct build-tree launch initially lacked a desktop entry in the host session's application directories, causing the portal to reject `org.kllama.Kllama`. Added an explicit `register-desktop` CMake target that installs a per-user launcher (pointing at the absolute build binary) and the icon. Tested registration in an isolated `XDG_DATA_HOME`, validated the generated launcher, then registered it in the user's application directory. A native `build/bin/kllama --smoke-test` subsequently exited successfully with no portal warning or other output. Normal builds/tests remain free of registration side effects.


# M2 and About validation

Built version 0.2.0 on the same Fedora 44 / Qt 6.11.2 / KF 6.31 target.

## Passed

- Native build includes Qt SQL/SQLite storage and the About dialog. Offscreen screenshots of About and populated chat were inspected; application dialogs and the message/composer layout fit their intended surfaces.
- Four CTest targets: AppStream, connection, chat, and QML smoke. The default suite explicitly skips opt-in live inference.
- Chat HTTP fixture tests cover fragmented UTF-8, multiple records per read, full follow-up history, HTTP/malformed/truncated/stream errors, retry, regenerate, edit-and-resend truncation, Stop with partial output, draft/model/title persistence, deletion, and crash recovery from a streaming checkpoint.
- UI interaction test exercises About, Enter versus Shift+Enter, reversed key preference, selectable response text retained across subsequent stream updates, clipboard copy/paste round trips with the offscreen platform, and compact resizing. The message model updates rows in place during streaming rather than resetting delegates.
- Native Wayland UI test passes for keyboard actions and streaming selection, with a screenshot at `build/m2-wayland-chat.png`.
- Opt-in real inference test using `gemma4:e4b`: the ChatSession service received `Kllama works`, with a backend-reported completion of 4 tokens at approximately 9.6 tokens/s. Test conversations were confined to temporary data and settings directories.
- Remote traffic accounting includes actual QNetworkReply upload progress and downloaded API payload bytes, with a shared per-connection counter and periodic display updates.

## Remaining acceptance and diagnostic limits

- Cross-application Wayland clipboard transfer and middle-click primary-selection paste require real compositor input and remain manual. Synthetic QtTest keys passed inside Qt but did not update the compositor clipboard in the native test. The native test therefore explicitly omits clipboard round trips; the offscreen test still requires them to pass. Wayland's selection API requires an input-event serial: https://wayland.freedesktop.org/docs/html/apa.html#protocol-spec-wl_data_device . This is the basis for the test limitation, rather than evidence that the manual workflow has passed.
- Native synthetic-input tests emitted QTextCursor position diagnostics, and source-loaded UI tests emitted Kirigami toolbar-incubation diagnostics; the assertions passed and no QQmlEngine warnings were emitted. The standalone application smoke check must be checked separately from test-fixture diagnostics.
- Screen-reader inspection, cross-application clipboard acceptance, fractional scaling, and theme changes remain hands-on checks.
- M2 shows plain text, including literal Markdown fences. Markdown rendering and file ingestion are M3 work. No model-management, tools, or MCP execution was added.

Final standalone Wayland `--smoke-test` (with isolated XDG data/config) opened settings and About, connected to local Ollama, captured `build/m2-native-about.png`, and exited with no diagnostic output. Final CTest result: 4/4 targets passed in approximately 3.2 seconds. The staged install was refreshed for 0.2.0.

## Scrollbar spacing and toolbar lifecycle fix

Reserved a stable right-side gutter for transcript bubbles and history rows. The composer retains its platform-provided scrollbar (including KDE placement) and reserves text padding beside it. Replaced dynamically incubated Kirigami page actions with persistent Qt toolbar buttons, keeping settings, refresh, About, and compact History available.

The UI regression now generates a long, wrapped response and a multiline draft, verifies that their text bounds end before the scrollbar lane at desktop and 640-pixel widths, and fails on ToolBarLayout/ActionToolBar lifecycle warnings. Offscreen and native Wayland runs passed; the native compact screenshot was visually inspected. No toolbar lifecycle warnings occurred. Synthetic Wayland selection tests still emit the separately documented QTextCursor diagnostics. Final suite: 4/4 CTest targets passed.

# M3 validation — version 0.3.0

Implemented model metadata inspection, selectable Markdown presentation, Markdown export, and managed UTF-8 text/code attachments on the same Fedora/Qt/KF target.

## Passed

- Native CMake/Ninja build; staged executable, desktop entry, icon, and updated AppStream metadata.
- Final regression suite: 5/5 CTest targets passed (AppStream, connection, chat/UI, M3 services, and QML smoke), approximately 5.7 seconds. Normal tests skip the opt-in local-inference checks.
- Capability tests use server-reported features rather than model names, including text-only Qwen-named and vision-capable differently named fixtures. Cache hits, explicit refresh, digest changes, stale split replies, endpoint switches, missing metadata, errors, and recovery are covered.
- File tests cover invalid/truncated UTF-8, binary content, empty files, PDF rejection, size limits, cancellation/retry, duplicate paths/content, clipboard file URLs, and managed-copy restoration after deleting the external source. Deleting a conversation removes its managed attachment directory.
- Budget tests verify that an oversized input is blocked before changing the draft/history, then fits after increasing context within the reported model maximum. Requests include explicit context/output limits and delimited reference text, without exposing original absolute source paths to the server.
- Schema-1 migration preserves the old conversation/draft and creates a consistent backup before adding schema-2 attachment metadata.
- Markdown tests cover headings, emphasis, quotes, lists, tables, code highlighting, incomplete code fences, omitted local/remote images, disabled HTML, rejected unsafe links, and raw Markdown export.
- UI checks cover file-URL paste into attachments while leaving ordinary text paste intact, Qt drag/drop routing, preview text, rendered tables and highlighted wrapping code, selection preservation during streaming, and settings fit at compact size. Native Wayland M3 interaction checks passed without diagnostic warnings; screenshots at `build/m3-wayland-final.png` were visually inspected.
- The final standalone Wayland binary connected to local Ollama, opened settings/About, captured `build/m3-native-about.png`, and exited with no diagnostic output. Test data/config were isolated beneath the build directory.
- Real local attachment inference using `gemma4:e4b`: a temporary `verification.txt` containing the number 731 was attached through ChatSession; the model answered `731`. Its inspected maximum context was 131,072; the request used the configured 4,096 context. No user conversations or files were altered by this test.

## Deliberate limits and remaining hands-on checks

- Eight attachments per conversation, at most 512 KiB each; two background workers. Files are included as extracted UTF-8 reference text. Native images, PDF/DOCX extraction, OCR, audio/video and retrieval remain later milestones, even when model metadata advertises vision or another modality.
- The context estimate is conservative, not an exact tokenizer count. Model maximum may be unknown. The application never drops input to make a request fit; backend tokenization and memory behavior remain Ollama's responsibility.
- Cross-application Wayland clipboard ownership and primary-selection paste still require real compositor input. Automated file-URL paste and clipboard round trips run offscreen; native Wayland checks cover Qt drag/drop routing and the rest of the rendered workflow. Full Dolphin-to-app drag/drop and native file-picker interaction remain hands-on acceptance.
- Markdown highlighting is a basic common-language keyword/string/comment treatment. Remote images and raw HTML remain disabled; HTTP/HTTPS links require explicit activation.
- Schema migration backups are retained; deleting a conversation removes active data and managed copies, not independent backups or forensic traces.

Final paste guard: file URLs pasted during generation are handled with an explicit “Stop generation before attaching files” notice rather than bypassing disabled attachment controls; ordinary clipboard text still goes into the draft. The guard and the full 5-target suite pass after the final rebuild, and the staged install was refreshed.

### Ollama runner RAM correction

The local RSS sampler now follows Ollama descendants instead of relying on legacy runner names. On the target machine the server used about 79 MiB while its `llama-server` child used about 10 GiB; the previous counter omitted that child. A synthetic proc-tree regression test covers modern and legacy runners, nested children, unrelated llama-server processes, and disappearing status files. The display explicitly identifies resident RAM and excludes GPU allocations.

### Automatic model context setting

Added an opt-in persistent setting to use the current model’s reported context maximum, including maxima above the manual control’s 262,144-token range. Unknown metadata falls back to the saved manual limit; disabling automatic selection restores that limit. Added regression coverage for API request values, metadata changes, unknown maxima, and persistence. Build and all five CTest targets pass, including automatic-context request and persistence checks. Socket fixture tests ran with local networking permitted.
