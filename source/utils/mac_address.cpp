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

#include "mac_address.h"
#include <boost/predef.h>

#if BOOST_OS_WINDOWS
#include "../logger.h"

#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif

// Windows
#    include <winsock2.h>
#    include <windows.h>
#    include <iphlpapi.h>

#    include <iomanip>
#    include <regex>
#    include <sstream>
#    include <vector>

#    pragma comment(lib, "iphlpapi.lib")
#else
#    include <ifaddrs.h>
#    include <cstring>
#    include <iomanip>
#    include <sstream>
#    include <sys/socket.h>
#    include <net/if.h>
#    include <regex>

#    if BOOST_OS_LINUX
#        include <netpacket/packet.h>
#    elif BOOST_OS_MACOS
#        include <net/if_dl.h> // For sockaddr_dl and LLADDR
#    endif
#endif

namespace scorbit {
namespace detail {

#if BOOST_OS_WINDOWS
bool isVirtualAdapter(const std::wstring &adapterName)
{
    static const std::wregex virtualPatterns(LR"((^isatap|^teredo|^loopback|^vEthernet|^TAP|^tun))",
                                             std::regex_constants::icase);
    return std::regex_search(adapterName, virtualPatterns);
}

std::string getMacAddress()
{
    ULONG bufferSize = 0;
    DWORD result = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &bufferSize);

    if (result != ERROR_BUFFER_OVERFLOW) {
        ERR("Failed to determine buffer size for adapter addresses. Error code: {}", result);
        return {};
    }

    std::vector<char> buffer(bufferSize);
    auto adapterAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());

    result = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapterAddresses, &bufferSize);
    if (result != NO_ERROR) {
        ERR("GetAdaptersAddresses failed. Error code: {}", result);
        return {};
    }

    for (IP_ADAPTER_ADDRESSES *adapter = adapterAddresses; adapter; adapter = adapter->Next) {
        if (isVirtualAdapter(adapter->FriendlyName)) {
            continue;
        }

        if (adapter->PhysicalAddressLength == 6) {
            std::ostringstream mac;
            for (int i = 0; i < 6; ++i) {
                mac << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(adapter->PhysicalAddress[i]);
                if (i != 5) {
                    mac << ":";
                }
            }
            return mac.str(); // First valid MAC found
        }
    }

    return {}; // No valid MAC found
}

#else
// Unix

// Check if the interface name matches common virtual interface patterns
bool isVirtualInterface(const std::string &interfaceName)
{
    static const std::regex virtualPatterns(
            R"((^lo$|^docker|^veth|^virbr|^br-|^tap|^tun|^wl|^awdl|^dummy|^anpi|^bridge|^vnet))",
            std::regex_constants::icase);
    return std::regex_search(interfaceName, virtualPatterns);
}

std::string getMacAddress()
{
    struct ifaddrs *ifaddr = nullptr;
    struct ifaddrs *ifa = nullptr;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return {};
    }

    std::string macAddress;
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr)
            continue;

        // Skip virtual interfaces by name
        if (isVirtualInterface(ifa->ifa_name)) {
            continue;
        }

#    if BOOST_OS_LINUX
        if (ifa->ifa_addr->sa_family != AF_PACKET)
            continue;

        struct sockaddr_ll *s = reinterpret_cast<struct sockaddr_ll *>(ifa->ifa_addr);
        if (s->sll_halen == 6) { // MAC address length
            std::ostringstream mac;
            for (int i = 0; i < 6; ++i) {
                mac << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(s->sll_addr[i]);
                if (i != 5) {
                    mac << ":";
                }
            }
            macAddress = mac.str();
            break;
        }
#    elif BOOST_OS_MACOS
        if (ifa->ifa_addr->sa_family != AF_LINK)
            continue;

        struct sockaddr_dl *sdl = reinterpret_cast<struct sockaddr_dl *>(ifa->ifa_addr);
        unsigned char *mac = (unsigned char *)LLADDR(sdl);
        if (sdl->sdl_alen == 6) { // MAC address length
            std::ostringstream macStream;
            for (int i = 0; i < 6; ++i) {
                macStream << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<int>(mac[i]);
                if (i != 5) {
                    macStream << ":";
                }
            }
            macAddress = macStream.str();
            break;
        }
#    endif
    }

    freeifaddrs(ifaddr);
    return macAddress;
}
#endif

} // namespace detail
} // namespace scorbit
