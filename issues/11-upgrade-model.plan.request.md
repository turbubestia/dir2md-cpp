# Upgrade the Model Classes

Currently the CLI system is failing because in stream mode the server respond with SSE message, which fails the JSON parsing silently failing.

The document `docs\internal\backend\core\sse-json-responses-stream.md` clearly describe the findings and how stream and responses works. This task focus in improving the `src\backend\core\model.hpp/.cpp` classes to be robust and be able to handle the different stream and thinking/reasoning cases.

The problem we are facing is the result of a mismatch of the expected results. And this revealed a more fundamental problem.

## Goals

- We have added a stream option in the core schema which needs to be wired in the corresponding model classes. This will required to add stream as an new property in the base model client class.

- make the model parser robuts to handle the different response format in `stream=true|false` and when the thinking response is encoded in a different channel or embedded in thinking/reasonig blocks.

- Add a HTTP server mockup in `test\backend\core` that can be reuse to respond in the different format so we can expand the `test\backend\core\test_model_client.cpp` tests.

- Since this fix lives in the `src\backend\core\model.hpp/.cpp` files, we will limit the test to this file only in `test\backend\core\test_model_client.cpp`. The CLI module will not be touched in this task.

- The coalsening of token should only be active when stream=true, beacuse when strem=false this is not required.

---
# Refinement Iteration 1
**Status:** PENDING USER FEEDBACK

## 1. Executive Summary
Make the backend model client (`src/backend/core/model.hpp/.cpp`) correctly honor a configurable `stream` option end to end: the request payload must carry the chosen stream mode, and the response handling must robustly extract the answer text for every supported wire format (SSE vs single JSON body, OpenAI vs native schema, reasoning in a dedicated field vs inline tags). A reusable in-process HTTP mock server is added under `test/backend/core` so `test_model_client.cpp` can cover the full format matrix.

## 2. Refined Requirements & Acceptance Criteria

- **Requirement R1: Stream property on the base model client**
  - **Description:** `model_client_base` exposes a boolean `stream` property (getter, setter, NOTIFY signal), consistent with the existing `endpoint_url`, `model_name`, `temperature`, and `coalescing_interval_ms` properties. It is inherited by both `text_to_text_client` and `image_to_text_client`.
  - **Acceptance Criteria:**
    - [ ] Given a client with `stream` unset, When the property is read, Then it returns the agreed default (see Q1).
    - [ ] Given a client, When `stream` is set to a new value, Then the getter returns the new value and the changed signal is emitted exactly once; setting the same value emits nothing.

- **Requirement R2: Request payload honors the stream property**
  - **Description:** The constructed request payload for both the OpenAI and the native schema sets the `stream` field from the client's `stream` property instead of the currently hardcoded `true`. The value is sent explicitly (not omitted) in both modes.
  - **Acceptance Criteria:**
    - [ ] Given `stream=true`, When a request is constructed, Then the payload contains `stream: true` for both OpenAI and native schemas.
    - [ ] Given `stream=false`, When a request is constructed, Then the payload contains `stream: false` for both OpenAI and native schemas.

- **Requirement R3: Response parsing is robust across all format combinations**
  - **Description:** The client extracts the complete answer text correctly for the full matrix of response formats documented in `docs/internal/backend/core/sse-json-responses-stream.md`:
    - `stream=true`, OpenAI schema: SSE-framed lines (`data:` prefix, blank lines, `data: [DONE]` sentinel) carrying `choices[0].delta.content`; plain NDJSON lines (no prefix) must keep working as well.
    - `stream=true`, native schema: SSE-framed flat `content` chunks terminated by a final chunk with `stop: true` (no `[DONE]` sentinel).
    - `stream=false`, OpenAI schema: a single JSON body with the answer at `choices[0].message.content`.
    - `stream=false`, native schema: a single JSON body with the answer in the top-level `content` field.
    - Thinking/reasoning content may arrive either in a dedicated channel (`reasoning_content` / `thinking` fields on the delta or message, native `reasoning_content`) or embedded inline as think/reasoning tags inside the content. In both cases the final answer text must be extracted completely and in order; inline-tagged content is passed through verbatim so downstream stripping (e.g., `thinking_stripper`) can handle it.
  - **Acceptance Criteria:**
    - [ ] Given an SSE OpenAI stream (`data:`-prefixed chunks ending with `[DONE]`) and `stream=true`, When the request completes, Then the completion text equals the concatenation of all `delta.content` fragments.
    - [ ] Given a non-streaming OpenAI body and `stream=false`, When the request completes, Then the completion text equals `message.content`.
    - [ ] Given a native SSE stream ending with `stop: true` (no `[DONE]`) and `stream=true`, When the request completes, Then the completion text equals the concatenation of all `content` chunks.
    - [ ] Given a non-streaming native body and `stream=false`, When the request completes, Then the completion text equals the top-level `content`.
    - [ ] Given reasoning delivered in a dedicated channel, When the response is parsed, Then the answer text contains only the final answer (reasoning text is not mixed into it).
    - [ ] Given reasoning embedded inline as tags inside content, When the response is parsed, Then the full raw content (tags included) is preserved in order in the accumulated text.

- **Requirement R4: Transport-level robustness**
  - **Description:** A response line split across two network reads must be reassembled before parsing (carry-over buffer). Unparseable lines must not crash or abort the request; the stream still completes with the text extracted from all valid lines. Stream termination is recognized both by the `[DONE]` sentinel (OpenAI) and by `stop: true` (native).
  - **Acceptance Criteria:**
    - [ ] Given a JSON line split across two TCP reads, When both reads are processed, Then its text is extracted exactly once (no loss, no duplication).
    - [ ] Given a malformed line in the middle of an otherwise valid stream, When the stream finishes, Then completion still fires with the text from all valid lines.
    - [ ] Given either termination signal (`[DONE]` or `stop: true`), When it is received, Then no further lines are expected and completion fires exactly once.

- **Requirement R5: Coalescing only active when streaming**
  - **Description:** The timer-based incremental chunk emission (coalescing) is active only when `stream=true`. When `stream=false`, the coalescing mechanism does not run; the full text is delivered via `completion` (see Q3 for whether any `incremental_chunk` is emitted at all in non-stream mode).
  - **Acceptance Criteria:**
    - [ ] Given `stream=true`, When tokens arrive, Then `incremental_chunk` emissions occur per the coalescing interval, as today.
    - [ ] Given `stream=false`, When the response arrives, Then no timer-driven `incremental_chunk` emissions occur and `completion` carries the full text.

- **Requirement R6: Reusable HTTP mock server and expanded client tests**
  - **Description:** A reusable in-process HTTP mock server is added under `test/backend/core`. It can be configured per test to respond in each format from R3 (SSE / NDJSON / single JSON body × OpenAI / native × reasoning channel / inline tags / plain). `test_model_client.cpp` is expanded to drive a real client against the mock and cover the full R3 matrix end to end.
  - **Acceptance Criteria:**
    - [ ] The mock is reusable: multiple test slots select different response modes without duplicating server code.
    - [ ] `test_model_client.cpp` contains at least one test per R3 format combination, asserting on the client's `completion` text (and `incremental_chunk` behavior where applicable).
    - [ ] The mock binds to a localhost ephemeral port in-process; no external processes or network access are required.
    - [ ] All pre-existing tests continue to pass.

## 3. Scope & Constraints
- **In-Scope:**
  - `src/backend/core/model.hpp` / `model.cpp` (client base class, both schema parsers).
  - `test/backend/core/test_model_client.cpp` (expanded tests).
  - New mock server file(s) under `test/backend/core` plus their CMake registration.
- **Out-of-Scope:**
  - The CLI module (`src/cli/`, `test/cli/`) — not touched, including the existing CLI integration test and its own mock.
  - Frontend module.
  - Changes to `thinking_stripper` or any downstream consumer of `incremental_chunk`.
  - Wiring the core schema settings (`language-model/stream`, `ocr-model/stream`) into CLI/frontend consumers — that happens in a later task; this task only makes the client property exist and be honored.
- **Technical Constraints / Edge Cases:**
  - Code conventions: snake_case identifiers, trailing return types, no `[[nodiscard]]`.
  - The mock must work under CTest on Windows (in-process `QTcpServer`, ephemeral port) with no external dependencies.
  - Existing `test_schema_parser.cpp` asserts `stream: true` in constructed payloads; if the request-construction API changes to carry the stream mode, those assertions may need a minimal update (see Q5).
  - SSE streams may contain blank lines and comment lines; they must be ignored, not treated as errors.

## 4. Open Design Choices (Questions for User)
- **[Technical] Q1 — Default value of the `stream` property:** Should the default be `false` (matching the core schema default) or `true` (preserving today's on-the-wire behavior)? Since the CLI is not touched in this task, the default alone determines the CLI's effective behavior until it is wired to the setting.
**User: keep it false as in the core schema.**

- **[Business Logic] Q2 — Dedicated reasoning channel:** When reasoning arrives in a separate field (`reasoning_content` / `thinking`), should it be silently discarded (user sees only the final answer), or surfaced through a separate signal so consumers can display it later?
**User: for simplicity lets discard it so no additional changes need to be introduce downstream.**

- **[Technical] Q3 — Non-stream emissions:** When `stream=false`, should `incremental_chunk` be emitted at all (e.g., once with the full text), or should only `completion` fire?
**User: Only completion.**

- **[Technical] Q4 — Malformed lines:** Keep the current silent drop for unparseable lines, or surface them (log and/or error signal) so format regressions are visible?
**User: surface them as warnings and keep going. We want to have the information.**

- **[Technical] Q5 — Existing parser tests:** If the request-construction API changes to carry the stream mode, are minimal updates to the affected assertions in `test_schema_parser.cpp` allowed, or must that file remain untouched as well?
**User: yes, that is allowed.**

---
# Refinement Iteration 2
**Status:** LOCKED

## 1. Executive Summary
All open design questions from Iteration 1 have been answered. This iteration folds those decisions into the requirement set: the `stream` property defaults to `false`, reasoning delivered in a dedicated channel is discarded, non-streaming responses emit only `completion` (no `incremental_chunk`), malformed stream lines are surfaced as warnings without aborting the request, and minimal updates to `test_schema_parser.cpp` assertions are permitted. No open design choices remain.

## 2. Refined Requirements & Acceptance Criteria

- **Requirement R1: Stream property on the base model client (default `false`)**
  - **Description:** `model_client_base` exposes a boolean `stream` property (getter, setter, NOTIFY signal), consistent with the existing `endpoint_url`, `model_name`, `temperature`, and `coalescing_interval_ms` properties. It is inherited by both `text_to_text_client` and `image_to_text_client`. The default value is `false`, matching the core schema default (Q1).
  - **Acceptance Criteria:**
    - [ ] Given a freshly constructed client, When the `stream` property is read, Then it returns `false`.
    - [ ] Given a client, When `stream` is set to a new value, Then the getter returns the new value and the changed signal is emitted exactly once; setting the same value emits nothing.

- **Requirement R2: Request payload honors the stream property**
  - **Description:** The constructed request payload for both the OpenAI and the native schema sets the `stream` field from the client's `stream` property instead of the currently hardcoded `true`. The value is sent explicitly (not omitted) in both modes.
  - **Acceptance Criteria:**
    - [ ] Given `stream=true`, When a request is constructed, Then the payload contains `stream: true` for both OpenAI and native schemas.
    - [ ] Given `stream=false`, When a request is constructed, Then the payload contains `stream: false` for both OpenAI and native schemas.

- **Requirement R3: Response parsing is robust across all format combinations**
  - **Description:** The client extracts the complete answer text correctly for the full matrix of response formats documented in `docs/internal/backend/core/sse-json-responses-stream.md`:
    - `stream=true`, OpenAI schema: SSE-framed lines (`data:` prefix, blank lines, `data: [DONE]` sentinel) carrying `choices[0].delta.content`; plain NDJSON lines (no prefix) must keep working as well.
    - `stream=true`, native schema: SSE-framed flat `content` chunks terminated by a final chunk with `stop: true` (no `[DONE]` sentinel).
    - `stream=false`, OpenAI schema: a single JSON body with the answer at `choices[0].message.content`.
    - `stream=false`, native schema: a single JSON body with the answer in the top-level `content` field.
    - Thinking/reasoning content may arrive either in a dedicated channel (`reasoning_content` / `thinking` fields on the delta or message, native `reasoning_content`) or embedded inline as think/reasoning tags inside the content. Reasoning in a dedicated channel is discarded (Q2) — it must not be mixed into the answer text and no new signal is introduced for it. Inline-tagged content is passed through verbatim so downstream stripping (e.g., `thinking_stripper`) can handle it.
  - **Acceptance Criteria:**
    - [ ] Given an SSE OpenAI stream (`data:`-prefixed chunks ending with `[DONE]`) and `stream=true`, When the request completes, Then the completion text equals the concatenation of all `delta.content` fragments.
    - [ ] Given a non-streaming OpenAI body and `stream=false`, When the request completes, Then the completion text equals `message.content`.
    - [ ] Given a native SSE stream ending with `stop: true` (no `[DONE]`) and `stream=true`, When the request completes, Then the completion text equals the concatenation of all `content` chunks.
    - [ ] Given a non-streaming native body and `stream=false`, When the request completes, Then the completion text equals the top-level `content`.
    - [ ] Given reasoning delivered in a dedicated channel (`reasoning_content` / `thinking`), When the response is parsed, Then the answer text contains only the final answer; the reasoning text is discarded and no additional signal is emitted for it.
    - [ ] Given reasoning embedded inline as tags inside content, When the response is parsed, Then the full raw content (tags included) is preserved in order in the accumulated text.

- **Requirement R4: Transport-level robustness**
  - **Description:** A response line split across two network reads must be reassembled before parsing (carry-over buffer). Unparseable lines must not crash or abort the request; each malformed line is surfaced as a warning (Q4) and parsing continues, so the stream still completes with the text extracted from all valid lines. Stream termination is recognized both by the `[DONE]` sentinel (OpenAI) and by `stop: true` (native).
  - **Acceptance Criteria:**
    - [ ] Given a JSON line split across two TCP reads, When both reads are processed, Then its text is extracted exactly once (no loss, no duplication).
    - [ ] Given a malformed line in the middle of an otherwise valid stream, When the stream finishes, Then completion still fires with the text from all valid lines and a warning is emitted for the malformed line.
    - [ ] Given either termination signal (`[DONE]` or `stop: true`), When it is received, Then no further lines are expected and completion fires exactly once.

- **Requirement R5: Coalescing only active when streaming**
  - **Description:** The timer-based incremental chunk emission (coalescing) is active only when `stream=true`. When `stream=false`, the coalescing mechanism does not run and no `incremental_chunk` signal is emitted at all (Q3); the full text is delivered exclusively via `completion`.
  - **Acceptance Criteria:**
    - [ ] Given `stream=true`, When tokens arrive, Then `incremental_chunk` emissions occur per the coalescing interval, as today.
    - [ ] Given `stream=false`, When the response arrives, Then no `incremental_chunk` emission occurs (timer-driven or otherwise) and `completion` carries the full text exactly once.

- **Requirement R6: Reusable HTTP mock server and expanded client tests**
  - **Description:** A reusable in-process HTTP mock server is added under `test/backend/core`. It can be configured per test to respond in each format from R3 (SSE / NDJSON / single JSON body × OpenAI / native × reasoning channel / inline tags / plain). `test_model_client.cpp` is expanded to drive a real client against the mock and cover the full R3 matrix end to end. Minimal updates to the affected `stream` assertions in `test_schema_parser.cpp` are permitted (Q5).
  - **Acceptance Criteria:**
    - [ ] The mock is reusable: multiple test slots select different response modes without duplicating server code.
    - [ ] `test_model_client.cpp` contains at least one test per R3 format combination, asserting on the client's `completion` text (and `incremental_chunk` behavior where applicable).
    - [ ] The mock binds to a localhost ephemeral port in-process; no external processes or network access are required.
    - [ ] All pre-existing tests continue to pass, including any minimally updated assertions in `test_schema_parser.cpp`.

## 3. Scope & Constraints
- **In-Scope:**
  - `src/backend/core/model.hpp` / `model.cpp` (client base class, both schema parsers).
  - `test/backend/core/test_model_client.cpp` (expanded tests).
  - New mock server file(s) under `test/backend/core` plus their CMake registration.
  - `test/backend/core/test_schema_parser.cpp` — minimal updates to assertions affected by the stream mode carried in request construction (Q5).
- **Out-of-Scope:**
  - The CLI module (`src/cli/`, `test/cli/`) — not touched, including the existing CLI integration test and its own mock.
  - Frontend module.
  - Changes to `thinking_stripper` or any downstream consumer of `incremental_chunk`.
  - New signals for dedicated-channel reasoning (discarded per Q2).
  - Wiring the core schema settings (`language-model/stream`, `ocr-model/stream`) into CLI/frontend consumers — that happens in a later task; this task only makes the client property exist and be honored.
- **Technical Constraints / Edge Cases:**
  - Code conventions: snake_case identifiers, trailing return types, no `[[nodiscard]]`.
  - The mock must work under CTest on Windows (in-process `QTcpServer`, ephemeral port) with no external dependencies.
  - SSE streams may contain blank lines and comment lines; they must be ignored, not treated as errors or warnings.
  - Malformed (unparseable) data lines produce a warning but never an error signal or abort (Q4).

**LOCKED**