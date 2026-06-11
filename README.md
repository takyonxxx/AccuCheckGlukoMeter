# Accu-Chek SmartGuide CGM Reader

Two companion apps — a **native iOS app** and a **cross-platform Qt desktop app** —
that connect to an **Accu-Chek SmartGuide** continuous glucose monitor (CGM) over
Bluetooth Low Energy, read the stored history, plot it, and keep the value
updating. Both clients implement the same reverse-engineered BLE/GATT flow.

> **Disclaimer — read me.** This is a personal engineering/exploration project.
> It is **not** a medical device and is **not** affiliated with or endorsed by
> Roche. Do **not** use the values shown here for therapy, dosing, or any health
> decision. The official Accu-Chek app and a blood-glucose meter (fingerstick)
> remain the authoritative source.

---

## Contents

| Folder | App | Stack |
|--------|-----|-------|
| `AccuCheckGlucose/` | iOS app | Swift, SwiftUI, CoreBluetooth, Swift Charts |
| `AccuCheckGlukoMeter/` | Desktop app (Windows / Linux / macOS) | C++17, Qt 6, Qt Bluetooth, Qt Charts |

Both apps share the same logic and produce the same view: a current value with a
trend arrow, a glucose history chart, and average glucose + estimated HbA1c.

---

## Features

- **Real timestamps.** Reads the sensor's Session Start Time (`0x2AAA`,
  time-zone aware) so every record maps to a real wall-clock time.
- **Local history.** All records are cached locally and survive restarts; the
  history accumulates over time (the sensor holds up to ~14 days).
- **Incremental sync.** On refresh, only records newer than the cache are pulled
  (RACP "greater-than"); falls back to a full pull when unsupported.
- **Robust full pull.** A record-count query reports completeness; a mid-transfer
  cut resumes from the last offset instead of restarting; drops trigger
  reconnect/retry.
- **Live + auto-refresh.** Stays connected for live notifications and re-syncs
  every 5 minutes, with a countdown to the next update. Monitoring keeps running
  even while the link sleeps between pushes (a **Stop** button ends it).
- **Readable chart.** Light background, target band (70–180 mg/dL), threshold
  lines, and a range selector: **8 h / 24 h / All**. On iOS the *All* view is a
  scrollable 24-hour window so the graph never gets squished.
- **Stats.** Average glucose and **estimated HbA1c** over all cached records
  (ADAG: `HbA1c% = (mean mg/dL + 46.7) / 28.7`).

---

## iOS app (`AccuCheckGlucose/`)

**Requirements:** Xcode 16+, an iPhone on iOS 16+ (scrolling chart needs iOS 17+),
and an Apple developer account/team for on-device signing.

**Build & run**
1. Open `AccuCheckGlucose.xcodeproj`.
2. Target → **Signing & Capabilities** → select your **Team** (set your own
   bundle identifier if needed).
3. Pick your iPhone as the run destination and press **Run** (⌘R).
4. Allow Bluetooth on first launch, then tap **Connect & Load**.

To keep updating while the screen is locked / app is backgrounded, add to
`Info.plist`:

```xml
<key>UIBackgroundModes</key>
<array><string>bluetooth-central</string></array>
```

---

## Desktop app (`AccuCheckGlukoMeter/`)

**Requirements:** Qt 6 with the **Bluetooth** and **Charts** modules, plus a C++17
compiler.

**Build & run**
- *Qt Creator:* open `AccuCheckGlukoMeter.pro`, choose a Qt 6 kit, Build & Run.
- *Command line:*
  ```
  qmake
  make            # Linux / macOS
  # nmake / jom    (Windows, MSVC)
  # mingw32-make   (Windows, MinGW)
  ```

**Per-platform notes**
- **Windows** — uses the WinRT Bluetooth backend. Pair the sensor first via
  Windows *Settings → Add device*. Windows BLE with rotating private addresses
  can be flaky; if a sync stalls, press **Refresh**.
- **Linux** — uses BlueZ; make sure `bluetoothd` is running.
- **macOS** — the bundled `Info.plist` supplies the Bluetooth usage string; macOS
  prompts for permission on first launch and handles bonding transparently.

Cache file: `cgm_cache_v2.json` under the platform's app-data location
(`%APPDATA%`, `~/.local/share`, or `~/Library/Application Support`).

---

## How it works

The sensor exposes a standard Bluetooth **Continuous Glucose Monitoring Service**
(`0x181F`). The apps:

1. Scan and match the sensor by the CGM service (its name is often hidden behind
   a rotating private address).
2. Connect — the OS handles pairing/encryption.
3. Read **Session Start Time** (`0x2AAA`) and subscribe to **CGM Measurement**
   (`0x2AA7`, notify) and the **Record Access Control Point** (`0x2A52`, indicate).
4. Query the record count (RACP `04 01`), then stream records (RACP `01 01`, or
   `01 03 01 <offset>` to resume / fetch newer).
5. Decode each measurement: glucose as an IEEE-11073 **SFLOAT** (mg/dL) plus a
   time offset in minutes from the session start.

### Sensor reference

| Item | Value |
|------|-------|
| CGM service | `0x181F` |
| Measurement (notify) | `0x2AA7` |
| RACP (write/indicate) | `0x2A52` |
| Session Start Time | `0x2AAA` |
| Device name | `0x2A00` |
| Glucose encoding | IEEE-11073 SFLOAT (mg/dL), little-endian |
| Time offset | uint16 minutes since session start |

---

## Limitations

- Only one BLE central can hold the sensor at a time. If the official app is
  connected, disconnect it first.
- The sensor exposes a limited buffer over BLE; full multi-day history builds up
  locally over time as the app keeps syncing.
- The estimated HbA1c is derived from whatever records are on hand, so it differs
  from a lab A1c until enough days of data accumulate.

## License

Add your preferred license here (e.g. MIT).
