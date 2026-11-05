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

#include <optional>
#include <string>

namespace scorbit {
namespace detail {
namespace achievements {

/** A definitions response as cached: its body, and the `ETag` to revalidate it with. */
struct CachedDefinitions {
    std::string body;

    /** Empty when the response carried none. */
    std::string etag;
};

/**
 * The machine's on-disk achievements cache, under `<data dir>/achievements/`: the last
 * definitions response and its `ETag`, so a machine that boots offline still evaluates, and a
 * reboot revalidates rather than downloads (§10.3).
 *
 * Every operation is best effort: a failure is logged and reported through the return value.
 */
class AchievementStorage
{
public:
    explicit AchievementStorage(std::string dataDir);

    std::optional<CachedDefinitions> loadDefinitions() const;
    bool saveDefinitions(const CachedDefinitions &definitions) const;

    /** Forgets the cached definitions, e.g. a body that no longer parses, with its `ETag`. */
    void discardDefinitions() const;

private:
    std::string m_root;
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
