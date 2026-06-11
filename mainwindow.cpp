#include "mainwindow.h"
#include "blescanner.h"
#include "bleclient.h"
#include "winpairing.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QPushButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QLabel>
#include <QFont>
#include <QColor>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QList>
#include <QBluetoothUuid>
#include <QBluetoothAddress>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("AccuCheckGlukoMeter"));
    resize(980, 720);

    m_scanner = new BleScanner(this);
    m_client  = new BleClient(this);
    m_pairing = new WinPairing(this);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);

    // ---- Big glucose display ----------------------------------------------
    m_glucoseValue = new QLabel(QStringLiteral("--"), this);
    QFont vf = m_glucoseValue->font();
    vf.setPointSize(96);
    vf.setBold(true);
    m_glucoseValue->setFont(vf);
    m_glucoseValue->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

    m_glucoseUnit = new QLabel(QStringLiteral("mg/dL"), this);
    QFont uf = m_glucoseUnit->font();
    uf.setPointSize(22);
    m_glucoseUnit->setFont(uf);
    m_glucoseUnit->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    auto *glucoseRow = new QHBoxLayout();
    glucoseRow->addStretch();
    glucoseRow->addWidget(m_glucoseValue);
    glucoseRow->addSpacing(10);
    glucoseRow->addWidget(m_glucoseUnit);
    glucoseRow->addStretch();
    root->addLayout(glucoseRow);

    // ---- Control row 1: scan / filter -------------------------------------
    auto *controls = new QHBoxLayout();

    m_filterEdit = new QLineEdit(QStringLiteral("SmartGuide"), this);
    m_filterEdit->setPlaceholderText(
        QStringLiteral("Sensor filter (name or serial fragment)"));

    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(3, 120);
    m_timeoutSpin->setValue(15);
    m_timeoutSpin->setSuffix(QStringLiteral(" s"));

    m_autoConnect = new QCheckBox(QStringLiteral("Auto-connect target"), this);
    m_autoConnect->setChecked(true);

    m_scanButton = new QPushButton(QStringLiteral("Start Scan"), this);
    m_connectButton = new QPushButton(QStringLiteral("Connect Selected"), this);
    m_saveButton = new QPushButton(QStringLiteral("Save Log..."), this);

    controls->addWidget(new QLabel(QStringLiteral("Filter:"), this));
    controls->addWidget(m_filterEdit, 1);
    controls->addWidget(new QLabel(QStringLiteral("Timeout:"), this));
    controls->addWidget(m_timeoutSpin);
    controls->addWidget(m_autoConnect);
    controls->addWidget(m_scanButton);
    controls->addWidget(m_connectButton);
    controls->addWidget(m_saveButton);
    root->addLayout(controls);

    // ---- Control row 2: pairing -------------------------------------------
    auto *pairRow = new QHBoxLayout();
    m_passkeyEdit = new QLineEdit(this);
    m_passkeyEdit->setPlaceholderText(
        QStringLiteral("Pair code from the sensor box (e.g. 6 digits)"));
    m_passkeyEdit->setMaximumWidth(280);
    m_pairButton = new QPushButton(QStringLiteral("Pair && Connect"), this);

    pairRow->addWidget(new QLabel(QStringLiteral("Pair code:"), this));
    pairRow->addWidget(m_passkeyEdit);
    pairRow->addWidget(m_pairButton);
    pairRow->addStretch();
    root->addLayout(pairRow);

    // ---- Device table ------------------------------------------------------
    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("Name"),
        QStringLiteral("Identifier (MAC/UUID)"),
        QStringLiteral("RSSI"),
        QStringLiteral("Manufacturer IDs"),
        QStringLiteral("Service UUIDs")
    });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 210);
    m_table->setColumnWidth(1, 280);
    m_table->setColumnWidth(2, 60);
    m_table->setColumnWidth(3, 150);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

    // ---- Log + status ------------------------------------------------------
    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(5000);
    m_logView->setMaximumHeight(180);

    m_statusLabel = new QLabel(QStringLiteral("Idle"), this);

    root->addWidget(m_table, 1);
    root->addWidget(new QLabel(QStringLiteral("Log:"), this));
    root->addWidget(m_logView);
    root->addWidget(m_statusLabel);

    setCentralWidget(central);

    // ---- Dark theme --------------------------------------------------------
    setStyleSheet(QStringLiteral(
        "QWidget { background-color: #2b2b2b; color: #e6e6e6; font-size: 13px; }"
        "QLineEdit, QSpinBox, QPlainTextEdit, QTableWidget {"
        "  background-color: #243347; border: 1px solid #3A5169;"
        "  border-radius: 4px; padding: 4px; }"
        "QHeaderView::section { background-color: #1D293D; color: #e6e6e6;"
        "  padding: 4px; border: 0px; }"
        "QTableWidget { gridline-color: #3A5169; }"
        "QPushButton { background-color: #3A5169; border: 1px solid #4d6a8a;"
        "  border-radius: 4px; padding: 6px 14px; }"
        "QPushButton:hover { background-color: #4d6a8a; }"
        "QPushButton:pressed { background-color: #243347; }"));
    m_glucoseValue->setStyleSheet(QStringLiteral("color: #8a8a8a;"));

    // ---- Wiring ------------------------------------------------------------
    connect(m_scanButton, &QPushButton::clicked, this, &MainWindow::onScanClicked);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_pairButton, &QPushButton::clicked, this, &MainWindow::onPairClicked);
    connect(m_saveButton, &QPushButton::clicked, this, &MainWindow::onSaveLogClicked);

    connect(m_scanner, &BleScanner::deviceFound,  this, &MainWindow::onDeviceFound);
    connect(m_scanner, &BleScanner::deviceLogLine, this, &MainWindow::log);
    connect(m_scanner, &BleScanner::scanStarted,  this, &MainWindow::onScanStarted);
    connect(m_scanner, &BleScanner::scanFinished, this, &MainWindow::onScanFinished);
    connect(m_scanner, &BleScanner::errorOccurred, this, &MainWindow::onError);

    connect(m_client, &BleClient::logLine, this, &MainWindow::log);
    connect(m_client, &BleClient::glucoseValue, this, &MainWindow::onGlucose);
    connect(m_client, &BleClient::connected, this, [this]() {
        m_connecting = true;
        m_statusLabel->setText(QStringLiteral("Connected. Discovering GATT..."));
    });
    connect(m_client, &BleClient::disconnected, this, [this]() {
        m_connecting = false;
        m_statusLabel->setText(QStringLiteral("Disconnected."));
    });
    connect(m_client, &BleClient::connectionFailed, this, [this]() {
        m_connecting = false;
        if (m_autoConnect->isChecked() && m_connectAttempts < 5) {
            log(QStringLiteral("Connect failed. Re-scanning for a fresh address "
                               "(attempt %1/5)...").arg(m_connectAttempts + 1));
            if (!m_scanning)
                m_scanner->startScan(m_timeoutSpin->value() * 1000);
        } else if (m_connectAttempts >= 5) {
            log(QStringLiteral("Giving up after 5 attempts."));
        }
    });

    connect(m_pairing, &WinPairing::logLine, this, &MainWindow::log);
    connect(m_pairing, &WinPairing::finished, this, [this](bool ok) {
        if (ok) {
            log(QStringLiteral("Paired. Connecting..."));
            m_connectAttempts = 0;
            connectToInfo(m_lastTarget);
        } else {
            log(QStringLiteral("Pairing failed. If the sensor is bonded to the "
                               "phone it may reject a second bond."));
        }
    });

    log(QStringLiteral("Ready. Turn the phone's Bluetooth off, enter the pair "
                       "code, then Pair && Connect."));
}

QString MainWindow::deviceId(const QBluetoothDeviceInfo &info) const
{
    if (!info.address().isNull())
        return info.address().toString();
    return info.deviceUuid().toString();
}

int MainWindow::rowForId(const QString &id)
{
    if (m_rowById.contains(id))
        return m_rowById.value(id);
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_rowById.insert(id, row);
    return row;
}

void MainWindow::onScanClicked()
{
    if (m_scanning) {
        m_scanner->stopScan();
        return;
    }
    m_scanner->setNameFilter(m_filterEdit->text());
    m_table->setRowCount(0);
    m_rowById.clear();
    m_connectAttempts = 0;
    m_scanner->startScan(m_timeoutSpin->value() * 1000);
}

void MainWindow::onPairClicked()
{
    m_pairPin = m_passkeyEdit->text().trimmed();
    if (m_pairPin.isEmpty()) {
        log(QStringLiteral("Enter the pair code first."));
        return;
    }
    // Arm: pair the next time the target is seen (fresh address).
    m_pairArmed = true;
    m_connectAttempts = 0;
    log(QStringLiteral("Pairing armed. Scanning for the sensor..."));
    if (!m_scanning) {
        m_scanner->setNameFilter(m_filterEdit->text());
        m_scanner->startScan(m_timeoutSpin->value() * 1000);
    }
}

void MainWindow::connectToInfo(const QBluetoothDeviceInfo &info)
{
    if (m_scanning)
        m_scanner->stopScan();
    m_connecting = true;
    ++m_connectAttempts;
    m_statusLabel->setText(QStringLiteral("Connecting to %1 ...").arg(deviceId(info)));
    m_client->connectToDevice(info);
}

void MainWindow::onConnectClicked()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        log(QStringLiteral("Select a device row in the table first."));
        return;
    }
    QTableWidgetItem *idItem = m_table->item(row, 1);
    if (!idItem)
        return;
    const QString id = idItem->text();
    if (!m_deviceById.contains(id)) {
        log(QStringLiteral("No cached device info for %1 (re-scan).").arg(id));
        return;
    }
    m_connectAttempts = 0;
    connectToInfo(m_deviceById.value(id));
}

void MainWindow::onSaveLogClicked()
{
    const QString suggested = QStringLiteral("accuchek_scan_%1.txt")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save log"), suggested,
        QStringLiteral("Text files (*.txt);;All files (*.*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        log(QStringLiteral("Could not write file: %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << m_logView->toPlainText() << '\n';
    file.close();
    log(QStringLiteral("Log saved: %1").arg(path));
}

void MainWindow::onDeviceFound(const QBluetoothDeviceInfo &info, bool isTarget)
{
    const QString id  = deviceId(info);
    const int     row = rowForId(id);
    m_deviceById.insert(id, info);

    const QString name = info.name().isEmpty() ? QStringLiteral("(no name)")
                                               : info.name();

    QStringList manuf;
    const QList<quint16> manufIds = info.manufacturerIds();
    for (quint16 cid : manufIds)
        manuf << QStringLiteral("0x%1").arg(cid, 4, 16, QLatin1Char('0'));

    QStringList svcs;
    const QList<QBluetoothUuid> uuids = info.serviceUuids();
    for (const QBluetoothUuid &u : uuids)
        svcs << u.toString();

    auto setItem = [&](int col, const QString &text) {
        QTableWidgetItem *item = m_table->item(row, col);
        if (!item) {
            item = new QTableWidgetItem();
            m_table->setItem(row, col, item);
        }
        item->setText(text);
    };

    setItem(0, name);
    setItem(1, id);
    setItem(2, QString::number(info.rssi()));
    setItem(3, manuf.join(QStringLiteral(", ")));
    setItem(4, svcs.join(QStringLiteral(", ")));

    if (isTarget) {
        for (int c = 0; c < m_table->columnCount(); ++c) {
            if (QTableWidgetItem *it = m_table->item(row, c)) {
                it->setBackground(QColor(QStringLiteral("#2e7d32")));
                it->setForeground(QColor(QStringLiteral("#ffffff")));
            }
        }
        m_lastTarget = info;
        m_statusLabel->setText(
            QStringLiteral("Target: %1 [%2]").arg(name, id));
        log(QStringLiteral("TARGET candidate: %1 [%2] rssi=%3")
                .arg(name, id).arg(info.rssi()));

        if (m_pairArmed) {
            m_pairArmed = false;
            if (m_scanning)
                m_scanner->stopScan();
            log(QStringLiteral("Pairing with code (address %1)...").arg(id));
            m_pairing->pair(info.address().toUInt64(), m_pairPin);
        } else if (m_autoConnect->isChecked() && !m_connecting
                   && m_connectAttempts < 5) {
            connectToInfo(info);
        }
    }
}

void MainWindow::onGlucose(double mgdl)
{
    m_glucoseValue->setText(QString::number(mgdl, 'f', 0));

    QString color = QStringLiteral("#43a047"); // in range (green)
    if (mgdl < 70.0)
        color = QStringLiteral("#e53935");      // low (red)
    else if (mgdl > 180.0)
        color = QStringLiteral("#fb8c00");      // high (orange)
    m_glucoseValue->setStyleSheet(QStringLiteral("color: %1;").arg(color));

    m_statusLabel->setText(QStringLiteral("Live glucose: %1 mg/dL")
                               .arg(mgdl, 0, 'f', 0));
}

void MainWindow::onScanStarted()
{
    m_scanning = true;
    m_scanButton->setText(QStringLiteral("Stop Scan"));
    m_statusLabel->setText(QStringLiteral("Scanning..."));
    log(QStringLiteral("Scan started."));
}

void MainWindow::onScanFinished()
{
    m_scanning = false;
    m_scanButton->setText(QStringLiteral("Start Scan"));
    m_statusLabel->setText(
        QStringLiteral("Scan finished. BLE devices: %1").arg(m_table->rowCount()));
    log(QStringLiteral("Scan finished. %1 BLE device(s).").arg(m_table->rowCount()));
}

void MainWindow::onError(const QString &message)
{
    m_scanning = false;
    m_scanButton->setText(QStringLiteral("Start Scan"));
    m_statusLabel->setText(QStringLiteral("Error: %1").arg(message));
    log(QStringLiteral("ERROR: %1").arg(message));
}

void MainWindow::log(const QString &line)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_logView->appendPlainText(QStringLiteral("[%1] %2").arg(ts, line));
}
