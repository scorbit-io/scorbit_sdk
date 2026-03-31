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

#include <boost/flyweight.hpp>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace scorbit {
namespace detail {

class Modes
{
public:
    Modes() = default;

    void addMode(std::string mode);
    /** Remove if present, then insert at front (most prominent in str()/JSON). */
    void addOrPromoteToFront(std::string mode);
    void removeMode(const std::string &mode);
    void clear();
    bool isEmpty() const;
    bool contains(const std::string &mode) const;

    /**
     * Add a mode that is removed automatically after a duration.
     * @param durationSeconds unsigned seconds; 0 -> 3s default, >10 -> 10s cap, 1-10 unchanged.
     */
    void addModeExpiring(std::string mode, uint32_t durationSeconds);

    /** Remove modes whose expiry deadline has passed. */
    void tickExpiries();

    /** Delay until the soonest deadline, or nullopt if no expiring modes. */
    std::optional<std::chrono::steady_clock::duration> nextExpiryDelay() const;

    bool hasExpiryDeadlines() const;

    /** Clear the deadline map only (leaves the mode list untouched). */
    void clearExpiries();

    std::string str() const;
    std::string jsonStr() const;

private:
    std::vector<boost::flyweight<std::string>> m_modes;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_modeExpiryDeadlines;

    friend bool operator==(const Modes &, const Modes &);
};

inline bool operator==(const scorbit::detail::Modes &lhs, const scorbit::detail::Modes &rhs)
{
    return lhs.m_modes == rhs.m_modes;
}

} // namespace detail
} // namespace scorbit
