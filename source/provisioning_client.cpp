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
#include <tpm/crypto_helpers.h>
#include <logger/logger.h>
#include <utils/bytearray.h>
#include <nlohmann/json.hpp>
#include <fmt/format.h>

using json = nlohmann::json;

namespace scorbit {
namespace detail {

namespace {

constexpr auto PROVISION_TIMEOUT = std::chrono::seconds(14);

constexpr auto HDR_PROVIDER_ID = "X-Provider-ID";
constexpr auto HDR_PROVIDER_TIMESTAMP = "X-Provider-Timestamp";
constexpr auto HDR_PROVIDER_SIGNATURE = "X-Provider-Signature";

} // namespace

ProvisioningClient::ProvisioningClient(std::string formattedHostname, cpr::SslOptions sslOptions)
    : m_hostname(std::move(formattedHostname))
    , m_sslOptions(std::move(sslOptions))
{
}

std::optional<ProvisionResult> ProvisioningClient::initiate(const std::string &providerId,
                                                            const std::vector<uint8_t> &providerKey,
                                                            const std::string &serverTimestamp)
{
    auto headers = buildProviderAuthHeaders(providerId, providerKey, serverTimestamp);
    headers[HDR_KEY_CACHE_CONTROL] = HDR_VAL_NO_CACHE;

    const auto fullUrl = fmt::format("{}/{}/{}", m_hostname, URL_API, URL_V2_PROVISION);
    INF("Provisioning: initiating GET {}", fullUrl);

    auto r = cpr::Get(cpr::Url {fullUrl}, headers, cpr::Timeout {PROVISION_TIMEOUT}, m_sslOptions);

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

std::optional<ProvisionResult>
ProvisioningClient::confirm(const ProvisionResult &initiated, const std::string &publicKeyHex,
                            const std::string &deviceSignatureHex, const std::string &timestamp,
                            const std::string &providerId, const std::vector<uint8_t> &providerKey,
                            const MachineFingerprint &fingerprints)
{
    json body;
    body["uuid"] = initiated.uuid;
    body["public_key"] = publicKeyHex;
    body["timestamp"] = std::stoull(timestamp);
    body["signature"] = deviceSignatureHex;
    body["key_source"] = "soft_key";

    if (fingerprints.hasAny()) {
        json fp;
        if (!fingerprints.macAddressPrimary.empty())
            fp["mac_address_primary"] = fingerprints.macAddressPrimary;
        if (!fingerprints.boardSerial.empty())
            fp["board_serial"] = fingerprints.boardSerial;
        if (!fingerprints.cpuSerial.empty())
            fp["cpu_serial"] = fingerprints.cpuSerial;
        if (!fingerprints.platformType.empty())
            fp["platform_type"] = fingerprints.platformType;
        body["fingerprints"] = fp;
    }

    const auto bodyStr = body.dump();

    auto headers = buildProviderAuthHeaders(providerId, providerKey, timestamp, bodyStr);
    headers[HDR_KEY_CONTENT_TYPE] = HDR_VAL_CONTENT_JSON;
    headers[HDR_KEY_CACHE_CONTROL] = HDR_VAL_NO_CACHE;

    const auto fullUrl = fmt::format("{}/{}/{}", m_hostname, URL_API, URL_V2_PROVISION);
    INF("Provisioning: confirming POST {}", fullUrl);

    auto r = cpr::Post(cpr::Url {fullUrl}, cpr::Body {bodyStr}, headers,
                       cpr::Timeout {PROVISION_TIMEOUT}, m_sslOptions);

    if (r.status_code != 200 && r.status_code != 201) {
        ERR("Provisioning confirm failed: HTTP {} - {}", r.status_code, r.text);
        return std::nullopt;
    }

    try {
        auto j = json::parse(r.text);
        ProvisionResult confirmed;
        confirmed.uuid = j.at("uuid").get<std::string>();
        confirmed.serialNumber = j.at("serial_number").get<uint64_t>();

        const auto status = j.value("status", "");
        INF("Provisioning confirmed: uuid={}, serial={}, status={}", confirmed.uuid,
            confirmed.serialNumber, status);
        return confirmed;
    } catch (const std::exception &e) {
        ERR("Provisioning confirm: failed to parse response: {}", e.what());
        return std::nullopt;
    }
}

cpr::Header ProvisioningClient::buildProviderAuthHeaders(const std::string &providerId,
                                                         const std::vector<uint8_t> &providerKey,
                                                         const std::string &serverTimestamp,
                                                         const std::string &body)
{
    const auto bodyHash = sha256Hash(
            utils::ByteArray(reinterpret_cast<const uint8_t *>(body.data()), body.size()));

    // message = provider_id + timestamp + body_hash (hex)
    const auto message = providerId + serverTimestamp + bodyHash.hex();

    // SHA-256 hash of the message, then sign with provider's private key (raw r||s format)
    const auto digest = sha256Hash(
            utils::ByteArray(reinterpret_cast<const uint8_t *>(message.data()), message.size()));

    utils::ByteArray key(providerKey.data(), providerKey.size());
    utils::ByteArray signature;
    if (!ecdsaSign(key, digest, signature)) {
        ERR("Failed to sign provider auth header");
        return {};
    }

    return {
            {HDR_PROVIDER_ID, providerId},
            {HDR_PROVIDER_TIMESTAMP, serverTimestamp},
            {HDR_PROVIDER_SIGNATURE, signature.hex()},
    };
}

} // namespace detail
} // namespace scorbit
