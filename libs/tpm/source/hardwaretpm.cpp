/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Jun 2020
 *
 ****************************************************************************/

#include "tpm/hardwaretpm.h"

using ByteArray = utils::ByteArray;

HardwareTpm::HardwareTpm(TpmBusFlags busFlags)
    : m_tpm {std::make_unique<Tpm>(busFlags)}
{
}

HardwareTpm::~HardwareTpm()
{
}

bool HardwareTpm::hasTpm() const
{
    return m_tpm->ok();
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

    return sum % 10 == 0 && m_tpm->uuid().size() == 16;
}

std::string HardwareTpm::provider() const
{
    return HW_PROVIDER;
}

uint64_t HardwareTpm::serial() const
{
    return m_tpm->serialNumber();
}

std::string HardwareTpm::uuid() const
{
    return m_tpm->uuid().hex();
}

std::string HardwareTpm::signMessage(const ByteArray &message) const
{
    return m_tpm->signMessage(KEY4_SLOT, message).hex();
}

ByteArray HardwareTpm::signDigest(const ByteArray &digest) const
{
    return m_tpm->signDigest(KEY4_SLOT, digest);
}
