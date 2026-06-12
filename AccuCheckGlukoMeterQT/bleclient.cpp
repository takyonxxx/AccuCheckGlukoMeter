#include "bleclient.h"
#include <QtBluetooth/QBluetoothUuid>
#include <QtBluetooth/QBluetoothLocalDevice>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimeZone>
#include <QDebug>
#include <cmath>

static const QBluetoothUuid kCgm{quint16(0x181F)};
static const QBluetoothUuid kDevInfo{quint16(0x180A)};
static const QBluetoothUuid kGenericAccess{quint16(0x1800)};
static const QBluetoothUuid kMeas{quint16(0x2AA7)};
static const QBluetoothUuid kRacp{quint16(0x2A52)};
static const QBluetoothUuid kSessionStart{quint16(0x2AAA)};
static const QBluetoothUuid kName{quint16(0x2A00)};

BleClient::BleClient(QObject *parent) : QObject(parent) {
    m_scanner = new BleScanner(this);
    connect(m_scanner, &BleScanner::found, this, &BleClient::onScannerFound);

    m_winPairing = new WinPairing(this);
    connect(m_winPairing, &WinPairing::finished, this, [this](bool ok, const QString &msg) {
        qDebug() << "[WinPairing]" << ok << msg;
        setStatus(ok ? "Paired - connecting..." : "Pairing not confirmed - connecting anyway...");
        if (m_deviceInfo.isValid()) connectToDevice(m_deviceInfo);
    });
    connect(m_scanner, &BleScanner::finishedWithoutTarget, this, [this]() {
        if (m_controller) return;             // already connecting
        setStatus("Sensor not found - keep it nearby and try again");
        scheduleAutoRefresh();
    });

    m_scanTimeout.setSingleShot(true);
    connect(&m_scanTimeout, &QTimer::timeout, this, [this]() {
        if (!m_controller) {
            m_scanner->stop();
            setStatus("Sensor not found - keep it nearby and try again");
            scheduleAutoRefresh();
        }
    });

    m_sessionTimeout.setSingleShot(true);
    connect(&m_sessionTimeout, &QTimer::timeout, this, [this]() {
        if (!m_sessionResolved) { m_sessionResolved = true; maybeStart(); }
    });

    m_stallTimer.setSingleShot(true);
    connect(&m_stallTimer, &QTimer::timeout, this, [this]() {
        if (m_wantLive && !m_gotCompletion && m_controller) retryOnDrop("stalled");
    });

    m_autoRefreshTimer.setSingleShot(true);
    connect(&m_autoRefreshTimer, &QTimer::timeout, this, [this]() {
        if (!m_wantLive) return;
        startScan();   // re-sync (incremental thanks to the cache)
    });

    loadCache();
}

// ============================ Public ============================
void BleClient::connectAndLoad() {
    m_wantLive = true;
    m_monitoring = true;
    emit monitoringChanged(true);
    setNextUpdate(QDateTime());
    startScan();
}

void BleClient::refresh() { connectAndLoad(); }

void BleClient::setPin(const QString &pin) { if (!pin.isEmpty()) m_pin = pin; }

void BleClient::stop() {
    m_wantLive = false;
    m_monitoring = false;
    m_liveMode = false;
    m_autoRefreshTimer.stop();
    setNextUpdate(QDateTime());
    cleanupController();
    emit monitoringChanged(false);
    emit connectedChanged(false);
    setStatus(m_readings.isEmpty() ? "Disconnected"
              : QString("Disconnected - %1 records cached").arg(m_readings.size()));
}

// ============================ Scan / connect ============================
void BleClient::startScan() {        // full reset + scan
    resetFetchState();
    beginScan();
}

void BleClient::resetFetchState() {
    m_collecting.clear();
    m_seenOffsets.clear();
    m_expectedCount = -1;
    m_attempts = 0;
    m_gotCompletion = false;
    m_resuming = false;
    m_triedResumeFallback = false;
    m_liveMode = false;
}

void BleClient::beginScan() {        // (re)connect; preserves fetch state for resume
    cleanupController();
    m_sessionResolved = false;
    m_sessionStart = QDateTime();
    m_cccdWritten = 0;
    m_cccdExpected = 0;
    m_measReady = false;
    m_racpReady = false;
    setStatus("Scanning for sensor...");
    m_scanner->start();
    m_scanTimeout.start(15000);
}

void BleClient::onScannerFound(const QBluetoothDeviceInfo &info) {
    m_scanTimeout.stop();
    m_deviceInfo = info;
    if (info.name().startsWith("AC-", Qt::CaseInsensitive))
        { m_deviceName = info.name(); emit deviceNameChanged(m_deviceName); }

#ifdef Q_OS_WIN
    // Windows needs a bond before encrypted GATT reads work. If not already
    // paired, pair with the supplied PIN (default 784651), then connect.
    QBluetoothLocalDevice local;
    const bool paired = local.isValid() &&
            local.pairingStatus(info.address()) != QBluetoothLocalDevice::Unpaired;
    if (paired) {
        connectToDevice(info);
    } else {
        setStatus(QString("Pairing (PIN %1)...").arg(m_pin));
        m_winPairing->pair(info.address(), m_pin, false);
    }
#else
    connectToDevice(info);
#endif
}

void BleClient::connectToDevice(const QBluetoothDeviceInfo &info) {
    setStatus("Connecting...");
    m_controller = QLowEnergyController::createCentral(info, this);

    connect(m_controller, &QLowEnergyController::connected, this, [this]() {
        emit connectedChanged(true);
        setStatus(m_resuming ? "Reconnected, resuming..." : "Discovering services...");
        m_controller->discoverServices();
    });
    connect(m_controller, &QLowEnergyController::disconnected, this, [this]() {
        emit connectedChanged(false);
        if (m_wantLive && !m_gotCompletion) {
            retryOnDrop("disconnected");
        } else if (m_wantLive) {
            m_liveMode = false;
            if (m_latest > 0)
                setStatus(QString("Monitoring - last %1 mg/dL - update ~5 min")
                          .arg(qRound(m_latest)));
            else
                setStatus("Monitoring - next update ~5 min");
            scheduleAutoRefresh();
        }
    });
    connect(m_controller, &QLowEnergyController::errorOccurred, this,
            [this](QLowEnergyController::Error) {
        if (m_wantLive && !m_gotCompletion) retryOnDrop("controller error");
    });
    connect(m_controller, &QLowEnergyController::serviceDiscovered, this,
            &BleClient::onServiceDiscovered);
    connect(m_controller, &QLowEnergyController::discoveryFinished, this,
            &BleClient::onDiscoveryFinished);

    m_controller->connectToDevice();
}

void BleClient::cleanupController() {
    m_scanTimeout.stop();
    m_sessionTimeout.stop();
    m_stallTimer.stop();
    m_scanner->stop();
    if (m_cgm) { m_cgm->deleteLater(); m_cgm = nullptr; }
    if (m_gap) { m_gap->deleteLater(); m_gap = nullptr; }
    if (m_controller) {
        m_controller->disconnectFromDevice();
        m_controller->deleteLater();
        m_controller = nullptr;
    }
    m_measReady = false;
    m_racpReady = false;
}

// ============================ Service discovery ============================
void BleClient::onServiceDiscovered(const QBluetoothUuid &) { /* wait for finish */ }

void BleClient::onDiscoveryFinished() {
    if (!m_resuming) m_sessionTimeout.start(3000);

    if (m_controller->services().contains(kGenericAccess)) {
        m_gap = m_controller->createServiceObject(kGenericAccess, this);
        if (m_gap) {
            connect(m_gap, &QLowEnergyService::characteristicRead, this, &BleClient::onCharacteristicRead);
            connect(m_gap, &QLowEnergyService::stateChanged, this, [this](QLowEnergyService::ServiceState s) {
                if (s == QLowEnergyService::RemoteServiceDiscovered) {
                    auto c = m_gap->characteristic(kName);
                    if (c.isValid()) m_gap->readCharacteristic(c);
                }
            });
            m_gap->discoverDetails();
        }
    }

    if (!m_controller->services().contains(kCgm)) {
        setStatus("CGM service not found on this device");
        return;
    }
    m_cgm = m_controller->createServiceObject(kCgm, this);
    if (!m_cgm) { setStatus("Could not open CGM service"); return; }

    connect(m_cgm, &QLowEnergyService::stateChanged, this, &BleClient::onServiceStateChanged);
    connect(m_cgm, &QLowEnergyService::characteristicChanged, this, &BleClient::onCharacteristicChanged);
    connect(m_cgm, &QLowEnergyService::characteristicRead, this, &BleClient::onCharacteristicRead);
    connect(m_cgm, &QLowEnergyService::descriptorWritten, this, &BleClient::onDescriptorWritten);
    m_cgm->discoverDetails();
}

void BleClient::onServiceStateChanged(QLowEnergyService::ServiceState s) {
    if (s != QLowEnergyService::RemoteServiceDiscovered) return;

    // Read session start time.
    auto ss = m_cgm->characteristic(kSessionStart);
    if (ss.isValid()) m_cgm->readCharacteristic(ss);

    // Enable notify on measurement, indicate on RACP.
    const QBluetoothUuid cccdUuid(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    m_cccdWritten = 0;
    m_cccdExpected = 0;

    auto meas = m_cgm->characteristic(kMeas);
    if (meas.isValid()) {
        auto cccd = meas.descriptor(cccdUuid);
        if (cccd.isValid()) { m_cgm->writeDescriptor(cccd, QByteArray::fromHex("0100")); m_cccdExpected++; }
    }
    auto racp = m_cgm->characteristic(kRacp);
    if (racp.isValid()) {
        auto cccd = racp.descriptor(cccdUuid);
        if (cccd.isValid()) { m_cgm->writeDescriptor(cccd, QByteArray::fromHex("0200")); m_cccdExpected++; }
    }
    if (m_cccdExpected == 0) setStatus("Could not subscribe to CGM notifications");
}

void BleClient::onDescriptorWritten(const QLowEnergyDescriptor &, const QByteArray &) {
    // Both CCCDs share the same UUID; count writes instead of identifying them.
    m_cccdWritten++;
    if (m_cccdExpected > 0 && m_cccdWritten >= m_cccdExpected) {
        m_measReady = true;
        m_racpReady = true;
        maybeStart();
    }
}

void BleClient::onCharacteristicRead(const QLowEnergyCharacteristic &c, const QByteArray &v) {
    if (c.uuid() == kSessionStart) {
        parseSessionStart(v);
        m_sessionResolved = true;
        m_sessionTimeout.stop();
        maybeStart();
    } else if (c.uuid() == kName) {
        const QString n = QString::fromUtf8(v);
        if (!n.isEmpty()) { m_deviceName = n; emit deviceNameChanged(n); }
    }
}

// ============================ Decoding ============================
double BleClient::sfloat(quint16 raw) {
    switch (raw) {
    case 0x07FF: case 0x0800: case 0x07FE: case 0x0801: case 0x0802:
        return std::nan("");
    default: break;
    }
    int mantissa = raw & 0x0FFF;
    int exponent = (raw >> 12) & 0x000F;
    if (exponent >= 0x0008) exponent -= 0x0010;
    if (mantissa >= 0x0800) mantissa -= 0x1000;
    return mantissa * std::pow(10.0, exponent);
}

bool BleClient::decodeMeasurement(const QByteArray &data, int &offset, double &mgdl) {
    if (data.size() < 6) return false;
    const quint8 *b = reinterpret_cast<const quint8 *>(data.constData());
    quint16 raw = quint16(b[2]) | (quint16(b[3]) << 8);
    mgdl = sfloat(raw);
    if (!std::isfinite(mgdl)) return false;
    offset = int(quint16(b[4]) | (quint16(b[5]) << 8));
    return true;
}

void BleClient::parseSessionStart(const QByteArray &data) {
    const quint8 *b = reinterpret_cast<const quint8 *>(data.constData());
    if (data.size() < 7) return;
    int year  = int(quint16(b[0]) | (quint16(b[1]) << 8));
    QDate date(year, b[2], b[3]);
    QTime time(b[4], b[5], b[6]);

    // Byte 7 = Time Zone, signed, in 15-minute units (sensor stores UTC -> 0).
    int tzSeconds = 0;
    if (data.size() >= 8) {
        qint8 tzRaw = qint8(b[7]);
        if (tzRaw != -128) tzSeconds = int(tzRaw) * 15 * 60;
    }
    QDateTime dt(date, time, QTimeZone(tzSeconds));
    if (dt.isValid()) m_sessionStart = dt;     // absolute instant
}

QDateTime BleClient::dateForOffset(int offset) const {
    if (m_sessionStart.isValid())
        return m_sessionStart.addSecs(qint64(offset) * 60).toLocalTime();
    return QDateTime::currentDateTime();
}

// ============================ Decide what to request ============================
void BleClient::maybeStart() {
    if (!(m_measReady && m_racpReady && m_sessionResolved)) return;
    if (m_resuming) { requestRecords(); return; }

    const bool sameSession = m_cachedSessionStart.isValid() && m_sessionStart.isValid()
            && qAbs(m_cachedSessionStart.secsTo(m_sessionStart)) < 90;
    if (sameSession) {
        int maxOffset = -1;
        for (const auto &r : m_readings) maxOffset = qMax(maxOffset, int(r.offsetMin));
        if (maxOffset >= 0) {
            m_mode = Mode::Incremental;
            m_seenOffsets.clear();
            for (const auto &r : m_readings) m_seenOffsets.insert(int(r.offsetMin));
            int from = maxOffset + 1;
            m_recordsCommand = QByteArray();
            m_recordsCommand.append(char(0x01)); m_recordsCommand.append(char(0x03));
            m_recordsCommand.append(char(0x01));
            m_recordsCommand.append(char(from & 0xFF));
            m_recordsCommand.append(char((from >> 8) & 0xFF));
            m_expectedCount = -1;
            requestRecords();
            return;
        }
    }
    startFullPull();
}

void BleClient::startFullPull() {
    m_mode = Mode::Full;
    m_recordsCommand = QByteArray::fromHex("0101");   // Report Stored Records / All
    setStatus("Counting records...");
    if (m_cgm) {
        auto racp = m_cgm->characteristic(kRacp);
        m_cgm->writeCharacteristic(racp, QByteArray::fromHex("0401")); // Number of Stored Records / All
    }
    m_stallTimer.start(6000);
}

void BleClient::prepareResumeCommand() {
    if (m_mode != Mode::Full) return;
    int maxOff = -1;
    for (const auto &r : m_collecting) maxOff = qMax(maxOff, int(r.offsetMin));
    if (maxOff >= 0) {
        int from = maxOff + 1;
        m_recordsCommand = QByteArray();
        m_recordsCommand.append(char(0x01)); m_recordsCommand.append(char(0x03));
        m_recordsCommand.append(char(0x01));
        m_recordsCommand.append(char(from & 0xFF));
        m_recordsCommand.append(char((from >> 8) & 0xFF));
    }
}

void BleClient::requestRecordCount() { startFullPull(); }

void BleClient::requestRecords() {
    if (!m_cgm) return;
    m_recordsAtRequest = m_collecting.size();
    updateLoadingStatus();
    auto racp = m_cgm->characteristic(kRacp);
    m_cgm->writeCharacteristic(racp, m_recordsCommand);
    m_stallTimer.start(6000);
}

void BleClient::updateLoadingStatus() {
    if (m_mode == Mode::Incremental) {
        setStatus(m_collecting.isEmpty() ? "Checking for new records..."
                  : QString("Syncing %1 new...").arg(m_collecting.size()));
    } else if (m_expectedCount > 0) {
        setStatus(QString("Loading %1 / %2...").arg(m_collecting.size()).arg(m_expectedCount));
    } else {
        setStatus(QString("Loading %1...").arg(m_collecting.size()));
    }
}

// ============================ Notifications ============================
void BleClient::onCharacteristicChanged(const QLowEnergyCharacteristic &c, const QByteArray &v) {
    if (c.uuid() == kMeas) {
        int offset; double mgdl;
        if (!decodeMeasurement(v, offset, mgdl)) return;
        if (m_liveMode) {
            addLive(offset, mgdl);
        } else {
            if (m_seenOffsets.contains(offset)) return;
            m_seenOffsets.insert(offset);
            GlucoseReading r; r.offsetMin = offset; r.mgdl = mgdl; r.date = QDateTime::currentDateTime();
            m_collecting.append(r);
            updateLoadingStatus();
            m_stallTimer.start(6000);
        }
    } else if (c.uuid() == kRacp) {
        if (v.isEmpty()) return;
        const quint8 op = quint8(v.at(0));
        if (op == 0x05) {                       // Number of Stored Records Response
            if (v.size() >= 4)
                m_expectedCount = int(quint16(quint8(v.at(2))) | (quint16(quint8(v.at(3))) << 8));
            m_resuming = true;
            requestRecords();
        } else if (op == 0x06) {                // Completion / Response Code
            handleCompletion(v);
        }
    }
}

void BleClient::handleCompletion(const QByteArray &b) {
    const quint8 rsp = b.size() >= 3 ? quint8(b.at(2)) : 0x01;

    if (rsp == 0x04 || rsp == 0x05 || rsp == 0x09) {   // operator/operand not supported
        if (m_mode == Mode::Incremental) {
            m_collecting.clear(); m_seenOffsets.clear();
            startFullPull();
        } else if (!m_triedResumeFallback) {
            m_triedResumeFallback = true;
            m_recordsCommand = QByteArray::fromHex("0101");
            m_resuming = true;
            requestRecords();
        } else {
            publishResults();
        }
        return;
    }

    m_stallTimer.stop();

    if (rsp == 0x06) { publishResults(); return; }     // no (more) records

    if (m_mode == Mode::Full) {
        bool progressed = m_collecting.size() > m_recordsAtRequest;
        bool needMore = (m_expectedCount < 0) ? progressed : (m_collecting.size() < m_expectedCount);
        if (progressed && needMore && m_attempts < m_maxAttempts) {
            m_attempts++;
            prepareResumeCommand();
            m_resuming = true;
            requestRecords();
            return;
        }
    }
    publishResults();
}

void BleClient::retryOnDrop(const QString &) {
    m_stallTimer.stop();
    if (m_attempts >= m_maxAttempts) { publishResults(); return; }
    m_attempts++;
    prepareResumeCommand();
    if (m_controller && m_controller->state() == QLowEnergyController::DiscoveredState && m_cgm) {
        m_resuming = true;
        requestRecords();
    } else if (m_deviceInfo.isValid()) {
        m_resuming = true;
        m_measReady = false; m_racpReady = false;
        setStatus(QString("Reconnecting (%1 new)...").arg(m_collecting.size()));
        QTimer::singleShot(400, this, [this]() { beginScan(); });
    } else {
        publishResults();
    }
}

// ============================ Publish / merge ============================
void BleClient::updateTrend() {
    if (m_readings.size() >= 2) {
        double d = m_readings[m_readings.size()-1].mgdl - m_readings[m_readings.size()-2].mgdl;
        m_trend = d > 4 ? QStringLiteral("\u2191") : (d < -4 ? QStringLiteral("\u2193") : QStringLiteral("\u2192"));
    } else m_trend.clear();
}

void BleClient::mergeIntoReadings(const QList<GlucoseReading> &newOnes) {
    QMap<qint64, GlucoseReading> map;
    for (const auto &r : m_readings) map.insert(r.date.toMSecsSinceEpoch(), r);
    for (const auto &r : newOnes)    map.insert(r.date.toMSecsSinceEpoch(), r);
    m_readings = map.values();   // QMap keeps keys sorted ascending
    if (!m_readings.isEmpty()) { m_latest = m_readings.last().mgdl; }
    updateTrend();
    emit readingsChanged();
    emit latestChanged(m_latest, m_trend, !m_readings.isEmpty());
    saveCache();
}

void BleClient::publishResults() {
    m_stallTimer.stop();

    // assign real dates to the freshly collected batch
    int maxOffset = 0;
    for (const auto &r : m_collecting) maxOffset = qMax(maxOffset, int(r.offsetMin));
    const QDateTime now = QDateTime::currentDateTime();
    for (auto &r : m_collecting) {
        if (m_sessionStart.isValid())
            r.date = m_sessionStart.addSecs(qint64(r.offsetMin) * 60).toLocalTime();
        else
            r.date = now.addSecs(qint64(r.offsetMin - maxOffset) * 60);
    }

    const int newCount = m_collecting.size();
    mergeIntoReadings(m_collecting);
    if (m_sessionStart.isValid()) m_cachedSessionStart = m_sessionStart;
    saveCache();

    if (m_readings.isEmpty()) {
        setStatus("No records found");
    } else if (m_mode == Mode::Incremental) {
        setStatus(newCount == 0 ? QString("Up to date - %1 records").arg(m_readings.size())
                                : QString("Added %1 - %2 total").arg(newCount).arg(m_readings.size()));
    } else if (m_expectedCount > 0 && newCount < m_expectedCount) {
        setStatus(QString("Loaded %1 records (%2/%3)")
                  .arg(m_readings.size()).arg(newCount).arg(m_expectedCount));
    } else {
        setStatus(QString("Loaded %1 records").arg(m_readings.size()));
    }
    m_gotCompletion = true;

    if (m_wantLive) {
        bool stillConnected = m_controller && m_controller->state() == QLowEnergyController::DiscoveredState;
        if (stillConnected) enterLiveMode();
        scheduleAutoRefresh();
    }
}

void BleClient::enterLiveMode() {
    if (!m_wantLive) return;
    m_liveMode = true;
    m_seenOffsets.clear();
    for (const auto &r : m_readings) m_seenOffsets.insert(int(r.offsetMin));
    if (m_latest > 0)
        setStatus(QString("Live - %1 mg/dL - %2 records").arg(qRound(m_latest)).arg(m_readings.size()));
    else
        setStatus(QString("Live - %1 records").arg(m_readings.size()));
}

void BleClient::addLive(int offset, double mgdl) {
    if (m_seenOffsets.contains(offset)) return;
    m_seenOffsets.insert(offset);
    GlucoseReading r; r.offsetMin = offset; r.mgdl = mgdl; r.date = dateForOffset(offset);
    mergeIntoReadings({r});
    setStatus(QString("Live - %1 - %2 mg/dL")
              .arg(QDateTime::currentDateTime().toString("HH:mm")).arg(qRound(mgdl)));
    scheduleAutoRefresh();
}

// ============================ Auto-refresh / status ============================
void BleClient::scheduleAutoRefresh() {
    m_autoRefreshTimer.stop();
    if (!m_wantLive) { setNextUpdate(QDateTime()); return; }
    setNextUpdate(QDateTime::currentDateTime().addMSecs(m_autoRefreshMs));
    m_autoRefreshTimer.start(m_autoRefreshMs);
}

void BleClient::setStatus(const QString &s) { emit statusChanged(s); }

void BleClient::setNextUpdate(const QDateTime &dt) {
    m_nextUpdateAt = dt;
    emit nextUpdateChanged();
}

// ============================ Persistence ============================
QString BleClient::cacheFilePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/cgm_cache_v2.json";
}

void BleClient::loadCache() {
    QFile f(cacheFilePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return;
    QJsonObject root = doc.object();
    QString ss = root.value("sessionStart").toString();
    if (!ss.isEmpty()) m_cachedSessionStart = QDateTime::fromString(ss, Qt::ISODate);
    m_readings.clear();
    for (const QJsonValue &v : root.value("readings").toArray()) {
        QJsonObject o = v.toObject();
        GlucoseReading r;
        r.offsetMin = o.value("o").toDouble();
        r.mgdl = o.value("v").toDouble();
        r.date = QDateTime::fromString(o.value("t").toString(), Qt::ISODate);
        if (r.date.isValid()) m_readings.append(r);
    }
    std::sort(m_readings.begin(), m_readings.end(),
              [](const GlucoseReading &a, const GlucoseReading &b){ return a.date < b.date; });
    if (!m_readings.isEmpty()) { m_latest = m_readings.last().mgdl; updateTrend(); }
}

void BleClient::saveCache() {
    QJsonArray arr;
    for (const auto &r : m_readings) {
        QJsonObject o;
        o.insert("o", r.offsetMin);
        o.insert("v", r.mgdl);
        o.insert("t", r.date.toString(Qt::ISODate));
        arr.append(o);
    }
    QJsonObject root;
    root.insert("readings", arr);
    QDateTime ss = m_sessionStart.isValid() ? m_sessionStart : m_cachedSessionStart;
    if (ss.isValid()) root.insert("sessionStart", ss.toString(Qt::ISODate));
    QFile f(cacheFilePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.close();
    }
}
