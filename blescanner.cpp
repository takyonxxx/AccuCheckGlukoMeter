#include "blescanner.h"

#include <QDebug>
#include <QStringList>
#include <QList>
#include <QByteArray>
#include <QBluetoothUuid>

// Builds one copyable log line describing a discovered BLE device.
// tag: short marker, e.g. "new" or "upd".
static QString formatDevice(const QBluetoothDeviceInfo &info, bool target,
                            const QString &tag)
{
    const QString name = info.name().isEmpty() ? QStringLiteral("(no name)")
                                               : info.name();
    const QString id = info.address().isNull() ? info.deviceUuid().toString()
                                               : info.address().toString();

    // Manufacturer data: company id -> raw bytes (hex). Useful to spot Roche
    // and to seed the payload-decoding step later.
    QStringList manuf;
    const QList<quint16> manufIds = info.manufacturerIds();
    for (quint16 cid : manufIds) {
        const QByteArray data = info.manufacturerData(cid);
        manuf << QStringLiteral("0x%1=%2")
                     .arg(static_cast<uint>(cid), 4, 16, QLatin1Char('0'))
                     .arg(QString::fromLatin1(data.toHex()));
    }

    // Service UUIDs advertised (often empty until GATT discovery).
    QStringList svcs;
    const QList<QBluetoothUuid> uuids = info.serviceUuids();
    for (const QBluetoothUuid &u : uuids)
        svcs << u.toString();

    return QStringLiteral(
               "[BLE %1] name=%2 | id=%3 | rssi=%4 | manuf=[%5] | services=[%6]%7")
        .arg(tag,
             name,
             id,
             QString::number(info.rssi()),
             manuf.isEmpty() ? QStringLiteral("-") : manuf.join(QStringLiteral("; ")),
             svcs.isEmpty() ? QStringLiteral("-") : svcs.join(QStringLiteral("; ")),
             target ? QStringLiteral(" | <-- TARGET") : QString());
}

BleScanner::BleScanner(QObject *parent)
    : QObject(parent)
    , m_agent(new QBluetoothDeviceDiscoveryAgent(this))
    , m_nameFilter(QStringLiteral("SmartGuide"))
{
    m_agent->setLowEnergyDiscoveryTimeout(15000);

    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BleScanner::onDeviceDiscovered);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceUpdated,
            this, &BleScanner::onDeviceUpdated);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BleScanner::onFinished);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::canceled,
            this, &BleScanner::onFinished);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, &BleScanner::onErrorOccurred);
}

BleScanner::~BleScanner()
{
    if (m_agent && m_agent->isActive())
        m_agent->stop();
}

void BleScanner::setNameFilter(const QString &filter)
{
    m_nameFilter = filter.trimmed();
}

QString BleScanner::nameFilter() const
{
    return m_nameFilter;
}

bool BleScanner::isScanning() const
{
    return m_agent && m_agent->isActive();
}

void BleScanner::startScan(int timeoutMs)
{
    if (m_agent->isActive()) {
        qDebug() << "[BleScanner] Scan already running";
        return;
    }
    m_agent->setLowEnergyDiscoveryTimeout(timeoutMs);
    qDebug() << "[BleScanner] Starting BLE scan, timeout(ms):" << timeoutMs
             << "filter:" << m_nameFilter;
    emit scanStarted();
    m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void BleScanner::stopScan()
{
    if (m_agent->isActive()) {
        qDebug() << "[BleScanner] Stopping BLE scan";
        m_agent->stop();
    }
}

void BleScanner::onDeviceDiscovered(const QBluetoothDeviceInfo &info)
{
    const bool ble = info.coreConfigurations()
                     & QBluetoothDeviceInfo::LowEnergyCoreConfiguration;
    if (!ble)
        return; // central role: only BLE devices are of interest

    const bool target = matchesTarget(info);
    const QString line = formatDevice(info, target, QStringLiteral("new"));
    qDebug().noquote() << line;
    emit deviceLogLine(line);
    emit deviceFound(info, target);
}

void BleScanner::onDeviceUpdated(const QBluetoothDeviceInfo &info,
                                 QBluetoothDeviceInfo::Fields fields)
{
    const bool ble = info.coreConfigurations()
                     & QBluetoothDeviceInfo::LowEnergyCoreConfiguration;
    if (!ble)
        return;

    // On Windows the manufacturer/service data often arrives via updates rather
    // than the first discovery. Log those (but not plain RSSI-only updates, to
    // keep the copyable list clean).
    if (fields.testFlag(QBluetoothDeviceInfo::Field::ManufacturerData)
        || fields.testFlag(QBluetoothDeviceInfo::Field::ServiceData)) {
        const QString line =
            formatDevice(info, matchesTarget(info), QStringLiteral("upd"));
        qDebug().noquote() << line;
        emit deviceLogLine(line);
    }

    // Re-emit so the UI can refresh RSSI / service data for known rows.
    emit deviceFound(info, matchesTarget(info));
}

void BleScanner::onFinished()
{
    qDebug() << "[BleScanner] Scan finished";
    emit scanFinished();
}

void BleScanner::onErrorOccurred(QBluetoothDeviceDiscoveryAgent::Error error)
{
    const QString msg = m_agent->errorString();
    qWarning().noquote() << "[BleScanner] Error:" << error << msg;
    emit errorOccurred(msg);
}

bool BleScanner::matchesTarget(const QBluetoothDeviceInfo &info) const
{
    // 1) Strongest signal: standard glucose-related GATT services in the
    //    advertisement. Works regardless of device name, and even with an
    //    empty name filter.
    static const QList<QBluetoothUuid> glucoseServices = {
        QBluetoothUuid(quint16(0x1808)), // Glucose
        QBluetoothUuid(quint16(0x181F)), // Continuous Glucose Monitoring
        QBluetoothUuid(quint16(0x181A))  // Environmental Sensing (some CGMs)
    };
    const QList<QBluetoothUuid> advertised = info.serviceUuids();
    for (const QBluetoothUuid &svc : advertised) {
        if (glucoseServices.contains(svc))
            return true;
    }

    // 2) Name / identifier substring filter (e.g. "SmartGuide" or serial frag).
    if (m_nameFilter.isEmpty())
        return false;

    if (info.name().contains(m_nameFilter, Qt::CaseInsensitive))
        return true;

    // Some sensors expose part of the serial in the advertised identifier.
    const QString idText = info.deviceUuid().toString();
    if (!idText.isEmpty() && idText.contains(m_nameFilter, Qt::CaseInsensitive))
        return true;

    return false;
}
