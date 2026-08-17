# pdf_renderer

## Source Files

- `src/backend/core/pdf_renderer.hpp` — Header: `pdf_renderer` class declaration in namespace `dir2md::backend`.
- `src/backend/core/pdf_renderer.cpp` — Implementation of `open()`, `page_count()`, and `render_page()`, plus module-local error codes and a QtPDF load-error description helper.

**Counterpart status:** Both files present and analyzed as a pair.

---

## Purpose and Role

`pdf_renderer` is the backend's PDF-to-image bridge. It wraps QtPDF's `QPdfDocument` to:

1. Open a PDF file from disk and report its page count.
2. Render individual pages (0-based) into in-memory `QImage`s at a caller-chosen resolution.

It exists so that the OCR pipeline can feed per-page images to the model client **without ever writing intermediate page files to disk**. The only production consumer is the CLI OCR workflow (`src/cli/ocr_workflow.cpp`, `process_source()`), which opens a PDF, then loops over pages rendering one at a time and sending each image to `image_to_text_client`.

The class is intentionally a thin, stateful wrapper: all PDF parsing and rasterization is delegated to `QPdfDocument`; the pair adds only error mapping (QtPDF error enum → `expected<>` with a human-readable message), index validation, and DPI-to-pixel conversion.

---

## Major Structures

### `pdf_renderer`

| Member | Type | Role |
|---|---|---|
| `m_document` | `std::unique_ptr<QPdfDocument>` | The loaded PDF document. Null when no document is open (initial state, or after a failed `open()`). |

The class has no other state. There is no explicit `close()`/`reset()` method; the only way to release a document is by destroying the `pdf_renderer` instance or by calling `open()` again (which replaces the pointer — see Static Analysis finding 1).

### Module-local error codes (`pdf_renderer.cpp`, anonymous namespace)

| Constant | Value | Meaning |
|---|---|---|
| `err_open_failed` | `-1` | `QPdfDocument::load()` returned a non-`None` error, or the document status was not `Ready`. |
| `err_no_document` | `-2` | `render_page()` called while no document is open. |
| `err_page_out_of_range` | `-3` | `page_index` outside `[0, page_count())`. |
| `err_render_failed` | `-4` | `QPdfDocument::render()` returned a null image. |

These are `inline const int` in an anonymous namespace: they are **not** visible to callers (see Static Analysis finding 5).

### `describe_load_error(QPdfDocument::Error, const QString &)` (file-local helper)

Maps each `QPdfDocument::Error` enumerator to a user-facing message that embeds the file path:

| QtPDF error | Message |
|---|---|
| `FileNotFound` | `PDF file not found: <path>` |
| `InvalidFileFormat` | `Invalid or corrupted PDF file: <path>` |
| `IncorrectPassword` | `PDF file is password protected: <path>` |
| `UnsupportedSecurityScheme` | `PDF file uses an unsupported security scheme: <path>` |
| `DataNotYetAvailable` | `PDF data not yet available: <path>` |
| `Unknown` / `None` / default | `Failed to open PDF file: <path>` |

---

## Public API and Usage Patterns

### `auto open(const QString &file_path) -> expected<int>`

- **Success:** value is the document's page count (≥ 0).
- **Failure:** error code `-1` with a description from `describe_load_error`.
- Side effect: replaces any previously open document (old `QPdfDocument` is destroyed by the `unique_ptr` reassignment).
- On failure the internal pointer is reset to null, so a failed `open()` leaves the renderer in the "no document" state.

### `auto page_count() const -> int`

Returns `m_document->pageCount()`, or `0` when no document is open. Callers use this as the loop bound for `render_page()`.

### `auto render_page(int page_index, int dpi = 150) -> expected<QImage>`

- **Success:** value is a `QImage` of the rendered page.
- **Failure codes:** `-2` (no document), `-3` (index out of range), `-4` (render produced a null image).
- Pixel size computation: `scale = dpi / 72.0` (PDF points are 72/inch); `width/height = max(1, round(pagePointSize * scale))`. The `max(1, …)` clamp guarantees a non-zero image size even for degenerate page sizes or non-positive DPI.

### Expected usage pattern (as used by the CLI)

```cpp
pdf_renderer renderer;
auto opened = renderer.open(path);
if (!opened.has_value()) { /* handle error */ }

for (int i = 0; i < renderer.page_count(); ++i) {
    auto page_image = renderer.render_page(i);   // default 150 dpi
    if (!page_image.has_value()) { /* handle error */ }
    // consume page_image.value() (e.g. send to OCR client);
    // the image is released at end of iteration
}
```

The one-page-at-a-time loop is deliberate: peak memory is bounded by a single page image plus the document, not by all pages simultaneously.

---

## Control Flow and State Transitions

State is fully determined by `m_document`:

```
        open() success                open() failure / initial
  (no doc) ──────────────────► (doc open) ────────────────────────► (no doc)
       ▲                            │  │
       │                            │  └─ render_page() → expected<QImage>
       │                            └──── page_count() → int
       └────────────────────────────┘
              open() again: unique_ptr reassignment destroys the old document
```

- `open()` is idempotent in the sense that it always leaves the renderer either in a valid "doc open" state or a clean "no doc" state; it never leaves a half-initialized document.
- `render_page()` performs three checks in order: document present → index in range → render non-null. Each failure returns immediately with its own error code, so the codes are mutually exclusive per call.

---

## Ownership, Lifetime, and Preconditions

- **Ownership:** `pdf_renderer` exclusively owns its `QPdfDocument` via `std::unique_ptr`. The default copy/move semantics of the class are implicitly deleted (a class with a `unique_ptr` member is not copyable; move is implicitly available but never used in the codebase).
- **QGuiApplication precondition:** The header documents that a `QGuiApplication` instance must exist before any method is called (QtPDF rendering requirement). This is **not enforced at runtime** by the pair — see Static Analysis finding 6. In the CLI it is satisfied by the `QGuiApplication` created in `main()`; tests construct one first.
- **Thread safety:** No synchronization and no documented thread-safety guarantee. All current usage is single-threaded (CLI main thread). See Static Analysis finding 4.
- **Exception safety:** The pair does not catch exceptions from QtPDF. If `QPdfDocument::load`/`render` throw (Qt generally signals failure via return values rather than exceptions, but this is not guaranteed by the pair), the exception propagates to the caller and `m_document` remains in whatever state the failed call left it — potentially a non-null pointer to a document that did not load. Callers relying solely on `expected<>` would not observe this.

---

## Input Validation and Error Handling

| Input | Validation | Behavior on invalid input |
|---|---|---|
| `file_path` (empty string) | None in the pair | Delegated to `QPdfDocument::load()`, which reports it as `FileNotFound` → error `-1`. |
| `file_path` (nonexistent / unreadable) | Via QtPDF | Error `-1` with `PDF file not found: …`. |
| `file_path` (password-protected, corrupt, unsupported security) | Via QtPDF | Error `-1` with the specific message from `describe_load_error`. |
| `page_index` < 0 or ≥ page count | Explicit check in `render_page()` | Error `-3` with the valid range in the message. |
| `dpi` ≤ 0 | **None** | Silently produces a 1×1 pixel image (clamped). See finding 2. |
| `dpi` very large | **None** | Pixel dimensions grow linearly with DPI; image memory grows quadratically. No cap. See finding 2. |

Error reporting is exclusively through `expected<>`; nothing is logged or thrown by the pair itself. The CLI consumer prints `error().description` to `std::cerr`.

---

## Contextual Dependencies

Understanding this pair requires awareness of (contextual, outside the pair):

- **`QPdfDocument` (QtPDF):** `load()`, `status()`, `error()`, `pageCount()`, `pagePointSize()`, and `render()` semantics. The pair assumes `load()` returning `None` plus `status() == Ready` is a sufficient success condition, and that `render()` returns a null `QImage` on failure.
- **`expected<T>` (`src/backend/core/expected.hpp`):** value-or-error holder; note its argument-count-based disambiguation between success and error constructors (see finding 5).
- **CLI consumer (`src/cli/ocr_workflow.cpp`):** the only production call site; establishes the single-threaded, one-page-at-a-time usage pattern and the `QGuiApplication` precondition.
- **Tests (`test/backend/core/pdf_renderer_test.cpp`):** cover missing file, invalid file, page count + render of a valid multi-page PDF, render-without-open, and that no per-page files are written to disk.

---

## Static Analysis and Security

### Finding 1 — `open()` does not close a previously open document before replacing it

- **Evidence:** `open()` begins with `m_document = std::make_unique<QPdfDocument>();` (line 36 of `pdf_renderer.cpp`). If a document is already open, the `unique_ptr` reassignment destroys the old `QPdfDocument` *after* the new one has been constructed and loaded. There is no `close()`/`reset()` API.
- **Risk:** Calling `open()` twice on the same instance (e.g., a CLI loop over multiple PDFs reusing one renderer, or a future frontend doing so) holds two full PDF documents in memory simultaneously during the second load. For large PDFs this doubles peak memory transiently, and there is no way to release a document before opening the next one short of destroying the whole `pdf_renderer`.
- **Impact:** Memory pressure / potential OOM on large multi-document workloads; surprising lifetime semantics for future callers who expect "open replaces and releases the old one first".
- **Mitigation:** At the top of `open()`, release the previous document before constructing the new one (`m_document.reset();` before `make_unique`), or add an explicit `close()` method and document that `open()` on an open instance is a replace-without-pre-release. Alternatively, make the class non-reopenable (assert/return error if already open) to force explicit lifecycle management.
- **Follow-up test recommendation:** A test that opens PDF A, then opens PDF B on the same instance, and asserts `page_count()` reflects B; combined with a memory-instrumented (heaptrack/ASan) run to confirm only one document is resident at a time after the mitigation.

### Finding 2 — `dpi` is not validated; non-positive or extreme values are silently accepted

- **Evidence:** `render_page(int page_index, int dpi = 150)` computes `scale = static_cast<qreal>(dpi) / 72.0` and then clamps dimensions with `qMax(1, qRound(...))`. There is no check that `dpi > 0` or that the resulting pixel dimensions are below any bound.
- **Risk:** (a) `dpi <= 0` yields `scale <= 0`, so every dimension rounds to ≤ 0 and is clamped to 1 — the caller silently receives a 1×1 pixel image instead of an error, which will produce garbage OCR output with no diagnostic. (b) A very large `dpi` (e.g., 12000) makes pixel dimensions ~167× the page size; since image memory is proportional to width × height, this can allocate hundreds of MB per page with no guard.
- **Impact:** Silent correctness failure for misconfigured DPI (wrong output, no error signal); unbounded memory allocation from a single integer argument, which in a CLI processing many pages could exhaust memory.
- **Mitigation:** Validate `dpi` at the top of `render_page()` and return a dedicated error (e.g., reuse `err_render_failed` or add `err_invalid_dpi`) for `dpi <= 0`; optionally cap the resulting pixel dimensions (or DPI) at a sane maximum and document the cap.
- **Follow-up test recommendation:** Unit tests asserting that `render_page(0, 0)` and `render_page(0, -5)` return an error (after mitigation), and that a very large DPI either errors or is clamped to the documented maximum.

### Finding 3 — `page_count()` and `render_page()` can disagree if the document changes between calls

- **Evidence:** The CLI loop reads `renderer.page_count()` once as the bound, then calls `render_page(i)` per iteration. `render_page()` re-reads `m_document->pageCount()` for its range check. Nothing in the pair prevents the two observations from differing if the document is replaced (via `open()`) or mutated between the two reads.
- **Risk:** In the current single-threaded CLI flow this cannot happen, but any future pattern that reopens the renderer mid-loop (or shares it across threads) would get an out-of-range error (`-3`) for indices that were valid when the loop bound was computed — a confusing failure mode with no indication that the document changed underneath.
- **Impact:** Latent state-invariant violation; correctness depends on an undocumented "document is stable for the duration of the render loop" assumption.
- **Mitigation:** Document the stability precondition in the header ("the open document must not be replaced or closed between `page_count()` and the corresponding `render_page()` calls"); alternatively, have `render_page()` treat a shrunken page count as a distinct, descriptive error.
- **Follow-up test recommendation:** A test that opens a 2-page PDF, reopens a 1-page PDF, then calls `render_page(1)` and asserts the error message makes clear the index is out of range for the *current* document.

### Finding 4 — No thread-safety guarantee; shared instance would race on `m_document`

- **Evidence:** All three methods read/write `m_document` (and the underlying `QPdfDocument`) with no mutex, atomic, or documented single-threaded restriction. The header documents only the `QGuiApplication` precondition, not threading.
- **Risk:** If a future frontend (QtQuick) shares one `pdf_renderer` across threads — e.g., rendering on a worker thread while the UI queries `page_count()` — concurrent access to the `unique_ptr` is a data race (undefined behavior), and even same-thread interleaving with Qt's event loop could observe inconsistent state.
- **Impact:** Undefined behavior / crashes under concurrent use; the absence of any documented guarantee makes it easy for a future caller to introduce the race unknowingly.
- **Mitigation:** Add an explicit header comment stating the class is not thread-safe and must be confined to one thread (or one event loop), or, if cross-thread use is anticipated, document that each thread must use its own instance.
- **Follow-up test recommendation:** A TSan-instrumented test that calls `page_count()` from one thread while `render_page()` runs on another, expected to report a race (guarding the documented single-thread contract).

### Finding 5 — Error codes are module-local and invisible to callers; `expected<int>` success/error share one constructor

- **Evidence:** The four error codes are `inline const int` in an anonymous namespace in `pdf_renderer.cpp`; they are not declared in the header. Callers therefore cannot compare `opened.error().error_code` against a named constant — the CLI instead prints `error().description`. Separately, `expected<int>` disambiguates success from error purely by argument count: `expected<int>(m_document->pageCount())` (1 arg) is success, `expected<int>(err_open_failed, …)` (2 args) is error. A page count of `0` is a legitimate success value that numerically collides with no error code only because the codes are negative — an invariant maintained by convention, not by type.
- **Risk:** Callers who want programmatic error handling (retry on password prompt, skip corrupt files, map errors to UI states) must string-match human-readable descriptions, which are not a stable API and are localized in one place. If a future refactor changes a message, silent behavior changes follow. The negative-code convention is also unenforced: nothing prevents a future success value or error code from breaking the "negative = error" assumption.
- **Impact:** Maintainability and extensibility: the error channel is effectively description-only, and the int-collision safety rests on an undocumented invariant.
- **Mitigation:** Expose the error codes as named constants (or a scoped enum) in the header so callers can branch on them; keep descriptions for display only. Longer term, give `open()` a dedicated result type (e.g., a small struct with `page_count`) so success and error cannot share one constructor — this is also flagged in `docs/internal/backend/core/expected.md`.
- **Follow-up test recommendation:** A test asserting each failure path returns the expected numeric code (requires the codes to be visible, i.e., after mitigation), guarding against accidental code renumbering.

### Finding 6 — `QGuiApplication` precondition is documented but not enforced

- **Evidence:** The header comment states a `QGuiApplication` must exist before any method is called, but neither `open()` nor `render_page()` checks `QGuiApplication::instance()`.
- **Risk:** A caller (most likely a future test or a non-CLI entry point) that forgets to create the application object will hit undefined behavior or a hard crash inside QtPDF rather than a clean `expected<>` error, because the failure occurs below the pair's error-handling layer.
- **Impact:** Debuggability: the failure mode is a crash deep in Qt with no project-level diagnostic, contradicting the pair's otherwise clean error-reporting contract.
- **Mitigation:** Add an early check in `open()`/`render_page()` (e.g., `if (!QGuiApplication::instance()) return expected<…>(err_no_document, "QGuiApplication is required …");`) or at minimum a `qFatal`/assert with a clear message, so the precondition fails loudly and locally.
- **Follow-up test recommendation:** A test (run in a process without a `QGuiApplication`, or by temporarily nulling the instance if feasible) asserting that `open()` returns a descriptive error instead of crashing.

### Residual risks and unanalyzed dependencies

- **QtPDF internals not analyzed:** The behavior of `QPdfDocument::load`/`render` on edge-case PDFs (encrypted with unsupported schemes, linearized files, files with zero-size pages, extremely large page counts) is assumed from the Qt documentation and the error enum; it was not exercised here. In particular, whether `pagePointSize()` can return a zero or negative size for malformed pages (which would be clamped to 1×1 by `qMax`) is an unverified assumption.
- **Exception behavior of QtPDF:** Whether `QPdfDocument` methods can throw was not verified; the pair has no `try/catch`, so any exception propagates with `m_document` in an unspecified state (see Ownership section).
- **`expected<T>` copy cost for `QImage`:** `render_page()` returns `expected<QImage>` by value and the CLI consumes it via `.value()`, which copies the image (the move constructor of `expected` is used for the return, but `value()` returns a copy). For large pages this is an extra full-image copy per page; it is bounded by the one-page-at-a-time design but was not profiled.
- **Frontend usage:** No frontend consumer exists yet; all findings about future misuse (findings 1, 3, 4) are forward-looking and grounded in the current single CLI call site only.
