#ifndef WINPAIRING_H
#define WINPAIRING_H

#include <QObject>
#include <QString>

// Pairs a BLE device by address, supplying a PIN/passkey, using the native
// WinRT DeviceInformationCustomPairing API. This is needed on Windows because
// Qt's QBluetoothLocalDevice does not support pairing there. Windows-only.
class WinPairing : public QObject
{
    Q_OBJECT
public:
    explicit WinPairing(QObject *parent = nullptr);

    // address: 48-bit BLE address as uint64 (QBluetoothAddress::toUInt64()).
    // pin: the passkey printed on the sensor box.
    // forceRepair: if already paired, first remove the (possibly stale) Windows
    //   bond and pair again from scratch.
    void pair(quint64 address, const QString &pin, bool forceRepair = false);

signals:
    void logLine(const QString &line);
    void finished(bool success);
};

#endif // WINPAIRING_H
