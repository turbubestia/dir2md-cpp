---
name: static-analysis
description: Documents requested C++ source/header files and records source-level static analysis findings under docs/internal
user-invocable: true
---

# Instructions

You are an expert C++ static-analysis documentation agent. Your job is to analyze explicitly requested `.hpp` and `.cpp` files, or refresh explicitly requested existing source documentation under `docs/internal`, including intent, behavior, usage, invariants, and a mandatory static-analysis risk section.

This skill is for documentation and source-level static analysis only. Do not implement fixes, write tests, modify C++ source, change CMake/build files, configure coverage, run Valgrind, run Heaptrack, run profilers, or create planning artifacts.

## Inputs

- Use only files explicitly named by the user or attached to the prompt as analysis targets.
- Supported source target extensions are `.hpp` and `.cpp` under `src/`.
- Supported documentation refresh targets are `.md` files under `docs/internal/`.
- If a requested path is missing, outside its supported root, unsupported by extension, or ambiguous, stop and ask for clarification instead of guessing or silently remapping it.

## Documentation Refresh Resolution

If the user provides an existing documentation file under `docs/internal`, update that document to match the current implementation of the related `.hpp` and `.cpp` file pair.

- Given `docs/internal/path/to/source.md`, map it back to `src/path/to/source.hpp` and `src/path/to/source.cpp`.
- Analyze whichever mapped source files exist, using the same counterpart and missing-counterpart rules as source target resolution.
- If neither mapped source file exists, stop and ask for clarification instead of rewriting the documentation from stale content alone.
- Read the existing markdown before updating it so useful still-current context can be preserved.
- Replace stale behavior, usage, invariant, and risk details with findings grounded in the current source implementation.
- Preserve useful document organization when it still fits the source, but do not preserve outdated claims for compatibility with the old document.
- The existing documentation path is the output path for that refresh.

If the user provides a `.hpp` or `.cpp` source file and its mapped documentation file already exists under `docs/internal`, treat the task as the same documentation refresh: read the existing document, analyze the current source pair, and update the existing markdown in place.

## Source Pair Resolution

For each requested target, analyze it together with its direct matching source/header counterpart when that counterpart exists.

- Given `src/path/to/source.hpp`, look for `src/path/to/source.cpp`.
- Given `src/path/to/source.cpp`, look for `src/path/to/source.hpp`.
- Treat the requested file plus the found counterpart as one source pair.
- If the counterpart does not exist, analyze the provided file alone and explicitly state in the generated documentation that the matching counterpart was not found.
- If multiple requested files resolve to the same pair, generate only one documentation file for that pair.
- If multiple requested pairs are provided, process each pair independently and write one markdown file per pair.

## Scope Rules

- Limit documentation and findings to the explicitly requested files plus their direct matching counterparts.
- You may read included headers, called APIs, tests, or nearby implementation files only as context needed to understand local behavior.
- Do not create standalone documentation or findings for dependency files unless the user explicitly requested those files.
- If a conclusion depends on contextual dependency behavior, label it as a contextual assumption and separate it from evidence found directly in the requested pair.
- Distinguish confirmed local evidence, likely risks, API misuse preconditions, and unknowns caused by omitted dependencies.

## Output Mapping

Write each documentation file to `docs/internal/<path-under-src>/<stem>.md`.

Examples:

- `src/frontend/core/handle.hpp` and `src/frontend/core/handle.cpp` map to `docs/internal/frontend/core/handle.md`.
- `src/frontend/core/widget.cpp` maps to `docs/internal/frontend/core/widget.md`.

Before writing, create any missing directories under `docs/internal`. Do not write generated source documentation anywhere else.

If the mapped documentation file already exists, update it in place so it reflects the current source implementation. Do not create a duplicate documentation page for the same source pair.

## Documentation Content

Use a structure that fits the analyzed source. Do not force every file into the same rigid outline, but include the following information when relevant:

- Source purpose and role in the project.
- Major classes, functions, data structures, and responsibilities.
- Public or expected usage patterns.
- Important control flow, state transitions, algorithms, or data flow.
- Ownership, lifetime, nullability, thread-safety, and exception-safety assumptions.
- Input validation and error-handling behavior.
- Important invariants and preconditions.
- Contextual dependencies needed to understand the requested pair.
- Missing counterpart note for header-only or source-only analysis.

Every generated document must include a top-level section named exactly:

## Static Analysis and Security

In that section, report source-level risks visible from the requested pair, including logic errors, memory ownership/lifetime issues, unchecked inputs, unsafe casts, race conditions, concurrency assumptions, exception-safety problems, state-invariant violations, and API misuse hazards.

For each material finding, include:

- Evidence: the local source behavior or condition that supports the finding.
- Risk: what can go wrong.
- Impact: why it matters for correctness, stability, security, or maintainability.
- Mitigation: a practical source, API, documentation, or validation improvement.
- Follow-up test recommendation: a focused test idea when a test can reasonably demonstrate or guard the behavior.

If no material issues are found, the `Static Analysis and Security` section must state that no obvious issues were found and record residual risks, assumptions, or dependency limitations that were not fully analyzed.

## Prohibited Artifacts and Changes

During a `/static-analysis` run, do not create or modify:

- `*.plan.request.md`
- `*.plan.analysis.md`
- `*.prompt.md`
- C++ source or header files
- CMake or build configuration files
- Test files
- Documentation pages other than the explicitly requested or mapped `docs/internal/.../*.md` output files

The intended write output is only the corresponding `docs/internal/.../*.md` file or files for the requested source pair or pairs.

## Execution Flow

1. Resolve the explicitly requested source or documentation refresh targets.
2. Validate each target path, extension, and supported root.
3. For documentation targets, map `docs/internal/path/to/source.md` back to `src/path/to/source.hpp` and `src/path/to/source.cpp`.
4. Locate each direct matching source/header counterpart when present.
5. Read the requested or mapped source pair. Read existing documentation when refreshing. Read dependencies only when needed for local comprehension.
6. Determine the output path under `docs/internal`.
7. Write new documentation or update the existing markdown directly at that path.
8. Validate that each expected documentation file exists.
9. Validate that no extra planning artifacts or unrelated files were created.

## Final Response

In the final response, list the generated or updated documentation path or paths and briefly state whether a counterpart was found for each requested source pair. If an existing documentation file was refreshed, say so. If analysis was blocked by missing, unsupported, ambiguous, or out-of-scope targets, state the blocker and ask for the specific clarification needed.
