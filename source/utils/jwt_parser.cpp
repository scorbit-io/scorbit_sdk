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

#include "jwt_parser.h"
#include <logger/logger.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <openssl/bio.h>
#include <openssl/evp.h>

namespace scorbit {
namespace detail {

std::optional<std::chrono::system_clock::time_point> parseJwtExpiration(const std::string& jwtToken)
{
    try {
        // JWT tokens have three parts separated by dots: header.payload.signature
        std::istringstream tokenStream(jwtToken);
        std::string header, payload, signature;
        
        if (!std::getline(tokenStream, header, '.') ||
            !std::getline(tokenStream, payload, '.') ||
            !std::getline(tokenStream, signature, '.')) {
            ERR("JWT token parsing failed: invalid format");
            return std::nullopt;
        }

        // Decode base64url payload
        // Add padding if needed
        while (payload.length() % 4) {
            payload += '=';
        }
        
        // Replace base64url characters with base64 characters
        std::replace(payload.begin(), payload.end(), '-', '+');
        std::replace(payload.begin(), payload.end(), '_', '/');

        // Decode base64 using OpenSSL
        std::string decodedPayload;
        const auto base64Decode = [](const std::string& input) -> std::string {
            BIO *bio, *b64;
            std::string result;
            
            bio = BIO_new_mem_buf(input.data(), input.size());
            b64 = BIO_new(BIO_f_base64());
            bio = BIO_push(b64, bio);
            BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); // No newline handling
            
            char buffer[1024];
            int length = BIO_read(bio, buffer, sizeof(buffer));
            if (length > 0) {
                result.assign(buffer, length);
            }
            
            BIO_free_all(bio);
            return result;
        };

        decodedPayload = base64Decode(payload);
        if (decodedPayload.empty()) {
            ERR("JWT token parsing failed: base64 decode error");
            return std::nullopt;
        }

        // Parse JSON payload
        const auto json = nlohmann::json::parse(decodedPayload);

        if (!json.contains("exp") || !json["exp"].is_number()) {
            ERR("JWT token parsing failed: no expiration time found");
            return std::nullopt;
        }

        // Convert Unix timestamp to system_clock time_point
        const auto expTimestamp = json["exp"].get<int64_t>();
        const auto expTime = std::chrono::system_clock::from_time_t(expTimestamp);
        
        INF("JWT token expires at: {}", expTimestamp);
        return expTime;

    } catch (const std::exception& e) {
        ERR("JWT token parsing failed: {}", e.what());
        return std::nullopt;
    }
}

bool isJwtTokenExpired(const std::string& jwtToken)
{
    const auto expiration = parseJwtExpiration(jwtToken);
    if (!expiration) {
        return true; // Consider invalid tokens as expired
    }
    
    const auto now = std::chrono::system_clock::now();
    return now >= *expiration;
}

std::optional<std::chrono::seconds> getJwtTokenTimeUntilExpiration(const std::string& jwtToken)
{
    const auto expiration = parseJwtExpiration(jwtToken);
    if (!expiration) {
        return std::nullopt;
    }
    
    const auto now = std::chrono::system_clock::now();
    if (now >= *expiration) {
        return std::chrono::seconds(0); // Already expired
    }
    
    return std::chrono::duration_cast<std::chrono::seconds>(*expiration - now);
}

} // namespace detail
} // namespace scorbit
