# AccuCheckGlucose (iOS)

A native iOS app (SwiftUI + CoreBluetooth + Swift Charts) that connects to an
**Accu-Chek SmartGuide** continuous glucose monitor over Bluetooth, pulls **all
stored records**, and plots the glucose history. Built for iPhone (tested target
iPhone 16 Pro, iOS 17+).

> Engineering/exploration project. Do **not** use the values shown here for
> therapy decisions — the official Accu-Chek app and a blood glucose meter
> remain the authoritative source.

## Open & run
1. Open `AccuCheckGlucose.xcodeproj` in **Xcode 16** (or newer).
2. Select the **AccuCheckGlucose** target → **Signing & Capabilities** →
   pick your **Team** (the project ships with an empty team and bundle id
   `com.tbiliyor.AccuCheckGlucose`; change the bundle id if needed).
3. Plug in your iPhone, select it as the run destination, and press **Run**.
4. On first launch iOS asks for Bluetooth permission — allow it.
5. Tap **Connect & Load**. iOS handles pairing/encryption automatically. Use
   **Refresh** to re-read.

## How it works
- Scans for the CGM service `0x181F` (the sensor advertises a privacy address
  with no readable name, so we match by service, not name).
- Connects; CoreBluetooth performs bonding/encryption transparently.
- Subscribes to **CGM Measurement** `0x2AA7` (notify) and the **Record Access
  Control Point** `0x2A52` (indicate).
- Writes RACP `01 01` (Report Stored Records / All Records) to stream the full
  history, then decodes each measurement:
  - bytes 2–3: glucose as an IEEE‑11073 SFLOAT (mg/dL)
  - bytes 4–5: time offset in minutes since session start
- The completion indication (`06 …`) marks the end of the batch; the chart and
  latest value are then shown.

## Files
- `AccuCheckGlucoseApp.swift` — app entry point
- `BLEManager.swift` — CoreBluetooth flow + SFLOAT/measurement decoding
- `ContentView.swift` — SwiftUI UI + Swift Charts history graph

## Notes
- Only **one** central can hold the sensor's bond at a time. If the official
  Accu-Chek app is actively connected, disconnect it first.
- This sensor does not expose a battery percentage over BLE.
