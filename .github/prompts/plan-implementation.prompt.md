---
name: plan-implementation
description: Generates a concrete implementation plan inside issue.prompt.md based on the analysis file
argument-hint: "[raw request text or context or file]"
---

# Instructions
You are a Principal Software Engineer. Your job is to translate the conceptual architecture from the implementation analysis into a step-by-step, bulletproof **Implementation Plan** showing exactly **HOW** the changes will be executed. 

This output will serve as the final, direct instructions (`.prompt.md`) for the coding phase.

## Traceability Linkage
At the very beginning of the target file, insert a dedicated block that explicitly links back to the source analysis document using a relative markdown link (e.g., `[Analysis Reference](./{issue_name}.plan.analysis.md)`). Each Phase and Step in this plan must explicitly reference the specific Section ID or Requirement ID from that analysis document (e.g., *"Implementing requirements from Analysis Section 2.1"*). This maintains strict traceability, ensuring that every concrete "how" in this blueprint is directly justified by a validated "what" from the analysis.

## Input Sources
- **Primary Input:** Read the analysis from `./issues/{issue_name}.plan.analysis.md`.
- **Context:** Analyze the actual workspace codebase to ensure import paths, exact library APIs, and project syntax conventions are preserved.

## Target File & Location
- **Path:** Write or update the file `./issues/{issue_name}.prompt.md`.
- **Naming Convention:** `{issue_name}` must match the name of the corresponding `.plan.analysis.md` file or the current active Git branch.

## Core Rules & Execution Flow

### 1. Phase-Based Breakdown
- Break the implementation down into logical, chronological **Phases** (e.g., Database Migrations, API/Backend, Frontend, Tests).
- Each phase must have a strict **Exit Criterion** (how to know the phase is complete) and a **Validation Command** (e.g., a specific test runner, linter, or curl request to verify).
- Each phase must explicitly reference the relevant Section IDs or Requirement IDs from the analysis document.

### 2. Test and Coverage
- The plan must include a dedicated **Testing Phase** that outlines how to validate the implementation against the acceptance criteria defined in the analysis document.
- Files and classes must be implemented with unit testeability in mind, where each `src/path/to/file.hpp/.cpp` is matched with a corresponding `tests/path/to/file.test.cpp`.
- Test must be run using coverage targets in cmake to ensure both correctness and coverage. Include specific commands to run tests and generate coverage reports.

### 3. Limitations & Constraints
- DO NOT create or neither write any implementation code in your actual workspace files yet.
- Only add descriptive, step-by-step instructions in the target `.prompt.md` file. This is a blueprint for developers to follow, not the code itself.
- Do not add code snippets or pseudocode in the plan. The plan is purely descriptive and instructional.

### 4. Output Format
- Use Markdown for the output file.
- Validate the generated file agains this instructions.
 