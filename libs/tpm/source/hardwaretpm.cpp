/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Jun 2020
 *
 ****************************************************************************/

#include "tpm/hardwaretpm.h"
#include "tpm_identity.h"
#include "tpm_locate.h"

#include <logger/logger.h>

#include <utility>

using ByteArray = utils::ByteArray;

HardwareTpm::HardwareTpm(TpmBusFlags busFlags, const std::string &usbDevicePath,
                         UsbLocator locateUsb)
    : m_busFlags(busFlags)
    , m_usbDevicePath(usbDevicePath)
    , m_locateUsb(std::move(locateUsb))
{
    readIdentity();
    m_identityRead = true;
}

HardwareTpm::~HardwareTpm()
{
}

bool HardwareTpm::hasTpm() const
{
    return tpm().ok();
}

bool HardwareTpm::isValid() const
{
    auto serialNumber = serial();
    if (serialNumber < 15004)
        return false;

    int sum = 0;
    while (serialNumber != 0) {
        sum += serialNumber % 10;
        serialNumber /= 10;
    }

    return sum % 10 == 0 && m_uuid.size() == 16;
}

std::string HardwareTpm::provider() const
{
    return HW_PROVIDER;
}

uint64_t HardwareTpm::serial() const
{
    return m_serial;
}

std::string HardwareTpm::uuid() const
{
    return m_uuid.hex();
}

std::string HardwareTpm::signMessage(const ByteArray &message) const
{
    auto device = tpm();
    if (!device.ok()) {
        return {};
    }

    return device.signMessage(KEY4_SLOT, message).hex();
}

ByteArray HardwareTpm::signDigest(const ByteArray &digest) const
{
    auto device = tpm();
    if (!device.ok()) {
        // Issuing the command anyway would hand calib_sign a null ATCADevice,
        // which comes back as ATCA_BAD_PARAM -- an error about our own call,
        // not about the chip, and useless to report as a signing failure.
        return {};
    }

    return device.signDigest(KEY4_SLOT, digest);
}

/**
 * @brief Open the TPM, rediscovering it if the cached location went stale.
 *
 * The cached TpmDevice is only a hint. A probe that resets re-enumerates on
 * USB, and the ttyACM node it comes back on need not be the one it left: a
 * Spike2 in the field moved its TPM from /dev/ttyACM1 to /dev/ttyACM2 after a
 * probe watchdog reset, and because the cache was written once at construction
 * and never revisited, every signature failed for three hours until scorbitd
 * was restarted. Worse, the stale node by then belonged to the NFC endpoint,
 * so each attempt was poking an unrelated device.
 *
 * So a cached location that stops working is dropped rather than retried
 * forever, and the next call falls back to full discovery. On success the
 * cache is refreshed so the recovery costs one failed open, not one per
 * signature.
 *
 * Tpm's own libusb CDC scan cannot follow the move on a host with cdc-acm,
 * which owns those interfaces (every Spike2), so when discovery fails the
 * locator is asked where the chip answers now and that node is tried. On 22727
 * a replugged tap pad came back with the TPM on ttyACM2 and ttyACM1 reused by
 * the NFC port, and without this only a restart recovered it (SB-4407).
 *
 * m_usbDevicePath survives the drop on purpose. It stays the first thing
 * discovery tries, which is all a platform without the libusb CDC scan has to
 * go on, and a USB discovery overwrites it with wherever the chip actually
 * turned up. Finding the chip on I2C leaves it alone rather than blanking it,
 * so a board with both still has a USB hint to fall back to.
 */
Tpm HardwareTpm::tpm() const
{
    const std::lock_guard<std::mutex> lock {m_deviceMutex};
    const auto accept = [this](uint64_t serial, const ByteArray &uuid) {
        return isOurChip(serial, uuid);
    };

    if (m_device.isValid()) {
        Tpm cached {m_device, accept};
        if (cached.ok()) {
            remember(cached.device());
            return cached;
        }

        WRN("Cached HSM device is no longer reachable, rediscovering...");
        m_device = {};
    }

    Tpm discovered {m_busFlags, m_usbDevicePath, accept};
    if (!discovered.ok() && m_busFlags.hasFlag(TpmBus::USB)) {
        if (const auto moved = tpm_locate::movedPath(m_usbDevicePath, m_locateUsb);
            !moved.empty()) {
            INF("TPM CDC port moved from {} to {}", m_usbDevicePath, moved);
            discovered = Tpm {TpmBus::USB, moved, accept};
        }
    }
    if (discovered.ok()) {
        remember(discovered.device());
    }
    return discovered;
}

// The open may have found the chip somewhere other than the path it was given.
void HardwareTpm::remember(const TpmDevice &device) const
{
    m_device = device;
    if (device.bus == TpmBus::USB) {
        m_usbDevicePath = device.usbDevicePath;
    }
}

// Any chip will do while the constructor reads the identity; after that only this one will.
bool HardwareTpm::isOurChip(uint64_t serial, const ByteArray &uuid) const
{
    return !m_identityRead || tpm_identity::isSameChip(m_serial, m_uuid, serial, uuid);
}

bool HardwareTpm::readIdentity()
{
    auto tpm1 = tpm();
    if (!tpm1.ok() || !tpm1.readIdentity()) {
        m_serial = 0;
        m_uuid.clear();

        // A chip we cannot read an identity out of is not one worth caching a
        // route to, even if opening it happened to succeed.
        const std::lock_guard<std::mutex> lock {m_deviceMutex};
        m_device = {};
        return false;
    }

    m_serial = tpm1.serialNumber();
    m_uuid = tpm1.uuid();
    // tpm() has already cached the location this was reached on.
    return true;
}
