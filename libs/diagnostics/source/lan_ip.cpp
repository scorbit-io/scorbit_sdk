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

#include <diagnostics/lan_ip.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/system/error_code.hpp>
#include <array>
#include <cstdint>

namespace scorbit {
namespace detail {
namespace {

using boost::asio::ip::address;
using boost::asio::ip::make_address;
using boost::asio::ip::udp;

bool isIpv4LinkLocal(const boost::asio::ip::address_v4 &address)
{
    constexpr std::uint32_t linkLocalPrefix = 0xA9FE0000; // 169.254.0.0
    constexpr std::uint32_t linkLocalMask = 0xFFFF0000;
    return (address.to_uint() & linkLocalMask) == linkLocalPrefix;
}

std::string getLanIpForDefaultRoute(const address &routeTarget)
{
    boost::system::error_code ec;
    boost::asio::io_context ioContext;
    udp::socket socket(ioContext);

    socket.open(routeTarget.is_v4() ? udp::v4() : udp::v6(), ec);
    if (ec) {
        return {};
    }

    socket.connect(udp::endpoint(routeTarget, 53), ec);
    if (ec) {
        return {};
    }

    const auto localEndpoint = socket.local_endpoint(ec);
    if (ec || !isUsableLanAddress(localEndpoint.address())) {
        return {};
    }

    return localEndpoint.address().to_string();
}

} // namespace

bool isUsableLanAddress(const address &address)
{
    if (address.is_unspecified() || address.is_loopback() || address.is_multicast()) {
        return false;
    }

    if (address.is_v4()) {
        return !isIpv4LinkLocal(address.to_v4());
    }

    const auto v6Address = address.to_v6();
    return !v6Address.is_link_local();
}

std::string getPrimaryLanIp()
{
    const std::array targets {
            make_address("8.8.8.8"),
            make_address("1.1.1.1"),
            make_address("2001:4860:4860::8888"),
            make_address("2606:4700:4700::1111"),
    };

    for (const auto &target : targets) {
        if (const auto lanIp = getLanIpForDefaultRoute(target); !lanIp.empty()) {
            return lanIp;
        }
    }

    return {};
}

} // namespace detail
} // namespace scorbit
