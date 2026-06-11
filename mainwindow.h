#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QBluetoothDeviceInfo>

class BleScanner;
class BleClient;
class WinPairing;
class BatteryWidget;
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
    void connectToInfo(const QBluetoothDeviceInfo &info);

    BleScanner *m_scanner = nullptr;
    BleClient  *m_client  = nullptr;
    WinPairing *m_pairing = nullptr;

    QLabel        *m_statusPill   = nullptr;
    BatteryWidget *m_battery      = nullptr;
    QLabel      *m_glucoseValue = nullptr;
    QLabel      *m_glucoseUnit  = nullptr;
    QLabel      *m_rangeLabel   = nullptr;
    QLabel      *m_updatedLabel = nullptr;
    QLineEdit   *m_passkeyEdit  = nullptr;
    QPushButton *m_actionButton = nullptr;
    QLabel      *m_deviceLabel  = nullptr;

    QBluetoothDeviceInfo m_lastTarget;
    QString m_pairPin;
    QString m_knownName;

    State m_state = State::Idle;
    bool m_scanning = false;
    bool m_pairArmed = false;
    bool m_bonded = false;
    int  m_attempts = 0;
    int  m_pairSightings = 0;
};

#endif // MAINWINDOW_H
