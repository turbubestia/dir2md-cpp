# `src/backend/core/thinking_stripper.hpp` / `thinking_stripper.cpp`

## Purpose and Role

A small stateful text filter in the backend core (`dir2md::backend`) that consumes **streamed** model output fragments and removes inline `think` / `reasoning` blocks while passing all other text through unchanged. It exists because model clients (see `model.cpp`) deliberately do not parse dedicated reasoning fields; instead, inline `<think>...</think>` / `<reasoning>...</reasoning>` tags inside content tokens pass through verbatim and are stripped downstream by this class.

Key properties:

- **Chunk-boundary safe**: a tag split across fragments is carried in a pending buffer (`m_pending`) between `process()` calls, so partial tags are neither dropped nor leaked into output.
- **Indicator emission**: the constant `thinking_indicator` (`"thinking ..."`, declared `inline const QString` in the header) is emitted exactly once, on the fragment where a block first opens, so consumers can show the user that thinking is happening without repeating it per chunk.
- **File-safe contract**: consumers that accumulate text for file output must strip `thinking_indicator` from the accumulated result (the header documents this; `src/cli/ocr_workflow.cpp` does so via `QString::remove`).

The class is a pure-logic utility with no QObject involvement, no timers, and no I/O. It is built into the backend core library (`src/backend/core/CMakeLists.txt`) and currently consumed by the CLI workflows (`src/cli/chat_workflow.cpp`, `src/cli/ocr_workflow.cpp`); the frontend does not reference it directly (contextual observation).

## API Surface

All members of `class thinking_stripper` use trailing return types.

| Member | Signature | Behavior |
|---|---|---|
| `process` | `(const QString &fragment) -> QString` | Appends the fragment to the pending buffer, scans for complete tags, and returns the text to emit for this fragment (outside-block text plus at most one indicator). Inside a block, content is discarded. |
| `flush` | `() -> QString` | End-of-stream: releases the held partial-tag tail if outside a block; **discards** it if inside an (unclosed) block so tag fragments never leak into output. |
| `is_thinking` | `() const -> bool` | True while a think/reasoning block is in progress (`m_in_block`). |
| `reset` | `() -> void` | Clears all state (`m_in_block`, `m_open_name`, `m_pending`) so the instance can be reused for the next source/request. |
| `pending_tail_length` (private) | `(const QString &buffer, bool in_block) const -> int` | Length of the longest suffix of `buffer` that is a prefix of a tag relevant to the current state: opening tags (`<think>`, `<reasoning>`) outside a block, or the single matching closing tag inside one. |

Free constant: `thinking_indicator` — `inline const QString`, value `"thinking ..."`.

## Behavior Details

### State machine

Two states, tracked by `m_in_block` (plus `m_open_name` recording the bare name of the open block):

- **Outside a block**: `process()` searches the pending buffer (from the current scan position) for the earliest complete `<think>` or `<reasoning>` opening tag, case-insensitive. Text before it is emitted; on a hit, `thinking_indicator` is appended exactly once, state switches to in-block, and scanning resumes after the tag. If no complete opening tag exists, all provably-outside text is emitted and only a tail that could still grow into an opening tag is held back for the next fragment.
- **Inside a block**: everything is discarded until the **matching** closing tag (`</think>` or `</reasoning>` per `m_open_name`) is fully seen. While waiting, only a tail that could grow into that specific closing tag is retained; all other content is dropped immediately. On the close, state returns to outside-block and scanning continues in the same fragment (so adjacent blocks like `<think>a</think><think>b</think>` are handled in one call).

### Pending-tail algorithm

`pending_tail_length` bounds the hold-back: it iterates suffix lengths from `min(buffer.size(), longest_candidate_tag)` down to 1 and returns the first length whose suffix is a prefix of any candidate tag. Candidate tags are at most 12 characters (`</reasoning>`), so the scan is bounded and cheap. Because every candidate tag starts with `<`, a held tail always begins at a `<` in the buffer; text that cannot grow into a tag (e.g. `y<re` where `<re` is not a prefix of any candidate) is emitted immediately.

Consequence: at most one partial-tag tail is ever withheld, and it is only released later if it turns out not to be a tag (via the next `process()` call or `flush()` outside a block).

### Indicator semantics

- Emitted exactly once per block, on the fragment containing the opening tag — never repeated while the block stays open across fragments.
- Emitted even if the block is later found to be unclosed at end of stream (the indicator already went out; only the held tail is discarded by `flush()`).
- Adjacent blocks each get their own indicator.

### Tag matching rules

- Only the bare names `think` and `reasoning` are recognized; lookups use `QString::indexOf(..., Qt::CaseInsensitive)`, so `<THINK>`, `<Reasoning>`, etc. match. Other tags (e.g. `<thinking>`) pass through as plain text.
- The closing tag must **match the name of the open block** (`m_open_name`). A `<think>` block is only closed by `</think>`; `</reasoning>` inside it is treated as discarded content.
- Nesting is not tracked: an opening tag inside a block is just content, and the first matching close ends the block (pinned by `test_nested_blocks`).

### `flush()` / `reset()` semantics

- Outside a block at end of stream: the held tail (a potential partial opening tag) is returned, so trailing text like `"a <th"` yields `flush() == "<th"`.
- Inside a block at end of stream: the held tail is a fragment of the closing tag, not text — it is discarded and `flush()` returns `""`.
- `reset()` is the only operation that fully restores a fresh-instance state; it is required before reusing an instance for another source/request.

## Invariants and Assumptions

- **Single-threaded per instance**: the class holds mutable state with no synchronization. Current consumers each create one instance per request on one thread (contextual: `chat_workflow.cpp`, `ocr_workflow.cpp`). No thread-safety guarantee is documented or provided.
- **Fragment order**: output correctness assumes fragments arrive in stream order; reordering would break tag reassembly.
- **`flush()` before reuse is not sufficient**: after `flush()` on an unclosed block, `m_in_block` remains `true` (see Static Analysis, Finding 1); only `reset()` restores a clean state.
- **Indicator collision**: the file-safety contract relies on consumers removing every occurrence of the literal `"thinking ..."` from accumulated output; model text that legitimately contains that string is affected (see Finding 3).
- **No diagnostics**: an unclosed block at end of stream is handled silently (tail discarded, no warning emitted).

## Contextual Dependencies

- `src/backend/core/model.cpp` (`model_client_base::process_line`): accumulates content tokens and emits them via the `incremental_chunk` signal; explicitly leaves inline think/reasoning tags in the text for the downstream stripper. Fragment granularity is whatever the coalescing timer produces, so tag-splitting across fragments is a normal case.
- `src/cli/ocr_workflow.cpp`: routes each `incremental_chunk` through `stripper.process`, prints emitted text, calls `stripper.flush()` on completion/error, and strips the indicator from the accumulated content before writing files.
- `src/cli/chat_workflow.cpp`: same pattern for interactive chat output (prints only; no file accumulation).
- `test/backend/core/thinking_stripper_test.cpp`: QTest suite covering passthrough, complete blocks, tags split across fragments, adjacent and nested blocks, single-indicator-per-block, case-insensitivity, unrecognized tags, both `flush()` paths, and `reset()` reuse.

## Static Analysis and Security

### Finding 1: `flush()` on an unclosed block leaves the instance still "in block" (state-invariant / API misuse hazard)

- **Evidence**: In `thinking_stripper.cpp`, `flush()` in the `m_in_block` branch only does `m_pending.clear(); return "";` — it does not clear `m_in_block` or `m_open_name`. Consequently, after end-of-stream with an unclosed block, `is_thinking()` still returns `true`, and any subsequent `process()` call continues to discard text as block content until a matching close tag appears. The header documents `reset()` as the reuse path, but nothing prevents a consumer from calling `process()` after `flush()` without `reset()`.
- **Risk**: A consumer that flushes at end of stream and then reuses the instance (or polls `is_thinking()` afterwards) gets surprising behavior: real answer text silently swallowed, or a stale "thinking" status.
- **Impact**: Correctness — silent loss of output text on a plausible reuse path; the state after `flush()` is not a well-defined resting state.
- **Mitigation**: Either make `flush()` also clear `m_in_block`/`m_open_name` (end of stream means no block is in progress), or explicitly document in the header that `flush()` does not restore a usable state and that `reset()` must follow before any further `process()` call.
- **Follow-up test recommendation**: Extend the unclosed-block flush test to assert the post-flush behavior explicitly: `QVERIFY(stripper.is_thinking())` (or `false`, once fixed) and that a subsequent `process("hello")` returns `"hello"` rather than `""`.

### Finding 2: Mismatched closing tags cause silent swallowing of all following text

- **Evidence**: Inside a block, `process()` searches only for `close_tag(m_open_name)` — the close must match the open name. For input like `<think>a</reasoning>b`, the `</reasoning>` is treated as discarded content; the stripper stays in-block and discards everything until a `</think>` appears or the stream ends (verified by tracing `process()`; no test covers mismatched closes).
- **Risk**: Models occasionally emit malformed or cross-named tags (e.g. open `<think>`, close `</reasoning>`), especially under truncation. When that happens, all subsequent answer text is silently dropped — potentially the entire response.
- **Impact**: Correctness/stability — total loss of the user-visible answer with no diagnostic; hard to diagnose from output alone.
- **Mitigation**: Consider accepting any recognized closing tag (`</think>` or `</reasoning>`) to end a block, or at minimum emit a `qWarning()` in `flush()` when an unclosed block is discarded so the condition is observable. Document the matching-close rule in the header if it is kept as-is.
- **Follow-up test recommendation**: Add a test for `<think>a</reasoning>b` pinning current behavior (stays in-block, `b` swallowed), and one asserting the warning fires on unclosed-block flush if that mitigation is adopted.

### Finding 3: Consumer-side indicator removal strips legitimate text containing the literal indicator string (contextual)

- **Evidence**: The header contract says consumers "should strip this constant so files contain only the stripped text." `src/cli/ocr_workflow.cpp` implements this as `result.content = emitted.remove(dir2md::backend::thinking_indicator);` — `QString::remove(const QString&)` removes **all** occurrences. The stripper itself passes plain text through verbatim, so model answer text that literally contains `"thinking ..."` (outside any block) reaches `emitted` and is then removed from the file content.
- **Risk**: Output files silently lose any genuine occurrence of the indicator string in the model's answer.
- **Impact**: Correctness — data corruption in generated markdown, bounded by how often the exact phrase appears in answers (low probability, but the phrase is short and English).
- **Mitigation**: Track indicator emissions in the stripper (e.g. a counter or a distinct internal marker) so consumers can remove exactly the emitted indicators rather than all substring occurrences; or choose a sentinel that is effectively impossible in model output. Labeling: this finding depends on consumer code outside the requested pair and is reported as a contextual assumption about how the documented contract is implemented.
- **Follow-up test recommendation**: CLI-level test: a mocked stream whose answer contains the literal text `"thinking ..."` outside any block should produce file content that still contains it.

### Finding 4: No exception-safety guarantee across `process()` (fragment output lost on allocation failure)

- **Evidence**: `process()` mutates state incrementally: `m_pending += fragment` first, then a series of allocating operations (`out += ...`, `m_pending.mid(...)`, `QStringList`/`QString` temporaries in `pending_tail_length`). If any allocation throws `std::bad_alloc` after `m_pending` has grown but before the return, the emitted text for that fragment is lost and cannot be recovered (the fragment is now inside `m_pending` in a partially-scanned state). The class documents no exception-safety guarantees.
- **Risk**: Under memory pressure, a stream can lose a chunk of answer text with the instance left in a consistent-but-lossy state; subsequent fragments continue to process normally, masking the loss.
- **Impact**: Stability — minor in practice (Qt `QString` throws only on OOM), but the failure mode is silent partial data loss rather than a clean error.
- **Mitigation**: Document that `process()` provides no strong exception guarantee and that callers should treat `std::bad_alloc` as fatal for the request (abort and report), or restructure to compute the output into locals before committing `m_pending` changes (strong guarantee at the cost of one extra copy).
- **Follow-up test recommendation**: Not practically testable without fault injection; cover by documenting the assumption instead.

### Finding 5: No thread-safety (assumption, not a defect)

- **Evidence**: All members mutate shared member state (`m_in_block`, `m_open_name`, `m_pending`) with no synchronization; the header documents no concurrency guarantees.
- **Risk**: Concurrent `process()`/`flush()`/`reset()` calls on one instance would race and corrupt state.
- **Impact**: None under current usage — each CLI workflow creates a per-request instance used from a single thread (contextual). Flagged only so the assumption is explicit rather than accidental.
- **Mitigation**: Add a one-line header note that instances are not thread-safe and must not be shared across threads.
- **Follow-up test recommendation**: None; document-only.

### Residual notes (not material findings)

- **Case folding**: `Qt::CaseInsensitive` uses simple case folding, which is exact for the ASCII tag names involved; no Unicode edge cases are reachable for `think`/`reasoning`.
- **Unclosed-block flush is silent**: `flush()` discards the held tail without any diagnostic (see Finding 2 mitigation).
- **Performance**: per-fragment work is O(fragment size) plus a bounded tail scan (candidate tags ≤ 12 chars); repeated `QString` copies (`mid`, `right`) are small and acceptable for streaming use. No action needed.
- **`thinking_indicator` as `inline const QString`**: dynamically initialized global; thread-safe under C++11 magic-static rules, negligible cost. Could be a function-local static or `QLatin1String`-based helper if static-initialization order were ever a concern (it is not here).
