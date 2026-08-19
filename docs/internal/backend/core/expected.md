# expected / error_frame / error_stack

## Source Files

- `src/backend/core/expected.hpp` — Header: `error_frame` struct, `expected<T>` template, `error_stack` class declaration.
- `src/backend/core/expected.cpp` — Implementation of the `error_stack` member functions.

**Counterpart status:** Both files present and analyzed as a pair.

---

## Purpose and Role

This pair provides the project's lightweight result/error type for the backend. It is the shared error-reporting primitive used by:

- `pdf_renderer` (`src/backend/core/pdf_renderer.cpp`) — returns `expected<int>` from `open()` and `expected<QImage>` from `render_page()`, with module-local negative error codes (`-1` open failed, `-2` no document, `-3` page out of range, `-4` render failed).
- The CLI (`src/cli/cli_common.hpp`) — defines aliases `expected_string = expected<QString>`, `expected_double = expected<double>`, and `expected_markdown_result = expected<markdown_write_result>` for settings/endpoint/model resolution.
- `model.cpp` — uses the `error_frame` struct directly (not wrapped in `expected<>`) as the payload of the `error_occurred` signal.

The design is intentionally minimal: a single-variant value-or-error holder with an error frame that carries an integer code, a `QString` description, and a `std::source_location` captured at construction time. It is **not** a full `std::expected` replacement (no monotype error type, no `std::unexpect_t`, no comparison operators).

---

## Major Structures

### `error_frame`

| Field | Type | Description |
|---|---|---|
| `error_code` | `int` | Opaque integer code. No enum; subsystems choose their own values (negative ad-hoc codes in `pdf_renderer`, Qt `QNetworkReply::NetworkError` cast to `int` in `model.cpp`, `-1` for generic CLI failures). |
| `description` | `QString` | Human-readable message. |
| `location` | `std::source_location` | Captured via `std::source_location::current()` default argument, so it records the call site that constructed the frame (or called `make_expected_error`). |

The constructor is `error_frame(int code, const QString &desc, const std::source_location &loc = std::source_location::current())`. Copying a frame preserves the *original* construction site — location is a property of where the error was created, not where it is observed.

### `expected<T>` (template)

A value-or-error holder with three member fields:

| Member | Role |
|---|---|
| `m_has_value` | `bool` state flag. `true` = success, `false` = error. |
| `m_value` | `T`, value-initialized (`T m_value {}`). Always present in memory, even in the error state. |
| `m_error` | `error_frame`. In the success state it holds the sentinel `(0, "")`. |

State model:

- **Success state** — constructed via `explicit expected(T value)` or `make_expected(value)`. `m_has_value == true`; `m_error` is the `(0, "")` sentinel.
- **Error state** — constructed via `expected(int code, const QString &desc, loc)` or `make_expected_error(code, desc)`. `m_has_value == false`.
- **Default state** — `expected()` produces an *error* state with code `0` and an empty description (see Static Analysis finding 1).

### `error_stack`

A simple ordered collection of `error_frame`s (declared in the header, implemented in `expected.cpp`):

- `push(const error_frame &)` — appends a frame.
- `frames()` — const reference to the internal `std::vector<error_frame>`.
- `clear()` — removes all frames.
- `to_string()` — renders each frame as `Error <code>: <description>` plus an indented `  at <file>:<line>` line when `location.file_name()` is non-null; returns `""` for an empty stack.

Note: `error_stack` is currently referenced only by the unit tests (`test/backend/core/expected_test.cpp`); no production code in `src/` uses it (see Static Analysis finding 8).

---

## Public API and Usage Patterns

### `expected<T>` members

| Member | Behavior |
|---|---|
| `has_value()` | Returns the state flag. The canonical first check before accessing a value. |
| `value()` | Returns `m_value` by value; **throws `std::runtime_error`** (message = error description only) if in the error state. Non-const. |
| `value_or(T default_value)` | Returns the stored value or the supplied default, by value. Default is taken by value (copied even on success). Non-const. |
| `error()` | Const reference to the `error_frame`. In the success state this is the `(0, "")` sentinel — callers must check `has_value()` first. |
| `make_expected(value)` | Static factory for the success state; makes intent explicit at call sites. |
| `make_expected_error(code, desc)` | Static factory for the error state; `source_location` defaults to the caller's line. |

Copy constructor/assignment are defaulted (exception specification computed). Move constructor/assignment are declared `noexcept = default` (see Static Analysis finding 2).

### Observed usage pattern (CLI)

The CLI consistently uses the check-then-access idiom, never relying on `value()` to throw:

```cpp
if (!result.has_value()) { /* report result.error() and bail */ }
const auto v = result.value();
```

`pdf_renderer` constructs results directly with the constructors (`expected<int>(code, desc)` / `expected<int>(value)`), while the CLI factories are used where intent clarity matters. Both styles are supported by design.

---

## Ownership, Lifetime, and Exception Safety

- **Value ownership:** `expected<T>` owns its `T` by value. For `T = QImage` (used by `pdf_renderer::render_page`) this means a full image copy on copy-construction; move-construction transfers it.
- **No raw pointers, no parent/child relationships** — the type is a plain value type with no Qt object integration.
- **Exception safety:** constructors perform only member initialization; there is no strong-guarantee-relevant allocation sequence beyond `T`'s and `QString`'s own guarantees. The one deliberate exception path is `value()` throwing `std::runtime_error` in the error state.
- **Thread safety:** none is documented or provided. `error_stack` mutates its vector without synchronization; `expected<T>` is a value type and is safe to use across threads only under the usual "no concurrent mutation" rule. No guarantee should be assumed.
- **Instantiation constraint:** because of `T m_value {}`, `expected<T>` is only instantiable for default-constructible `T`. All current instantiations (`int`, `double`, `QString`, `QImage`, `markdown_write_result`) satisfy this.

---

## Contextual Dependencies (assumptions, not part of the pair)

- Error-code conventions come from the *callers*: `pdf_renderer.cpp` defines module-local `inline const int` codes; `model.cpp` reuses `-1` for several distinct failures and casts Qt network error enums to `int`. The pair itself imposes no code namespace or registry.
- `error_frame` doubles as a Qt signal payload (`model_client_base::error_occurred(const error_frame &)` in `model.hpp`), so its ABI stability matters to the QObject boundary even though it is a plain struct.
- The CLI's `markdown_write_result` (in `cli_common.hpp`) is an aggregate carried inside `expected<>`; its `written == false` case is a *successful* result (skip notice), not an error — a semantic distinction that lives entirely in the CLI layer.

---

## Static Analysis and Security

### Finding 1 — Default constructor silently produces an error state with an empty message (material)

- **Evidence:** `expected() : m_has_value(false), m_error(0, "") {}` — a default-constructed object is in the error state with code `0` and an empty description. There is no distinct "uninitialized" state, and no other constructor produces this exact combination.
- **Risk:** Any function returning `expected<T>` that falls off the end without an explicit return (or returns `{}`) yields a silent error whose `value()` throws `std::runtime_error("")` — an exception with an empty message — and whose `error().description` is empty. The failure is indistinguishable from an intentional `make_expected_error(0, "")`.
- **Impact:** Hard-to-diagnose runtime failures; empty exception messages defeat logging and crash reporting; the code-0/empty-description sentinel is overloaded as both "success-state placeholder" and "default-constructed error".
- **Mitigation:** Either delete the default constructor (forcing explicit state construction) or make the default state explicitly documented and give it a non-zero sentinel code with a description such as `"default-constructed expected<T>"`. Alternatively, assert in `value()` that the description is non-empty when throwing.
- **Follow-up test recommendation:** A test that default-constructs `expected<int>`, asserts `!has_value()`, and documents (or, after mitigation, rejects) the resulting code/description; plus a compile-time or static-analysis check that no backend function returning `expected<T>` has a control path without an explicit return.
**User: Actionable, delete the default constructor. If the intention is to make a default initialized `expected<T>`, then it must be explicit with the constructor `expected<T>({})`. In addition, the use of the static `make(...)` methods does not add any improve semantic in its current form, it stil creates a temporaty value that is then move to the stored, it does not create an inplace object. So we either remove it or we create a template `make<>` to mimic for example the distintion between `std::any()` and `std::make_any<T>(...)`.**

### Finding 2 — `noexcept = default` move operations can terminate for non-noexcept-movable `T` (latent)

- **Evidence:** `expected(expected &&other) noexcept = default;` and `auto operator=(expected &&other) noexcept -> expected & = default;`. An explicit `noexcept` on a defaulted special member function overrides the computed exception specification: if `T`'s move constructor/assignment throws, the move of `expected<T>` calls `std::terminate` instead of propagating.
- **Risk:** Any future instantiation with a type whose move operations can throw (e.g., a custom result struct wrapping a throwing-movable member) turns a recoverable exception into process termination during a move — including moves performed inside exception unwinding, which is itself undefined behavior territory.
- **Impact:** Correctness/stability hazard that is invisible until the first such `T` is introduced; current instantiations (`int`, `double`, `QString`, `QImage`, `markdown_write_result`) all have noexcept moves, so this is latent, not active.
- **Mitigation:** Drop the explicit `noexcept` (let the specification be computed from `T`), or document an explicit requirement that `T` must have noexcept move operations and enforce it with a static assertion (`std::is_nothrow_move_constructible_v<T>`).
- **Follow-up test recommendation:** A static-assertion-based compile test (or a trait check in the unit test) verifying the intended guarantee; if the computed-specification route is chosen, a runtime test moving an `expected<T>` of a throwing-movable `T` inside a `try` block to confirm propagation instead of termination.
**User: add a static assert.**

### Finding 3 — Single-argument constructor overloads value vs. error-code intent for arithmetic `T` (misuse hazard)

- **Evidence:** For `T = int`, `expected<int>(5)` constructs a *success* state holding the value `5`, while `expected<int>(-1, "desc")` constructs an *error* state. Disambiguation is purely by argument count. `pdf_renderer::open()` relies on exactly this: `expected<int>(m_document->pageCount())` (success) vs. `expected<int>(err_open_failed, ...)` (error).
- **Risk:** A caller who mentally treats the single argument as an error code (e.g., writing `return expected<int>(-1);` to signal failure) silently produces a *successful* result with value `-1`. For `open()`, a negative "page count" would then flow to callers as a valid page count.
- **Impact:** Logic errors that compile cleanly and invert success/failure semantics; especially dangerous for `expected<int>` where the value domain (page counts) and error-code domain (negative codes) overlap in type but not in meaning.
- **Mitigation:** Prefer the `make_expected` / `make_expected_error` factories at all call sites (they make intent explicit and are the pattern the CLI already uses); document the argument-count rule in the header; optionally add a debug assertion in the success constructor that `T` values from known error-code-producing call sites are non-negative — or, more robustly, give `pdf_renderer::open()` a dedicated result type so page count and error code cannot share one constructor.
- **Follow-up test recommendation:** A unit test asserting `expected<int>(-1).has_value() == true` to pin down the current (surprising) semantics, plus a review gate that new `expected<arithmetic>` call sites use the factories.
**User: Actionable, this finding goes along with finding 1. To make the class more robust, lets make the constructors private and implement two static methods `make_value` and `make_error`. For `make_value` we must support make_value({}), make_value(), make_value(temporal), make_value(copy), make_value(std::move(...)), to cover the several ways a value can be constructed.**

### Finding 4 — `value()` discards error code and source location; accessors are non-const (minor)

- **Evidence:** `value()` throws `std::runtime_error(m_error.description.toStdString())` — only the description survives into the exception; `error_code` and `location` are dropped. `value()`, `value_or()` are non-const even though they do not mutate.
- **Risk:** A caller that catches the exception (rather than checking `has_value()`) loses the machine-readable code and the origin location, degrading diagnostics to a bare string. Non-const accessors prevent reading values from `const expected<T>&`.
- **Impact:** Maintainability/diagnostic quality; low runtime risk since the established CLI pattern checks `has_value()` first.
- **Mitigation:** Include the code in the exception message (e.g., `"error <code>: <description>"`) or throw a small custom exception type carrying the full `error_frame`; mark `value()`, `value_or()`, and `has_value()` const where practical.
- **Follow-up test recommendation:** Extend `expected_test.cpp`'s `test_make_expected_error_value_throws` to verify the thrown message contains the error code after mitigation; add a const-correctness test reading through `const expected<int>&`.
**User: Actionable, first make value_or() to be const, and value() to be const returning const T&. Second, compose a exception description including error code, description and source:line.**

### Finding 5 — `T m_value {}` requires default-constructible `T` and keeps a dead value in the error state (design constraint)

- **Evidence:** The member is declared `T m_value {};`, so every instantiation value-initializes a `T` even when the object holds an error; and any non-default-constructible `T` makes `expected<T>` uninstantiable.
- **Risk:** For heavy `T` (e.g., `QImage` in `render_page`), every error result still pays for a default-constructed image's storage; future value types without a default constructor cannot be used without redesign.
- **Impact:** Minor memory overhead on the error path; reduced type flexibility. Not a correctness bug for current instantiations.
- **Mitigation:** If the constraint ever bites, switch to `std::optional<T>`/`std::variant<T, error_frame>` storage or a union with explicit lifetime management; document the default-constructibility requirement in the header until then.
- **Follow-up test recommendation:** None required for current code; a static assertion (`std::is_default_constructible_v<T>`) would make the implicit requirement explicit and turn accidental violations into clear compile errors.
**User: Actionable. Store `T` as `std::variant<T,error_frame>`.**

### Finding 6 — Unescaped descriptions allow frame forgery in `error_stack::to_string()` (minor security)

- **Evidence:** `to_string()` concatenates `frame.description` verbatim into a multi-line string (`"Error %1: %2\n"` followed by indented location lines). A description containing embedded newlines (e.g., text derived from network error strings or file contents) can inject lines that visually mimic additional error frames or location lines.
- **Risk:** Log/display spoofing: an attacker-influenced or corrupted description could make logs appear to contain errors (or locations) that did not occur, complicating incident analysis.
- **Impact:** Low — the output is diagnostic text, not parsed by security-relevant code in this repository; but descriptions do originate partly from external sources (Qt network error strings, file paths).
- **Mitigation:** Sanitize or escape newlines in `description` when rendering (e.g., replace `\n`/`\r` with a visible marker), or document that `to_string()` output is untrusted diagnostic text.
- **Follow-up test recommendation:** A unit test pushing a frame whose description contains `\n  at fake.cpp:1` and asserting the rendered output cannot be misread as a separate frame (after mitigation, asserting the escaped form).
**User: Actionable, lets remove new lines and simplify spaces (double spaces to single spaces, or tabs to single space, etc.).**

### Finding 7 — Error codes are unnamespaced `int`s with cross-module collisions (contextual)

- **Evidence (contextual, outside the pair):** `model.cpp` uses `-1` for at least three distinct failures ("No image configured for request", "Cannot open file: …", "Cannot encode QImage to bytes") and casts `QNetworkReply::NetworkError` to `int`; `pdf_renderer.cpp` defines its own `-1..-4` module-local codes. The pair itself defines no code registry.
- **Risk:** A consumer switching on `error().error_code` cannot reliably distinguish failures across modules; `-1` means different things in different subsystems, and Qt's enum values may overlap the ad-hoc negatives.
- **Impact:** Maintainability and diagnostic accuracy for any code that branches on codes rather than descriptions.
- **Mitigation:** Introduce per-module `enum class` error codes (or a shared namespaced enum) and construct `error_frame` from them; keep `int` only as the wire/storage representation.
- **Follow-up test recommendation:** A test asserting that each module's error codes are unique within that module, once enums exist.

**User: Actionable, make each translation unit (.hpp/.cpp) to have a distionary of error like `QMap<int,QString>`. If we use the same error code in multiple lines, it could happend each to have a sligly different error descriptions.**

### Finding 8 — `error_stack` is unused by production code (low)

- **Evidence:** Grep across `src/` shows `error_stack` referenced only in `expected.hpp`/`expected.cpp` and `test/backend/core/expected_test.cpp`; no backend, frontend, or CLI code constructs one.
- **Risk:** Dead API surface: behavior can drift (e.g., the finding-6 rendering issue) without any production signal, and readers may assume error stacking is in use when it is not.
- **Impact:** Maintainability only.
- **Mitigation:** Either wire `error_stack` into a real aggregation point (e.g., CLI workflow error reporting) or mark it as reserved/experimental in the header.
- **Follow-up test recommendation:** None beyond the existing tests; revisit when (if) it is adopted.
**User: Mark it as `experimental or not fully specified feature.**

### Residual risks and unanalyzed assumptions

- No thread-safety guarantees exist anywhere in the pair; none were assumed in this analysis.
- `QString::toStdString()` in `value()` performs UTF-16 → UTF-8 conversion; non-representable sequences are replaced per Qt's rules — acceptable, but exception messages for non-ASCII descriptions may differ from the stored text.
- Behavior of callers that store `expected<T>` across Qt event-loop boundaries (signals/slots) was not analyzed; only the synchronous usage sites in `src/cli` and `pdf_renderer` were reviewed.
- The frontend (`src/frontend/`) does not currently use this pair; its future adoption is out of scope.
