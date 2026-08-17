# `src/backend/core/model.hpp` / `model.cpp`

## Purpose and Role

This source pair implements the backend's model-client layer: a set of schema parsers that translate between wire formats (OpenAI chat-completions JSON and llama.cpp native JSON) and a common in-process representation, plus two concrete `QObject`-based HTTP clients (`image_to_text_client`, `text_to_text_client`) that POST prompts to a configurable endpoint and stream or receive the response.

The pair is the single point of contact between the application and any LLM endpoint. Both the QtQuick frontend and the CLI consume these clients; the backend library itself contains no UI code.

## Major Types and Responsibilities

### Schema types and registry

| Type | Role |
|---|---|
| `schema_type` (enum) | Selects the active wire format: `openai` or `native`. |
| `api_schema_parser` (abstract) | Interface for line-by-line streaming parse, usage extraction, full-response parse, and request construction. |
| `openai_schema_parser` | Implements the OpenAI chat-completions schema. Reads `choices[0].delta.content` (streaming) or `choices[0].message.content` (full response). Writes `messages[]`, `temperature`, `stream`. |
| `native_schema_parser` | Implements the llama.cpp native schema. Reads top-level `content` (streaming) or `content` (full response). Writes a single `prompt` string (all message contents concatenated with `\n`; roles are discarded), `temperature`, `stream`. |
| `schema_registry` | Static factory holding a process-global `s_active_schema`. `create_parser()` returns a fresh `std::unique_ptr<api_schema_parser>` on each call. |

### Token statistics

`token_stats` carries `total_tokens`, `tokens_per_sec`, `generation_time_ms`, `prompt_tokens`, and `prompt_eval_time_ms`. The OpenAI parser populates only `total_tokens` and `prompt_tokens`; the native parser populates all five fields (computing `tokens_per_sec` from `tokens_predicted` and `eval_time_ms`).

### Chat message

`chat_message` is a plain struct with `message_role` (`system` / `user` / `assistant`) and a `QString content`. The OpenAI parser maps roles to their string names; the native parser ignores roles entirely.

### `model_client_base` (abstract `QObject`)

The shared HTTP client logic:

- **Properties** (Q_PROPERTY with change signals): `endpoint_url`, `model_name`, `temperature`, `coalescing_interval_ms`, `stream`.
- **Request lifecycle**: `send_request()` (pure virtual) → subclass calls `format_payload()` → base calls `start_request(payload)` which issues a `QNetworkAccessManager::post()`.
- **Streaming path** (`m_stream == true`): `on_ready_read()` reassembles lines split across TCP reads using `m_line_carryover`, then feeds each complete line to `process_line()`. `process_line()` strips SSE `data:` prefixes, ignores blank/comment lines, detects `[DONE]` (OpenAI) or `stop: true` (native) termination, extracts text via the active parser, and appends tokens to `m_chunk_buffer`. A single-shot `QTimer` (`m_coalescing_timer`) flushes `m_chunk_buffer` as an `incremental_chunk` signal every `m_coalescing_interval_ms`.
- **Non-streaming path** (`m_stream == false`): `on_ready_read()` appends raw bytes to `m_full_body`. `on_finished()` parses the complete body once via `parse_full_response()`.
- **Termination**: `on_finished()` flushes any remaining carry-over line and chunk buffer, then emits `completion(full_text, stats)`.
- **Cancellation**: `cancel()` sets `m_cancelled`, aborts the reply, and emits `cancelled()`.
- **Error handling**: `on_error_occurred()` emits `error_occurred(error_frame)` and clears `m_busy`.
- **Test hooks**: `set_raw_line_probe()` / `clear_raw_line_probe()` install a process-global callback that receives every raw line before schema parsing. A `DIR2MD_MODEL_DEBUG` environment variable enables stderr + file logging of raw lines.

### `image_to_text_client`

Extends `model_client_base` for vision requests. Accepts an image via file path or `QImage`, base64-encodes it into a data URI, and constructs an OpenAI-style multimodal content array (`type: "text"` + `type: "image_url"`). MIME type is inferred from the file extension (png/jpg/jpeg/gif/webp; defaults to `image/png`) or from the `QImage` format (always returns `image/png`).

### `text_to_text_client`

Extends `model_client_base` for text-only chat. Holds `system_prompt`, `user_prompt`, and `assistant_prompt` as Q_PROPERTYs. `format_payload()` sanitizes the user prompt via `sanitize_prompt()`, builds a `std::vector<chat_message>`, delegates to the active parser's `construct_request()`, and injects the `model` field. The assistant prompt participates only on the native schema path; it is discarded for OpenAI.

## Public Usage Patterns

```cpp
// Select wire format (process-global)
schema_registry::set_active_schema(schema_type::openai);

// Text-to-text
auto client = std::make_unique<text_to_text_client>();
client->set_endpoint_url("http://localhost:8080/v1/chat/completions");
client->set_model_name("gpt-4o");
client->set_stream(true);
client->set_system_prompt("You are a helpful assistant.");
client->set_user_prompt("Summarize this document…");
QObject::connect(client.get(), &text_to_text_client::incremental_chunk,
                 [](const QString &chunk) { /* append to UI */ });
QObject::connect(client.get(), &text_to_text_client::completion,
                 [](const QString &full, const token_stats &stats) { /* done */ });
client->send_request();

// Image-to-text
auto ocr = std::make_unique<image_to_text_client>();
ocr->set_endpoint_url("http://localhost:8080/v1/chat/completions");
ocr->set_model_name("gpt-4o");
ocr->send_request("/path/to/scan.png", "Transcribe all text.");
```

## Control Flow and State Transitions

```
send_request()
  └─ format_payload()          (subclass)
       └─ start_request(payload)
            ├─ m_busy = true, reset all buffers
            ├─ QNetworkAccessManager::post()
            └─ connect readyRead / finished / errorOccurred

on_ready_read()
  ├─ [stream=false]  m_full_body += data
  └─ [stream=true]   reassemble lines → process_line() per line
       ├─ probe / debug log
       ├─ [DONE] or stop:true → m_stream_finished = true
       ├─ parse_line() → m_accumulated_text, m_chunk_buffer
       ├─ start coalescing timer
       └─ parse_usage() → m_current_stats (if total_tokens > 0)

on_coalescing_timeout()
  └─ emit incremental_chunk(m_chunk_buffer), clear buffer

on_finished()
  ├─ [stream=true]   flush carry-over + chunk buffer
  ├─ [stream=false]  parse_full_response(m_full_body)
  ├─ m_busy = false, deleteLater reply
  └─ emit completion(text, stats)

on_error_occurred(code)
  ├─ emit error_occurred(error_frame)
  └─ m_busy = false

cancel()
  ├─ m_cancelled = true, abort reply
  ├─ m_busy = false
  └─ emit cancelled()
```

## Ownership, Lifetime, and Nullability

- `m_network_manager` is parented to the client (`QObject` child); destroyed with the client.
- `m_network_reply` is owned by `QNetworkAccessManager` (parented to the manager). The base class holds a raw pointer and calls `deleteLater()` on it in `on_finished()` and at the top of `start_request()`. It is **not** nulled or deleted in `on_error_occurred()` or `cancel()`; cleanup relies on the subsequent `finished` signal or the next `start_request()`.
- `m_coalescing_timer` is parented to the client.
- Parsers returned by `schema_registry::create_parser()` are `std::unique_ptr` and are destroyed at the end of each `process_line()` / `on_finished()` scope. A new parser is allocated per line.
- `s_raw_line_probe` is a process-global `std::function`. It must be cleared before the object it captures is destroyed; there is no automatic cleanup.

## Thread-Safety and Exception-Safety Assumptions

- All `QObject` members and `QNetworkAccessManager` / `QNetworkReply` usage assume single-thread (Qt main thread) access. No mutexes or `QMutexLocker` guards are present.
- `schema_registry::s_active_schema` and `model_client_base::s_raw_line_probe` are mutable process-global statics with no synchronization.
- `DEBUG_ASSERT(!m_busy)` in `start_request()` and each `send_request()` overload is a no-op in release builds (`_DEBUG` undefined). There is no release-mode guard against concurrent requests.
- Exceptions thrown inside `process_line()` (e.g., from the raw-line probe) propagate through the Qt slot `on_ready_read()`. Qt's event loop does not catch exceptions from slots; an uncaught exception will terminate the application.

## Input Validation and Error Handling

- **Endpoint URL**: passed directly to `QUrl` with no validation. An empty or malformed URL produces a failed network request surfaced via `error_occurred`.
- **File path** (`image_to_text_client::send_request(QString, QString)`): `QFile::open` failure emits `error_occurred(-1, …)`. No file-size limit; the entire file is read into memory.
- **MIME detection**: unknown extensions default to `image/png`. `QImage` format detection always returns `image/png`.
- **JSON parse failures** in `process_line()`: malformed lines are logged via `qWarning()` and skipped; the stream continues.
- **Network errors**: `on_error_occurred()` emits `error_occurred` but does not prevent `on_finished()` from also emitting `completion` (see Static Analysis, Finding 1).
- **Prompt sanitization**: `sanitize_prompt()` inserts zero-width spaces around `<|…|>` tokens in the user prompt only. System and assistant prompts are not sanitized.

## Contextual Dependencies

- `backend/core/expected.hpp` — provides `error_frame` (used in `error_occurred` signal) and the `expected<T>` template (included but not directly used in this pair).
- `backend/core/assert.hpp` — provides `DEBUG_ASSERT` / `RUNTIME_ASSERT` macros.
- `QNetworkAccessManager`, `QNetworkReply`, `QTimer`, `QJsonDocument` — Qt Network and Core modules.
- Downstream consumers (frontend QML, CLI) connect to the signals; they are not part of this pair.

## Static Analysis and Security

### Finding 1 — `completion` emitted after network error (logic error)

- **Evidence**: `on_error_occurred()` sets `m_busy = false` and emits `error_occurred`, but does not set any flag to suppress the subsequent `on_finished()`. Qt emits `finished` after `errorOccurred` on the same reply. `on_finished()` checks only `m_cancelled`, so it proceeds to emit `completion(m_accumulated_text, m_current_stats)` with whatever partial text was accumulated before the error.
- **Risk**: Consumers receive both an error and a completion for the same request. If they treat `completion` as success (e.g., display the text, advance workflow state), a failed request is silently treated as a partial success.
- **Impact**: Correctness — OCR or chat workflows may proceed with truncated or empty text after a network failure, producing incorrect output without any visible error.
- **Mitigation**: Add a `bool m_error_occurred` flag set in `on_error_occurred()` and checked at the top of `on_finished()` to suppress the `completion` emission. Alternatively, check `m_network_reply->error() != QNetworkReply::NoError` before emitting completion.
- **Follow-up test**: Start a mock server that accepts the connection then resets it mid-stream. Assert that `error_occurred` is emitted and `completion` is **not** emitted.

### Finding 2 — Busy guard is debug-only; concurrent requests corrupt state in release builds

- **Evidence**: `start_request()` begins with `DEBUG_ASSERT(!m_busy)`, which compiles to nothing when `_DEBUG` is undefined. Each `send_request()` overload also carries the same debug-only assert. In a release build, calling `send_request()` while a previous request is in flight resets all buffers (`m_accumulated_text`, `m_chunk_buffer`, `m_line_carryover`, `m_full_body`, `m_stream_finished`, `m_current_stats`) and calls `deleteLater()` on the in-flight reply, then issues a new POST.
- **Risk**: The old reply's `readyRead` / `finished` signals remain connected to the same slots. When they fire (before or after the deferred deletion), the slots operate on `m_network_reply`, which now points to the **new** reply. `on_ready_read()` calls `m_network_reply->readAll()` and reads data from the wrong request. `on_finished()` may emit a `completion` for the new request using stale or mixed buffer contents.
- **Impact**: Correctness and stability — interleaved requests produce garbled text, lost chunks, or spurious completions. In a CLI batch workflow this could silently corrupt OCR output.
- **Mitigation**: Replace `DEBUG_ASSERT(!m_busy)` with a release-mode guard: if `m_busy`, either emit `error_occurred` and return, or queue the request. Additionally, disconnect the old reply's signals before calling `deleteLater()`, or capture the reply pointer in a lambda so slots operate on the correct object.
- **Follow-up test**: In a release build, issue two rapid `send_request()` calls against a mock server with artificial latency. Assert that each completion contains only the text from its own request.

### Finding 3 — Signal connections from superseded reply are not disconnected

- **Evidence**: `start_request()` connects `readyRead`, `finished`, and `errorOccurred` on the new reply to the base-class slots. When a previous reply still exists, it is `deleteLater()`'d but its signal connections remain active until the object is actually destroyed. The slots use the member `m_network_reply` rather than the sender, so they cannot distinguish which reply emitted the signal.
- **Risk**: As described in Finding 2, a pending signal from the old reply causes the slot to read from or finalize the new reply's state.
- **Impact**: Same as Finding 2 — cross-request data corruption.
- **Mitigation**: Call `disconnect(m_network_reply)` before `deleteLater()`, or use a per-reply lambda: `connect(reply, &QNetworkReply::readyRead, this, [this, reply]() { if (reply == m_network_reply) on_ready_read(); });`.
- **Follow-up test**: Same as Finding 2.

### Finding 4 — Process-global mutable statics without synchronization (data race)

- **Evidence**: `schema_registry::s_active_schema` is written by `set_active_schema()` and read by `get_active_schema()` / `create_parser()` with no mutex. `model_client_base::s_raw_line_probe` is a `std::function` written by `set_raw_line_probe()` / `clear_raw_line_probe()` and read in `process_line()`. Both are plain static members.
- **Risk**: If any code path (e.g., a worker thread, a QML binding evaluation, or a test running in parallel) calls `set_active_schema` or `set_raw_line_probe` concurrently with a request in flight, this is a data race (undefined behavior per the C++ memory model). Reading a `std::function` while it is being assigned can crash.
- **Impact**: Stability — intermittent crashes or parser-type mismatches under concurrent use. In practice the Qt main-thread event loop likely serializes access, but the code does not document or enforce this.
- **Mitigation**: Document the single-thread requirement explicitly in the header. If multi-threaded use is ever anticipated, protect the statics with a `QMutex` or make them per-instance rather than global.
- **Follow-up test**: A stress test that toggles `set_active_schema` from a separate thread while a request is streaming; verify no crash (will require TSan to be meaningful).

### Finding 5 — Exception in raw-line probe propagates through Qt slot

- **Evidence**: `process_line()` calls `s_raw_line_probe(line)` unconditionally when the probe is set. The probe is a user-supplied `std::function`. If it throws (e.g., an assertion failure in test code, a `std::bad_alloc`), the exception propagates through `on_ready_read()` (a Qt slot). Qt's event loop does not catch exceptions from slots; the application terminates.
- **Risk**: A misbehaving test probe or a memory allocation failure in the probe crashes the entire application rather than being contained to the test.
- **Impact**: Stability — a test-only hook can take down the production process if accidentally left installed.
- **Mitigation**: Wrap the probe call in a `try/catch` that logs and continues, or document that the probe must be noexcept. Consider making the probe type `std::function<void(const QString &)>` with a documented noexcept contract.
- **Follow-up test**: Install a probe that throws `std::runtime_error`, send a request against a mock server, and verify the application does not crash (after mitigation).

### Finding 6 — Cancel path leaves `m_network_reply` dangling until next request

- **Evidence**: `cancel()` aborts the reply and sets `m_busy = false` but does not call `deleteLater()` or null `m_network_reply`. The aborted reply will emit `finished`, but `on_finished()` returns early because `m_cancelled` is true, so the reply is never cleaned up in that path. The pointer remains valid (the reply is a child of the manager) until the next `start_request()` calls `deleteLater()`.
- **Risk**: The aborted reply object lingers in memory. If the client is long-lived and cancel is called frequently without subsequent requests, multiple aborted replies accumulate as children of the manager.
- **Impact**: Memory — bounded by the number of cancels before the next request or client destruction, but unbounded over a long session with many cancels.
- **Mitigation**: In `cancel()`, call `m_network_reply->deleteLater(); m_network_reply = nullptr;` after aborting.
- **Follow-up test**: Call `cancel()` N times without intervening requests and verify the manager's child count does not grow.

### Finding 7 — Per-line parser allocation

- **Evidence**: `process_line()` calls `schema_registry::create_parser()` (which does `std::make_unique<…>`) for every line of a streamed response. A typical LLM response may produce hundreds or thousands of lines.
- **Risk**: Unnecessary heap allocation and deallocation per token chunk.
- **Impact**: Performance — measurable overhead on high-throughput streaming, though likely small relative to network latency.
- **Mitigation**: Cache a single parser instance as a member (created once in the constructor or lazily) and reuse it across lines. The parsers are stateless, so this is safe.
- **Follow-up test**: Benchmark a 10k-token stream with per-line allocation vs. a cached parser.

### Finding 8 — MIME type default mislabels unknown image formats

- **Evidence**: `detect_mime_from_extension()` returns `"image/png"` for any suffix not in {png, jpg, jpeg, gif, webp}. A `.bmp`, `.tiff`, or `.heic` file is base64-encoded and labeled as PNG.
- **Risk**: The server receives a data URI with a mismatched MIME type. Depending on the server, it may reject the request, misinterpret the bytes, or produce garbled OCR output.
- **Impact**: Correctness — silent mislabeling of common image formats.
- **Mitigation**: Either extend the suffix table to cover all supported formats, or return an empty string / emit `error_occurred` for unrecognized extensions so the caller can decide.
- **Follow-up test**: Call `send_request` with a `.bmp` file and verify the emitted data URI has the correct MIME type (or that an error is emitted).

### Finding 9 — Unbounded file read in image-to-text path

- **Evidence**: `image_to_text_client::send_request(const QString &file_path, …)` calls `file.readAll()` with no size check. A multi-gigabyte file is loaded entirely into a `QByteArray`, then base64-encoded (≈ 33 % size increase), then embedded in a JSON payload.
- **Risk**: Memory exhaustion if the file path is user-controlled or points to an unexpectedly large file.
- **Impact**: Stability — process crash or system-wide memory pressure.
- **Mitigation**: Check `QFileInfo::size()` against a configurable maximum before reading, and emit `error_occurred` if exceeded.
- **Follow-up test**: Call `send_request` with a path to a file larger than the limit and verify an error is emitted without allocating the full buffer.

### Finding 10 — Only user prompt is sanitized; system and assistant prompts are not

- **Evidence**: `text_to_text_client::format_payload()` calls `sanitize_prompt(m_user_prompt)` but passes `m_system_prompt` and `m_assistant_prompt` through unchanged. The comment states the intent is to "prevent prompt injection" from user input.
- **Risk**: If the system or assistant prompt is also user-configurable at runtime (it is, via Q_PROPERTY setters), it can contain `<|…|>` tokens that are not neutralized. On the native path, the assistant prompt is concatenated directly into the prompt string, so an unsanitized token there is sent verbatim to the model.
- **Impact**: Correctness / security — a crafted system or assistant prompt could inject special tokens that alter model behavior (e.g., forcing a stop sequence, switching role).
- **Mitigation**: Apply `sanitize_prompt()` to all three prompts, or document explicitly which prompts are trusted and which are not. If only the user prompt is untrusted, add a comment stating that system/assistant prompts are assumed to be developer-controlled.
- **Follow-up test**: Set a system prompt containing `<|stop|>` and verify it is either sanitized or documented as trusted.

### Residual risks and assumptions not fully analyzed

- **Qt event-loop reentrancy**: If a slot handler (e.g., an `incremental_chunk` consumer) synchronously calls `send_request()` or `cancel()`, the state machine may be re-entered mid-processing. The code does not guard against this.
- **`QUrl` construction from `m_endpoint_url`**: No validation that the URL is absolute, uses http/https, or is well-formed. Malformed URLs produce a failed request surfaced only via `error_occurred`.
- **Non-streaming body size**: `m_full_body` grows without bound for large non-streaming responses (e.g., a model returning a very long document). No size cap is enforced.
- **OpenAI `choices[0].text` fallback in `parse_line`**: The streaming parser falls back to `choices[0].text` (a completions-API field) when `delta.content` is absent. This path is exercised only by non-standard OpenAI-compatible servers; its correctness depends on the server's actual wire format (contextual assumption).
- **Native schema role discarding**: The native parser concatenates all message contents with `\n` and ignores roles. Whether the target llama.cpp endpoint can distinguish system/user/assistant turns from plain text is a property of the server, not this code (contextual assumption).
