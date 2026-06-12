#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTimer>
#include "bleclient.h"
#include "glucosechart.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void onLatest(double mgdl, const QString &trend, bool valid);
    void onReadings();
    void onStatus(const QString &s);
    void onMonitoring(bool on);
    void onNextUpdate();
    void tickCountdown();
    void selectRange(int idx);

    BleClient    *m_ble = nullptr;
    GlucoseChart *m_chart = nullptr;

    QLabel  *m_title = nullptr;
    QLabel  *m_status = nullptr;
    QLabel  *m_countdown = nullptr;
    QLabel  *m_value = nullptr;
    QLabel  *m_unit = nullptr;
    QLabel  *m_avg = nullptr;
    QLabel  *m_device = nullptr;
    QLineEdit *m_pinEdit = nullptr;

    QPushButton *m_primary = nullptr;
    QPushButton *m_refresh = nullptr;
    QPushButton *m_btn8 = nullptr;
    QPushButton *m_btn24 = nullptr;
    QPushButton *m_btnAll = nullptr;

    QTimer m_countdownTimer;
    int    m_range = 2;   // 0=8h, 1=24h, 2=all
};

#endif // MAINWINDOW_H
