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

### Adding Activities

Drop `.fit` files into the inbox folder (or download from Strava):
- **Linux**: `~/.local/share/fitpower/inbox/`
- **macOS**: `~/Library/Application Support/fitpower/inbox/`

On startup, files are automatically organized into `activity/YYYY/MM/` based on the activity timestamp extracted from the FIT file.

### Tabs

- **Local**: Browse your activities organized by year and month
- **Strava**: Browse and view activities from your Strava account

### Controls

- **1/2**: Switch between Local and Strava tabs
- **S/G/M**: Switch between Summary, Graph, and Map view
- **Up/Down** or **J/K**: Navigate tree/list
- **Left/Right**: Collapse/expand year or month nodes
- **Enter/Space**: Load selected file or toggle expand/collapse
- **Mouse wheel**: Scroll list
- **Click**: Select and load file, or toggle expand/collapse
- **Page Up/Down**: Jump through list
- **ESC**: Quit

## Data Directory

Activities are stored in a platform-specific location:

**Linux**: `~/.local/share/fitpower/`
```
~/.local/share/fitpower/
├── inbox/              # Drop new .fit files here
├── activity/
│   ├── 2024/
│   │   ├── 07/         # July
│   │   ├── 08/         # August
│   │   └── 12/         # December
│   └── 2025/
│       ├── 01/         # January
│       └── ...
└── tiles/              # Cached OpenStreetMap tiles
    └── {z}/{x}/{y}.png
```

**macOS**: `~/Library/Application Support/fitpower/`

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

### Local Activities
- Supports both FIT files and JSON files (downloaded from Strava)
- Automatic inbox processing and organization by date
- Year/month tree browser with expand/collapse
- Parses FIT binary protocol (definition messages, data messages, compressed timestamps)
- Extracts power, GPS coordinates, heart rate, cadence, and activity timestamps
- Overlapping activities (within 10 minutes) are automatically grouped for comparison
- Groups have editable titles/descriptions stored in separate sidecar files

### Strava Integration
- OAuth2 authentication (opens browser, captures callback on localhost:8089)
- Fetches activity list from Strava API
- Shows activity details (date, type, distance, power indicator)
- Download activities as JSON with full stream data (power, GPS, heartrate, cadence)
- Downloaded activities appear in the Local tab and can be viewed like FIT files
- Tokens automatically refreshed and saved

### Summary Tab
- Displays activity metadata: title, type, date, duration, distance, speed
- Shows power stats (avg/max), heart rate (avg/max), cadence (avg/max)
- Editable title and description fields
- Auto-saves edits to `.meta.json` sidecar files
- User edits persist and override default metadata on reload

### Graph View
- Power curve display
- Smoothing slider (Off, 5s, 15s, 30s, 1m, 2m, 5m rolling average)
- Grid lines with power (W) and time labels
- Average power line (for single activities)
- Stats display (min/max/avg, sample count)
- Multi-graph comparison: select a group to overlay power curves from multiple files
- Color-coded legend for comparing power meters or duplicate recordings

### GPS Map View
- Displays activity route on OpenStreetMap tiles
- Automatic zoom to fit activity bounds
- Route colored by power intensity (blue/green/red)
- Start (green) and end (red) markers
- Tiles cached locally for offline viewing
- Only available for activities with GPS data (press M to switch)
- Zwift activities automatically display on the corresponding Zwift world map (Watopia)

## Project Structure

- `main.c` - Raylib GUI, activity tree browser, tab system, graph rendering
- `fit_parser.c/h` - FIT/JSON file parser for power, GPS, heart rate, cadence
- `activity_meta.c/h` - Metadata persistence for user-edited title/description
- `file_organizer.c/h` - Inbox processing and file organization
- `activity_tree.c/h` - Year/month tree data structure
- `strava_api.c/h` - Strava OAuth and API client
- `tile_map.c/h` - OpenStreetMap tile fetching, caching, and map rendering
- `zwift_worlds.c/h` - Zwift world detection and map image handling
- `Makefile` - Build configuration (Linux and macOS)

## Potential Improvements

- Zoom/pan on graph
- Heart rate/cadence graph overlays
- Export to image
- Power zones visualization
