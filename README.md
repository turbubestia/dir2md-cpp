# dir2md-cpp

A desktop application for performing **Optical Character Recognition (OCR)** on PDFs and images, with the ability to merge multiple documents into a single output.

## What It Does

dir2md-cpp helps you process scanned documents and image files by extracting readable text through OCR, then optionally merging several processed documents together. Whether you have a folder full of scanned pages or need to combine multiple PDFs into one cohesive file, this tool automates the workflow.

## Features

- **OCR Processing** — Extract text from scanned PDFs and image files (PNG, JPG, etc.)
- **Document Merging** — Combine multiple processed documents into a single output
- **Desktop Application** — A clean Qt-based graphical interface for easy file selection and configuration
- **Command-Line Interface** — Use the same functionality from the terminal for scripting or batch processing

## Architecture

The project is split into three parts:

| Component | Description |
|-----------|-------------|
| **Backend** | Core logic library handling OCR and merging — shared between the GUI and CLI |
| **Frontend** | Qt Quick desktop application with a graphical user interface |
| **CLI** | Standalone command-line tool for terminal-based usage |

## License

GPL3
