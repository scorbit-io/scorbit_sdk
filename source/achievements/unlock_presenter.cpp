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

#include "unlock_presenter.h"
#include <logger/logger.h>
#include <utility>

namespace scorbit {
namespace detail {
namespace achievements {

UnlockPresenter::UnlockPresenter(ArtworkStore &artwork, Sink sink)
    : m_artwork {artwork}
    , m_sink {std::move(sink)}
{
}

void UnlockPresenter::granted(const GrantId &grant, AchievementUnlock unlock,
                              const std::string &iconUrl)
{
    if (!m_raised.insert(grant).second) {
        return;
    }
    raise(std::move(unlock), iconUrl);
}

void UnlockPresenter::revoked(const GrantId &grant)
{
    m_raised.erase(grant);
}

void UnlockPresenter::forgetSession(uint64_t session)
{
    std::erase_if(m_raised, [session](const GrantId &grant) { return grant.session == session; });
}

void UnlockPresenter::raise(AchievementUnlock unlock, const std::string &iconUrl)
{
    // Held while its artwork is still downloading, so the game can present both together
    auto key = unlock.key;
    m_artwork.whenReady(
            key, iconUrl, [this, unlock = std::move(unlock)](std::vector<uint8_t> artwork) mutable {
                unlock.artwork = std::move(artwork);
                INF("Achievements: '{}' unlocked for player {}", unlock.key, unlock.player);
                if (m_sink) {
                    m_sink(std::move(unlock));
                }
            });
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
