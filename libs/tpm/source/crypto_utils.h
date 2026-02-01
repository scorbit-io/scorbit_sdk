/*
 * scorbitd
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * Proprietary License
 */

#pragma once

#include <utils/bytearray.h>
#include <openssl/evp.h>
#include <openssl/ecdsa.h>
#include <openssl/bn.h>
#include <openssl/param_build.h>
#include <memory>
#include <string>

using ByteArray = utils::ByteArray;

namespace tpm {
namespace crypto {

// RAII wrappers for OpenSSL types
template<typename T, void (*F)(T *)>
using ossl_ptr = std::unique_ptr<T, decltype(F)>;

using EVP_MD_CTX_ptr = ossl_ptr<EVP_MD_CTX, EVP_MD_CTX_free>;
using EVP_PKEY_ptr = ossl_ptr<EVP_PKEY, EVP_PKEY_free>;
using EVP_PKEY_CTX_ptr = ossl_ptr<EVP_PKEY_CTX, EVP_PKEY_CTX_free>;
using OSSL_PARAM_BLD_ptr = ossl_ptr<OSSL_PARAM_BLD, OSSL_PARAM_BLD_free>;
using OSSL_PARAM_ptr = ossl_ptr<OSSL_PARAM, OSSL_PARAM_free>;
using ECDSA_SIG_ptr = ossl_ptr<ECDSA_SIG, ECDSA_SIG_free>;
using BN_ptr = ossl_ptr<BIGNUM, BN_free>;

// Utility functions
std::string getOpenSSLErrorString();
bool validateEcdsaInputs(const ByteArray &key, const ByteArray &hash, size_t expectedKeySize,
                         size_t expectedHashSize);

// Key creation helpers
EVP_PKEY_ptr createEcdsaPrivateKey(const ByteArray &privateKey);
EVP_PKEY_ptr createEcdsaPublicKey(const ByteArray &publicKey);

} // namespace crypto
} // namespace tpm
