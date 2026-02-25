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

#include "provisioning_client.h"
#include "identifiers.h"
#include "net_util.h"
#include "utils/signer.h"
#include <logger/logger.h>
#include <utils/bytearray.h>
#include <nlohmann/json.hpp>
#include <cmrc/cmrc.hpp>
#include <openssl/sha.h>
#include <fmt/format.h>
#include <chrono>
#include <iomanip>
#include <sstream>

CMRC_DECLARE(scorbit);

using json = nlohmann::json;

namespace scorbit {
namespace detail {

namespace {

constexpr auto PROVISION_TIMEOUT = std::chrono::seconds(14);

constexpr auto HDR_PROVIDER_ID = "X-Provider-ID";
constexpr auto HDR_PROVIDER_TIMESTAMP = "X-Provider-Timestamp";
constexpr auto HDR_PROVIDER_SIGNATURE = "X-Provider-Signature";

std::string sha256HexDigest(const std::string &data)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(data.data()), data.size(), hash);

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
}

std::string currentTimestamp()
{
    return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count());
}

} // namespace

ProvisioningClient::ProvisioningClient(std::string hostname)
    : m_hostname(std::move(hostname))
{
    if (m_hostname == PRODUCTION_LABEL || m_hostname.empty()) {
        m_hostname = PRODUCTION_HOSTNAME;
    } else if (m_hostname == STAGING_LABEL) {
        m_hostname = STAGING_HOSTNAME;
    }

    const auto host = exctractHostAndPort(m_hostname);
    m_hostname = fmt::format("{}://{}:{}", host.protocol, host.hostname, host.port);
}

std::optional<ProvisionResult> ProvisioningClient::initiate(const std::string &providerId,
                                                            const std::vector<uint8_t> &providerKey)
{
    auto headers = buildProviderAuthHeaders(providerId, providerKey);
    headers[HDR_KEY_CACHE_CONTROL] = HDR_VAL_NO_CACHE;

    const auto fullUrl = fmt::format("{}/{}/{}", m_hostname, URL_API, URL_V2_PROVISION);
    INF("Provisioning: initiating GET {}", fullUrl);

    auto r = cpr::Get(cpr::Url {fullUrl}, headers, cpr::Timeout {PROVISION_TIMEOUT}, sslOptions());

    if (r.status_code != 200) {
        ERR("Provisioning initiate failed: HTTP {} - {}", r.status_code, r.text);
        return std::nullopt;
    }

    try {
        auto j = json::parse(r.text);
        ProvisionResult result;
        result.uuid = j.at("uuid").get<std::string>();
        result.serialNumber = j.at("serial_number").get<uint64_t>();
        INF("Provisioning initiated: uuid={}, serial={}", result.uuid, result.serialNumber);
        return result;
    } catch (const std::exception &e) {
        ERR("Provisioning initiate: failed to parse response: {}", e.what());
        return std::nullopt;
    }
}

bool ProvisioningClient::confirm(const ProvisionResult &result, const std::string &publicKeyHex,
                                 const std::string &deviceSignatureHex,
                                 const std::string &timestamp, const std::string &providerId,
                                 const std::vector<uint8_t> &providerKey)
{
    json body;
    body["uuid"] = result.uuid;
    body["public_key"] = publicKeyHex;
    body["timestamp"] = std::stoull(timestamp);
    body["signature"] = deviceSignatureHex;
    body["key_source"] = "soft_key";

    const auto bodyStr = body.dump();

    auto headers = buildProviderAuthHeaders(providerId, providerKey, bodyStr);
    headers[HDR_KEY_CONTENT_TYPE] = HDR_VAL_CONTENT_JSON;
    headers[HDR_KEY_CACHE_CONTROL] = HDR_VAL_NO_CACHE;

    const auto fullUrl = fmt::format("{}/{}/{}", m_hostname, URL_API, URL_V2_PROVISION);
    INF("Provisioning: confirming POST {}", fullUrl);

    auto r = cpr::Post(cpr::Url {fullUrl}, cpr::Body {bodyStr}, headers,
                       cpr::Timeout {PROVISION_TIMEOUT}, sslOptions());

    if (r.status_code != 200 && r.status_code != 201) {
        ERR("Provisioning confirm failed: HTTP {} - {}", r.status_code, r.text);
        return false;
    }

    INF("Provisioning confirmed successfully");
    return true;
}

cpr::Header ProvisioningClient::buildProviderAuthHeaders(const std::string &providerId,
                                                         const std::vector<uint8_t> &providerKey,
                                                         const std::string &body)
{
    const auto timestamp = currentTimestamp();
    const auto bodyHash = sha256HexDigest(body);

    // message = provider_id + timestamp + body_hash
    const auto message = providerId + timestamp + bodyHash;

    // SHA-256 hash of the message
    std::array<uint8_t, SHA256_DIGEST_LENGTH> digest {};
    SHA256(reinterpret_cast<const unsigned char *>(message.data()), message.size(), digest.data());

    // Sign the digest with the provider's private key (DER format)
    std::vector<uint8_t> signature;
    auto signResult = Signer::sign(signature, digest, providerKey);
    if (signResult != SignErrorCode::Ok) {
        ERR("Failed to sign provider auth header: error {}", static_cast<int>(signResult));
        return {};
    }

    // Convert signature to hex
    utils::ByteArray sigBytes(signature.data(), signature.size());

    return {
            {HDR_PROVIDER_ID, providerId},
            {HDR_PROVIDER_TIMESTAMP, timestamp},
            {HDR_PROVIDER_SIGNATURE, sigBytes.hex()},
    };
}

cpr::SslOptions ProvisioningClient::sslOptions() const
{
    auto fs = cmrc::scorbit::get_filesystem();
    auto certFile = fs.open("cacert.pem");
    cpr::SslOptions ssl;
    ssl.SetOption(cpr::ssl::CaBuffer {std::string(certFile.begin(), certFile.end())});
    ssl.SetOption(cpr::ssl::VerifyHost {true});
    return ssl;
}

} // namespace detail
} // namespace scorbit
