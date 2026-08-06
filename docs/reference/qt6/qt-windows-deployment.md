# Qt 6 Windows Deployment Reference

Source: [Qt for Windows - Deployment | Qt 6.11](https://doc.qt.io/qt-6/windows-deployment.html)

## Overview

Deploying a Qt application on Windows requires distributing the Qt runtime (DLLs), plugins, QML modules, and compiler-specific libraries alongside your executable.

The recommended approach is using `windeployqt`, which automates this process.

---

## The Windows Deployment Tool (`windeployqt`)

`windeployqt` is located in `<qt-installation-folder>/bin/`. It scans an executable for Qt dependencies and copies all required files into a deployment folder.

### Setup

Before running `windeployqt`, set up the Qt build environment:

```bat
<qt-installation-folder>/bin/qtenv2.bat
```

### Basic Usage

```bat
# Standard application
windeployqt.exe .\build\MyApp.exe

# Qt Quick application with QML sources
windeployqt.exe --qmldir .\qml .\build\MyApp.exe

# Debug build
windeployqt.exe --debug .\build\Debug\MyApp.exe

# Dry run (simulate without copying)
windeployqt.exe --dry-run MyApp.exe
```

### Key Options

| Option | Description |
|--------|-------------|
| `--dir <path>` | Custom deployment target directory |
| `--debug` / `--release` | Assume debug or release binaries |
| `--qmldir <directory>` | Scan for QML imports in this directory |
| `--pdb` | Deploy MSVC `.pdb` files |
| `--force` | Overwrite existing files |
| `--no-plugins` | Skip plugin deployment |
| `--no-translations` | Skip translations |
| `--no-compiler-runtime` | Don't deploy compiler runtime DLLs |
| `--verbose <level>` | Verbosity level (0–2) |
| `--json` | Output deployment info as JSON |

### What `windeployqt` Does

1. Copies required Qt DLLs (e.g., `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Quick.dll`)
2. Copies the platform plugin (`platforms/qwindows.dll`)
3. Copies other plugins (`imageformats/`, `styles/`, etc.)
4. Copies QML modules if `--qmldir` is provided
5. Patches `QtCore` with relative paths (if Qt was not built with `-relocatable`)
6. Copies compiler runtime files (unless `--no-compiler-runtime` is specified)

---

## Shared Libraries Approach

Qt is installed as shared libraries by default. The deployment tree looks like:

```
dist/
├── bin/
│   ├── MyApp.exe
│   ├── Qt6Core.dll
│   ├── Qt6Gui.dll
│   ├── Qt6Quick.dll
│   └── ... (other Qt DLLs)
├── platforms/
│   └── qwindows.dll
├── imageformats/
│   ├── qjpeg.dll
│   └── ...
├── styles/
│   └── qwindowsvistastyle.dll
└── qml/
    └── ... (QML modules)
```

### Plugin Structure

Qt plugins must be in subdirectories matching the plugin type:

| Plugin Type | Subdirectory | Example |
|-------------|--------------|---------|
| Platform | `platforms/` | `qwindows.dll` |
| Image Formats | `imageformats/` | `qjpeg.dll`, `qpng.dll` |
| Styles | `styles/` | `qwindowsvistastyle.dll` |
| SQL Drivers | `sqldrivers/` | `qsqlsqlite.dll` |
| Print Support | `printsupport/` | `windowsprintersupport.dll` |

---

## Compiler Runtime

For MSVC builds, the Visual C++ Redistributable is required:

- **Recommended**: Include `vc_redist.x64.exe` in your installer
- **Alternative**: `windeployqt` copies individual runtime DLLs (`vcruntime140.dll`, `msvcp140.dll`) — but these are not licensed for standalone redistribution

---

## Application Dependencies

Use the [Dependencies](https://learn.microsoft.com/en-us/sysinternals/downloads/dependencies) tool (modern replacement for Dependency Walker) to check what libraries your executable links against:

```bat
depends <application executable>
```

---

## Non-Relocatable Builds

If Qt was built with `-relocatable` turned off, plugin search paths are hardcoded. To fix:

1. **Use `qt.conf`** (recommended for multiple executables sharing plugins)
2. **Use `QCoreApplication::addLibraryPath()`** (for a single executable)
3. **Third-party tool** to patch hardcoded paths in `QtCore`

---

## Windows Application Manifest

Qt automatically generates and embeds an application manifest for Windows executables:

- Declares Windows 10/11 compatibility
- Enables long path awareness (>260 characters)
- Sets application version from `PROJECT_VERSION`
- Defines project identifier (`com.yourcompany.<target_name>`)
- Configures execution level (default: `asInvoker`)

Customize via target properties:

```cmake
set_target_properties(MyApp PROPERTIES
    QT_WINDOWS_APP_PROJECT_IDENTIFIER "org.example.myapp"
    QT_WINDOWS_APP_PROJECT_EXECUTION_LEVEL "highestAvailable"
)
```

---

## Static Linking

To build a static Qt application:

1. Build Qt with `configure -static`
2. Link against static Qt libraries
3. Note: plugins cannot be deployed statically; use shared libraries for plugin-based apps

---

## Recommended Workflow for This Project

For our project structure (`dist/bin/`), the deployment steps would be:

```bat
# 1. Build the project
cmake --build --preset release

# 2. Install executables to dist/bin
cmake --install build/cmake-release

# 3. Set up Qt environment
"<QT_DIR>/bin/qtenv2.bat"

# 4. Deploy Qt runtime for the frontend (Qt Quick app)
windeployqt.exe --dir dist/bin --qmldir src/frontend dist/bin/dir2md_frontend.exe

# 5. Deploy Qt runtime for the CLI (Core only)
windeployqt.exe --dir dist/bin dist/bin/dir2md_cli.exe
```

This will place all required DLLs, plugins, and QML modules alongside the executables in `dist/bin/`.
