#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QVector>
#include <QPointF>
#include <QDateTime>
#include <QBluetoothDeviceInfo>

class BleScanner;
class BleClient;
class WinPairing;
class GlucoseChart;
class QPushButton;
class QLineEdit;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onActionClicked();
    void onDeviceFound(const QBluetoothDeviceInfo &info, bool isTarget);
    void onScanFinished();
    void onGlucose(double mgdl);

private:
    enum class State { Idle, Searching, Pairing, Connecting, Connected, Live };

    void setState(State s, const QString &detail = QString());
    void startFlow();
    void connectToInfo(const QBluetoothDeviceInfo &info);

    BleScanner *m_scanner = nullptr;
    BleClient  *m_client  = nullptr;
    WinPairing *m_pairing = nullptr;

    QLabel        *m_statusPill   = nullptr;
    QLabel      *m_glucoseValue = nullptr;
    QLabel      *m_glucoseUnit  = nullptr;
    QLabel      *m_rangeLabel   = nullptr;
    QLabel      *m_updatedLabel = nullptr;
    GlucoseChart *m_chart       = nullptr;
    QLineEdit   *m_passkeyEdit  = nullptr;
    QPushButton *m_actionButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QLabel      *m_deviceLabel  = nullptr;

    QVector<QPointF> m_series;       // accumulated readings for the chart
    QDateTime m_firstReading;        // time of the first reading (chart x origin)
    bool m_haveData = false;

    QBluetoothDeviceInfo m_lastTarget;
    QString m_pairPin;
    QString m_knownName;

    State m_state = State::Idle;
    bool m_scanning = false;
    bool m_pairArmed = false;
    bool m_bonded = false;
    bool m_didForceRepair = false;
    int  m_attempts = 0;
    int  m_pairSightings = 0;
    int  m_staleSkips = 0;
};

#endif // MAINWINDOW_H
