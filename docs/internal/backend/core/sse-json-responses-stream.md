# CLI `chat` — "response received, nothing printed" findings

**Status:** Root cause **confirmed**. Diagnosis tooling + integration test implemented. The parser fix itself is a **pending follow-up** (see [The fix](#the-fix-pending-follow-up)).

**Date:** 2026-08-15/16

---

## Source Files

- `src/backend/core/model.cpp` — `openai_schema_parser::parse_line` (response parsing), `model_client_base::on_ready_read` / `process_line` (stream handling), `construct_request` (sets `"stream": true`).
- `src/backend/core/model.hpp` — `model_client_base` declaration; new test-only raw-line probe API.
- `test/cli/chat_integration_test.cpp` — **new** in-process integration test + mock model server.
- `test/cli/CMakeLists.txt` — registers the new test target `cli_chat_integration_test`.
- `src/cli/chat_workflow.cpp` — `execute_chat` (the stdout print path; verified correct, not the cause).

---

## The Problem

Running the CLI against a real OpenAI-compatible endpoint:

```
./dir2md_cli.exe chat --system "you are a mathematician" --prompt hello
```

prints the header (`temperature:`, `system prompt:`, `user prompt:`), **waits until the model finishes**, then prints **nothing** and exits with code 0. No error is reported.

The configured settings were:

```json
"language-model": {
    "endpoint": "http://192.168.1.147:8080/v1/chat/completions",
    "model-name": "Qwen3.8-27B-MTP"
}
```

The CLI print path (`execute_chat`, `src/cli/chat_workflow.cpp`) was verified to be correct: `incremental_chunk` → `thinking_stripper` → `std::cout`, plus `completion`/`error_occurred` handlers, all wired and the event loop pumped. The text is lost **before** it reaches the CLI — in the backend parser.

---

## Root Cause

The endpoint is a **llama.cpp** server. For `stream: true` requests it responds with **Server-Sent Events (SSE)**:

- `Content-Type: text/event-stream`
- `Transfer-Encoding: chunked`
- Every payload line is prefixed with `data: `, and the stream ends with `data: [DONE]`.

But the client parses each raw line directly as JSON. In `openai_schema_parser::parse_line` (`src/backend/core/model.cpp:44`):

```cpp
QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parse_error);
if (parse_error.error != QJsonParseError::NoError) {
    return "";   // <-- silent drop
}
```

A line like `data: {"choices":[...]}` is **not** valid JSON because of the `data:` prefix, so `fromJson` fails and the function returns `""` **silently** for every line. No token is ever accumulated, so `completion` fires with an empty string and the CLI prints nothing — exactly the observed symptom.

### Why existing tests / smoke runs did not catch it

`build/smoke_mock_server.py` serves **raw NDJSON without the `data:` prefix** (its own comment says *"the format model_client_base expects"*), and the unit test `test_openai_parse_streaming_line` feeds bare JSON. Both match what the parser currently accepts, so they pass — while any real SSE endpoint drops everything.

### Confirmed wire format (from the real endpoint)

Captured via the new debug log (`%TEMP%\dir2md_model_debug.log`, 39 lines, all `data:`-prefixed):

1. First line: `delta.content: null` (role `assistant`).
2. Many lines: `delta.reasoning_content: "..."` — **Qwen3.8 is a thinking model**; llama.cpp streams the chain-of-thought in a **separate `reasoning_content` field**, not as `<think>` tags inside `content`.
3. Answer lines: `delta.content: "Hello"`, `"!"`, `" How"`, … (the actual reply).
4. Final: a `finish_reason: "stop"` line, then `data: [DONE]`.

---

## How It Was Diagnosed

Two independent confirmations:

1. **`curl`** against the endpoint with the same payload the client sends — showed the full SSE stream and headers (`text/event-stream`, chunked, `data:` lines).
2. **The real CLI's debug log** — set `DIR2MD_MODEL_DEBUG=1`, ran the actual `dir2md_cli.exe chat …`, and read `%TEMP%\dir2md_model_debug.log`. It captured all 39 raw lines arriving at the client, proving the data reaches the process and is dropped by the parser (not lost on the wire).

### Diagnosis tooling added (this work)

- **Raw-line probe** on `model_client_base` (`src/backend/core/model.hpp`): test-only static `set_raw_line_probe(...)` / `clear_raw_line_probe(...)`, invoked at the top of `process_line` **before** schema parsing. Mirrors the existing `SettingsManager::setTestBaseDirectory` pattern (process-global, no-op unless set).
- **Env-gated debug dump** in `process_line` (`src/backend/core/model.cpp`): when `DIR2MD_MODEL_DEBUG` is set, every raw response line is written to stderr **and** appended to `%TEMP%\dir2md_model_debug.log`. The file survives piped/redirected stderr (the terminal's inline stderr capture for native exes is unreliable). The env check is cached in a function-local `static`.

### Integration test added

`test/cli/chat_integration_test.cpp` (target `cli_chat_integration_test`) drives `text_to_text_client` **directly** (not through `execute_chat`) against an in-process `QTcpServer` mock, so any drop is localized to the backend parser. The mock has two response modes:

- **NDJSON** — raw JSON lines (what the client currently expects; mirrors `smoke_mock_server.py`).
- **SSE** — `data:`-prefixed lines + `data: [DONE]` (what real servers send).

Slots:

| Slot | Asserts |
|---|---|
| `test_ndjson_response_completes_with_text` | NDJSON mock → completion text contains the streamed words (happy path works). |
| `test_sse_response_dropped` | SSE mock → completion text is **empty** (reproduces the bug) **and** the probe captured `data:`-prefixed lines (proves they arrived but were dropped by the parser). |
| `test_probe_captures_raw_lines` | Probe saw the exact raw lines (4 content + 1 usage). |
| `test_request_payload_is_openai_format` | Outgoing body has model name, `stream: true`, system + user roles. |

The NDJSON-vs-SSE pair is the key: the same client + mock works in one mode and drops everything in the other, proving the test is genuinely sensitive to the format difference.

**Result:** all 139 tests pass (4 new), clean build, no regressions.

---

## The Fix (pending follow-up)

Not yet implemented — this task was scoped to diagnosis only. The fix belongs in `model_client_base::on_ready_read` / `process_line` (`src/backend/core/model.cpp`):

1. **Strip the SSE `data:` prefix** from each line before parsing (leading `data:` + surrounding whitespace). Treat a bare `[DONE]` as end-of-stream.
2. Once stripped, the existing `delta.content` extraction already works — the answer will print. No change to `parse_line`'s field lookup is strictly required for the streaming case.

Notes / optional hardening:

- **Thinking tokens:** because llama.cpp puts chain-of-thought in a separate `reasoning_content` field (not `content`), it is naturally *not* passed to the `thinking_stripper` — the user simply sees the final answer, which is the desired behavior. If thinking display is ever wanted, `parse_line` would need to read `delta.reasoning_content`.
- **Non-streaming fallback:** if a server ignores `stream: true` and returns a single JSON body, content lives at `choices[0].message.content`, which `parse_line` does not currently check (it checks `delta.content` and `choice.text`). Add `message.content` for robustness.
- **Partial-line buffering:** `on_ready_read` splits each `readAll()` chunk independently; a JSON line split across two TCP reads would be dropped. Low probability for small replies, but a latent bug worth a carry-over buffer.

**Verification after the fix:** `test_sse_response_dropped` should be updated to assert the SSE text is now present (or renamed), and the real CLI should print the answer against the configured endpoint.

---

## Reproducing

```powershell
# Capture the real endpoint's raw lines (diagnosis aid):
$env:DIR2MD_MODEL_DEBUG = "1"
.\build\cmake-debug\dir2md_cli.exe chat --system "you are a mathematician" --prompt hello
Get-Content "$env:TEMP\dir2md_model_debug.log"

# Run the integration test:
ctest --preset debug -R "cli.chat_integration_test"
```

---
## **User** Feedback

Responses are **not** always sent in SSE mode.

The use of SSE (Server-Sent Events) is controlled entirely by the **`"stream"`** parameter.

### 1. When `"stream": false` (Default)

If `stream` is `false` or omitted entirely, the server holds the connection open until the model finishes generating the complete text.

* **`Content-Type`:** `application/json`
* **Format:** A single, standard JSON object.
* **`data:` prefix:** **No.** You do **not** get `data: ...` or `[DONE]` sentinels.

**Example HTTP Response Body:**

```json
{
  "id": "chatcmpl-123",
  "object": "chat.completion",
  "created": 1700000000,
  "model": "local-model",
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": "Hello! How can I assist you today?"
      },
      "finish_reason": "stop"
    }
  ],
  "usage": {
    "prompt_tokens": 10,
    "completion_tokens": 8,
    "total_tokens": 18
  }
}

```

### 2. When `"stream": true`

When you explicitly set `"stream": true`, the server switches to SSE mode to stream tokens to you incrementally as they are generated.

* **`Content-Type`:** `text/event-stream`
* **Format:** Line-delimited SSE chunks starting with `data:`.
* **`data:` prefix:** **Yes.** Every chunk is prefixed with `data: `, ending with `data: [DONE]` to signal completion.

**Example HTTP Response Body:**

```text
data: {"id":"chatcmpl-123","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":""}}]}

data: {"id":"chatcmpl-123","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":"Hello"}}]}

data: {"id":"chatcmpl-123","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":"!"}}]}

data: [DONE]

```

| Parameter | Response Header | Response Body Format | `data:` Prefix? |
| --- | --- | --- | --- |
| **`"stream": false`** | `application/json` | Standard JSON object | **No** |
| **`"stream": true`** | `text/event-stream` | SSE chunks (`data: {...}`) | **Yes** |

llama.cpp allows a native mode in `completion/` endpoints or in `v1/chat/completion/` endpoints. In both cases, stream parameter is available. The `"stream"` parameter is supported in both native and OpenAI-compatible endpoints in `llama.cpp` (`llama-server`).

Setting `"stream": true` or `"stream": false` works across both interface styles, though the JSON payload structure delivered over the stream differs depending on which route you call.

---

### 1. Native Endpoint (`POST /completion`)

* **Request:** You pass standard `llama.cpp` native fields along with `"stream": true`.
```json
{
  "prompt": "Building a web server in C++ requires",
  "n_predict": 64,
  "stream": true
}

```

* **Streaming Format:** Streams using SSE (`text/event-stream`), returning native `llama.cpp` JSON payloads.
```text
data: {"content":" a","stop":false}

data: {"content":" clear","stop":false}

data: {"content":".","stop":true,"tokens_evaluated":8,"tokens_predicted":3}

```

### 2. OpenAI-Compatible Endpoint (`POST /v1/chat/completions`)

* **Request:** You pass OpenAI-formatted message objects along with `"stream": true`.
```json
{
  "messages": [
    {"role": "user", "content": "Explain SSE in one sentence."}
  ],
  "stream": true
}

```

* **Streaming Format:** Streams using SSE, matching OpenAI's exact delta payload structure and concluding with the `[DONE]` sentinel line.
```text
data: {"id":"chatcmpl-1","object":"chat.completion.chunk","choices":[{"delta":{"role":"assistant","content":""}}]}

data: {"id":"chatcmpl-1","object":"chat.completion.chunk","choices":[{"delta":{"content":"SSE"}}]}

data: {"id":"chatcmpl-1","object":"chat.completion.chunk","choices":[{"delta":{"content":" streams"}}]}

data: [DONE]

```

| Endpoint | Native `/completion` | OpenAI `/v1/chat/completions` |
| --- | --- | --- |
| **Supports `"stream": true`?** | **Yes** | **Yes** |
| **Transport Protocol** | Server-Sent Events (`text/event-stream`) | Server-Sent Events (`text/event-stream`) |
| **Chunk Structure** | Native (`{"content": "...", "stop": false}`) | OpenAI Delta (`{"choices": [{"delta": ...}]}`) |
| **End Sentinel** | Last object contains `"stop": true` | `data: [DONE]` |

Whether streaming (`stream: true`) or non-streaming (`stream: false`), reasoning content can arrive **either** as a separate JSON field or embedded directly inside `<think>` / `<reasoning>` tags.

### Thinking and Reasoning Channels

Which behavior occurs is **not decided by the model itself**, but by whether the inference server (such as `llama-server`, Ollama, vLLM, or DeepSeek API) has an active **reasoning parser** enabled.

### 1. Streaming Mode (`stream: true`)

#### Scenario A: Dedicated Reasoning Channel (Parsed)

If the server has a reasoning parser active, it intercepts raw model tokens, strips the `<think>` tags, and streams the thought process into a dedicated field (most commonly `delta.reasoning_content` or `delta.thinking`). Once thinking finishes, tokens switch over to `delta.content`.

```json
/* Phase 1: Reasoning */
data: {"choices":[{"delta":{"reasoning_content":"Step 1: Calculate 2+2..."}}]}

/* Phase 2: Final Answer */
data: {"choices":[{"delta":{"content":"The answer is 4."}}]}

```

#### Scenario B: Inline Tags in `delta.content` (Unparsed)

If the server does not parse reasoning tags (or if parsing is disabled), it streams raw output directly into `delta.content`. Your client receives literal `<think>` and `</think>` tags inside the standard content stream:

```json
data: {"choices":[{"delta":{"content":"<think>\nStep 1: Calculate 2+2...\n"}}]}
data: {"choices":[{"delta":{"content":"</think>\nThe answer is 4."}}]}

```

### 2. Non-Streaming Mode (`stream: false`)

Non-streaming responses mirror the exact same two behaviors:

#### Scenario A: Dedicated Reasoning Field

When reasoning parsing is enabled, the server splits the final payload into two separate message keys:

```json
{
  "choices": [
    {
      "message": {
        "role": "assistant",
        "reasoning_content": "Step 1: Calculate 2+2...",
        "content": "The answer is 4."
      }
    }
  ]
}

```

#### Scenario B: Embedded Tags in `message.content`

When reasoning parsing is disabled, the entire output—tags included—is placed into `message.content`, while `reasoning_content` is omitted or `null`:

```json
{
  "choices": [
    {
      "message": {
        "role": "assistant",
        "content": "<think>\nStep 1: Calculate 2+2...\n</think>\n\nThe answer is 4."
      }
    }
  ]
}

```

### How `llama.cpp` (`llama-server`) Handles This

In `llama.cpp`, this behavior is controlled via the **`--reasoning-format`** flag:

* **`--reasoning-format deepseek`**: Extracts thoughts into `reasoning_content` (cleans tags out of `content`).
* **`--reasoning-format none`**: Disables parsing; outputs raw `<think>` tags inside `content`.
* **`--reasoning-format deepseek-legacy`**: Populates `reasoning_content` *and* keeps `<think>` tags inside `content`.
* **`--reasoning-format auto`** *(Default)*: Automatically detects models like DeepSeek-R1 or Qwen-3 and extracts reasoning into `reasoning_content`.

## Native vs OpenAI Responses

No, the native (`/completion`) and OpenAI-compatible (`/v1/chat/completions`) endpoints have **different request formats and different response formats**. The OpenAI endpoint acts as a translation layer built on top of the native engine.

**1. Request Format Differences**

* **Native (`/completion`):** Expects a raw text string in the `"prompt"` field. You must format system instructions, user queries, and chat history manually into a single string.
* **OpenAI (`/v1/chat/completions`):** Expects a structured `"messages"` array. The server automatically formats this array using the model's GGUF chat template metadata (e.g., ChatML, Llama-3, DeepSeek) before handing it to the core model.

**2. Response Format Differences**

* **Native (`/completion`):** Returns a flat JSON object with the generated text placed directly in `"content"`.
* **OpenAI (`/v1/chat/completions`):** Returns OpenAI's standard nested structure containing `choices`, `message` or `delta` objects, `finish_reason`, and generation metadata (`id`, `object`, `usage`).

---

**Comparison Matrix**

| Feature | Native Endpoint (`/completion`) | OpenAI Endpoint (`/v1/chat/completions`) |
| --- | --- | --- |
| **Request Input** | `"prompt": "User: Hi\nAssistant:"` | `"messages": [{"role": "user", "content": "Hi"}]` |
| **Chat Templating** | Manual | Automatic (server applies GGUF template) |
| **Non-Stream Response** | `{"content": "Hello!"}` | `{"choices": [{"message": {"content": "Hello!"}}]}` |
| **Stream Chunk (`stream: true`)** | `{"content": "Hel"}` | `{"choices": [{"delta": {"content": "Hel"}}]}` |
| **End of Stream Signal** | Final object containing `"stop": true` | `data: [DONE]` line |

---

### What Happens Under the Hood

The underlying GGUF model always expects a single string of tokens and outputs a single string of tokens.

When you call `/v1/chat/completions`, `llama-server` converts your input message list into a raw string using the model's chat template, passes it to the inference engine, and translates the engine's output back into OpenAI-formatted JSON.

### Native mode with stream=true

In the native `llama.cpp` server (`POST /completion` endpoint), setting `"stream": true` works by establishing a **Server-Sent Events (SSE)** connection.

Instead of waiting for generation to finish and returning a large aggregate object, `llama-server` streams **one JSON object per generated token/chunk**, prefixed with `data: `.

---

### 1. Request Structure

You send a raw prompt string (and optionally generation parameters) along with `"stream": true`:

```bash
curl http://localhost:8080/completion \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "Building an embedded app requires",
    "n_predict": 32,
    "stream": true
  }'

```

---

### 2. Stream Response Mechanics

The server returns HTTP response headers specifying an event stream:

```text
HTTP/1.1 200 OK
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive

```

As generation occurs, the payload streams in line-delimited SSE frames:

```text
data: {"content":" clear","stop":false}

data: {"content":" planning","stop":false}

data: {"content":",","stop":false}

data: {"content":" robust","stop":false}

data: {"content":" memory","stop":false}

data: {"content":" management","stop":false}

data: {"content":".","id_slot":0,"stop":true,"model":"local-model","tokens_predicted":7,"tokens_evaluated":6,"generation_settings":{...},"stop_type":"limit","timing":{...}}

```

---

### 3. Key Differences from OpenAI Streaming

Unlike the OpenAI-compatible stream (`/v1/chat/completions`), the native `llama.cpp` stream operates under a distinct set of rules:

* **Flat Payloads:** Text arrives in a top-level `"content"` field (`{"content": "..."}`) rather than wrapped inside nested objects like `choices[0].delta.content`.
* **No `[DONE]` Sentinel:** Standard OpenAI streams end with a literal `data: [DONE]` string. Native `llama.cpp` does **not** send `[DONE]`.
* **Completion Flag (`"stop": true`):** The server signals completion through the JSON payload itself. Intermediate chunks set `"stop": false`.
* **Final Chunk Metadata Attachment:** The very last JSON chunk flip-switches `"stop": true` and contains metadata for the entire run, including execution timings, total tokens evaluated/predicted, and stopping reason.

---

### 4. Native Reasoning Support

If you run a reasoning model (like DeepSeek-R1) and use `--reasoning-format deepseek` or `auto`, the native stream separates thoughts from the completion text dynamically:

```text
data: {"reasoning_content":"Analyze requirement for embedded system...","stop":false}

data: {"content":"Clear planning...","stop":false}

```