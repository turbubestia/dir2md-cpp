# dir2md-cpp — AI Agent Onboarding Instructions

## Project Overview
Qt6 application for OCR and merging of PDFs and images, split into three parts:
- **backend** (`src/backend/`): QtCore library only — no QWidget or UI elements
- **frontend** (`src/frontend/`): QtQuick UI application
- **cli** (`src/cli/`): Standalone Qt console app consuming the backend

## Build Commands
```bash
cmake --preset debug          # Configure (debug preset)
cmake --build --preset debug  # Build all targets
```

## Build for Coverage
```bash
# Configure + build coverage
cmake --preset debug-coverage
cmake --build --preset debug-coverage

# > Run your application or test (it will produce .profraw files)

# llvm-cov (native LLVM format)
llvm-profdata merge -o default.profdata *.profraw
llvm-cov show build/cmake-debug-coverage/your_app.exe -instr-profile=default.profdata
```

## Code Conventions (STRICT)
- **snake_case** for EVERYTHING: variables, methods, functions, classes, namespaces
- **trailing return types** for all functions
- **No [[nodiscard]]** decorator on any method
- QML file names must use **CamelCase** (e.g., `Main.qml`, not `main.qml`) — this is required by Qt's QML module system

## Architecture Notes
- Backend is a pure logic library meant to be shared between frontend and CLI
- Frontend uses QtQuick with QML modules registered via `qt_add_qml_module()` in CMake
- CLI provides command-line interface to the same backend functionality

## Documentation folders
- There are a few documentation folders:
  - `docs/`: For user-facing documentation, including README, user guides, and tutorials
  - `docs/internal/`: For internal dev design docs, architecture notes, and developer guides
  - `issues/`: For issue tracking, feature requests, and archived discussions
  - `issues/archived/`: For archived issues and discussions that are no longer active and MUST NOT BE USED as reference for current development. These are kept for historical purposes only and may contain outdated information. Please do not reference these files for current development.
  - `issues/mitigation/`: For mitigation plans of static analysis of source code that are being actively worked on. These files contain the probably up-to-date information and can be used as reference for current development. However, be careful to check the timestamps and ensure that the information is still relevant to your current work.

  ## Limitations
  - Do not use documentation files in `issues/archived/` or `docs/benchmarks` for current development. These files are kept for historical purposes only and may contain outdated information. Please do not reference these files for current development.
  