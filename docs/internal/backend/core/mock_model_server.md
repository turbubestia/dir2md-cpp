# `mock_model_server` — in-process mock model endpoint

**Source pair:** `src/backend/core/mock_model_server.hpp` + `src/backend/core/mock_model_server.cpp` (both present).
**Role:** Test-only. A reusable, in-process `QTcpServer` that stands in for a real model endpoint so the backend model client (`model_client_base` and its subclasses in `src/backend/core/model.{hpp,cpp}`) can be exercised without threads, external processes, or network access beyond localhost.
**Date:** 2026-08-16

---

## Purpose and role in the project

The backend model client speaks HTTP to a model endpoint and must tolerate a matrix of real-world response shapes. `mock_model_server` lets each test slot configure that shape precisely and deterministically, then assert on both:

- the **outgoing** request (captured raw body), and
- the **incoming** response as reassembled by the client (completion text, chunk count, error count).

It is consumed by `test/backend/core/model_client_test.cpp` (the primary consumer) and is conceptually the successor to the ad-hoc mock embedded in `test/cli/chat_integration_test.cpp` (which still carries its own private `mock_model_server` class — see [Contextual dependencies](#contextual-dependencies)).

It is **not** part of the shipped backend library's public surface; it exists to make client behavior observable and repeatable.

## Response matrix

Three orthogonal knobs, set per test via `set_transport` / `set_schema` / `set_reasoning`:

| Knob | Values | Effect |
| --- | --- | --- |
| `transport` | `sse` \| `ndjson` \| `single_body` | Wire framing of the response. |
| `schema` | `openai` \| `native` | JSON shape of each payload object. |
| `reasoning` | `plain` \| `dedicated` \| `inline_tags` | How (whether) chain-of-thought is represented. |

### Framing per transport

- **`sse` + `openai`:** each payload is a `data: {...}` line terminated by a blank line; the stream ends with the sentinel `data: [DONE]`.
- **`sse` + `native`:** same `data:` framing, but there is no `[DONE]` sentinel — the final object carries `"stop": true` (mirroring llama-server).
- **`ndjson`:** bare JSON lines, one per chunk, newline-terminated, no framing and no sentinel. Completion is signalled by the final object's `stop: true` (native) or by end-of-stream (openai).
- **`single_body`:** one JSON object, no framing. OpenAI shape uses `choices[0].message.content`; native shape uses a top-level `content`.

### Reasoning representation

- **`plain`:** answer text only (`"Hello world!"`, streamed as three chunks `"Hello"`, `" world"`, `"!"`).
- **`dedicated`:** a separate `reasoning_content` field carries the chain-of-thought (`"Step 1: think..."`), emitted in its own leading chunk/object. The client is expected to **discard** this channel, so it does not appear in the completion text.
- **`inline_tags`:** the chain-of-thought is embedded verbatim inside `content` as a `<think> ... </think>` block, riding in the **first** content chunk. The client is expected to **preserve** it verbatim.

`expected_completion_text(reasoning)` is the single source of truth for what the completion text must be for a given reasoning mode: dedicated reasoning is dropped, inline tags are kept verbatim in order. Tests compare the client's `completion` payload against it directly, so the mock and the assertion cannot drift apart.

## Major members and responsibilities

### Construction and lifecycle

- `mock_model_server(QObject *parent = nullptr)` — parents the internal `QTcpServer m_server` to itself, so the server and any pending sockets are torn down with the mock.
- `start() -> bool` — binds `QHostAddress::LocalHost` on an **ephemeral port** (port 0) and connects `newConnection`. Returns `false` if `listen` fails. Tests assert `QVERIFY(server.start())`.
- `port() const -> quint16` — the bound port, used to build the client's endpoint URL (`http://127.0.0.1:<port>`).

### Per-test configuration

- `set_transport` / `set_schema` / `set_reasoning` — plain member writes; no validation, no emission.
- `active_schema() const -> schema` — exposes the current schema so a test can synchronise the process-global `schema_registry` active schema with the mock's response format (see `run_request` in the test).
- `set_split_at(int body_offset)` — if `0 < body_offset < response.size()`, the response is written across **two TCP sends**: the first carries everything up to `body_offset` bytes (deliberately landing mid-line), the rest follows after a 25 ms `QTimer::singleShot`. This exercises the client's carry-over / line-reassembly buffer. `0` (default) disables splitting.
- `set_inject_malformed_line(bool)` — when enabled, one malformed `data: {this is not json` line is injected **after the first content line** so tests can verify mid-stream tolerance (the client must warn and continue, not abort).

### Request capture

- `last_request_body() const -> QString` — the decoded body of the most recent connection, empty if none. Lets tests assert on the exact outgoing payload (e.g. that `"stream": true/false` is present, that image payloads are well-formed JSON).

### Connection handling (private)

- `on_new_connection()` — pulls `nextPendingConnection()`, **clears the shared request buffer** for a fresh capture, tags the socket with a `response_sent=false` property, wires `readyRead` to `on_socket_ready_read`, and wires `disconnected` to `deleteLater` so each socket frees itself.
- `on_socket_ready_read(socket)` — appends `readAll()` to the buffer; if the request is not yet complete it returns and waits for more data. On completion it captures the body, sets `response_sent=true` (so a later `readyRead` cannot double-respond), and sends the response.
- `request_complete() -> bool` — detects end-of-request by parsing headers:
  - finds the header/body boundary `\r\n\r\n`;
  - if `Transfer-Encoding: chunked`, completes when the zero-length terminator `\r\n0\r\n\r\n` is present;
  - otherwise completes when at least `Content-Length` bytes of body have arrived.
- `capture_request_body()` — re-parses the headers, then decodes the body: for chunked transfer it walks `<hex-size>\r\n<data>\r\n … 0\r\n\r\n` and concatenates the data segments (ignoring chunk extensions after `;`); otherwise it takes the first `Content-Length` bytes. The result is UTF-8-decoded into `m_request_body`.
- `send_response(socket)` — builds the body, emits an `HTTP/1.1 200 OK` response with the correct `Content-Type` (`text/event-stream` for SSE, `application/x-ndjson` for NDJSON, `application/json` otherwise), `Connection: close`, and a `Content-Length`. Applies the optional split, then disconnects.
- `build_body() -> QByteArray` — assembles the payload per the current transport/schema/reasoning configuration (see [Response matrix](#response-matrix)).

### Anonymous-namespace helpers

- `answer_chunks` / `reasoning_text` / `inline_think_prefix` — the canned content constants.
- `join_answer()` / `full_content(r)` / `content_chunks(r)` — derive the complete text and the per-chunk split for a reasoning mode.
- `openai_delta_line(content, reasoning)` / `native_chunk_line(content, reasoning, stop)` — emit one compact-JSON line (newline-terminated) in the respective schema; the native final chunk additionally carries run metadata (`tokens_predicted`, `prompt_tokens`, `eval_time_ms`) to mirror llama-server.

## Usage pattern

The canonical flow (from `model_client_test.cpp`):

1. Construct a `mock_model_server`.
2. Configure the matrix: `set_transport`, `set_schema`, optionally `set_reasoning`, `set_split_at`, `set_inject_malformed_line`.
3. `QVERIFY(server.start())`.
4. Point a real client at it: `client.set_endpoint_url("http://127.0.0.1:" + QString::number(server.port()))`, set model/prompt, `set_stream(...)`.
5. Sync the process-global schema registry to `server.active_schema()`.
6. Attach result capture to the client's `completion` / `incremental_chunk` / `error_occurred` signals, call `client.send_request()`, and pump the event loop until completion/error or a safety timeout.
7. Assert on the captured result and on `server.last_request_body()`.

Because the mock and the client share one event loop, no threads are involved and the interaction is fully deterministic apart from the intentional 25 ms split delay.

## Ownership, lifetime, and threading assumptions

- **Single event loop.** The mock runs in the same loop as the client. There is no cross-thread access to any member; all state is touched only from that loop. No locking is present or needed under this assumption.
- **Socket lifetime.** Each accepted socket is parented to nothing but is connected to `deleteLater` on `disconnected`, so it frees itself. The `send_response` split lambda captures the raw `QTcpSocket*`; this is safe because `deleteLater` defers destruction until the event loop, and the 25 ms timer fires while the socket is still alive (the mock disconnects only after the second write).
- **Server lifetime.** `m_server` is parented to the mock, so destroying the mock tears down the listener and any pending connections.
- **One request per connection.** The `response_sent` property guarantees at most one response per socket even if multiple `readyRead` signals arrive.

## Input validation and error handling

- `start()` returns `false` on `listen` failure; it does not throw.
- Header parsing is tolerant of CRLF vs LF (lines are split on `\n` then trimmed).
- The chunked decoder and the `Content-Length` path assume well-formed input from the client (which is under test control). There is no defensive handling of malformed request framing — appropriate for a test double, but see [Static Analysis and Security](#static-analysis-and-security).
- Malformed **response** lines are an intentional feature (`set_inject_malformed_line`), not an error condition of the mock.

## Contextual dependencies

- **`model_client_base` / `text_to_text_client`** (`src/backend/core/model.{hpp,cpp}`): the system under test. The mock's framing choices are meaningful only relative to how the client reassembles lines, detects `[DONE]` / `stop: true`, and partitions `reasoning_content` vs `content`.
- **`schema_registry`** (process-global): the test resets its active schema to match `server.active_schema()` before each request to avoid cross-slot contamination. This is a test-harness concern, not a mock responsibility, but it explains why `active_schema()` is exposed.
- **`test/cli/chat_integration_test.cpp`**: contains a *separate*, private `mock_model_server` class (with a `response_mode` enum rather than the transport/schema/reasoning matrix). The two are independent; the backend one is the more general, reusable implementation. This duplication is a maintainability observation, not a defect in this pair.

---

## Static Analysis and Security

The pair is a test-only local-socket double, so the security surface is minimal (localhost, no auth, no untrusted remote input). The findings below are about **correctness and maintainability** of the mock itself, and about the preconditions its consumers must respect.

### F1 — Shared request-capture state is clobbered by concurrent connections

- **Evidence:** `on_new_connection()` calls `m_request_data.clear()` unconditionally at the start of every new connection, and both `request_complete()` / `capture_request_body()` read from the single shared `m_request_data` buffer. There is no per-socket storage of request bytes.
- **Risk:** If two connections are accepted before the first has completed its request, the second connection's `clear()` discards the first's partially-read body, and both sockets' completion checks then operate on the same (now mixed) buffer. `last_request_body()` would reflect whichever connection finished last.
- **Impact:** Wrong or empty captured request body and possibly a missed/duplicated response if a test ever opens more than one connection to a single mock instance. Correctness of any multi-connection test would be silently compromised.
- **Mitigation:** Either (a) document and enforce the single-connection-per-mock precondition (e.g. assert in `on_new_connection` that no prior socket is still pending), or (b) move request accumulation into per-socket state (e.g. a `QByteArray` stored as a dynamic property, or a small per-connection helper object) so concurrent connections cannot interfere.
- **Follow-up test recommendation:** A test that opens two sequential connections to one mock and asserts `last_request_body()` matches the *second* request (documenting the intended last-wins behaviour), plus a guard that a second connection while the first is incomplete does not corrupt the first's capture if per-socket state is adopted.

### F2 — Chunked-completion detection can false-positive on body content

- **Evidence:** `request_complete()` declares a chunked request complete when `m_request_data.contains("\r\n0\r\n\r\n")`. This searches the **entire** buffer, including body bytes, not just the framing.
- **Risk:** If a request body itself contained the byte sequence `\r\n0\r\n\r\n`, completion would be declared before the true zero-length terminator arrives, truncating the captured body.
- **Impact:** Under-captured `last_request_body()` for any payload that happens to embed that sequence. Low likelihood for the JSON payloads this project sends, but it is a latent correctness hazard rather than a guaranteed-safe check.
- **Mitigation:** Detect the terminator structurally — parse chunk frames sequentially (as `capture_request_body` already does) and stop at the first zero-length chunk — instead of a raw substring search over the whole buffer.
- **Follow-up test recommendation:** A unit test feeding a chunked body that contains `\r\n0\r\n\r\n` inside a data segment and asserting the full body (including bytes after that sequence) is captured.

### F3 — Duplicated header-parsing logic between `request_complete` and `capture_request_body`

- **Evidence:** Both functions independently locate `\r\n\r\n`, split headers on `\n`, trim, and re-derive `chunked` / `content_length` with the same `mid(15)` offset for `Content-Length`.
- **Risk:** The two copies can drift. A fix to one (e.g. handling a differently-cased header, a different offset, or an extra header) that is not mirrored in the other would make completion detection and body capture disagree.
- **Impact:** Maintainability hazard; a subtle divergence could cause the mock to declare completion at a different point than it uses to slice the body, yielding a truncated or over-read body without any error.
- **Mitigation:** Factor the header scan into a single helper returning a small struct (`{ header_end, chunked, content_length }`) and call it from both functions.
- **Follow-up test recommendation:** A test that sends a request with an unusual but valid header ordering/casing and asserts both completion and capture agree on the body.

### F4 — `Content-Length` parsed with unchecked `toInt()`

- **Evidence:** `content_length = line.mid(15).trimmed().toInt();` — no base argument, no failure flag, no range check.
- **Risk:** A malformed or missing value silently yields `0`, making `request_complete()` treat the request as complete as soon as headers are seen and `capture_request_body()` to capture an empty body.
- **Impact:** Silent empty capture rather than a loud failure. Acceptable for a test double driven by a well-behaved in-process client, but it masks misconfiguration if the client's framing ever changes.
- **Mitigation:** Use `toInt(&ok)` and, on failure or a negative value, surface it (e.g. `qWarning` or an assertion) so a framing regression is visible during test runs.
- **Follow-up test recommendation:** A test that points a client configured to send a body at the mock and asserts `last_request_body()` is non-empty and matches the expected JSON, guarding against a silent zero-length regression.

### F5 — Chunked decoder assumes well-formed framing (no bounds/trailer validation)

- **Evidence:** In `capture_request_body()`, the chunked loop computes `pos = data_start + chunk_size + 2` and calls `payload.mid(pos, ...)` / `payload.indexOf("\r\n", pos)` without verifying `pos` stays within `payload.size()`, and it does not handle chunk trailers or a non-hex size (a bad hex yields `0` via `toUInt(...,16)` and terminates the loop early).
- **Risk:** On malformed chunked input the loop can terminate early (truncating the body) or, in a contrived case, walk past the end. Qt's `QByteArray::mid` clamps out-of-range arguments so this will not crash, but the result is a silently truncated capture.
- **Impact:** Same class of silent-truncation risk as F4/F2; bounded by the fact that the producer is the in-process client under test.
- **Mitigation:** Validate `chunk_size` and bounds inside the loop (`if (data_start + chunk_size > payload.size()) break;`) and treat a non-hex size as an explicit error rather than a silent stop.
- **Follow-up test recommendation:** A focused test that feeds a deliberately truncated chunked body and asserts the mock does not hang and reports a capture consistent with the bytes actually present.

### F6 — `start()` is not idempotent (duplicate `newConnection` connections)

- **Evidence:** `start()` calls `connect(&m_server, &QTcpServer::newConnection, this, &mock_model_server::on_new_connection)` on every invocation, with no guard against a prior call.
- **Risk:** Calling `start()` twice on the same instance connects the slot twice, so each accepted socket would be handled twice (double `readyRead` wiring, double response attempt — partially masked by the `response_sent` property, but the request buffer would still be cleared twice).
- **Impact:** Misbehaviour if a test reuses a mock instance across `start()` calls. Currently every test constructs a fresh instance and calls `start()` once, so this is latent.
- **Mitigation:** Guard with a `m_started` flag (return the existing port / `true` if already started) or disconnect any prior connection before reconnecting.
- **Follow-up test recommendation:** A test that calls `start()` twice on one instance and asserts a single subsequent request still yields exactly one response and one captured body.

### Residual risks and assumptions not fully analyzed

- **Client-side behaviour is out of scope.** Whether the client correctly reassembles split lines, tolerates the injected malformed line, fires `completion` exactly once on `[DONE]` / `stop: true`, and partitions `reasoning_content` vs `content` is a property of `model.{hpp,cpp}`, not this pair. The mock only *produces* those stimuli; the corresponding assertions live in `model_client_test.cpp`.
- **The 25 ms split delay** introduces a small, fixed timing dependency. It is deterministic enough for the event-loop-pumping test harness, but a host under heavy load could in principle interleave other events during that window. No correctness issue is expected, but it is an assumption worth noting.
- **`test/cli/chat_integration_test.cpp`'s private mock** was not analyzed as part of this pair (it is a separate, self-contained class). Its duplication with this implementation is flagged as context only.
- **No thread-safety or exception-safety guarantees are claimed** beyond the single-event-loop assumption stated above; the source documents neither, and none is required for its test-only role.
