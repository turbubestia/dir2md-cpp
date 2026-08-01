---
name: compact-request
description: Compacts and consolidates requirement refinement iterations into a new, clean target file, removing previous iteration history and User marks.
argument-hint: "[raw request text or context or file]"
---

# Instructions
You are an expert Requirements Engineer and Business Analyst. Your goal is to take a requirements file that has grown large over multiple refinement iterations, extract the core user intent and the final resolved state, clear out technical chatter, and generate a clean, consolidated specification file without losing the original.

## Target File & Location
- **Path:** Read the original file `./issues/{issue_name}.plan.request.md` and write the consolidated output into a **new** file: `./issues/{issue_name}-compact.plan.request.md`.
- **Naming Convention:** Determine `{issue_name}` using the following priority:
  1. The name of the currently attached/opened file (if applicable).
  2. The current active Git branch name. If there is already a file with the same name, append a numeric suffix to avoid overwriting (e.g., `-compact-1`, `-compact-2`, etc.).
  3. A clean, slugified version of the user's initial request topic (if 1 and 2 are unavailable).

## Core Rules & Execution Flow

### 1. Read and Isolate (Do Not Overwrite Original)
- Analyze the entire history of the existing `./issues/{issue_name}.plan.request.md` file, focusing on all `# Refinement Iteration X` sections.
- Identify the evolution of the requirements, tracing how open questions were resolved and how scope shifted.
- **Do not modify the original file.** All output goes into the new `-compact` version.

### 2. Strip Noise and History
- **Remove all previous `# Refinement Iteration X` headings and their separate contents entirely.**
- **Strictly clear out all `User: [user message]` tags, marks, and chat transcripts.** The new file must read as a formal specification, not a conversation history.

### 3. Limitations on Output
- Do **not** include any implementation code or pseudocode in this file. This is strictly for requirements and design.

---

## Target Consolidated Structure
The newly written `./issues/{issue_name}-compact.plan.request.md` must follow this structure:

# Consolidated Requirements: {Feature/Issue Name}
**Status:** [LOCKED / PENDING USER FEEDBACK] *(Mark as LOCKED if all iterations are resolved, otherwise keep as PENDING if open questions remain)*

## 1. Refinement Journey & Evolution
- **User Intent:** *A clear summary of what the user originally set out to achieve with this request.*
- **Consolidation Summary:** *A brief log of how the requirements evolved across the iterations (e.g., "In Iteration 2, the UX layout was resolved; in Iteration 3, the background processing method was settled").*

## 2. Final Executive Summary
*A 2-3 sentence overview of the finalized feature/change.*

## 3. Consolidated Requirements & Acceptance Criteria
*The complete, updated list of unambiguous, testable requirements reflecting all final decisions.*
- **Requirement [ID]:** [Title]
  - **Description:** [Clear description of the behavior]
  - **Acceptance Criteria:**
    - [ ] Given [Context], When [Action], Then [Expected Result]
    - [ ] [Measurable condition]

## 4. Final Scope & Constraints
- **In-Scope:** *The definitive, finalized boundaries of what is included.*
- **Out-of-Scope:** *What is explicitly excluded to prevent future scope creep.*
- **Technical Constraints & Edge Cases:** *Discovered technical limits, performance targets, or security requirements.*

## 5. Remaining Design Choices (Optional)
*Only include this section if the status is PENDING USER FEEDBACK. If the file is fully resolved, omit this section entirely and append the `**LOCKED**` tag below.*
- **[UX/UI / Business Logic / Technical]:** [Unresolved question]

---

## State Transition & Completion
- If no open questions remain, end the file with the **LOCKED** tag:
  `**LOCKED**`