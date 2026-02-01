/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Jun 2020
 *
 ****************************************************************************/

#include "tpm/softwaretpm.h"
#include "crypto_helpers.h"

#include <nlohmann/json.hpp>
#include <logger/logger.h>
#include "utils/utils.h"

#include <openssl/evp.h>
#include <openssl/provider.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>

constexpr int SHA256_DIGEST_SIZE = 32;
constexpr int ECCP256_SIG_SIZE = 64;
constexpr int ECCP256_KEY_SIZE = 32;

using ByteArray = utils::ByteArray;

SoftwareTpm::SoftwareTpm(const std::string &keyFile, ByteArray verifyPublicKey)
    : m_verifyPublicKey {verifyPublicKey}
    , m_initArray(SHA256_DIGEST_SIZE, 0)
{
    const std::array<uint8_t, 3> INIT {'d', 't', 'm'};
    m_initArray = sha256Hash(ByteArray(INIT.data(), INIT.size()));

    readData(keyFile);
}

SoftwareTpm::~SoftwareTpm()
{
}

bool SoftwareTpm::isValid() const
{
    return isValidData();
}

std::string SoftwareTpm::provider() const
{
    return SW_PROVIDER;
}

uint64_t SoftwareTpm::serial() const
{
    return m_serial;
}

std::string SoftwareTpm::uuid() const
{
    return m_uuid.hex();
}

std::string SoftwareTpm::signMessage(const ByteArray &message) const
{
    ByteArray digest = sha256Hash(message);
    return signDigest(sha256Hash(message)).hex();
}

ByteArray SoftwareTpm::signDigest(const ByteArray &digest) const
{
    if (!isValidData()) {
        ERR("Data is not valid: uuid size = {}, key size = {}", m_uuid.size(), m_key.size());
        return {};
    }

    ByteArray signature(ECCP256_SIG_SIZE);

    if (ecdsaSign(m_key, digest, signature)) {
        return signature;
    }

    return {};
}

bool SoftwareTpm::startProvisioning(const std::string &filename, uint64_t serial,
                                    const ByteArray &uuid, ByteArray signKey)
{
    m_serial = serial;
    m_uuid = uuid;
    signKey.initialize(m_initArray);

    if (!generateEcdsaKeyPair(m_publicKey, m_key)) {
        ERR("Failed to generate ECDSA key pair");
        return false;
    }

    auto digest = getCompiledMessageDigest();
    ByteArray signature(ECCP256_SIG_SIZE);

    if (!ecdsaSign(signKey, digest, signature)) {
        ERR("{}, {}: couldn't sign message", __func__, __LINE__);
        return false;
    }

    ByteArray initializedKey(m_key);
    initializedKey.initialize(m_initArray);

    nlohmann::json jsonObj;
    jsonObj["serial"] = m_serial;
    jsonObj["uuid"] = m_uuid.hex();
    jsonObj["key"] = initializedKey.hex();
    jsonObj["signature"] = signature.hex();

    if (!utils::writeJsonFile(filename, jsonObj)) {
        return false;
    }

    readData(filename);
    return isValid();
}

ByteArray SoftwareTpm::publicKey() const
{
    return m_publicKey;
}

void SoftwareTpm::readData(const std::string &filename)
{
    nlohmann::json json = utils::readJsonFile(filename);
    if (json.empty())
        return;

    m_serial = json.value("serial", 0);
    m_uuid = ByteArray(json.value("uuid", ""));
    m_key = ByteArray(json.value("key", ""));
    // Make sure to call SoftwareTpm::isValid() as readData called from constructor
    if (!SoftwareTpm::isValid()) {
        ERR("Keyfile is invalid!");
        clear();
        return;
    }

    ByteArray signature(json.value("signature", ""));

    m_key.initialize(m_initArray);

    // check signature
    auto digest = getCompiledMessageDigest();

    if (!ecdsaVerify(m_verifyPublicKey, digest, signature)) {
        ERR("Keyfile is invalid!");
        clear();
    }
}

bool SoftwareTpm::isValidData() const
{
    return m_uuid.size() == 16 && m_key.size() == ECCP256_KEY_SIZE;
}

ByteArray SoftwareTpm::getCompiledMessageDigest() const
{
    std::string serialStr = std::to_string(m_serial);
    ByteArray message(serialStr.cbegin(), serialStr.cend());
    message.insert(message.end(), m_uuid.cbegin(), m_uuid.cend());
    message.insert(message.end(), m_key.cbegin(), m_key.cend());

    return sha256Hash(message);
}

void SoftwareTpm::clear()
{
    m_serial = 0;
    m_uuid.clear();
    m_key.clear();
}
