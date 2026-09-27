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
#include <optional>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

/**
 * The machine's on-disk achievements cache, under `<data dir>/achievements/`:
 *
 * - the last definitions response and its `ETag`, so a boot can revalidate with `If-None-Match`
 *   instead of refetching (§10.1);
 * - the extracted DMD frame bundle and its version, so a frame is local before it is needed and
 *   is downloaded again only when the version changes (§3.9).
 *
 * Every operation is best effort: a failure is logged and reported through the return value.
 */
class AchievementStorage
{
public:
    explicit AchievementStorage(std::string dataDir);

    struct CachedDefinitions {
        std::string body;
        std::string etag;
    };

    std::optional<CachedDefinitions> loadDefinitions() const;
    bool saveDefinitions(const std::string &body, const std::string &etag) const;

    /** The version of the installed frame bundle, if one is installed. */
    std::optional<int64_t> framesVersion() const;

    /** Where a downloaded bundle archive can be written before @ref installFrames. */
    std::string framesDownloadPath() const;

    /**
     * Extracts the bundle archive at @p archivePath and makes it the installed bundle at
     * @p version. The previous bundle stays in place until the new one is fully extracted.
     */
    bool installFrames(const std::string &archivePath, int64_t version) const;

    /** The frame for achievement @p key, read from the installed bundle (`<key>.<ext>`). */
    std::optional<std::vector<uint8_t>> frame(const std::string &key) const;

private:
    std::string m_root;
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
