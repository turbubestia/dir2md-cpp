# Implementation Plan: 10-cli-chat-ocr-workflow-cmds

> **Traceability Linkage**
> [Analysis Reference](./10-cli-chat-ocr-workflow-cmds.plan.analysis.md)
>
> Every Phase and Step below cites the specific Section of the analysis document that justifies it.
> Section IDs used throughout: **§1** (Architectural Impact & Data Flow), **§2** (Component & File
> Impact Map, with per-file subsections), **§3** (Boundary & Edge Case Analysis), **§4** (Verification
> Checklist). This plan defines **HOW** to execute the validated **WHAT** from the analysis. It contains
> no implementation code or pseudocode — only descriptive, step-by-step instructions.

## How to Read This Plan

- Phases are strictly chronological and dependency-ordered: build prerequisites first, then backend
  components (each independently testable), then CLI shared helpers, then the two workflows, and finally
  a dedicated testing/coverage phase.
- Each Phase lists its **Analysis Reference**, ordered **Steps**, an **Exit Criterion**, and a
  **Validation Command**.
- Follow the project's strict conventions (see `copilot-instructions.md`): **snake_case** for all
  identifiers (variables, methods, functions, classes, namespaces), **trailing return types** on every
  function, and **no `[[nodiscard]]`** decorators. QML file names use CamelCase (not relevant here — no
  QML changes).
- The backend remains a pure QtCore/QtGui logic library shared by frontend and CLI; no QWidget or UI
  elements may be introduced into `src/backend/`.

---

## Phase 1 — Build System & Qt6::Pdf Wiring

**Analysis Reference:** §2 (`./CMakeLists.txt`, `./src/backend/CMakeLists.txt`), §3 (Notable Structural
Risks: "Qt6::Pdf availability", "QGuiApplication switch").

This phase is a prerequisite for the PDF renderer and must land before any code that touches `Qt6::Pdf`.
The configured Qt installation (`C:/Qt/6.11.1/msvc2022_64`) has been verified to provide the Pdf module
(`lib/cmake/Qt6Pdf` and the `QPdfDocument` header both exist), so this is a wiring task, not an install.

### Steps
1. In the top-level `./CMakeLists.txt`, add `Pdf` to the existing `find_package(Qt6 ... COMPONENTS ...)`
   list (currently `Core Network Quick QuickControls2 Test`) so the `Qt6::Pdf` target is resolvable.
2. In `./src/backend/CMakeLists.txt`, add `Qt6::Pdf` to the `target_link_libraries(dir2md_backend PUBLIC ...)`
   set (currently `Qt6::Core Qt6::Network Qt6::Gui fzy`). Use `PUBLIC` so the CLI target inherits it
   transitively, matching §2's note that the CLI links `dir2md_backend` and receives `Qt6::Pdf` through it.
3. Reconfigure and build to confirm the new component resolves and links cleanly with no source changes yet.

### Exit Criterion
The project configures and builds successfully with `Qt6::Pdf` found by CMake and linked into
`dir2md_backend`; no other files changed.

### Validation Command
```
cmake --preset debug
cmake --build --preset debug
```

---

## Phase 2 — Backend: Register CLI Settings

**Analysis Reference:** §2 (`./src/backend/core/core_schema.cpp`), §3 (Notable Structural Risks: "Settings
category consistency"), §4 (checklist item on `cli/temperature` and `cli/system-prompt-file` registration).

The two CLI settings are already declared as key constants in the anonymous namespace of
`core_schema.cpp` (`cli::system_prompt_file`, `cli::temperature`) but are **not** registered. This phase
registers them so `SettingsManager::get()` returns their defaults when no persisted value exists.

### Steps
1. In `CoreSchema::registerSchemas()`, add a `manager.registerSchema(...)` call for `cli/temperature`:
   - key `cli/temperature`, a human title, a description, category **`CLI`** (must normalize to the `cli`
     key prefix per the category-consistency rule in `SettingsManager::registerSchema`; an empty category
     would also auto-derive correctly, but an explicit `CLI` is preferred for clarity), default value
     `0.7`, type `QMetaType::fromType<double>()`, min `0.0`, max `2.0`.
2. Add a second `manager.registerSchema(...)` call for `cli/system-prompt-file`:
   - key `cli/system-prompt-file`, a title, a description, category **`CLI`**, default value empty string,
     type `QMetaType::fromType<QString>()`, no min/max.
3. Confirm the category string normalizes to the `cli` prefix (the manager compares
   `toNormalizedFormat(category)` against the normalized key prefix; a mismatch is silently rejected with a
   warning). Use the same casing style as the existing categories in this file.

### Exit Criterion
Both keys are present in `SettingsManager::schemas()` after `CoreSchema::registerSchemas()` runs, and
`get("cli/temperature")` returns `0.7` and `get("cli/system-prompt-file")` returns an empty string when no
persisted value is set.

### Validation Command
```
cmake --build --preset debug
ctest --test-dir build/cmake-debug -R "backend.core.setting_manager_test"
```
(Extend the existing settings-manager test coverage in Phase 9 to assert the two new keys' defaults.)

---

## Phase 3 — Backend: Stateful Thinking/Reasoning Stripper

**Analysis Reference:** §2 (`./src/backend/core/thinking_stripper.hpp/.cpp`, `./test/backend/core/
thinking_stripper_test.cpp`), §3 (Notable Structural Risks: "Chunk-boundary stripping", "`thinking ...`
indicator semantics"), §4 (checklist item on the stripper).

Create a new stateful, UI-free backend component that consumes streamed text fragments and emits only the
non-thinking text. It must be safe to reuse across sequential sources (OCR folder case) via a reset.

### Steps
1. Create `./src/backend/core/thinking_stripper.hpp` declaring a class `thinking_stripper` in namespace
   `dir2md::backend`, with trailing return types and no `[[nodiscard]]`.
2. Define the public surface:
   - A `process(const QString &fragment)` method returning the text to emit for that fragment. It must
     suppress the inner content of any in-progress `think`/`reasoning` block, pass through all text outside
     blocks unchanged, and emit the `thinking ...` indicator **exactly once** on the fragment where a block
     first opens (not repeated on subsequent fragments while the block is open).
   - An `is_thinking()` const query reporting whether a block is currently in progress.
   - A `reset()` method clearing all internal state so one instance can be reused for the next source/request.
3. Implement `./src/backend/core/thinking_stripper.cpp`:
   - Recognize both `think` and `reasoning` block tags, open and close forms, in a case-appropriate manner.
   - Be **stateful across chunk boundaries**: carry partial-tag state between calls so a tag split across
     multiple fragments is neither dropped nor leaked. Maintain a small pending buffer for a trailing
     substring of the input that could be the start of an opening or closing tag, and only emit text that is
     provably outside a block.
   - Track whether a block is open; while open, discard content until the matching close tag is fully seen.
4. Add both new files to `target_sources(dir2md_backend PRIVATE ...)` in `./src/backend/core/CMakeLists.txt`.
5. Create `./test/backend/core/thinking_stripper_test.cpp` as a `QTest` class (see Phase 9 for the exact
   case list and registration).

### Exit Criterion
The component compiles into `dir2md_backend`, is free of any Qt GUI dependency, and its unit test passes
for all chunk-boundary and indicator cases.

### Validation Command
```
cmake --build --preset debug
ctest --test-dir build/cmake-debug -R "backend.core.thinking_stripper_test"
```

---

## Phase 4 — Backend: PDF Page Renderer

**Analysis Reference:** §2 (`./src/backend/core/pdf_renderer.hpp/.cpp`, `./test/backend/core/
pdf_renderer_test.cpp`), §3 (Notable Structural Risks: "QGuiApplication switch", "Qt6::Pdf availability";
Performance/Scale: "PDF memory"), §4 (checklist items on PDF rendering).

Create a new backend component that renders a PDF into in-memory page images using QtPDF
(`QPdfDocument`/`QPdfPage`). It must write **no** per-page files to disk.

### Steps
1. Create `./src/backend/core/pdf_renderer.hpp` declaring a class `pdf_renderer` in namespace
   `dir2md::backend`, with trailing return types and no `[[nodiscard]]`.
2. Define the public surface using the existing `expected<T>` result type from `backend/core/expected.hpp`:
   - An open/construct entry point that loads a PDF file by path and reports a clear error (via an
     `expected` error or an `error_frame`) when the file cannot be opened or parsed.
   - A `page_count()` query returning the number of pages once the document is open.
   - A per-page render method that renders a single page (by 0-based index) into an in-memory `QImage`,
     returning an `expected<QImage>` so a per-page render failure yields a clear error. Rendering one page
     at a time (rather than all pages up front) bounds peak memory, consistent with §3's note to release a
     page image after its OCR completes.
   - Document in the header that the component must only be used after a `QGuiApplication` instance exists
     (QtPDF rendering precondition).
3. Implement `./src/backend/core/pdf_renderer.cpp`:
   - Open the document with `QPdfDocument`, check its load status, and surface a descriptive error on failure.
   - For each requested page, obtain the `QPdfPage` and render it to a `QImage` at a sensible resolution;
     never write intermediate image files to disk.
4. Add both new files to `target_sources(dir2md_backend PRIVATE ...)` in `./src/backend/core/CMakeLists.txt`.
5. Create `./test/backend/core/pdf_renderer_test.cpp` as a `QTest` class (see Phase 9 for the case list,
   fixture PDF requirement, and registration).

### Exit Criterion
The component compiles into `dir2md_backend`, renders a valid multi-page PDF to the correct number of
non-empty in-memory images, returns a clear error for a missing/invalid file, and writes no per-page files.

### Validation Command
```
cmake --build --preset debug
ctest --test-dir build/cmake-debug -R "backend.core.pdf_renderer_test"
```

---

## Phase 5 — Backend: OpenAI System-Message Role Fix

**Analysis Reference:** §2 (`./src/backend/core/model.cpp` and `model.hpp`), §3 (Notable Structural Risks:
"OpenAI role bug"; Security & Permissions: "Prompt sanitization"), §4 (checklist item on the OpenAI request
sending the system prompt with role `system`).

As shipped, `openai_schema_parser::construct_request` assigns role `user` to every message, so the system
prompt is sent as a `user` message. This phase corrects the request builder so role information flows
through, while preserving the native (llama.cpp) path and the vision payload.

### Steps
1. In `./src/backend/core/model.hpp`, introduce a small message representation (a struct carrying a role
   discriminator — system/user/assistant — and a content string) in namespace `dir2md::backend`.
2. Change the pure-virtual `api_schema_parser::construct_request` signature to accept that structured
   message list (plus the existing temperature) instead of a flat `QStringList`. Update the declarations of
   both `openai_schema_parser::construct_request` and `native_schema_parser::construct_request` to match.
3. In `./src/backend/core/model.cpp`, update `openai_schema_parser::construct_request` to emit each message
   with its **actual** role (system/user/assistant) rather than hardcoding `user`.
4. Update `native_schema_parser::construct_request` to concatenate the message **contents** with the existing
   newline delimiter, ignoring roles — preserving the current native behavior exactly (the native path has no
   role concept).
5. Update `text_to_text_client::format_payload()` to build the structured message list:
   - the system prompt as a **system**-role message when non-empty;
   - the sanitized user prompt (via the existing `sanitize_prompt`) as a **user**-role message;
   - the assistant prompt as an **assistant**-role message on the native path only (it is discarded for the
     OpenAI path, matching current behavior).
   Ensure the role fix does **not** bypass `sanitize_prompt` for the user prompt.
6. Leave `image_to_text_client::format_payload()` unchanged — it builds its own vision payload directly and
   does not use `construct_request`.

### Exit Criterion
The OpenAI request payload now carries the system prompt with role `system` and the user prompt with role
`user`; the native path output is byte-for-byte equivalent to before; the vision payload is unchanged.

### Validation Command
```
cmake --build --preset debug
ctest --test-dir build/cmake-debug -R "backend.core.test_schema_parser"
ctest --test-dir build/cmake-debug -R "backend.core.test_model_client"
```
(Extend `test_schema_parser` in Phase 9 to assert the system message role in the constructed OpenAI payload.)

---

## Phase 6 — CLI: Shared Helpers (`cli_common`)

**Analysis Reference:** §2 (`./src/cli/cli_common.hpp/.cpp`, `./src/cli/CMakeLists.txt`), §3 (Error Handling
& Exit Codes; Security & Permissions: "Path handling"), §4 (checklist items on prompt/temperature/endpoint
resolution).

Create the shared, workflow-agnostic CLI helpers so `chat` and `ocr` resolve configuration identically.
Keep these helpers free of workflow-specific control flow so each workflow stays thin.

### Steps
1. Create `./src/cli/cli_common.hpp` / `./src/cli/cli_common.cpp` (namespace consistent with the existing CLI,
   snake_case, trailing return types, no `[[nodiscard]]`). Provide:
   - **Text-or-file prompt resolution**: given a string, if it names an existing readable file return the
     file's contents; otherwise return the string verbatim as literal text. Use defensive path normalization
     (`QDir::cleanPath`, absolute-path checks) consistent with the backend.
   - **System-prompt fallback resolution** against `cli/system-prompt-file`: resolve `--system` (text-or-file)
     if present, else the file named by the setting; produce a clear hard-error result when the effective
     system prompt is missing/empty/unreadable (no request may be sent in that case).
   - **Temperature resolution/validation**: accept an optional `--temperature`, validate it as a real value in
     `[0.0, 2.0]` (reject non-numeric/out-of-range), else fall back to `cli/temperature`.
   - **Endpoint resolution** from settings for a given key, with a clear hard-error result when the endpoint
     is empty.
   - **Markdown output writer**: resolve the target path (alongside the source by default, or into an
     `--output` folder when given), apply the `general/overwrite` policy for an existing target (false → skip
     with a notice; true → overwrite), create the output folder if absent, and assemble PDF page content using
     the `---` / `**page N**` separator (1-based) before every page.
   - **Interactive-stdin (TTY) detection** used by the confirmation decision.
2. Centralize the "option overrides setting" resolution here so both workflows behave identically.
3. Add `cli_common.hpp`/`cli_common.cpp` to the `dir2md_cli` target sources in `./src/cli/CMakeLists.txt`.

### Exit Criterion
The helpers compile into `dir2md_cli`, expose pure resolution/validation functions (no workflow control
flow), and are individually unit-testable.

### Validation Command
```
cmake --build --preset debug
```
(Add focused unit tests for the resolvers in Phase 9 under a new `test/cli/` directory.)

---

## Phase 7 — CLI: Chat Workflow + Global Routing + QGuiApplication Switch

**Analysis Reference:** §2 (`./src/cli/main.cpp`, `./src/cli/chat_workflow.hpp/.cpp`,
`./src/cli/CMakeLists.txt`), §1 (Data Flow: "Global routing", "Chat workflow"; New Patterns: "Two-level
command parsing", "`QGuiApplication` requirement"), §3 (Error Handling & Exit Codes; Notable Structural
Risks: "QGuiApplication switch"), §4 (checklist items on routing, help, mandatory prompt, temperature,
header, streaming, exit codes).

This is the highest-risk structural change (the `QCoreApplication` → `QGuiApplication` switch) and must be
validated early. It also establishes the two-level command parsing that the OCR workflow reuses.

### Steps
1. In `./src/cli/main.cpp`, switch the application object from `QCoreApplication` to `QGuiApplication`
   (required for QtPDF rendering in the `ocr` workflow; harmless for `chat`). Keep the existing application
   name/version setup.
2. Replace the single flat `QCommandLineParser` with a **global workflow parser** that recognizes exactly two
   positional subcommands, `chat` and `ocr`. Preserve top-level `--help`/`-h` (which lists both workflows) and
   `--version`, and keep `--verbose` available at the top level (applies to both workflows).
3. Add a dispatch step that hands off to the selected workflow's entry point based on the subcommand, and map
   the workflow's returned exit status to the process exit code (0 success, non-zero failure).
4. Enforce: no subcommand, or an unknown subcommand → print usage to stderr and exit non-zero.
5. Create `./src/cli/chat_workflow.hpp` / `./src/cli/chat_workflow.cpp` exposing a parser limited to
   `--prompt` (mandatory), `--system` (optional), `--temperature` (optional), plus `--help`, and an
   `execute` entry point returning an exit code. Wire it to `text_to_text_client`, `SettingsManager`, the
   thinking stripper, and stdout.
6. Implement the chat logic:
   - Enforce `--prompt` as mandatory (missing → error, non-zero exit).
   - Resolve the system prompt and temperature via the `cli_common` helpers (including the hard-error case).
   - Read `language-model/endpoint` from settings; hard error if empty.
   - Before streaming, print the effective temperature, the **full** system prompt, and the **full** user
     prompt (no truncation).
   - Send the request and run the Qt event loop while the reply streams; route each `incremental_chunk`
     through the thinking stripper to stdout, flushing promptly. On `completion` exit 0; on
     `error_occurred` report and exit non-zero.
   - Produce **no** output file.
7. Bootstrap settings in the CLI (shared by both workflows): construct a `SettingsManager`, call
   `CoreSchema::registerSchemas()`, load persisted settings via `load_from_file` using a project-constant
   simple file name (resolved by the manager to `~/.config/dir2md/`; a missing file is a no-op that leaves
   schema defaults in place), then read fallback values.
8. Add `chat_workflow.hpp`/`chat_workflow.cpp` to the `dir2md_cli` target sources in `./src/cli/CMakeLists.txt`.

### Exit Criterion
The CLI builds and runs under `QGuiApplication`; with no/unknown subcommand it prints usage and exits
non-zero; top-level `--help` lists both workflows and exits 0; `chat` enforces its options, resolves config
via settings fallbacks, prints the full header, streams to stdout, and returns correct exit codes.

### Validation Command
```
cmake --build --preset debug
# Smoke: no subcommand -> usage + non-zero; --help -> lists workflows + 0
./build/cmake-debug/src/cli/dir2md_cli.exe; echo "exit=$?"
./build/cmake-debug/src/cli/dir2md_cli.exe --help
```

---

## Phase 8 — CLI: OCR Workflow

**Analysis Reference:** §2 (`./src/cli/ocr_workflow.hpp/.cpp`), §1 (Data Flow: "OCR workflow"), §3 (Error
Handling & Exit Codes; Performance/Scale: "Sequential processing", "Folder scan"; Notable Structural Risks:
"Non-interactive stdin", "Empty folder scan", "Overwrite policy", "PDF markdown structure"), §4 (checklist
items on OCR file/folder handling, confirmation, sequential streaming, PDF rendering, markdown output,
overwrite, exit codes).

### Steps
1. Create `./src/cli/ocr_workflow.hpp` / `./src/cli/ocr_workflow.cpp` exposing a parser limited to
   `--source` (mandatory), `--system` (optional), `--temperature` (optional), `--output` (optional folder),
   `--yes`/`-y` (flag), plus `--help`, and an `execute` entry point returning an exit code. Wire it to
   `image_to_text_client`, the PDF renderer, `SettingsManager`, the thinking stripper, stdout, and markdown
   file output.
2. Enforce `--source` as mandatory; validate `--temperature` and resolve system prompt/temperature via the
   `cli_common` helpers (same rules as chat, including the hard-error case). Read `ocr-model/endpoint` from
   settings; hard error if empty.
3. Classify `--source`:
   - **File**: validate a supported extension (`.jpg`/`.jpeg`/`.png`/`.pdf`, case-insensitive); reject an
     unsupported extension with a clear error; process directly with **no** confirmation.
   - **Folder**: perform a non-recursive top-level scan for `.jpg`/`.jpeg`/`.png`/`.pdf` (case-insensitive),
     excluding subfolders and non-matching extensions. Print the discovered list. If none are found, print
     that none were found and exit cleanly (no error, no processing).
4. Implement the confirmation decision: prompt to continue **unless** `--yes`/`-y` is set **or** stdin is not
   an interactive TTY (in which cases proceed automatically). A negative answer → process nothing and exit.
5. Process sources **sequentially**; for each, stream OCR text to stdout (flushed) via the thinking stripper:
   - **Image**: call `image_to_text_client::send_request(file_path, system_prompt)`.
   - **PDF**: use the PDF renderer to render each page to an in-memory image (no per-page files on disk) and
     call `image_to_text_client::send_request(QImage, system_prompt)` per page; release each page image after
     its OCR completes.
   - Accumulate the full stripped text per source (per page for a PDF).
6. Write one markdown file per source (base filename + `.md`) via the `cli_common` markdown writer: default
   alongside the source, or into `--output` when given; existing-target handling follows `general/overwrite`;
   for a PDF, join pages with the `---` / `**page N**` separator (1-based) before every page.
7. Report per-source render/parse/model errors per the error policy: a failing source in a folder batch is
   reported and processing continues to the next source; the run's exit code is non-zero if any source failed.
   Success with no failures → exit 0.
8. Add `ocr_workflow.hpp`/`ocr_workflow.cpp` to the `dir2md_cli` target sources in `./src/cli/CMakeLists.txt`.

### Exit Criterion
The CLI builds; `ocr` enforces its options, classifies file vs folder correctly, applies the confirmation
rules, processes sources sequentially with streaming stdout, renders PDFs in-memory only, writes one markdown
file per source with correct naming/separator/overwrite behavior, and returns correct exit codes.

### Validation Command
```
cmake --build --preset debug
# Smoke: missing --source -> non-zero; unsupported extension -> non-zero
./build/cmake-debug/src/cli/dir2md_cli.exe ocr; echo "exit=$?"
```

---

## Phase 9 — Testing & Coverage (Dedicated)

**Analysis Reference:** §2 (test files `thinking_stripper_test.cpp`, `pdf_renderer_test.cpp`,
`./test/backend/core/CMakeLists.txt`, optional `./test/cli/`), §4 (full Verification Checklist).

This phase validates the implementation against the analysis acceptance criteria and produces coverage
reports. Each new `src/...` component is matched by a corresponding test under `test/...` following the
project's existing layout and naming (`test/backend/core/<name>_test.cpp`, registered via `qtest_add_test`).

### Steps
1. **Register the new backend tests.** In `./test/backend/core/CMakeLists.txt`, add `qtest_add_test` lines for
   `thinking_stripper_test.cpp` (PREFIX `backend.core`) and `pdf_renderer_test.cpp` (PREFIX `backend.core`).
2. **Complete `thinking_stripper_test.cpp`** covering: plain-text passthrough; a complete `think` block; a
   complete `reasoning` block; a tag **split across multiple fragments** (chunk-boundary case); nested/adjacent
   blocks; the `thinking ...` indicator emitted exactly once per block; and reset/reuse behavior.
3. **Complete `pdf_renderer_test.cpp`** covering: a valid multi-page PDF yields the correct page count and
   non-empty in-memory images; a missing/invalid file yields a clear error; no per-page files are written to
   disk. Provide a small fixture PDF and run under a `QGuiApplication`-capable harness (the test executable
   must construct a `QGuiApplication` before rendering).
4. **Extend existing backend tests** for the changes in Phases 2 and 5:
   - In the settings-manager test, assert `cli/temperature` defaults to `0.7` and `cli/system-prompt-file`
     defaults to empty after `CoreSchema::registerSchemas()`.
   - In `test_schema_parser.cpp`, assert the constructed OpenAI payload carries the system message with role
     `system` and the user message with role `user`, and that the native path output is unchanged.
5. **Add CLI tests (optional but recommended).** Create a `./test/cli/` directory with a `CMakeLists.txt`
   (mirroring `test/backend/core/CMakeLists.txt`) and a test that invokes the CLI argument parsing / workflow
   entry points in-process to assert exit codes and option isolation: an OCR-only option rejected under
   `chat`, a chat-only option rejected under `ocr`, a missing mandatory option, and a bad temperature. Keep
   these tests free of real network calls (assert on parsing/validation and the resolved effective config, not
   on live model responses). Add `add_subdirectory(cli)` to `./test/CmakeLists.txt`.
6. **Run the full suite under coverage** and generate reports (see Validation Commands). Confirm both
   correctness (all tests pass) and that the new components are exercised by the tests.

### Exit Criterion
All CTest tests pass in the coverage build; coverage reports show the new backend components
(`thinking_stripper`, `pdf_renderer`) and the modified `model.cpp` request path are covered; the §4
Verification Checklist items that are automatable are satisfied.

### Validation Commands
```
# Configure + build the coverage-instrumented target
cmake --preset debug-coverage
cmake --build --preset debug-coverage

# Run the full test suite (coverage build)
ctest --test-dir build/cmake-debug-coverage --output-on-failure

# Targeted runs while iterating
ctest --test-dir build/cmake-debug-coverage -R "backend.core.thinking_stripper_test"
ctest --test-dir build/cmake-debug-coverage -R "backend.core.pdf_renderer_test"
ctest --test-dir build/cmake-debug-coverage -R "backend.core.test_schema_parser"

# Generate coverage data (LLVM native format) from the produced .profraw files
llvm-profdata merge -o default.profdata *.profraw
llvm-cov show build/cmake-debug-coverage/src/cli/dir2md_cli.exe -instr-profile=default.profdata
```

---

## Final Verification Checklist (maps to Analysis §4)

Work through every item in the analysis §4 Verification Checklist after Phase 9. Items not covered by
automated tests (interactive TTY confirmation, live streaming to a real endpoint, deploy step) must be
verified manually and noted as such. Key manual checks:
- `QGuiApplication` switch causes no regression to the `chat` path and the deploy step still succeeds.
- Folder confirmation: interactive stdin prompts; `--yes`/`-y` skips it; non-interactive stdin proceeds
  automatically; a "no" answer processes nothing and exits.
- Chat streams incrementally to stdout and exits 0 on completion; a model/network error yields non-zero.
- Default output is alongside the source; `--output <folder>` redirects all output there (base filename
  preserved); existing-target handling follows `general/overwrite`.
