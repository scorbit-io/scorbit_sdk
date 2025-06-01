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
#include <vector>
#include <string>
#include <string_view>

namespace scorbit {
namespace detail {

class Modes
{
public:
    Modes() = default;

    void addMode(std::string mode);
    void removeMode(std::string_view mode);
    void clear();
    bool isEmpty() const;
    bool contains(std::string_view mode) const;

    std::string str() const;

private:
    std::vector<boost::flyweight<std::string>> m_modes;

    // Declare friend for operator==
    friend bool operator==(const Modes &, const Modes &);
};

inline bool operator==(const scorbit::detail::Modes &lhs, const scorbit::detail::Modes &rhs)
{
    return lhs.m_modes == rhs.m_modes;
}

} // namespace detail
} // namespace scorbit
