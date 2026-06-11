#include "mainwindow.h"
#include "blescanner.h"
#include "bleclient.h"
#include "winpairing.h"
#include "glucosechart.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QFont>
#include <QDateTime>
#include <QTimer>
#include <QBluetoothAddress>

namespace {
constexpr int kMaxAttempts = 10;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Accu-Chek Glucose"));
    resize(460, 760);

    m_scanner = new BleScanner(this);
    m_client  = new BleClient(this);
    m_pairing = new WinPairing(this);

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("root"));
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(28, 28, 28, 28);
    root->setSpacing(0);

    // --- Header -------------------------------------------------------------
    auto *title = new QLabel(QStringLiteral("Accu-Chek SmartGuide"), this);
    title->setObjectName(QStringLiteral("title"));
    title->setAlignment(Qt::AlignHCenter);
    root->addWidget(title);

    root->addSpacing(10);

    // Connection status pill
    m_statusPill = new QLabel(this);
    m_statusPill->setObjectName(QStringLiteral("pill"));
    m_statusPill->setAlignment(Qt::AlignCenter);
    auto *pillRow = new QHBoxLayout();
    pillRow->addStretch();
    pillRow->addWidget(m_statusPill);
    pillRow->addStretch();
    root->addLayout(pillRow);

    root->addSpacing(16);

    // --- Glucose reading ----------------------------------------------------
    m_glucoseValue = new QLabel(QStringLiteral("--"), this);
    m_glucoseValue->setObjectName(QStringLiteral("value"));
    m_glucoseValue->setAlignment(Qt::AlignCenter);
    {
        QFont f = m_glucoseValue->font();
        f.setPointSize(86);
        f.setBold(true);
        m_glucoseValue->setFont(f);
    }
    root->addWidget(m_glucoseValue);

    m_glucoseUnit = new QLabel(QStringLiteral("mg/dL"), this);
    m_glucoseUnit->setObjectName(QStringLiteral("unit"));
    m_glucoseUnit->setAlignment(Qt::AlignCenter);
    root->addWidget(m_glucoseUnit);

    root->addSpacing(14);

    m_rangeLabel = new QLabel(QString(), this);
    m_rangeLabel->setObjectName(QStringLiteral("range"));
    m_rangeLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_rangeLabel);

    m_updatedLabel = new QLabel(QString(), this);
    m_updatedLabel->setObjectName(QStringLiteral("updated"));
    m_updatedLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_updatedLabel);

    root->addSpacing(12);

    // --- History chart ------------------------------------------------------
    m_chart = new GlucoseChart(this);
    root->addWidget(m_chart, 1);

    root->addSpacing(12);

    // --- Controls -----------------------------------------------------------
    m_passkeyEdit = new QLineEdit(this);
    m_passkeyEdit->setObjectName(QStringLiteral("pin"));
    m_passkeyEdit->setText(QStringLiteral("784651"));
    m_passkeyEdit->setAlignment(Qt::AlignCenter);
    m_passkeyEdit->setPlaceholderText(
        QStringLiteral("Pair code from sensor box (first time)"));
    m_passkeyEdit->setMaxLength(20);
    root->addWidget(m_passkeyEdit);

    root->addSpacing(12);

    m_actionButton = new QPushButton(QStringLiteral("Pair & Connect"), this);
    m_actionButton->setObjectName(QStringLiteral("action"));
    m_actionButton->setMinimumHeight(52);
    m_actionButton->setCursor(Qt::PointingHandCursor);
    root->addWidget(m_actionButton);

    root->addSpacing(8);

    m_refreshButton = new QPushButton(QStringLiteral("Refresh"), this);
    m_refreshButton->setObjectName(QStringLiteral("refresh"));
    m_refreshButton->setMinimumHeight(40);
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    root->addWidget(m_refreshButton);

    root->addSpacing(10);

    m_deviceLabel = new QLabel(QString(), this);
    m_deviceLabel->setObjectName(QStringLiteral("device"));
    m_deviceLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(m_deviceLabel);

    setCentralWidget(central);

    // --- Style --------------------------------------------------------------
    setStyleSheet(QStringLiteral(
        "#root { background-color: #11151f; }"
        "QLabel { color: #e6e6e6; }"
        "#title { color: #9fb3c8; font-size: 16px; font-weight: 600;"
        "  letter-spacing: 1px; }"
        "#pill { color: #c2cede; background-color: #1b2433;"
        "  border: 1px solid #2b3950; border-radius: 13px;"
        "  padding: 5px 14px; font-size: 13px; }"
        "#value { color: #8a99ab; }"
        "#unit { color: #7b8a9c; font-size: 22px; letter-spacing: 1px; }"
        "#range { font-size: 18px; font-weight: 600; }"
        "#updated { color: #6b7787; font-size: 12px; }"
        "#device { color: #6b7787; font-size: 12px; letter-spacing: 1px; }"
        "#pin { background-color: #1b2433; border: 1px solid #2b3950;"
        "  border-radius: 8px; padding: 10px; color: #e6e6e6; font-size: 14px; }"
        "#action { background-color: #2f6fed; color: white; border: none;"
        "  border-radius: 10px; font-size: 16px; font-weight: 600; }"
        "#action:hover { background-color: #3b7bf6; }"
        "#action:pressed { background-color: #2861d6; }"
        "#refresh { background-color: #1b2433; color: #c2cede;"
        "  border: 1px solid #2b3950; border-radius: 10px;"
        "  font-size: 14px; font-weight: 600; }"
        "#refresh:hover { background-color: #243347; }"
        "#refresh:disabled { color: #4a5568; border-color: #222c3a; }"));

    // --- Wiring -------------------------------------------------------------
    connect(m_actionButton, &QPushButton::clicked, this, &MainWindow::onActionClicked);
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() {
        // Refresh = reconnect and pull the latest value again. The sensor closes
        // the link after each read, so a fresh connect is the way to re-read.
        if (m_state == State::Idle)
            startFlow();
    });

    connect(m_scanner, &BleScanner::deviceFound,  this, &MainWindow::onDeviceFound);
    connect(m_scanner, &BleScanner::scanStarted,  this, [this]() { m_scanning = true; });
    connect(m_scanner, &BleScanner::scanFinished, this, &MainWindow::onScanFinished);

    connect(m_client, &BleClient::glucoseValue, this, &MainWindow::onGlucose);
    connect(m_client, &BleClient::deviceName, this, [this](const QString &n) {
        if (!n.isEmpty())
            m_deviceLabel->setText(n);
    });
    connect(m_client, &BleClient::connected, this, [this]() {
        setState(State::Connected, QStringLiteral("waiting for reading"));
    });
    connect(m_client, &BleClient::disconnected, this, [this]() {
        // Only treat as a clean stop if we were actually online. If the link
        // dropped mid-handshake (e.g. the sensor closed it right after the RACP
        // write), a retry is already in flight from connectionFailed - leave it.
        if (m_state == State::Connected || m_state == State::Live)
            setState(State::Idle, QStringLiteral("disconnected"));
    });
    connect(m_client, &BleClient::connectionFailed, this, [this]() {
        // Already showing data: a later drop is fine, just stop cleanly.
        if (m_state == State::Live) {
            setState(State::Idle, QStringLiteral("disconnected"));
            return;
        }
        if (m_attempts < kMaxAttempts) {
            setState(State::Searching, QStringLiteral("retrying"));
            QTimer::singleShot(1500, this, [this]() {
                if (m_state == State::Searching && !m_scanning)
                    m_scanner->startScan(10000);
            });
        } else {
            setState(State::Idle, QStringLiteral("could not connect"));
        }
    });

    connect(m_pairing, &WinPairing::finished, this, [this](bool ok) {
        if (ok) {
            m_bonded = true;
            connectToInfo(m_lastTarget);
        } else {
            setState(State::Idle, QStringLiteral("pairing failed"));
        }
    });

    setState(State::Idle);
}

void MainWindow::setState(State s, const QString &detail)
{
    m_state = s;

    QString text, dot;
    switch (s) {
    case State::Idle:       text = QStringLiteral("Disconnected"); dot = QStringLiteral("#6b7787"); break;
    case State::Searching:  text = QStringLiteral("Searching for sensor"); dot = QStringLiteral("#fb8c00"); break;
    case State::Pairing:    text = QStringLiteral("Pairing"); dot = QStringLiteral("#fb8c00"); break;
    case State::Connecting: text = QStringLiteral("Connecting"); dot = QStringLiteral("#fb8c00"); break;
    case State::Connected:  text = QStringLiteral("Connected"); dot = QStringLiteral("#2f6fed"); break;
    case State::Live:       text = QStringLiteral("Live"); dot = QStringLiteral("#43a047"); break;
    }
    if (!detail.isEmpty())
        text += QStringLiteral(" \u00b7 ") + detail;

    m_statusPill->setText(QStringLiteral("\u25CF  ") + text);
    m_statusPill->setStyleSheet(
        QStringLiteral("#pill { color: %1; background-color: #1b2433;"
                       " border: 1px solid #2b3950; border-radius: 13px;"
                       " padding: 5px 14px; font-size: 13px; }").arg(dot));

    const bool busy = (s == State::Searching || s == State::Pairing
                       || s == State::Connecting);
    const bool online = (s == State::Connected || s == State::Live);

    m_actionButton->setEnabled(true);
    if (online)
        m_actionButton->setText(QStringLiteral("Disconnect"));
    else if (busy)
        m_actionButton->setText(QStringLiteral("Cancel"));
    else
        m_actionButton->setText(m_haveData ? QStringLiteral("Connect")
                                            : QStringLiteral("Pair & Connect"));
    m_refreshButton->setEnabled(s == State::Idle && m_haveData);
    m_passkeyEdit->setEnabled(s == State::Idle);

    if (s == State::Idle && !m_haveData) {
        m_glucoseValue->setText(QStringLiteral("--"));
        m_glucoseValue->setStyleSheet(QStringLiteral("color: #5a6675;"));
        m_rangeLabel->clear();
        m_deviceLabel->clear();
    }
}

void MainWindow::onActionClicked()
{
    // Online -> disconnect.
    if (m_state == State::Connected || m_state == State::Live) {
        m_client->disconnectFromDevice();
        setState(State::Idle, QStringLiteral("disconnected"));
        return;
    }

    // Busy -> cancel.
    if (m_state == State::Searching || m_state == State::Pairing
        || m_state == State::Connecting) {
        m_pairArmed = false;
        if (m_scanning)
            m_scanner->stopScan();
        m_client->disconnectFromDevice();
        setState(State::Idle, QStringLiteral("cancelled"));
        return;
    }

    // Idle -> start the full find/pair/connect flow.
    startFlow();
}

void MainWindow::startFlow()
{
    if (m_state != State::Idle)
        return;
    m_pairPin = m_passkeyEdit->text().trimmed();
    m_attempts = 0;
    m_pairSightings = 0;
    m_staleSkips = 0;
    m_didForceRepair = false;
    m_pairArmed = true;       // pair on first sighting (fresh address)
    setState(State::Searching);
    if (!m_scanning)
        m_scanner->startScan(10000);
}

void MainWindow::connectToInfo(const QBluetoothDeviceInfo &info)
{
    ++m_attempts;
    setState(State::Connecting);   // set state first so a synchronous
                                    // scanFinished does not relaunch the scan
    if (m_scanning)
        m_scanner->stopScan();
    m_client->connectToDevice(info);
}

void MainWindow::onDeviceFound(const QBluetoothDeviceInfo &info, bool isTarget)
{
    if (!isTarget)
        return;

    m_lastTarget = info;

    const QString nm = info.name();
    if (nm.startsWith(QStringLiteral("AC-"), Qt::CaseInsensitive))
        m_knownName = nm;                 // remember the resolved name
    if (!m_knownName.isEmpty())
        m_deviceLabel->setText(m_knownName);
    else if (!nm.isEmpty())
        m_deviceLabel->setText(nm);       // fallback until AC- name is seen

    if (m_pairArmed) {
        // The sensor advertises a generic (RPA) name; its "AC-" name only
        // appears after connecting (read from 0x2A00). So pair immediately on
        // the first sighting instead of waiting for a name that never comes.
        m_pairArmed = false;
        setState(State::Pairing);   // state first so a synchronous scanFinished
                                    // does not relaunch the scan during pairing
        if (m_scanning)
            m_scanner->stopScan();
        m_pairing->pair(info.address().toUInt64(), m_pairPin);
    } else if (m_bonded && m_state == State::Searching
               && m_attempts < kMaxAttempts) {
        // After a drop the sensor re-advertises with a new RPA, but Windows
        // keeps handing back the stale cached entry (rssi==0) which fails to
        // connect. Prefer a fresh advertisement; only fall back to a stale one
        // if no live sighting arrives for a while.
        if (info.rssi() != 0 || m_staleSkips >= 8) {
            m_staleSkips = 0;
            connectToInfo(info);
        } else {
            ++m_staleSkips;
        }
    }
}

void MainWindow::onScanFinished()
{
    m_scanning = false;
    // Keep looking while we're in the searching phase; the user can press
    // Cancel to stop. Connection-retry bounding is handled in connectionFailed.
    if (m_state == State::Searching)
        m_scanner->startScan(10000);
}

void MainWindow::onGlucose(double mgdl)
{
    m_glucoseValue->setText(QString::number(mgdl, 'f', 0));

    QString color, label;
    if (mgdl < 70.0) {
        color = QStringLiteral("#e53935"); label = QStringLiteral("Low");
    } else if (mgdl > 180.0) {
        color = QStringLiteral("#fb8c00"); label = QStringLiteral("High");
    } else {
        color = QStringLiteral("#43a047"); label = QStringLiteral("In range");
    }
    m_glucoseValue->setStyleSheet(QStringLiteral("color: %1;").arg(color));
    m_rangeLabel->setText(label);
    m_rangeLabel->setStyleSheet(QStringLiteral("#range { color: %1; }").arg(color));
    m_updatedLabel->setText(QStringLiteral("Updated %1")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));

    // Accumulate readings so the chart builds a live trend across refreshes.
    const QDateTime now = QDateTime::currentDateTime();
    if (!m_firstReading.isValid())
        m_firstReading = now;
    const double minutes = m_firstReading.secsTo(now) / 60.0;
    m_series.append(QPointF(minutes, mgdl));
    if (m_series.size() > 1000)
        m_series.remove(0);
    m_chart->setData(m_series);

    m_haveData = true;
    setState(State::Live);
}
