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

#include "artwork_store.h"
#include "timeline_row.h"
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

/** An achievement unlocked for a claimed player, as delivered to game code (§12.1). */
struct AchievementUnlock {
    std::string key;
    PlayerNumber player {0};

    /** From the definition; empty when the machine doesn't hold it. */
    std::string name;
    std::string description;

    /** The artwork of the chosen kind; empty when there is none. */
    std::vector<uint8_t> artwork;
};

/** When game code hears of an unlock the machine decided itself (§7.2, §14.3). */
enum class Presentation {
    /** Once Scorbit grants it: one round trip later, and never while offline. */
    Granted,
    /**
     * The moment the machine decides it, never waiting for artwork, and not again on the grant.
     * Not authoritative: a refusal that follows is not signalled.
     */
    Local,
};

/** One grant: an achievement, for a player, in one session. */
struct GrantId {
    uint64_t session {0};
    std::string userId;
    std::string key;

    auto operator<=>(const GrantId &) const = default;
};

/**
 * Decides when game code hears of an unlock (§7.2, §12.1), and hands it over with its artwork.
 *
 * - **Granted** (the default): one event per grant. A grant can be signalled more than once (the
 *   report's result and the server's event), and only the first signal raises it. Separate grants
 *   are not merged: a trophy granted again after it was revoked is a new grant.
 * - **Local**: an achievement the machine decided itself is raised at once, with its artwork only
 *   if that is already held, and its grant raises nothing more. A refusal raises nothing and
 *   clears nothing. Anything the server decides is raised on its grant, as in granted mode.
 */
class UnlockPresenter
{
public:
    /** Receives every unlock for game code. */
    using Sink = std::function<void(AchievementUnlock)>;

    UnlockPresenter(ArtworkStore &artwork, Sink sink,
                    Presentation presentation = Presentation::Granted);

    Presentation presentation() const { return m_presentation; }

    /** The machine decided @p unlock itself: raised at once in local mode, else not yet. */
    void decided(const GrantId &grant, AchievementUnlock unlock);

    UnlockPresenter(const UnlockPresenter &) = delete;
    UnlockPresenter &operator=(const UnlockPresenter &) = delete;

    /**
     * Scorbit granted @p unlock: raised, once its artwork is settled, unless this grant was
     * raised already. An icon not held yet is fetched from @p iconUrl first.
     */
    void granted(const GrantId &grant, AchievementUnlock unlock, const std::string &iconUrl);

    /** The grant was revoked (a trophy changed hands): granting it again is a new grant. */
    void revoked(const GrantId &grant);

    /** Session @p session is over and nothing more will be signalled for it. */
    void forgetSession(uint64_t session);

private:
    void raise(AchievementUnlock unlock, const std::string &iconUrl);

    void hand(AchievementUnlock unlock);

    ArtworkStore &m_artwork;
    Sink m_sink;
    Presentation m_presentation;

    /** The grants raised so far; in local mode, also the decisions raised. */
    std::set<GrantId> m_raised;
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
