#ifndef BLESCANNER_H
#define BLESCANNER_H

#include <QObject>
#include <QtBluetooth/QBluetoothDeviceDiscoveryAgent>
#include <QtBluetooth/QBluetoothDeviceInfo>

// Scans for the Accu-Chek SmartGuide sensor. The sensor advertises a privacy
// address and only sometimes a name, so we match on either the "AC-" name
// prefix or the CGM service (0x181F).
class BleScanner : public QObject {
    Q_OBJECT
public:
    explicit BleScanner(QObject *parent = nullptr);
    void start();
    void stop();

signals:
    void found(const QBluetoothDeviceInfo &info);
    void finishedWithoutTarget();

private:
    void onDeviceDiscovered(const QBluetoothDeviceInfo &info);
    void onFinished();

    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    bool m_matched = false;
};

#endif // BLESCANNER_H
