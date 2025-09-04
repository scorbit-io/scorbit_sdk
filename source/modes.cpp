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
#include "logger.h"
#include <algorithm>
#include <numeric>

namespace scorbit {
namespace detail {

using namespace std;

void Modes::addMode(std::string mode)
{
    // TODO check if mode is valid

    if (find(begin(m_modes), end(m_modes), mode) != end(m_modes)) {
        DBG("Skipping addition: mode '{}' already exists.", mode);
        return;
    }

    m_modes.emplace_back(std::move(mode));
}

void Modes::removeMode(std::string_view mode)
{
    const auto oldSize = m_modes.size();
    m_modes.erase(remove(begin(m_modes), end(m_modes), mode), end(m_modes));

    if (oldSize == m_modes.size()) {
        DBG("Skipping removal of mode '{}': not found.", mode);
    }
}

void Modes::clear()
{
    m_modes.clear();
}

bool Modes::isEmpty() const
{
    return m_modes.empty();
}

bool Modes::contains(std::string_view mode) const
{
    return find(begin(m_modes), end(m_modes), mode) != end(m_modes);
}

string Modes::str() const
{
    if (m_modes.empty())
        return string {};

    return accumulate(next(begin(m_modes)), end(m_modes), m_modes.front(),
                      [](std::string a, const std::string &b) { return std::move(a) + ';' + b; });
}

string Modes::jsonStr() const
{
    if (m_modes.empty())
        return "[]";

    string json = "[\"";
    json += accumulate(next(begin(m_modes)), end(m_modes), m_modes.front(),
                      [](std::string a, const std::string &b) {
                          return std::move(a) + "\",\"" + b;
                      });
    json += "\"]";
    return json;
}

} // namespace detail
} // namespace scorbit
