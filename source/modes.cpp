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

#include "modes.h"
#include <logger/logger.h>
#include <algorithm>
#include <numeric>
#include <ranges>

namespace {

constexpr uint32_t MODE_EXPIRY_DEFAULT_SEC = 3;
constexpr uint32_t MODE_EXPIRY_MAX_SEC = 10;

/** API contract: 0 -> 3s (recommended default), >10 -> 10s cap, 1-10 unchanged. */
uint32_t normalizeModeExpirySeconds(uint32_t s)
{
    if (s == 0) {
        return MODE_EXPIRY_DEFAULT_SEC;
    }
    if (s > MODE_EXPIRY_MAX_SEC) {
        return MODE_EXPIRY_MAX_SEC;
    }
    return s;
}

} // namespace

namespace scorbit {
namespace detail {

using namespace std;

void Modes::addMode(std::string mode)
{
    // TODO check if mode is valid

    if (std::ranges::find(m_modes, mode) != end(m_modes)) {
        DBG("Skipping addition: mode '{}' already exists.", mode);
        return;
    }

    m_modes.emplace_back(std::move(mode));
}

void Modes::addOrPromoteToFront(std::string mode)
{
    std::erase(m_modes, mode);
    m_modes.emplace(m_modes.begin(), std::move(mode));
}

void Modes::removeMode(const std::string &mode)
{
    const auto oldSize = m_modes.size();
    std::erase(m_modes, mode);
    m_modeExpiryDeadlines.erase(mode);

    if (oldSize == m_modes.size()) {
        DBG("Skipping removal of mode '{}': not found.", mode);
    }
}

void Modes::clear()
{
    m_modes.clear();
    m_modeExpiryDeadlines.clear();
}

void Modes::addModeExpiring(std::string mode, uint32_t durationSeconds)
{
    const uint32_t norm = normalizeModeExpirySeconds(durationSeconds);
    const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<int64_t>(norm));
    m_modeExpiryDeadlines[mode] = deadline;
    addOrPromoteToFront(std::move(mode));
}

void Modes::tickExpiries()
{
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::string> due;
    due.reserve(m_modeExpiryDeadlines.size());
    for (const auto &kv : m_modeExpiryDeadlines) {
        if (kv.second <= now) {
            due.push_back(kv.first);
        }
    }
    for (const auto &m : due) {
        m_modeExpiryDeadlines.erase(m);
        std::erase(m_modes, m);
    }
}

std::optional<std::chrono::steady_clock::duration> Modes::nextExpiryDelay() const
{
    if (m_modeExpiryDeadlines.empty()) {
        return std::nullopt;
    }

    auto minTime = std::ranges::min_element(m_modeExpiryDeadlines, {}, &decltype(m_modeExpiryDeadlines)::value_type::second)->second;

    using clock = std::chrono::steady_clock;
    auto delay = minTime - clock::now();
    if (delay < clock::duration::zero()) {
        delay = clock::duration::zero();
    }
    return delay;
}

bool Modes::hasExpiryDeadlines() const
{
    return !m_modeExpiryDeadlines.empty();
}

void Modes::clearExpiries()
{
    m_modeExpiryDeadlines.clear();
}

bool Modes::isEmpty() const
{
    return m_modes.empty();
}

bool Modes::contains(const string &mode) const
{
    return std::ranges::find(m_modes, mode) != end(m_modes);
}

string Modes::str() const
{
    if (m_modes.empty())
        return string {};

    return accumulate(next(begin(m_modes)), end(m_modes), string(m_modes.front()),
                      [](std::string a, const std::string &b) { return std::move(a) + ';' + b; });
}

string Modes::jsonStr() const
{
    if (m_modes.empty())
        return "[]";

    string json = "[\"";
    json += accumulate(next(begin(m_modes)), end(m_modes), string(m_modes.front()),
                      [](std::string a, const std::string &b) {
                          return std::move(a) + "\",\"" + b;
                      });
    json += "\"]";
    return json;
}

} // namespace detail
} // namespace scorbit
