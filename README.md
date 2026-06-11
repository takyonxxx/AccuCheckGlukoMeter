# AccuCheckGlukoMeter

A Qt 6.11 / MSVC / Windows desktop app that finds an **Accu-Chek SmartGuide**
continuous glucose monitor (CGM) sensor over Bluetooth Low Energy, pairs with
it using the box pair code, connects, and shows the live glucose reading.

> Engineering/exploration project. **Do not use values shown here for therapy
> decisions.** The official Accu-Chek app + a blood glucose meter remain the
> authoritative source for dosing.

## What it does
- Scans BLE and identifies the sensor (name starting with `AC-`, or the
  standard CGM service `0x181F`).
- Pairs by address with the **pair code** using native WinRT
  `DeviceInformationCustomPairing` (Qt cannot pair on Windows).
- Connects as a GATT central, discovers services, enables notifications.
- Parses the standard **CGM Measurement** characteristic and displays:
  - large glucose value + `mg/dL`, color-coded (Low / In range / High),
  - sensor name (`AC-...`),
  - battery percentage (if the sensor exposes Battery Level),
  - connection status pill and last-update time.

One button drives the whole flow: **Pair & Connect** (becomes *Cancel* while
working, *Disconnect* when online).

## Files
| File | Role |
|------|------|
| `AccuCheckGlukoMeter.pro` | qmake project (Qt + bluetooth, C++/WinRT include & libs) |
| `main.cpp` | entry point |
| `blescanner.h/.cpp` | `QBluetoothDeviceDiscoveryAgent` wrapper; target matching |
| `bleclient.h/.cpp` | `QLowEnergyController` GATT client; parses glucose/battery/name |
| `winpairing.h/.cpp` | WinRT PIN pairing (Windows only) |
| `mainwindow.h/.cpp` | UI + flow state machine |

## Build
Qt Creator: open `AccuCheckGlukoMeter.pro`, pick a **Qt 6.11 MSVC 64-bit** kit
with the `bluetooth` module, then **Build > Run qmake > Rebuild All**. Run qmake
again whenever the `.pro` or a signal/header changes.

Command line:
```
qmake AccuCheckGlukoMeter.pro
nmake          # or jom
```

### Windows / C++/WinRT notes
- `.pro` adds the C++/WinRT header path. **Adjust the SDK version** if yours
  differs (look under `C:\Program Files (x86)\Windows Kits\10\Include\`):
  ```
  INCLUDEPATH += ".../Windows Kits/10/Include/10.0.22621.0/cppwinrt"
  ```
- `.pro` links `ole32`, `oleaut32`, `runtimeobject` (required by C++/WinRT in a
  classic desktop app).
- `CONFIG += console` keeps a console window for `qDebug` during development;
  remove it for a clean GUI-only build.

## Usage
1. Put the **phone in airplane mode** (the sensor keeps a single bonded link;
   if the phone holds it, the PC can't connect).
2. Enter the **pair code** from the sensor box (only needed the first time;
   Windows remembers the bond afterwards).
3. Press **Pair & Connect**. The app searches, pairs with the current address,
   connects, and starts showing the reading.

## How it works (key points)
- **Target match** lives in `BleScanner::matchesTarget()`: CGM service
  (`0x181F`/`0x1808`) or a name starting with `AC-`.
- **RPA**: the sensor advertises with a rotating (resolvable private) address,
  so the app pairs/connects with the address captured at the moment of the
  sighting and re-scans for a fresh one on failure.
- **Glucose**: `BleClient` reads the **CGM Measurement** char `0x2AA7`; bytes
  2-3 are an IEEE-11073 SFLOAT in mg/dL.
- **Device name**: read from `0x2A00` (Generic Access).
- **Battery**: read from `0x2A19` if a Battery Service is present.

## GATT map observed on the sensor (FW_V7.5.1)
- `0x1800` Generic Access - device name `AC-1R000960570`
- `0x1801` Generic Attribute
- `0x180A` Device Information - manufacturer "Roche Diabetes Care GmbH",
  model `303`, serial `1R000960570`, HW `3.5.64_...`, FW `FW_V7.5.1-...`
- `0x181F` Continuous Glucose Monitoring
  - `0x2AA7` CGM Measurement (Notify)
  - `0x2AA8` Feature, `0x2AA9` Status, `0x2AAA` Session Start, `0x2AAB` Run Time
  - `0x2A52` Record Access Control Point, `0x2AAC` Specific Ops Control Point
- `0x1829` Bond Management

No standard Battery Service (`0x180F`) was advertised, so the battery indicator
may stay empty on this sensor.

## Known limitations
- **Encryption/bond required**: CGM Measurement notifications need an encrypted
  (bonded) link. Without a successful pair, enabling notify fails with
  `DescriptorWriteError`.
- **Single bond**: CGM sensors typically allow one bonded peer. Pairing the PC
  may drop the phone's bond and require re-setup/recalibration on the phone.
- **RPA timing**: if the address rotates between sighting and pairing, you may
  see "device not reachable" - press Pair & Connect again.
