#include "bleclient.h"

#include <QLowEnergyDescriptor>
#include <QStringList>

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

        // Auto-enable notify/indicate via the CCCD (0x2902) so data shows up.
        if (props.testFlag(QLowEnergyCharacteristic::Notify)
            || props.testFlag(QLowEnergyCharacteristic::Indicate)) {
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
}

void BleClient::onCharacteristicChanged(const QLowEnergyCharacteristic &c,
                                        const QByteArray &value)
{
    log(QStringLiteral("NOTIFY %1 = %2")
            .arg(c.uuid().toString(), QString::fromLatin1(value.toHex())));
}

void BleClient::onServiceError(QLowEnergyService::ServiceError error)
{
    log(QStringLiteral("Service error code: %1 "
                       "(often means encryption/authorization required)")
            .arg(static_cast<int>(error)));
}

void BleClient::log(const QString &line)
{
    emit logLine(line);
}
