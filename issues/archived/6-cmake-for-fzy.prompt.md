# Implementation Plan: 6-cmake-for-fzy

[Analysis Reference](./6-cmake-for-fzy.plan.analysis.md)

---

## Phase 1 — Enable C Language in Top-Level Project

**Purpose:** Allow CMake to compile the vendored `match.c` source as C99 rather than treating it as C++.

**Implementing requirements from Analysis Section 2.1 (C/C++ Interoperability), Section 2.2 (CMakeLists.txt — Enable C language), and Section 3 (Language Boundary).**

### Step 1.1 — Modify `CMakeLists.txt` to declare the C language

- Open `CMakeLists.txt` at the workspace root.
- Locate the `project(MyQtApp LANGUAGES CXX)` line.
- Change it to `project(MyQtApp LANGUAGES C CXX)`.
- This single modification enables C compilation for the entire project without affecting any existing C++ behavior.

### Step 1.2 — Ensure coverage flags apply to C compilation as well

- In the same `CMakeLists.txt`, locate the coverage flag assignment block (`set(COVERAGE_FLAGS ...)` followed by `set(CMAKE_CXX_FLAGS ...)`).
- Add a parallel assignment for the C compiler flags: `set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COVERAGE_FLAGS}")`.
- This ensures that when `BUILD_COVERAGE` is enabled (via the `debug-coverage` preset), the fzy C compilation unit receives identical instrumentation flags.

**User: we don't need to have code-coverage on any of the thirdparty libraries in `thirdparty`.**

### Phase 1 Exit Criterion

- The root `CMakeLists.txt` declares both `C` and `CXX` languages.
- Coverage flags are assigned to both `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS`.

### Phase 1 Validation Command

```bash
cmake --preset debug
```

The configure step must complete without errors. The CMake output should show that both C and C++ compilers were detected.

---

## Phase 2 — Create the Repository-Local Find Module

**Purpose:** Define `cmake/Findfzy.cmake` to discover, validate, build, and expose the vendored fzy match component as a static library with strict isolation boundaries.

**Implementing requirements from Analysis Section 2.2 (cmake/Findfzy.cmake — Create), Section 3 (Target Ownership and Repeat Discovery, Generated-File Ordering, Public Header Isolation), and Section 4 (Verification Checklist items 1–7).**

### Step 2.1 — Create `cmake/Findfzy.cmake` with guard against duplicate targets

- Create the file `cmake/Findfzy.cmake`.
- At the top of the file, define a guard variable: `if(TARGET fzy)` followed by `return()` and then `endif()`. This ensures that repeated `find_package(fzy REQUIRED)` calls within the same configure pass do not attempt to recreate the target.

### Step 2.2 — Validate required vendored inputs

- Define variables pointing to the expected vendored files relative to `${CMAKE_SOURCE_DIR}`:
  - `FZY_MATCH_C` → `thirdparty/fzy/src/match.c`
  - `FZY_MATCH_H` → `thirdparty/fzy/src/match.h`
  - `FZY_BONUS_H` → `thirdparty/fzy/src/bonus.h`
  - `FZY_CONFIG_DEF_H` → `thirdparty/fzy/src/config.def.h`
- Use `file(READ ...)` or `file(EXISTS ...)` checks to verify each file exists.
- If any file is missing, call `message(FATAL_ERROR "fzy vendored source incomplete: <missing file path>")`.
- This implements the early-fail error handling requirement from Analysis Section 3 (Error Handling).

### Step 2.3 — Prepare build-tree directories

- Define a build-tree include directory variable: `set(FZY_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/thirdparty/fzy/includes")`.
- Create this directory using `file(MAKE_DIRECTORY "${FZY_INCLUDE_DIR}")`.
- This ensures the directory exists before any file copy or generation step runs.

### Step 2.4 — Generate private build-tree `config.h` from `config.def.h`

- Define a private config path: `set(FZY_CONFIG_H "${CMAKE_CURRENT_BINARY_DIR}/thirdparty/fzy/config.h")`.
- Use `configure_file("${FZY_CONFIG_DEF_H}" "${FZY_CONFIG_H}")` to copy the vendored `config.def.h` into the build tree as `config.h`.
- This keeps the generated configuration entirely within the build tree and does not pollute the source directory.

### Step 2.5 — Copy the public header to the build-tree include root

- Create a subdirectory: `file(MAKE_DIRECTORY "${FZY_INCLUDE_DIR}/fzy")`.
- Use `file(COPY "${FZY_MATCH_H}" DESTINATION "${FZY_INCLUDE_DIR}/fzy" RENAME match.h)` to place the public header at `${FZY_INCLUDE_DIR}/fzy/match.h`.
- This establishes the consumer-facing include contract: consumers include `<fzy/match.h>` and receive only this file.

### Step 2.6 — Define the `fzy` static library target

- Create the static library: `add_library(fzy STATIC thirdparty/fzy/src/match.c)`.
- This is the sole source file compiled into the fzy target, satisfying the isolation requirement from Analysis Section 1 (Upstream Dependency Boundary).

### Step 2.7 — Set target-scoped compilation requirements

Apply the following to the `fzy` target using `target_compile_features`, `target_compile_definitions`, `target_compile_options`, and `target_compile_options`:

- **C99 language standard:** `target_compile_features(fzy PUBLIC c_std_99)` or equivalently set `CMAKE_C_STANDARD 99` on the target.
- **Upstream version macro:** `target_compile_definitions(fzy PRIVATE MATCH_VERSION=1.1)` — define the version identifier expected by `match.c`.
- **GNU source extension:** `target_compile_definitions(fzy PRIVATE _GNU_SOURCE)`.
- **Warning policy:** `target_compile_options(fzy PRIVATE -Wall -Wextra -pedantic)`.
- **VLA warning as error:** `target_compile_options(fzy PRIVATE -Werror=vla)`.

These map directly to the upstream Makefile flags documented in Analysis Section 3 (Compilation Flags).

### Step 2.8 — Configure include directories with proper visibility

- **Private requirements** (not exposed to consumers):
  - `${CMAKE_CURRENT_BINARY_DIR}/thirdparty/fzy` — so `config.h` is reachable during the `match.c` compilation.
  - `${CMAKE_SOURCE_DIR}/thirdparty/fzy/src` — so `bonus.h` can include `"../config.h"` via its relative path.
- **Public usage requirement** (exposed to consumers):
  - `${FZY_INCLUDE_DIR}` — the build-tree directory containing only `fzy/match.h`.

Use `target_include_directories(fzy PRIVATE ...)` for the private paths and `target_include_directories(fzy PUBLIC INTERFACE ${FZY_INCLUDE_DIR})` or `target_include_directories(fzy PUBLIC ...)` with `USE_HEADER_DIR` semantics for the public path. The key distinction: consumers must not receive the vendored source directory or the generated config.h as part of their include paths.

### Step 2.9 — Ensure no executable-only link dependencies

- Verify that `target_link_libraries(fzy PRIVATE ...)` is either empty or contains only system libraries required for the static library itself (e.g., `m` for math functions if needed on Linux; on Windows with MSVC this is typically not required).
- Do NOT add pthread or any executable-oriented link dependency.

### Phase 2 Exit Criterion

- `cmake/Findfzy.cmake` exists and defines the `fzy` static library target exactly once.
- All vendored input validation, build-tree directory preparation, config.h generation, public header copy, compilation requirements, and include directory visibility are implemented within this single file.
- No source files in `thirdparty/fzy/` have been modified.

### Phase 2 Validation Command

```bash
cmake --preset debug
```

The configure step must succeed. Verify that:
- The CMake output shows no FATAL_ERROR from the fzy validation checks.
- The build tree at `build/cmake-debug/thirdparty/fzy/includes/fzy/match.h` exists after configuration (or will exist after build).
- The build tree at `build/cmake-debug/thirdparty/fzy/config.h` exists after configuration (or will exist after build).

---

## Phase 3 — Integrate fzy into the Backend Library

**Purpose:** Have `dir2md_backend` discover and link against the fzy static library through the repository-local find module, preserving the backend as the sole transitive dependency boundary.

**Implementing requirements from Analysis Section 2.2 (src/backend/CMakeLists.txt — Modify) and Section 4 (Verification Checklist items 8–9).**

### Step 3.1 — Add `find_package(fzy REQUIRED)` to `src/backend/CMakeLists.txt`

- Open `src/backend/CMakeLists.txt`.
- Insert `find_package(fzy REQUIRED)` before the `target_link_libraries` call.
- This triggers the repository-local `Findfzy.cmake` module during backend configuration.

### Step 3.2 — Link `dir2md_backend` against the `fzy` target

- In the existing `target_link_libraries(dir2md_backend PUBLIC ...)` block, add `fzy` to the list of linked targets.
- The final block should read:
  ```cmake
  target_link_libraries(dir2md_backend PUBLIC
      Qt6::Core
      fzy
  )
  ```
- This makes the fzy static library available transitively to all backend consumers (including tests) through `dir2md_backend`.

### Step 3.3 — Verify no fzy implementation details leak into the backend CMakeLists.txt

- Confirm that `src/backend/CMakeLists.txt` does NOT declare any fzy source paths, generated-header handling, compiler settings, or vendored include paths.
- All such details must remain encapsulated within `cmake/Findfzy.cmake`.

### Phase 3 Exit Criterion

- `dir2md_backend` discovers fzy via `find_package(fzy REQUIRED)` and links against the `fzy` target.
- No fzy implementation details are declared in the backend CMakeLists.txt.
- The backend remains the sole transitive dependency boundary for fzy.

### Phase 3 Validation Command

```bash
cmake --preset debug
cmake --build --preset debug
```

Both configure and build must succeed. Confirm that:
- A static library artifact for `fzy` is produced in the build tree (e.g., `build/cmake-debug/src/backend/.../libfzy.a` or `fzy.lib`).
- The `dir2md_backend` target links against this static library without unresolved symbols.

---

## Phase 4 — Verify Build-Tree Isolation and C/C++ Interoperability

**Purpose:** Confirm that the build tree structure, include directory visibility, and C/C++ linkage are correct per the analysis requirements.

**Implementing requirements from Analysis Section 3 (Public Header Isolation, C/C++ Interoperability) and Section 4 (Verification Checklist items 2–7).**

### Step 4.1 — Verify static library artifact exists and contains only `match.c`

- After a successful build, inspect the build tree for the fzy static library artifact.
- Confirm that no other fzy source files (e.g., executable entry points, terminal UI, choices, options, test sources) were compiled.

### Step 4.2 — Verify generated file locations

- Confirm `build/cmake-debug/thirdparty/fzy/config.h` exists (private build-tree config).
- Confirm `build/cmake-debug/thirdparty/fzy/includes/fzy/match.h` exists (public consumer header).
- Confirm NO `config.h` was created in `thirdparty/fzy/src/` (source tree must remain unmodified).

### Step 4.3 — Verify C99 compilation and flag propagation

- Inspect the build log (e.g., `build/cmake-debug/CMakeFiles/fzy.dir/src/match.c.obj.d` or equivalent Ninja build rules) to confirm:
  - The source is compiled with a C compiler, not C++.
  - Compilation flags include `-std=c99` (or MSVC equivalent), `-Wall`, `-Wextra`, `-pedantic`, `-Werror=vla`, `_GNU_SOURCE`, and the version macro.
  - Debug/Release build type controls optimization level, not hardcoded upstream flags.

### Step 4.4 — Verify no pthread or executable-only link dependency on fzy

- Check the link command for the `fzy` target in the build log.
- Confirm it does NOT include `-lpthread`, `-pthread`, or any other executable-oriented link flag.

### Step 4.5 — Verify C++ consumer can use the public header

- The existing `dir2md_backend` is a C++ target linked to `fzy`. Its successful compilation (verified in Phase 3) already confirms that:
  - A C++ consumer can `#include <fzy/match.h>` via the public include root.
  - The `extern "C"` boundary in `match.h` resolves correctly from C++ to the C static library.
- No additional code changes are needed; this is a build-verification step.

### Phase 4 Exit Criterion

- All verification checklist items from Analysis Section 4 (items 1–7) are confirmed satisfied through manual inspection of the build tree and build logs.

### Phase 4 Validation Command

```bash
# Inspect build tree structure
dir /s build\cmake-debug\thirdparty\fzy

# Inspect Ninja build rules for fzy target
findstr /i "match.c" build\cmake-debug\build.ninja
```

---

## Phase 5 — Validate Existing Tests Remain Passing

**Purpose:** Confirm that the fzy integration does not break any existing backend tests, since they transitively depend on `dir2md_backend` which now links fzy.

**Implementing requirements from Analysis Section 2.2 (test/backend/core/CMakeLists.txt — No immediate change required) and Section 4 (Verification Checklist items 9–10).**

### Step 5.1 — Run existing backend tests through CTest

- Execute the test preset to run all registered tests:
  ```bash
  cmake --build --preset debug --target test
  ```
  Or equivalently:
  ```bash
  ctest --preset debug
  ```

### Step 5.2 — Verify test results

- Confirm all existing backend tests pass without errors or warnings.
- Confirm no new linker errors or symbol resolution failures appear.
- The `backend_core_test` target should link successfully against `dir2md_backend`, which now transitively includes `fzy`.

### Phase 5 Exit Criterion

- All existing backend tests pass.
- No new test targets were added; the fzy integration is purely a build-system change with no behavioral impact on existing code.

### Phase 5 Validation Command

```bash
ctest --preset debug --output-on-failure
```

All tests must report `PASSED`.

---

## Phase 6 — Validate Release and Debug Coverage Presets

**Purpose:** Confirm the fzy integration works correctly across all build presets, including Release and Debug Coverage.

**Implementing requirements from Analysis Section 3 (Configuration Coverage) and Section 4 (Verification Checklist items 11–12).**

### Step 6.1 — Configure and build the Release preset

```bash
cmake --preset release
cmake --build --preset release
```

- Confirm configure succeeds with fzy discovery.
- Confirm build produces a static fzy artifact compiled as C99.
- Confirm no configuration errors or warnings.

### Step 6.2 — Configure and build the Debug Coverage preset

```bash
cmake --preset debug-coverage
cmake --build --preset debug-coverage
```

- Confirm configure succeeds with fzy discovery.
- Confirm build produces a static fzy artifact compiled with coverage instrumentation flags (`-fprofile-instr-generate -fcoverage-mapping`).
- Verify that the C compiler received the coverage flags (check build log for `-fprofile-instr-generate` in the match.c compilation command).

### Step 6.3 — Run tests under Debug Coverage preset

```bash
ctest --preset debug-coverage --output-on-failure
```

- Confirm all existing tests pass under coverage instrumentation.

### Step 6.4 — Verify thirdparty/fzy/LICENSE remains present

- Confirm `thirdparty/fzy/LICENSE` exists and is unmodified.
- No vendored fzy algorithm or public API behavior was changed (verified by the fact that no source files in `thirdparty/fzy/` were edited).

### Phase 6 Exit Criterion

- All three presets (Debug, Release, Debug Coverage) configure and build successfully with fzy integration.
- The fzy target remains C-only, source-isolated, and free of executable-only dependencies across all presets.
- `thirdparty/fzy/LICENSE` is present and unmodified.

### Phase 6 Validation Commands

```bash
# Release preset
cmake --preset release
cmake --build --preset release

# Debug Coverage preset
cmake --preset debug-coverage
cmake --build --preset debug-coverage
ctest --preset debug-coverage --output-on-failure

# Verify LICENSE file
dir thirdparty\fzy\LICENSE
```

---

## Phase 7 — Final Verification and Documentation

**Purpose:** Perform a comprehensive end-to-end verification against the complete analysis checklist and document the integration.

**Implementing requirements from Analysis Section 4 (Verification Checklist items 1–12) in aggregate.**

### Step 7.1 — Execute full verification checklist

Systematically verify each item from Analysis Section 4:

| # | Checklist Item | Status |
|---|---------------|--------|
| 1 | Clean Debug configure with `find_package(fzy REQUIRED)` resolves `Findfzy.cmake` | [ ] |
| 2 | Debug build produces static fzy artifact from `match.c` only | [ ] |
| 3 | Build tree contains private `config.h` and public `fzy/match.h` | [ ] |
| 4 | No `config.h` created in vendored source tree | [ ] |
| 5 | fzy compilation uses C99, version, GNU-source, warnings, pedantic, VLA policy | [ ] |
| 6 | fzy has no pthread or executable-only link dependency | [ ] |
| 7 | C++ consumer compiles `#include <fzy/match.h>` without receiving config.h or src dir | [ ] |
| 8 | `dir2md_backend` discovers and links fzy without implementation details | [ ] |
| 9 | Existing backend tests pass with fzy linked transitively | [ ] |
| 10 | Debug test preset confirms all existing tests still passing | [ ] |
| 11 | Release and Debug Coverage presets work correctly with fzy | [ ] |
| 12 | `thirdparty/fzy/LICENSE` present, no vendored behavior modified | [ ] |

### Step 7.2 — Document the integration boundary for future developers

- Add a brief comment at the top of `cmake/Findfzy.cmake` explaining:
  - The purpose of the module (vendored fzy match component).
  - The public include contract (`#include <fzy/match.h>`).
  - The isolation boundary (only `match.c` is compiled; no executable sources).
  - How to add a new backend consumer (link against `fzy` via `dir2md_backend`).

### Phase 7 Exit Criterion

- All 12 verification checklist items are marked as satisfied.
- `cmake/Findfzy.cmake` includes documentation comments explaining the integration boundary.
- No source files in `thirdparty/fzy/` were modified.

### Phase 7 Validation Command

```bash
# Final clean build across all presets to confirm end-to-end correctness
cmake --preset debug && cmake --build --preset debug && ctest --preset debug --output-on-failure
cmake --preset release && cmake --build --preset release
cmake --preset debug-coverage && cmake --build --preset debug-coverage && ctest --preset debug-coverage --output-on-failure
```

---

## Summary of Files to Create or Modify

| File | Action | Phase |
|------|--------|-------|
| `CMakeLists.txt` (root) | Modify: add C language, add C coverage flags | 1 |
| `cmake/Findfzy.cmake` | Create: find module for vendored fzy | 2 |
| `src/backend/CMakeLists.txt` | Modify: add `find_package(fzy REQUIRED)` and link `fzy` | 3 |

**No other files are modified.** The vendored `thirdparty/fzy/` directory remains untouched. Backend source files (`src/backend/core/`) and test files (`test/backend/core/`) are not changed in this integration phase; they will be updated in a subsequent phase when actual fuzzy-matching behavior is introduced into the backend.
