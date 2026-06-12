#ifndef WINPAIRING_H
#define WINPAIRING_H

#include <QObject>
#include <QtBluetooth/QBluetoothAddress>
#include <QString>

// Programmatic BLE pairing with a supplied PIN.
//  - On Windows: uses WinRT DeviceInformationCustomPairing (ProvidePin/ConfirmOnly).
//  - On other platforms: no-op (the OS handles pairing) so the build stays portable.
class WinPairing : public QObject {
    Q_OBJECT
public:
    explicit WinPairing(QObject *parent = nullptr);

    // Pair `address` providing `pin`. `forceRepair` unpairs first (use with care).
    void pair(const QBluetoothAddress &address, const QString &pin, bool forceRepair = false);

signals:
    void finished(bool success, const QString &message);
};

#endif // WINPAIRING_H
