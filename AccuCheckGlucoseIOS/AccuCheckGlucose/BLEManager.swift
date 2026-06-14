import Foundation
import CoreBluetooth
import Combine

/// One decoded CGM measurement record.
struct GlucoseReading: Identifiable, Equatable, Codable {
    var id = UUID()
    var offsetMin: Double   // minutes since session start
    var date: Date          // real wall-clock timestamp
    var mgdl: Double
}

/// BLE flow for the Accu-Chek SmartGuide CGM sensor.
///
/// History: reads the session start time -> real timestamps, caches locally,
/// fetches only newer records on refresh.
/// Live: after the history loads it stays connected (sensor pushes every 5 min
/// via SOCP comm-interval) and, as a fallback, auto-refreshes every 5 minutes
/// so the value keeps updating on its own.
final class BLEManager: NSObject, ObservableObject {

    // MARK: - GATT identifiers
    private let cgmService       = CBUUID(string: "181F")
    private let glucoseService   = CBUUID(string: "1808")
    private let deviceInfo       = CBUUID(string: "180A")
    private let genericAccess    = CBUUID(string: "1800")
    private let measurementChar  = CBUUID(string: "2AA7") // Notify
    private let racpChar         = CBUUID(string: "2A52") // Write / Indicate
    private let socpChar         = CBUUID(string: "2AAC") // Specific Ops Control Point
    private let sessionStartChar = CBUUID(string: "2AAA") // Session Start Time (Read)
    private let nameChar         = CBUUID(string: "2A00")

    // MARK: - Published UI state
    @Published var status: String = "Tap Connect to start"
    @Published var deviceName: String = ""
    @Published var latest: Double? = nil
    @Published var trendArrow: String = ""
    @Published var readings: [GlucoseReading] = []
    @Published var isBusy: Bool = false
    @Published var connected: Bool = false
    @Published var monitoring: Bool = false   // user wants continuous updates (independent of the transient link)
    @Published var nextUpdateAt: Date? = nil  // when the next automatic refresh is due (for the countdown)
    @Published var bluetoothReady: Bool = false

    // MARK: - Persistence
    private let kReadings = "cgm_readings_v2"
    private let kSession  = "cgm_session_start_v2"
    private var cachedSessionStart: Date?

    // MARK: - BLE internals
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var racpCharacteristic: CBCharacteristic?
    private var socpCharacteristic: CBCharacteristic?
    private var measReady = false
    private var racpReady = false
    private var sessionResolved = false
    private var sessionStart: Date?

    // MARK: - Fetch state
    private enum Mode { case full, incremental }
    private var mode: Mode = .full
    private var recordsCommand = Data([0x01, 0x01])
    private var collecting: [GlucoseReading] = []
    private var seenOffsets = Set<Int>()
    private var expectedCount = -1
    private var attempts = 0
    private let maxAttempts = 40
    private var gotCompletion = false
    private var resuming = false
    private var triedResumeFallback = false
    private var recordsAtRequest = 0

    // MARK: - Live / auto-update
    private var wantLive = false          // user wants continuous updates
    private var liveMode = false          // currently receiving live notifications
    private let autoRefreshSeconds = 300.0
    private var autoRefreshTimer: DispatchWorkItem?

    private var scanTimeout: DispatchWorkItem?
    private var sessionTimeout: DispatchWorkItem?
    private var stallTimer: DispatchWorkItem?
    private let stallSeconds = 6.0

    private let hmFormatter: DateFormatter = {
        let f = DateFormatter(); f.dateFormat = "HH:mm"; return f
    }()

    override init() {
        super.init()
        loadCache()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    // MARK: - Public actions
    func connect() {
        guard central.state == .poweredOn else { status = "Turn on Bluetooth"; return }
        wantLive = true
        monitoring = true
        nextUpdateAt = nil
        fullCleanup()
        collecting = []
        seenOffsets = []
        expectedCount = -1
        attempts = 0
        gotCompletion = false
        resuming = false
        triedResumeFallback = false
        liveMode = false
        sessionResolved = false
        sessionStart = nil
        isBusy = true
        status = "Scanning for sensor…"
        central.scanForPeripherals(withServices: [cgmService, glucoseService], options: nil)
        armScanTimeout()
    }

    func refresh() { connect() }

    func disconnect() {
        wantLive = false
        monitoring = false
        liveMode = false
        nextUpdateAt = nil
        autoRefreshTimer?.cancel()
        fullCleanup()
        isBusy = false
        connected = false
        status = readings.isEmpty ? "Disconnected" : "Disconnected · \(readings.count) records cached"
    }

    // MARK: - Persistence
    private func loadCache() {
        let d = UserDefaults.standard
        if let data = d.data(forKey: kReadings),
           let arr = try? JSONDecoder().decode([GlucoseReading].self, from: data) {
            readings = arr.sorted { $0.date < $1.date }
            latest = readings.last?.mgdl
            updateTrend()
            if !readings.isEmpty { status = "\(readings.count) records cached" }
        }
        cachedSessionStart = d.object(forKey: kSession) as? Date
    }

    private func saveCache() {
        let d = UserDefaults.standard
        if let data = try? JSONEncoder().encode(readings) { d.set(data, forKey: kReadings) }
        if let ss = sessionStart ?? cachedSessionStart { d.set(ss, forKey: kSession) }
    }

    // MARK: - Timers
    private func armScanTimeout() {
        scanTimeout?.cancel()
        let w = DispatchWorkItem { [weak self] in
            guard let self else { return }
            if self.peripheral == nil {
                self.central.stopScan()
                self.isBusy = false
                self.status = "Sensor not found - keep it nearby and try again"
                self.scheduleAutoRefresh()   // try again later if user wants live
            }
        }
        scanTimeout = w
        DispatchQueue.main.asyncAfter(deadline: .now() + 15, execute: w)
    }

    private func armSessionTimeout() {
        sessionTimeout?.cancel()
        let w = DispatchWorkItem { [weak self] in
            guard let self, !self.sessionResolved else { return }
            self.sessionResolved = true
            self.maybeStart()
        }
        sessionTimeout = w
        DispatchQueue.main.asyncAfter(deadline: .now() + 3, execute: w)
    }

    private func armStall() {
        stallTimer?.cancel()
        let w = DispatchWorkItem { [weak self] in
            guard let self, self.isBusy, !self.gotCompletion else { return }
            self.retryOnDrop(reason: "stalled")
        }
        stallTimer = w
        DispatchQueue.main.asyncAfter(deadline: .now() + stallSeconds, execute: w)
    }
    private func cancelStall() { stallTimer?.cancel(); stallTimer = nil }

    /// Schedule the next automatic incremental refresh (~5 min). Live
    /// notifications reset this, so polling only happens if the sensor is quiet.
    private func scheduleAutoRefresh() {
        autoRefreshTimer?.cancel()
        guard wantLive else { nextUpdateAt = nil; return }
        let w = DispatchWorkItem { [weak self] in
            guard let self, self.wantLive else { return }
            if self.isBusy { self.scheduleAutoRefresh(); return }
            self.connect()
        }
        autoRefreshTimer = w
        nextUpdateAt = Date().addingTimeInterval(autoRefreshSeconds)
        DispatchQueue.main.asyncAfter(deadline: .now() + autoRefreshSeconds, execute: w)
    }

    private func fullCleanup() {
        scanTimeout?.cancel(); sessionTimeout?.cancel(); cancelStall()
        if central.isScanning { central.stopScan() }
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        peripheral = nil
        racpCharacteristic = nil
        socpCharacteristic = nil
        measReady = false; racpReady = false
    }

    // MARK: - Decoding
    private func sfloat(_ raw: UInt16) -> Double {
        switch raw {
        case 0x07FF, 0x0800, 0x07FE, 0x0801, 0x0802: return .nan
        default: break
        }
        var mantissa = Int(raw & 0x0FFF)
        var exponent = Int((raw >> 12) & 0x000F)
        if exponent >= 0x0008 { exponent -= 0x0010 }
        if mantissa >= 0x0800 { mantissa -= 0x1000 }
        return Double(mantissa) * pow(10.0, Double(exponent))
    }

    private func parseSessionStart(_ data: Data) {
        let b = [UInt8](data)
        guard b.count >= 7 else { return }
        let year = Int(UInt16(b[0]) | (UInt16(b[1]) << 8))
        var c = DateComponents()
        c.year = year; c.month = Int(b[2]); c.day = Int(b[3])
        c.hour = Int(b[4]); c.minute = Int(b[5]); c.second = Int(b[6])

        // Byte 7 = Time Zone (signed, 15-minute units). The sensor stores the
        // start time in this zone (0 = UTC). Build the absolute instant in that
        // zone; the UI then renders it in the device's local time.
        var tzSeconds = 0
        if b.count >= 8 {
            let tzRaw = Int8(bitPattern: b[7])
            if tzRaw != Int8.min { tzSeconds = Int(tzRaw) * 15 * 60 }
        }
        var cal = Calendar(identifier: .gregorian)
        cal.timeZone = TimeZone(secondsFromGMT: tzSeconds) ?? TimeZone(identifier: "UTC")!
        sessionStart = cal.date(from: c)
    }

    /// CGM Measurement: [0]=size [1]=flags [2..3]=glucose SFLOAT(LE) [4..5]=offset(min, u16 LE)
    private func decodeMeasurement(_ data: Data) -> (Int, Double)? {
        guard data.count >= 6 else { return nil }
        let b = [UInt8](data)
        let raw = UInt16(b[2]) | (UInt16(b[3]) << 8)
        let mgdl = sfloat(raw)
        guard mgdl.isFinite else { return nil }
        let offset = Int(UInt16(b[4]) | (UInt16(b[5]) << 8))
        return (offset, mgdl)
    }

    private func date(forOffset offset: Int) -> Date {
        if let ss = sessionStart { return ss.addingTimeInterval(Double(offset) * 60) }
        return Date()
    }

    // MARK: - Decide what to request
    private func sameSession(_ a: Date?, _ b: Date?) -> Bool {
        guard let a, let b else { return false }
        return abs(a.timeIntervalSince(b)) < 90
    }

    private func maybeStart() {
        guard measReady, racpReady, sessionResolved else { return }
        if resuming { requestRecords(); return }

        if sameSession(cachedSessionStart, sessionStart), sessionStart != nil {
            let maxOffset = readings.map { Int($0.offsetMin) }.max() ?? -1
            if maxOffset >= 0 {
                mode = .incremental
                seenOffsets = Set(readings.map { Int($0.offsetMin) })
                let from = maxOffset + 1
                recordsCommand = Data([0x01, 0x03, 0x01,
                                       UInt8(from & 0xFF), UInt8((from >> 8) & 0xFF)])
                expectedCount = -1
                requestRecords()
                return
            }
        }
        startFullPull()
    }

    private func startFullPull() {
        mode = .full
        recordsCommand = Data([0x01, 0x01])
        guard let p = peripheral, let racp = racpCharacteristic else { return }
        status = "Counting records…"
        p.writeValue(Data([0x04, 0x01]), for: racp, type: .withResponse)
        armStall()
    }

    private func requestRecords() {
        guard let p = peripheral, let racp = racpCharacteristic else { return }
        recordsAtRequest = collecting.count
        updateLoadingStatus()
        p.writeValue(recordsCommand, for: racp, type: .withResponse)
        armStall()
    }

    /// Resume a full pull from just after the highest offset already collected,
    /// so a mid-transfer cut continues instead of restarting from the beginning.
    private func prepareResumeCommand() {
        guard mode == .full else { return }
        let maxOff = collecting.map { Int($0.offsetMin) }.max() ?? -1
        if maxOff >= 0 {
            let from = maxOff + 1
            recordsCommand = Data([0x01, 0x03, 0x01,
                                   UInt8(from & 0xFF), UInt8((from >> 8) & 0xFF)])
        }
    }

    private func updateLoadingStatus() {
        if mode == .incremental {
            status = collecting.isEmpty ? "Checking for new records…" : "Syncing \(collecting.count) new…"
        } else if expectedCount > 0 {
            status = "Loading \(collecting.count) / \(expectedCount)…"
        } else {
            status = "Loading \(collecting.count)…"
        }
    }

    // MARK: - Retry / finish
    private func retryOnDrop(reason: String) {
        cancelStall()
        guard attempts < maxAttempts else { publishResults(); return }
        attempts += 1
        prepareResumeCommand()
        if connected, peripheral != nil, racpCharacteristic != nil {
            resuming = true
            requestRecords()
        } else if let p = peripheral {
            resuming = true
            measReady = false; racpReady = false
            status = "Reconnecting (\(collecting.count) new)…"
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { [weak self] in
                self?.central.connect(p, options: nil)
            }
        } else {
            publishResults()
        }
    }

    private func handleCompletion(_ b: [UInt8]) {
        let rsp = b.count >= 3 ? b[2] : 0x01

        // Operator / operand not supported by this sensor.
        if rsp == 0x04 || rsp == 0x05 || rsp == 0x09 {
            if mode == .incremental {
                collecting = []; seenOffsets = []
                startFullPull()
            } else if !triedResumeFallback {
                // Resume-by-offset rejected -> fall back to plain "all records".
                triedResumeFallback = true
                recordsCommand = Data([0x01, 0x01])
                resuming = true
                requestRecords()
            } else {
                publishResults()
            }
            return
        }

        cancelStall()

        // No (more) records in the requested range -> we have everything.
        if rsp == 0x06 { publishResults(); return }

        // Success: for a full pull, keep resuming until we reach the expected
        // count (or stop making progress).
        if mode == .full {
            let progressed = collecting.count > recordsAtRequest
            let needMore = expectedCount < 0 ? progressed : (collecting.count < expectedCount)
            if progressed, needMore, attempts < maxAttempts {
                attempts += 1
                prepareResumeCommand()
                resuming = true
                requestRecords()
                return
            }
        }
        publishResults()
    }

    private func assignDates() {
        let maxOffset = collecting.map { Int($0.offsetMin) }.max() ?? 0
        let now = Date()
        for i in collecting.indices {
            if let ss = sessionStart {
                collecting[i].date = ss.addingTimeInterval(collecting[i].offsetMin * 60)
            } else {
                collecting[i].date = now.addingTimeInterval((collecting[i].offsetMin - Double(maxOffset)) * 60)
            }
        }
    }

    private func updateTrend() {
        if readings.count >= 2 {
            let d = readings[readings.count - 1].mgdl - readings[readings.count - 2].mgdl
            trendArrow = d > 4 ? "↑" : (d < -4 ? "↓" : "→")
        } else { trendArrow = "" }
    }

    private func mergeIntoReadings(_ newOnes: [GlucoseReading]) {
        var map = [Date: GlucoseReading]()
        for r in readings { map[r.date] = r }
        for r in newOnes { map[r.date] = r }
        readings = map.values.sorted { $0.date < $1.date }
        latest = readings.last?.mgdl
        updateTrend()
        saveCache()
    }

    private func publishResults() {
        cancelStall()
        assignDates()
        let newCount = collecting.count
        mergeIntoReadings(collecting)
        cachedSessionStart = sessionStart ?? cachedSessionStart
        saveCache()

        if readings.isEmpty {
            status = "No records found"
        } else if mode == .incremental {
            status = newCount == 0 ? "Up to date · \(readings.count) records"
                                   : "Added \(newCount) · \(readings.count) total"
        } else if expectedCount > 0 && newCount < expectedCount {
            status = "Loaded \(readings.count) records (\(newCount)/\(expectedCount))"
        } else {
            status = "Loaded \(readings.count) records"
        }
        isBusy = false
        gotCompletion = true

        // Continuous updates: stay live if connected, and always (re)schedule the
        // 5-minute fallback poll.
        if wantLive {
            if connected { enterLiveMode() }
            scheduleAutoRefresh()
        }
    }

    // MARK: - Live mode
    private func enterLiveMode() {
        guard wantLive, connected else { return }
        liveMode = true
        seenOffsets = Set(readings.map { Int($0.offsetMin) })
        // No SOCP write here: it can destabilize the link on this sensor. Live
        // notifications (if the sensor sends them) plus the 5-minute poll keep
        // the value current.
        if let v = latest {
            status = "Live · \(Int(v.rounded())) mg/dL · \(readings.count) records"
        } else {
            status = "Live · \(readings.count) records"
        }
    }

    private func addLive(offset: Int, mgdl: Double) {
        if seenOffsets.contains(offset) { return }
        seenOffsets.insert(offset)
        let r = GlucoseReading(offsetMin: Double(offset), date: date(forOffset: offset), mgdl: mgdl)
        mergeIntoReadings([r])
        status = "Live · \(hmFormatter.string(from: Date())) · \(Int(mgdl.rounded())) mg/dL"
        scheduleAutoRefresh()   // got a live value -> push the fallback poll out
    }
}

// MARK: - CBCentralManagerDelegate
extension BLEManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        bluetoothReady = central.state == .poweredOn
        switch central.state {
        case .poweredOn:    if readings.isEmpty { status = "Ready - tap Connect" }
        case .poweredOff:   status = "Bluetooth is off"
        case .unauthorized: status = "Bluetooth permission denied"
        default:            status = "Bluetooth unavailable"
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        scanTimeout?.cancel()
        central.stopScan()
        self.peripheral = peripheral
        peripheral.delegate = self
        let adv = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? peripheral.name ?? ""
        if adv.uppercased().hasPrefix("AC-") { deviceName = adv }
        status = "Connecting…"
        central.connect(peripheral, options: nil)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        connected = true
        status = resuming ? "Reconnected, resuming…" : "Discovering services…"
        peripheral.discoverServices([cgmService, deviceInfo, genericAccess])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        connected = false
        if isBusy && !gotCompletion { retryOnDrop(reason: "failed to connect") }
        else { scheduleAutoRefresh() }
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        connected = false
        if isBusy && !gotCompletion {
            retryOnDrop(reason: "disconnected")
        } else if wantLive {
            // Sensor closed the link (it sleeps between pushes). Monitoring stays
            // ON; the 5-minute poll reconnects and syncs automatically.
            liveMode = false
            if let v = latest {
                status = "Monitoring · last \(Int(v.rounded())) mg/dL · update ~5 min"
            } else {
                status = "Monitoring · next update ~5 min"
            }
            scheduleAutoRefresh()
        }
    }
}

// MARK: - CBPeripheralDelegate
extension BLEManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let services = peripheral.services else { return }
        for s in services { peripheral.discoverCharacteristics(nil, for: s) }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard let chars = service.characteristics else { return }
        if !resuming { armSessionTimeout() }
        for c in chars {
            switch c.uuid {
            case measurementChar:  peripheral.setNotifyValue(true, for: c)
            case racpChar:         racpCharacteristic = c; peripheral.setNotifyValue(true, for: c)
            case socpChar:         socpCharacteristic = c
            case sessionStartChar: peripheral.readValue(for: c)
            case nameChar:         peripheral.readValue(for: c)
            default: break
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        if characteristic.uuid == measurementChar { measReady = true }
        if characteristic.uuid == racpChar { racpReady = true }
        maybeStart()
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard let data = characteristic.value else { return }
        switch characteristic.uuid {
        case measurementChar:
            guard let (offset, mgdl) = decodeMeasurement(data) else { return }
            if liveMode {
                addLive(offset: offset, mgdl: mgdl)
            } else {
                if seenOffsets.contains(offset) { return }
                seenOffsets.insert(offset)
                collecting.append(GlucoseReading(offsetMin: Double(offset), date: Date(), mgdl: mgdl))
                updateLoadingStatus()
                armStall()
            }

        case racpChar:
            let b = [UInt8](data)
            guard let op = b.first else { return }
            if op == 0x05 {
                if b.count >= 4 { expectedCount = Int(UInt16(b[2]) | (UInt16(b[3]) << 8)) }
                status = "Sensor reports \(expectedCount) records"
                resuming = true
                requestRecords()
            } else if op == 0x06 {
                handleCompletion(b)
            }

        case sessionStartChar:
            parseSessionStart(data)
            sessionResolved = true
            sessionTimeout?.cancel()
            maybeStart()

        case nameChar:
            if let name = String(data: data, encoding: .utf8), !name.isEmpty { deviceName = name }

        default: break
        }
    }
}
