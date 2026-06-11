#include "mainwindow.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>

static QString colorFor(double v) {
    if (v < 70) return "#D64545";
    if (v > 180) return "#E08A2B";
    return "#3DA35D";
}
static QString rangeLabel(double v) {
    if (v < 70) return "Low";
    if (v > 180) return "High";
    return "In range";
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Accu-Chek SmartGuide");
    resize(560, 720);

    auto *central = new QWidget(this);
    central->setStyleSheet("background:#12161F;");
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    m_title = new QLabel("Accu-Chek SmartGuide");
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setStyleSheet("color:#CBCBCB; font-size:16px; font-weight:600;");
    root->addWidget(m_title);

    m_status = new QLabel("Tap Connect to start");
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setStyleSheet("color:#EAEAEA; background:#222C3D; border-radius:12px; padding:6px 14px;");
    auto *statusRow = new QHBoxLayout();
    statusRow->addStretch(); statusRow->addWidget(m_status); statusRow->addStretch();
    root->addLayout(statusRow);

    m_countdown = new QLabel("");
    m_countdown->setAlignment(Qt::AlignCenter);
    m_countdown->setStyleSheet("color:#9AA3AE; font-size:11px;");
    root->addWidget(m_countdown);

    m_value = new QLabel("--");
    m_value->setAlignment(Qt::AlignCenter);
    m_value->setStyleSheet("color:#666; font-size:72px; font-weight:700;");
    root->addWidget(m_value);

    m_unit = new QLabel("mg/dL");
    m_unit->setAlignment(Qt::AlignCenter);
    m_unit->setStyleSheet("color:#888; font-size:14px;");
    root->addWidget(m_unit);

    m_avg = new QLabel("");
    m_avg->setAlignment(Qt::AlignCenter);
    m_avg->setStyleSheet("color:#C7CDD6; font-size:13px; font-weight:600;");
    root->addWidget(m_avg);

    m_chart = new GlucoseChart(this);
    m_chart->setMinimumHeight(260);
    root->addWidget(m_chart, 1);

    // range buttons
    auto *rangeRow = new QHBoxLayout();
    auto mkRange = [&](const QString &t) {
        auto *b = new QPushButton(t);
        b->setCheckable(true);
        b->setStyleSheet(
            "QPushButton{color:#CFCFCF;background:#1B2331;border:1px solid #2C3850;"
            "padding:6px 10px;border-radius:6px;}"
            "QPushButton:checked{background:#2974D1;color:white;border:1px solid #2974D1;}");
        rangeRow->addWidget(b);
        return b;
    };
    m_btn8  = mkRange("8 Saat");
    m_btn24 = mkRange("24 Saat");
    m_btnAll = mkRange("Tumu");
    root->addLayout(rangeRow);
    connect(m_btn8,  &QPushButton::clicked, this, [this]{ selectRange(0); });
    connect(m_btn24, &QPushButton::clicked, this, [this]{ selectRange(1); });
    connect(m_btnAll, &QPushButton::clicked, this, [this]{ selectRange(2); });

    // primary + refresh
    m_primary = new QPushButton("Connect & Load");
    m_primary->setMinimumHeight(48);
    m_primary->setStyleSheet(
        "QPushButton{background:#3475F0;color:white;font-size:15px;font-weight:600;border-radius:10px;}");
    root->addWidget(m_primary);

    m_refresh = new QPushButton("Refresh");
    m_refresh->setMinimumHeight(40);
    m_refresh->setStyleSheet(
        "QPushButton{color:#E0E0E0;background:transparent;border:1px solid #4A4A4A;border-radius:10px;}");
    root->addWidget(m_refresh);

    m_device = new QLabel("");
    m_device->setAlignment(Qt::AlignCenter);
    m_device->setStyleSheet("color:#777; font-size:11px;");
    root->addWidget(m_device);

    setCentralWidget(central);

    // BLE
    m_ble = new BleClient(this);
    connect(m_ble, &BleClient::statusChanged, this, &MainWindow::onStatus);
    connect(m_ble, &BleClient::readingsChanged, this, &MainWindow::onReadings);
    connect(m_ble, &BleClient::latestChanged, this, &MainWindow::onLatest);
    connect(m_ble, &BleClient::monitoringChanged, this, &MainWindow::onMonitoring);
    connect(m_ble, &BleClient::nextUpdateChanged, this, &MainWindow::onNextUpdate);
    connect(m_ble, &BleClient::deviceNameChanged, this,
            [this](const QString &n){ m_device->setText(n); });

    connect(m_primary, &QPushButton::clicked, this, [this]{
        if (m_ble->monitoring()) m_ble->stop(); else m_ble->connectAndLoad();
    });
    connect(m_refresh, &QPushButton::clicked, this, [this]{ m_ble->refresh(); });

    m_countdownTimer.setInterval(1000);
    connect(&m_countdownTimer, &QTimer::timeout, this, &MainWindow::tickCountdown);
    m_countdownTimer.start();

    selectRange(2);
    onReadings();   // show cached history immediately
}

void MainWindow::selectRange(int idx) {
    m_range = idx;
    m_btn8->setChecked(idx == 0);
    m_btn24->setChecked(idx == 1);
    m_btnAll->setChecked(idx == 2);
    m_chart->setRangeHours(idx == 0 ? 8 : (idx == 1 ? 24 : -1));
}

void MainWindow::onReadings() {
    const auto rs = m_ble->readings();
    m_chart->setReadings(rs);

    if (rs.isEmpty()) {
        m_avg->setText("");
    } else {
        double sum = 0;
        for (const auto &r : rs) sum += r.mgdl;
        const double avg = sum / rs.size();
        const double a1c = (avg + 46.7) / 28.7;   // ADAG estimate
        m_avg->setText(QString("Avg %1 mg/dL    Est. HbA1c %2%")
                       .arg(qRound(avg))
                       .arg(a1c, 0, 'f', 1));
    }
}

void MainWindow::onLatest(double mgdl, const QString &trend, bool valid) {
    if (valid) {
        m_value->setText(QString::number(qRound(mgdl)) + "  " + trend);
        m_value->setStyleSheet(QString("color:%1; font-size:72px; font-weight:700;").arg(colorFor(mgdl)));
        m_unit->setText(QString("mg/dL - %1").arg(rangeLabel(mgdl)));
        m_unit->setStyleSheet(QString("color:%1; font-size:14px;").arg(colorFor(mgdl)));
    } else {
        m_value->setText("--");
        m_value->setStyleSheet("color:#666; font-size:72px; font-weight:700;");
        m_unit->setText("mg/dL");
    }
}

void MainWindow::onStatus(const QString &s) { m_status->setText(s); }

void MainWindow::onMonitoring(bool on) {
    if (on) {
        m_primary->setText("Stop");
        m_primary->setStyleSheet(
            "QPushButton{background:#CC4D4D;color:white;font-size:15px;font-weight:600;border-radius:10px;}");
    } else {
        m_primary->setText("Connect & Load");
        m_primary->setStyleSheet(
            "QPushButton{background:#3475F0;color:white;font-size:15px;font-weight:600;border-radius:10px;}");
    }
}

void MainWindow::onNextUpdate() { tickCountdown(); }

void MainWindow::tickCountdown() {
    if (!m_ble->monitoring()) { m_countdown->setText(""); return; }
    QDateTime t = m_ble->nextUpdateAt();
    if (!t.isValid()) { m_countdown->setText("syncing..."); return; }
    qint64 secs = QDateTime::currentDateTime().secsTo(t);
    if (secs <= 0) { m_countdown->setText("updating..."); return; }
    m_countdown->setText(QString("next update in %1:%2")
                         .arg(secs / 60).arg(secs % 60, 2, 10, QChar('0')));
}
