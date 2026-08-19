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

#include "heartbeat.h"
#include <logger/logger.h>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/post.hpp>
#include <algorithm>
#include <cstdlib>

namespace scorbit {
namespace detail {

using namespace std::chrono_literals;
using udp = boost::asio::ip::udp;

namespace {

constexpr auto DEFAULT_HOST = "heartbeat.scorbit.io";
constexpr auto DEFAULT_PORT = "8443";

// Development-only overrides, consulted when the configured values were left unset
constexpr auto ENV_HOST = "HEARTBEAT_HOST";
constexpr auto ENV_PORT = "HEARTBEAT_PORT";

constexpr auto INTERVAL = 45s;
/// How long to wait for the 1-byte reply before giving up on this cycle.
constexpr auto REPLY_TIMEOUT = 5s;
/// DNS for the heartbeat host is resolved lazily; back off while it is unreachable.
constexpr auto RESOLVE_INITIAL_BACKOFF = 2s;
constexpr auto RESOLVE_MAX_BACKOFF = 5min;

/// Endpoint parts in precedence order: configured value, then environment, then built-in default.
/// The environment path exists for development against a local heartbeat server.
std::string configured(const std::string &value, const char *envName, const char *fallback)
{
    if (!value.empty()) {
        return value;
    }

    if (const auto *env = std::getenv(envName); env != nullptr && *env != '\0') {
        return env;
    }

    return fallback;
}

std::string toString(const udp::endpoint &endpoint)
{
    return fmt::format("{}:{}", endpoint.address().to_string(), endpoint.port());
}

} // namespace

Heartbeat::Heartbeat(asio_strand strand, const std::string &host, std::uint16_t port,
                     WakeHandler onWake)
    : m_strand(std::move(strand))
    , m_host(configured(host, ENV_HOST, DEFAULT_HOST))
    , m_port(configured(port != 0 ? std::to_string(port) : std::string {}, ENV_PORT, DEFAULT_PORT))
    , m_onWake(std::move(onWake))
    , m_socket(m_strand)
    , m_resolver(m_strand)
    , m_tickTimer(m_strand)
    , m_replyTimer(m_strand)
    , m_resolveBackoff(RESOLVE_INITIAL_BACKOFF)
{
}

Heartbeat::~Heartbeat()
{
    stop();
}

void Heartbeat::start(std::string deviceUuid)
{
    if (deviceUuid.empty()) {
        WRN("API-HB not started, device uuid is empty");
        return;
    }

    m_stopped = false;

    boost::asio::post(m_strand, [this, uuid = std::move(deviceUuid)]() mutable {
        if (m_stopped) {
            return;
        }

        m_deviceUuid = std::move(uuid);
        scheduleNextTick();
        send();
    });
}

void Heartbeat::stop()
{
    if (m_stopped.exchange(true)) {
        return; // never started, or already stopped
    }

    // Tear down on the strand rather than here: a receive or a tick handler may be running there,
    // and closing the socket underneath it would be a race.
    boost::asio::post(m_strand, [this] {
        m_tickTimer.cancel();
        m_replyTimer.cancel();
        m_resolver.cancel();

        boost::system::error_code ignored;
        m_socket.close(ignored);

        DBG("API-HB stopped");
    });
}

void Heartbeat::scheduleNextTick()
{
    m_tickTimer.expires_after(INTERVAL);
    m_tickTimer.async_wait([this](const boost::system::error_code &ec) {
        if (ec) {
            return; // cancelled by stop(), or superseded by a restart
        }

        scheduleNextTick();
        send();
    });
}

void Heartbeat::send()
{
    if (m_stopped || m_isAwaitingReply) {
        return;
    }

    if (!m_isEndpointResolved) {
        resolveEndpoint(); // sends again once the endpoint lands
        return;
    }

    boost::system::error_code ec;

    if (!m_socket.is_open()) {
        m_socket.open(m_endpoint.protocol(), ec);
        if (ec) {
            ERR("API-HB can't open socket: {}", ec.message());
            return;
        }
    }

    // The server identifies the device solely by the 36 character uuid in the datagram
    m_socket.send_to(boost::asio::buffer(m_deviceUuid), m_endpoint, 0, ec);
    if (ec) {
        ERR("API-HB send failed: {}", ec.message());
        return;
    }

    DBG("API-HB sent to {}", toString(m_endpoint));
    awaitReply();
}

void Heartbeat::awaitReply()
{
    m_isAwaitingReply = true;

    m_socket.async_receive_from(
            boost::asio::buffer(m_reply), m_senderEndpoint,
            [this](const boost::system::error_code &ec, std::size_t bytes) { onReply(ec, bytes); });

    // UDP has no delivery guarantee, so bound the wait. Cancelling the socket completes the receive
    // above with operation_aborted.
    m_replyTimer.expires_after(REPLY_TIMEOUT);
    m_replyTimer.async_wait([this](const boost::system::error_code &ec) {
        if (ec) {
            return; // the reply arrived, or we are stopping
        }

        boost::system::error_code ignored;
        m_socket.cancel(ignored);
    });
}

void Heartbeat::onReply(const boost::system::error_code &ec, std::size_t bytes)
{
    m_isAwaitingReply = false;
    m_replyTimer.cancel();

    if (ec == boost::asio::error::operation_aborted) {
        // Either the reply timed out or we are stopping; the next tick retries
        WRN("API-HB no reply within {}", REPLY_TIMEOUT);
        return;
    }

    if (ec) {
        ERR("API-HB receive failed: {}", ec.message());
        return;
    }

    if (bytes < m_reply.size()) {
        WRN("API-HB truncated reply, {} bytes", bytes);
        return;
    }

    const auto flags = m_reply[0];
    DBG("API-HB reply from {}, flags: {:#04x}", toString(m_senderEndpoint),
        static_cast<unsigned>(flags));

    if (!isHeartbeatAcked(flags)) {
        WRN("API-HB reply without ack flag: {:#04x}", static_cast<unsigned>(flags));
    }

    if (isHeartbeatWakeRequested(flags) && m_onWake) {
        INF("API-HB wake flag received");
        m_onWake();
    }
}

void Heartbeat::resolveEndpoint()
{
    // One lookup at a time: a tick may come around again while a slow one is still outstanding
    if (m_isResolving || std::chrono::steady_clock::now() < m_resolveNextAttempt) {
        return;
    }

    m_isResolving = true;
    m_resolver.async_resolve(
            udp::v4(), m_host, m_port,
            [this](const boost::system::error_code &ec, udp::resolver::results_type endpoints) {
                m_isResolving = false;

                if (m_stopped || ec == boost::asio::error::operation_aborted) {
                    return;
                }

                if (ec || endpoints.empty()) {
                    m_resolveNextAttempt = std::chrono::steady_clock::now() + m_resolveBackoff;
                    WRN("API-HB can't resolve {}:{}, next attempt in {}: {}", m_host, m_port,
                        m_resolveBackoff, ec.message());
                    m_resolveBackoff = std::min<std::chrono::seconds>(m_resolveBackoff * 2,
                                                                      RESOLVE_MAX_BACKOFF);
                    return;
                }

                m_endpoint = *endpoints.begin();
                m_resolveBackoff = RESOLVE_INITIAL_BACKOFF;
                m_isEndpointResolved = true;

                INF("API-HB endpoint resolved: {}", toString(m_endpoint));

                send(); // the tick that triggered the lookup still owes a datagram
            });
}

} // namespace detail
} // namespace scorbit
