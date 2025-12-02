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

#include "event_manager.h"
#include "net_base.h"
#include <nlohmann/json_fwd.hpp>
#include <boost/filesystem.hpp>
#include <string>
#include <atomic>
#include <regex>
#include <string_view>

namespace scorbit {
namespace detail {

class Updater
{
    struct UrlInfo {
        std::string url;
        std::string version;
        std::string contentType;
        int size {-1};
    };

    struct BinaryInfo {
        boost::filesystem::path path;
        std::regex re;
    };

public:
    Updater(NetBase &net, bool useEncryptedKey, const std::string &scorbitdVersion,
            const std::string &scorbitdPlatformId);

    void checkNewVersionAndUpdate(const nlohmann::json &json,
                                  std::shared_ptr<EventManager> eventManager);

private:
    UrlInfo parseUrls(const nlohmann::json &sdk) const;
    bool update(const std::string &archivePath, const BinaryInfo &binaryInfo) const;
    bool replaceBinary(const std::string &libPath, const std::string &newLibPath) const;

    bool canUpdateSdk(const UrlInfo &urlInfo, const BinaryInfo &binaryInfo) const;
    bool canUpdateScorbitd(const UrlInfo &urlInfo, const BinaryInfo &binaryInfo) const;

    bool isPlatformIdValid(std::string_view platformId) const;
    bool isBinaryObjectValid(const BinaryInfo &binaryInfo) const;
    bool isEncryptedKeyValid() const;
    bool isSdkVersionCompatible(const std::string &newVersion) const;
    bool isScorbitdVersionCompatible(const std::string &newVersion) const;

    bool tryToRemountAndUpdate(const UrlInfo &urlInfo, const BinaryInfo &binaryInfo);
    bool downloadAndupdateTgz(const UrlInfo &urlInfo, const BinaryInfo &binaryInfo) const;

    void feedback(std::string_view out) const;

private:
    NetBase &m_net;
    std::atomic_bool m_updateInProgress {false};

    const bool m_useEncryptedKey;

    const std::string m_scorbitdVersion;
    const std::string m_scorbitdPlatformId;

    mutable std::string m_feedback;
};


} // namespace detail
} // namespace scorbit
