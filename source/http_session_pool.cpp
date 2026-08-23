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

#include "http_session_pool.h"

namespace scorbit {
namespace detail {

HttpSessionPool &HttpSessionPool::instance()
{
    static HttpSessionPool pool;
    return pool;
}

std::string HttpSessionPool::originOf(const cpr::Url &url)
{
    const std::string &s = url.str();
    const auto schemeEnd = s.find("://");
    if (schemeEnd == std::string::npos) {
        return s;
    }
    const auto pathStart = s.find('/', schemeEnd + 3);
    return pathStart == std::string::npos ? s : s.substr(0, pathStart);
}

HttpSessionPool::Lease HttpSessionPool::lease(const cpr::Url &url)
{
    auto origin = originOf(url);
    std::unique_ptr<cpr::Session> session;
    {
        std::scoped_lock lock(m_mutex);
        auto it = m_idle.find(origin);
        if (it != m_idle.end() && !it->second.empty()) {
            session = std::move(it->second.back());
            it->second.pop_back();
        }
    }
    if (!session) {
        session = std::make_unique<cpr::Session>();
    }

    // A pooled Session still carries the previous request's body, query parameters and headers,
    // and SetUrl() clears none of them. Without this a GET issued after a POST to the same origin
    // resends that POST's body and query string. Clearing once here covers every verb; on a fresh
    // session it does nothing.
    session->RemoveContent();
    session->SetParameters(cpr::Parameters {});
    session->SetHeader(cpr::Header {});

    return Lease {*this, std::move(origin), std::move(session)};
}

void HttpSessionPool::release(std::string origin, std::unique_ptr<cpr::Session> session)
{
    std::scoped_lock lock(m_mutex);
    auto &idle = m_idle[origin];
    if (idle.size() < kMaxIdlePerOrigin) {
        idle.push_back(std::move(session));
    }
    // else: drop it: cpr::Session's destructor closes the underlying libcurl handle.
}

} // namespace detail
} // namespace scorbit
