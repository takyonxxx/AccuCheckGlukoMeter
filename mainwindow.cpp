#include "mainwindow.h"
#include "blescanner.h"
#include "bleclient.h"

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
    setWindowTitle(QStringLiteral("AccuCheckGlukoMeter - BLE Scanner"));
    resize(960, 640);

    m_scanner = new BleScanner(this);
    m_client  = new BleClient(this);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);

    // ---- Control row -------------------------------------------------------
    auto *controls = new QHBoxLayout();

    m_filterEdit = new QLineEdit(QStringLiteral("SmartGuide"), this);
    m_filterEdit->setPlaceholderText(
        QStringLiteral("Sensor filter: name or serial fragment (e.g. SmartGuide or 960570)"));

    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(3, 120);
    m_timeoutSpin->setValue(15);
    m_timeoutSpin->setSuffix(QStringLiteral(" s"));

    m_scanButton = new QPushButton(QStringLiteral("Start Scan"), this);
    m_connectButton = new QPushButton(QStringLiteral("Connect Selected"), this);
    m_saveButton = new QPushButton(QStringLiteral("Save Log..."), this);
    m_autoConnect = new QCheckBox(QStringLiteral("Auto-connect target"), this);
    m_autoConnect->setChecked(true);

    controls->addWidget(new QLabel(QStringLiteral("Filter:"), this));
    controls->addWidget(m_filterEdit, 1);
    controls->addWidget(new QLabel(QStringLiteral("Timeout:"), this));
    controls->addWidget(m_timeoutSpin);
    controls->addWidget(m_autoConnect);
    controls->addWidget(m_scanButton);
    controls->addWidget(m_connectButton);
    controls->addWidget(m_saveButton);

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
    m_logView->setMaximumHeight(170);

    m_statusLabel = new QLabel(QStringLiteral("Idle"), this);

    root->addLayout(controls);
    root->addWidget(m_table, 1);
    root->addWidget(new QLabel(QStringLiteral("Log:"), this));
    root->addWidget(m_logView);
    root->addWidget(m_statusLabel);

    setCentralWidget(central);

    // ---- Dark theme --------------------------------------------------------
    setStyleSheet(QStringLiteral(
        "QWidget { background-color: #2b2b2b; color: #e6e6e6;"
        "  font-size: 13px; }"
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

    // ---- Wiring ------------------------------------------------------------
    connect(m_scanButton, &QPushButton::clicked, this, &MainWindow::onScanClicked);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_saveButton, &QPushButton::clicked, this, &MainWindow::onSaveLogClicked);
    connect(m_scanner, &BleScanner::deviceFound,  this, &MainWindow::onDeviceFound);
    connect(m_scanner, &BleScanner::deviceLogLine, this, &MainWindow::log);
    connect(m_scanner, &BleScanner::scanStarted,  this, &MainWindow::onScanStarted);
    connect(m_scanner, &BleScanner::scanFinished, this, &MainWindow::onScanFinished);
    connect(m_scanner, &BleScanner::errorOccurred, this, &MainWindow::onError);
    connect(m_client, &BleClient::logLine, this, &MainWindow::log);
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
        // Retry with a fresh (current) RPA by scanning again.
        if (m_autoConnect->isChecked() && m_connectAttempts < 5) {
            log(QStringLiteral("Connect failed. Re-scanning for a fresh address "
                               "(attempt %1/5)...").arg(m_connectAttempts + 1));
            if (!m_scanning)
                m_scanner->startScan(m_timeoutSpin->value() * 1000);
        } else if (m_connectAttempts >= 5) {
            log(QStringLiteral("Giving up after 5 attempts. The sensor is likely "
                               "bonded to the phone and/or requires pairing."));
        }
    });

    log(QStringLiteral("Ready. Make sure Bluetooth is ON and the official "
                       "Accu-Chek app is closed before scanning."));
}

QString MainWindow::deviceId(const QBluetoothDeviceInfo &info) const
{
    // On Windows/macOS the MAC is hidden by the OS; deviceUuid() is the stable
    // per-host identifier. On Linux/Android address() is usually a real MAC.
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

void MainWindow::connectToInfo(const QBluetoothDeviceInfo &info)
{
    if (m_scanning)
        m_scanner->stopScan();

    m_connecting = true;
    ++m_connectAttempts;

    const QString id = deviceId(info);
    m_statusLabel->setText(QStringLiteral("Connecting to %1 ...").arg(id));
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

    // Scanning can interfere with establishing a GATT connection; stop it.
    m_connectAttempts = 0;
    connectToInfo(m_deviceById.value(id));
}

void MainWindow::onSaveLogClicked()
{
    const QString suggested = QStringLiteral("accuchek_scan_%1.txt")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save scan log"), suggested,
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
        m_statusLabel->setText(
            QStringLiteral("Target candidate: %1 [%2]").arg(name, id));
        log(QStringLiteral("TARGET candidate: %1 [%2] rssi=%3")
                .arg(name, id).arg(info.rssi()));

        // Auto-connect immediately while the RPA is still valid.
        if (m_autoConnect->isChecked() && !m_connecting && m_connectAttempts < 5)
            connectToInfo(info);
    }
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
