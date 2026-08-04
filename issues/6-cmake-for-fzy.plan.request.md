# CMake for the Thridparty package fzy

We want to use the module `thirdparty\fzy\src\match.h/.c` from the third party package `thirdparty\fzy`. However this projects compiles to an executable. Here we only need to compile this match module to use it directly in our application. This package is release under MIT license so we are permited to do it. To be compliant we decided to copy the entire repo (which is small enough).

For this task we want to create a cmake package in `cmake/` folder such that the cmake command `find_package(fzy REQUIRED)` would make it available, and compile it as requirement when using it in `src\backend` and provide the includes using `#include <fzy/match>` which will require to compile it and copy the relevant header to `build/{build-path}/thirdparty/fzy/includes`.

From the `thirdparty\fzy\Makefile` we need to extract the required compilation flags and dependencies to build only the `match.c` module.

# Goals

- Compile the `thirdparty\fzy\src\match.h/.c` as a static library
- Make the fzy match library available as a cmake package to use with `find_package(fzy REQUIRED)`
- be able to reference the package with `#include <fzy/match.h>

---

# Refinement Iteration 1
**Status:** PENDING USER FEEDBACK

## 1. Executive Summary

Add a CMake-managed, isolated static library for fzy's matching implementation so the project can consume the match functionality without building the fzy executable or unrelated fzy modules. The package must be discoverable through `find_package(fzy REQUIRED)`, expose the required public header under the `fzy` include namespace, and be linkable by the backend target.

## 2. Refined Requirements & Acceptance Criteria

- **Requirement FZY-CMAKE-001: Build the isolated match library**
	- **Description:** The project shall compile `thirdparty/fzy/src/match.c` and its required fzy sources into a static library dedicated to the match functionality. It shall not compile or link the fzy executable, terminal UI, choices, options, or other unrelated fzy modules.
	- **Acceptance Criteria:**
		- [ ] Given a configured project build, when the fzy target is built, then a static library artifact is produced successfully.
		- [ ] The fzy target contains only the source files required by the match API and its direct dependencies.
		- [ ] The fzy target does not require the fzy executable entry point or executable-only source modules.

- **Requirement FZY-CMAKE-002: Preserve required upstream compilation behavior**
	- **Description:** The CMake target shall carry the compilation settings required by the upstream fzy Makefile for `match.c`, including C99 compatibility, the required preprocessor definitions, the fzy dependency include directory, and warnings appropriate to the upstream module. Executable-only link dependencies shall not be added unless the isolated source actually requires them.
	- **Acceptance Criteria:**
		- [ ] Given a clean configure and build, when `match.c` is compiled with the project toolchain, then it compiles as C99-compatible C code without relying on compiler-specific C++ compilation.
		- [ ] The required upstream definitions and include paths are available to the fzy compilation unit.
		- [ ] The static library does not introduce an unnecessary pthread or other executable-only link requirement.
		- [ ] The upstream fzy version metadata used by the match module is represented consistently with the copied source.

- **Requirement FZY-CMAKE-003: Provide a discoverable CMake package**
	- **Description:** The repository shall provide a CMake package definition in `cmake/` that makes fzy available through the standard `find_package(fzy REQUIRED)` call. The package shall expose a stable target for consumers to link against and shall fail configuration clearly when the package cannot be made available.
	- **Acceptance Criteria:**
		- [ ] Given the repository's configured module/package search path, when a consumer calls `find_package(fzy REQUIRED)`, then configuration succeeds.
		- [ ] Given a consumer target, when it links the exported fzy target, then the consumer receives the static library and its required usage requirements.
		- [ ] The package exposes no dependency on building or installing the fzy executable.

- **Requirement FZY-CMAKE-004: Expose the public match header**
	- **Description:** The package shall expose fzy's public match header through an include directory whose layout supports the canonical namespaced include `fzy/match.h`. The generated/build-tree include location shall be available under the configured build directory in the requested `thirdparty/fzy/includes` area, or an equivalent package include location agreed in the open design choices.
	- **Acceptance Criteria:**
		- [ ] Given a C or C++ consumer that links the fzy target, when it includes `fzy/match.h`, then the source compiles without including files through the third-party source-relative path.
		- [ ] The exposed header remains compatible with C++ consumers through its existing C linkage boundary.
		- [ ] The required public header is present at the package's advertised include location after configuration/build as applicable.

- **Requirement FZY-CMAKE-005: Integrate fzy with the backend**
	- **Description:** The backend shall declare and consume fzy through the CMake package rather than duplicating fzy source compilation details. Backend code shall be able to use the match API through the package's public target and header include path.
	- **Acceptance Criteria:**
		- [ ] Given a normal project configure, when the backend target is built, then fzy is discovered and built automatically as a backend requirement.
		- [ ] Given the backend source includes `fzy/match.h` and uses the public match API, when the backend is compiled and linked, then the build succeeds.
		- [ ] The backend does not need to know fzy's source directory, private source files, or upstream Makefile details.

- **Requirement FZY-CMAKE-006: Keep the copied third-party source compliant**
	- **Description:** The repository shall continue to contain the copied fzy source needed for this integration, together with its applicable MIT license and attribution information. The CMake integration shall not modify the third-party implementation behavior.
	- **Acceptance Criteria:**
		- [ ] Given the repository contents, when the fzy library is built, then the implementation is sourced from the vendored fzy copy.
		- [ ] The applicable fzy license text remains available in the repository.
		- [ ] The integration does not alter the public behavior of the fzy match functions.

- **Requirement FZY-CMAKE-007: Validate the integration**
	- **Description:** The change shall include build or test coverage sufficient to verify package discovery, header visibility, static-library linkage, and basic match functionality through the project build system.
	- **Acceptance Criteria:**
		- [ ] Given the debug CMake preset, when the project is configured and built, then all existing targets and the new fzy target build successfully.
		- [ ] Given the project test invocation, when the fzy integration is exercised, then at least one test verifies a successful match API call through the public include and linked target.
		- [ ] Given a clean build directory, when configuration and compilation are repeated, then the result does not depend on manually generated files in the source tree.

## 3. Scope & Constraints

- **In-Scope:**
	- A CMake package/module in `cmake/` for the vendored fzy match component.
	- A static library containing the isolated match implementation and direct source dependencies.
	- Public target usage requirements, include layout, and backend linkage.
	- CMake build/test validation for the integration.
	- Retention of the vendored fzy license and source attribution.
- **Out-of-Scope:**
    - Building, installing, or invoking the fzy executable.
    - Porting or refactoring fzy's matching algorithm.
    - Integrating fzy's terminal UI, choices, options, or command-line behavior.
    - Replacing the project's existing package manager or changing unrelated CMake targets.
- **Technical Constraints / Edge Cases:**
    - The source file is C and must remain compilable when consumed by the C++ backend.
    - The public header currently uses the filename `match.h`; include naming must be made consistent across documentation, CMake, and source code.
    - The match implementation includes fzy headers and a generated/configuration header, so the package must provide those inputs without requiring a source-tree-generated file.
    - Build-tree-generated headers must be available before compiling the static library and consumers.
    - Debug, release, and coverage configurations must not accidentally inherit executable-only flags or dependencies.
    - CMake target names and include directories should avoid leaking private third-party paths to unrelated consumers.

## 4. Open Design Choices (Questions for User)

- **[Technical]:** Should the package be implemented as a CMake module (`Findfzy.cmake`) found through the repository's `CMAKE_MODULE_PATH`, a config package (`fzyConfig.cmake`), or should it support both discovery forms?
**User:  CMake module (`Findfzy.cmake`)**

- **[Technical]:** What canonical consumer target name should be required, for example `fzy::match` or `fzy`?
**User just fzy**

- **[Technical]:** Should the public include contract support only `#include <fzy/match.h>`, or must it also support the extensionless form `#include <fzy/match>` mentioned in the original request?
**User: `#include <fzy/match.h>`**

- **[Technical]:** Must the header be physically copied into `build/{build-path}/thirdparty/fzy/includes`, or is an equivalent generated/build-tree include directory acceptable when it provides the same `fzy/match.h` include contract?
**User: the source foler also have the .c and other files, we don't want the app be able to reference them. beaside the Makefile create the config.h from the config.def.h so it would work either. It have to be copied and the config.h be generated in the same way.**

- **[Technical]:** Should optimization/debug options from the upstream Makefile (`-O3` and `-g`) be reproduced exactly, or should CMake build types control optimization and debug information while retaining only the required language, definitions, include paths, and warnings?
**User: we will not debug the library, so the basic distintion between debug and release is fine.**

- **[Technical]:** Is a standalone fzy integration test required, or is exercising the API through an existing backend test sufficient?
**User: No, we don't need test for the library. We will carry the test to were we will use it in backend.**

ADD NEXT ITERATION BELOW THIS LINE

---

# Refinement Iteration 2
**Status:** LOCKED

## 1. Executive Summary

Provide a repository-local CMake module that discovers and builds the vendored fzy matching implementation as a static library target named `fzy`. The integration shall expose only the namespaced public match header and a build-generated configuration header to consumers, while keeping fzy's source-relative implementation files private and avoiding executable-only dependencies.

## 2. Refined Requirements & Acceptance Criteria

- **Requirement FZY-CMAKE-008: Define the fzy package module**
	- **Description:** The repository shall provide a `Findfzy.cmake` module in the repository's `cmake/` directory. The top-level project shall make that module discoverable through `CMAKE_MODULE_PATH`, and `find_package(fzy REQUIRED)` shall create or expose the canonical target `fzy` exactly once.
	- **Acceptance Criteria:**
		- [ ] Given a normal project configure, when `find_package(fzy REQUIRED)` is evaluated, then configuration succeeds using the repository-local module.
		- [ ] The package exposes the target name `fzy` requested by the consumer and does not require a separately named namespace target.
		- [ ] Repeated package discovery does not create duplicate targets or duplicate build rules.
		- [ ] Configuration fails with a clear CMake error if the vendored fzy source or required input files are unavailable.

- **Requirement FZY-CMAKE-009: Build only the match implementation**
	- **Description:** The `fzy` target shall be a static library built from the vendored match implementation and only the direct files required for that implementation. It shall not include the fzy executable entry point, terminal UI, choices, options, or test sources.
	- **Acceptance Criteria:**
		- [ ] Given the debug, release, or coverage configuration, when the `fzy` target is built, then a static library is produced from the match implementation without compiling the fzy executable.
		- [ ] The target has no link dependency on pthread or other executable-only libraries unless a direct match implementation dependency is demonstrated by the selected source.
		- [ ] Debug and release behavior follows the project's CMake build type distinction; the upstream executable optimization and debug flags are not forced onto the library.
		- [ ] The C source is compiled as C99-compatible C with the required upstream version definition, GNU-source definition, warnings, pedantic checks, and VLA warning policy represented in the target's compile requirements.

- **Requirement FZY-CMAKE-010: Generate an isolated build-tree configuration and public header area**
	- **Description:** During configuration or build preparation, CMake shall create a build-tree directory at the configured build path under `thirdparty/fzy/includes`. It shall generate `config.h` from the vendored `src/config.def.h` with equivalent content and copy `src/match.h` into the namespaced public location `thirdparty/fzy/includes/fzy/match.h`. The library and its consumers shall use these generated/copied files rather than requiring generated files in the source tree.
	- **Acceptance Criteria:**
		- [ ] Given a clean source tree, when the project is configured and built, then no source-tree `config.h` is required or created for the fzy integration.
		- [ ] After the required generation/copy step, the build tree contains `thirdparty/fzy/includes/config.h` and `thirdparty/fzy/includes/fzy/match.h`.
		- [ ] The generated configuration reflects the vendored `config.def.h` content and is available before compilation of the fzy library.
		- [ ] The copied public header is available through the include contract `#include <fzy/match.h>`.
		- [ ] Consumer include paths do not expose the vendored fzy `src` directory or unrelated fzy source and dependency directories as public usage requirements.

- **Requirement FZY-CMAKE-011: Publish correct target usage requirements**
	- **Description:** The `fzy` target shall publish the build-tree public include directory needed by C and C++ consumers and shall keep implementation-only include paths private. Its public header shall remain usable from C++ through its existing C linkage boundary.
	- **Acceptance Criteria:**
		- [ ] Given a C or C++ target linked to `fzy`, when the target includes `fzy/match.h`, then compilation succeeds without source-relative fzy includes.
		- [ ] Given a C++ backend target linked to `fzy`, when it calls the match API, then compilation and static linkage succeed.
		- [ ] The `fzy` target does not expose unrelated fzy headers as part of its public include contract.
		- [ ] The target's include and generation dependencies are ordered so consumers cannot compile before the generated/copied headers exist.

- **Requirement FZY-CMAKE-012: Integrate the backend through the package**
	- **Description:** The backend CMake target shall discover fzy with `find_package(fzy REQUIRED)` and link the canonical `fzy` target. Backend CMake configuration shall not duplicate vendored source lists, generated-header commands, upstream Makefile flags, or private fzy include paths.
	- **Acceptance Criteria:**
		- [ ] Given a normal project configure and build, when the backend is built, then fzy is discovered and built automatically as a backend requirement.
		- [ ] Backend source code can include `fzy/match.h` and use the public match API through the linked target.
		- [ ] Removing or changing fzy's private source layout requires changes only in the package integration, not in backend build declarations.

- **Requirement FZY-CMAKE-013: Preserve vendored source and attribution**
	- **Description:** The integration shall compile the copied fzy source present in `thirdparty/fzy` and shall retain its applicable MIT license and attribution files. The CMake integration shall not modify the algorithm or public behavior of the vendored match implementation.
	- **Acceptance Criteria:**
		- [ ] Given the repository contents, when the fzy library is built, then its implementation originates from the vendored fzy copy.
		- [ ] The fzy MIT license remains present and discoverable in the repository.
		- [ ] The upstream version metadata used for the match compilation is kept consistent with the copied source.

- **Requirement FZY-CMAKE-014: Validate through the consuming backend**
	- **Description:** Validation shall cover package discovery, generated header availability, public include visibility, static linkage, and basic match API use through the backend or its existing tests. A separate standalone fzy library test is not required.
	- **Acceptance Criteria:**
		- [ ] Given a clean build directory and the debug preset, when CMake configures and builds the project, then all existing targets and the fzy static library build successfully.
		- [ ] Given the backend test coverage added at the point of fzy use, when the project tests run, then at least one test exercises a successful match API call through `fzy/match.h` and the linked `fzy` target.
		- [ ] Given a clean build directory, when configuration and compilation are repeated, then the result does not depend on manually generated files in the source tree.
		- [ ] Given release and coverage configurations, when the relevant targets are built, then the fzy integration remains free of executable-only source and link dependencies.

## 3. Scope & Constraints

- **In-Scope:**
	- A repository-local `Findfzy.cmake` module and top-level module-path registration.
	- A static `fzy` target for the isolated match implementation.
	- Build-tree generation of `config.h` from `config.def.h` and copying of `match.h` into the `fzy/match.h` namespace.
	- Public/private include visibility and backend linkage through the package target.
	- Validation through the backend's eventual fzy usage and existing project build/test configurations.
	- Retention of the vendored fzy source, version metadata, MIT license, and attribution.
- **Out-of-Scope:**
	- Building, installing, or invoking the fzy executable.
	- Compiling fzy terminal UI, choices, options, test, or unrelated modules.
	- Adding a standalone fzy test executable or porting/refactoring the matching algorithm.
	- Reproducing upstream `-O3`, `-g`, pthread, or executable-specific build behavior independently of the project's CMake configuration.
	- Exposing the full fzy source tree or its unrelated headers to application consumers.
- **Technical Constraints / Edge Cases:**
	- The implementation is C and must remain C99-compatible while the consuming backend is C++.
	- The generated and copied headers must be available before both the static library and consuming targets compile.
	- The integration must work from a clean build tree and must not rely on source-tree-generated files.
	- Include directory layout must support exactly `fzy/match.h`; the extensionless `fzy/match` form is not part of the contract.
	- The package target must avoid leaking private vendored paths and must not add executable-only link requirements.
	- Debug, release, and coverage configurations must preserve the project's normal configuration semantics.

**LOCKED**
---