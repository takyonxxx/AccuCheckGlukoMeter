#ifndef BLECLIENT_H
#define BLECLIENT_H

#include <QObject>
#include <QString>
#include <QList>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QLowEnergyCharacteristic>
#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>

// Connects to a selected BLE device (central role), discovers the full GATT
// tree, logs every service/characteristic/property, and auto-enables
// notify/indicate so incoming data is visible in the log.
class BleClient : public QObject
{
    Q_OBJECT
public:
    explicit BleClient(QObject *parent = nullptr);
    ~BleClient() override;

    void connectToDevice(const QBluetoothDeviceInfo &info);
    void disconnectFromDevice();

signals:
    void logLine(const QString &line);
    void connected();
    void disconnected();
    void connectionFailed();

private slots:
    void onConnected();
    void onDisconnected();
    void onControllerError(QLowEnergyController::Error error);
    void onServiceDiscovered(const QBluetoothUuid &serviceUuid);
    void onDiscoveryFinished();
    void onServiceStateChanged(QLowEnergyService::ServiceState newState);
    void onCharacteristicChanged(const QLowEnergyCharacteristic &c,
                                 const QByteArray &value);
    void onServiceError(QLowEnergyService::ServiceError error);

private:
    void log(const QString &line);

    QLowEnergyController *m_controller = nullptr;
    QList<QLowEnergyService *> m_services;
};

#endif // BLECLIENT_H
