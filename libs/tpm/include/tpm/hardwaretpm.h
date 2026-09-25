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
    // Holds the cached route and its mutex; shared by pointer, never copied.
    HardwareTpm(const HardwareTpm &) = delete;
    HardwareTpm &operator=(const HardwareTpm &) = delete;

    bool hasTpm() const;

    bool isValid() const override;
    std::string provider() const override;
    uint64_t serial() const override;
    std::string uuid() const override;

    std::string signMessage(const utils::ByteArray &message) const override;
    utils::ByteArray signDigest(const utils::ByteArray &digest) const override;

private:
    Tpm tpm() const;
    bool isOurChip(uint64_t serial, const ByteArray &uuid) const;
    void remember(const TpmDevice &device) const;
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

    bool m_identityRead {false};
    uint64_t m_serial {0};
    ByteArray m_uuid;
};
