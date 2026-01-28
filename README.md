# FIT Power Viewer

A C program using raylib that parses `.fit` files and displays power data in an interactive graph.

## Requirements

- macOS (tested on Apple Silicon)
- raylib (`brew install raylib`)
- clang

## Build

```bash
make
```

## Usage

```bash
./fitpower
```

The program scans `/Users/jan/Downloads` for `.fit` files and displays them sorted by date (newest first).

### Controls

- **Up/Down** or **J/K**: Navigate file list
- **Enter/Space**: Load selected file
- **Mouse wheel**: Scroll file list
- **Click**: Select and load file
- **Page Up/Down**: Jump through file list
- **ESC**: Quit

## Features

- Parses FIT binary protocol (handles definition messages, data messages, compressed timestamps)
- Extracts power data from record messages
- Interactive file browser
- Power graph with:
  - Color-coded curve (blue=low, green=medium, red=high intensity)
  - Filled area under curve
  - Grid lines with power (W) and time labels
  - Average power line
  - Stats display (min/max/avg, sample count)

## Project Structure

- `main.c` - Raylib GUI, file browser, graph rendering
- `fit_parser.c/h` - FIT file parser
- `Makefile` - Build configuration

## Potential Improvements

- Configurable Downloads path (currently hardcoded)
- Zoom/pan on graph
- Heart rate overlay
- Cadence display
- Export to image
- Smoothing/averaging options
- Power zones visualization
