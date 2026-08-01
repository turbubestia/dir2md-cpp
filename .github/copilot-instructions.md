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
