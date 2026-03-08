#include <logger/logger.h>
#include <doctest/doctest.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "crypto_helpers.h"
#include <string>
#include <vector>
#include <algorithm>

// Test constants
constexpr size_t kP256KeySize = 32;
constexpr size_t kP256PubKeySize = 65;
constexpr size_t kP256SigSize = 64;
constexpr size_t kSHA256Size = 32;

// Helper to check if ByteArray contains only zeros
bool isZeros(const ByteArray &data)
{
    return std::all_of(data.begin(), data.end(), [](uint8_t b) { return b == 0; });
}

// ============================================================================
// SHA-256 Hash Tests
// ============================================================================

TEST_SUITE("SHA-256 Hash Tests")
{
    TEST_CASE("sha256Hash - Empty input")
    {
        ByteArray empty;
        ByteArray hash = sha256Hash(empty);

        CHECK(hash.size() == kSHA256Size);
        CHECK_FALSE(hash.empty());

        // SHA-256 of empty string should be:
        std::string expected = "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855";
        CHECK(hash.hex(ByteArray::Case::UpperCase) == expected);
    }

    TEST_CASE("sha256Hash - Known test vectors")
    {
        SUBCASE("Single byte")
        {
            ByteArray input("00");
            ByteArray hash = sha256Hash(input);
            CHECK(hash.size() == kSHA256Size);
            std::string expected =
                    "6E340B9CFFB37A989CA544E6BB780A2C78901D3FB33738768511A30617AFA01D";
            CHECK(hash.hex(ByteArray::Case::UpperCase) == expected);
        }

        SUBCASE("ASCII text")
        {
            std::string text = "hello world";
            ByteArray input(text.begin(), text.end());
            ByteArray hash = sha256Hash(input);
            CHECK(hash.size() == kSHA256Size);
            // SHA-256("hello world") =
            std::string expected =
                    "B94D27B9934D3E08A52E52D7DA7DABFAC484EFE37A5380EE9088F7ACE2EFCDE9";
            CHECK(hash.hex(ByteArray::Case::UpperCase) == expected);
        }
    }

    TEST_CASE("sha256Hash - Large input")
    {
        // Test with 1MB of data
        ByteArray largeInput(1024 * 1024, 0xAA);
        ByteArray hash = sha256Hash(largeInput);
        CHECK(hash.size() == kSHA256Size);
        CHECK_FALSE(isZeros(hash));
    }

    TEST_CASE("sha256Hash - Deterministic")
    {
        ByteArray input("deadbeef");
        ByteArray hash1 = sha256Hash(input);
        ByteArray hash2 = sha256Hash(input);
        CHECK(hash1 == hash2);
    }
}

// ============================================================================
// Key Generation Tests
// ============================================================================

TEST_SUITE("Key Generation Tests")
{
    TEST_CASE("generateEcdsaKeyPair - Basic functionality")
    {
        ByteArray publicKey, privateKey;
        bool result = generateEcdsaKeyPair(publicKey, privateKey);

        CHECK(result);
        CHECK(privateKey.size() == kP256KeySize);
        CHECK(publicKey.size() == kP256PubKeySize);
        CHECK(publicKey[0] == 0x04); // Uncompressed format
        CHECK_FALSE(isZeros(privateKey));
        CHECK_FALSE(isZeros(publicKey));
    }

    TEST_CASE("generateEcdsaKeyPair - Multiple generations are different")
    {
        ByteArray pubKey1, privKey1;
        ByteArray pubKey2, privKey2;

        CHECK(generateEcdsaKeyPair(pubKey1, privKey1));
        CHECK(generateEcdsaKeyPair(pubKey2, privKey2));

        // Keys should be different
        CHECK(privKey1 != privKey2);
        CHECK(pubKey1 != pubKey2);
    }

    TEST_CASE("generateEcdsaKeyPair - Private key range validation")
    {
        ByteArray publicKey, privateKey;
        CHECK(generateEcdsaKeyPair(publicKey, privateKey));

        // Private key should not be zero or all 0xFF
        CHECK_FALSE(isZeros(privateKey));
        CHECK_FALSE(std::all_of(privateKey.begin(), privateKey.end(),
                                [](uint8_t b) { return b == 0xFF; }));
    }

    TEST_CASE("generateEcdsaKeyPair - Public key format validation")
    {
        ByteArray publicKey, privateKey;
        CHECK(generateEcdsaKeyPair(publicKey, privateKey));

        // First byte should be 0x04 (uncompressed format)
        CHECK(publicKey[0] == 0x04);

        // X and Y coordinates should not be all zeros
        ByteArray xCoord {
                utils::ByteArrayBase {publicKey.begin() + 1, publicKey.begin() + 33}};
        ByteArray yCoord {utils::ByteArrayBase {publicKey.begin() + 33, publicKey.end()}};
        CHECK_FALSE(isZeros(xCoord));
        CHECK_FALSE(isZeros(yCoord));
    }
}

// ============================================================================
// ECDSA Signing Tests
// ============================================================================

TEST_SUITE("ECDSA Signing Tests")
{
    TEST_CASE("ecdsaSign - Basic functionality")
    {
        // Generate a key pair
        ByteArray publicKey, privateKey;
        REQUIRE(generateEcdsaKeyPair(publicKey, privateKey));

        // Create a test hash
        ByteArray testData("deadbeef");
        ByteArray hash = sha256Hash(testData);
        REQUIRE(hash.size() == kSHA256Size);

        // Sign the hash
        ByteArray signature;
        bool result = ecdsaSign(privateKey, hash, signature);

        CHECK(result);
        CHECK(signature.size() == kP256SigSize);
        CHECK_FALSE(isZeros(signature));
    }

    TEST_CASE("ecdsaSign - Invalid private key size")
    {
        ByteArray shortKey(16, 0x01); // Too short
        ByteArray longKey(48, 0x01);  // Too long
        ByteArray validHash(kSHA256Size, 0x02);
        ByteArray signature;

        CHECK_FALSE(ecdsaSign(shortKey, validHash, signature));
        CHECK_FALSE(ecdsaSign(longKey, validHash, signature));
        CHECK(signature.empty());
    }

    TEST_CASE("ecdsaSign - Invalid hash size")
    {
        ByteArray validPrivKey(kP256KeySize, 0x01);
        ByteArray shortHash(16, 0x02); // Too short
        ByteArray longHash(48, 0x02);  // Too long
        ByteArray signature;

        CHECK_FALSE(ecdsaSign(validPrivKey, shortHash, signature));
        CHECK_FALSE(ecdsaSign(validPrivKey, longHash, signature));
        CHECK(signature.empty());
    }

    TEST_CASE("ecdsaSign - Different messages produce different signatures")
    {
        ByteArray publicKey, privateKey;
        REQUIRE(generateEcdsaKeyPair(publicKey, privateKey));

        ByteArray hash1 = sha256Hash(ByteArray {'a'});
        ByteArray hash2 = sha256Hash(ByteArray {'b'});

        ByteArray sig1, sig2;
        CHECK(ecdsaSign(privateKey, hash1, sig1));
        CHECK(ecdsaSign(privateKey, hash2, sig2));

        // Signatures should be different (with extremely high probability)
        CHECK(sig1 != sig2);
    }

    TEST_CASE("ecdsaSign - Same message can produce different signatures")
    {
        ByteArray publicKey, privateKey;
        REQUIRE(generateEcdsaKeyPair(publicKey, privateKey));

        ByteArray hash = sha256Hash(ByteArray {'t', 'e', 's', 't'});

        ByteArray sig1, sig2;
        CHECK(ecdsaSign(privateKey, hash, sig1));
        CHECK(ecdsaSign(privateKey, hash, sig2));

        // ECDSA signatures include randomness, so they might be different
        // (This is expected behavior due to the random k value in ECDSA)
    }
}

// ============================================================================
// ECDSA Verification Tests
// ============================================================================

TEST_SUITE("ECDSA Verification Tests")
{
    TEST_CASE("ecdsaVerify - Basic functionality")
    {
        // Generate a key pair
        ByteArray publicKey, privateKey;
        REQUIRE(generateEcdsaKeyPair(publicKey, privateKey));

        // Create and sign a test hash
        ByteArray testData("cafebabe");
        ByteArray hash = sha256Hash(testData);
        ByteArray signature;
        REQUIRE(ecdsaSign(privateKey, hash, signature));

        // Verify the signature
        bool result = ecdsaVerify(publicKey, hash, signature);
        CHECK(result);
    }

    TEST_CASE("ecdsaVerify - Invalid public key size")
    {
        ByteArray shortPubKey(32, 0x04); // Too short
        ByteArray longPubKey(96, 0x04);  // Too long
        ByteArray validHash(kSHA256Size, 0x02);
        ByteArray validSig(kP256SigSize, 0x03);

        CHECK_FALSE(ecdsaVerify(shortPubKey, validHash, validSig));
        CHECK_FALSE(ecdsaVerify(longPubKey, validHash, validSig));
    }

    TEST_CASE("ecdsaVerify - Invalid public key format")
    {
        ByteArray invalidPubKey(kP256PubKeySize, 0x03); // Should start with 0x04
        ByteArray validHash(kSHA256Size, 0x02);
        ByteArray validSig(kP256SigSize, 0x03);

        CHECK_FALSE(ecdsaVerify(invalidPubKey, validHash, validSig));
    }

    TEST_CASE("ecdsaVerify - Invalid hash size")
    {
        ByteArray validPubKey(kP256PubKeySize, 0x04);
        validPubKey[0] = 0x04;
        ByteArray shortHash(16, 0x02); // Too short
        ByteArray longHash(48, 0x02);  // Too long
        ByteArray validSig(kP256SigSize, 0x03);

        CHECK_FALSE(ecdsaVerify(validPubKey, shortHash, validSig));
        CHECK_FALSE(ecdsaVerify(validPubKey, longHash, validSig));
    }

    TEST_CASE("ecdsaVerify - Invalid signature size")
    {
        ByteArray validPubKey(kP256PubKeySize, 0x04);
        validPubKey[0] = 0x04;
        ByteArray validHash(kSHA256Size, 0x02);
        ByteArray shortSig(32, 0x03); // Too short
        ByteArray longSig(96, 0x03);  // Too long

        CHECK_FALSE(ecdsaVerify(validPubKey, validHash, shortSig));
        CHECK_FALSE(ecdsaVerify(validPubKey, validHash, longSig));
    }

    TEST_CASE("ecdsaVerify - Wrong signature fails verification")
    {
        // Generate two key pairs
        ByteArray pubKey1, privKey1;
        ByteArray pubKey2, privKey2;
        REQUIRE(generateEcdsaKeyPair(pubKey1, privKey1));
        REQUIRE(generateEcdsaKeyPair(pubKey2, privKey2));

        // Sign with first key, try to verify with second key
        ByteArray hash = sha256Hash(ByteArray {'t', 'e', 's', 't'});
        ByteArray signature;
        REQUIRE(ecdsaSign(privKey1, hash, signature));

        // Should fail when verifying with wrong public key
        CHECK_FALSE(ecdsaVerify(pubKey2, hash, signature));

        // But should succeed with correct public key
        CHECK(ecdsaVerify(pubKey1, hash, signature));
    }

    TEST_CASE("ecdsaVerify - Modified hash fails verification")
    {
        ByteArray publicKey, privateKey;
        REQUIRE(generateEcdsaKeyPair(publicKey, privateKey));

        ByteArray originalHash = sha256Hash(ByteArray {'o', 'r', 'i', 'g', 'i', 'n', 'a', 'l'});
        ByteArray modifiedHash = sha256Hash(ByteArray {'m', 'o', 'd', 'i', 'f', 'i', 'e', 'd'});

        ByteArray signature;
        REQUIRE(ecdsaSign(privateKey, originalHash, signature));

        // Should fail with modified hash
        CHECK_FALSE(ecdsaVerify(publicKey, modifiedHash, signature));

        // But should succeed with original hash
        CHECK(ecdsaVerify(publicKey, originalHash, signature));
    }

    TEST_CASE("ecdsaVerify - Modified signature fails verification")
    {
        ByteArray publicKey, privateKey;
        REQUIRE(generateEcdsaKeyPair(publicKey, privateKey));

        ByteArray hash = sha256Hash(ByteArray {'t', 'e', 's', 't'});
        ByteArray signature;
        REQUIRE(ecdsaSign(privateKey, hash, signature));

        // Modify the signature
        ByteArray modifiedSig = signature;
        modifiedSig[0] ^= 0x01; // Flip one bit

        // Should fail with modified signature
        CHECK_FALSE(ecdsaVerify(publicKey, hash, modifiedSig));

        // But should succeed with original signature
        CHECK(ecdsaVerify(publicKey, hash, signature));
    }
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_SUITE("Integration Tests")
{
    TEST_CASE("Full cryptographic workflow")
    {
        // Generate key pair
        ByteArray publicKey, privateKey;
        REQUIRE(generateEcdsaKeyPair(publicKey, privateKey));

        // Create some test data
        std::string message = "This is a test message for cryptographic signing.";
        ByteArray messageBytes(message.begin(), message.end());

        // Hash the message
        ByteArray hash = sha256Hash(messageBytes);
        REQUIRE(hash.size() == kSHA256Size);

        // Sign the hash
        ByteArray signature;
        REQUIRE(ecdsaSign(privateKey, hash, signature));
        REQUIRE(signature.size() == kP256SigSize);

        // Verify the signature
        CHECK(ecdsaVerify(publicKey, hash, signature));

        // Verify that tampering with any component breaks verification
        ByteArray tamperedHash = hash;
        tamperedHash[0] ^= 0x01;
        CHECK_FALSE(ecdsaVerify(publicKey, tamperedHash, signature));
    }

    TEST_CASE("Multiple message signing with same key pair")
    {
        ByteArray publicKey, privateKey;
        REQUIRE(generateEcdsaKeyPair(publicKey, privateKey));

        std::vector<std::string> messages = {
                "Message 1", "Message 2",
                "A much longer message that contains more data to test with various lengths",
                "", // Empty message
                "🚀 Unicode test message! 🔐"};

        for (const auto &msg : messages) {
            ByteArray msgBytes(msg.begin(), msg.end());
            ByteArray hash = sha256Hash(msgBytes);
            ByteArray signature;

            REQUIRE(ecdsaSign(privateKey, hash, signature));
            CHECK(ecdsaVerify(publicKey, hash, signature));
        }
    }

    TEST_CASE("Cross-verification between different key pairs fails")
    {
        // Generate multiple key pairs
        std::vector<std::pair<ByteArray, ByteArray>> keyPairs;
        for (int i = 0; i < 3; ++i) {
            ByteArray pubKey, privKey;
            REQUIRE(generateEcdsaKeyPair(pubKey, privKey));
            keyPairs.emplace_back(std::move(pubKey), std::move(privKey));
        }

        ByteArray testMessage("deadbeefcafebabe");
        ByteArray hash = sha256Hash(testMessage);

        // Sign with each private key and verify that only the corresponding
        // public key can verify the signature
        for (size_t i = 0; i < keyPairs.size(); ++i) {
            ByteArray signature;
            REQUIRE(ecdsaSign(keyPairs[i].second, hash, signature));

            for (size_t j = 0; j < keyPairs.size(); ++j) {
                bool shouldVerify = (i == j);
                bool doesVerify = ecdsaVerify(keyPairs[j].first, hash, signature);
                CHECK(doesVerify == shouldVerify);
            }
        }
    }

    TEST_CASE("Stress test - Many operations")
    {
        const int iterations = 10;

        for (int i = 0; i < iterations; ++i) {
            ByteArray publicKey, privateKey;
            REQUIRE(generateEcdsaKeyPair(publicKey, privateKey));

            // Create unique message for this iteration
            std::string msg = "Iteration " + std::to_string(i) + " test message";
            ByteArray msgBytes(msg.begin(), msg.end());
            ByteArray hash = sha256Hash(msgBytes);

            ByteArray signature;
            REQUIRE(ecdsaSign(privateKey, hash, signature));
            CHECK(ecdsaVerify(publicKey, hash, signature));
        }
    }
}
