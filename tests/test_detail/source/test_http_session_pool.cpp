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

#include "http_session_pool.h"
#include <catch2/catch_test_macros.hpp>
#include <variant>

// clazy:excludeall=non-pod-global-static

using namespace scorbit::detail;

namespace {

// Nothing here connects; the pool is exercised through leases, not requests.
const cpr::Url TEST_URL {"https://example.com:443/scorbitron"};

} // namespace

TEST_CASE("A reused session carries nothing over from the last request", "[HttpSessionPool]")
{
    auto &pool = HttpSessionPool::instance();

    {
        auto session = pool.lease(TEST_URL);
        session->SetUrl(TEST_URL);
        session->SetBody(cpr::Body {R"({"stale":true})"});
        session->SetParameters(cpr::Parameters {{"stale", "1"}});
        session->SetHeader(cpr::Header {{"X-Stale", "1"}});
    } // returned to the pool

    auto session = pool.lease(TEST_URL);
    session->SetUrl(TEST_URL);

    // A GET issued after that POST would otherwise resend its body, query string and headers.
    CHECK(std::holds_alternative<std::monostate>(session->GetContent()));
    CHECK(session->GetFullRequestUrl() == TEST_URL.str());
    CHECK(session->GetHeader().find("X-Stale") == session->GetHeader().end());
}

TEST_CASE("Sessions come back to the pool for reuse", "[HttpSessionPool]")
{
    auto &pool = HttpSessionPool::instance();

    // The point of the pool: the second request to an origin gets the first one's session back,
    // so libcurl can keep the connection alive rather than paying another TLS handshake.
    cpr::Session *first = nullptr;
    {
        auto session = pool.lease(TEST_URL);
        first = &*session;
    }

    auto session = pool.lease(TEST_URL);
    CHECK(&*session == first);
}

TEST_CASE("Different origins do not share a session", "[HttpSessionPool]")
{
    auto &pool = HttpSessionPool::instance();

    const cpr::Url other {"https://other.example.com:443/scorbitron"};

    cpr::Session *fromTestUrl = nullptr;
    {
        auto session = pool.lease(TEST_URL);
        fromTestUrl = &*session;
    }

    auto session = pool.lease(other);
    CHECK(&*session != fromTestUrl);
}

TEST_CASE("Concurrent leases on one origin are distinct sessions", "[HttpSessionPool]")
{
    auto &pool = HttpSessionPool::instance();

    // A cpr::Session must not be driven from two threads at once, so overlapping leases have to
    // hand out separate ones even though they share an origin.
    auto first = pool.lease(TEST_URL);
    auto second = pool.lease(TEST_URL);

    CHECK(&*first != &*second);
}
