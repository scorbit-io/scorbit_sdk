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

#include <cpr/cpr.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scorbit {
namespace detail {

/**
 * Thread-safe pool of reusable cpr::Session objects, keyed by request origin
 * (scheme://host[:port]).
 *
 * Reusing a Session lets libcurl keep the TCP connection alive and resume the
 * TLS session on the next request instead of paying a full handshake every
 * time. The handshake's ECDHE key exchange plus certificate-chain
 * verification is real CPU work, and on a single-core embedded target a
 * burst of fresh handshakes (e.g. several sequential API calls during SDK
 * startup) can occupy the only core long enough to stall a latency-sensitive
 * game loop sharing it.
 *
 * A Session is leased out for the duration of exactly one request and
 * returned to the per-origin idle list afterwards, so concurrent requests to
 * the same origin from different worker threads always get distinct
 * Session/libcurl-handle instances -- a cpr::Session must not be used from
 * two threads at once. Only requests that happen to be serialized in time
 * actually share a connection.
 */
class HttpSessionPool
{
public:
    static HttpSessionPool &instance();

    template<typename... Ts>
    cpr::Response Get(const cpr::Url &url, Ts &&...ts)
    {
        auto leased = lease(url);
        leased->SetUrl(url);
        (leased->SetOption(std::forward<Ts>(ts)), ...);
        return leased->Get();
    }

    template<typename... Ts>
    cpr::Response Post(const cpr::Url &url, Ts &&...ts)
    {
        auto leased = lease(url);
        leased->SetUrl(url);
        (leased->SetOption(std::forward<Ts>(ts)), ...);
        return leased->Post();
    }

    template<typename... Ts>
    cpr::Response Patch(const cpr::Url &url, Ts &&...ts)
    {
        auto leased = lease(url);
        leased->SetUrl(url);
        (leased->SetOption(std::forward<Ts>(ts)), ...);
        return leased->Patch();
    }

    class Lease;

    /**
     * Borrow a session for @p url, reset and ready for one request.
     *
     * Returning the lease puts the session back in the per-origin idle list. Exposed so the
     * reset can be tested without issuing a request.
     */
    Lease lease(const cpr::Url &url);

private:
    // Caps idle connections retained per origin; sessions beyond this (e.g. from a burst of
    // concurrent worker threads hitting the same host) are simply dropped instead of kept
    // warm -- this is an embedded SDK's client pool, not a server-side connection cache.
    static constexpr std::size_t kMaxIdlePerOrigin = 4;

public:
    class Lease
    {
    public:
        Lease(HttpSessionPool &pool, std::string origin, std::unique_ptr<cpr::Session> session)
            : m_pool(pool)
            , m_origin(std::move(origin))
            , m_session(std::move(session))
        {
        }

        ~Lease()
        {
            if (m_session) {
                m_pool.release(std::move(m_origin), std::move(m_session));
            }
        }

        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        Lease(Lease &&) = default;
        Lease &operator=(Lease &&) = delete;

        cpr::Session *operator->() const { return m_session.get(); }
        cpr::Session &operator*() const { return *m_session; }

    private:
        HttpSessionPool &m_pool;
        std::string m_origin;
        std::unique_ptr<cpr::Session> m_session;
    };

private:
    void release(std::string origin, std::unique_ptr<cpr::Session> session);
    static std::string originOf(const cpr::Url &url);

    std::mutex m_mutex;
    std::unordered_map<std::string, std::vector<std::unique_ptr<cpr::Session>>> m_idle;
};

} // namespace detail
} // namespace scorbit
