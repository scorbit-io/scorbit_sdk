/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Jun 2020
 *
 ****************************************************************************/

#pragma once

#include "itpm.h"
#include "tpm.h"
#include <memory>

class HardwareTpm : public ITpm
{
public:
    HardwareTpm(TpmBusFlags busFlags, const std::string &usbDevicePath = {});
    ~HardwareTpm() override;

    bool hasTpm() const;

    bool isValid() const override;
    std::string provider() const override;
    uint64_t serial() const override;
    std::string uuid() const override;

    std::string signMessage(const utils::ByteArray &message) const override;
    utils::ByteArray signDigest(const utils::ByteArray &digest) const override;

private:
    Tpm tpm() const;
    bool readIdentity();

private:
    TpmBusFlags m_busFlags;
    std::string m_usbDevicePath;

    uint64_t m_serial {0};
    ByteArray m_uuid;
};
