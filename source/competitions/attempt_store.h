/*
 * Scorbit SDK
 *
 * (c) 2026 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include "messages.h"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {
namespace competitions {

/** Where a message to the API stands. */
enum class Delivery {
    Pending,   ///< Not answered yet: it is sent, and sent again, until it is
    Saved,     ///< The server saved it
    Duplicate, ///< The server had saved it already
    Refused,   ///< The server refused it for good (a 409 and its code)
};

/** One installed attempt, as kept on disk. */
struct AttemptRecord {
    CompetitionGoal goal;
    /** When the goal was saved here, device UTC: `installed_at`. */
    int64_t installedAtMs {0};
    Delivery installed {Delivery::Pending};

    /** Set once, when the attempt's game started; never replaced. */
    std::optional<AttemptStart> start;
    Delivery started {Delivery::Pending};

    /** The code of the refusal that ended the attempt, if one did. */
    std::string refusal;

    /** The attempt can no longer start: it has, or the server refused it. */
    bool closed() const { return start.has_value() || installed == Delivery::Refused; }
};

/**
 * The attempts installed on this machine, under `<data dir>/competitions/`, apart from the
 * ordinary definitions cache: one file per attempt, `attempts/<attempt>.json`, so several
 * competitions' goals are kept at once; and `sequence`, the last device event ordinal used.
 *
 * Every write is written, fsynced and renamed into place, then its directory fsynced, so a power
 * cut leaves either the old file or the new one (SB-6437 AC 2).
 */
class AttemptStore
{
public:
    explicit AttemptStore(std::string dataDir);

    /** Every attempt kept here. A file that doesn't parse is skipped and logged. */
    std::vector<AttemptRecord> load() const;

    /** Saves @p record durably; false if it could not be. */
    bool save(const AttemptRecord &record) const;

    /**
     * The next device event ordinal, saved durably before it is returned so that none is ever
     * used twice, across restarts included; nullopt if it could not be saved.
     */
    std::optional<int64_t> nextSequence();

    const std::filesystem::path &root() const { return m_root; }

private:
    std::filesystem::path m_root;
    std::optional<int64_t> m_lastSequence;
};

} // namespace competitions
} // namespace detail
} // namespace scorbit
