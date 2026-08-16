# Implementation Analysis: 10-cli-chat-ocr-workflow-cmds

> Scope note: This analysis maps the **LOCKED** Refinement Iteration 2 requirements to the
> existing codebase and defines **WHAT** must change structurally, logically, and architecturally.
> It deliberately does **not** prescribe **HOW** (no implementation code).

## 1. Architectural Impact & Data Flow

### Affected Subsystems
- **CLI (`src/cli/`)** — the primary surface of change. Currently a stub (`QCoreApplication` + a
  single `QCommandLineParser` exposing only `--verbose`). It gains two workflow subcommands
  (`chat`, `ocr`), global workflow routing, per-workflow option parsing, settings bootstrap,
  streaming-to-stdout, and (for `ocr`) folder scanning, confirmation, and markdown output.
- **Backend (`src/backend/core/`)** — limited, additive changes:
  - Register the missing `cli/temperature` and `cli/system-prompt-file` settings in `CoreSchema`.
  - Add a **PDF page renderer** (new component) that turns a PDF into in-memory page images via QtPDF.
  - Add a **stateful thinking/reasoning stripper** (new component) shared by both workflows.
  - Fix the OpenAI request builder so the system message is sent with role `system` (see §3).
  - Link `Qt6::Pdf` into the backend library.
- **Build system** — add `Qt6::Pdf` to the backend link set; add new CLI and backend source files to
  their respective `CMakeLists.txt`; register new unit tests.
- **Frontend (`src/frontend/`)** — **no changes** (explicitly out of scope).

### Data Flow Changes

**Global routing (both workflows)**
```
main() -> QGuiApplication (NOT QCoreApplication)
       -> global workflow parser selects `chat` | `ocr` (unknown/absent -> usage + non-zero exit)
       -> per-workflow parser (isolated option set; foreign options are unrecognized)
       -> SettingsManager bootstrap: register CoreSchema, load persisted settings
       -> resolve effective config (options override settings; settings are fallbacks)
       -> run workflow (Qt event loop drives streaming)
```

**Chat workflow**
```
parse --prompt (mandatory) / --system / --temperature
  -> resolve system prompt: --system (text-or-file) else cli/system-prompt-file (hard error if unresolvable)
  -> resolve temperature: --temperature else cli/temperature
  -> read language-model/endpoint from settings (hard error if empty)
  -> text_to_text_client { endpoint, temperature, system_prompt, user_prompt }
  -> print header (temperature + FULL system prompt + FULL user prompt)
  -> send_request(); run event loop
  -> incremental_chunk -> thinking_stripper -> stdout (flushed)
  -> completion -> exit 0 ; error_occurred -> report + non-zero exit
```

**OCR workflow**
```
parse --source (mandatory) / --system / --temperature / --output / --yes
  -> resolve system prompt + temperature (same rules as chat)
  -> read ocr-model/endpoint from settings (hard error if empty)
  -> classify --source:
       file  -> validate supported extension (.jpg/.jpeg/.png/.pdf, case-insensitive) -> process directly
       folder-> non-recursive top-level scan for .jpg/.jpeg/.png/.pdf
               -> print discovered list
               -> confirm unless (--yes OR stdin not a TTY) ; decline -> exit
  -> for each source (sequential):
       image -> image_to_text_client::send_request(file_path, system_prompt)
       pdf   -> pdf_renderer -> [QImage per page, in memory]
                -> for each page: image_to_text_client::send_request(QImage, system_prompt)
       -> incremental_chunk -> thinking_stripper -> stdout (flushed)
       -> accumulate full text (per page for pdf)
  -> write one markdown file per source (base name + .md):
       default alongside source ; --output redirects ; general/overwrite governs existing target
       pdf: pages joined with `---` / `**page N**` separators
  -> exit 0 on success ; non-zero on error
```

### New Patterns / Structural Additions
- **Two-level command parsing**: a global workflow parser plus one isolated parser per workflow
  (mutual option isolation is achieved by each parser only knowing its own options).
- **Settings bootstrap in the CLI**: the CLI currently never touches `SettingsManager`; it must now
  construct one, register `CoreSchema`, load persisted settings, and read fallback values.
- **`QGuiApplication` requirement**: PDF rendering via QtPDF requires a `QGuiApplication` instance.
  The CLI must switch from `QCoreApplication` to `QGuiApplication`. This is the single most
  consequential structural change and must be validated early (see §3, §4).
- **Backend gains two reusable, UI-free components** (PDF renderer, thinking stripper) consistent
  with the "backend = pure logic shared by frontend + CLI" rule.
- **Streaming in a console app**: the Qt event loop must run while the network reply streams;
  stdout must be flushed promptly so text appears as received. The existing 250ms coalescing timer
  in `model_client_base` already batches `incremental_chunk` emissions and is acceptable for the
  "as received" requirement.

## 2. Component & File Impact Map

### `./src/cli/main.cpp`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Switch the application object from `QCoreApplication` to `QGuiApplication` (required for QtPDF rendering in the `ocr` workflow).
  - [ ] Replace the single flat `QCommandLineParser` with a **global workflow parser** that recognizes exactly two positional subcommands: `chat` and `ocr`.
  - [ ] Add a dispatch step that hands off to the `chat` or `ocr` workflow entry point based on the selected subcommand.
  - [ ] Preserve top-level `--help`/`-h` and `--version` behavior; top-level `--help` lists the two workflows.
- **Logic Modifications Required:**
  - [ ] No workflow subcommand, or an unknown subcommand → print usage and exit non-zero.
  - [ ] Map each workflow's return/exit status to the process exit code (0 success, non-zero failure).
  - [ ] Keep `--verbose` available at the top level (applies to both workflows).

### `./src/cli/chat_workflow.hpp` / `./src/cli/chat_workflow.cpp`
- **Type of Change:** Create
- **Structural Changes:**
  - [ ] A `chat` workflow type (or free function pair) exposing: an option parser limited to `--prompt` (mandatory), `--system` (optional), `--temperature` (optional), plus `--help`; and an `execute` entry point returning an exit code.
  - [ ] Wire the workflow to `text_to_text_client`, `SettingsManager`, the thinking stripper, and stdout.
- **Logic Modifications Required:**
  - [ ] Enforce `--prompt` as mandatory (missing → error, non-zero exit).
  - [ ] Validate `--temperature` as a real value in `[0.0, 2.0]` (reject non-numeric/out-of-range).
  - [ ] Resolve the system prompt: `--system` (text-or-file) if present, else the file named by `cli/system-prompt-file`; **hard error** (clear message, non-zero exit, no request sent) when the effective system prompt is missing/empty/unreadable.
  - [ ] Resolve temperature: `--temperature` else `cli/temperature`.
  - [ ] Read `language-model/endpoint` from settings; hard error if empty.
  - [ ] Before streaming, print the effective temperature, the **full** system prompt, and the **full** user prompt (no truncation).
  - [ ] Stream `incremental_chunk` through the thinking stripper to stdout (flushed); on `completion` exit 0; on `error_occurred` report and exit non-zero.
  - [ ] Produce **no** output file.

### `./src/cli/ocr_workflow.hpp` / `./src/cli/ocr_workflow.cpp`
- **Type of Change:** Create
- **Structural Changes:**
  - [ ] An `ocr` workflow type (or free function pair) exposing: an option parser limited to `--source` (mandatory), `--system` (optional), `--temperature` (optional), `--output` (optional folder), `--yes`/`-y` (flag), plus `--help`; and an `execute` entry point returning an exit code.
  - [ ] Wire the workflow to `image_to_text_client`, the PDF renderer, `SettingsManager`, the thinking stripper, stdout, and markdown file output.
- **Logic Modifications Required:**
  - [ ] Enforce `--source` as mandatory (missing → error, non-zero exit).
  - [ ] Validate `--temperature` in `[0.0, 2.0]`; resolve system prompt and temperature with the same rules as `chat` (including the hard-error case).
  - [ ] Read `ocr-model/endpoint` from settings; hard error if empty.
  - [ ] Classify `--source`: existing supported **file** → process directly (no confirmation); **folder** → non-recursive top-level scan for `.jpg`/`.jpeg`/`.png`/`.pdf` (case-insensitive), excluding subfolders and non-matching extensions.
  - [ ] For a folder: print the discovered list, then prompt to continue **unless** `--yes`/`-y` is set **or** stdin is not an interactive TTY (in which cases proceed automatically). A negative answer → process nothing and exit.
  - [ ] Reject an unsupported file extension for the file case with a clear error.
  - [ ] Process sources **sequentially**; for each, stream OCR text to stdout (flushed) via the thinking stripper.
  - [ ] For a PDF: render each page to an in-memory image (no per-page files on disk) and OCR page by page.
  - [ ] Write one markdown file per source (base filename + `.md`): default alongside the source, or into `--output` when given; existing-target handling follows `general/overwrite`.
  - [ ] For a PDF, join pages with a `---` line, a newline, a `**page N**` line, and a newline (N = 1-based page number).
  - [ ] Report per-source render/parse/model errors per the error policy (§3) and reflect them in the exit code.

### `./src/cli/cli_common.hpp` / `./src/cli/cli_common.cpp`
- **Type of Change:** Create
- **Structural Changes:**
  - [ ] Shared CLI helpers used by both workflows:
    - Text-or-file prompt resolution (existing readable file → contents; otherwise literal text).
    - System-prompt fallback resolution against `cli/system-prompt-file` (including the hard-error case).
    - Temperature resolution/validation against `[0.0, 2.0]` with `cli/temperature` fallback.
    - Endpoint resolution from settings (with the empty-endpoint hard error).
    - Markdown output writer (target path resolution: alongside-source vs `--output`; `general/overwrite` policy; PDF page-separator assembly).
    - Interactive-stdin (TTY) detection for the confirmation decision.
- **Logic Modifications Required:**
  - [ ] Centralize the "option overrides setting" resolution so `chat` and `ocr` behave identically.
  - [ ] Keep these helpers free of workflow-specific control flow so each workflow stays thin.

### `./src/cli/CMakeLists.txt`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add the new CLI sources (`chat_workflow.*`, `ocr_workflow.*`, `cli_common.*`) to the `dir2md_cli` target.
- **Logic Modifications Required:**
  - [ ] Ensure the target still links `dir2md_backend` (which will now transitively provide `Qt6::Pdf` and the new backend components).

### `./src/backend/core/core_schema.cpp`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Register the two currently-unregistered CLI settings inside `CoreSchema::registerSchemas()`:
    - `cli/temperature` — numeric (double) with bounds `[0.0, 2.0]`, sensible default (e.g. `0.7`).
    - `cli/system-prompt-file` — string, default empty.
- **Logic Modifications Required:**
  - [ ] Ensure the registered category matches the `cli` key prefix per `SettingsManager::registerSchema` category-consistency rules (otherwise registration is silently rejected).
  - [ ] After registration, `SettingsManager::get()` must return the defaults so the CLI fallbacks work even when no persisted value exists.

### `./src/backend/core/pdf_renderer.hpp` / `./src/backend/core/pdf_renderer.cpp`
- **Type of Change:** Create
- **Structural Changes:**
  - [ ] A new backend component that renders a PDF file into a sequence of in-memory page images (one image per page), using QtPDF (`QPdfDocument`/`QPdfPage`).
  - [ ] Expose a render entry point that returns the per-page images (or an `expected`/error result on failure) and a page-count query.
  - [ ] Add the new sources to `./src/backend/core/CMakeLists.txt`.
- **Logic Modifications Required:**
  - [ ] Open the PDF and report a clear error when the file cannot be opened/parsed.
  - [ ] Render each page to an in-memory image only (no per-page image files written to disk).
  - [ ] Report a clear error for a page that fails to render (handled per the per-source error policy).
  - [ ] Be safe to call only after a `QGuiApplication` exists (document this precondition).

### `./src/backend/core/thinking_stripper.hpp` / `./src/backend/core/thinking_stripper.cpp`
- **Type of Change:** Create
- **Structural Changes:**
  - [ ] A new stateful backend component that consumes streamed text fragments and emits only the non-thinking text, while signaling that a thinking/reasoning block is in progress.
  - [ ] Must recognize both `think` and `reasoning` block tags (open and close), in a case-appropriate manner.
  - [ ] Add the new sources to `./src/backend/core/CMakeLists.txt`.
- **Logic Modifications Required:**
  - [ ] Be **stateful across chunk boundaries**: a tag may be split across multiple streamed fragments, so partial-tag state must be carried between calls.
  - [ ] Suppress the inner content of any in-progress `think`/`reasoning` block.
  - [ ] While a block is in progress, surface a `thinking ...` indicator (emitted once per block, not repeated per fragment).
  - [ ] Pass through all text outside thinking blocks unchanged.
  - [ ] Provide a reset so a single instance can be reused across sequential sources (OCR folder case) or re-created per request.

### `./src/backend/core/model.cpp` (and `model.hpp` if the signature changes)
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Correct the OpenAI request construction so the **system** message is emitted with role `system` rather than the current behavior of assigning role `user` to every message. This likely requires the request builder to receive role information (e.g., a structured message list) instead of a flat string list, or an explicit system-message parameter.
- **Logic Modifications Required:**
  - [ ] Ensure `text_to_text_client` passes its system prompt as a system-role message and the user prompt as a user-role message in the OpenAI path.
  - [ ] Preserve the existing native (llama.cpp) path behavior, which concatenates prompts with delimiters and has no role concept.
  - [ ] Keep `image_to_text_client`'s vision payload (single user message with text + image parts) unchanged.

### `./src/backend/CMakeLists.txt`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add `Qt6::Pdf` to the `dir2md_backend` link libraries (currently only `Core`/`Network`/`Gui`/`fzy`).
- **Logic Modifications Required:**
  - [ ] Confirm `Qt6::Pdf` is available in the Qt installation used by the build (it is a separate Qt6 component that must be found/installed).

### `./CMakeLists.txt`
- **Type of Change:** Modify (likely)
- **Structural Changes:**
  - [ ] Add `Pdf` to the `find_package(Qt6 ... COMPONENTS ...)` list so `Qt6::Pdf` is resolvable.
- **Logic Modifications Required:**
  - [ ] Verify the configured Qt installation actually provides the Pdf module; if not, this is a build-environment prerequisite to resolve before implementation.

### `./test/backend/core/thinking_stripper_test.cpp`
- **Type of Change:** Create
- **Structural Changes:**
  - [ ] A new `QTest` unit test for the thinking stripper, registered via `qtest_add_test` in `./test/backend/core/CMakeLists.txt`.
- **Logic Modifications Required:**
  - [ ] Cover: plain text passthrough; a complete `think` block; a complete `reasoning` block; a tag **split across multiple fragments** (chunk-boundary case); nested/adjacent blocks; the `thinking ...` indicator emitted once per block; and reset/reuse behavior.

### `./test/backend/core/pdf_renderer_test.cpp`
- **Type of Change:** Create
- **Structural Changes:**
  - [ ] A new `QTest` unit test for the PDF renderer, registered via `qtest_add_test`.
- **Logic Modifications Required:**
  - [ ] Cover: a valid multi-page PDF yields the correct page count and non-empty in-memory images; a missing/invalid file yields a clear error; no per-page files are written to disk. (Requires a small fixture PDF and a `QGuiApplication`-capable test harness.)

### `./test/backend/core/CMakeLists.txt`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Register the new test sources (`thinking_stripper_test.cpp`, `pdf_renderer_test.cpp`) with `qtest_add_test`.

### Optional: `./test/cli/` (CLI integration tests)
- **Type of Change:** Create (optional)
- **Structural Changes:**
  - [ ] A lightweight integration test harness that invokes the CLI argument parsing / workflow entry points in-process to assert exit codes and option isolation (e.g., an OCR-only option rejected under `chat`, and vice versa; missing mandatory option; bad temperature).
- **Logic Modifications Required:**
  - [ ] Keep these tests free of real network calls (assert on parsing/validation and on the resolved effective config, not on live model responses).

## 3. Boundary & Edge Case Analysis

### Error Handling & Exit Codes
- **Argument errors** (missing workflow, unknown workflow, missing mandatory `--prompt`/`--source`,
  unrecognized/foreign option, non-numeric or out-of-range `--temperature`) → clear message to
  stderr + **non-zero** exit. Option isolation is structural: each workflow parser only registers its
  own options, so a foreign option is simply unrecognized.
- **System-prompt hard error** (Q1): when `--system` is omitted and `cli/system-prompt-file` is
  unset/empty/missing/unreadable → clear error + non-zero exit, **no request sent**. Applies to both
  workflows.
- **Endpoint missing**: `language-model/endpoint` (chat) or `ocr-model/endpoint` (ocr) empty →
  clear error + non-zero exit (a request to an empty URL would otherwise fail opaquely).
- **File I/O errors**: unreadable `--source`/prompt file, unsupported extension (file case),
  unwritable/missing `--output` folder, PDF that fails to open/parse/render → clear per-source
  error. For a folder batch, a failing source is reported and processing continues to the next
  source; the run's exit code is non-zero if any source failed.
- **Model/network errors** (mid-stream `error_occurred`) → report + non-zero exit.
- **Success** → exit 0.

### Security & Permissions
- **Path handling**: prompt text-or-file resolution and `--source`/`--output` path handling should
  use the same defensive path normalization the backend already uses (`QDir::cleanPath`,
  absolute-path checks). The `--output` folder is created if absent; output filenames are derived
  from the source base name (no user-controlled path segments beyond the chosen folder).
- **No new credentials/scopes**: the CLI reuses existing endpoint settings; no new auth surface.
- **Prompt sanitization**: `text_to_text_client::sanitize_prompt` (zero-width neutralization of
  `<|...|>`) already applies to the user prompt in the OpenAI path; no change required, but the
  system-prompt role fix (§2, `model.cpp`) must not bypass it.

### Performance / Scale Impact
- **Sequential processing only** (no concurrency) — a folder with many sources is processed one at
  a time; each OCR request is a separate streamed network call. No new indexes or heavy in-memory
  structures beyond per-PDF page images.
- **PDF memory**: all page images for one PDF are held in memory for the duration of that PDF's
  processing (per the requirement "keep only in memory"). For very large/high-DPI PDFs this is the
  main memory consideration; pages are rendered and consumed one at a time, so peak memory is
  bounded by the renderer's per-page retention strategy (prefer releasing a page image after its
  OCR completes).
- **Streaming latency**: the existing 250ms coalescing timer batches `incremental_chunk` emissions;
  this is acceptable for "print as received." stdout must be flushed per emission so text is not
  buffered by the console.
- **Folder scan**: non-recursive, top-level only — bounded by directory entry count; extension
  matching is case-insensitive.

### Notable Structural Risks / Edge Cases
- **`QGuiApplication` switch (highest risk)**: QtPDF rendering requires a `QGuiApplication`. The CLI
  must move from `QCoreApplication` to `QGuiApplication`. This must be validated early (configure +
  build + a smoke render) because it affects the whole CLI target and the deploy step.
- **`Qt6::Pdf` availability**: the module must be present in the Qt installation and added to both
  `find_package` and the backend link set. If absent, it is a build-environment prerequisite.
- **OpenAI role bug**: as shipped, the system prompt would be sent as a `user` message. The request
  builder must be corrected so the system prompt uses role `system`; this is a correctness fix that
  the chat workflow depends on.
- **Chunk-boundary stripping**: thinking tags can be split across streamed fragments; the stripper
  must be stateful and must not drop or leak partial tags at boundaries.
- **`thinking ...` indicator semantics**: emitted once per in-progress block (not per fragment), and
  must not interfere with the accumulated text that is written to the markdown file (the file should
  contain the stripped text, not the indicator).
- **Non-interactive stdin**: when stdin is not a TTY (or `--yes` is set), the folder confirmation is
  skipped and processing proceeds automatically (Q6).
- **Empty folder scan**: a folder with no matching sources → print that none were found and exit
  cleanly (no error, no processing).
- **Overwrite policy**: existing target `.md` with `general/overwrite` false → do not clobber (skip
  with a notice); with `true` → overwrite. This keeps behavior deterministic and aligned with the
  existing setting's semantics.
- **PDF markdown structure (Q5)**: each page is preceded by a `---` line, a newline, a `**page N**`
  line, and a newline (N = 1-based). Apply the separator before every page (including page 1) for a
  uniform structure.
- **Settings category consistency**: the two new `cli/*` schemas must use a category consistent with
  the `cli` key prefix, or `registerSchema` will silently reject them.

## 4. Verification Checklist

- [ ] Verify the CLI builds and runs after switching to `QGuiApplication` (no regression to the `chat` path, which does not need GUI).
- [ ] Verify `Qt6::Pdf` is found by CMake and linked into `dir2md_backend`; a minimal PDF render smoke test succeeds.
- [ ] Verify `dir2md-cpp.exe` with no/unknown subcommand prints usage and exits non-zero.
- [ ] Verify `chat` rejects an OCR-only option (e.g. `--source`) and `ocr` rejects a chat-only option (e.g. none currently exclusive, but `--output`/`--yes` are OCR-only) with a clear "unrecognized option" error and non-zero exit.
- [ ] Verify top-level `--help` lists both workflows and exits 0; per-workflow `--help` prints that workflow's options and exits 0.
- [ ] Verify `chat` with no `--prompt` errors (missing mandatory) and exits non-zero.
- [ ] Verify `--temperature` outside `[0.0, 2.0]` or non-numeric is rejected (both workflows).
- [ ] Verify `cli/temperature` and `cli/system-prompt-file` are registered (defaults returned by `SettingsManager::get` when unset) and that the CLI uses them as fallbacks.
- [ ] Verify system-prompt resolution: `--system` as an existing file path uses file contents; as free text uses the text verbatim; `--prompt` as a file path uses file contents.
- [ ] Verify the hard-error case: `--system` omitted and `cli/system-prompt-file` unset/empty/missing → clear error, non-zero exit, no request sent (both workflows).
- [ ] Verify the chat header prints the effective temperature, the **full** system prompt, and the **full** user prompt before streaming (no truncation).
- [ ] Verify chat streams the model response to stdout incrementally and exits 0 on completion; a model/network error yields a non-zero exit.
- [ ] Verify the OpenAI request now sends the system prompt with role `system` (inspect the constructed payload / a mock endpoint).
- [ ] Verify `ocr` with a supported file processes it directly (no confirmation prompt); an unsupported file extension is rejected.
- [ ] Verify `ocr` with a folder lists only top-level `.jpg`/`.jpeg`/`.png`/`.pdf` (case-insensitive), excluding subfolders and other extensions.
- [ ] Verify the folder confirmation: interactive stdin prompts; `--yes`/`-y` skips it; non-interactive stdin proceeds automatically; a "no" answer processes nothing and exits.
- [ ] Verify OCR processes sources sequentially and streams each source's text to stdout as received.
- [ ] Verify the thinking/reasoning stripper: inner content suppressed, `thinking ...` shown while in progress, text outside blocks printed normally, and correct behavior when a tag is split across chunks (unit test).
- [ ] Verify a PDF is rendered page-by-page to in-memory images only (no per-page files on disk) and OCR'd sequentially.
- [ ] Verify one markdown file per source with the source's base filename + `.md`: `photo.png` → `photo.md`, `doc.pdf` → `doc.md`.
- [ ] Verify default output is alongside the source; `--output <folder>` redirects all output there (base filename preserved).
- [ ] Verify existing-target handling follows `general/overwrite` (false → not clobbered; true → overwritten).
- [ ] Verify a multi-page PDF's markdown uses the `---` / `**page N**` separator (1-based) between pages.
- [ ] Verify exit codes: fully successful run → 0; any argument/file/model error → non-zero.
- [ ] Verify new unit tests (`thinking_stripper_test`, `pdf_renderer_test`) are registered with CTest and pass.
