#include <logger/logger.h>
#include <doctest/doctest.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "crypto_utils.h"
#include "crypto_constants.h"
#include <openssl/err.h>
#include <string>
#include <vector>

using namespace tpm::crypto;

// ============================================================================
// OpenSSL Error String Tests
// ============================================================================

TEST_SUITE("OpenSSL Error String Tests")
{
    TEST_CASE("getOpenSSLErrorString - No errors")
    {
        // Clear any existing errors
        ERR_clear_error();

        std::string errorStr = getOpenSSLErrorString();
        CHECK(errorStr.empty());
    }

    TEST_CASE("getOpenSSLErrorString - With errors")
    {
        // Clear any existing errors
        ERR_clear_error();

        // Try to decode invalid DER as a private key
        const unsigned char bogus[] = {0x01, 0x02, 0x03};
        const unsigned char* p = bogus;
        EVP_PKEY* pkey = d2i_PrivateKey(EVP_PKEY_RSA, nullptr, &p, sizeof(bogus));
        REQUIRE(pkey == nullptr);

        std::string errorStr = getOpenSSLErrorString();
        CHECK_FALSE(errorStr.empty());
    }
}

// ============================================================================
// Input Validation Tests
// ============================================================================

TEST_SUITE("Input Validation Tests")
{
    TEST_CASE("validateEcdsaInputs - Valid inputs")
    {
        ByteArray key(kP256KeySize, 0xAA);
        ByteArray hash(kSHA256Size, 0xBB);

        bool result = validateEcdsaInputs(key, hash, kP256KeySize, kSHA256Size);
        CHECK(result == true);
    }

    TEST_CASE("validateEcdsaInputs - Invalid key size")
    {
        ByteArray key(kP256KeySize - 1, 0xAA); // Wrong size
        ByteArray hash(kSHA256Size, 0xBB);

        bool result = validateEcdsaInputs(key, hash, kP256KeySize, kSHA256Size);
        CHECK(result == false);
    }

    TEST_CASE("validateEcdsaInputs - Invalid hash size")
    {
        ByteArray key(kP256KeySize, 0xAA);
        ByteArray hash(kSHA256Size - 1, 0xBB); // Wrong size

        bool result = validateEcdsaInputs(key, hash, kP256KeySize, kSHA256Size);
        CHECK(result == false);
    }

    TEST_CASE("validateEcdsaInputs - Both invalid sizes")
    {
        ByteArray key(kP256KeySize - 1, 0xAA); // Wrong size
        ByteArray hash(kSHA256Size - 1, 0xBB); // Wrong size

        bool result = validateEcdsaInputs(key, hash, kP256KeySize, kSHA256Size);
        CHECK(result == false);
    }
}

// ============================================================================
// Key Creation Tests
// ============================================================================

TEST_SUITE("Key Creation Tests")
{
    TEST_CASE("createEcdsaPrivateKey - Valid private key")
    {
        // Create a valid P-256 private key
        ByteArray privateKey("1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF");

        EVP_PKEY_ptr pkey = createEcdsaPrivateKey(privateKey);
        CHECK(pkey != nullptr);
    }

    TEST_CASE("createEcdsaPrivateKey - Invalid size")
    {
        // Create an invalid size private key
        ByteArray privateKey(kP256KeySize - 1, 0xAA);

        EVP_PKEY_ptr pkey = createEcdsaPrivateKey(privateKey);
        CHECK(pkey == nullptr);
    }

    TEST_CASE("createEcdsaPublicKey - Valid public key")
    {
        // Create a valid P-256 raw public key
        ByteArray publicKey("04"
                            "f2044829723d462a797a7d067e8cda7728eee12643548c95ce17dc366f98809b"
                            "1e7644dbc54b41e7a32ff90174371f5588518dcaa20b7e194746752c42417663");

        EVP_PKEY_ptr pkey = createEcdsaPublicKey(publicKey);
        INFO(getOpenSSLErrorString());
        CHECK(pkey != nullptr);
    }

    TEST_CASE("createEcdsaPublicKey - Invalid size")
    {
        // Create an invalid size public key
        ByteArray publicKey(kP256PubKeySize - 1, 0xAA);

        EVP_PKEY_ptr pkey = createEcdsaPublicKey(publicKey);
        CHECK(pkey == nullptr);
    }

    TEST_CASE("createEcdsaPublicKey - Invalid format")
    {
        // Create a public key with wrong prefix
        ByteArray publicKey(kP256PubKeySize, 0xAA);
        publicKey[0] = 0x03; // Wrong prefix

        EVP_PKEY_ptr pkey = createEcdsaPublicKey(publicKey);
        CHECK(pkey == nullptr);
    }

    TEST_CASE("createEcdsaPublicKey - Wrong prefix")
    {
        // Create a public key with wrong prefix
        ByteArray publicKey(kP256PubKeySize, 0xAA);
        publicKey[0] = 0x02; // Compressed format prefix

        EVP_PKEY_ptr pkey = createEcdsaPublicKey(publicKey);
        CHECK(pkey == nullptr);
    }
}

// ============================================================================
// Edge Cases and Stress Tests
// ============================================================================

TEST_SUITE("Edge Cases and Stress Tests")
{
    TEST_CASE("validateEcdsaInputs - Zero-sized inputs")
    {
        ByteArray emptyKey;
        ByteArray emptyHash;

        bool result = validateEcdsaInputs(emptyKey, emptyHash, 0, 0);
        CHECK(result == true);
    }
}
