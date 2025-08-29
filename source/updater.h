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

#include "net_base.h"
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <atomic>

namespace scorbit {
namespace detail {

class Updater
{
public:
    Updater(NetBase &net, bool useEncryptedKey);

    void checkNewVersionAndUpdate(const nlohmann::json &json);

private:
    void parseUrl(const nlohmann::json &sdk);
    bool update(const std::string &archivePath);
    bool replaceLibrary(const std::string &libPath, const std::string &newLibPath);

private:
    NetBase &m_net;
    std::atomic_bool m_updateInProgress {false};

    const bool m_useEncryptedKey;

    std::string m_url;
    std::string m_version;
    std::string m_feedback;
};


} // namespace detail
} // namespace scorbit
