/*
 * scorbitd - Refactored Implementation
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * Proprietary License
 */

#include <tpm/crypto_helpers.h>
#include "crypto_constants.h"
#include "crypto_utils.h"
#include <logger/logger.h>

#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <openssl/ecdsa.h>
#include <openssl/err.h>
#include <openssl/bn.h>

using namespace tpm::crypto;

// --- Helpers: DER <-> RAW -------------------------------------------------

static bool derToRaw(const uint8_t *der, size_t der_len, ByteArray &raw)
{
    const unsigned char *p = der;

    ECDSA_SIG_ptr sig(d2i_ECDSA_SIG(nullptr, &p, der_len), ECDSA_SIG_free);
    if (!sig) {
        ERR("d2i_ECDSA_SIG failed: {}", getOpenSSLErrorString());
        return false;
    }

    const BIGNUM *r = nullptr;
    const BIGNUM *s = nullptr;
    ECDSA_SIG_get0(sig.get(), &r, &s);
    if (!r || !s) {
        ERR("ECDSA_SIG_get0 failed: {}", getOpenSSLErrorString());
        return false;
    }

    raw.resize(kP256SigSize);
    if (BN_bn2binpad(r, raw.data(), kP256KeySize) != static_cast<int>(kP256KeySize)) {
        ERR("BN_bn2binpad(r) failed: {}", getOpenSSLErrorString());
        return false;
    }
    if (BN_bn2binpad(s, raw.data() + kP256KeySize, kP256KeySize)
        != static_cast<int>(kP256KeySize)) {
        ERR("BN_bn2binpad(s) failed: {}", getOpenSSLErrorString());
        return false;
    }
    return true;
}

static bool rawToDer(const ByteArray &raw, ByteArray &der)
{
    if (raw.size() != kP256SigSize) {
        ERR(kErrorInvalidSignatureSize, raw.size(), kP256SigSize);
        return false;
    }

    BN_ptr r(BN_bin2bn(raw.data(), kP256KeySize, nullptr), BN_free);
    BN_ptr s(BN_bin2bn(raw.data() + kP256KeySize, kP256KeySize, nullptr), BN_free);
    if (!r || !s) {
        ERR("BN_bin2bn failed: {}", getOpenSSLErrorString());
        return false;
    }

    ECDSA_SIG_ptr sig(ECDSA_SIG_new(), ECDSA_SIG_free);
    if (!sig) {
        ERR("ECDSA_SIG_new failed: {}", getOpenSSLErrorString());
        return false;
    }
    if (ECDSA_SIG_set0(sig.get(), r.release(), s.release()) != 1) {
        ERR("ECDSA_SIG_set0 failed: {}", getOpenSSLErrorString());
        return false;
    }

    int len = i2d_ECDSA_SIG(sig.get(), nullptr);
    if (len <= 0) {
        ERR("i2d_ECDSA_SIG length failed: {}", getOpenSSLErrorString());
        return false;
    }
    der.resize(len);
    unsigned char *p = der.data();
    if (i2d_ECDSA_SIG(sig.get(), &p) != len) {
        ERR("i2d_ECDSA_SIG encode failed: {}", getOpenSSLErrorString());
        return false;
    }
    return true;
}

// --- API ------------------------------------------------------------------

ByteArray sha256Hash(const ByteArray &data)
{
    ByteArray out(kSHA256Size);

    EVP_MD_CTX_ptr ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx) {
        ERR("EVP_MD_CTX_new failed: {}", getOpenSSLErrorString());
        return {};
    }

    unsigned int digest_len = 0;
    if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1
        || EVP_DigestUpdate(ctx.get(), data.data(), data.size()) != 1
        || EVP_DigestFinal_ex(ctx.get(), out.data(), &digest_len) != 1
        || digest_len != kSHA256Size) {
        ERR("SHA256 digest failed: {}", getOpenSSLErrorString());
        return {};
    }

    return out;
}

bool ecdsaSign(const ByteArray &privateKey, const ByteArray &hash, ByteArray &signature)
{
    signature.clear();

    // Input validation using utility function
    if (!validateEcdsaInputs(privateKey, hash, kP256KeySize, kSHA256Size)) {
        return false;
    }

    // Create private key using utility function
    EVP_PKEY_ptr pkey = createEcdsaPrivateKey(privateKey);
    if (!pkey) {
        return false;
    }

    // Sign
    EVP_PKEY_CTX_ptr ctx(EVP_PKEY_CTX_new_from_pkey(nullptr, pkey.get(), nullptr),
                         EVP_PKEY_CTX_free);
    if (!ctx) {
        ERR("EVP_PKEY_CTX_new_from_pkey failed: {}", getOpenSSLErrorString());
        return false;
    }

    if (EVP_PKEY_sign_init(ctx.get()) != 1) {
        ERR("EVP_PKEY_sign_init failed: {}", getOpenSSLErrorString());
        return false;
    }

    size_t der_len = 0;
    if (EVP_PKEY_sign(ctx.get(), nullptr, &der_len, hash.data(), hash.size()) != 1) {
        ERR("EVP_PKEY_sign (len) failed: {}", getOpenSSLErrorString());
        return false;
    }

    ByteArray der(der_len);
    if (EVP_PKEY_sign(ctx.get(), der.data(), &der_len, hash.data(), hash.size()) != 1) {
        ERR("EVP_PKEY_sign failed: {}", getOpenSSLErrorString());
        return false;
    }
    der.resize(der_len);

    return derToRaw(der.data(), der.size(), signature);
}

bool ecdsaVerify(const ByteArray &publicKey, const ByteArray &hash, const ByteArray &signature)
{
    // Input validation
    if (publicKey.size() != kP256PubKeySize) {
        ERR(kErrorInvalidKeySize, publicKey.size(), kP256PubKeySize);
        return false;
    }
    if (publicKey[0] != kUncompressedKeyPrefix) {
        ERR(kErrorInvalidPublicKeyFormat);
        return false;
    }
    if (hash.size() != kSHA256Size) {
        ERR(kErrorInvalidHashSize, hash.size(), kSHA256Size);
        return false;
    }
    if (signature.size() != kP256SigSize) {
        ERR(kErrorInvalidSignatureSize, signature.size(), kP256SigSize);
        return false;
    }

    // Create public key using utility function
    EVP_PKEY_ptr pkey = createEcdsaPublicKey(publicKey);
    if (!pkey) {
        return false;
    }

    // Verify
    EVP_PKEY_CTX_ptr ctx(EVP_PKEY_CTX_new_from_pkey(nullptr, pkey.get(), nullptr),
                         EVP_PKEY_CTX_free);
    if (!ctx) {
        ERR("EVP_PKEY_CTX_new_from_pkey failed");
        return false;
    }

    if (EVP_PKEY_verify_init(ctx.get()) != 1) {
        ERR("EVP_PKEY_verify_init failed: {}", getOpenSSLErrorString());
        return false;
    }

    ByteArray der;
    if (!rawToDer(signature, der)) {
        ERR("rawToDer failed");
        return false;
    }

    int rc = EVP_PKEY_verify(ctx.get(), der.data(), der.size(), hash.data(), hash.size());
    if (rc != 1) {
        ERR("EVP_PKEY_verify failed with result: {}: {}", rc, getOpenSSLErrorString());
        return false;
    }

    return true;
}

bool generateEcdsaKeyPair(ByteArray &publicKey, ByteArray &privateKey)
{
    publicKey.clear();
    privateKey.clear();

    EVP_PKEY_CTX_ptr kctx(EVP_PKEY_CTX_new_from_name(nullptr, kEcAlgName, nullptr),
                          EVP_PKEY_CTX_free);
    if (!kctx) {
        ERR("EVP_PKEY_CTX_new_from_name failed: {}", getOpenSSLErrorString());
        return false;
    }

    if (EVP_PKEY_keygen_init(kctx.get()) != 1) {
        ERR("EVP_PKEY_keygen_init failed: {}", getOpenSSLErrorString());
        return false;
    }

    // Use parameter builder approach for consistency
    OSSL_PARAM_BLD_ptr bld(OSSL_PARAM_BLD_new(), OSSL_PARAM_BLD_free);
    if (!bld) {
        ERR("OSSL_PARAM_BLD_new failed: {}", getOpenSSLErrorString());
        return false;
    }

    if (OSSL_PARAM_BLD_push_utf8_string(bld.get(), OSSL_PKEY_PARAM_GROUP_NAME, kEcGroupName, 0)
        != 1) {
        ERR("OSSL_PARAM_BLD_push failed: {}", getOpenSSLErrorString());
        return false;
    }

    OSSL_PARAM_ptr params(OSSL_PARAM_BLD_to_param(bld.get()), OSSL_PARAM_free);
    if (!params) {
        ERR("OSSL_PARAM_BLD_to_param failed: {}", getOpenSSLErrorString());
        return false;
    }

    if (EVP_PKEY_CTX_set_params(kctx.get(), params.get()) != 1) {
        ERR("EVP_PKEY_CTX_set_params failed: {}", getOpenSSLErrorString());
        return false;
    }

    EVP_PKEY *raw_pkey = nullptr;
    if (EVP_PKEY_generate(kctx.get(), &raw_pkey) != 1) {
        ERR("EVP_PKEY_generate failed: {}", getOpenSSLErrorString());
        return false;
    }
    EVP_PKEY_ptr pkey(raw_pkey, EVP_PKEY_free);

    // Extract private key using parameter approach
    BIGNUM *priv_bn = nullptr;
    if (EVP_PKEY_get_bn_param(pkey.get(), OSSL_PKEY_PARAM_PRIV_KEY, &priv_bn) != 1 || !priv_bn) {
        ERR("EVP_PKEY_get_bn_param for private key failed: {}", getOpenSSLErrorString());
        return false;
    }
    BN_ptr priv_bn_ptr(priv_bn, BN_free); // Wrap in smart pointer

    privateKey.resize(kP256KeySize);
    if (BN_bn2binpad(priv_bn_ptr.get(), privateKey.data(), kP256KeySize)
        != static_cast<int>(kP256KeySize)) {
        ERR("BN_bn2binpad for private key failed: {}", getOpenSSLErrorString());
        return false;
    }

    // Extract public key using parameter approach
    size_t pub_len = 0;
    if (EVP_PKEY_get_octet_string_param(pkey.get(), OSSL_PKEY_PARAM_PUB_KEY, nullptr, 0, &pub_len)
                != 1
        || pub_len != kP256PubKeySize) {
        ERR("EVP_PKEY_get_octet_string_param length failed, got {} expected {}", pub_len,
            kP256PubKeySize);
        return false;
    }

    publicKey.resize(kP256PubKeySize);
    if (EVP_PKEY_get_octet_string_param(pkey.get(), OSSL_PKEY_PARAM_PUB_KEY, publicKey.data(),
                                        publicKey.size(), &pub_len)
        != 1) {
        ERR("EVP_PKEY_get_octet_string_param failed: {}", getOpenSSLErrorString());
        return false;
    }

    return true;
}
