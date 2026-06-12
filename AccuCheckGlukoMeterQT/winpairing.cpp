#include "winpairing.h"
#include <QDebug>

#ifdef Q_OS_WIN
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <thread>
#include <string>

using namespace winrt;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Enumeration;
#endif

WinPairing::WinPairing(QObject *parent) : QObject(parent) {}

void WinPairing::pair(const QBluetoothAddress &address, const QString &pin, bool forceRepair) {
#ifdef Q_OS_WIN
    const uint64_t addr = address.toUInt64();
    const std::wstring wpin = pin.toStdWString();
    WinPairing *self = this;

    std::thread([self, addr, wpin, forceRepair]() {
        bool ok = false;
        QString msg;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);

            auto dev = BluetoothLEDevice::FromBluetoothAddressAsync(addr).get();
            if (!dev) {
                msg = "Device not found for pairing";
            } else {
                auto pairing = dev.DeviceInformation().Pairing();

                if (forceRepair && pairing.IsPaired())
                    pairing.UnpairAsync().get();

                if (pairing.IsPaired()) {
                    ok = true;
                    msg = "Already paired";
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
                                args.Accept();
                                break;
                            default:
                                args.Accept();
                                break;
                            }
                        });

                    auto result = custom.PairAsync(
                        DevicePairingKinds::ProvidePin | DevicePairingKinds::ConfirmOnly,
                        DevicePairingProtectionLevel::Default).get();

                    custom.PairingRequested(token);

                    auto status = result.Status();
                    ok = (status == DevicePairingResultStatus::Paired ||
                          status == DevicePairingResultStatus::AlreadyPaired);
                    msg = QString("Pairing status code: %1").arg(int(status));
                }
            }
        } catch (winrt::hresult_error const &e) {
            msg = QString("WinRT error 0x%1").arg(uint32_t(e.code()), 0, 16);
        } catch (...) {
            msg = "Unknown pairing error";
        }

        QMetaObject::invokeMethod(self, [self, ok, msg]() {
            emit self->finished(ok, msg);
        }, Qt::QueuedConnection);
    }).detach();
#else
    Q_UNUSED(address);
    Q_UNUSED(pin);
    Q_UNUSED(forceRepair);
    emit finished(true, "Pairing handled by the OS");
#endif
}
