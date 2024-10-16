/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include "net.h"
#include "net_util.h"
#include "logger.h"
#include "scorbit_sdk/net_types.h"
#include "utils/bytearray.h"
#include <fmt/format.h>
#include <openssl/sha.h>
#include <chrono>

using namespace std;

namespace scorbit {
namespace detail {

constexpr auto PRODUCTION_LABEL = "production";
constexpr auto STAGING_LABEL = "staging";
constexpr auto PRODUCTION_HOSTNAME = "https://api.scorbit.io";
constexpr auto STAGING_HOSTNAME = "https://staging.scorbit.io";

string getSignature(const SignerCallback &signer, const std::string &uuid,
                    const std::string &timestamp)
{
    ByteArray message(uuid);
    ByteArray timestampBytes(timestamp.cbegin(), timestamp.cend());
    message.insert(message.end(), timestampBytes.cbegin(), timestampBytes.cend());

    Digest digest;
    SHA256(message.data(), message.size(), digest.data());

    Signature signature;
    size_t signatureLen = 0;
    if (!signer(signature, signatureLen, digest)) {
        ERR("Can't sign message, signer callback returned error");
        return string {};
    }

    return ByteArray(signature.data(), signatureLen).hex();
}

Net::Net(SignerCallback signer, DeviceInfo deviceInfo)
    : m_signer(std::move(signer))
    , m_deviceInfo(std::move(deviceInfo))
{
    setHostname(m_deviceInfo.hostname);

    // Cleanup UUID
    m_deviceInfo.uuid = removeSymbols(m_deviceInfo.uuid, "-{}");
}

std::string Net::hostname() const
{
    return m_hostname;
}

void Net::setHostname(std::string hostname)
{
    if (hostname == PRODUCTION_LABEL || hostname.empty()) {
        hostname = PRODUCTION_HOSTNAME;
    } else if (hostname == STAGING_LABEL) {
        hostname = STAGING_HOSTNAME;
    }

    const auto host = exctractHostAndPort(hostname);
    m_hostname = fmt::format("{}://{}:{}", host.protocol, host.hostname, host.port);
}

void Net::authenticate()
{
}

bool Net::isAuthenticated() const
{
    return m_isAuthenticated;
}

void Net::sendGameData(const detail::GameData &data)
{
    (void)data;
}

} // namespace detail
} // namespace scorbit
