# Implementation Analysis: 6-cmake-for-fzy

## 1. Architectural Impact & Data Flow

- **Affected Subsystems:** Top-level CMake configuration, repository-local CMake package discovery, the vendored fzy dependency boundary, the backend static library, and backend test coverage when a backend feature begins using fuzzy matching.
- **Data Flow Changes:** Top-level CMake registers `cmake/` as a module search location and enables C compilation -> `src/backend/CMakeLists.txt` discovers `fzy` through `find_package(fzy REQUIRED)` -> `Findfzy.cmake` validates the vendored inputs, prepares the build-tree header area, and creates the static `fzy` target from `thirdparty/fzy/src/match.c` -> `dir2md_backend` links `fzy` and receives only the public build-tree include directory -> a backend source can include `fzy/match.h` and call the fzy C API -> backend consumers and its tests receive the static library transitively through `dir2md_backend`.
- **Isolation Boundary:** `thirdparty/fzy/src` remains an implementation-only include location for compiling `match.c`. The consumer-facing include root is `build/<preset>/thirdparty/fzy/includes`, containing only `fzy/match.h`. The generated `config.h` is a separate private build-tree input for the fzy compilation and must not be exposed through consumer usage requirements; neither the vendored source directory, `bonus.h`, nor any executable-oriented fzy modules are public.
- **Language Boundary:** The root project currently declares only `CXX`, while the vendored match implementation is C99. Project configuration must recognize both C and C++ so the fzy target compiles `match.c` as C rather than treating it as C++.
- **Upstream Dependency Boundary:** `match.c` directly includes `match.h`, `bonus.h`, and `../config.h`. `bonus.h` only depends on `config.h`; therefore the fzy static library needs the isolated generated configuration and private access to the vendored source directory, but no other fzy implementation source, executable entry point, terminal UI, choices, options, test source, or pthread link dependency.

## 2. Component & File Impact Map

### `CMakeLists.txt`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Enable the C language in addition to the existing C++ project language so CMake can compile the vendored `match.c` source using the selected C compiler.
  - [ ] Retain `cmake/` in `CMAKE_MODULE_PATH` as the discovery location for repository-local modules; this is the location `find_package(fzy REQUIRED)` will use.
  - [ ] Ensure existing coverage configuration applies consistently to the C compilation path as well as the current C++ path, without adding fzy-specific executable options globally.
- **Logic Modifications Required:**
  - [ ] Keep configuration ordering such that the module path is established before the backend evaluates `find_package(fzy REQUIRED)`.

### `cmake/Findfzy.cmake`
- **Type of Change:** Create
- **Structural Changes:**
  - [ ] Define the repository-local CMake find module for the vendored fzy match component.
  - [ ] Validate the required vendored inputs: `thirdparty/fzy/src/match.c`, `match.h`, `bonus.h`, and `config.def.h`.
  - [ ] Create or expose the canonical static library target named `fzy` exactly once, allowing repeated `find_package(fzy REQUIRED)` calls without duplicate targets or build rules.
  - [ ] Associate the target only with `thirdparty/fzy/src/match.c`; do not add the upstream executable, terminal, choices, options, test, or dependency sources.
  - [ ] Represent the match module's required C99 language level, upstream version metadata, GNU-source definition, warning policy, pedantic checking, and VLA warning policy as target-scoped compilation requirements.
  - [ ] Use the project build configuration for debug versus release behavior rather than forcing upstream `-g` or `-O3` flags.
- **Logic Modifications Required:**
  - [ ] Prepare `thirdparty/fzy/includes` beneath the active build tree.
  - [ ] Generate a private build-tree `config.h` from the vendored `src/config.def.h` without creating a source-tree `config.h` or placing the configuration header in the consumer include root.
  - [ ] Copy the vendored public `match.h` to `thirdparty/fzy/includes/fzy/match.h`.
  - [ ] Make the generated configuration available before compiling `match.c` and the copied public header available before consumers compile.
  - [ ] Publish only the public include root containing `fzy/match.h` as the target's public include usage requirement; retain the generated configuration and vendored source access as private compile requirements.
  - [ ] Report a clear configuration failure when required vendored inputs are missing.

### `src/backend/CMakeLists.txt`
- **Type of Change:** Modify
- **Structural Changes:**
  - [ ] Discover fzy using `find_package(fzy REQUIRED)`.
  - [ ] Link `dir2md_backend` against the canonical `fzy` target.
- **Logic Modifications Required:**
  - [ ] Keep all fzy source lists, generated-header handling, compiler settings, and private vendored include paths inside the package module.
  - [ ] Preserve the backend target as the sole transitive dependency boundary for backend consumers and tests.

### `src/backend/core/CMakeLists.txt`
- **Type of Change:** Modify when the first backend feature consumes fuzzy matching
- **Structural Changes:**
  - [ ] Add the backend source file that introduces the actual fuzzy-matching behavior to the existing `dir2md_backend` source list.
- **Logic Modifications Required:**
  - [ ] The consuming backend source uses only the public `fzy/match.h` include contract and public match API; it must not include vendored paths or private fzy headers.

### `test/backend/core/CMakeLists.txt`
- **Type of Change:** No immediate change required; modify when backend fuzzy-match behavior is introduced
- **Structural Changes:**
  - [ ] Retain linkage to `dir2md_backend`, which will transitively provide the `fzy` static library to backend tests.
- **Logic Modifications Required:**
  - [ ] Do not add a standalone fzy test target. Test the successful match API call through the backend feature at the point that feature is added.

### `test/backend/core/setting_manager_test.cpp`
- **Type of Change:** No immediate change required; modify or supplement when backend fuzzy-match behavior is introduced
- **Structural Changes:**
  - [ ] Add focused backend-level coverage in the relevant test file when a backend feature first exposes fzy behavior.
- **Logic Modifications Required:**
  - [ ] Verify a successful match operation through the backend's public behavior, indirectly confirming public header visibility and static linkage.

### `thirdparty/fzy/src/config.def.h`
- **Type of Change:** No source change required
- **Structural Changes:**
  - [ ] Remain the vendored source of truth for the private build-tree `config.h`.
- **Logic Modifications Required:**
  - [ ] Its copied/generated content must be available only to the `match.c` compilation unit through a private build-tree location.

### `thirdparty/fzy/src/match.h`
- **Type of Change:** No source change required
- **Structural Changes:**
  - [ ] Remain the vendored public API source; its build-tree copy becomes the sole supported consumer header at `fzy/match.h`.
- **Logic Modifications Required:**
  - [ ] Preserve its existing C++ linkage boundary and public match API without altering vendored algorithm behavior.

### `thirdparty/fzy/src/match.c` and `thirdparty/fzy/src/bonus.h`
- **Type of Change:** No source change required
- **Structural Changes:**
  - [ ] Remain private vendored implementation inputs to the `fzy` static library.
- **Logic Modifications Required:**
  - [ ] Compile `match.c` as C99 with access to the build-tree configuration header and private vendored implementation headers.

### `thirdparty/fzy/LICENSE`
- **Type of Change:** No change required
- **Structural Changes:**
  - [ ] Remain in place as the discoverable MIT license for the vendored fzy source.
- **Logic Modifications Required:**
  - [ ] The integration must continue to build from this copied source without changing its public matching behavior.

## 3. Boundary & Edge Case Analysis

- **Error Handling:** CMake configuration must fail early and identify the missing vendored input when `match.c`, `match.h`, `bonus.h`, or `config.def.h` is unavailable. Required package discovery must not silently create a partial or interface-only fzy target.
- **Target Ownership and Repeat Discovery:** The find module owns the `fzy` target and generated include area. It must recognize an already-created target so multiple consumers can call `find_package(fzy REQUIRED)` within one configure pass without duplicate target errors.
- **Generated-File Ordering:** The private `config.h` must exist before compiling `match.c`; `fzy/match.h` must exist before consuming targets compile. A clean configure/build must not depend on files manually generated in `thirdparty/fzy`.
- **Public Header Isolation:** Consumers receive an include root exposing only `fzy/match.h`. The generated `config.h` is not a public usage requirement. Publishing `thirdparty/fzy/src` would make `bonus.h`, implementation-relative configuration paths, and unrelated source headers reachable, violating the required public boundary.
- **C/C++ Interoperability:** `match.c` is compiled as C, while `dir2md_backend` and tests are C++. The public header's existing `extern "C"` boundary must remain intact, and static linkage must resolve from the C++ backend without compiling fzy as C++.
- **Compilation Flags:** Upstream `VERSION=1.1`, `_GNU_SOURCE`, C99, `-Wall`, `-Wextra`, `-pedantic`, and VLA error policy apply to the match compilation unit. Upstream dependency generation, `-g`, `-O3`, and executable pthread linkage are not direct requirements of the isolated static library and must not be transferred indiscriminately.
- **Configuration Coverage:** The project has Debug, Release, and Debug Coverage presets. The C portion of coverage instrumentation must be validated because the current top-level coverage flags are assigned only to `CMAKE_CXX_FLAGS`.
- **Security & Permissions:** No runtime permissions, network access, persistence, or security boundary changes are introduced. The main boundary is build-time encapsulation of third-party implementation files.
- **Performance / Scale Impact:** The integration adds one small static library compiled once per build configuration. The matching algorithm and its fixed `MATCH_MAX_LEN` behavior remain unchanged; no additional runtime work is introduced until a backend feature uses the API.

## 4. Verification Checklist

- [ ] Configure a clean Debug build with the `debug` preset and confirm `find_package(fzy REQUIRED)` resolves the repository-local `Findfzy.cmake` module.
- [ ] Build the Debug preset and confirm a static `fzy` artifact is produced from `thirdparty/fzy/src/match.c` without compiling fzy executable, terminal, choices, options, or test sources.
- [ ] Confirm the Debug build tree contains the private generated `config.h` outside the public include root and `thirdparty/fzy/includes/fzy/match.h`.
- [ ] Confirm no `config.h` is created in the vendored fzy source tree during a clean configure/build.
- [ ] Confirm the fzy compilation uses C99 and the required upstream version, GNU-source, warning, pedantic, and VLA policies, while Debug/Release options remain controlled by the project's build type.
- [ ] Confirm fzy has no pthread or other executable-only link dependency.
- [ ] Confirm a C++ consumer linked to `fzy` can compile `#include <fzy/match.h>` and call the public match API without receiving the generated `config.h` or vendored `src` directory as public include paths.
- [ ] Confirm `dir2md_backend` discovers and links `fzy` without declaring fzy source paths, generation steps, or implementation compilation settings.
- [ ] When a backend feature starts using fzy, run the corresponding backend test and verify at least one successful match result through the backend behavior.
- [ ] Run the Debug test preset to confirm existing backend tests remain linked and passing.
- [ ] Configure/build the Release and Debug Coverage presets and confirm the isolated fzy target remains C-only, source-isolated, and free of executable-only dependencies.
- [ ] Confirm `thirdparty/fzy/LICENSE` remains present and that no vendored fzy algorithm or public API behavior was modified.
