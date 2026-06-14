#ifndef BLECLIENT_H
#define BLECLIENT_H

#include <QObject>
#include <QtBluetooth/QLowEnergyController>
#include <QtBluetooth/QLowEnergyService>
#include <QtBluetooth/QBluetoothDeviceInfo>
#include <QDateTime>
#include <QTimer>
#include <QSet>
#include <QList>
#include "glucosereading.h"
#include "blescanner.h"
#include "winpairing.h"

// Cross-platform port of the iOS BLEManager. Connects to the Accu-Chek
// SmartGuide CGM, pulls history (with resume + record-count checks), caches
// locally, fetches only newer records on refresh, and keeps the value current
// via live notifications plus a 5-minute auto-refresh.
class BleClient : public QObject {
    Q_OBJECT
public:
    explicit BleClient(QObject *parent = nullptr);

    void connectAndLoad();   // user starts monitoring
    void refresh();          // re-sync (incremental when possible)
    void setPin(const QString &pin);
    void stop();             // stop monitoring + disconnect

    QList<GlucoseReading> readings() const { return m_readings; }
    bool      monitoring() const { return m_monitoring; }
    QDateTime nextUpdateAt() const { return m_nextUpdateAt; }
    QString   deviceName() const { return m_deviceName; }

signals:
    void statusChanged(const QString &text);
    void readingsChanged();
    void latestChanged(double mgdl, const QString &trend, bool valid);
    void monitoringChanged(bool on);
    void connectedChanged(bool on);
    void nextUpdateChanged();
    void deviceNameChanged(const QString &name);

private:
    enum class Mode { Full, Incremental };

    // ---- flow ----
    void startScan();
    void resetFetchState();
    void beginScan();
    void onScannerFound(const QBluetoothDeviceInfo &info);
    void connectToDevice(const QBluetoothDeviceInfo &info);
    void cleanupController();

    void onServiceDiscovered(const QBluetoothUuid &uuid);
    void onDiscoveryFinished();
    void onServiceStateChanged(QLowEnergyService::ServiceState s);
    void onCharacteristicChanged(const QLowEnergyCharacteristic &c, const QByteArray &v);
    void onCharacteristicRead(const QLowEnergyCharacteristic &c, const QByteArray &v);
    void onDescriptorWritten(const QLowEnergyDescriptor &d, const QByteArray &v);

    void maybeStart();
    void startFullPull();
    void prepareResumeCommand();
    void requestRecords();
    void requestRecordCount();
    void handleCompletion(const QByteArray &b);
    void retryOnDrop(const QString &reason);
    void publishResults();
    void enterLiveMode();
    void addLive(int offset, double mgdl);

    // ---- helpers ----
    static double sfloat(quint16 raw);
    static bool   decodeMeasurement(const QByteArray &data, int &offset, double &mgdl);
    void   parseSessionStart(const QByteArray &data);
    QDateTime dateForOffset(int offset) const;
    void   updateTrend();
    void   mergeIntoReadings(const QList<GlucoseReading> &newOnes);
    void   updateLoadingStatus();
    void   scheduleAutoRefresh();
    void   setStatus(const QString &s);
    void   setNextUpdate(const QDateTime &dt);

    // ---- persistence ----
    QString cacheFilePath() const;
    void loadCache();
    void saveCache();

    // ---- members ----
    BleScanner           *m_scanner = nullptr;
    WinPairing           *m_winPairing = nullptr;
    QString               m_pin = QStringLiteral("784651");
    QLowEnergyController *m_controller = nullptr;
    QLowEnergyService    *m_cgm = nullptr;
    QLowEnergyService    *m_gap = nullptr;
    QBluetoothDeviceInfo  m_deviceInfo;

    bool m_measReady = false;
    bool m_racpReady = false;
    int  m_cccdWritten = 0;
    int  m_cccdExpected = 0;
    bool m_sessionResolved = false;
    QDateTime m_sessionStart;          // absolute instant of session start
    QDateTime m_cachedSessionStart;

    Mode m_mode = Mode::Full;
    QByteArray m_recordsCommand;
    QList<GlucoseReading> m_collecting;
    QSet<int> m_seenOffsets;
    int  m_expectedCount = -1;
    int  m_attempts = 0;
    const int m_maxAttempts = 40;
    bool m_gotCompletion = false;
    bool m_resuming = false;
    bool m_triedResumeFallback = false;
    int  m_recordsAtRequest = 0;

    QList<GlucoseReading> m_readings;  // master, persisted
    double  m_latest = 0;
    QString m_trend;
    QString m_deviceName;

    bool m_monitoring = false;
    bool m_wantLive = false;
    bool m_liveMode = false;
    const int m_autoRefreshMs = 300000;   // 5 minutes
    QDateTime m_nextUpdateAt;

    QTimer m_scanTimeout;
    QTimer m_sessionTimeout;
    QTimer m_stallTimer;
    QTimer m_autoRefreshTimer;
};

#endif // BLECLIENT_H
