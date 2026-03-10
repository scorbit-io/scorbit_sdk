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

#include "machine_fingerprint.h"
#include "mac_address.h"
#include <platform_id.h>
#include <logger/logger.h>
#include <tpm/crypto_helpers.h>
#include <boost/predef.h>

#if BOOST_OS_LINUX
#    include <fstream>
#    include <algorithm>
#    include <cctype>
#elif BOOST_OS_WINDOWS
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#    include <comdef.h>
#    include <wbemidl.h>
#    pragma comment(lib, "wbemuuid.lib")
#    pragma comment(lib, "ole32.lib")
#    pragma comment(lib, "oleaut32.lib")
#endif

namespace scorbit {
namespace detail {

namespace {

#if BOOST_OS_LINUX

std::string readSysfsFile(const char *path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }
    std::string value;
    std::getline(file, value);
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [](unsigned char ch) { return !std::isspace(ch); })
                        .base(),
                value.end());
    return value;
}

std::string getBoardSerial()
{
    auto serial = readSysfsFile("/sys/class/dmi/id/board_serial");
    if (serial.empty()) {
        serial = readSysfsFile("/sys/class/dmi/id/product_serial");
    }
    if (serial.empty()) {
        DBG("Fingerprint: board serial not available");
    }
    return serial;
}

std::string getCpuSerial()
{
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (!cpuinfo.is_open()) {
        DBG("Fingerprint: /proc/cpuinfo not accessible");
        return {};
    }

    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.compare(0, 6, "Serial") == 0) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                auto serial = line.substr(pos + 1);
                serial.erase(0, serial.find_first_not_of(" \t"));
                serial.erase(serial.find_last_not_of(" \t\n\r") + 1);
                return serial;
            }
        }
    }

    DBG("Fingerprint: CPU serial not available");
    return {};
}

#elif BOOST_OS_WINDOWS

std::string queryWmi(const wchar_t *wqlQuery, const wchar_t *propertyName)
{
    std::string result;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool weInitializedCom = SUCCEEDED(hr);
    if (!weInitializedCom && hr != RPC_E_CHANGED_MODE) {
        ERR("Fingerprint: CoInitializeEx failed: 0x{:08X}", static_cast<unsigned>(hr));
        return result;
    }

    // CoInitializeSecurity can only be called once per process; ignore
    // RPC_E_TOO_LATE which means another component already configured it.
    hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                              RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        ERR("Fingerprint: CoInitializeSecurity failed: 0x{:08X}", static_cast<unsigned>(hr));
        if (weInitializedCom)
            CoUninitialize();
        return result;
    }

    IWbemLocator *pLoc = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                          reinterpret_cast<LPVOID *>(&pLoc));
    if (FAILED(hr)) {
        ERR("Fingerprint: CoCreateInstance(WbemLocator) failed: 0x{:08X}",
            static_cast<unsigned>(hr));
        if (weInitializedCom)
            CoUninitialize();
        return result;
    }

    IWbemServices *pSvc = nullptr;
    hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, nullptr, 0, nullptr,
                             nullptr, &pSvc);
    if (FAILED(hr)) {
        ERR("Fingerprint: ConnectServer failed: 0x{:08X}", static_cast<unsigned>(hr));
        pLoc->Release();
        if (weInitializedCom)
            CoUninitialize();
        return result;
    }

    CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    IEnumWbemClassObject *pEnumerator = nullptr;
    hr = pSvc->ExecQuery(bstr_t("WQL"), bstr_t(wqlQuery),
                         WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                         &pEnumerator);
    if (SUCCEEDED(hr)) {
        IWbemClassObject *pObj = nullptr;
        ULONG uReturn = 0;
        if (pEnumerator->Next(WBEM_INFINITE, 1, &pObj, &uReturn) == S_OK) {
            VARIANT vtProp;
            hr = pObj->Get(propertyName, 0, &vtProp, nullptr, nullptr);
            if (SUCCEEDED(hr) && vtProp.vt == VT_BSTR && vtProp.bstrVal) {
                _bstr_t bstrVal(vtProp.bstrVal, true);
                result = static_cast<const char *>(bstrVal);
            }
            VariantClear(&vtProp);
            pObj->Release();
        }
        pEnumerator->Release();
    }

    pSvc->Release();
    pLoc->Release();
    if (weInitializedCom)
        CoUninitialize();
    return result;
}

std::string getBoardSerial()
{
    auto serial = queryWmi(L"SELECT SerialNumber FROM Win32_BaseBoard", L"SerialNumber");
    if (serial.empty()) {
        DBG("Fingerprint: board serial not available via WMI");
    }
    return serial;
}

std::string getCpuSerial()
{
    auto serial = queryWmi(L"SELECT ProcessorId FROM Win32_Processor", L"ProcessorId");
    if (serial.empty()) {
        DBG("Fingerprint: CPU serial not available via WMI");
    }
    return serial;
}

#else

std::string getBoardSerial()
{
    DBG("Fingerprint: board serial collection not supported on this platform");
    return {};
}

std::string getCpuSerial()
{
    DBG("Fingerprint: CPU serial collection not supported on this platform");
    return {};
}

#endif

} // namespace

MachineFingerprint collectFingerprints()
{
    MachineFingerprint fp;
    fp.macAddressPrimary = getMacAddress();
    fp.boardSerial = getBoardSerial();
    fp.cpuSerial = getCpuSerial();
    fp.platformType = SCORBIT_SDK_PLATFORM_ID;
    return fp;
}

std::string MachineFingerprint::computeHash() const
{
    auto normalize = [](const std::string &s) {
        std::string result;
        result.reserve(s.size());
        for (char c : s) {
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return result;
    };

    // Must match API's compute_composite_hash: mac_primary|mac_secondary|board|cpu|platform
    const std::string combined = normalize(macAddressPrimary) + "|"
                               + "|" // mac_address_secondary is always empty in the SDK
                               + normalize(boardSerial) + "|" + normalize(cpuSerial) + "|"
                               + normalize(platformType);
    INF("Collected fingerprint: mac={}, board={}, cpu={}, platform={}", macAddressPrimary,
        boardSerial, cpuSerial, platformType);

    const utils::ByteArray message(combined.cbegin(), combined.cend());
    return sha256Hash(message).hex();
}

} // namespace detail
} // namespace scorbit
