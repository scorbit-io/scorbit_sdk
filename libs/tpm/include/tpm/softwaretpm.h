/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Jun 2020
 *
 ****************************************************************************/

#pragma once

#include "itpm.h"

class SoftwareTpm : public ITpm
{
public:
    SoftwareTpm(const std::string &keyFile, utils::ByteArray verifyPublicKey);
    ~SoftwareTpm() override;

    void readData(const std::string &filename);
    bool isValid() const override;
    std::string provider() const override;
    uint64_t serial() const override;
    std::string uuid() const override;

    std::string signMessage(const utils::ByteArray &message) const override;
    utils::ByteArray signDigest(const utils::ByteArray &digest) const override;

    bool startProvisioning(const std::string &filename, uint64_t serial, const utils::ByteArray &uuid,
                           utils::ByteArray signKey);
    utils::ByteArray publicKey() const;

private:
    bool isValidData() const;
    utils::ByteArray getCompiledMessageDigest() const;
    void clear();

private:
    uint64_t m_serial;
    utils::ByteArray m_uuid;
    utils::ByteArray m_key;
    utils::ByteArray m_publicKey;
    utils::ByteArray m_verifyPublicKey; // This is to ensure that keyfile it's not corrupted
    utils::ByteArray m_initArray;
};
