/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Jun 2020
 *
 ****************************************************************************/

#include "tpm/hardwaretpm.h"

#include <logger/logger.h>

using ByteArray = utils::ByteArray;

HardwareTpm::HardwareTpm(TpmBusFlags busFlags, const std::string &usbDevicePath)
    : m_busFlags(busFlags)
    , m_usbDevicePath(usbDevicePath)
{
    readIdentity();
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
 * forever, and the next call falls back to full discovery. Tpm's USB path
 * scans libusb for CDC devices, which survives ttyACM renumbering. On success
 * the cache is refreshed so the recovery costs one failed open, not one per
 * signature.
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

    if (m_device.isValid()) {
        Tpm cached {m_device};
        if (cached.ok()) {
            return cached;
        }

        WRN("Cached HSM device is no longer reachable, rediscovering...");
        m_device = {};
    }

    Tpm discovered {m_busFlags, m_usbDevicePath};
    if (discovered.ok()) {
        m_device = discovered.device();
        if (m_device.bus == TpmBus::USB) {
            m_usbDevicePath = m_device.usbDevicePath;
        }
    }

    return discovered;
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
