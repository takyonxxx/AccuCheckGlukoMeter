#include "bleclient.h"

#include <QLowEnergyDescriptor>
#include <QStringList>
#include <QTimer>
#include <cmath>

// IEEE-11073 16-bit SFLOAT (medfloat16) used by the CGM Measurement char.
static double sfloatToDouble(quint16 v)
{
    int mantissa = v & 0x0FFF;
    int exponent = (v >> 12) & 0x000F;
    if (mantissa >= 0x0800) mantissa -= 0x1000; // signed 12-bit
    if (exponent >= 0x0008) exponent -= 0x0010; // signed 4-bit
    return mantissa * std::pow(10.0, exponent);
}

BleClient::BleClient(QObject *parent)
    : QObject(parent)
{
}

BleClient::~BleClient()
{
    disconnectFromDevice();
}

void BleClient::connectToDevice(const QBluetoothDeviceInfo &info)
{
    disconnectFromDevice();

    const QString id = info.address().isNull() ? info.deviceUuid().toString()
                                               : info.address().toString();
    log(QStringLiteral("Connecting to %1 [%2] ...")
            .arg(info.name().isEmpty() ? QStringLiteral("(no name)") : info.name(),
                 id));

    m_controller = QLowEnergyController::createCentral(info, this);

    connect(m_controller, &QLowEnergyController::connected,
            this, &BleClient::onConnected);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &BleClient::onDisconnected);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &BleClient::onControllerError);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &BleClient::onServiceDiscovered);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &BleClient::onDiscoveryFinished);

    m_controller->connectToDevice();
}

void BleClient::disconnectFromDevice()
{
    m_cgmService = nullptr;
    m_bulkLoading = false;
    qDeleteAll(m_services);
    m_services.clear();

    if (m_controller) {
        m_controller->disconnectFromDevice();
        m_controller->deleteLater();
        m_controller = nullptr;
    }
}

void BleClient::onConnected()
{
    log(QStringLiteral("Connected. Discovering services..."));
    emit connected();
    m_controller->discoverServices();
}

void BleClient::onDisconnected()
{
    log(QStringLiteral("Disconnected."));
    if (!m_history.isEmpty())
        emit historyReady(m_history);
    emit disconnected();
}

void BleClient::onControllerError(QLowEnergyController::Error error)
{
    Q_UNUSED(error);
    log(QStringLiteral("Controller error: %1")
            .arg(m_controller ? m_controller->errorString()
                              : QStringLiteral("unknown")));
    emit connectionFailed();
}

void BleClient::onServiceDiscovered(const QBluetoothUuid &serviceUuid)
{
    log(QStringLiteral("Service found: %1").arg(serviceUuid.toString()));
}

void BleClient::onDiscoveryFinished()
{
    log(QStringLiteral("Service discovery finished. Reading details..."));

    const QList<QBluetoothUuid> uuids = m_controller->services();
    for (const QBluetoothUuid &u : uuids) {
        QLowEnergyService *svc = m_controller->createServiceObject(u, this);
        if (!svc)
            continue;
        m_services.append(svc);
        connect(svc, &QLowEnergyService::stateChanged,
                this, &BleClient::onServiceStateChanged);
        connect(svc, &QLowEnergyService::characteristicChanged,
                this, &BleClient::onCharacteristicChanged);
        connect(svc, &QLowEnergyService::errorOccurred,
                this, &BleClient::onServiceError);
        connect(svc, &QLowEnergyService::descriptorWritten, this,
                [this](const QLowEnergyDescriptor &d, const QByteArray &v) {
                    log(QStringLiteral("CCCD written (%1) = %2 -> notifications ON")
                            .arg(d.uuid().toString(),
                                 QString::fromLatin1(v.toHex())));
                });
        connect(svc, &QLowEnergyService::characteristicRead, this,
                [this](const QLowEnergyCharacteristic &c, const QByteArray &v) {
                    log(QStringLiteral("READ %1 = %2")
                            .arg(c.uuid().toString(),
                                 QString::fromLatin1(v.toHex())));
                });
        svc->discoverDetails();
    }
}

void BleClient::onServiceStateChanged(QLowEnergyService::ServiceState newState)
{
    if (newState != QLowEnergyService::RemoteServiceDiscovered)
        return;

    auto *svc = qobject_cast<QLowEnergyService *>(sender());
    if (!svc)
        return;

    log(QStringLiteral("=== Service %1 ===").arg(svc->serviceUuid().toString()));

    const QList<QLowEnergyCharacteristic> chars = svc->characteristics();
    for (const QLowEnergyCharacteristic &c : chars) {
        const QLowEnergyCharacteristic::PropertyTypes props = c.properties();

        QStringList p;
        if (props.testFlag(QLowEnergyCharacteristic::Read))
            p << QStringLiteral("Read");
        if (props.testFlag(QLowEnergyCharacteristic::Write))
            p << QStringLiteral("Write");
        if (props.testFlag(QLowEnergyCharacteristic::WriteNoResponse))
            p << QStringLiteral("WriteNoResp");
        if (props.testFlag(QLowEnergyCharacteristic::Notify))
            p << QStringLiteral("Notify");
        if (props.testFlag(QLowEnergyCharacteristic::Indicate))
            p << QStringLiteral("Indicate");

        const QByteArray val = c.value();
        log(QStringLiteral("  char %1 [%2]%3")
                .arg(c.uuid().toString(),
                     p.join(QStringLiteral(",")),
                     val.isEmpty()
                         ? QString()
                         : QStringLiteral(" value=") + QString::fromLatin1(val.toHex())));

        // Battery Level (0x2A19): first byte is percent 0..100.
        if (c.uuid() == QBluetoothUuid(quint16(0x2A19)) && !val.isEmpty())
            emit batteryLevel(static_cast<quint8>(val[0]));

        // Device Name (0x2A00): plain string, e.g. "AC-1R000960570".
        if (c.uuid() == QBluetoothUuid(quint16(0x2A00)) && !val.isEmpty())
            emit deviceName(QString::fromUtf8(val));

        // Only enable the two characteristics we actually use: CGM Measurement
        // (0x2AA7 notify) and the Record Access Control Point (0x2A52 indicate).
        // Subscribing to bond-management / SOCP adds GATT traffic that can make
        // this sensor drop the link, so we leave those alone.
        const bool wanted =
            (c.uuid() == QBluetoothUuid(quint16(0x2AA7)))
            || (c.uuid() == QBluetoothUuid(quint16(0x2A52)));
        if (wanted && (props.testFlag(QLowEnergyCharacteristic::Notify)
                       || props.testFlag(QLowEnergyCharacteristic::Indicate))) {
            const QLowEnergyDescriptor cccd = c.descriptor(
                QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
            if (cccd.isValid()) {
                const bool notify = props.testFlag(QLowEnergyCharacteristic::Notify);
                const QByteArray enable = notify ? QByteArray::fromHex("0100")
                                                 : QByteArray::fromHex("0200");
                svc->writeDescriptor(cccd, enable);
                log(QStringLiteral("    -> enabling %1 on %2")
                        .arg(notify ? QStringLiteral("notify")
                                    : QStringLiteral("indicate"),
                             c.uuid().toString()));
            }
        }
    }

    // On the CGM service, once notify/indicate are armed, ask the sensor to
    // report ALL stored records so we can plot the history. The most recent
    // record also updates the big live value.
    if (svc->serviceUuid() == QBluetoothUuid(quint16(0x181F))) {
        m_cgmService = svc;
        QTimer::singleShot(1200, this, [this]() { requestLastRecord(); });
    }
}

void BleClient::requestLastRecord()
{
    if (!m_cgmService)
        return;
    const QLowEnergyCharacteristic racp =
        m_cgmService->characteristic(QBluetoothUuid(quint16(0x2A52)));
    if (!racp.isValid()) {
        log(QStringLiteral("RACP characteristic not found"));
        return;
    }
    m_history.clear();
    // RACP op-code 0x01 (Report Stored Records), operator 0x06 (Last Record).
    // Asking for a single record is far gentler than "all records", which this
    // sensor tends to refuse by closing the link.
    const QByteArray cmd = QByteArray::fromHex("0106");
    log(QStringLiteral("Writing RACP 01 06 (report last record)"));
    m_cgmService->writeCharacteristic(racp, cmd,
                                      QLowEnergyService::WriteWithResponse);
}

void BleClient::onCharacteristicChanged(const QLowEnergyCharacteristic &c,
                                        const QByteArray &value)
{
    // CGM Measurement: [0]=size [1]=flags [2..3]=glucose SFLOAT(LE) mg/dL
    //                  [4..5]=time offset (minutes, uint16 LE) ...
    if (c.uuid() == QBluetoothUuid(quint16(0x2AA7)) && value.size() >= 4) {
        const quint16 raw =
            static_cast<quint8>(value[2])
            | (static_cast<quint16>(static_cast<quint8>(value[3])) << 8);
        const double mgdl = sfloatToDouble(raw);

        double offsetMin = 0.0;
        if (value.size() >= 6) {
            const quint16 off =
                static_cast<quint8>(value[4])
                | (static_cast<quint16>(static_cast<quint8>(value[5])) << 8);
            offsetMin = off;
        } else {
            offsetMin = m_history.isEmpty() ? 0.0 : m_history.last().x() + 5.0;
        }

        m_history.append(QPointF(offsetMin, mgdl));

        // During the RACP bulk dump, stay silent (no per-record logging or UI
        // updates) so the whole history streams in as fast as the link allows;
        // we draw it once when the batch finishes. Only live (non-bulk)
        // notifications update the big value immediately.
        if (!m_bulkLoading) {
            log(QStringLiteral("NOTIFY glucose = %1 mg/dL @ %2 min")
                    .arg(mgdl, 0, 'f', 0).arg(offsetMin, 0, 'f', 0));
            emit glucoseValue(mgdl);
        }
        return;
    }

    // RACP (0x2A52) indication: op-code 0x06 = response code => request done.
    if (c.uuid() == QBluetoothUuid(quint16(0x2A52)) && !value.isEmpty()
        && static_cast<quint8>(value[0]) == 0x06) {
        m_bulkLoading = false;
        log(QStringLiteral("RACP done. Records this fetch: %1").arg(m_history.size()));
        return;
    }

    log(QStringLiteral("NOTIFY %1 = %2")
            .arg(c.uuid().toString(), QString::fromLatin1(value.toHex())));

    // Battery Level notify.
    if (c.uuid() == QBluetoothUuid(quint16(0x2A19)) && !value.isEmpty())
        emit batteryLevel(static_cast<quint8>(value[0]));
}

void BleClient::onServiceError(QLowEnergyService::ServiceError error)
{
    log(QStringLiteral("Service error code: %1 "
                       "(often means encryption/authorization required)")
            .arg(static_cast<int>(error)));
}

void BleClient::log(const QString &line)
{
    qDebug().noquote() << "[BleClient]" << line;
    emit logLine(line);
}
