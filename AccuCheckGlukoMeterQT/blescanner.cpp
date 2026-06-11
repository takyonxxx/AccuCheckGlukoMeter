#include "blescanner.h"
#include <QtBluetooth/QBluetoothUuid>
#include <QDebug>

static const QBluetoothUuid kCgmService{quint16(0x181F)};
static const QBluetoothUuid kGlucoseService{quint16(0x1808)};

BleScanner::BleScanner(QObject *parent) : QObject(parent) {
    m_agent = new QBluetoothDeviceDiscoveryAgent(this);
    m_agent->setLowEnergyDiscoveryTimeout(15000);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BleScanner::onDeviceDiscovered);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BleScanner::onFinished);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::canceled,
            this, &BleScanner::onFinished);
}

void BleScanner::start() {
    m_matched = false;
    if (m_agent->isActive())
        m_agent->stop();
    m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    qDebug() << "[BleScanner] Scan started";
}

void BleScanner::stop() {
    if (m_agent->isActive())
        m_agent->stop();
}

void BleScanner::onDeviceDiscovered(const QBluetoothDeviceInfo &info) {
    if (m_matched)
        return;

    const bool nameMatch = info.name().startsWith("AC-", Qt::CaseInsensitive);
    bool serviceMatch = false;
    const auto uuids = info.serviceUuids();
    for (const QBluetoothUuid &u : uuids) {
        if (u == kCgmService || u == kGlucoseService) { serviceMatch = true; break; }
    }

    if (nameMatch || serviceMatch) {
        m_matched = true;
        qDebug() << "[BleScanner] TARGET" << info.name();
        m_agent->stop();
        emit found(info);
    }
}

void BleScanner::onFinished() {
    if (!m_matched)
        emit finishedWithoutTarget();
}
