# AccuCheckGlukoMeter (Qt, cross-platform)

A desktop app (Qt 6 / Qt Bluetooth / Qt Charts) that connects to an Accu-Chek
SmartGuide CGM over Bluetooth Low Energy, pulls the stored history, caches it
locally, and keeps the value current. Runs on **Windows, Linux and macOS**.

> Engineering/exploration project. Do not use the values shown here for therapy
> decisions; the official Accu-Chek app and a blood glucose meter remain the
> authoritative source.

## Features (ported from the iOS app)
- Reads **Session Start Time (0x2AAA)** so every record gets a real timestamp
  (time-zone aware).
- **Local cache** (JSON in the app data folder) - history survives restarts and
  accumulates over time (the sensor stores up to ~14 days).
- **Incremental sync**: on refresh, only records newer than the cache are
  fetched (RACP "greater-than"); falls back to a full pull if unsupported.
- **Full pull with resume**: a mid-transfer cut resumes from the last offset
  instead of restarting; a record-count query reports completeness.
- **Live + auto-refresh**: stays connected for live notifications and re-syncs
  every 5 minutes; a countdown shows the time to the next update.
- **Time-axis chart** with a 70-180 target band, 250 line, and **8 Saat /
  24 Saat / Tumu** range selector (axis switches to dates over long spans).
- Monitoring state with a **Stop** button (the link drops between pushes; the
  app keeps monitoring and reconnects automatically).

## Build

Requires Qt 6 with the **Bluetooth** and **Charts** modules.

### Qt Creator
Open `AccuCheckGlukoMeter.pro`, select a Qt 6 kit, build and run.

### Command line (qmake)
```
qmake
make            # Linux / macOS
# or: nmake / jom on Windows (MSVC), mingw32-make on MinGW
```

### Per-platform notes
- **Windows**: uses Qt's WinRT Bluetooth backend. Pair the sensor first via
  Windows Settings (Add device). Windows BLE with rotating private addresses can
  be flaky; if a sync stalls, press Refresh.
- **Linux**: uses BlueZ. Make sure `bluetoothd` is running. Pairing is handled
  by the OS agent on first encrypted access.
- **macOS**: the bundled `Info.plist` supplies the Bluetooth usage string; macOS
  prompts for permission on first launch and handles bonding transparently.

## Notes
- Only one BLE central can hold the sensor at a time. If the official app is
  connected, disconnect it first.
- Cache file: `cgm_cache_v2.json` under the platform app-data location
  (e.g. `%APPDATA%`, `~/.local/share`, `~/Library/Application Support`).
