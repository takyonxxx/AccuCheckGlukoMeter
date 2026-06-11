#ifndef BLESCANNER_H
#define BLESCANNER_H

#include <QObject>
#include <QString>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>

// Thin wrapper around QBluetoothDeviceDiscoveryAgent that performs a BLE-only
// scan and flags devices whose name (or identifier) matches a configurable
// filter. The filter default targets the Accu-Chek SmartGuide sensor.
class BleScanner : public QObject
{
    Q_OBJECT
public:
    explicit BleScanner(QObject *parent = nullptr);
    ~BleScanner() override;

    // Case-insensitive substring used to flag the target sensor.
    // Examples: "SmartGuide", "Accu-Chek", or a serial fragment like "960570".
    void setNameFilter(const QString &filter);
    QString nameFilter() const;

    bool isScanning() const;

public slots:
    void startScan(int timeoutMs = 15000);
    void stopScan();

signals:
    void deviceFound(const QBluetoothDeviceInfo &info, bool isTarget);
    void deviceLogLine(const QString &line);
    void scanStarted();
    void scanFinished();
    void errorOccurred(const QString &message);

private slots:
    void onDeviceDiscovered(const QBluetoothDeviceInfo &info);
    void onDeviceUpdated(const QBluetoothDeviceInfo &info,
                         QBluetoothDeviceInfo::Fields fields);
    void onFinished();
    void onErrorOccurred(QBluetoothDeviceDiscoveryAgent::Error error);

private:
    bool matchesTarget(const QBluetoothDeviceInfo &info) const;

    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    QString m_nameFilter;
};

#endif // BLESCANNER_H
