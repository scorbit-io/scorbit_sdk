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

#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace scorbit {
namespace detail {

/// Heartbeat v2 reply flags, packed into the single response byte from the heartbeat server.
constexpr std::uint8_t HEARTBEAT_FLAG_ACK {0x01};
constexpr std::uint8_t HEARTBEAT_FLAG_WAKE {0x02};

/** @return true when the heartbeat reply acknowledges receipt of the datagram. */
constexpr bool isHeartbeatAcked(std::uint8_t flags)
{
    return (flags & HEARTBEAT_FLAG_ACK) != 0;
}

/** @return true when the server asks the device to come online for pending work. */
constexpr bool isHeartbeatWakeRequested(std::uint8_t flags)
{
    return (flags & HEARTBEAT_FLAG_WAKE) != 0;
}

/**
 * @brief Heartbeat v2 UDP keepalive.
 *
 * Sends the device uuid as a datagram to the heartbeat server on a fixed interval and reads back
 * the single reply byte. That tells the server the device is alive, and lets the server ask the
 * device to come online when there is work waiting: a reply carrying the wake flag invokes the
 * wake handler.
 *
 * Every member is touched only on the strand given to the constructor, so the class needs no
 * locks. The strand's io_context must outlive the object, and because stop() only requests the
 * teardown, the io_context has to be drained before destruction (the usual asio ownership rule).
 */
class Heartbeat
{
public:
    using asio_strand = boost::asio::strand<boost::asio::io_context::executor_type>;

    /// Invoked on the heartbeat strand when the server asks the device to come online. Keep it
    /// short and hand any real work to another executor.
    using WakeHandler = std::function<void()>;

    /**
     * @param strand Serialises the socket, the timers and all internal state.
     * @param host Heartbeat server host. Empty selects $HEARTBEAT_HOST, else the built-in default.
     * @param port Heartbeat server UDP port. 0 selects $HEARTBEAT_PORT, else the built-in default.
     * @param onWake Called when a reply carries the wake flag. May be nullptr.
     */
    Heartbeat(asio_strand strand, const std::string &host, std::uint16_t port, WakeHandler onWake);

    ~Heartbeat();

    Heartbeat(const Heartbeat &) = delete;
    Heartbeat &operator=(const Heartbeat &) = delete;

    /**
     * @brief Send a heartbeat now, then keep sending one on every interval.
     *
     * Safe to call repeatedly: it replaces the pending tick instead of adding a second one, and a
     * send is skipped while the previous reply is still outstanding. That is what lets the caller
     * simply re-arm the keepalive whenever authentication succeeds, and restart it after stop().
     *
     * @param deviceUuid Identifies the device to the server; it is the whole payload. Taken per
     *                   call because key provisioning may only learn the uuid after construction.
     */
    void start(std::string deviceUuid);

    /**
     * @brief Cancel the keepalive and close the socket.
     *
     * Returns before the teardown has run, but the teardown is queued on the strand, so draining
     * the io_context completes it. Cancelling matters at shutdown: an outstanding receive or tick
     * would otherwise keep the io_context busy waiting for a datagram that may never come.
     */
    void stop();

private:
    void scheduleNextTick();
    void send();
    void awaitReply();
    void onReply(const boost::system::error_code &ec, std::size_t bytes);

    /// Resolve the host once and cache the endpoint. Returns false while unresolved; a failure
    /// defers the next attempt with exponential backoff. Runs on the strand.
    /// ponytail: the endpoint is cached for the lifetime of the object, so a server that moves to
    /// another address is only picked up on restart. Re-resolve periodically if that becomes a
    /// problem.
    bool resolveEndpoint();

    asio_strand m_strand;
    const std::string m_host;
    const std::string m_port; // text, because asio's resolver takes the service as a string
    const WakeHandler m_onWake;

    boost::asio::ip::udp::socket m_socket;
    boost::asio::steady_timer m_tickTimer;
    boost::asio::steady_timer m_replyTimer;

    boost::asio::ip::udp::endpoint m_endpoint;
    boost::asio::ip::udp::endpoint m_senderEndpoint;
    std::array<std::uint8_t, 1> m_reply {};

    std::string m_deviceUuid;
    bool m_isEndpointResolved {false};
    bool m_isAwaitingReply {false};
    std::chrono::seconds m_resolveBackoff;
    std::chrono::steady_clock::time_point m_resolveNextAttempt;

    /// Written by start() and stop() from outside the strand, hence atomic.
    std::atomic_bool m_stopped {true};
};

} // namespace detail
} // namespace scorbit
