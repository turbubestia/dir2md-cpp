# Implement a Chat and OCR Commands Workflow in CLI

In `src\backend\core` we have implemented the setting manager, a robust text search, and a LLM API model system. In this task we want to implement in `src\cli` two workflows: chat and ocr. These two operate as two main flow commands like
```
dir2md-cpp.exe chat --chat-options ... -> this activates the chat workflow
dir2md-cpp.exe ocr --ocr-options ...   -> this activates the ocr workflow
```
This means that in `chat` mode any --ocr-options (which is not also present in `chat` mode) would be invalid or not recognized. The same in `ocr` mode. The easies way is to implement a global workflow command parser, and per-module command parsers.

## Chat Mode
The chat mode for now is a single turn chat, we provide an optional system prompt and a mandatory user prompt and this message is compose and sent to `v1/chat/completion`. The chat options are
- `system` [optional] : system prompt which can be a direct text or a file path. If not specified we use the file from the setting `cli/system-prompt-file`.
```
dir2md-cpp.exe chat --system "Your a math expert"
dir2md-cpp.exe chat --system path/to/system-prompt.md
```
- `prompt` [mandatory] : this is the user prompt. Similarly it can be a text or a file path.
- `temperature` [options] : a single real value between 0.0 and 2.0. If not specified we use the setting `cli/temperature`.

The output of the command will be the used temperature, system prompt, and user prompt. Then it start to print the LLM model response as it arrives, similar to other chats when the answer is immediately display as they are received.

Once the answer ends, the program exits.

## OCR Mode
The OCR mode is a simple image-to-text workflow. We provide an optional system prompt and a mandatory image source. The options are:
- `source` [mandatory] : it can be a file path or a folder path. When the user gives a file path we directly pass it to the ocr model. When it is a folder we scan the folder (non-recursive, only the same level provided) for images (jpg, jpeg, or png) and PDF and print the list of discovered sources and then we ask the user to continue or not. If continue we run the ocr model over each image sequentially printing to the screen the ocr text as received.
- `temperature` [optional] : a single real value between 0.0 and 2.0. If not specified we use the setting `cli/temperature`.

Once all images are procesed, the program exits.

## Thinking mode
Some models may have thinking or reasoning mode enable by default, some can'v even turn it off. So we need to strip any <think> or <reasoning> tags. When a think or reasoning block is in progress we symply print `thinking ...`.

## PDF procesing

For PDF we want to use QtPDF to convert each page (keep only in memory) and pass it to the OCR module. We do this page by page and at the end we write all of them to a single markdown file.

## Output

In OCR, for image-to-text or pdf-to-text we produce a single markdown file per image with the same filename as the image.
In Chat mode no file is produced, only print to stdout.

---
# Refinement Iteration 1
**Status:** PENDING USER FEEDBACK

## 1. Executive Summary
Add two top-level workflow commands to the CLI (`chat` and `ocr`), each with its own isolated option set, built on the existing backend model clients (`text_to_text_client`, `image_to_text_client`) and `SettingsManager`. `chat` performs a single-turn, streamed text completion to stdout; `ocr` performs image/PDF-to-text, streaming results to stdout and writing one markdown file per source. Both strip model "thinking"/"reasoning" blocks from streamed output.

## 2. Refined Requirements & Acceptance Criteria

- **Requirement CLI-01: Global workflow command routing**
  - **Description:** The CLI exposes exactly two workflow subcommands, `chat` and `ocr`. A global parser selects the workflow; each workflow is then parsed by its own per-module parser. Options belonging to the other workflow are not recognized in the active workflow.
  - **Acceptance Criteria:**
    - [ ] Given `dir2md-cpp.exe chat ...`, When an OCR-only option is supplied, Then the command fails with a clear "unrecognized option" error and non-zero exit code.
    - [ ] Given `dir2md-cpp.exe ocr ...`, When a chat-only option is supplied, Then the command fails with a clear "unrecognized option" error and non-zero exit code.
    - [ ] Given no workflow subcommand (or an unknown one), When the program runs, Then it prints usage and exits non-zero.
    - [ ] Given `--help`/`-h`, When invoked at the top level or within a workflow, Then it prints the relevant options and exits zero.

- **Requirement CLI-02: Chat workflow — options**
  - **Description:** `chat` accepts `--prompt` (mandatory), `--system` (optional), and `--temperature` (optional, real value in `[0.0, 2.0]`).
  - **Acceptance Criteria:**
    - [ ] Given `chat` with no `--prompt`, When run, Then it errors (missing mandatory option) and exits non-zero.
    - [ ] Given `--temperature` outside `[0.0, 2.0]` or non-numeric, When run, Then it is rejected with a validation error and non-zero exit.
    - [ ] Given no `--temperature`, When run, Then the value from setting `cli/temperature` is used.

- **Requirement CLI-03: Chat workflow — prompt resolution (text vs file)**
  - **Description:** Both `--system` and `--prompt` accept either literal text or a file path. Resolution rule: if the value is an existing, readable file path, its contents are used; otherwise the value is used as literal text.
  - **Acceptance Criteria:**
    - [ ] Given `--system` equal to an existing file path, When run, Then the file's contents become the system prompt.
    - [ ] Given `--system` equal to free text (no matching file), When run, Then the text is used verbatim as the system prompt.
    - [ ] Given `--prompt` as a file path, When run, Then the file's contents become the user prompt.
    - [ ] Given `--system` omitted, When run, Then the system prompt is read from the file named by setting `cli/system-prompt-file` (see CLI-07 for the missing/empty case).

- **Requirement CLI-04: Chat workflow — request & streaming output**
  - **Description:** The resolved system + user prompts are composed into a single-turn chat completion request. Before streaming, the command prints the effective temperature, system prompt, and user prompt. The model response is then printed to stdout incrementally as chunks arrive. No file is produced.
  - **Acceptance Criteria:**
    - [ ] Given a valid chat invocation, When run, Then stdout first shows the used temperature, system prompt, and user prompt, followed by the streamed response.
    - [ ] Given a streaming response, When chunks arrive, Then each is printed as soon as it is received (no waiting for full completion).
    - [ ] Given completion, When the response ends, Then the program exits zero.
    - [ ] Given a network/model error, When it occurs, Then an error is reported and the program exits non-zero.

- **Requirement CLI-05: OCR workflow — options**
  - **Description:** `ocr` accepts `--source` (mandatory), `--system` (optional), and `--temperature` (optional, real value in `[0.0, 2.0]`).
  - **Acceptance Criteria:**
    - [ ] Given `ocr` with no `--source`, When run, Then it errors (missing mandatory option) and exits non-zero.
    - [ ] Given `--temperature` outside `[0.0, 2.0]` or non-numeric, When run, Then it is rejected with a validation error and non-zero exit.
    - [ ] Given no `--temperature`, When run, Then the value from setting `cli/temperature` is used.
    - [ ] Given `--system` omitted, When run, Then the system prompt is resolved as in CLI-03/CLI-07.

- **Requirement CLI-06: OCR workflow — source handling (file vs folder)**
  - **Description:** `--source` is either a single file or a folder. A file is processed directly. A folder is scanned non-recursively (top level only) for images (`.jpg`, `.jpeg`, `.png`) and PDFs (`.pdf`); the discovered list is printed and the user is asked to confirm before processing. On confirmation, sources are processed sequentially, each streaming its OCR text to stdout as received.
  - **Acceptance Criteria:**
    - [ ] Given `--source` as an existing supported file, When run, Then it is processed directly without a folder confirmation prompt.
    - [ ] Given `--source` as a folder, When run, Then the discovered sources (top level only) are listed and a continue/cancel prompt is shown.
    - [ ] Given a folder scan, When subfolders or non-matching extensions are present, Then they are excluded from the list.
    - [ ] Given the user declines to continue, When the prompt is answered negatively, Then no source is processed and the program exits.
    - [ ] Given the user confirms, When processing runs, Then each source is handled one at a time (sequential) and its OCR text is streamed to stdout as received.
    - [ ] Given an unsupported file extension for `--source` (file case), When run, Then it is rejected with a clear error.

- **Requirement CLI-07: Thinking / reasoning block stripping (both workflows)**
  - **Description:** Streamed model output may contain `<think>...</think>` or `<reasoning>...</reasoning>` blocks. Their inner content must not be printed; while such a block is in progress the command prints `thinking ...`. Content outside these blocks is printed normally.
  - **Acceptance Criteria:**
    - [ ] Given a streamed chunk containing a `think`/`reasoning` block, When processed, Then the block's inner text is suppressed and `thinking ...` is shown while it is in progress.
    - [ ] Given text outside thinking blocks, When streamed, Then it is printed normally.
    - [ ] Given a thinking block split across multiple streamed chunks, When processed, Then stripping still works correctly across chunk boundaries.

- **Requirement CLI-08: PDF processing**
  - **Description:** For a PDF source, each page is rendered to an in-memory image (via QtPDF) and passed to the OCR model page by page. All page results are combined and written to a single markdown file for that PDF.
  - **Acceptance Criteria:**
    - [ ] Given a PDF source, When processed, Then each page is OCR'd sequentially and page images are held in memory only (no per-page image files on disk).
    - [ ] Given a multi-page PDF, When processing completes, Then a single markdown file is produced containing all pages' text.
    - [ ] Given a PDF that fails to render/parse, When it occurs, Then an error is reported for that source and processing continues/handles per error policy (see CLI-11).

- **Requirement CLI-09: OCR markdown output**
  - **Description:** For each OCR source (image or PDF), one markdown file is produced whose base filename matches the source's base filename (`.md` extension).
  - **Acceptance Criteria:**
    - [ ] Given an image `photo.png`, When OCR completes, Then a markdown file `photo.md` is produced.
    - [ ] Given a PDF `doc.pdf`, When OCR completes, Then a single markdown file `doc.md` is produced.
    - [ ] Given the output location rules (see open question Q2), When writing, Then files are placed accordingly.

- **Requirement CLI-10: Settings integration**
  - **Description:** The CLI reads `cli/temperature` and `cli/system-prompt-file` from `SettingsManager` as fallbacks when the corresponding options are not supplied.
  - **Acceptance Criteria:**
    - [ ] Given `cli/temperature` set and no `--temperature`, When run, Then the setting value is used.
    - [ ] Given `cli/system-prompt-file` set and no `--system`, When run, Then that file is used as the system prompt source.

- **Requirement CLI-11: Error handling & exit codes**
  - **Description:** Invalid arguments, missing/unreadable files, unsupported formats, and model/network failures produce clear messages and non-zero exit codes; successful runs exit zero.
  - **Acceptance Criteria:**
    - [ ] Given a missing or unreadable `--source`/prompt file, When run, Then a clear error is printed and exit code is non-zero.
    - [ ] Given a model/network error mid-stream, When it occurs, Then the error is reported and exit code is non-zero.
    - [ ] Given a fully successful run, When it completes, Then exit code is zero.

## 3. Scope & Constraints
- **In-Scope:**
  - `chat` and `ocr` workflow commands with per-workflow option parsing and mutual option isolation.
  - Single-turn chat with streamed stdout output (no file output).
  - OCR for `.jpg`/`.jpeg`/`.png` images and `.pdf` (page-by-page via QtPDF), streamed stdout output plus one markdown file per source.
  - Thinking/reasoning block stripping with `thinking ...` indicator.
  - Fallback to `cli/temperature` and `cli/system-prompt-file` settings.
- **Out-of-Scope:**
  - Multi-turn / conversational chat (explicitly single-turn for now).
  - Recursive folder scanning (top level only).
  - Image/PDF formats beyond `.jpg`, `.jpeg`, `.png`, `.pdf`.
  - Parallel/concurrent processing of multiple sources (sequential only).
  - Any frontend (QtQuick) changes; backend changes limited to what these workflows require.
  - Markdown merging/summarization workflows (separate concern).
- **Technical Constraints / Edge Cases:**
  - **Settings gap:** `cli/temperature` and `cli/system-prompt-file` are defined as keys in `core_schema.cpp` but are **not registered** in `CoreSchema::registerSchemas()`. They must be registered (with sensible defaults, e.g. temperature `0.7` in `[0.0, 2.0]`, empty system-prompt-file) before the CLI can rely on them.
  - **New dependency:** PDF rendering requires `Qt6::Pdf`, which the backend does not currently link (only `Qt6::Core/Network/Gui`). It must be added to the build.
  - **Streaming in a console app:** incremental output requires the Qt event loop to run while the network reply streams; stdout must flush promptly so text appears as received.
  - **Text-vs-file ambiguity:** a `--system`/`--prompt` value that happens to match an existing file path is treated as a file; all other values are literal text.
  - **Chunk-boundary stripping:** thinking tags may be split across streamed chunks, so stripping must be stateful across chunks.
  - **Non-interactive stdin:** the folder confirmation prompt assumes an interactive terminal; behavior when stdin is not a TTY is an open question (Q6).
  - **Case sensitivity:** file-extension matching for the folder scan (e.g. `.JPG` vs `.jpg`) is assumed case-insensitive (confirm in Q3 if needed).

## 4. Open Design Choices (Questions for User)
- **[Business Logic] Q1 — Missing/empty system prompt:** When `--system` is omitted and `cli/system-prompt-file` is unset, empty, or points to a missing file, should the request be sent with **no system message at all**, or should this be a **hard error**? (Applies to both `chat` and `ocr`.)
**User: Hard error, print the error and exit.**

- **[Business Logic] Q2 — OCR output location:** Where should the per-source markdown files be written — (a) alongside each source file, (b) into the `general/output-folder` setting, or (c) the current working directory? What should happen if the target `.md` file already exists (overwrite vs. skip vs. error), given the existing `general/overwrite` setting?
**User: along with the source file [default]. We can add a --output [optional] option to specify and alterantive folder.**

- **[UX/UI] Q3 — Skip folder confirmation:** Should a flag (e.g. `--yes`/`-y`) be added to `ocr` to skip the "continue?" prompt (useful for scripting), and if so, should the default when stdin is non-interactive be to proceed or to abort?
**User: a --yes/-y will be handy, so include it.**

- **[UX/UI] Q4 — Chat header format:** The chat command prints the used temperature, system prompt, and user prompt before streaming. Should the system/user prompts be printed **in full** or **truncated/summarized** (they can be long), and in what layout (labeled lines vs. a block)?
**User: print in full.**

- **[Business Logic] Q5 — PDF markdown structure:** For the single per-PDF markdown file, should each page be separated by a header (e.g. `## Page N`) and/or should the file include a top-level title with the source name?
**User: add a `---` newline `**page N**` newline.**

- **[Technical] Q6 — Non-interactive confirmation:** When `ocr` is given a folder but stdin is not an interactive terminal, what is the desired behavior — abort with an error, or proceed automatically? (Related to Q3.)
**User: proceed automatically.**
---

---
# Refinement Iteration 2
**Status:** LOCKED

## 1. Executive Summary
This iteration folds the user's answers to all six open questions from Iteration 1 into the requirement set, resolving every ambiguity. The CLI's `chat` and `ocr` workflows are now fully specified: a missing/empty system prompt is a hard error; OCR markdown is written alongside each source (with an optional `--output` folder override); `ocr` gains a `--yes`/`-y` flag and proceeds automatically when stdin is non-interactive; the chat header prints prompts in full; and per-PDF markdown uses a `---` / `**page N**` separator between pages.

## 2. Refined Requirements & Acceptance Criteria
*(Supersedes and completes the affected requirements from Iteration 1. Unchanged requirements CLI-01, CLI-02, CLI-04 (streaming), CLI-06 (scan/sequential), CLI-08 (page-by-page), CLI-10, CLI-11 remain as defined there.)*

- **Requirement CLI-03 (revised): Prompt resolution (text vs file) + system-prompt fallback**
  - **Description:** `--system` and `--prompt` each accept literal text or a file path (existing readable file → contents; otherwise literal text). When `--system` is omitted, the system prompt is read from the file named by setting `cli/system-prompt-file`.
  - **Acceptance Criteria:**
    - [ ] Given `--system`/`--prompt` equal to an existing file path, When run, Then the file's contents are used.
    - [ ] Given `--system`/`--prompt` equal to free text (no matching file), When run, Then the text is used verbatim.
    - [ ] Given `--system` omitted and `cli/system-prompt-file` set to an existing readable file, When run, Then that file's contents become the system prompt.
    - [ ] Given `--system` omitted and `cli/system-prompt-file` unset, empty, or pointing to a missing/unreadable file, When run, Then the command prints a clear error and exits non-zero (hard error; no request is sent). *(Resolves Q1 — applies to both `chat` and `ocr`.)*

- **Requirement CLI-04 (revised): Chat header output**
  - **Description:** Before streaming, the chat command prints the effective temperature, the full system prompt, and the full user prompt. Prompts are printed **in full** (not truncated or summarized). The model response is then streamed to stdout incrementally. No file is produced.
  - **Acceptance Criteria:**
    - [ ] Given a valid chat invocation, When run, Then stdout first shows the used temperature, the full system prompt, and the full user prompt, followed by the streamed response. *(Resolves Q4.)*
    - [ ] Given a long system/user prompt, When printed, Then the entire text is shown without truncation.
    - [ ] Given a streaming response, When chunks arrive, Then each is printed as soon as it is received.
    - [ ] Given completion, When the response ends, Then the program exits zero.

- **Requirement CLI-05 (revised): OCR workflow — options**
  - **Description:** `ocr` accepts `--source` (mandatory), `--system` (optional), `--temperature` (optional, real value in `[0.0, 2.0]`), `--output` (optional, alternative output folder), and `--yes`/`-y` (optional flag to skip the folder confirmation prompt).
  - **Acceptance Criteria:**
    - [ ] Given `ocr` with no `--source`, When run, Then it errors (missing mandatory option) and exits non-zero.
    - [ ] Given `--temperature` outside `[0.0, 2.0]` or non-numeric, When run, Then it is rejected with a validation error and non-zero exit.
    - [ ] Given no `--temperature`, When run, Then the value from setting `cli/temperature` is used.
    - [ ] Given `--system` omitted, When run, Then the system prompt is resolved per CLI-03 (including the hard-error case).
    - [ ] Given `--output <folder>`, When run, Then OCR markdown files are written to that folder instead of alongside the source (see CLI-09).
    - [ ] Given `--yes`/`-y`, When a folder source is processed, Then the "continue?" confirmation prompt is skipped and processing proceeds. *(Resolves Q3.)*

- **Requirement CLI-06 (revised): OCR source handling (file vs folder) + confirmation**
  - **Description:** `--source` is a single file or a folder. A file is processed directly. A folder is scanned non-recursively (top level only) for `.jpg`/`.jpeg`/`.png`/`.pdf`; the discovered list is printed and the user is asked to confirm, unless `--yes`/`-y` is set or stdin is non-interactive. On confirmation, sources are processed sequentially, each streaming its OCR text to stdout as received.
  - **Acceptance Criteria:**
    - [ ] Given `--source` as an existing supported file, When run, Then it is processed directly without a confirmation prompt.
    - [ ] Given `--source` as a folder, When run, Then the discovered sources (top level only) are listed and a continue/cancel prompt is shown.
    - [ ] Given a folder scan, When subfolders or non-matching extensions are present, Then they are excluded.
    - [ ] Given the user declines to continue, When the prompt is answered negatively, Then no source is processed and the program exits.
    - [ ] Given the user confirms, When processing runs, Then each source is handled one at a time (sequential) and its OCR text is streamed to stdout as received.
    - [ ] Given `--yes`/`-y` with a folder source, When run, Then the confirmation prompt is skipped and processing proceeds. *(Resolves Q3.)*
    - [ ] Given a folder source and a non-interactive stdin (not a TTY), When run, Then processing proceeds automatically without prompting. *(Resolves Q6.)*
    - [ ] Given an unsupported file extension for `--source` (file case), When run, Then it is rejected with a clear error.

- **Requirement CLI-09 (revised): OCR markdown output — location & overwrite**
  - **Description:** For each OCR source (image or PDF), one markdown file is produced whose base filename matches the source's base filename (`.md` extension). By default it is written **alongside the source file**. An optional `--output <folder>` redirects all output files to that folder. If the target `.md` already exists, behavior follows the existing `general/overwrite` setting.
  - **Acceptance Criteria:**
    - [ ] Given an image `photo.png` and no `--output`, When OCR completes, Then `photo.md` is written in the same directory as `photo.png`.
    - [ ] Given a PDF `doc.pdf` and no `--output`, When OCR completes, Then a single `doc.md` is written in the same directory as `doc.pdf`.
    - [ ] Given `--output <folder>`, When OCR completes, Then each `.md` file is written into `<folder>` (base filename preserved).
    - [ ] Given an existing target `.md` and `general/overwrite` false, When writing, Then the existing file is not clobbered (skip or error per the setting's semantics).
    - [ ] Given an existing target `.md` and `general/overwrite` true, When writing, Then the existing file is overwritten. *(Resolves Q2.)*

- **Requirement CLI-08 (revised): PDF processing — markdown structure**
  - **Description:** For a PDF source, each page is rendered to an in-memory image (via QtPDF) and OCR'd page by page. All page results are combined into a single markdown file. Between pages, the file inserts a `---` line, a newline, a `**page N**` line, and a newline as the separator (N = 1-based page number).
  - **Acceptance Criteria:**
    - [ ] Given a PDF source, When processed, Then each page is OCR'd sequentially and page images are held in memory only (no per-page image files on disk).
    - [ ] Given a multi-page PDF, When processing completes, Then a single markdown file is produced containing all pages' text.
    - [ ] Given a multi-page PDF, When the file is written, Then each page is preceded by a `---` line, a newline, a `**page N**` line, and a newline (N = 1-based page number). *(Resolves Q5.)*
    - [ ] Given a PDF that fails to render/parse, When it occurs, Then an error is reported for that source and handled per CLI-11.

## 3. Scope & Constraints
- **In-Scope:**
  - `chat` and `ocr` workflow commands with per-workflow option parsing and mutual option isolation.
  - `chat`: single-turn, streamed stdout output; header prints temperature + full system prompt + full user prompt; no file output.
  - `ocr`: `.jpg`/`.jpeg`/`.png` images and `.pdf` (page-by-page via QtPDF); streamed stdout output plus one markdown file per source; `--output` folder override; `--yes`/`-y` to skip confirmation; automatic proceed on non-interactive stdin.
  - Thinking/reasoning block stripping with `thinking ...` indicator (both workflows).
  - Fallback to `cli/temperature` and `cli/system-prompt-file` settings; hard error when the effective system prompt cannot be resolved.
  - Per-PDF markdown page separator: `---` / `**page N**`.
- **Out-of-Scope:**
  - Multi-turn / conversational chat (single-turn only).
  - Recursive folder scanning (top level only).
  - Image/PDF formats beyond `.jpg`, `.jpeg`, `.png`, `.pdf`.
  - Parallel/concurrent processing of multiple sources (sequential only).
  - Any frontend (QtQuick) changes; backend changes limited to what these workflows require.
  - Markdown merging/summarization workflows (separate concern).
- **Technical Constraints / Edge Cases:**
  - **Settings gap:** `cli/temperature` and `cli/system-prompt-file` are defined as keys in `core_schema.cpp` but are **not registered** in `CoreSchema::registerSchemas()`. They must be registered (sensible defaults: temperature `0.7` in `[0.0, 2.0]`, empty system-prompt-file) before the CLI can rely on them.
  - **New dependency:** PDF rendering requires `Qt6::Pdf`, which the backend does not currently link (only `Qt6::Core/Network/Gui`). It must be added to the build.
  - **Streaming in a console app:** incremental output requires the Qt event loop to run while the network reply streams; stdout must flush promptly so text appears as received.
  - **Text-vs-file ambiguity:** a `--system`/`--prompt` value that matches an existing file path is treated as a file; all other values are literal text.
  - **Chunk-boundary stripping:** thinking tags may be split across streamed chunks, so stripping must be stateful across chunks.
  - **Non-interactive stdin:** folder confirmation is skipped and processing proceeds automatically when stdin is not a TTY (or when `--yes`/`-y` is set).
  - **Case sensitivity:** file-extension matching for the folder scan is case-insensitive (e.g. `.JPG` matches).
  - **Overwrite policy:** existing-target handling for OCR markdown is governed by the existing `general/overwrite` setting (default false).

## 4. Open Design Choices (Questions for User)
*(None remaining — all six questions from Iteration 1 are resolved.)*
---

**LOCKED** 

