/*
 * scorbitd
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * Proprietary License
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace tpm {
namespace crypto {

// ECDSA constants
constexpr const char *kEcAlgName = "EC";
constexpr const char *kEcGroupName = "prime256v1"; // P-256 curve
constexpr size_t kP256KeySize = 32;                // Private key size for P-256
constexpr size_t kP256PubKeySize = 65;             // Uncompressed public key size (0x04 + 32 + 32)
constexpr size_t kP256SigSize = 64;                // Signature size (r + s, 32 bytes each)
constexpr size_t kSHA256Size = 32;                 // SHA-256 digest size

// Public key format constants
constexpr uint8_t kUncompressedKeyPrefix = 0x04;

// Error message constants
constexpr const char *kErrorInvalidKeySize = "Invalid key size: {} (expected {})";
constexpr const char *kErrorInvalidHashSize = "Invalid hash size: {} (expected {})";
constexpr const char *kErrorInvalidSignatureSize = "Invalid signature size: {} (expected {})";
constexpr const char *kErrorInvalidPublicKeyFormat =
        "Invalid public key format (expected uncompressed format starting with 0x04)";

} // namespace crypto
} // namespace tpm
