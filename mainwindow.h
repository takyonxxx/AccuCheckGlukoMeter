#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QHash>
#include <QString>
#include <QBluetoothDeviceInfo>

class BleScanner;
class BleClient;
class WinPairing;
class QTableWidget;
class QPushButton;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QPlainTextEdit;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onScanClicked();
    void onConnectClicked();
    void onPairClicked();
    void onSaveLogClicked();
    void onDeviceFound(const QBluetoothDeviceInfo &info, bool isTarget);
    void onScanStarted();
    void onScanFinished();
    void onError(const QString &message);
    void onGlucose(double mgdl);

private:
    QString deviceId(const QBluetoothDeviceInfo &info) const;
    int rowForId(const QString &id);
    void log(const QString &line);
    void connectToInfo(const QBluetoothDeviceInfo &info);

    BleScanner *m_scanner = nullptr;
    BleClient  *m_client  = nullptr;
    WinPairing *m_pairing = nullptr;

    QLabel         *m_glucoseValue  = nullptr;
    QLabel         *m_glucoseUnit   = nullptr;

    QTableWidget   *m_table         = nullptr;
    QPushButton    *m_scanButton    = nullptr;
    QPushButton    *m_connectButton = nullptr;
    QPushButton    *m_pairButton    = nullptr;
    QPushButton    *m_saveButton    = nullptr;
    QLineEdit      *m_filterEdit    = nullptr;
    QLineEdit      *m_passkeyEdit   = nullptr;
    QSpinBox       *m_timeoutSpin   = nullptr;
    QCheckBox      *m_autoConnect   = nullptr;
    QPlainTextEdit *m_logView       = nullptr;
    QLabel         *m_statusLabel   = nullptr;

    QHash<QString, int> m_rowById;
    QHash<QString, QBluetoothDeviceInfo> m_deviceById;
    QBluetoothDeviceInfo m_lastTarget;
    QString m_pairPin;

    bool m_scanning = false;
    bool m_connecting = false;
    bool m_pairArmed = false;
    int  m_connectAttempts = 0;
};

#endif // MAINWINDOW_H
