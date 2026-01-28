# FIT Power Viewer

A C program using raylib that parses `.fit` files and displays power data in an interactive graph. Includes Strava integration for browsing activities.

## Requirements

- raylib
- libcurl
- clang
- pkg-config

### macOS

```bash
brew install raylib
```

### Linux (Fedora)

```bash
sudo dnf install raylib-devel libcurl-devel
```

### Linux (Ubuntu/Debian)

```bash
sudo apt install libraylib-dev libcurl4-openssl-dev
```

### Optional: JetBrains Mono font

The app uses JetBrains Mono if available, otherwise falls back to raylib's default font. Install locations checked:
- Linux: `~/.local/share/fonts/JetBrainsMono-Regular.ttf`
- macOS: `~/Library/Fonts/JetBrainsMono-VariableFont_wght.ttf`

## Build

```bash
make
```

## Usage

```bash
./fitpower
```

The program has two tabs:
- **Local**: Scans `~/Downloads` for `.fit` files (sorted by date, newest first)
- **Strava**: Browse and view activities from your Strava account

### Controls

- **1/2**: Switch between Local and Strava tabs
- **Up/Down** or **J/K**: Navigate file/activity list
- **Enter/Space**: Load selected file
- **Mouse wheel**: Scroll list
- **Click**: Select and load file/activity
- **Page Up/Down**: Jump through list
- **ESC**: Quit

## Strava Setup

1. Create a Strava API application at https://www.strava.com/settings/api
2. Set "Authorization Callback Domain" to `localhost`
3. Create config file `~/.config/fitpower/config`:
   ```json
   {
     "client_id": "YOUR_CLIENT_ID",
     "client_secret": "YOUR_CLIENT_SECRET"
   }
   ```
4. Run fitpower, switch to Strava tab (press `2`), click "Connect to Strava"
5. Authorize in browser, then click "Fetch Activities"

## Features

### Local FIT Files
- Parses FIT binary protocol (definition messages, data messages, compressed timestamps)
- Extracts power data from record messages
- Interactive file browser

### Strava Integration
- OAuth2 authentication (opens browser, captures callback on localhost:8089)
- Fetches activity list from Strava API
- Shows activity details (date, type, distance, power indicator)
- Tokens automatically refreshed and saved

### Power Graph
- Color-coded curve (blue=low, green=medium, red=high intensity)
- Filled area under curve
- Grid lines with power (W) and time labels
- Average power line
- Stats display (min/max/avg, sample count)

## Project Structure

- `main.c` - Raylib GUI, file browser, tab system, graph rendering
- `fit_parser.c/h` - FIT file parser
- `strava_api.c/h` - Strava OAuth and API client
- `Makefile` - Build configuration

## Potential Improvements

- Power graph for Strava activities (fetch streams)
- Zoom/pan on graph
- Heart rate overlay
- Cadence display
- Export to image
- Smoothing/averaging options
- Power zones visualization
