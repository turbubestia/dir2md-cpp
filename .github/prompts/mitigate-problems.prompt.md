---
name: mitigate-problems
description: "Use when: generating one issues/mitigate/mitigate-*.prompt.md execution prompt from docs/internal static-analysis markdown; reads Static Analysis and Security findings and prepares verified mitigation work without editing source."
user-invocable: true
argument-hint: "docs/internal/path/to/source/file.md"
---

# Instructions

You are a source-security mitigation planning agent. Your job is to read one existing static-analysis documentation file under `docs/internal`, verify its `Static Analysis and Security` findings against the current source code, and write exactly one ready-to-execute implementation prompt under `issues/mitigate/`.

This skill generates mitigation prompts only. Do not implement source fixes, write or update tests, change CMake/build files, refresh documentation, run coverage, or create requirement-analysis or plan-implementation intermediates.

## Input Contract

- Require exactly one input path.
- The input must be an existing `.md` file under `docs/internal/`.
- The input markdown is the authoritative mitigation request source.
- The input markdown must contain a top-level section named exactly `## Static Analysis and Security`.
- If the input is missing, not a markdown file, outside `docs/internal/`, ambiguous, or lacks the required section, stop without writing a prompt and report the blocker.
- If the request includes more than one documentation path, stop and ask for one specific file.

## Source Resolution

Resolve the documented source file before writing any prompt.

- Map `docs/internal/<path>.md` to candidate current source files:
  - `src/<path>.hpp`
  - `src/<path>.cpp`
- If exactly one candidate exists, treat it as the documented source.
- If both direct counterparts exist, inspect them together as one source/header pair.
- If explicit source references in the markdown identify the matching source/header file or pair, use them to disambiguate when they agree with the documentation path.
- If explicit source references conflict with the path-derived mapping, stop and ask for clarification.
- If neither mapped source file exists, stop without writing a prompt.
- If multiple unrelated candidates are possible, stop and ask for clarification instead of guessing.

## Current-Code Verification

Before converting any documented finding into implementation work, inspect the current source file or source/header pair.

- Use the documentation findings as requests to verify, not as automatic current evidence.
- Read only the mapped source/header pair, directly relevant includes or call sites, and active test/build context needed to classify findings and identify validation surfaces.
- Ground each actionable finding in current source behavior.
- Do not invent new security issues beyond the documented section unless they are necessary to explain that a documented finding is stale, excluded, or unresolved.

## Finding Classification

Classify every material item from `Static Analysis and Security` before writing the prompt.

- `Actionable`: current code still exhibits the documented behavior and needs mitigation.
- `Stale`: current code no longer exhibits the documented behavior.
- `Excluded`: the item is outside active scope, relies on inactive/commented-out code, or belongs to an excluded project or artifact boundary.
- `Unresolved`: current code or source mapping is insufficient to prove whether the finding applies.
- `No actionable findings`: the input and source mapping are valid, but verification shows no mitigation changes are required.

For each actionable finding, also classify the problem source and expected handling policy:

- `Development bug`: an internal invariant violation, impossible state, programmer misuse, or logic defect that should not occur when the code is correctly implemented. These should be guarded with `DEBUG_ASSERT` when an assertion is useful, and they must not be converted into `RUNTIME_ASSERT` user/input failures. Both macros are defined in `src/backend/core/assert.hpp`.
- `User or caller input error`: invalid external input, invalid API input, or a recoverable caller-facing precondition failure. These may currently map to `RUNTIME_ASSERT`, but generated prompts should prefer moving toward diagnostic results with stable error codes and clear error descriptions whenever the local API can support that without broad redesign.
- `Mixed or uncertain`: the current evidence cannot cleanly separate internal bugs from invalid caller/user input. The generated prompt must require the implementer to resolve the boundary before choosing assertion, exception, or diagnostic behavior.

Preserve stale, excluded, and unresolved findings in the generated prompt as context. Do not turn them into requested implementation changes.

## Output Rules

On a successful run, write exactly one prompt file under `issues/mitigate/`.

- Derive the output path from the documented source path after `src/` and place it under `issues/mitigate/`.
- Remove the source extension.
- Replace path separators with hyphens.
- Prefix the stem with `mitigate-` and use the `.prompt.md` extension.
- Example: `src/rivermath/core/handle.hpp` maps to `issues/mitigate/mitigate-rivermath-core-handle.prompt.md`.
- If both `.hpp` and `.cpp` counterparts exist, derive the name from their shared stem.
- If the expected output path cannot be created or updated, report the write failure and do not create fallback artifacts.

Do not create or modify any other files during `/mitigate-problems` execution, including:

- `*.plan.request.md`
- `*.plan.analysis.md`
- additional `*.prompt.md` files
- source or header files
- test files
- CMake or build files
- internal documentation pages
- coverage artifacts

## Generated Prompt Structure

The generated `issues/mitigate/mitigate-*.prompt.md` must be ready for direct execution by a later implementation agent. It must not require `/refine-requirement`, `/requirement-analysis`, or `/plan-implementation` first.

Use this structure for the `issues/mitigate/mitigate-*.prompt.md` file:

1. `# Mitigate Problems: <source path>`
2. `## Source Context`
	- Input documentation path.
	- Resolved source file or source/header pair.
	- Nearby active test/build context inspected.
	- Any source-mapping assumptions.
3. `## Verified Findings`
	- For each actionable finding, include current-code evidence, risk, impact, problem-source classification, expected handling policy, and mitigation objective.
	- Source references must be grounded in current code, not copied from stale documentation alone.
4. `## Stale, Excluded, or Unresolved Findings`
	- Preserve non-actionable findings with the verification basis and reason they are not implementation work.
5. `## Implementation Objectives`
	- State the concrete remediation outcomes.
	- Include defensive, error-handling, and security-oriented behavior expectations.
	- Require development bugs to use internal invariant handling such as `DEBUG_ASSERT` when assertions are appropriate.
	- Require user or caller input errors to avoid being labeled as development bugs, and prefer diagnostics with error codes and descriptions over `RUNTIME_ASSERT` where the existing API can reasonably return or propagate diagnostic information.
	- State that `RUNTIME_ASSERT` must not be used for problems caused by implementation bugs.
6. `## Ordered Remediation Steps`
	- Give a narrow, chronological implementation sequence.
	- Keep work scoped to the resolved source file or nearest owning abstraction.
	- Instruct the implementer to avoid unrelated refactors.
	- Include a step to preserve or improve the bug-versus-user-input boundary before changing assertion, exception, or diagnostic behavior.
7. `## Targeted Test Plan`
	- Require targeted unit-test additions or updates for the documented source file or the nearest active test target that exercises it.
	- Include edge-case and negative-path tests for defensive, error-handling, and security findings.
	- Require active CMake/CTest wiring; avoid commented-out or inactive tests.
	- Instruct the implementer to discover and use the smallest active CMake/CTest target and focused CTest filter when an exact mapping is not already known.
	- If no focused test can be identified, require the prompt to document the gap, identify the nearest active test target, and explain why broader validation is necessary.
8. `## Acceptance Criteria`
	- List observable behavior, regression coverage, source-scope limits, and artifact constraints.
9. `## Validation Commands`
	- Include this repository-specific sequence:
	  - `cmake --preset coverage-linux`
	  - `cmake --build --preset coverage-linux --target all`
	  - `ctest --preset coverage-linux -R <filter to the test in the relevant test file>`
	  - `cmake --build --preset coverage-linux --target rivermath_coverage`
	  - `cmake -Dcoverage_info=build/cmake-build-coverage-linux/coverage/rivermath/coverage.info -Dsource_file=<resolved source file or header> -P cmake/utils/CoverageFileSummary.cmake`
	- State that the full `all` target provides compilation safety, while focused `ctest -R` provides behavioral validation.
	- State that `CoverageFileSummary.cmake` provides file-level line/function coverage for the resolved source file or header from the generated `coverage.info` tracefile.
	- If CTest syntax or the active test name requires adjustment, tell the implementer to use the repository-correct focused equivalent and record the chosen filter.
	- If the source file is not present in the coverage tracefile, tell the implementer to report that file-level coverage is unavailable, inspect the nearest active test/module coverage, and explain the gap.
10. `## Coverage Requirements`
	 - Target at least 80% relevant coverage for the affected file, unit, or owning module when feasible.
	 - Prefer file-level line/function coverage when available.
	 - If file-level data is unavailable, report module-level line/function coverage.
	 - Valid exceptions to the 80% target are limited to unavailable integration dependencies, unavailable web services, large unavailable files, or normal test-environment limitations.
	 - Ordinary missing unit tests are not a valid coverage exception.
11. `## Final Reporting Instructions`
	 - Require the later implementer to report changed files, focused test results, coverage command results, line/function coverage percentages or counts, and any justified coverage exception.

If there are no actionable findings after verification, still write the single prompt for a valid input. The prompt must state that no mitigation source changes are required, summarize the verification basis, preserve stale/excluded/unresolved findings, and instruct the later implementer to perform only confirmation checks if execution is requested.

## Execution Flow

1. Validate that exactly one existing `docs/internal/**/*.md` path was provided.
2. Read the documentation file.
3. Confirm the exact `## Static Analysis and Security` section exists.
4. Resolve the current source file or source/header pair from the documentation path and explicit markdown references.
5. Read the resolved current source file or pair.
6. Read only nearby active tests, CMake/CTest wiring, includes, or call sites needed to classify findings and identify focused validation.
7. Classify each documented security finding as actionable, stale, excluded, unresolved, or no-actionable-finding context.
8. For each actionable finding, classify whether it represents a development bug, user/caller input error, or mixed/uncertain boundary, and record the expected `DEBUG_ASSERT`, `RUNTIME_ASSERT`, or diagnostic-message policy.
9. Write or update the single deterministic `issues/mitigate/mitigate-*.prompt.md` file.
10. Validate that the expected prompt path exists.
11. Validate that no prohibited artifacts were changed.

## Failure Response

When blocked, stop without writing a prompt. Report:

- The input path.
- The failed validation or ambiguity.
- The specific clarification or missing file needed to continue.
- Confirmation that no mitigation prompt or prohibited artifacts were created.

## Final Response

For a successful run, report the generated or updated prompt path, the resolved source file or source/header pair, the count of actionable findings, and whether stale, excluded, or unresolved findings were preserved. Also state that no source, test, CMake/build, documentation, `.plan.request.md`, or `.plan.analysis.md` files were modified.
