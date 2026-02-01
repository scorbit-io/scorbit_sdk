/*
 * scorbitd
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * Proprietary License
 */

#include "crypto_utils.h"
#include "crypto_constants.h"
#include <logger/logger.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <string>

namespace tpm {
namespace crypto {

std::string getOpenSSLErrorString()
{
    std::string result;
    result.reserve(512);

    unsigned long err;
    char err_buf[256];

    while ((err = ERR_get_error()) != 0) {
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        if (!result.empty()) {
            result += "; ";
        }
        result += err_buf;
    }

    return result;
}

bool validateEcdsaInputs(const ByteArray &key, const ByteArray &hash, size_t expectedKeySize,
                         size_t expectedHashSize)
{
    if (key.size() != expectedKeySize) {
        ERR(kErrorInvalidKeySize, key.size(), expectedKeySize);
        return false;
    }
    if (hash.size() != expectedHashSize) {
        ERR(kErrorInvalidHashSize, hash.size(), expectedHashSize);
        return false;
    }
    return true;
}

EVP_PKEY_ptr createEcdsaPrivateKey(const ByteArray &privateKey)
{
    if (privateKey.size() != kP256KeySize) {
        ERR(kErrorInvalidKeySize, privateKey.size(), kP256KeySize);
        return EVP_PKEY_ptr(nullptr, EVP_PKEY_free);
    }

    EVP_PKEY_CTX_ptr kctx(EVP_PKEY_CTX_new_from_name(nullptr, kEcAlgName, nullptr),
                          EVP_PKEY_CTX_free);
    if (!kctx) {
        ERR("EVP_PKEY_CTX_new_from_name failed: {}", getOpenSSLErrorString());
        return EVP_PKEY_ptr(nullptr, EVP_PKEY_free);
    }

    // Build params
    OSSL_PARAM_BLD_ptr bld(OSSL_PARAM_BLD_new(), OSSL_PARAM_BLD_free);
    if (!bld) {
        ERR("OSSL_PARAM_BLD_new failed: {}", getOpenSSLErrorString());
        return EVP_PKEY_ptr(nullptr, EVP_PKEY_free);
    }

    // Convert private key bytes to BIGNUM
    BN_ptr priv_key_bn(BN_bin2bn(privateKey.data(), privateKey.size(), nullptr), BN_free);
    if (!priv_key_bn) {
        ERR("BN_bin2bn failed for private key: {}", getOpenSSLErrorString());
        return EVP_PKEY_ptr(nullptr, EVP_PKEY_free);
    }

    if (OSSL_PARAM_BLD_push_utf8_string(bld.get(), OSSL_PKEY_PARAM_GROUP_NAME, kEcGroupName, 0) != 1
        || OSSL_PARAM_BLD_push_BN(bld.get(), OSSL_PKEY_PARAM_PRIV_KEY, priv_key_bn.get()) != 1) {
        ERR("OSSL_PARAM_BLD_push failed: {}", getOpenSSLErrorString());
        return EVP_PKEY_ptr(nullptr, EVP_PKEY_free);
    }

    OSSL_PARAM_ptr params(OSSL_PARAM_BLD_to_param(bld.get()), OSSL_PARAM_free);
    if (!params) {
        ERR("OSSL_PARAM_BLD_to_param failed: {}", getOpenSSLErrorString());
        return EVP_PKEY_ptr(nullptr, EVP_PKEY_free);
    }

    // Create key
    EVP_PKEY *raw_pkey = nullptr;
    if (EVP_PKEY_fromdata_init(kctx.get()) != 1
        || EVP_PKEY_fromdata(kctx.get(), &raw_pkey, EVP_PKEY_PRIVATE_KEY, params.get()) != 1) {
        ERR("EVP_PKEY_fromdata failed: {}", getOpenSSLErrorString());
        return EVP_PKEY_ptr(nullptr, EVP_PKEY_free);
    }

    return EVP_PKEY_ptr(raw_pkey, EVP_PKEY_free);
}

EVP_PKEY_ptr createEcdsaPublicKey(const ByteArray &publicKey)
{
    if (publicKey.size() != kP256PubKeySize) {
        ERR(kErrorInvalidKeySize, publicKey.size(), kP256PubKeySize);
        return EVP_PKEY_ptr(nullptr, EVP_PKEY_free);
    }
    if (publicKey[0] != kUncompressedKeyPrefix) {
        ERR(kErrorInvalidPublicKeyFormat);
        return EVP_PKEY_ptr(nullptr, EVP_PKEY_free);
    }

    EVP_PKEY_CTX_ptr kctx(EVP_PKEY_CTX_new_from_name(nullptr, kEcAlgName, nullptr),
                          EVP_PKEY_CTX_free);
    if (!kctx) {
        ERR("EVP_PKEY_CTX_new_from_name failed: {}", getOpenSSLErrorString());
        return EVP_PKEY_ptr(nullptr, EVP_PKEY_free);
    }

    // Build params
    OSSL_PARAM_BLD_ptr bld(OSSL_PARAM_BLD_new(), OSSL_PARAM_BLD_free);
    if (!bld) {
        ERR("OSSL_PARAM_BLD_new failed: {}", getOpenSSLErrorString());
        return EVP_PKEY_ptr(nullptr, EVP_PKEY_free);
    }

    if (OSSL_PARAM_BLD_push_utf8_string(bld.get(), OSSL_PKEY_PARAM_GROUP_NAME, kEcGroupName, 0) != 1
        || OSSL_PARAM_BLD_push_octet_string(bld.get(), OSSL_PKEY_PARAM_PUB_KEY, publicKey.data(),
                                            publicKey.size())
                   != 1) {
        ERR("OSSL_PARAM_BLD_push failed: {}", getOpenSSLErrorString());
        return EVP_PKEY_ptr(nullptr, EVP_PKEY_free);
    }

    OSSL_PARAM_ptr params(OSSL_PARAM_BLD_to_param(bld.get()), OSSL_PARAM_free);
    if (!params) {
        ERR("OSSL_PARAM_BLD_to_param failed: {}", getOpenSSLErrorString());
        return EVP_PKEY_ptr(nullptr, EVP_PKEY_free);
    }

    // Create key
    EVP_PKEY *raw_pkey = nullptr;
    if (EVP_PKEY_fromdata_init(kctx.get()) != 1
        || EVP_PKEY_fromdata(kctx.get(), &raw_pkey, EVP_PKEY_PUBLIC_KEY, params.get()) != 1) {
        ERR("EVP_PKEY_fromdata failed: {}", getOpenSSLErrorString());
        return EVP_PKEY_ptr(nullptr, EVP_PKEY_free);
    }

    return EVP_PKEY_ptr(raw_pkey, EVP_PKEY_free);
}

} // namespace crypto
} // namespace tpm
