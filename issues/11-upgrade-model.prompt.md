# Implementation Plan: 11-upgrade-model

[Analysis Reference](./11-upgrade-model.plan.analysis.md)

> **Authoritative wire-format source:** `docs/internal/backend/core/sse-json-responses-stream.md`.
> Every response-handling decision in this plan (SSE vs single JSON body, OpenAI vs native schema,
> dedicated reasoning channel vs inline tags, `[DONE]` vs `stop: true` termination) is derived from that
> document. Consult it before implementing any Phase 3 step.

This blueprint translates the validated "what" in the analysis into a concrete "how". It is purely
descriptive and instructional — **no implementation code or pseudocode is included here**. Each phase
references the Analysis Section and Requirement IDs (R1–R6, Q2–Q5) it implements, and carries an explicit
**Exit Criterion** and **Validation Command**.

## Grounding Facts (verified against the current codebase)

These facts anchor the plan to the real APIs and conventions. Do not re-derive them; use them as-is.

- **Property pattern to mirror:** `model_client_base` (`src/backend/core/model.hpp`) already declares
  `endpoint_url`, `model_name`, `temperature`, and `coalescing_interval_ms` as `Q_PROPERTY` with a
  `READ`/`WRITE`/`NOTIFY` triple, a getter/setter pair, a matching signal, and a member. The new `stream`
  property must follow this exact pattern (Analysis §2, R1).
- **Parser interface:** `api_schema_parser` (`src/backend/core/model.hpp`) currently exposes
  `parse_line`, `parse_usage`, and `construct_request(messages, temperature)`. Both concrete parsers
  (`openai_schema_parser`, `native_schema_parser`) hardcode the stream flag to true inside
  `construct_request` (Analysis §2, R2).
- **Image payload:** `image_to_text_client::format_payload` (`src/backend/core/model.cpp`) builds its own
  inline payload and also hardcodes the stream flag to true (Analysis §2, R2).
- **Text payload:** `text_to_text_client::format_payload` delegates request construction to the active
  parser's `construct_request`, then adds the model name. It is the single call site that must thread the
  client's stream value into the parser (Analysis §2, R2).
- **Response path today:** `on_ready_read` splits each `readAll()` chunk on newlines with empty parts
  skipped and has no carry-over buffer; `process_line` runs the raw-line probe, the env-gated debug dump,
  `parse_line`, `parse_usage`, and restarts the coalescing timer; `on_finished` flushes the remaining chunk
  buffer and emits `completion`. There is no SSE `data:` stripping, no `[DONE]`/`stop: true` handling, and
  no non-streaming whole-body path (Analysis §1, §2, R3–R5).
- **Test helper constraint:** `cmake/qtest_add_test.cmake` accepts a single `SOURCE` and derives the target
  name from the prefix plus the file stem (dots become underscores), so `test_model_client.cpp` builds the
  target `backend_core_test_model_client`. It links only `Qt6::Test` and `dir2md_backend`. To compile an
  additional mock source into that target, add it after the helper call (the CLI tests already use this
  post-call `target_link_libraries` pattern in `test/cli/CMakeLists.txt`) (Analysis §2, R6).
- **Slot discovery gotcha:** `qtest_add_test` parses the source at **configure** time to register CTest
  slots. After adding any new test slot you MUST re-run configure (`cmake --preset debug`), not just build,
  or the new slot will not be registered.
- **Reference mock pattern:** `test/cli/chat_integration_test.cpp` already contains an in-process
  `QTcpServer` mock (`mock_model_server`) with NDJSON and SSE modes, request-body capture, and a
  pump-the-event-loop-until-completion helper. The new backend mock should follow this proven shape but be
  generalized to the full format matrix (Analysis §2, R6).
- **Schema settings exist but are out of scope:** `core_schema.cpp` already registers
  `language-model/stream` and `ocr-model/stream` (default `false`). This task does **not** wire them into
  the client; the client's new `stream` property is set directly by callers/tests (Analysis §1).
- **Conventions (STRICT):** snake_case for all identifiers; trailing return types on every function; no
  `[[nodiscard]]`; QML files CamelCase (not relevant here, no QML touched).

---

## Phase 1 — Add the `stream` property to `model_client_base`

**Implements:** Analysis §2 (`src/backend/core/model.hpp`, `src/backend/core/model.cpp`), Requirement **R1**.

**Goal:** Give the client a first-class, observable `stream` boolean that defaults to `false` and is
inherited by both `text_to_text_client` and `image_to_text_client`.

Steps:

1. In `src/backend/core/model.hpp`, inside `model_client_base`, add a `Q_PROPERTY` declaration for `stream`
   following the existing four properties exactly: a boolean type, a `READ` getter, a `WRITE` setter, and
   a `NOTIFY` signal named after the property with a `_changed` suffix.
2. In the same class, declare the getter (const, returning the boolean) and setter (taking a const bool
   reference) accessors using trailing return types, placed alongside the other property accessors.
3. Declare the `stream_changed(bool)` signal in the existing `signals:` block, next to the other
   `*_changed` signals.
4. Add the backing member `m_stream` in the protected/private member section, initialized to `false`,
   adjacent to the other property members (e.g., near `m_temperature`).
5. In `src/backend/core/model.cpp`, implement the getter to return the member and the setter to follow the
   existing guard pattern: assign and emit `stream_changed` exactly once only when the value actually
   changes; emit nothing when the value is unchanged.

**Exit Criterion:** A freshly constructed client reports `stream == false`; setting a new value emits
`stream_changed` exactly once; setting the same value again emits nothing. Both concrete clients inherit the
property with no additional code.

**Validation Command:**
```powershell
cmake --build --preset debug
ctest --test-dir build/cmake-debug -R "backend.core.test_model_client"
```
(The R1 assertions themselves are authored in Phase 5; this command confirms the property compiles and the
existing slots still pass.)

---

## Phase 2 — Make request construction stream-aware and add whole-body extraction

**Implements:** Analysis §2 (`model.hpp`, `model.cpp`), Requirements **R2** and the non-streaming half of
**R3**.

**Goal:** Stop hardcoding the stream flag in every request; carry the client's chosen value through both
parsers and the image payload. Add a whole-body extraction operation to the parser interface for the
non-streaming path.

Steps:

1. In `src/backend/core/model.hpp`, change the `api_schema_parser::construct_request` pure-virtual signature
   to accept the stream mode as an additional boolean parameter (in addition to messages and temperature),
   keeping the trailing return type. Update both concrete parser declarations (`openai_schema_parser`,
   `native_schema_parser`) to match the new override signature.
2. In the same header, extend the `api_schema_parser` interface with a new pure-virtual whole-body
   extraction operation (e.g., `parse_full_response`) that takes the full response body as a string and
   returns the extracted answer text. This is the non-streaming counterpart to `parse_line`. Declare it in
   both concrete parsers.
3. In `src/backend/core/model.cpp`, update `openai_schema_parser::construct_request` to set the stream field
   on the root object from the new parameter instead of the hardcoded true.
4. In the same file, update `native_schema_parser::construct_request` identically (set the stream field from
   the parameter).
5. Implement `openai_schema_parser::parse_full_response` to parse the whole body once and return the answer
   text from the non-streaming OpenAI location: the content of the first choice's message object. Return an
   empty string when the body is not valid JSON, is not an object, or lacks that path.
6. Implement `native_schema_parser::parse_full_response` to parse the whole body once and return the answer
   text from the non-streaming native location: the top-level content field. Return an empty string on the
   same missing/invalid conditions.
7. In `text_to_text_client::format_payload`, pass the client's current `stream` value into the parser's
   `construct_request` call (replacing the previous two-argument call).
8. In `image_to_text_client::format_payload`, set the stream field on its inline payload from the client's
   `stream` property instead of the hardcoded true.

**Exit Criterion:** For both OpenAI and native schemas, the constructed payload carries `stream: true` when
the client's property is true and `stream: false` when it is false; the image-to-text payload behaves the
same. Each parser's whole-body extraction returns the correct answer text for its non-streaming shape and an
empty string otherwise.

**Validation Command:**
```powershell
cmake --build --preset debug
ctest --test-dir build/cmake-debug -R "backend.core.test_schema_parser"
```
(The R2 payload assertions and the minimal `test_schema_parser.cpp` update are authored in Phase 5; this
command confirms the new signatures compile and existing parser slots still pass after the signature change.)

---

## Phase 3 — Rework the response path to be format-aware end to end

**Implements:** Analysis §1 (Data Flow Changes) and §2 (`model.cpp`), Requirements **R3**, **R4**, **R5**.
This is the core of the task. Work through the sub-steps in order; they are interdependent.

### 3a. Line reassembly (carry-over buffer) — R4

1. In `src/backend/core/model.hpp`, add a carry-over buffer member to `model_client_base` that holds the
   trailing partial line from the previous read.
2. In `on_ready_read` (`src/backend/core/model.cpp`), prepend the carry-over buffer to the newly read text
   before splitting. After splitting, keep the final (incomplete) segment — the one not terminated by a
   newline — in the carry-over buffer and process only the complete lines. This guarantees a JSON line split
   across two TCP reads is reassembled exactly once, with no loss and no duplication.

### 3b. SSE framing strip + termination detection — R3, R4

3. In the per-line handling (the path that feeds `process_line`), before schema parsing: strip a leading
   `data:` prefix plus surrounding whitespace from each line; treat a bare `[DONE]` sentinel as
   end-of-stream; ignore blank lines and SSE comment lines silently (no warning, no error).
4. Add termination recognition for both signals: `[DONE]` (OpenAI) and a final object carrying `stop: true`
   (native). After either is observed, mark the stream finished so no further lines are expected and
   `completion` fires exactly once.

### 3c. Malformed-line tolerance — R4

5. When a data line fails to parse as JSON, emit a warning through the existing logging path and continue
   parsing the remaining lines. Never raise an error signal or abort the stream; the stream still completes
   with the text accumulated from all valid lines.

### 3d. Reasoning-channel handling — R3 / Q2

6. Ensure dedicated reasoning fields are discarded and never mixed into the answer text: on the OpenAI path,
   `reasoning_content`/`thinking` on the delta (streaming) and on the message (non-streaming); on the native
   path, `reasoning_content`. No new signal is added for reasoning.
7. Ensure inline `think`/`reasoning` tags that appear inside content pass through verbatim into the
   accumulated text (the downstream `thinking_stripper` handles them; do not strip them here).

### 3e. Non-streaming whole-body path — R3, R5

8. When the client's `stream` property is false, do not run line splitting or the coalescing timer. Instead,
   accumulate the full response body across reads and, on finish, parse it once via the parser's
   whole-body extraction operation (added in Phase 2) to obtain the answer text.

### 3f. Coalescing gating — R5 / Q3

9. Gate the coalescing timer and `incremental_chunk` emission so they are active only when `stream` is true.
   When `stream` is false, no `incremental_chunk` is emitted at all; `completion` carries the full text
   exactly once.
10. In `on_finished`, branch on the stream mode: for streaming, flush any remaining chunk buffer and emit
    `completion` with the accumulated text (preserving current behavior); for non-streaming, extract the
    answer from the accumulated body and emit `completion` once. Ensure `completion` fires exactly once in
    both modes and that the busy flag and reply cleanup are preserved.

**Exit Criterion:** Every combination in the R3 format matrix yields the correct completion text; a line
split across two reads is extracted exactly once; a malformed mid-stream line warns and still completes; each
termination signal fires `completion` exactly once; dedicated reasoning is discarded while inline tags are
preserved verbatim; and `incremental_chunk` is emitted only when streaming.

**Validation Command:**
```powershell
cmake --build --preset debug
ctest --test-dir build/cmake-debug -R "backend.core.test_model_client"
```
(The matrix assertions are authored in Phase 5 against the mock from Phase 4; this command confirms the
reworked response path compiles and existing slots still pass.)

---

## Phase 4 — Create the reusable in-process mock server and register it

**Implements:** Analysis §2 (`test/backend/core/<new mock server file>`, `test/backend/core/CMakeLists.txt`),
Requirement **R6**.

**Goal:** Provide a single reusable in-process HTTP mock that any test slot can configure to emit any format
from the matrix, so `test_model_client.cpp` drives a real client over a real localhost socket.

Steps:

1. Create a new header/source pair under `test/backend/core/` (e.g., `mock_model_server.hpp` and
   `mock_model_server.cpp`) defining a reusable mock built on `QTcpServer`. Model it on the proven
   `mock_model_server` in `test/cli/chat_integration_test.cpp`, but generalize it.
2. The mock must bind to a localhost ephemeral port (port 0) in-process, with no external processes or
   network access, and expose its bound port so a test can point a real client at it.
3. Give the mock per-test configuration covering the full matrix: transport (SSE / NDJSON / single JSON
   body) × schema (OpenAI / native) × reasoning (dedicated channel / inline tags / plain).
4. Implement correct framing per mode: `data:`-prefixed lines plus a `[DONE]` sentinel for OpenAI SSE;
   `data:`-prefixed lines ending in a `stop: true` object for native SSE; bare JSON lines for NDJSON; and a
   single JSON object for the non-streaming body.
5. Add an optional control to split a write across two TCP sends so tests can exercise the carry-over buffer
   (R4).
6. Serve the configured body on each connection and close cleanly so the client's `finished` fires; capture
   the raw request body so tests can assert on the outgoing payload (R2).
7. In `test/backend/core/CMakeLists.txt`, register the new mock source(s) so they compile into the
   `backend_core_test_model_client` target. Because `qtest_add_test` takes a single `SOURCE`, add the mock
   source to that target after the helper call (mirroring the post-call `target_link_libraries` pattern used
   in `test/cli/CMakeLists.txt`). Confirm the mock needs no link dependencies beyond what the target already
   links (`Qt6::Test`, `dir2md_backend`).

**Exit Criterion:** The mock compiles into the `backend_core_test_model_client` target, binds to a localhost
ephemeral port in-process, and can be configured per test to emit any matrix mode with correct framing.

**Validation Command:**
```powershell
cmake --preset debug
cmake --build --preset debug
```
(Re-running configure is required so the target picks up the newly added source.)

---

## Phase 5 — Author the test coverage and update the schema-parser assertions

**Implements:** Analysis §2 (`test_model_client.cpp`, `test_schema_parser.cpp`), Requirements **R1–R6**,
and the minimal change permitted by **Q5**.

**Goal:** Cover the full acceptance matrix end to end (real client against the mock) and make the two
affected schema-parser assertions match the new stream-carrying construction API.

### 5a. Expand `test/backend/core/test_model_client.cpp`

1. Add a helper that, given a configured mock, points a real `text_to_text_client` at the mock's port, drives
   `send_request()`, pumps the event loop until completion/error (with a safety timeout), and returns the
   completion text plus any captured `incremental_chunk` emissions. Reset the process-global
   `schema_registry` active schema at the start of each slot to avoid cross-slot contamination.
2. Add slots for **R1**: default `stream == false`; setting a new value emits `stream_changed` exactly once;
   setting the same value emits nothing.
3. Add slots for **R2**: the outgoing payload carries `stream: true` when set and `stream: false` when unset,
   for both OpenAI and native schemas, including the image-to-text payload.
4. Add one slot per **R3** format combination, each asserting on completion text (and `incremental_chunk`
   where applicable): SSE OpenAI (`data:`-prefixed, ending `[DONE]`) with `stream=true`; NDJSON OpenAI;
   non-streaming OpenAI body with `stream=false`; native SSE ending in `stop: true` (no `[DONE]`) with
   `stream=true`; non-streaming native body with `stream=false`; dedicated reasoning channel (discarded —
   answer text contains only the final answer and no extra signal); inline tags (preserved verbatim, in
   order).
5. Add slots for **R4**: a JSON line split across two TCP reads is extracted exactly once; a malformed line
   mid-stream produces a warning and completion still fires with all valid-line text; each termination signal
   (`[DONE]`, `stop: true`) results in `completion` firing exactly once.
6. Add slots for **R5**: `incremental_chunk` emissions occur per the coalescing interval when `stream=true`;
   no `incremental_chunk` is emitted at all when `stream=false`.

### 5b. Minimally update `test/backend/core/test_schema_parser.cpp` (Q5)

7. Update only the two `construct_request` assertions that currently assert a hardcoded stream value to match
   the new stream-carrying construction API (passing the stream mode explicitly). Change no other assertion in
   this file; parser line/usage behavior under test is unchanged.

**Exit Criterion:** `test_model_client.cpp` has at least one slot per R3 format combination asserting on
completion text (and `incremental_chunk` where applicable), plus slots for R1, R2, R4, and R5; the two
schema-parser stream assertions are updated and no other assertion in that file changed.

**Validation Command:**
```powershell
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/cmake-debug -R "backend.core.test_model_client"
ctest --test-dir build/cmake-debug -R "backend.core.test_schema_parser"
```
(Re-running configure is required so the newly added slots are registered with CTest.)

---

## Phase 6 — Testing & coverage validation (acceptance gate)

**Implements:** Analysis §4 (Verification Checklist), Requirements **R1–R6**.

**Goal:** Validate the full implementation against every acceptance criterion and confirm no regressions,
using the project's coverage build.

Steps:

1. Run the complete backend test suite in the debug build and confirm all slots pass, including the new
   `test_model_client` matrix slots and the updated `test_schema_parser` slots:
   ```powershell
   cmake --preset debug
   cmake --build --preset debug
   ctest --test-dir build/cmake-debug -R "backend.core"
   ```
2. Run the full test suite (all targets) to confirm no regressions, including the untouched CLI integration
   test and its own mock (R6):
   ```powershell
   ctest --preset debug
   ```
3. Build and run the tests under the coverage preset to capture instrumentation:
   ```powershell
   cmake --preset debug-coverage
   cmake --build --preset debug-coverage
   ctest --test-dir build/cmake-debug-coverage -R "backend.core"
   ```
4. Generate the coverage report from the produced profiling data (per the project's documented workflow):
   ```powershell
   llvm-profdata merge -o default.profdata *.profraw
   llvm-cov show build/cmake-debug-coverage/dir2md_frontend.exe -instr-profile=default.profdata
   ```
   Confirm the newly added/modified lines in `src/backend/core/model.cpp` (SSE framing, line reassembly,
   termination, malformed handling, reasoning discard, non-streaming path, coalescing gating) and the new
   `stream` property accessors are exercised by the tests.

**Known toolchain limitation (honest note):** The `debug-coverage` preset sets Clang coverage flags
(`-fprofile-instr-generate -fcoverage-mapping`), but this project builds with MSVC, which ignores them
(warning D9002). As a result no `.profraw` files are produced and `llvm-cov` has nothing to show on this
toolchain as currently configured. Tests still run correctly in that build. If the coverage report comes back
empty, treat the debug-build test pass (steps 1–2) as the authoritative correctness gate and note the
coverage limitation rather than failing the task on it.

**Exit Criterion:** Every item in Analysis §4's Verification Checklist is satisfied; all pre-existing tests
still pass with no regressions (including the untouched CLI integration test); and coverage of the new/changed
`model.cpp` logic is confirmed (or the MSVC coverage limitation is documented when the toolchain cannot emit
profiling data).

**Validation Command:**
```powershell
ctest --preset debug
```

---

## Traceability Summary

| Phase | Analysis Section(s) | Requirement(s) | Files touched |
|-------|---------------------|----------------|---------------|
| 1 — `stream` property | §2 (model.hpp, model.cpp) | R1 | `src/backend/core/model.hpp`, `src/backend/core/model.cpp` |
| 2 — stream-aware request + whole-body extraction | §2 (model.hpp, model.cpp) | R2, R3 (non-streaming) | `src/backend/core/model.hpp`, `src/backend/core/model.cpp` |
| 3 — format-aware response path | §1, §2 (model.cpp) | R3, R4, R5 | `src/backend/core/model.hpp`, `src/backend/core/model.cpp` |
| 4 — reusable mock + CMake registration | §2 (test mock, CMakeLists) | R6 | `test/backend/core/mock_model_server.{hpp,cpp}` (new), `test/backend/core/CMakeLists.txt` |
| 5 — test authoring + schema-parser update | §2 (test_model_client, test_schema_parser) | R1–R6, Q5 | `test/backend/core/test_model_client.cpp`, `test/backend/core/test_schema_parser.cpp` |
| 6 — testing & coverage gate | §4 (Verification Checklist) | R1–R6 | (no source changes; validation only) |

**Out of scope (per Analysis §1):** CLI (`src/cli/`, `test/cli/`), frontend, `thinking_stripper`, and any
downstream consumer of `incremental_chunk` are not touched. The existing `language-model/stream` and
`ocr-model/stream` schema settings are not wired into the client in this task.
