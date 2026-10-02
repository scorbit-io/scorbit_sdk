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

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

/**
 * The machine's on-disk achievements cache, under `<data dir>/achievements/`:
 *
 * - the last definitions response, so a machine that boots offline still evaluates;
 * - each achievement's DMD frame (`frames/<key><ext>`) and the `frame_version` it was downloaded
 *   at (`frames.json`), so a frame is local before it is needed and is downloaded again only when
 *   its version changes (§3.9).
 *
 * Every operation is best effort: a failure is logged and reported through the return value.
 */
class AchievementStorage
{
public:
    /** Also removes what an older SDK left behind: the definitions ETag and the frame bundle. */
    explicit AchievementStorage(std::string dataDir);

    std::optional<std::string> loadDefinitions() const;
    bool saveDefinitions(const std::string &body) const;

    /** The `frame_version` of every installed frame, keyed by achievement key. */
    std::map<std::string, int64_t> frameVersions() const;

    /** Where a frame for @p key can be downloaded before @ref installFrame. */
    std::string frameDownloadPath(const std::string &key) const;

    /**
     * Moves the file at @p downloadedPath into place as the frame of @p key at @p version, named
     * `<key><extension>`. The previous frame stays in place until then.
     */
    bool installFrame(const std::string &key, const std::string &downloadedPath,
                      const std::string &extension, int64_t version) const;

    /** Removes the frame of @p key, if one is installed. */
    bool removeFrame(const std::string &key) const;

    /** The installed frame of achievement @p key. */
    std::optional<std::vector<uint8_t>> frame(const std::string &key) const;

private:
    bool saveFrameVersions(const std::map<std::string, int64_t> &versions) const;
    void removeFrameFiles(const std::string &key) const;

    std::string m_root;
};

/**
 * The file extension (with its dot) of the frame at @p url, from its path; `.png`, what the API
 * serves, when the path has no usable one.
 */
std::string frameExtension(std::string_view url);

} // namespace achievements
} // namespace detail
} // namespace scorbit
