# Implementation Analysis: 8-implement-model-enpoint

## 1. Architectural Impact & Data Flow

### High-Level Overview

This feature introduces two new subsystems into the backend library:

1. **Error Handling Framework** (`expected.hpp/.cpp`): A header-only (with optional cpp) template-based error handling infrastructure that replaces or supplements exception-based error reporting. This is a foundational utility consumed by all subsequent backend code.

2. **Language Model Client Hierarchy** (`model.hpp/.cpp`): A QObject-based class hierarchy for consuming llama.cpp HTTP API endpoints, consisting of:
   - An abstract base class for streamed HTTP responses with coalescing and statistics
   - An image-to-text (OCR) derived class
   - A text-to-text derived class with prompt injection protection
   - A pluggable schema registry for dual-format parsing (OpenAI vs native llama.cpp)

### Affected Subsystems

| Subsystem | Impact |
|-----------|--------|
| **Backend Library** (`dir2md_backend`) | New files added, QtGui dependency introduced, existing empty `model.hpp/.cpp` populated |
| **CLI** (`dir2md_cli`) | QtGui dependency added (transitive via backend), no functional changes yet |
| **Frontend** (`dir2md_frontend`) | No direct changes; will consume backend via model/view layer later |
| **Settings Schema** (`core_schema.cpp`) | Already has `ocr_model::endpoint` and `lang_model::endpoint` keys defined — these will be read by callers to configure the model clients |
| **Tests** (`test/backend/core/`) | New test files required for both error framework and model client classes |

### Data Flow Changes

```
[Caller (CLI/Frontend)] --sets properties--> [Model Client Instance]
    |-- endpoint URL (from SettingsManager, caller's responsibility)
    |-- temperature (from SettingsManager, caller's responsibility)
    |-- coalescing interval (optional override)

[Model Client Instance] --HTTP POST stream--> [llama.cpp Server]
    |-- /v1/chat/completions (OpenAI schema) OR /completion (native schema)
    |   Schema selected at runtime via static registry call

[llama.cpp Server] --SSE/NDJSON stream--> [Model Client Base Class]
    |-- Raw JSON lines or native token stream
    |-- Parsed by active schema parser (pluggable registry)

[Base Class Coalescer] --accumulates tokens for 250ms window--> [Chunk Signal]
    |-- Emits incremental_chunk(const QString&) when timer fires
    |-- Collects token statistics concurrently

[Base Class Completion] --on stream end--> [Completion Signal/Callback]
    |-- Emits completion(const QString& fullText, const TokenStats& stats)
    |-- Returns expected<T> for synchronous callers

[Error Framework] --wraps all failure paths--> [expected<T> or error_frame]
    |-- Network errors, parse errors, server errors all produce error_frame
    |-- error_stack aggregates multiple frames for debugging
```

### Class Hierarchy Design

```
dir2md::backend::
├── expected<T>                    # Template class in expected.hpp
├── error_frame                    # Struct in expected.hpp
├── error_stack                    # Class in expected.hpp/.cpp
│
├── model_client_base              # Abstract QObject in model.hpp/.cpp
│   ├── image_to_text_client       # Derived QObject — OCR
│   └── text_to_text_client        # Derived QObject — prompts
│
├── api_schema_parser              # Pure virtual interface for schema parsing
│   ├── openai_schema_parser       # Concrete: v1/chat/completions NDJSON
│   └── native_schema_parser       # Concrete: /completion native format
│
└── schema_registry                # Singleton/static registry for runtime selection
```

## 2. Component & File Impact Map

### [`./src/backend/core/expected.hpp`]
- **Type of Change:** Create (new file)
- **Structural Changes:**
  - [ ] Define `error_frame` struct with: `int error_code`, `QString description`, `std::source_location location` (with default argument `std::source_location::current()`).
  - [ ] Define template class `expected<T>` with:
    - State: holds either a value of type `T` or an `error_frame`.
    - Accessors: `has_value()`, `value_or(default)`, `value()` (throws if error), `error()` (returns error_frame).
    - Construction: `make_expected<T>(value)` and `make_expected_error<T>(code, desc, location)`.
    - Copy/move semantics for both success and error states.
  - [ ] Define `error_stack` class with:
    - `push(const error_frame&)` — append an error frame.
    - `frames() const` — return all accumulated frames (vector or similar).
    - `clear()` — reset the stack.
    - `to_string() const` — formatted dump of all frames for logging.
  - [ ] All definitions are header-only where possible (template class `expected<T>` must be in header). Non-template `error_stack` may have implementation in `.cpp`.

### [`./src/backend/core/expected.cpp`]
- **Type of Change:** Create (new file)
- **Structural Changes:**
  - [ ] Implement non-template methods for `error_stack` class (if not fully header-only).
  - [ ] Any helper functions for error frame formatting or serialization.

### [`./src/backend/core/model.hpp`]
- **Type of Change:** Modify (currently empty — will contain all declarations)
- **Structural Changes:**
  - [ ] Declare abstract `model_client_base` class inheriting from `QObject`:
    - Properties: `endpoint_url` (QString), `temperature` (float), `coalescing_interval_ms` (int, default 250).
    - Signals: `incremental_chunk(const QString&)`, `completion(const QString&, const token_stats&)`, `cancelled()`, `error_occurred(const error_frame&)`.
    - Methods: `send_request()` (pure virtual or abstract), `cancel()`, `is_busy() const`.
    - Internal state: `QNetworkAccessManager*`, `QNetworkReply*`, `QTimer*` (coalescer), `token_stats` accumulator, busy flag.
  - [ ] Declare `token_stats` struct with:
    - Generation metrics: `total_tokens` (int), `tokens_per_sec` (double), `generation_time_ms` (qint64).
    - Prompt metrics: `prompt_tokens` (int), `prompt_eval_time_ms` (qint64).
  - [ ] Declare `image_to_text_client` class deriving from `model_client_base`:
    - Methods: `send_image(const QString& filePath)` and `send_image(const QImage&)`.
    - Internal: base64 data URI encoding logic.
  - [ ] Declare `text_to_text_client` class deriving from `model_client_base`:
    - Properties: `system_prompt` (QString), `user_prompt` (QString), `assistant_prompt` (QString).
    - Methods: `send_request()` (concrete override), `sanitize_prompt(const QString&) -> QString`.
    - Internal: prompt formatting per active schema, special token escaping.
  - [ ] Declare `api_schema_parser` abstract interface:
    - Pure virtual: `parse_streamed_line(const QByteArray&) -> parsed_token_result`, `format_request(...) -> QJsonDocument`.
  - [ ] Declare `openai_schema_parser` and `native_schema_parser` concrete classes.
  - [ ] Declare `schema_registry` class with static methods:
    - `set_active_schema(schema_type)` — runtime selection (enum: OpenAI, Native).
    - `get_active_schema() -> schema_type`.
    - `create_parser() -> std::unique_ptr<api_schema_parser>`.

### [`./src/backend/core/model.cpp`]
- **Type of Change:** Modify (currently empty — will contain all implementations)
- **Structural Changes:**
  - [ ] Implement `model_client_base`:
    - HTTP POST construction with streaming (`QNetworkAccessManager::post`).
    - Coalescing timer logic: accumulate tokens in a buffer, emit `incremental_chunk` on timer timeout, reset buffer.
    - Token statistics collection: parse timing/token count from server response metadata.
    - `cancel()`: call `QNetworkReply::abort()`, emit `cancelled()`.
    - Busy flag enforcement: `Q_ASSERT_X(!m_busy)` in debug, `std::runtime_error` in release.
    - Schema parser delegation: use `schema_registry::create_parser()` to parse incoming data.
  - [ ] Implement `image_to_text_client`:
    - File path reading: `QFile::readAll()` → base64 encode → construct data URI (`data:image/<mime>;base64,<data>`).
    - QImage encoding: `QImage::save()` to QByteArray → base64 encode → data URI.
    - MIME type detection from file extension or QImage format.
    - Request payload construction per active schema (OpenAI vision format vs native).
  - [ ] Implement `text_to_text_client`:
    - Prompt sanitization: regex-based scan for `<|...|>` patterns, replace with `<\u200B|...\u200B>`.
    - OpenAI schema formatting: construct message array `[{"role":"system",...}, {"role":"user",...}]`, discard assistant prompt.
    - Native schema formatting: concatenate prompts with `<|...|>` delimiters into single prompt string.
    - Post-processing: strip zero-width spaces from final response text.
  - [ ] Implement `openai_schema_parser`:
    - Parse NDJSON lines: each line is a JSON object, extract `choices[0].delta.content` or `choices[0].text`.
    - Extract token usage from final `usage` object (completion_tokens, prompt_tokens, etc.).
  - [ ] Implement `native_schema_parser`:
    - Parse native llama.cpp response format: extract `content` field from each JSON line.
    - Extract timing metrics from response metadata.
  - [ ] Implement `schema_registry`:
    - Static member for active schema type.
    - Factory method returning appropriate parser instance.

### [`./src/backend/CMakeLists.txt`]
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add `Qt6::Gui` to `target_link_libraries` (required for `QImage` support in `image_to_text_client`).
  - [ ] Add `expected.hpp` and `expected.cpp` to `target_sources` via the core subdirectory CMakeLists.

### [`./src/backend/core/CMakeLists.txt`]
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add `expected.hpp` and `expected.cpp` to the `target_sources(dir2md_backend PRIVATE ...)` list.

### [`./src/cli/CMakeLists.txt`]
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Add `Qt6::Gui` to `target_link_libraries` (transitive dependency via backend, but may need explicit declaration since backend now links QtGui).
  - [ ] Ensure `dir2md_backend` is linked (currently only links `Qt6::Core`).

### [`./src/backend/core/core_schema.cpp`]
- **Type of Change:** No change required (informational)
- **Notes:**
  - Already defines `ocr_model::endpoint` and `lang_model::endpoint` settings keys. The caller (CLI or frontend controller) will read these values from `SettingsManager` and inject them into the model client instances via property setters. No coupling between `SettingsManager` and model clients.

### [`./test/backend/core/`]
- **Type of Change:** Create new test files
- **Structural Changes:**
  - [ ] Create `test_expected.cpp`: Unit tests for `expected<T>`, `error_frame`, and `error_stack`.
    - Test success/error state transitions.
    - Test `source_location` capture accuracy.
    - Test `error_stack` aggregation and formatting.
  - [ ] Create `test_model_client.cpp`: Unit tests for model client hierarchy.
    - Test coalescing interval behavior (mock network replies).
    - Test busy flag enforcement (second request triggers assert/exception).
    - Test cancel behavior.
    - Test prompt sanitization in `text_to_text_client`.
    - Test schema registry runtime switching.
    - Test image-to-base64 encoding in `image_to_text_client`.
    - **Note:** HTTP integration tests will require a mock server or `QNetworkAccessManager` stubbing.

## 3. Boundary & Edge Case Analysis

### Error Handling

| Scenario | Mechanism | Consumer Pattern |
|----------|-----------|------------------|
| Network timeout / DNS failure | `error_frame` with network error code, emitted via `error_occurred` signal | GUI: connect signal to error handler. CLI: check `expected<T>` from QFuture. |
| HTTP 4xx/5xx server error | Parse status code from `QNetworkReply`, construct `error_frame` with HTTP status | Same as above. |
| JSON parse error (malformed response) | `error_frame` with parse error code, includes raw payload snippet | Same as above. |
| Schema mismatch (parser can't decode line) | `error_frame` from schema parser, pushed to `error_stack` for aggregation | Same as above. |
| Image file not found / unreadable | `expected<bool>` or `expected<QImage>` from file read, before request is sent | Caller checks `expected` before invoking send. |
| Second request on busy instance | Debug assert (`Q_ASSERT_X`) in debug builds, `std::runtime_error` in release builds | Caller must check `is_busy()` or wait for completion before reusing. |

### Security & Permissions

- **Prompt Injection Protection:** The `text_to_text_client` must sanitize all user-supplied text (especially OCR-extracted text) for llama.cpp special tokens (`<|begin_of_text|>`, `<|end_of_text|>`, `<|reserved_special_token_...|>`, etc.). The sanitization inserts zero-width spaces (`\u200B`) within the token delimiters to neutralize them.
- **Token Pattern Registry:** The set of special tokens to sanitize should be maintainable as a list/regex pattern, not hardcoded inline, so new tokens can be added without code changes.
- **Post-Processing Strip:** After response completion, zero-width space escapes must be stripped from the final output text to prevent them from leaking into user-visible content.

### Performance / Scale Impact

- **Coalescing Timer:** Default 250ms interval balances UI responsiveness against signal emission overhead. The timer is a `QTimer::SingleShot` re-triggered on each incoming token, so it has minimal CPU cost during idle periods.
- **Schema Parser Overhead:** JSON parsing per line via `QJsonDocument::fromJson()` is the dominant per-token cost. This is unavoidable for NDJSON streaming but acceptable given typical token generation rates (~10–100 tokens/sec).
- **Memory:** Accumulated full text is stored in a `QString` member. For very long responses (e.g., large document OCR), this could grow to several MB. No explicit size limit is imposed, but the caller should be aware.
- **Concurrent Requests:** Each instance handles one request. Multiple concurrent operations require multiple instances, each with its own `QNetworkAccessManager`. This is memory-proportional to concurrency level but avoids shared-state complexity.

### Network Resilience

- **Connection Abort on Cancel:** `QNetworkReply::abort()` is called on `cancel()`, which releases the socket. The reply's `finished()` signal still fires, so the base class must check an internal `m_cancelled` flag to distinguish abort from normal completion.
- **Reconnection:** No automatic retry logic. The caller is responsible for re-instantiating or re-invoking if a network error occurs.

### CLI Async Pattern Considerations

- **Primary (QPromise):** Qt 6.3+ provides `QPromise<T>` which integrates with `QThreadStorage` and the event loop. The base class can expose a method returning `QFuture<expected<result_t>>` that resolves on completion. Incremental chunks are still delivered via signals, which CLI code can connect to with lambdas for stdout printing.
- **Fallback (STL):** If QPromise proves insufficient (e.g., composition issues), a wrapper class using `std::packaged_task`, `std::future`, and `std::condition_variable` can provide a blocking `wait()` method. This runs alongside the QObject signal system — the signal handler sets the condition variable when completion arrives.
- **Decision Point:** The implementation should first attempt the QPromise approach. If it requires excessive boilerplate or has composability issues, fall back to the STL pattern. The acceptance criteria in LM-09 explicitly call for this evaluation before locking the final pattern.

## 4. Verification Checklist

### Error Framework (`expected.hpp/.cpp`)
- [ ] Verify `expected<T>` holds a value and `has_value()` returns true after successful construction.
- [ ] Verify `expected<T>` holds an error and `has_value()` returns false after error construction.
- [ ] Verify `error_frame` captures `std::source_location` with correct file and line number.
- [ ] Verify `error_stack` accumulates multiple frames and `to_string()` produces readable output.
- [ ] Verify `expected<T>` copy/move semantics preserve both success and error states correctly.

### Model Client Base Class
- [ ] Verify coalescing timer emits `incremental_chunk` signals at approximately the configured interval (250ms default), not per-token.
- [ ] Verify emitted chunks are incremental (not full text-so-far).
- [ ] Verify concatenating all incremental chunks produces the same text as the completion signal's full text.
- [ ] Verify `token_stats` contains non-zero values after a successful response with usage metadata.
- [ ] Verify second request on busy instance triggers debug assert (debug build) or throws `std::exception` (release build).
- [ ] Verify `cancel()` aborts the HTTP connection and emits `cancelled()` signal.
- [ ] Verify network error produces `error_frame` via `error_occurred` signal with descriptive message.

### Image-to-Text Client
- [ ] Verify file path input produces correct base64 data URI in request payload.
- [ ] Verify `QImage` input produces correct base64 data URI in request payload.
- [ ] Verify MIME type in data URI matches image format (e.g., `image/png`, `image/jpeg`).
- [ ] Verify completion signal contains extracted text from a known test image.

### Text-to-Text Client
- [ ] Verify prompt sanitization replaces `<|begin_of_text|>` with `<\u200B|begin_of_text|\u200B>`.
- [ ] Verify OpenAI schema formatting produces correct message array (system + user only, assistant discarded).
- [ ] Verify native schema formatting concatenates prompts with `<|...|>` delimiters.
- [ ] Verify final response text has zero-width spaces stripped.
- [ ] Verify no conversation history is retained between requests (single-turn enforcement).

### Schema Registry
- [ ] Verify runtime switch from OpenAI to Native schema takes effect on next request.
- [ ] Verify OpenAI parser correctly extracts tokens from NDJSON `choices[0].delta.content`.
- [ ] Verify Native parser correctly extracts tokens from llama.cpp native JSON lines.
- [ ] Verify schema selection is not exposed as a user-configurable property (internal-only).

### Build & Integration
- [ ] Verify backend library builds with `Qt6::Core` + `Qt6::Gui` linkage (no QWidget/QtQuick).
- [ ] Verify CLI target links against updated backend without errors.
- [ ] Verify frontend target still builds (no breaking changes to existing backend API surface).
- [ ] Verify CMake `target_sources` includes new `expected.hpp/.cpp` files.

### Cross-Cutting
- [ ] Verify no `SettingsManager` reference exists in any model client class (decoupling requirement).
- [ ] Verify all class/function/variable names use **snake_case** per project convention.
- [ ] Verify all function signatures use **trailing return type** syntax per project convention.
- [ ] Verify no `[[nodiscard]]` decorators are present per project convention.
