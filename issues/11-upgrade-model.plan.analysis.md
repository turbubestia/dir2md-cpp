# Implementation Analysis: 11-upgrade-model

> **Reference documentation (use in later stages):** `docs/internal/backend/core/sse-json-responses-stream.md`
> is the authoritative source for every wire format this task must handle (SSE vs single JSON body, OpenAI vs
> native schema, dedicated reasoning channel vs inline tags, `[DONE]` vs `stop: true` termination). All format
> decisions below are derived from that document.

## 1. Architectural Impact & Data Flow

The model client (`model_client_base` + the two schema parsers) currently assumes a single wire format:
line-delimited JSON with no framing, and it hardcodes `stream: true` in every request. This task makes the
client **format-aware end to end**: the chosen stream mode is carried in the request payload, and the response
path branches on that mode plus the active schema to extract the answer text for every combination documented in
the reference doc.

- **Affected Subsystems:**
  - `src/backend/core` — model client base class and both schema parsers (OpenAI + native). This is the only
    production code touched.
  - `test/backend/core` — new reusable in-process HTTP mock server + expanded `test_model_client.cpp`; minimal
    assertion updates in `test_schema_parser.cpp`.
  - **Not touched:** CLI (`src/cli/`, `test/cli/`), frontend, `thinking_stripper`, and any downstream consumer of
    `incremental_chunk`. The core schema settings (`language-model/stream`, `ocr-model/stream`) already exist in
    `core_schema.cpp` (default `false`) but are **not** wired into the client in this task.

- **Data Flow Changes:**
  - **Request path:** `send_request()` → `format_payload()` → parser `construct_request(...)`. The parser's
    request construction changes from a hardcoded `stream: true` to a value supplied by the client's new `stream`
    property, so the payload explicitly carries `stream: true|false` for both schemas. `image_to_text_client`'s
    own inline payload (which also hardcodes `stream: true`) must honor the same property.
  - **Response path (streaming, `stream=true`):** `on_ready_read()` → line reassembly (new carry-over buffer) →
    SSE framing strip (`data:` prefix, blank/comment lines ignored, `[DONE]` sentinel) → parser `parse_line`
    extracts the text fragment (dedicated reasoning fields are discarded; inline tags pass through verbatim) →
    accumulate + coalescing timer emits `incremental_chunk` → termination on `[DONE]` (OpenAI) or `stop: true`
    (native) → `completion`.
  - **Response path (non-streaming, `stream=false`):** the body is a single JSON object, not lines. The client
    accumulates the full body and, on finish, parses it once via a new whole-body extraction path (OpenAI:
    `choices[0].message.content`; native: top-level `content`). No line splitting, no coalescing timer, no
    `incremental_chunk` — only `completion` fires with the full text.
  - **New structural pattern:** a reusable in-process `QTcpServer` mock under `test/backend/core` that can be
    configured per test to emit any format from the matrix, so `test_model_client.cpp` drives a real client over
    a real (localhost) socket.

## 2. Component & File Impact Map

### `src/backend/core/model.hpp`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add a boolean `stream` property to `model_client_base`: `Q_PROPERTY` declaration, `get_stream()` /
        `set_stream(bool)` accessors, a `stream_changed(bool)` NOTIFY signal, and an `m_stream` member defaulting
        to `false`. Inherited by both `text_to_text_client` and `image_to_text_client`.
  - [ ] Add a carry-over buffer member (partial-line state) to `model_client_base` for reassembling lines split
        across TCP reads.
  - [ ] Extend the `api_schema_parser` interface with a whole-body extraction operation (e.g.
        `parse_full_response`) for the non-streaming path, alongside the existing `parse_line`.
  - [ ] Adjust the parser `construct_request` signature to carry the stream mode (instead of hardcoding it), so
        both parsers can emit the client's chosen value.
- **Logic Modifications Required:**
  - [ ] `set_stream` follows the existing property pattern: emit `stream_changed` exactly once on change, nothing
        when the value is unchanged.

### `src/backend/core/model.cpp`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Implement the `stream` property accessors and default (`false`).
  - [ ] Thread the client's `stream` value into request construction for both `openai_schema_parser` and
        `native_schema_parser`, replacing the two hardcoded `root["stream"] = true;` sites.
  - [ ] Update `image_to_text_client::format_payload` to set `stream` from the property instead of the hardcoded
        `true`.
- **Logic Modifications Required:**
  - [ ] **SSE framing (streaming path):** strip a leading `data:` prefix (plus surrounding whitespace) before
        parsing; treat a bare `[DONE]` as end-of-stream; ignore blank lines and SSE comment lines silently (no
        warning).
  - [ ] **Line reassembly:** buffer the tail of each `readAll()` chunk so a JSON line split across two reads is
        reassembled before parsing (no loss, no duplication).
  - [ ] **Termination detection:** recognize both `[DONE]` (OpenAI) and `stop: true` (native) as end-of-stream;
        after either, no further lines are expected and `completion` fires exactly once.
  - [ ] **Malformed lines:** an unparseable data line emits a warning (via the existing logging path) and parsing
        continues — never an error signal or abort.
  - [ ] **Reasoning channels:** dedicated reasoning fields (`reasoning_content` / `thinking` on delta/message,
        native `reasoning_content`) are discarded and never mixed into the answer text; no new signal is added.
        Inline `think`/`reasoning` tags inside content pass through verbatim (downstream `thinking_stripper`
        handles them).
  - [ ] **Non-streaming path:** when `stream=false`, accumulate the full body and extract the answer once on
        finish (OpenAI `choices[0].message.content`; native top-level `content`); do not run line splitting or the
        coalescing timer.
  - [ ] **Coalescing gating:** the coalescing timer / `incremental_chunk` emission is active only when
        `stream=true`. When `stream=false`, no `incremental_chunk` is emitted at all; `completion` carries the
        full text exactly once.

### `test/backend/core/<new mock server file>` (e.g. `mock_model_server.hpp` / `.cpp`)
- **Type of Change:** Create
- **Structural Changes:**
  - [ ] A reusable in-process HTTP mock built on `QTcpServer`, binding to a localhost ephemeral port with no
        external processes or network access.
  - [ ] Per-test configuration selecting the response mode: transport (SSE / NDJSON / single JSON body) × schema
        (OpenAI / native) × reasoning (dedicated channel / inline tags / plain).
  - [ ] Ability to emit the correct framing per mode (`data:` prefixes + `[DONE]` for OpenAI SSE; `data:` +
        `stop: true` for native SSE; bare JSON lines for NDJSON; a single JSON object for non-streaming).
  - [ ] Optional control to split a write across two TCP sends so tests can exercise the carry-over buffer.
- **Logic Modifications Required:**
  - [ ] Serve the configured body on each connection and close cleanly so the client's `finished` fires.

### `test/backend/core/test_model_client.cpp`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add test slots covering the full R3 format matrix end to end (real client against the mock): SSE OpenAI,
        NDJSON OpenAI, non-streaming OpenAI, SSE native (`stop: true`), non-streaming native, dedicated reasoning
        channel (discarded), inline tags (preserved verbatim).
  - [ ] Add slots for R1 (default `false`; set emits once; same value emits nothing), R2 (payload carries
        `stream` true/false for both schemas), R4 (split line reassembled exactly once; malformed line warns and
        still completes; each termination signal fires completion exactly once), and R5 (coalescing active only
        when streaming; no `incremental_chunk` when non-streaming).
- **Logic Modifications Required:**
  - [ ] Each slot configures the mock for one mode, points a real client at the mock's port, drives
        `send_request()`, and asserts on `completion` text (and `incremental_chunk` behavior where applicable).

### `test/backend/core/CMakeLists.txt`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Register the new mock server source(s) so they compile into the `test_model_client` target (the existing
        `qtest_add_test` helper takes a single `SOURCE`, so the mock must be added to that target's sources or the
        helper extended minimally).
- **Logic Modifications Required:**
  - [ ] Ensure the mock has no extra link dependencies beyond what the test target already links (`Qt6::Test`,
        `dir2md_backend`).

### `test/backend/core/test_schema_parser.cpp`
- **Type of Change:** Modify (minimal, permitted by Q5)
- **Structural Changes:**
  - [ ] Update the two `construct_request` assertions that currently assert a hardcoded `stream: true` to match
        the new stream-carrying construction API.
- **Logic Modifications Required:**
  - [ ] No other assertions in this file change; parser line/usage behavior under test is unchanged.

## 3. Boundary & Edge Case Analysis

- **Error Handling:**
  - Malformed (unparseable) data lines → warning emitted, parsing continues, stream still completes with the text
    from all valid lines (no error signal, no abort).
  - Blank lines and SSE comment lines → ignored silently (not errors, not warnings).
  - A line split across two TCP reads → reassembled via the carry-over buffer; extracted exactly once.
  - Termination is recognized by either `[DONE]` (OpenAI) or `stop: true` (native); after either, completion fires
    exactly once and no further lines are expected.
  - Network-level errors continue to surface through the existing `error_occurred` path (unchanged).
- **Security & Permissions:**
  - No new permissions, middleware, or scope checks. The mock binds only to a localhost ephemeral port in-process;
    no external network access is required.
- **Performance / Scale Impact:**
  - The carry-over buffer and SSE framing strip add negligible per-line work. The non-streaming path avoids line
    splitting entirely (single parse of the full body). No new heavy loops or allocations of concern.
  - The process-global `schema_registry` active-schema state is shared across test slots; existing tests already
    reset it, and new slots must do the same to avoid cross-slot contamination.

## 4. Verification Checklist

- [ ] Verify a freshly constructed client reports `stream == false` (R1 default).
- [ ] Verify setting `stream` to a new value emits `stream_changed` exactly once; setting the same value emits
      nothing (R1).
- [ ] Verify the constructed payload contains `stream: true` when set and `stream: false` when unset, for both
      OpenAI and native schemas (R2), including the image-to-text payload.
- [ ] Verify an SSE OpenAI stream (`data:`-prefixed, ending `[DONE]`) with `stream=true` yields completion text
      equal to the concatenation of all `delta.content` fragments (R3).
- [ ] Verify a non-streaming OpenAI body with `stream=false` yields completion text equal to
      `choices[0].message.content` (R3).
- [ ] Verify a native SSE stream ending in `stop: true` (no `[DONE]`) with `stream=true` yields completion text
      equal to the concatenation of all `content` chunks (R3).
- [ ] Verify a non-streaming native body with `stream=false` yields completion text equal to top-level `content`
      (R3).
- [ ] Verify reasoning in a dedicated channel (`reasoning_content` / `thinking`) is discarded — answer text contains
      only the final answer and no extra signal is emitted (R3 / Q2).
- [ ] Verify inline-tagged reasoning content is preserved verbatim, in order, in the accumulated text (R3).
- [ ] Verify a JSON line split across two TCP reads is extracted exactly once (R4).
- [ ] Verify a malformed line mid-stream produces a warning and completion still fires with all valid-line text
      (R4 / Q4).
- [ ] Verify each termination signal (`[DONE]`, `stop: true`) results in `completion` firing exactly once (R4).
- [ ] Verify `incremental_chunk` emissions occur per the coalescing interval when `stream=true`, and no
      `incremental_chunk` is emitted at all when `stream=false` (R5 / Q3).
- [ ] Verify the mock is reusable across slots (different modes, no duplicated server code) and binds to a
      localhost ephemeral port in-process (R6).
- [ ] Verify `test_model_client.cpp` has at least one slot per R3 format combination asserting on `completion`
      text (and `incremental_chunk` where applicable) (R6).
- [ ] Verify the minimally updated `test_schema_parser.cpp` stream assertions pass and no other assertions in that
      file changed (R6 / Q5).
- [ ] Verify all pre-existing tests still pass (no regressions), including the untouched CLI integration test and
      its own mock (R6).
