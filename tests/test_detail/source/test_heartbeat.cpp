/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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
#include "worker.h"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <cstdlib>
#include <string>
#include <thread>

#ifdef _WIN32
#include <mstcpip.h> // SIO_UDP_CONNRESET
#endif

// clazy:excludeall=non-pod-global-static

using namespace scorbit::detail;
using namespace std::chrono_literals;
using udp = boost::asio::ip::udp;

namespace {

constexpr auto DEVICE_UUID = "1e1c4d54-2eef-4c86-8e4d-1e6c4a6a0f21";

/// Stand-in heartbeat server: waits for one datagram and replies with @p flags.
/// @param client Set to the heartbeat's address when not null.
/// @param stray When not null, sends the heartbeat a wake from another address ahead of the reply.
/// @return the payload received, or an empty string when nothing arrived in time.
std::string exchange(boost::asio::io_context &ioc, udp::socket &server, std::uint8_t flags,
                     udp::endpoint *client = nullptr, udp::socket *stray = nullptr)
{
    std::array<char, 64> buffer {};
    udp::endpoint sender;
    std::string payload;

    server.async_receive_from(boost::asio::buffer(buffer), sender,
                              [&](const boost::system::error_code &ec, std::size_t bytes) {
                                  if (ec) {
                                      return;
                                  }
                                  payload.assign(buffer.data(), bytes);
                                  if (client != nullptr) {
                                      *client = sender;
                                  }

                                  boost::system::error_code ignored;
                                  if (stray != nullptr) {
                                      const std::uint8_t wake =
                                              HEARTBEAT_FLAG_ACK | HEARTBEAT_FLAG_WAKE;
                                      stray->send_to(boost::asio::buffer(&wake, 1), sender, 0,
                                                     ignored);
                                      std::this_thread::sleep_for(50ms);
                                  }
                                  server.send_to(boost::asio::buffer(&flags, 1), sender, 0,
                                                 ignored);
                              });

    ioc.restart();
    ioc.run_for(2s);

    // Drop the receive if it is still pending, so a later exchange() starts clean
    server.cancel();
    ioc.restart();
    ioc.run();

    return payload;
}

/// Stand-in heartbeat server that takes one datagram and does not answer it.
/// @return the payload received, or an empty string when nothing arrived in time.
std::string swallow(boost::asio::io_context &ioc, udp::socket &server, udp::endpoint &client)
{
    std::array<char, 64> buffer {};
    std::string payload;

    server.async_receive_from(boost::asio::buffer(buffer), client,
                              [&](const boost::system::error_code &ec, std::size_t bytes) {
                                  if (!ec) {
                                      payload.assign(buffer.data(), bytes);
                                  }
                              });

    ioc.restart();
    ioc.run_for(2s);
    server.cancel();
    ioc.restart();
    ioc.run();

    return payload;
}

/// Sets or clears $HEARTBEAT_HOST for one scope, then restores whatever was there.
class HeartbeatHostEnv
{
public:
    explicit HeartbeatHostEnv(const char *value)
    {
        if (const auto *old = std::getenv(NAME); old != nullptr) {
            m_old = old;
            m_hadOld = true;
        }
        set(value);
    }
    ~HeartbeatHostEnv() { set(m_hadOld ? m_old.c_str() : nullptr); }

    HeartbeatHostEnv(const HeartbeatHostEnv &) = delete;
    HeartbeatHostEnv &operator=(const HeartbeatHostEnv &) = delete;

private:
    static void set(const char *value)
    {
#ifdef _WIN32
        _putenv_s(NAME, value != nullptr ? value : "");
#else
        if (value != nullptr) {
            setenv(NAME, value, 1);
        } else {
            unsetenv(NAME);
        }
#endif
    }

    static constexpr auto NAME = "HEARTBEAT_HOST";
    std::string m_old;
    bool m_hadOld {false};
};

} // namespace

TEST_CASE("Heartbeat", "[wake flag]")
{
    boost::asio::io_context serverIoc;
    udp::socket server(serverIoc, udp::endpoint {boost::asio::ip::make_address("127.0.0.1"),
                                                 0 /* any port */});

    std::atomic_int wakeCount {0};

    Worker worker;
    worker.start();

    Heartbeat heartbeat(worker.heartbeatStrand(), "127.0.0.1", server.local_endpoint().port(),
                        [&wakeCount] { ++wakeCount; });

    // The datagram is nothing but the device uuid, and a plain ack must leave the device asleep
    heartbeat.start(DEVICE_UUID);
    CHECK(exchange(serverIoc, server, HEARTBEAT_FLAG_ACK) == DEVICE_UUID);
    std::this_thread::sleep_for(100ms);
    CHECK(wakeCount == 0);

    // The same reply with the wake bit set asks the device to come online
    heartbeat.start(DEVICE_UUID);
    CHECK(exchange(serverIoc, server, HEARTBEAT_FLAG_ACK | HEARTBEAT_FLAG_WAKE) == DEVICE_UUID);
    std::this_thread::sleep_for(100ms);
    CHECK(wakeCount == 1);

    // Unless stop() cancels the pending tick and closes the socket, this blocks until the next
    // heartbeat is due
    heartbeat.stop();
    worker.stop();
    CHECK(!worker.isRunning());
}

TEST_CASE("Heartbeat", "[stale reply]")
{
    boost::asio::io_context serverIoc;
    udp::socket server(serverIoc, udp::endpoint {boost::asio::ip::make_address("127.0.0.1"),
                                                 0 /* any port */});

    std::atomic_int wakeCount {0};

    Worker worker;
    worker.start();

    Heartbeat heartbeat(worker.heartbeatStrand(), "127.0.0.1", server.local_endpoint().port(),
                        [&wakeCount] { ++wakeCount; });

#ifdef _WIN32
    // The late reply below goes to a closed port on purpose. Windows reports the ICMP "port
    // unreachable" that comes back as WSAECONNRESET on the server's next receive, which would end
    // the following exchange() before the heartbeat's datagram arrives.
    BOOL reportConnReset = FALSE;
    DWORD ignoredBytes = 0;
    REQUIRE(WSAIoctl(server.native_handle(), SIO_UDP_CONNRESET, &reportConnReset,
                     sizeof(reportConnReset), nullptr, 0, &ignoredBytes, nullptr, nullptr) == 0);
#endif

    // The server takes the datagram but answers only after the heartbeat's 5 s wait has given up
    udp::endpoint client;
    heartbeat.start(DEVICE_UUID);
    REQUIRE(swallow(serverIoc, server, client) == DEVICE_UUID);
    std::this_thread::sleep_for(6s); // past the 5 s reply timeout; swallow() returns on receipt

    // SB-5170/SB-5172: the late wake reply must not be read as the answer to the next heartbeat.
    // The failed wait closed the socket, so the kernel drops it instead of queueing it.
    const std::uint8_t late = HEARTBEAT_FLAG_ACK | HEARTBEAT_FLAG_WAKE;
    server.send_to(boost::asio::buffer(&late, 1), client);
    std::this_thread::sleep_for(100ms);
    CHECK(wakeCount == 0);

    heartbeat.start(DEVICE_UUID);
    CHECK(exchange(serverIoc, server, HEARTBEAT_FLAG_ACK) == DEVICE_UUID);
    std::this_thread::sleep_for(100ms);
    CHECK(wakeCount == 0);

    // And a wake is acted on by the heartbeat whose reply carries it
    heartbeat.start(DEVICE_UUID);
    CHECK(exchange(serverIoc, server, late) == DEVICE_UUID);
    std::this_thread::sleep_for(100ms);
    CHECK(wakeCount == 1);

    heartbeat.stop();
    worker.stop();
}

TEST_CASE("Heartbeat", "[stray sender]")
{
    boost::asio::io_context serverIoc;
    const auto loopback = boost::asio::ip::make_address("127.0.0.1");
    udp::socket server(serverIoc, udp::endpoint {loopback, 0 /* any port */});
    udp::socket stray(serverIoc, udp::endpoint {loopback, 0 /* any port */});

    std::atomic_int wakeCount {0};

    Worker worker;
    worker.start();

    Heartbeat heartbeat(worker.heartbeatStrand(), "127.0.0.1", server.local_endpoint().port(),
                        [&wakeCount] { ++wakeCount; });

    // A wake from anyone but the server is ignored, and the wait carries on to the server's reply
    heartbeat.start(DEVICE_UUID);
    CHECK(exchange(serverIoc, server, HEARTBEAT_FLAG_ACK, nullptr, &stray) == DEVICE_UUID);
    std::this_thread::sleep_for(100ms);
    CHECK(wakeCount == 0);

    // The stray did not consume the wait: the next heartbeat is sent and its wake is acted on
    heartbeat.start(DEVICE_UUID);
    CHECK(exchange(serverIoc, server, HEARTBEAT_FLAG_ACK | HEARTBEAT_FLAG_WAKE) == DEVICE_UUID);
    std::this_thread::sleep_for(100ms);
    CHECK(wakeCount == 1);

    heartbeat.stop();
    worker.stop();
}

TEST_CASE("Heartbeat", "[ack flag]")
{
    CHECK_FALSE(isHeartbeatAcked(0x00));
    CHECK(isHeartbeatAcked(HEARTBEAT_FLAG_ACK));

    // Ack is reported independently of the wake bit
    CHECK(isHeartbeatAcked(HEARTBEAT_FLAG_ACK | HEARTBEAT_FLAG_WAKE));
    CHECK_FALSE(isHeartbeatAcked(HEARTBEAT_FLAG_WAKE));
}

TEST_CASE("Heartbeat", "[wake flag parsing]")
{
    CHECK_FALSE(isHeartbeatWakeRequested(0x00));

    // Plain ack must not wake the device, otherwise every heartbeat would connect Centrifugo
    CHECK_FALSE(isHeartbeatWakeRequested(HEARTBEAT_FLAG_ACK));

    CHECK(isHeartbeatWakeRequested(HEARTBEAT_FLAG_WAKE));
    CHECK(isHeartbeatWakeRequested(HEARTBEAT_FLAG_ACK | HEARTBEAT_FLAG_WAKE));
}

TEST_CASE("Heartbeat", "[unknown flags are ignored]")
{
    // Unused high bits are reserved; they must not be mistaken for ack or wake
    constexpr std::uint8_t reserved = 0xFC;

    CHECK_FALSE(isHeartbeatAcked(reserved));
    CHECK_FALSE(isHeartbeatWakeRequested(reserved));

    CHECK(isHeartbeatAcked(reserved | HEARTBEAT_FLAG_ACK));
    CHECK(isHeartbeatWakeRequested(reserved | HEARTBEAT_FLAG_WAKE));

    // 0xFF has every bit set, so both are true
    CHECK(isHeartbeatAcked(0xFF));
    CHECK(isHeartbeatWakeRequested(0xFF));
}

TEST_CASE("Heartbeat", "[host selection]")
{
    // SB-5158: a device must heartbeat to the server its own api wakes it through.
    CHECK(defaultHeartbeatHost("staging") == "heartbeat-staging.scorbit.io");
    CHECK(defaultHeartbeatHost("production").empty());
    CHECK(defaultHeartbeatHost("").empty());
    CHECK(defaultHeartbeatHost("http://localhost:8000").empty());

    Worker worker;
    worker.start();
    const auto hostOf = [&worker](const std::string &host, const std::string &defaultHost) {
        return Heartbeat(worker.heartbeatStrand(), host, 0, nullptr, defaultHost).host();
    };

    {
        const HeartbeatHostEnv env(nullptr);
        CHECK(hostOf("", "") == "heartbeat.scorbit.io");
        CHECK(hostOf("", "heartbeat-staging.scorbit.io") == "heartbeat-staging.scorbit.io");
        CHECK(hostOf("hb.example", "heartbeat-staging.scorbit.io") == "hb.example");
    }
    {
        // The development override still beats the per-environment default.
        const HeartbeatHostEnv env("127.0.0.1");
        CHECK(hostOf("", "heartbeat-staging.scorbit.io") == "127.0.0.1");
        CHECK(hostOf("hb.example", "heartbeat-staging.scorbit.io") == "hb.example");
    }

    worker.stop();
}
