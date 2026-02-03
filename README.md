# Sweattrails

A C program using raylib that parses `.fit` files and displays power data in an interactive graph. Includes Strava integration for browsing activities.

## Requirements

- raylib
- libcurl
- openssl
- clang
- pkg-config
- mkcert (optional, for Wahoo HTTPS callback)

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
./sweattrails
```

### Adding Activities

Drop `.fit` files into the inbox folder (or download from Strava):
- **Linux**: `~/.local/share/sweattrails/inbox/`
- **macOS**: `~/Library/Application Support/sweattrails/inbox/`

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

**Linux**: `~/.local/share/sweattrails/`
```
~/.local/share/sweattrails/
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

**macOS**: `~/Library/Application Support/sweattrails/`

## Strava Setup

1. Create a Strava API application at https://www.strava.com/settings/api
2. Set "Authorization Callback Domain" to `localhost`
3. Create config file `~/.config/sweattrails/strava_config`:
   ```json
   {
     "client_id": "YOUR_CLIENT_ID",
     "client_secret": "YOUR_CLIENT_SECRET"
   }
   ```
4. Run sweattrails, switch to Strava tab (press `2`), click "Connect to Strava"
5. Authorize in browser - activities will sync automatically on startup

## Wahoo Setup

1. Create a Wahoo API application at https://developers.wahooligan.com
2. Set redirect URI to `https://localhost:8090/callback` (HTTPS required)
3. Request `workouts_read` scope
4. Install mkcert and create localhost certificate:
   ```bash
   # Install mkcert (Fedora)
   sudo dnf install mkcert
   # Or on macOS
   brew install mkcert

   # Create local CA and localhost certificate
   mkcert -install
   mkdir -p ~/.config/sweattrails/certs
   cd ~/.config/sweattrails/certs
   mkcert localhost 127.0.0.1
   ```
5. Create config file `~/.config/sweattrails/wahoo_config`:
   ```json
   {
     "client_id": "YOUR_CLIENT_ID",
     "client_secret": "YOUR_CLIENT_SECRET"
   }
   ```
6. Run sweattrails, switch to Strava tab, click "Connect to Wahoo" - workouts sync automatically on startup

## Zwift Setup

Sweattrails can automatically import FIT files from Zwift's local Activities folder, including from a remote machine via SSH.

### Local Zwift folder (same machine)

No configuration needed - the app auto-detects `~/Documents/Zwift/Activities/`.

### Remote Zwift folder (via SSH)

1. Ensure SSH public key authentication is set up to the remote machine
2. Create config file `~/.config/sweattrails/zwift_config`:
   ```json
   {
     "source_folder": "/Users/username/Documents/Zwift/Activities",
     "remote_host": "user@192.168.1.100",
     "auto_sync": true
   }
   ```
3. Activities sync automatically on startup

Files are deduplicated by activity timestamp and file size, so re-running won't import duplicates.

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
- **Automatic sync on startup**: Downloads new activities (up to 500 most recent)
- Progress modal shows sync status with download/skip counts
- Fetches activity list from Strava API
- Shows activity details (date, type, distance, power indicator)
- Download activities as JSON with full stream data (power, GPS, heartrate, cadence)
- Downloaded activities appear in the Local tab and can be viewed like FIT files
- Tokens automatically refreshed and saved

### Wahoo Integration
- OAuth2 authentication with HTTPS callback (uses mkcert for trusted localhost certificate)
- **Automatic sync on startup**: Downloads new workouts as FIT files (up to 300 most recent)
- Progress modal shows sync status with download/skip counts
- Downloads original FIT files from Wahoo CDN
- Workouts organized by date in the Local tab
- Tokens automatically refreshed and saved

### Zwift Integration
- **Automatic sync on startup**: Imports FIT files from Zwift's Activities folder
- Supports local folder or remote machine via SSH/SCP
- Efficient remote sync: single SSH call for directory listing, skips already-imported files
- Deduplication by activity timestamp + file size
- Files organized by date in the Local tab as `zwift_<timestamp>.fit`

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
- `wahoo_api.c/h` - Wahoo OAuth and API client
- `zwift_sync.c/h` - Zwift local/remote folder sync via SSH/SCP
- `tile_map.c/h` - OpenStreetMap tile fetching, caching, and map rendering
- `zwift_worlds.c/h` - Zwift world detection and map image handling
- `Makefile` - Build configuration (Linux and macOS)

## Potential Improvements

- Zoom/pan on graph
- Heart rate/cadence graph overlays
- Export to image
- Power zones visualization
