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
#include <mutex>

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
    bool isOurChip(const Tpm &tpm) const;
    bool readIdentity();

private:
    TpmBusFlags m_busFlags;

    /// The bus and path the device was last reached on, cached so that routine
    /// operations skip discovery, and dropped as soon as it stops working --
    /// see HardwareTpm::tpm(). Mutable because that refresh has to happen from
    /// the const signing path, and guarded because a signature can be asked
    /// for from more than one thread.
    mutable std::string m_usbDevicePath;
    mutable TpmDevice m_device;
    mutable std::mutex m_deviceMutex;

    uint64_t m_serial {0};
    ByteArray m_uuid;
};
