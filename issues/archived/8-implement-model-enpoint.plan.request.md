# Implement the Language Model Client

In this project we use one vision/language model for OCR processing of PDF and Images and a language model to construct summaries, determine if two adjacent losses pages (from a scanned pages) correspond to the same document, and to sugest file names. This models run on a llama.cpp server over HTTP endpoints using either `v1/chat/completion` or `completion/`.

We need to design a set of base/derive classes to handle this models.

## Goals

- Have one base class that can handle streamed responses for live token generation. This must coalse several server responses into larger chunch to avoid excesive sub-second UI updates. From this responses also collect the token statistics. We must determine if we can handle both openAI `v1/chat/completion` or native `completion/` schemas. Maybe we can use a system with a api class to register either scheme, so the instance of the model client route directly to the chooses schema. Note this is solely for test purpose to determine which one work best for our pusposes, the use will never get to choose (and he shouldn't).

- There will be one derived class to handle the image-to-text. It should recieved either a image filepath to read or the image (like q QImage) and convert it to base64 data URI. Since the OCR model does not care about user request, it does not need a system or user prompt, other than the image it self. The usage class would be instantiate it, send the image request and get back the text page. Note that the base class during reception we coalse multiple response within a given time (like a second, but must be parametrised), when the base class finished will emit a completion signal or callback.

- There will be another derived class to handle text-to-text which can be customized with system, user, and assistant prompts. When using `v1/chat/completion` only system and user prompt makes senses, and assistant prompt should be just discaded. When using `completion/` we must merge the prompt with the appopiate tokens like `<|...|>`, however we must pre-scan the prompts to remove them to prevent prompt-injection attacks. It would be possible for example to a image of PDF that have this tokens in the text and must no be allowed. This token must be converted to `<\u200B|...|\u200B>` using the zero width spaces. Later we can remove them in the final response. The usage would be instantiate it, set the prompts, send the request

- This class must be usable in the CLI without UI and in the frontend with UI in QtQuick. Find a good pattern for the CLI such that we can print to stdout the OCR text live. Note, we are not asking to implement this feature in the CLI, but design the language model client classes to support this.

- None of this class should have access to the setting. It is the caller who will read the settings to set the options. This will decouple the classes and make the test easier.

- We need to able to set as properties the endpoint URL, which will be of the form `http://IP:port` and `temperature`. We don't need to define the model name since llama.cpp does not care about it.

WRITE NEXT ITERATION BELOW THIS LINE. DON'T DELETE OR EDIT ANYTHING ABOVE.
-------------------------------------------------------------------------------

# Refinement Iteration 1

**Status:** PENDING USER FEEDBACK

## 1. Executive Summary

Design and specify a C++ class hierarchy (backend library) for consuming llama.cpp HTTP API endpoints. The system provides a base class for streamed token responses with coalescing and statistics, an image-to-text derived class for OCR, and a text-to-text derived class with prompt injection protection. The design must support both OpenAI-compatible (`v1/chat/completions`) and native llama.cpp (`/completion`) schemas, be usable from both Qt GUI (signals/slots) and CLI (callbacks), and remain decoupled from any settings management.

## 2. Refined Requirements & Acceptance Criteria

- **Requirement [LM-01]:** Streamed Response Base Class
  - **Description:** An abstract base class that manages HTTP streaming connections to a llama.cpp server, coalesces sub-second token responses into configurable chunks, and collects token statistics (tokens count, tokens/sec, timing).
  - **Acceptance Criteria:**
    - [ ] Given a valid endpoint URL, When the request is sent, Then the class establishes an HTTP streaming connection and yields partial results over time.
    - [ ] Given a coalescing interval (parametrizable in ms), When multiple server responses arrive within the interval, Then they are accumulated and emitted as a single chunk after the interval elapses.
    - [ ] Given a completed response stream, When all tokens are received, Then token statistics (total tokens, tokens/sec, elapsed time) are available.
    - [ ] Given a completion event, When the stream ends, Then a completion signal/callback is emitted with the full accumulated text and statistics.

- **Requirement [LM-02]:** Dual Schema Support (OpenAI & Native)
  - **Description:** The base class supports both `v1/chat/completions` (OpenAI-compatible JSON lines) and `/completion` (native llama.cpp) response schemas through a pluggable API schema registry.
  - **Acceptance Criteria:**
    - [ ] Given a registered schema parser, When a streamed response arrives, Then the correct parser extracts tokens from the response format.
    - [ ] Given a model client instance, When constructed or configured, Then it routes parsing to the selected schema (OpenAI or native).
    - [ ] Given the user has no control over schema selection, When the application is built, Then the active schema is determined internally (test/development purpose only).

- **Requirement [LM-03]:** Image-to-Text (OCR) Derived Class
  - **Description:** A derived class that accepts an image (as file path or `QImage`), converts it to a base64 data URI, sends it to the model endpoint, and returns the extracted text.
  - **Acceptance Criteria:**
    - [ ] Given a file path to an image, When the request is sent, Then the file is read and encoded as a base64 data URI in the request payload.
    - [ ] Given a `QImage` object, When the request is sent, Then the image is encoded as a base64 data URI in the request payload.
    - [ ] Given an OCR request completes, When the stream ends, Then the full extracted text is available via the completion signal/callback.
    - [ ] Given the OCR model requires no conversational prompts, When constructing the request, Then only the image data is sent (no system/user/assistant prompts).

- **Requirement [LM-04]:** Text-to-Text Derived Class with Prompt Injection Protection
  - **Description:** A derived class that accepts system, user, and assistant prompts, formats them for the selected schema, and sanitizes prompts against prompt-injection attacks by escaping llama.cpp special tokens (`<|...|>`) with zero-width spaces.
  - **Acceptance Criteria:**
    - [ ] Given OpenAI schema is active, When formatting prompts, Then only system and user prompts are used (assistant prompt is discarded).
    - [ ] Given native schema is active, When formatting prompts, Then system, user, and assistant prompts are merged using `<|...|>` delimiters.
    - [ ] Given a prompt contains special tokens like `<|begin_of_text|>`, When sanitizing, Then the token is replaced with `<\u200B|begin_of_text|\u200B>` (zero-width spaces inserted).
    - [ ] Given sanitized prompts are processed, When the final response is returned, Then zero-width space escapes can be stripped from the output.
    - [ ] Given text extracted from OCR (untrusted source), When used as input to a prompt, Then any embedded special tokens are escaped before being sent to the model.

- **Requirement [LM-05]:** Dual Usage Pattern (GUI Signals + CLI Callbacks)
  - **Description:** The class hierarchy must be usable from both Qt GUI code (via signals/slots) and CLI code (via function callbacks or a polling mechanism), without requiring `QWidget` or any UI dependency in the backend.
  - **Acceptance Criteria:**
    - [ ] Given the base class is a `QObject`, When used from QML/QtQuick, Then signals can be connected to slots for live updates.
    - [ ] Given the base class supports callbacks, When used from CLI, Then a lambda or function pointer can receive chunked token updates for stdout printing.
    - [ ] Given the backend library has no UI dependencies, When building the CLI target, Then no QWidget or QtQuick headers are included in the backend.

- **Requirement [LM-06]:** Configuration Properties
  - **Description:** The endpoint URL (`http://IP:port`) and `temperature` are settable properties on the client classes, injected by the caller (not read from settings internally).
  - **Acceptance Criteria:**
    - [ ] Given an endpoint URL property, When set before a request, Then subsequent requests target the configured URL.
    - [ ] Given a temperature property, When set before a request, Then the value is included in the request payload.
    - [ ] Given the class has no settings dependency, When inspecting the class, Then no `SettingsManager` or similar dependency is referenced.

- **Requirement [LM-07]:** Request Cancellation
  - **Description:** An in-flight streaming request must be cancellable, aborting the HTTP connection and emitting a cancelled state.
  - **Acceptance Criteria:**
    - [ ] Given an active streaming request, When cancel is called, Then the HTTP connection is aborted and no further tokens are delivered.
    - [ ] Given a cancelled request, When the cancellation completes, Then a cancellation signal/callback is emitted (distinct from completion).

## 3. Scope & Constraints

- **In-Scope:**
  - Base class design for streamed HTTP responses with coalescing and statistics
  - Image-to-text derived class (OCR)
  - Text-to-text derived class with prompt injection protection
  - Dual schema parsing (OpenAI JSON lines + native llama.cpp)
  - QObject-based signals AND callback support for CLI compatibility
  - Configuration via properties (endpoint URL, temperature, coalescing interval)
  - Request cancellation

- **Out-of-Scope:**
  - Settings management integration (caller's responsibility)
  - CLI implementation for printing tokens (only design for CLI compatibility)
  - QML frontend integration code (only backend class design)
  - Model selection or model name configuration
  - Authentication/API keys for the endpoint
  - Connection pooling or HTTP keep-alive management beyond what `QNetworkAccessManager` provides natively

- **Technical Constraints / Edge Cases:**
  - Backend library must be QtCore-only (no QtGui/QWidget/QQuick) — image handling (`QImage`) may require careful consideration since `QImage` is in QtGui
  - The coalescing interval must be configurable; a default of ~100–500ms seems reasonable but needs confirmation
  - Prompt injection escaping must handle nested or overlapping token patterns
  - The base64 data URI format for images must match what the llama.cpp OpenAI-compatible API expects (`data:image/png;base64,...`)
  - Network errors (timeout, DNS failure, server error) must be reported via error signal/callback
  - Concurrent requests: can multiple instances run simultaneously, or is a single instance expected per operation?

## 4. Open Design Choices (Questions for User)

- **[Technical]:** The backend is specified as QtCore-only, but `QImage` lives in QtGui. For the image-to-text class, should we:
  - Accept raw binary image data (`QByteArray`) and a MIME type string instead of `QImage`, keeping the backend pure QtCore?
  - Or is it acceptable to add a QtGui dependency to the backend for `QImage` support?
  **User: we will have to relax the cli and allow to link against QtGui. The library will be installed anyway because of the frontend.**

- **[Technical]:** For the coalescing interval, what default value do you prefer? (e.g., 100ms for responsive feel, 500ms for fewer updates?) And should the signal emit the accumulated chunk text, or the full text-so-far each time?
**User: let's settle with 250ms default.**

- **[Technical]:** For CLI callback support, do you prefer:
  - A `std::function<void(const QString& chunk)>` callback property on the QObject base class?
  - Or a separate non-QObject interface/adapter pattern (e.g., a pure virtual listener interface)?
  **User: if we still can use qt signals and slot to implement a kind of async and wait pattern, let's consider that. If not then a std::function() as callback with STL thread synchronization and async/wait style can be used instead. Make it a request to propose the best pattern and options to be considered for evaluation, but it is not clear at this moment.**

- **[Business Logic]:** For the text-to-text class, do we need conversation history (multi-turn context), or is it always a single request with system + user + assistant prompts only?
**User: Not chat-style conversation is required. It is single-turn always.**

- **[Technical]:** Should the token statistics include only generation metrics (tokens/sec, total tokens), or also prompt-processing metrics (prompt tokens, prompt eval time) if the server provides them?
**User: let save generation and prompt procesing if available. whether we will use it in the cli or back it no in the scope of this task. So for future usage or pure testing lets include both.**

- **[Technical]:** For error handling, should network errors and server errors (HTTP 4xx/5xx) be distinguished, or is a single generic error signal/callback sufficient?
**User: Let's start with a `template class expected<T>` that encapsulate the results and error diagnostic in an error_frame struct. So we can easily log the errors and track them. we can have a class error_stack where we can agregate the error_frameso we can pin-point any error and present the user (at least for testing and debug, or an advance console log in the future) details errors log. Let's have this in an own `src/backend/core/expected.hpp` file.**

WRITE NEXT ITERATION BELOW THIS LINE. DON'T DELETE OR EDIT ANYTHING ABOVE.
-------------------------------------------------------------------------------

# Refinement Iteration 2

**Status:** PENDING USER FEEDBACK

## 1. Executive Summary

Incorporate user decisions from Iteration 1 into concrete requirements: QtGui dependency is now allowed, coalescing defaults to 250ms, single-turn only for text-to-text, both generation and prompt-processing metrics in token statistics, and a custom `expected<T>` error-handling framework. Additionally, new requirements are added for the CLI async/await pattern (to be evaluated), the error handling infrastructure, and the schema registry design. One open question remains on whether signals emit incremental chunks or full text-so-far.

## 2. Refined Requirements & Acceptance Criteria

- **Requirement [LM-01]:** Streamed Response Base Class
  - **Description:** An abstract `QObject` base class that manages HTTP streaming connections to a llama.cpp server, coalesces sub-second token responses into configurable chunks (default 250 ms), and collects both generation and prompt-processing token statistics.
  - **Acceptance Criteria:**
    - [ ] Given a valid endpoint URL, When the request is sent, Then the class establishes an HTTP streaming connection and yields partial results over time.
    - [ ] Given a coalescing interval (parametrizable in ms, default 250 ms), When multiple server responses arrive within the interval, Then they are accumulated and emitted as a single chunk after the interval elapses.
    - [ ] Given a completed response stream, When all tokens are received, Then token statistics including generation metrics (total tokens, tokens/sec, elapsed time) and prompt-processing metrics (prompt tokens, prompt eval time, if provided by server) are available.
    - [ ] Given a completion event, When the stream ends, Then a completion signal/callback is emitted with the full accumulated text and statistics.

- **Requirement [LM-02]:** Dual Schema Support (OpenAI & Native)
  - **Description:** The base class supports both `v1/chat/completions` (OpenAI-compatible JSON lines) and `/completion` (native llama.cpp) response schemas through a pluggable API schema registry. The active schema is selected internally and is not exposed to end users.
  - **Acceptance Criteria:**
    - [ ] Given a registered schema parser, When a streamed response arrives, Then the correct parser extracts tokens from the response format.
    - [ ] Given a model client instance, When constructed or configured, Then it routes parsing to the selected schema (OpenAI or native).
    - [ ] Given the user has no control over schema selection, When the application is built, Then the active schema is determined internally (test/development purpose only).

- **Requirement [LM-03]:** Image-to-Text (OCR) Derived Class
  - **Description:** A derived class that accepts an image (as file path or `QImage`), converts it to a base64 data URI, sends it to the model endpoint, and returns the extracted text. The backend may depend on QtGui for `QImage` support.
  - **Acceptance Criteria:**
    - [ ] Given a file path to an image, When the request is sent, Then the file is read and encoded as a base64 data URI in the request payload.
    - [ ] Given a `QImage` object, When the request is sent, Then the image is encoded as a base64 data URI in the request payload.
    - [ ] Given an OCR request completes, When the stream ends, Then the full extracted text is available via the completion signal/callback.
    - [ ] Given the OCR model requires no conversational prompts, When constructing the request, Then only the image data is sent (no system/user/assistant prompts).

- **Requirement [LM-04]:** Text-to-Text Derived Class with Prompt Injection Protection
  - **Description:** A derived class for single-turn text-to-text requests that accepts system, user, and assistant prompts, formats them for the selected schema, and sanitizes all prompt inputs against prompt-injection attacks by escaping llama.cpp special tokens (`<|...|>`) with zero-width spaces.
  - **Acceptance Criteria:**
    - [ ] Given OpenAI schema is active, When formatting prompts, Then only system and user prompts are used (assistant prompt is discarded).
    - [ ] Given native schema is active, When formatting prompts, Then system, user, and assistant prompts are merged using `<|...|>` delimiters.
    - [ ] Given a prompt contains special tokens like `<|begin_of_text|>`, When sanitizing, Then the token is replaced with `<\u200B|begin_of_text|\u200B>` (zero-width spaces inserted).
    - [ ] Given sanitized prompts are processed, When the final response is returned, Then zero-width space escapes can be stripped from the output.
    - [ ] Given text extracted from OCR (untrusted source), When used as input to a prompt, Then any embedded special tokens are escaped before being sent to the model.
    - [ ] Given the class is single-turn only, When a request completes, Then no conversation history is retained for subsequent requests.

- **Requirement [LM-05]:** Dual Usage Pattern (GUI Signals + CLI Async)
  - **Description:** The class hierarchy must be usable from both Qt GUI code (via QObject signals/slots) and CLI code. For CLI usage, the design must support an async/await-style pattern so that calling code can wait for completion or receive chunked updates without blocking the event loop. The final pattern to be used will be determined by evaluation of candidate approaches.
  - **Acceptance Criteria:**
    - [ ] Given the base class is a `QObject`, When used from QML/QtQuick, Then signals can be connected to slots for live updates.
    - [ ] Given the design supports CLI async usage, When used from CLI, Then the calling code can either (a) use a `QFuture`/`QPromise`-based async/await pattern with Qt signals, or (b) use a `std::function` callback with STL synchronization primitives — the chosen approach must allow waiting for completion and receiving chunked token updates.
    - [ ] Given the backend library links against QtCore and QtGui only, When building the CLI target, Then no QWidget or QtQuick headers are included in the backend.

- **Requirement [LM-06]:** Configuration Properties
  - **Description:** The endpoint URL (`http://IP:port`), `temperature`, and coalescing interval are settable properties on the client classes, injected by the caller (not read from settings internally).
  - **Acceptance Criteria:**
    - [ ] Given an endpoint URL property, When set before a request, Then subsequent requests target the configured URL.
    - [ ] Given a temperature property, When set before a request, Then the value is included in the request payload.
    - [ ] Given a coalescing interval property, When set before a request, Then the signal emission frequency uses the configured interval.
    - [ ] Given the class has no settings dependency, When inspecting the class, Then no `SettingsManager` or similar dependency is referenced.

- **Requirement [LM-07]:** Request Cancellation
  - **Description:** An in-flight streaming request must be cancellable, aborting the HTTP connection and emitting a cancelled state.
  - **Acceptance Criteria:**
    - [ ] Given an active streaming request, When cancel is called, Then the HTTP connection is aborted and no further tokens are delivered.
    - [ ] Given a cancelled request, When the cancellation completes, Then a cancellation signal/callback is emitted (distinct from completion).

- **Requirement [LM-08]:** Error Handling Framework (`expected<T>`, `error_frame`, `error_stack`)
  - **Description:** A custom error handling infrastructure in `src/backend/core/expected.hpp` providing: (1) an `error_frame` struct that encapsulates an error diagnostic with message, location/context, and timestamp; (2) a template class `expected<T>` that holds either a successful result of type `T` or an `error_frame`; (3) an `error_stack` class that aggregates multiple `error_frame` entries for detailed logging and debugging.
  - **Acceptance Criteria:**
    - [ ] Given an operation succeeds, When the result is wrapped in `expected<T>`, Then the caller can extract the value of type `T`.
    - [ ] Given an operation fails, When the result is wrapped in `expected<T>`, Then the caller can extract an `error_frame` with a descriptive message and context.
    - [ ] Given multiple errors occur, When added to an `error_stack`, Then each `error_frame` is preserved with its own identity for later inspection.
    - [ ] Given an `error_frame`, When logged or inspected, Then it contains sufficient detail (message, source location, timestamp) to pinpoint the origin of the error.
    - [ ] Given the error framework is in the backend core, When included by the language model client classes, Then all errors from HTTP operations (network failures, server errors, parsing errors) are reported as `expected<T>` return values or signal payloads.

- **Requirement [LM-09]:** CLI Async/Await Pattern Evaluation
  - **Description:** Before final implementation, a design evaluation must be performed to select the best async/await pattern for CLI usage. The candidates to evaluate are: (a) Qt's `QPromise`/`QFuture` with signal-based chunk delivery, (b) `std::function` callbacks with `std::condition_variable`/`std::mutex` for synchronization, and (c) a hybrid approach using QObject signals internally with a CLI adapter that wraps signals into a waitable interface. The evaluation must consider ease of use from CLI, ability to print tokens live to stdout, composability (chaining multiple requests), and minimal overhead.
  - **Acceptance Criteria:**
    - [ ] Given the design evaluation is complete, When documented, Then it compares at least two candidate patterns against the criteria: live token printing, wait-for-completion, composability, and code simplicity.
    - [ ] Given a pattern is selected, When the base class is implemented, Then CLI code can call a request method and synchronously or asynchronously wait for the result without running a separate QEventLoop manually if avoidable.

## 3. Scope & Constraints

- **In-Scope:**
  - Base class design for streamed HTTP responses with coalescing (250 ms default) and statistics
  - Image-to-text derived class (OCR) accepting `QImage` or file path
  - Text-to-text derived class with prompt injection protection (single-turn only)
  - Dual schema parsing (OpenAI JSON lines + native llama.cpp) via pluggable registry
  - QObject-based signals for GUI usage
  - CLI async/await pattern design and evaluation (implementation deferred to selected pattern)
  - Configuration via properties (endpoint URL, temperature, coalescing interval)
  - Request cancellation
  - Custom error handling framework (`expected<T>`, `error_frame`, `error_stack`) in `src/backend/core/expected.hpp`
  - Backend may depend on QtCore and QtGui (no QWidget/QQuick)

- **Out-of-Scope:**
  - Settings management integration (caller's responsibility)
  - CLI implementation for printing tokens (only design for CLI compatibility)
  - QML frontend integration code (only backend class design)
  - Model selection or model name configuration
  - Authentication/API keys for the endpoint
  - Connection pooling or HTTP keep-alive management beyond what `QNetworkAccessManager` provides natively
  - Multi-turn conversation history

- **Technical Constraints / Edge Cases:**
  - Backend library depends on QtCore and QtGui (no QWidget/QQuick)
  - The coalescing interval defaults to 250 ms and is configurable at runtime
  - Prompt injection escaping must handle nested or overlapping token patterns
  - The base64 data URI format for images must match what the llama.cpp OpenAI-compatible API expects (`data:image/png;base64,...`)
  - Network errors (timeout, DNS failure, server error) must be wrapped in `expected<T>` and reported via error signal/callback
  - Concurrent requests: each instance of a model client handles one request at a time; multiple concurrent operations require multiple instances

## 4. Open Design Choices (Questions for User)

- **[Technical]:** When the coalescing timer fires, should the signal emit only the **incremental chunk** accumulated since the last emission, or the **full text-so-far**? Incremental chunks are lighter but require the caller to concatenate; full text-so-far is heavier per emission but simpler for the caller (e.g., just replace the label text).

**User: we prefer incremental, this way we can either pdirectly print out to stdou or simple append to current document.**

- **[Technical]:** For the CLI async/await pattern evaluation (LM-09), do you have a preference for which candidates to prioritize? Specifically:
  - Qt's `QPromise`/`QFuture` (Qt 6.3+, integrates naturally with QObject signals)
  - Raw STL (`std::function` + `std::condition_variable`) (no Qt event loop dependency in CLI wait path)
  - Or should the evaluation be unbiased and cover all three equally?
**User: First Qt base system, then C++ STL as second choice.**

- **[Technical]:** For the schema registry, should the active schema (OpenAI vs native) be selected at **compile time** (e.g., CMake define / template parameter) or at **runtime** (e.g., static method call during initialization)? Compile-time is simpler and has zero overhead; runtime allows switching without rebuild during testing.

**User: will be selected at runtime, so we can test the performance and easy of use without have to compile again.**

# Refinement Iteration 3

**Status:** PENDING USER FEEDBACK

## 1. Executive Summary

Finalize all open design decisions from Iteration 2: incremental chunk emission for coalesced signals, Qt-first CLI async pattern (QPromise/QFuture with STL fallback), and runtime schema selection via static initialization. This iteration consolidates every requirement into its final form and surfaces the last few edge-case questions before locking the specification.

## 2. Refined Requirements & Acceptance Criteria

- **Requirement [LM-01]:** Streamed Response Base Class
  - **Description:** An abstract `QObject` base class that manages HTTP streaming connections to a llama.cpp server, coalesces sub-second token responses into configurable chunks (default 250 ms), and collects both generation and prompt-processing token statistics. Signals emit incremental chunks (not full text-so-far).
  - **Acceptance Criteria:**
    - [ ] Given a valid endpoint URL, When the request is sent, Then the class establishes an HTTP streaming connection and yields partial results over time.
    - [ ] Given a coalescing interval (parametrizable in ms, default 250 ms), When multiple server responses arrive within the interval, Then they are accumulated and emitted as a single incremental chunk after the interval elapses.
    - [ ] Given a completed response stream, When all tokens are received, Then token statistics including generation metrics (total tokens, tokens/sec, elapsed time) and prompt-processing metrics (prompt tokens, prompt eval time, if provided by server) are available.
    - [ ] Given a completion event, When the stream ends, Then a completion signal/callback is emitted with the full accumulated text and statistics.

- **Requirement [LM-02]:** Dual Schema Support (OpenAI & Native) — Runtime Selection
  - **Description:** The base class supports both `v1/chat/completions` (OpenAI-compatible JSON lines) and `/completion` (native llama.cpp) response schemas through a pluggable API schema registry. The active schema is selected at runtime via a static initialization call, allowing switching without recompilation.
  - **Acceptance Criteria:**
    - [ ] Given a registered schema parser, When a streamed response arrives, Then the correct parser extracts tokens from the response format.
    - [ ] Given a model client instance, When constructed or configured, Then it routes parsing to the currently active schema selected at runtime.
    - [ ] Given the user has no control over schema selection, When the application is built, Then the active schema is determined internally via a static init call (test/development purpose only).
    - [ ] Given the schema is changed at runtime, When a new request is initiated, Then the new schema is used for that request and subsequent requests.

- **Requirement [LM-03]:** Image-to-Text (OCR) Derived Class
  - **Description:** A derived class that accepts an image (as file path or `QImage`), converts it to a base64 data URI, sends it to the model endpoint, and returns the extracted text. The backend depends on QtGui for `QImage` support.
  - **Acceptance Criteria:**
    - [ ] Given a file path to an image, When the request is sent, Then the file is read and encoded as a base64 data URI in the request payload.
    - [ ] Given a `QImage` object, When the request is sent, Then the image is encoded as a base64 data URI in the request payload.
    - [ ] Given an OCR request completes, When the stream ends, Then the full extracted text is available via the completion signal/callback.
    - [ ] Given the OCR model requires no conversational prompts, When constructing the request, Then only the image data is sent (no system/user/assistant prompts).

- **Requirement [LM-04]:** Text-to-Text Derived Class with Prompt Injection Protection
  - **Description:** A derived class for single-turn text-to-text requests that accepts system, user, and assistant prompts, formats them for the selected schema, and sanitizes all prompt inputs against prompt-injection attacks by escaping llama.cpp special tokens (`<|...|>`) with zero-width spaces.
  - **Acceptance Criteria:**
    - [ ] Given OpenAI schema is active, When formatting prompts, Then only system and user prompts are used (assistant prompt is discarded).
    - [ ] Given native schema is active, When formatting prompts, Then system, user, and assistant prompts are merged using `<|...|>` delimiters.
    - [ ] Given a prompt contains special tokens like `<|begin_of_text|>`, When sanitizing, Then the token is replaced with `<\u200B|begin_of_text|\u200B>` (zero-width spaces inserted).
    - [ ] Given sanitized prompts are processed, When the final response is returned, Then zero-width space escapes can be stripped from the output.
    - [ ] Given text extracted from OCR (untrusted source), When used as input to a prompt, Then any embedded special tokens are escaped before being sent to the model.
    - [ ] Given the class is single-turn only, When a request completes, Then no conversation history is retained for subsequent requests.

- **Requirement [LM-05]:** Dual Usage Pattern (GUI Signals + CLI Async via QPromise)
  - **Description:** The class hierarchy must be usable from both Qt GUI code (via QObject signals/slots) and CLI code. For CLI usage, the primary pattern is Qt's `QPromise`/`QFuture` with signal-based chunk delivery; a raw STL fallback (`std::function` + `std::condition_variable`) is available as a secondary option if QPromise proves insufficient.
  - **Acceptance Criteria:**
    - [ ] Given the base class is a `QObject`, When used from QML/QtQuick, Then signals can be connected to slots for live incremental chunk updates.
    - [ ] Given the design uses QPromise/QFuture as the primary CLI pattern, When used from CLI, Then the calling code can await a QFuture for completion while receiving incremental chunks via signals or a callback.
    - [ ] Given QPromise is insufficient for a specific use case, When the STL fallback is needed, Then a `std::function` callback with `std::condition_variable` synchronization is available as an alternative.
    - [ ] Given the backend library links against QtCore and QtGui only, When building the CLI target, Then no QWidget or QtQuick headers are included in the backend.

- **Requirement [LM-06]:** Configuration Properties
  - **Description:** The endpoint URL (`http://IP:port`), `temperature`, and coalescing interval are settable properties on the client classes, injected by the caller (not read from settings internally).
  - **Acceptance Criteria:**
    - [ ] Given an endpoint URL property, When set before a request, Then subsequent requests target the configured URL.
    - [ ] Given a temperature property, When set before a request, Then the value is included in the request payload.
    - [ ] Given a coalescing interval property, When set before a request, Then the signal emission frequency uses the configured interval.
    - [ ] Given the class has no settings dependency, When inspecting the class, Then no `SettingsManager` or similar dependency is referenced.

- **Requirement [LM-07]:** Request Cancellation
  - **Description:** An in-flight streaming request must be cancellable, aborting the HTTP connection and emitting a cancelled state.
  - **Acceptance Criteria:**
    - [ ] Given an active streaming request, When cancel is called, Then the HTTP connection is aborted and no further tokens are delivered.
    - [ ] Given a cancelled request, When the cancellation completes, Then a cancellation signal/callback is emitted (distinct from completion).

- **Requirement [LM-08]:** Error Handling Framework (`expected<T>`, `error_frame`, `error_stack`)
  - **Description:** A custom error handling infrastructure in `src/backend/core/expected.hpp` providing: (1) an `error_frame` struct that encapsulates an error diagnostic with message, location/context, and timestamp; (2) a template class `expected<T>` that holds either a successful result of type `T` or an `error_frame`; (3) an `error_stack` class that aggregates multiple `error_frame` entries for detailed logging and debugging.
  - **Acceptance Criteria:**
    - [ ] Given an operation succeeds, When the result is wrapped in `expected<T>`, Then the caller can extract the value of type `T`.
    - [ ] Given an operation fails, When the result is wrapped in `expected<T>`, Then the caller can extract an `error_frame` with a descriptive message and context.
    - [ ] Given multiple errors occur, When added to an `error_stack`, Then each `error_frame` is preserved with its own identity for later inspection.
    - [ ] Given an `error_frame`, When logged or inspected, Then it contains sufficient detail (message, source location, timestamp) to pinpoint the origin of the error.
    - [ ] Given the error framework is in the backend core, When included by the language model client classes, Then all errors from HTTP operations (network failures, server errors, parsing errors) are reported as `expected<T>` return values or signal payloads.

- **Requirement [LM-09]:** CLI Async/Await Pattern Evaluation
  - **Description:** Before final implementation, a design evaluation must be performed to select the best async/await pattern for CLI usage. The primary candidate is Qt's `QPromise`/`QFuture` with signal-based chunk delivery; the secondary candidate is raw STL (`std::function` + `std::condition_variable`). The evaluation prioritizes the Qt-based approach first and falls back to STL only if QPromise proves insufficient.
  - **Acceptance Criteria:**
    - [ ] Given the design evaluation is complete, When documented, Then it compares the Qt QPromise/QFuture pattern against the STL callback pattern on: live token printing, wait-for-completion, composability, and code simplicity.
    - [ ] Given a pattern is selected, When the base class is implemented, Then CLI code can call a request method and synchronously or asynchronously wait for the result without running a separate QEventLoop manually if avoidable.

## 3. Scope & Constraints

- **In-Scope:**
  - Base class design for streamed HTTP responses with coalescing (250 ms default) and incremental chunk emission
  - Image-to-text derived class (OCR) accepting `QImage` or file path
  - Text-to-text derived class with prompt injection protection (single-turn only)
  - Dual schema parsing (OpenAI JSON lines + native llama.cpp) via pluggable registry with runtime selection
  - QObject-based signals for GUI usage (incremental chunks)
  - CLI async/await pattern: QPromise/QFuture primary, STL fallback secondary
  - Configuration via properties (endpoint URL, temperature, coalescing interval)
  - Request cancellation
  - Custom error handling framework (`expected<T>`, `error_frame`, `error_stack`) in `src/backend/core/expected.hpp`
  - Backend depends on QtCore and QtGui (no QWidget/QQuick)

- **Out-of-Scope:**
  - Settings management integration (caller's responsibility)
  - CLI implementation for printing tokens (only design for CLI compatibility)
  - QML frontend integration code (only backend class design)
  - Model selection or model name configuration
  - Authentication/API keys for the endpoint
  - Connection pooling or HTTP keep-alive management beyond what `QNetworkAccessManager` provides natively
  - Multi-turn conversation history

- **Technical Constraints / Edge Cases:**
  - Backend library depends on QtCore and QtGui (no QWidget/QQuick)
  - The coalescing interval defaults to 250 ms and is configurable at runtime
  - Signals emit incremental chunks only (caller appends or prints as needed)
  - Prompt injection escaping must handle nested or overlapping token patterns
  - The base64 data URI format for images must match what the llama.cpp OpenAI-compatible API expects (`data:image/png;base64,...`)
  - Network errors (timeout, DNS failure, server error) must be wrapped in `expected<T>` and reported via error signal/callback
  - Concurrent requests: each instance of a model client handles one request at a time; multiple concurrent operations require multiple instances
  - Schema selection is runtime-configurable via static initialization call

## 4. Open Design Choices (Questions for User)

- **[Technical]:** What should happen when a new request is started on an instance that already has an in-flight request? Should the class:
  - **Reject** the new request with an error (caller must cancel first)?
  - **Auto-cancel** the previous request and start the new one?
  - **Queue** the new request for after the current one completes?
  **User: for now we will make sure to make one request per given instance of a model client. Make a new request to an instance wit a running job should trigger a debug assert or exception. If we need concurrent request to the server we will spawn multiple clients.**

- **[Technical]:** The frontend uses QtQuick/QML. Should the language model client classes be registered as QML types (via `QML_ELEMENT` macro or `qmlRegisterType`) so they can be instantiated directly from QML? Or will they always be managed from C++ and exposed to QML via context properties?
**User: The backend must be QML free. We will have QtGui only because we need to read images, otherwise it would be strictly QtCore. so absolutely no QML code here. Later in the frontend we will make model/views to consume the backend when required.**

- **[Technical]:** For the `error_frame` struct, should it include a stack trace (e.g., via `backtrace()` on Linux or StackWalk on Windows), or is a simple source file + line number sufficient? Stack traces add diagnostic power but increase complexity and platform-specific code.
**User: Not backtrack. The error_frame will have only a error code, error description and std::source_location defaulted to std::source_location::current as last default argument at the error frame creation. We dot not need stack trace.**

# Refinement Iteration 4 — Final Specification

**Status:** LOCKED

## 1. Executive Summary

Final locked specification for the language model client class hierarchy in the backend library. All design decisions have been resolved: one-request-per-instance with debug assert on conflict, QtCore+QtGui-only backend (no QML), incremental chunk signals, runtime schema selection, QPromise-first CLI async pattern, and `expected<T>` error framework with `std::source_location`. This document is ready for implementation planning.

## 2. Refined Requirements & Acceptance Criteria

- **Requirement [LM-01]:** Streamed Response Base Class
  - **Description:** An abstract `QObject` base class that manages HTTP streaming connections to a llama.cpp server, coalesces sub-second token responses into configurable chunks (default 250 ms), and collects both generation and prompt-processing token statistics. Signals emit incremental chunks only. Each instance handles exactly one request at a time; attempting a second request triggers a debug assert or exception.
  - **Acceptance Criteria:**
    - [ ] Given a valid endpoint URL, When the request is sent, Then the class establishes an HTTP streaming connection and yields partial results over time.
    - [ ] Given a coalescing interval (parametrizable in ms, default 250 ms), When multiple server responses arrive within the interval, Then they are accumulated and emitted as a single incremental chunk after the interval elapses.
    - [ ] Given a completed response stream, When all tokens are received, Then token statistics including generation metrics (total tokens, tokens/sec, elapsed time) and prompt-processing metrics (prompt tokens, prompt eval time, if provided by server) are available.
    - [ ] Given a completion event, When the stream ends, Then a completion signal/callback is emitted with the full accumulated text and statistics.
    - [ ] Given an instance with an active in-flight request, When a new request is started on the same instance, Then a debug assert fires (in debug builds) or a std::exception is thrown (in release builds).

- **Requirement [LM-02]:** Dual Schema Support (OpenAI & Native) — Runtime Selection
  - **Description:** The base class supports both `v1/chat/completions` (OpenAI-compatible JSON lines) and `/completion` (native llama.cpp) response schemas through a pluggable API schema registry. The active schema is selected at runtime via a static initialization call, allowing switching without recompilation.
  - **Acceptance Criteria:**
    - [ ] Given a registered schema parser, When a streamed response arrives, Then the correct parser extracts tokens from the response format.
    - [ ] Given a model client instance, When constructed or configured, Then it routes parsing to the currently active schema selected at runtime.
    - [ ] Given the user has no control over schema selection, When the application is built, Then the active schema is determined internally via a static init call (test/development purpose only).
    - [ ] Given the schema is changed at runtime, When a new request is initiated, Then the new schema is used for that request and subsequent requests.

- **Requirement [LM-03]:** Image-to-Text (OCR) Derived Class
  - **Description:** A derived class that accepts an image (as file path or `QImage`), converts it to a base64 data URI, sends it to the model endpoint, and returns the extracted text. The backend depends on QtGui for `QImage` support.
  - **Acceptance Criteria:**
    - [ ] Given a file path to an image, When the request is sent, Then the file is read and encoded as a base64 data URI in the request payload.
    - [ ] Given a `QImage` object, When the request is sent, Then the image is encoded as a base64 data URI in the request payload.
    - [ ] Given an OCR request completes, When the stream ends, Then the full extracted text is available via the completion signal/callback.
    - [ ] Given the OCR model requires no conversational prompts, When constructing the request, Then only the image data is sent (no system/user/assistant prompts).

- **Requirement [LM-04]:** Text-to-Text Derived Class with Prompt Injection Protection
  - **Description:** A derived class for single-turn text-to-text requests that accepts system, user, and assistant prompts, formats them for the selected schema, and sanitizes all prompt inputs against prompt-injection attacks by escaping llama.cpp special tokens (`<|...|>`) with zero-width spaces.
  - **Acceptance Criteria:**
    - [ ] Given OpenAI schema is active, When formatting prompts, Then only system and user prompts are used (assistant prompt is discarded).
    - [ ] Given native schema is active, When formatting prompts, Then system, user, and assistant prompts are merged using `<|...|>` delimiters.
    - [ ] Given a prompt contains special tokens like `<|begin_of_text|>`, When sanitizing, Then the token is replaced with `<\u200B|begin_of_text|\u200B>` (zero-width spaces inserted).
    - [ ] Given sanitized prompts are processed, When the final response is returned, Then zero-width space escapes can be stripped from the output.
    - [ ] Given text extracted from OCR (untrusted source), When used as input to a prompt, Then any embedded special tokens are escaped before being sent to the model.
    - [ ] Given the class is single-turn only, When a request completes, Then no conversation history is retained for subsequent requests.

- **Requirement [LM-05]:** Dual Usage Pattern (GUI Signals + CLI Async via QPromise)
  - **Description:** The class hierarchy must be usable from both Qt GUI code (via QObject signals/slots) and CLI code. For CLI usage, the primary pattern is Qt's `QPromise`/`QFuture` with signal-based chunk delivery; a raw STL fallback (`std::function` + `std::condition_variable`) is available as a secondary option if QPromise proves insufficient. The backend contains no QML code — it is strictly QtCore and QtGui only.
  - **Acceptance Criteria:**
    - [ ] Given the base class is a `QObject`, When used from QML/QtQuick via a C++ model/view layer, Then signals can be connected to slots for live incremental chunk updates.
    - [ ] Given the design uses QPromise/QFuture as the primary CLI pattern, When used from CLI, Then the calling code can await a QFuture for completion while receiving incremental chunks via signals or a callback.
    - [ ] Given QPromise is insufficient for a specific use case, When the STL fallback is needed, Then a `std::function` callback with `std::condition_variable` synchronization is available as an alternative.
    - [ ] Given the backend library links against QtCore and QtGui only, When building any target (CLI or frontend), Then no QWidget, QtQuick, or QML headers are included in the backend.

- **Requirement [LM-06]:** Configuration Properties
  - **Description:** The endpoint URL (`http://IP:port`), `temperature`, and coalescing interval are settable properties on the client classes, injected by the caller (not read from settings internally).
  - **Acceptance Criteria:**
    - [ ] Given an endpoint URL property, When set before a request, Then subsequent requests target the configured URL.
    - [ ] Given a temperature property, When set before a request, Then the value is included in the request payload.
    - [ ] Given a coalescing interval property, When set before a request, Then the signal emission frequency uses the configured interval.
    - [ ] Given the class has no settings dependency, When inspecting the class, Then no `SettingsManager` or similar dependency is referenced.

- **Requirement [LM-07]:** Request Cancellation
  - **Description:** An in-flight streaming request must be cancellable, aborting the HTTP connection and emitting a cancelled state.
  - **Acceptance Criteria:**
    - [ ] Given an active streaming request, When cancel is called, Then the HTTP connection is aborted and no further tokens are delivered.
    - [ ] Given a cancelled request, When the cancellation completes, Then a cancellation signal/callback is emitted (distinct from completion).

- **Requirement [LM-08]:** Error Handling Framework (`expected<T>`, `error_frame`, `error_stack`)
  - **Description:** A custom error handling infrastructure in `src/backend/core/expected.hpp` providing: (1) an `error_frame` struct with an error code, error description, and `std::source_location` (defaulted to `std::source_location::current()` at creation time); (2) a template class `expected<T>` that holds either a successful result of type `T` or an `error_frame`; (3) an `error_stack` class that aggregates multiple `error_frame` entries for detailed logging and debugging. No stack traces are captured.
  - **Acceptance Criteria:**
    - [ ] Given an operation succeeds, When the result is wrapped in `expected<T>`, Then the caller can extract the value of type `T`.
    - [ ] Given an operation fails, When the result is wrapped in `expected<T>`, Then the caller can extract an `error_frame` with an error code, descriptive message, and source location.
    - [ ] Given multiple errors occur, When added to an `error_stack`, Then each `error_frame` is preserved with its own identity for later inspection.
    - [ ] Given an `error_frame`, When logged or inspected, Then it contains an error code, description, and source file + line number from `std::source_location`.
    - [ ] Given the error framework is in the backend core, When included by the language model client classes, Then all errors from HTTP operations (network failures, server errors, parsing errors) are reported as `expected<T>` return values or signal payloads.

- **Requirement [LM-09]:** CLI Async/Await Pattern Evaluation
  - **Description:** Before final implementation, a design evaluation must be performed to select the best async/await pattern for CLI usage. The primary candidate is Qt's `QPromise`/`QFuture` with signal-based chunk delivery; the secondary candidate is raw STL (`std::function` + `std::condition_variable`). The evaluation prioritizes the Qt-based approach first and falls back to STL only if QPromise proves insufficient.
  - **Acceptance Criteria:**
    - [ ] Given the design evaluation is complete, When documented, Then it compares the Qt QPromise/QFuture pattern against the STL callback pattern on: live token printing, wait-for-completion, composability, and code simplicity.
    - [ ] Given a pattern is selected, When the base class is implemented, Then CLI code can call a request method and synchronously or asynchronously wait for the result without running a separate QEventLoop manually if avoidable.

## 3. Scope & Constraints

- **In-Scope:**
  - Base class design for streamed HTTP responses with coalescing (250 ms default) and incremental chunk emission
  - Image-to-text derived class (OCR) accepting `QImage` or file path
  - Text-to-text derived class with prompt injection protection (single-turn only)
  - Dual schema parsing (OpenAI JSON lines + native llama.cpp) via pluggable registry with runtime selection
  - QObject-based signals for GUI usage (incremental chunks)
  - CLI async/await pattern: QPromise/QFuture primary, STL fallback secondary
  - Configuration via properties (endpoint URL, temperature, coalescing interval)
  - Request cancellation
  - Custom error handling framework (`expected<T>`, `error_frame`, `error_stack`) in `src/backend/core/expected.hpp`
  - Backend depends on QtCore and QtGui only (no QWidget, no QtQuick, no QML)

- **Out-of-Scope:**
  - Settings management integration (caller's responsibility)
  - CLI implementation for printing tokens (only design for CLI compatibility)
  - QML frontend integration code or model/view layer (frontend responsibility)
  - Model selection or model name configuration
  - Authentication/API keys for the endpoint
  - Connection pooling or HTTP keep-alive management beyond what `QNetworkAccessManager` provides natively
  - Multi-turn conversation history
  - Stack traces in error reporting

- **Technical Constraints / Edge Cases:**
  - Backend library depends on QtCore and QtGui only (no QWidget, no QtQuick, no QML)
  - The coalescing interval defaults to 250 ms and is configurable at runtime
  - Signals emit incremental chunks only (caller appends or prints as needed)
  - Prompt injection escaping must handle nested or overlapping token patterns
  - The base64 data URI format for images must match what the llama.cpp OpenAI-compatible API expects (`data:image/png;base64,...`)
  - Network errors (timeout, DNS failure, server error) must be wrapped in `expected<T>` and reported via error signal/callback
  - Each instance handles one request at a time; concurrent operations require multiple instances
  - Starting a second request on a busy instance triggers a debug assert (debug) or std::exception (release)
  - Schema selection is runtime-configurable via static initialization call

## 4. Open Design Choices

No open design choices remain. All decisions have been resolved across four refinement iterations.

---

**LOCKED**