/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <utils/jwt_parser.h>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <chrono>

using namespace scorbit::detail;
using namespace std::chrono;

namespace {

std::string createTestJwtToken(int64_t expTimestamp)
{
    // Create JWT header
    nlohmann::json header;
    header["alg"] = "HS256";
    header["typ"] = "JWT";
    std::string headerStr = header.dump();

    // Create JWT payload
    nlohmann::json payload;
    payload["sub"] = "test_user";
    payload["iat"] = 1640995200; // 2022-01-01 00:00:00 UTC
    payload["exp"] = expTimestamp;
    payload["iss"] = "test_issuer";
    std::string payloadStr = payload.dump();

    // OpenSSL-based base64url encoding
    auto base64urlEncode = [](const std::string &input) -> std::string {
        BIO *b64 = BIO_new(BIO_f_base64());
        BIO *mem = BIO_new(BIO_s_mem());
        BIO *bio = BIO_push(b64, mem);

        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
        BIO_write(bio, input.c_str(), static_cast<int>(input.length()));
        BIO_flush(bio);

        char *data;
        long len = BIO_get_mem_data(bio, &data);

        std::string result(data, len);
        BIO_free_all(bio);

        // Convert to base64url
        std::replace(result.begin(), result.end(), '+', '-');
        std::replace(result.begin(), result.end(), '/', '_');
        result.erase(std::remove(result.begin(), result.end(), '='), result.end());

        return result;
    };

    // Encode header and payload
    std::string encodedHeader = base64urlEncode(headerStr);
    std::string encodedPayload = base64urlEncode(payloadStr);

    // Create signature using HMAC-SHA256
    std::string message = encodedHeader + "." + encodedPayload;
    std::string secret = "test_secret_key";

    unsigned char hash[SHA256_DIGEST_LENGTH];
    unsigned int hashLen;

    HMAC(EVP_sha256(), secret.c_str(), static_cast<int>(secret.length()),
         reinterpret_cast<const unsigned char *>(message.c_str()),
         static_cast<int>(message.length()), hash, &hashLen);

    std::string signature(reinterpret_cast<char *>(hash), hashLen);
    std::string encodedSignature = base64urlEncode(signature);

    return encodedHeader + "." + encodedPayload + "." + encodedSignature;
}

} // namespace

// ----------------- Simple, hard-coded tests ------------------------------

TEST_CASE("JWT Parser - Basic Functionality", "[jwt_parser_simple]")
{
    // Test with a simple, known JWT token
    // This is a test token with exp: 1640995200 (2022-01-01 00:00:00 UTC)
    std::string testJwt = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                          "eyJzdWIiOiJ0ZXN0X3VzZXIiLCJpYXQiOjE2NDA5OTUyMDAsImV4cCI6MTY0MDk5NTIwMCwi"
                          "aXNzIjoidGVzdF9pc3N1ZXIifQ.test_signature";

    auto expiration = parseJwtExpiration(testJwt);

    // The token should be expired (exp: 1640995200 is in the past)
    REQUIRE(expiration.has_value());

    bool expired = isJwtTokenExpired(testJwt);
    REQUIRE(expired);

    auto timeLeft = getJwtTokenTimeUntilExpiration(testJwt);
    REQUIRE(timeLeft.has_value());
    REQUIRE(timeLeft->count() == 0);
}

TEST_CASE("JWT Parser - Invalid Token Format", "[jwt_parser_simple]")
{
    std::string invalidToken = "invalid.token.format";

    auto expiration = parseJwtExpiration(invalidToken);
    REQUIRE_FALSE(expiration.has_value());

    bool expired = isJwtTokenExpired(invalidToken);
    REQUIRE(expired);

    auto timeLeft = getJwtTokenTimeUntilExpiration(invalidToken);
    REQUIRE_FALSE(timeLeft.has_value());
}

TEST_CASE("JWT Parser - Empty Token", "[jwt_parser_simple]")
{
    std::string emptyToken = "";

    auto expiration = parseJwtExpiration(emptyToken);
    REQUIRE_FALSE(expiration.has_value());

    bool expired = isJwtTokenExpired(emptyToken);
    REQUIRE(expired);

    auto timeLeft = getJwtTokenTimeUntilExpiration(emptyToken);
    REQUIRE_FALSE(timeLeft.has_value());
}

TEST_CASE("JWT Parser - Token Without Expiration", "[jwt_parser_simple]")
{
    // This is a test token without exp field
    std::string testJwt = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                          "eyJzdWIiOiJ0ZXN0X3VzZXIiLCJpYXQiOjE2NDA5OTUyMDAsImlzcyI6InRlc3RfaXNzdWVy"
                          "In0.test_signature";

    auto expiration = parseJwtExpiration(testJwt);
    REQUIRE_FALSE(expiration.has_value());

    bool expired = isJwtTokenExpired(testJwt);
    REQUIRE(expired);

    auto timeLeft = getJwtTokenTimeUntilExpiration(testJwt);
    REQUIRE_FALSE(timeLeft.has_value());
}

// ----------------- Comprehensive tests with dynamic tokens ------------------------------

TEST_CASE("parseJwtExpiration - Valid JWT Token", "[jwt_parser]")
{
    // Test with a token that expires in 1 hour from now
    auto now = system_clock::now();
    auto expTime = now + hours(1);
    auto expTimestamp = duration_cast<seconds>(expTime.time_since_epoch()).count();

    std::string jwtToken = createTestJwtToken(expTimestamp);

    auto result = parseJwtExpiration(jwtToken);

    REQUIRE(result.has_value());

    // Check that the parsed time is within 1 second of expected time
    auto timeDiff = duration_cast<seconds>(result.value() - expTime);
    REQUIRE(std::abs(timeDiff.count()) <= 1);
}

TEST_CASE("parseJwtExpiration - Expired JWT Token", "[jwt_parser]")
{
    // Test with a token that expired 1 hour ago
    auto now = system_clock::now();
    auto expTime = now - hours(1);
    auto expTimestamp = duration_cast<seconds>(expTime.time_since_epoch()).count();

    std::string jwtToken = createTestJwtToken(expTimestamp);

    auto result = parseJwtExpiration(jwtToken);

    REQUIRE(result.has_value());

    // Check that the parsed time is within 1 second of expected time
    auto timeDiff = duration_cast<seconds>(result.value() - expTime);
    REQUIRE(std::abs(timeDiff.count()) <= 1);
}

TEST_CASE("parseJwtExpiration - Invalid JWT Token Format", "[jwt_parser]")
{
    // Test with invalid JWT token formats
    std::vector<std::string> invalidTokens = {
            "",                           // Empty token
            "invalid.token",              // Missing signature
            "invalid",                    // Only one part
            "invalid.token.format.extra", // Too many parts
            "invalid.token.signature",    // Invalid base64
            "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.invalid_payload.test_signature", // Invalid
                                                                                   // payload
    };

    for (const auto &token : invalidTokens) {
        auto result = parseJwtExpiration(token);
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("parseJwtExpiration - JWT Token Without Expiration", "[jwt_parser]")
{
    // Create a JWT token without exp field
    std::string header = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9";

    nlohmann::json payload;
    payload["sub"] = "test_user";
    payload["iat"] = 1640995200;
    // No exp field

    std::string payloadStr = payload.dump();

    // Simple base64url encoding
    auto base64urlEncode = [](const std::string &input) -> std::string {
        const std::string chars =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        int val = 0, valb = -8;
        for (char c : input) {
            val = (val << 8) + static_cast<unsigned char>(c);
            valb += 8;
            while (valb >= 0) {
                result.push_back(chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6)
            result.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
        while (result.size() % 4)
            result.push_back('=');

        std::replace(result.begin(), result.end(), '+', '-');
        std::replace(result.begin(), result.end(), '/', '_');
        std::replace(result.begin(), result.end(), '=', ' ');
        result.erase(std::remove(result.begin(), result.end(), ' '), result.end());

        return result;
    };

    std::string encodedPayload = base64urlEncode(payloadStr);
    std::string signature = "test_signature";
    std::string jwtToken = header + "." + encodedPayload + "." + signature;

    auto result = parseJwtExpiration(jwtToken);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("isJwtTokenExpired - Valid Non-Expired Token", "[jwt_parser]")
{
    // Test with a token that expires in 1 hour
    auto now = system_clock::now();
    auto expTime = now + hours(1);
    auto expTimestamp = duration_cast<seconds>(expTime.time_since_epoch()).count();

    std::string jwtToken = createTestJwtToken(expTimestamp);

    bool expired = isJwtTokenExpired(jwtToken);
    REQUIRE_FALSE(expired);
}

TEST_CASE("isJwtTokenExpired - Expired Token", "[jwt_parser]")
{
    // Test with a token that expired 1 hour ago
    auto now = system_clock::now();
    auto expTime = now - hours(1);
    auto expTimestamp = duration_cast<seconds>(expTime.time_since_epoch()).count();

    std::string jwtToken = createTestJwtToken(expTimestamp);

    bool expired = isJwtTokenExpired(jwtToken);
    REQUIRE(expired);
}

TEST_CASE("isJwtTokenExpired - Invalid Token", "[jwt_parser]")
{
    // Test with invalid token
    std::string invalidToken = "invalid.token.format";

    bool expired = isJwtTokenExpired(invalidToken);
    REQUIRE(expired); // Invalid tokens should be considered expired
}

TEST_CASE("getJwtTokenTimeUntilExpiration - Valid Non-Expired Token", "[jwt_parser]")
{
    // Test with a token that expires in 2 hours
    auto now = system_clock::now();
    auto expTime = now + hours(2);
    auto expTimestamp = duration_cast<seconds>(expTime.time_since_epoch()).count();

    std::string jwtToken = createTestJwtToken(expTimestamp);

    auto timeLeft = getJwtTokenTimeUntilExpiration(jwtToken);

    REQUIRE(timeLeft.has_value());

    // Should be approximately 2 hours (7200 seconds)
    // Allow for some tolerance due to test execution time
    REQUIRE(timeLeft->count() >= 7190); // At least 7190 seconds
    REQUIRE(timeLeft->count() <= 7210); // At most 7210 seconds
}

TEST_CASE("getJwtTokenTimeUntilExpiration - Expired Token", "[jwt_parser]")
{
    // Test with a token that expired 1 hour ago
    auto now = system_clock::now();
    auto expTime = now - hours(1);
    auto expTimestamp = duration_cast<seconds>(expTime.time_since_epoch()).count();

    std::string jwtToken = createTestJwtToken(expTimestamp);

    auto timeLeft = getJwtTokenTimeUntilExpiration(jwtToken);

    REQUIRE(timeLeft.has_value());
    REQUIRE(timeLeft->count() == 0); // Should be 0 for expired tokens
}

TEST_CASE("getJwtTokenTimeUntilExpiration - Invalid Token", "[jwt_parser]")
{
    // Test with invalid token
    std::string invalidToken = "invalid.token.format";

    auto timeLeft = getJwtTokenTimeUntilExpiration(invalidToken);
    REQUIRE_FALSE(timeLeft.has_value());
}

TEST_CASE("JWT Token Edge Cases", "[jwt_parser]")
{
    // Test with token that expires exactly now
    auto now = system_clock::now();
    auto expTimestamp = duration_cast<seconds>(now.time_since_epoch()).count();

    std::string jwtToken = createTestJwtToken(expTimestamp);

    // Should be considered expired
    bool expired = isJwtTokenExpired(jwtToken);
    REQUIRE(expired);

    auto timeLeft = getJwtTokenTimeUntilExpiration(jwtToken);
    REQUIRE(timeLeft.has_value());
    REQUIRE(timeLeft->count() == 0);
}

TEST_CASE("JWT Token with Different Expiration Times", "[jwt_parser]")
{
    auto now = system_clock::now();

    // Test various expiration times
    std::vector<std::pair<seconds, bool>> testCases = {
            {seconds(30), false}, // 30 seconds from now - not expired
            {seconds(-30), true}, // 30 seconds ago - expired
            {seconds(0), true},   // Exactly now - expired
            {hours(24), false},   // 24 hours from now - not expired
            {hours(-24), true},   // 24 hours ago - expired
    };

    for (const auto &[timeOffset, shouldBeExpired] : testCases) {
        auto expTime = now + timeOffset;
        auto expTimestamp = duration_cast<seconds>(expTime.time_since_epoch()).count();

        std::string jwtToken = createTestJwtToken(expTimestamp);

        bool expired = isJwtTokenExpired(jwtToken);
        REQUIRE(expired == shouldBeExpired);

        if (!shouldBeExpired) {
            auto timeLeft = getJwtTokenTimeUntilExpiration(jwtToken);
            REQUIRE(timeLeft.has_value());
            REQUIRE(timeLeft->count() > 0);
        }
    }
}
