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

Drop `.fit` files into the inbox folder:
- **Linux**: `~/.local/share/fitpower/inbox/`
- **macOS**: `~/Library/Application Support/fitpower/inbox/`

On startup, files are automatically organized into `activity/YYYY/MM/` based on the activity timestamp extracted from the FIT file.

### Tabs

- **Local**: Browse your activities organized by year and month
- **Strava**: Browse and view activities from your Strava account

### Controls

- **1/2**: Switch between Local and Strava tabs
- **P/M**: Switch between Power graph and Map view (Map only available for GPS activities)
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

### Local FIT Files
- Automatic inbox processing and organization by date
- Year/month tree browser with expand/collapse
- Parses FIT binary protocol (definition messages, data messages, compressed timestamps)
- Extracts power data, GPS coordinates, and activity timestamps from record messages

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
- `fit_parser.c/h` - FIT file parser for power and GPS data
- `file_organizer.c/h` - Inbox processing and file organization
- `activity_tree.c/h` - Year/month tree data structure
- `strava_api.c/h` - Strava OAuth and API client
- `tile_map.c/h` - OpenStreetMap tile fetching, caching, and map rendering
- `zwift_worlds.c/h` - Zwift world detection and map image handling
- `Makefile` - Build configuration (Linux and macOS)

## Potential Improvements

- Power graph for Strava activities (fetch streams)
- Zoom/pan on graph
- Heart rate overlay
- Cadence display
- Export to image
- Smoothing/averaging options
- Power zones visualization
