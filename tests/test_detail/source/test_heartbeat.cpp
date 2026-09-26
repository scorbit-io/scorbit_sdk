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

// clazy:excludeall=non-pod-global-static

using namespace scorbit::detail;
using namespace std::chrono_literals;
using udp = boost::asio::ip::udp;

namespace {

constexpr auto DEVICE_UUID = "1e1c4d54-2eef-4c86-8e4d-1e6c4a6a0f21";

/// Stand-in heartbeat server: waits for one datagram and replies with @p flags.
/// @return the payload received, or an empty string when nothing arrived in time.
std::string exchange(boost::asio::io_context &ioc, udp::socket &server, std::uint8_t flags)
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

                                  boost::system::error_code ignored;
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
