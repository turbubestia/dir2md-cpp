# Implementation Plan: 8-implement-model-enpoint

[Analysis Reference](./8-implement-model-enpoint.plan.analysis.md)

---

## Traceability Map

| Phase | Analysis Section(s) Referenced |
|-------|-------------------------------|
| Phase 1 — Error Handling Framework | Section 2 (`expected.hpp`/`expected.cpp`), Section 3 (Error Handling table) |
| Phase 2 — CMake Infrastructure | Section 2 (`CMakeLists.txt` files), Section 4 (Build & Integration checklist) |
| Phase 3 — Schema Parser Interface | Section 2 (`model.hpp` schema_parser declarations), Section 3 (Schema Registry) |
| Phase 4 — Model Client Base Class | Section 2 (`model.hpp`/`model.cpp` base class), Section 3 (Network Resilience, Performance) |
| Phase 5 — Image-to-Text Client | Section 2 (`image_to_text_client`), Section 3 (Image file edge cases) |
| Phase 6 — Text-to-Text Client | Section 2 (`text_to_text_client`), Section 3 (Prompt Injection Protection) |
| Phase 7 — Schema Registry | Section 2 (`schema_registry`), Section 4 (Schema Registry checklist) |
| Phase 8 — Unit Tests & Coverage | Section 2 (`test/backend/core/`), Section 4 (all Verification Checklists) |

---

## Phase 1: Error Handling Framework

**Reference:** Analysis Section 2 (`./src/backend/core/expected.hpp`, `expected.cpp`) & Section 3 (Error Handling table).

### Objectives

Establish the foundational error handling infrastructure before any model client code depends on it. This is a header-only template class with a small non-template implementation file.

### Steps

1. **Create `error_frame` struct in `expected.hpp`**
   - Define a struct named `error_frame` with three members: an integer error code, a QString description, and a `std::source_location` with a default argument of `std::source_location::current()`.
   - All names must use snake_case per project convention.

2. **Create `expected<T>` template class in `expected.hpp`**
   - Define a header-only template class that holds either a value of type `T` or an `error_frame`.
   - Implement state query: `has_value()` returning bool.
   - Implement accessors with trailing return type syntax: `value_or(default_value)`, `value()` (throws `std::runtime_error` if error state), and `error()` (returns the stored `error_frame`).
   - Provide factory functions: `make_expected<T>(value)` for success state and `make_expected_error<T>(code, desc, location)` for error state.
   - Implement copy and move semantics that correctly preserve both success and error states.
   - Do NOT apply `[[nodiscard]]` per project convention.

3. **Create `error_stack` class declaration in `expected.hpp`**
   - Declare a non-template class with methods: `push(const error_frame&)`, `frames() const` returning a vector of frames, `clear()`, and `to_string() const` for formatted output.
   - The method declarations go in the header; implementation goes in `.cpp`.

4. **Create `expected.cpp` with `error_stack` implementation**
   - Implement all non-template `error_stack` methods.
   - The `to_string()` method should produce a human-readable multi-line dump of all accumulated error frames including file paths and line numbers from source_location.

5. **Add both files to CMake sources list**
   - Add `expected.hpp` and `expected.cpp` to the `target_sources()` call in `src/backend/core/CMakeLists.txt`.

### Exit Criterion

- Backend library compiles without errors after adding the new files to CMake.
- The `expected<T>` template is usable from any backend source file via `#include "backend/core/expected.hpp"`.

### Validation Command

```bash
cmake --build --preset debug
```

---

## Phase 2: CMake Infrastructure Updates

**Reference:** Analysis Section 2 (`CMakeLists.txt` files) & Section 4 (Build & Integration checklist).

### Objectives

Wire up the Qt dependencies required by the model client classes before implementing them. This ensures that subsequent phases can compile without further build system changes.

### Steps

1. **Add `Qt6::Network` to backend library linkage**
   - In `src/backend/CMakeLists.txt`, add `Qt6::Network` to the `target_link_libraries(dir2md_backend PUBLIC ...)` call alongside the existing `Qt6::Core` and `fzy`.
   - This is required for `QNetworkAccessManager` used by the model client base class.

2. **Add `Qt6::Gui` to backend library linkage**
   - In the same `target_link_libraries()` call, add `Qt6::Gui` for `QImage` support required by the image-to-text client.

3. **Update CLI target to link backend**
   - In `src/cli/CMakeLists.txt`, add `dir2md_backend` to `target_link_libraries(dir2md_cli PRIVATE ...)`.
   - The QtGui and QtNetwork dependencies will be pulled in transitively from the backend library.

4. **Verify root CMake already finds required components**
   - Confirm that `find_package(Qt6 REQUIRED COMPONENTS Core Network Quick QuickControls2 Test)` in the root `CMakeLists.txt` already includes `Network`. It does — no change needed there.

### Exit Criterion

- Backend library and CLI executable both compile cleanly with the new linkage.
- No undefined reference errors when including Qt Network or Gui headers in backend source files.

### Validation Command

```bash
cmake --build --preset debug
```

---

## Phase 3: Schema Parser Interface & Concrete Parsers

**Reference:** Analysis Section 2 (`model.hpp` schema_parser declarations).

### Objectives

Define the abstract parsing interface and both concrete implementations before the base client class needs them. This bottom-up approach ensures the base class has a complete dependency to delegate to.

### Steps

1. **Declare `api_schema_parser` abstract interface in `model.hpp`**
   - Define a pure virtual class (not inheriting QObject) with:
     - A pure virtual method for parsing a single streamed line of data and returning a parsed token result struct containing the extracted text fragment.
     - A pure virtual method for constructing the request payload as a `QJsonDocument` given the parameters (messages, temperature, etc.).
   - Define an enum `schema_type` with values for OpenAI and Native formats.

2. **Declare `openai_schema_parser` concrete class in `model.hpp`**
   - Derive from `api_schema_parser`.
   - Implement parsing of NDJSON lines: extract text from `choices[0].delta.content` field, and extract token usage metadata from a final `usage` object if present.
   - Implement request construction using the OpenAI-compatible message array format with role/content objects.

3. **Declare `native_schema_parser` concrete class in `model.hpp`**
   - Derive from `api_schema_parser`.
   - Implement parsing of llama.cpp native JSON response lines: extract text from the `content` field and timing metrics from metadata fields.
   - Implement request construction using the native prompt string format with `<|...|>` delimiters.

4. **Implement both parser classes in `model.cpp`**
   - Use `QJsonDocument::fromJson()` for line parsing.
   - Handle missing or null JSON fields gracefully — return empty strings rather than crashing on malformed input.
   - For the OpenAI parser, check both `choices[0].delta.content` (streaming) and `choices[0].text` (non-streaming fallback).

### Exit Criterion

- Both parser classes compile and can be instantiated independently of any network or client code.
- Parser methods handle empty input and malformed JSON without throwing exceptions (return empty results instead).

### Validation Command

```bash
cmake --build --preset debug
```

---

## Phase 4: Model Client Base Class

**Reference:** Analysis Section 2 (`model.hpp`/`model.cpp` base class), Section 3 (Network Resilience, Performance/Scale Impact, Coalescing Timer).

### Objectives

Implement the abstract QObject base class that manages HTTP streaming, coalescing, token statistics, and busy-state enforcement. This is the core infrastructure consumed by both derived clients.

### Steps

1. **Declare `token_stats` struct in `model.hpp`**
   - Define a plain struct with fields for: total_tokens (int), tokens_per_sec (double), generation_time_ms (qint64), prompt_tokens (int), and prompt_eval_time_ms (qint64).
   - Provide a default constructor zero-initializing all fields.

2. **Declare `model_client_base` abstract class in `model.hpp`**
   - Inherit from QObject with Q_OBJECT macro.
   - Define Q_PROPERTY for: endpoint_url (QString), temperature (float), and coalescing_interval_ms (int with default 250).
   - Declare signals: incremental_chunk (QString), completion (QString, token_stats), cancelled (), and error_occurred (error_frame).
   - Declare public methods: abstract `send_request()` (pure virtual), concrete `cancel()`, and `is_busy() const`.
   - Declare internal members: QNetworkAccessManager pointer, QNetworkReply pointer, QTimer pointer for coalescing, QString buffer for accumulated text, QString buffer for coalesced chunk, token_stats member, bool busy flag, and bool cancelled flag.

3. **Implement constructor and property accessors in `model.cpp`**
   - Initialize QNetworkAccessManager as a child of this object.
   - Initialize QTimer as SingleShot mode, connected to the coalescing emit logic.
   - Implement getter/setter for each Q_PROPERTY with proper notify signals if needed.

4. **Implement HTTP POST streaming in `model.cpp`**
   - Construct a QNetworkRequest with the endpoint_url property.
   - Set appropriate Content-Type header (application/json).
   - Call QNetworkAccessManager::post() with the request payload generated by the active schema parser.
   - Connect the reply's readyRead signal to a slot that processes incoming data line-by-line.
   - Connect the reply's finished signal to a completion handler.
   - Connect the reply's errorOccurred signal to an error handler that constructs an error_frame from the QNetworkReply error.

5. **Implement coalescing timer logic in `model.cpp`**
   - On each incoming token (parsed via schema parser), append to a chunk buffer and restart the SingleShot QTimer with coalescing_interval_ms.
   - On timer timeout, emit incremental_chunk with the current chunk buffer contents, then clear the buffer.
   - Also append each token to the full accumulated text string for the completion signal.

6. **Implement `cancel()` method in `model.cpp`**
   - Set the internal cancelled flag to true.
   - Call QNetworkReply::abort() if a reply exists.
   - Reset busy flag.
   - Emit the cancelled() signal.
   - In the finished handler, check the cancelled flag to distinguish an abort from normal completion — do NOT emit completion if cancelled.

7. **Implement busy-state enforcement in `model.cpp`**
   - At the entry of any send method, check is_busy(). If true, use DEBUG_ASSERT (from assert.hpp) in debug builds and throw std::runtime_error in release builds.
   - Set busy flag to true before starting a request and false on completion or cancellation.

8. **Implement schema parser delegation in `model.cpp`**
   - Use schema_registry::create_parser() to obtain the active parser instance for parsing incoming lines and constructing request payloads.
   - The parser is used transiently — created when needed, not stored as a long-lived member.

### Exit Criterion

- Base class compiles as an abstract QObject with all signals and properties declared.
- Attempting to instantiate `model_client_base` directly fails at compile time (pure virtual method).
- Derived classes can override `send_request()` without reimplementing network or coalescing logic.

### Validation Command

```bash
cmake --build --preset debug
```

---

## Phase 5: Image-to-Text Client

**Reference:** Analysis Section 2 (`image_to_text_client`), Section 3 (Image file edge cases).

### Steps

1. **Declare `image_to_text_client` class in `model.hpp`**
   - Derive from `model_client_base`.
   - Declare two public methods: one accepting a QString file path, and one accepting a QImage directly.
   - Both methods return void (results delivered via base class signals).

2. **Implement file path method in `model.cpp`**
   - Read the file using QFile::readAll() and wrap the result in an expected<QByteArray> pattern: if the file cannot be opened, emit error_occurred with an appropriate error_frame and return early.
   - Detect MIME type from the file extension (e.g., .png → image/png, .jpg → image/jpeg).
   - Base64-encode the raw bytes and construct a data URI string in the format `data:image/<mime>;base64,<encoded_data>`.
   - Construct the request payload using the active schema parser's request formatting method, passing the data URI as the image content.
   - Call the base class's internal HTTP POST mechanism (or reimplement send_request to use this payload).

3. **Implement QImage method in `model.cpp`**
   - Convert QImage to QByteArray using QImage::save() with a format detected from the QImage::format().
   - Base64-encode and construct data URI as above.
   - Proceed with request construction and dispatch.

4. **Override `send_request()` in `model.cpp`**
   - The image-to-text client's send_request constructs the vision-specific request payload (image + optional text prompt) and delegates HTTP execution to the base class infrastructure.

### Exit Criterion

- Both overloads compile and correctly produce base64 data URIs for common image formats (PNG, JPEG).
- File-not-found scenario emits an error_occurred signal rather than crashing.

### Validation Command

```bash
cmake --build --preset debug
```

---

## Phase 6: Text-to-Text Client

**Reference:** Analysis Section 2 (`text_to_text_client`), Section 3 (Prompt Injection Protection, Security & Permissions).

### Steps

1. **Declare `text_to_text_client` class in `model.hpp`**
   - Derive from `model_client_base`.
   - Declare Q_PROPERTY members for: system_prompt (QString), user_prompt (QString), and assistant_prompt (QString).
   - Declare public methods: concrete `send_request()` override and `sanitize_prompt(const QString&)` returning QString.

2. **Implement prompt sanitization in `model.cpp`**
   - Build a regex pattern that matches llama.cpp special token delimiters of the form `<|...|>`.
   - The pattern should match any substring starting with `<|` and ending with `|>` to catch all special tokens (begin_of_text, end_of_text, reserved_special_token_*, etc.).
   - Replace each match by inserting a zero-width space (`\u200B`) after the opening `<` and before the closing `>`, effectively neutralizing the token while preserving visual appearance.
   - This method is applied to user_prompt and any OCR-extracted text before constructing the request.

3. **Implement OpenAI schema request construction in `model.cpp`**
   - When the active schema is OpenAI, construct a JSON message array with system and user role objects only.
   - The assistant_prompt property is discarded for OpenAI format (not used).
   - Apply sanitize_prompt() to the user_prompt before embedding in the message array.

4. **Implement native schema request construction in `model.cpp`**
   - When the active schema is Native, concatenate system_prompt, user_prompt, and assistant_prompt into a single prompt string using `<|...|>` delimiter tokens between segments.
   - Apply sanitize_prompt() to user-provided text segments but NOT to the delimiter tokens themselves.

5. **Implement post-processing in `model.cpp`**
   - Before emitting the completion signal, strip all zero-width space characters (`\u200B`) from the accumulated response text to prevent them from leaking into user-visible content.

6. **Override `send_request()` in `model.cpp`**
   - Check busy state (inherited enforcement from base class).
   - Select schema parser via registry, construct payload with sanitized prompts, and dispatch via base class HTTP mechanism.

### Exit Criterion

- Prompt sanitization correctly neutralizes `<|begin_of_text|>` by inserting zero-width spaces.
- OpenAI format produces a message array with system + user roles only.
- Native format produces a concatenated prompt string with delimiter tokens.
- Completion text is free of zero-width space characters.

### Validation Command

```bash
cmake --build --preset debug
```

---

## Phase 7: Schema Registry

**Reference:** Analysis Section 2 (`schema_registry`), Section 4 (Schema Registry checklist).

### Steps

1. **Declare `schema_registry` class in `model.hpp`**
   - Define a class with only static members (no instantiation needed).
   - Static member variable holding the current `schema_type` enum value.
   - Static method `set_active_schema(schema_type)` to change the active schema at runtime.
   - Static method `get_active_schema()` returning the current schema_type.
   - Static factory method `create_parser()` returning a `std::unique_ptr<api_schema_parser>` of the appropriate concrete type based on the current active schema.

2. **Implement registry in `model.cpp`**
   - Initialize the static active schema to OpenAI by default (most commonly used format).
   - The factory method uses a simple switch or if-else on the active schema_type to return either an openai_schema_parser or native_schema_parser instance.

3. **Ensure thread-safety consideration**
   - The registry is a simple static variable with no mutex. Document that schema switching should not occur concurrently with active requests. This is acceptable since schema selection is an internal implementation detail, not a user-configurable runtime property.

### Exit Criterion

- Calling `set_active_schema(Native)` followed by `create_parser()` returns a native_schema_parser instance.
- Switching back to OpenAI and calling `create_parser()` returns an openai_schema_parser instance.
- The default active schema (before any explicit set) is OpenAI.

### Validation Command

```bash
cmake --build --preset debug
```

---

## Phase 8: Unit Tests & Coverage

**Reference:** Analysis Section 2 (`test/backend/core/`), Section 4 (all Verification Checklists).

### Objectives

Comprehensive unit test coverage for all new code, executed with coverage instrumentation to verify both correctness and line coverage.

### Steps

1. **Create `test_expected.hpp` and `test_expected.cpp`**
   - Follow the existing test pattern: a QObject subclass with Q_OBJECT macro and private slots for each test method.
   - Test cases for `expected<T>`:
     - Verify success state: construct via make_expected, assert has_value() is true, verify value() returns the stored value.
     - Verify error state: construct via make_expected_error, assert has_value() is false, verify error() returns the correct error_frame with expected code and description.
     - Verify value_or() returns the default when in error state.
     - Verify value() throws std::runtime_error when in error state.
     - Verify copy semantics preserve both success and error states.
     - Verify move semantics transfer state correctly and leave source in a valid but unspecified state.
   - Test cases for `error_frame`:
     - Verify std::source_location captures the correct file path and line number where the frame was constructed.
   - Test cases for `error_stack`:
     - Verify push() appends frames and frames() returns them in insertion order.
     - Verify clear() empties the stack.
     - Verify to_string() produces non-empty output containing error codes and descriptions after pushing frames.

2. **Create `test_model_client.hpp` and `test_model_client.cpp`**
   - Same QObject test class pattern.
   - Test cases for coalescing behavior:
     - Use a mock QNetworkAccessManager or stubbed replies to simulate streamed token arrival.
     - Verify that incremental_chunk signals are emitted at approximately the configured interval, not per-token.
     - Verify that concatenating all incremental chunks produces the same text as the completion signal's full text.
   - Test cases for busy-state enforcement:
     - Simulate a request in progress and verify that a second send_request() triggers an assertion or exception.
   - Test cases for cancel behavior:
     - Call cancel() during a simulated request and verify the cancelled() signal is emitted.
     - Verify that completion is NOT emitted after cancellation.
   - Test cases for token_stats:
     - Simulate a response with usage metadata and verify token_stats fields are populated with non-zero values.

3. **Create `test_schema_parser.hpp` and `test_schema_parser.cpp`**
   - Test cases for OpenAI parser:
     - Parse a sample NDJSON line containing `choices[0].delta.content` and verify the extracted text matches.
     - Parse a malformed JSON line and verify an empty result is returned (no crash).
   - Test cases for Native parser:
     - Parse a sample llama.cpp native JSON line and verify text extraction.
   - Test cases for schema registry:
     - Verify default schema is OpenAI.
     - Verify switching to Native and back produces the correct parser types from create_parser().

4. **Create `test_prompt_sanitization.hpp` and `test_prompt_sanitization.cpp`**
   - Test cases for text_to_text_client sanitization:
     - Verify that `<|begin_of_text|>` in input text is replaced with zero-width space escaped version.
     - Verify that normal text without special tokens passes through unchanged.
     - Verify that post-processing strips zero-width spaces from output text.

5. **Register new test files in CMake**
   - Add the new test header and source files to `test/backend/core/CMakeLists.txt` in the `qt6_add_executable()` call.
   - Add corresponding `qtest_add_test()` calls for CTest integration, following the existing pattern for fzy_test.

6. **Build and run tests with coverage**
   - Configure with the debug-coverage preset.
   - Build all targets including tests.
   - Run the test executable via CTest.
   - Generate the coverage data files (.profraw) and merge into .profdata.
   - Use llvm-cov to display coverage for the new source files (expected.hpp, expected.cpp, model.hpp, model.cpp).

### Exit Criterion

- All new test methods pass with zero failures.
- Coverage report shows that the core logic paths in expected.hpp/.cpp and model.hpp/.cpp are exercised by the tests.
- Network-dependent code paths are tested via mocked QNetworkAccessManager (no real HTTP calls required).

### Validation Commands

```bash
# Configure with coverage instrumentation
cmake --preset debug-coverage

# Build all targets including tests
cmake --build --preset debug-coverage

# Run all tests via CTest
ctest --test-dir build/cmake-debug-coverage

# Merge coverage data
llvm-profdata merge -o default.profdata build/cmake-debug-coverage/*.profraw

# Show coverage for expected.cpp
llvm-cov show build/cmake-debug-coverage/src/backend/core/expected.cpp -instr-profile=default.profdata

# Show coverage for model.cpp
llvm-cov show build/cmake-debug-coverage/src/backend/core/model.cpp -instr-profile=default.profdata
```

---

## Phase 9: Cross-Cutting Validation

**Reference:** Analysis Section 4 (Cross-Cutting checklist), copilot-instructions.md conventions.

### Objectives

Final verification that all project conventions are met and no regressions were introduced.

### Steps

1. **Verify naming conventions across all new code**
   - Confirm all variable names, method names, function names, class names, and namespace names use snake_case.
   - The only exception is QML file names (not applicable here — no QML files created).

2. **Verify trailing return type syntax**
   - Confirm all function and method declarations use trailing return type syntax (e.g., `auto function_name() -> return_type`).

3. **Verify no `[[nodiscard]]` decorators**
   - Search all new files for the `[[nodiscard]]` attribute and confirm none are present.

4. **Verify SettingsManager decoupling**
   - Confirm that no model client class includes or references `settings_manager.hpp`.
   - The endpoint URL and temperature are injected by the caller, not read from SettingsManager internally.

5. **Verify backend library composition**
   - Confirm the backend links only Qt6::Core, Qt6::Gui, and Qt6::Network (no QWidget or QtQuick).
   - Confirm the CLI target links dir2md_backend and transitively receives the Qt dependencies.

6. **Full clean build of all presets**
   - Build with debug preset to verify normal compilation.
   - Build with release preset to verify release-mode behavior (busy-state throws std::runtime_error instead of assert).
   - Build with debug-coverage preset to verify coverage instrumentation still works.

7. **Verify frontend target still builds**
   - Confirm that the frontend application compiles without errors despite the backend changes (no breaking changes to existing API surface).

### Exit Criterion

- All three build presets compile without errors or warnings introduced by this change.
- Grep search for `[[nodiscard]]` in new files returns zero results.
- Grep search for `settings_manager` in model.hpp and model.cpp returns zero results.

### Validation Commands

```bash
cmake --build --preset debug
cmake --build --preset release
cmake --build --preset debug-coverage
```

---

## Summary of New Files

| File | Purpose | Phase Created |
|------|---------|---------------|
| `src/backend/core/expected.hpp` | Template error handling framework | Phase 1 |
| `src/backend/core/expected.cpp` | Non-template error_stack implementation | Phase 1 |
| `test/backend/core/test_expected.hpp` | Unit test declarations for expected | Phase 8 |
| `test/backend/core/test_expected.cpp` | Unit test implementations for expected | Phase 8 |
| `test/backend/core/test_model_client.hpp` | Unit test declarations for model clients | Phase 8 |
| `test/backend/core/test_model_client.cpp` | Unit test implementations for model clients | Phase 8 |
| `test/backend/core/test_schema_parser.hpp` | Unit test declarations for schema parsers | Phase 8 |
| `test/backend/core/test_schema_parser.cpp` | Unit test implementations for schema parsers | Phase 8 |
| `test/backend/core/test_prompt_sanitization.hpp` | Unit test declarations for prompt sanitization | Phase 8 |
| `test/backend/core/test_prompt_sanitization.cpp` | Unit test implementations for prompt sanitization | Phase 8 |

## Summary of Modified Files

| File | Change | Phase Modified |
|------|--------|----------------|
| `src/backend/core/model.hpp` | All class declarations (parsers, clients, registry, token_stats) | Phases 3-7 |
| `src/backend/core/model.cpp` | All class implementations | Phases 3-7 |
| `src/backend/core/CMakeLists.txt` | Add expected.hpp and expected.cpp to sources | Phase 1 |
| `src/backend/CMakeLists.txt` | Add Qt6::Network and Qt6::Gui to linkage | Phase 2 |
| `src/cli/CMakeLists.txt` | Add dir2md_backend to linkage | Phase 2 |
| `test/backend/core/CMakeLists.txt` | Add new test files and qtest_add_test entries | Phase 8 |
