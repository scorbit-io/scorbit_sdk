/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Jun 2020
 *
 ****************************************************************************/

#include "tpm/hardwaretpm.h"

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
    return tpm().signMessage(KEY4_SLOT, message).hex();
}

ByteArray HardwareTpm::signDigest(const ByteArray &digest) const
{
    return tpm().signDigest(KEY4_SLOT, digest);
}

Tpm HardwareTpm::tpm() const
{
    if (m_device.isValid()) {
        return Tpm(m_device);
    }

    return Tpm(m_busFlags, m_usbDevicePath);
}

bool HardwareTpm::readIdentity()
{
    auto tpm1 = tpm();
    if (!tpm1.ok() || !tpm1.readSerialUuid()) {
        m_serial = 0;
        m_uuid.clear();
        return false;
    }

    m_serial = tpm1.serialNumber();
    m_uuid = tpm1.uuid();
    m_device = tpm1.device();
    return true;
}
