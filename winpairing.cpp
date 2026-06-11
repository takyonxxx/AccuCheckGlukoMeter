#include "winpairing.h"

#include <QtGlobal>
#include <QDebug>

#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <thread>
#include <string>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>

using namespace winrt;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Enumeration;
#endif

WinPairing::WinPairing(QObject *parent)
    : QObject(parent)
{
}

void WinPairing::pair(quint64 address, const QString &pin, bool forceRepair)
{
#ifdef Q_OS_WIN
    const std::wstring wpin = pin.toStdWString();

    std::thread([this, address, wpin, forceRepair]() {
        bool ok = false;
        QString msg;

        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);

            auto dev = BluetoothLEDevice::FromBluetoothAddressAsync(address).get();
            if (!dev) {
                // RPA devices often need the Random address type.
                dev = BluetoothLEDevice::FromBluetoothAddressAsync(
                          address, BluetoothAddressType::Random).get();
            }

            if (!dev) {
                msg = QStringLiteral("Device not reachable at this address "
                                     "(RPA may have rotated - rescan and retry).");
            } else {
                auto pairing = dev.DeviceInformation().Pairing();

                if (pairing.IsPaired() && forceRepair) {
                    auto unpair = pairing.UnpairAsync().get();
                    const auto ustatus = unpair.Status();
                    QMetaObject::invokeMethod(this, [this, ustatus]() {
                        qDebug().noquote() << "[WinPairing] Unpair status code:"
                                           << static_cast<int>(ustatus);
                    }, Qt::QueuedConnection);
                    // Re-acquire device + pairing object after unpair.
                    dev = BluetoothLEDevice::FromBluetoothAddressAsync(
                              address, BluetoothAddressType::Random).get();
                    if (dev)
                        pairing = dev.DeviceInformation().Pairing();
                }

                if (pairing.IsPaired() && !forceRepair) {
                    ok = true;
                    msg = QStringLiteral("Already paired.");
                } else {
                    auto custom = pairing.Custom();
                    auto token = custom.PairingRequested(
                        [wpin](DeviceInformationCustomPairing const &,
                               DevicePairingRequestedEventArgs const &args) {
                            switch (args.PairingKind()) {
                            case DevicePairingKinds::ProvidePin:
                                args.Accept(winrt::hstring(wpin));
                                break;
                            case DevicePairingKinds::ConfirmOnly:
                            case DevicePairingKinds::ConfirmPinMatch:
                            default:
                                args.Accept();
                                break;
                            }
                        });

                    auto result = custom.PairAsync(
                        DevicePairingKinds::ProvidePin
                        | DevicePairingKinds::ConfirmOnly).get();

                    custom.PairingRequested(token); // revoke handler

                    const auto status = result.Status();
                    ok = (status == DevicePairingResultStatus::Paired
                          || status == DevicePairingResultStatus::AlreadyPaired);
                    msg = QStringLiteral("Pairing result status code: %1")
                              .arg(static_cast<int>(status));
                }
            }
        } catch (winrt::hresult_error const &e) {
            msg = QStringLiteral("WinRT error 0x%1: %2")
                      .arg(static_cast<quint32>(e.code().value), 8, 16, QLatin1Char('0'))
                      .arg(QString::fromWCharArray(e.message().c_str()));
        } catch (...) {
            msg = QStringLiteral("Unknown pairing error.");
        }

        QMetaObject::invokeMethod(this, [this, ok, msg]() {
            qDebug().noquote() << "[WinPairing]" << (ok ? "OK -" : "FAIL -") << msg;
            emit logLine(msg);
            emit finished(ok);
        }, Qt::QueuedConnection);
    }).detach();
#else
    Q_UNUSED(address);
    Q_UNUSED(pin);
    emit logLine(QStringLiteral("WinRT pairing is only available on Windows."));
    emit finished(false);
#endif
}
