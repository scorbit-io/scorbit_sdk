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

#include "serial_task_queue.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// clazy:excludeall=non-pod-global-static

using namespace scorbit::detail;

// task_t is qualified throughout: macOS's <mach/mach_types.h> declares a global
// ::task_t, which the using-directive above makes ambiguous.

TEST_CASE("SerialTaskQueue dispatches immediately when the key is idle")
{
    std::vector<std::string> ran;
    SerialTaskQueue queue {[](scorbit::detail::task_t task) { task(); }};

    queue.submit("capture_retained", [&ran] { ran.push_back("A"); });

    REQUIRE(ran == std::vector<std::string> {"A"});
    CHECK(queue.isInFlight("capture_retained"));
    CHECK(queue.pendingCount("capture_retained") == 0);
}

TEST_CASE("SerialTaskQueue holds a second task for the same key until the first finishes")
{
    std::vector<std::string> ran;
    SerialTaskQueue queue {[](scorbit::detail::task_t task) { task(); }};

    queue.submit("capture_retained", [&ran] { ran.push_back("A"); });
    queue.submit("capture_retained", [&ran] { ran.push_back("B"); });

    // B must not have gone out while A is still in flight.
    CHECK(ran == std::vector<std::string> {"A"});
    CHECK(queue.pendingCount("capture_retained") == 1);

    queue.finish("capture_retained");

    CHECK(ran == std::vector<std::string> {"A", "B"});
    CHECK(queue.pendingCount("capture_retained") == 0);
}

TEST_CASE("SerialTaskQueue keeps a parked task ahead of one queued behind it")
{
    // The regression this queue exists for. A is dispatched and then stalls -- standing in for a
    // request the auth gate parked and will re-post at the tail of its strand. B is submitted
    // while A is stalled. Without serialisation B would reach the API first and A would land
    // afterwards, re-asserting the state B withdrew.
    std::vector<std::string> sent;
    SerialTaskQueue queue {[](scorbit::detail::task_t task) { task(); }};

    queue.submit("capture_retained", [&sent] { sent.push_back("report v1"); });
    queue.submit("capture_retained", [&sent] { sent.push_back("withdraw"); });

    // While A is parked, nothing else for this type has gone out.
    REQUIRE(sent == std::vector<std::string> {"report v1"});

    // Authentication completes and A finally finishes; only then does the withdrawal go out.
    queue.finish("capture_retained");

    REQUIRE(sent == std::vector<std::string> {"report v1", "withdraw"});
    // The withdrawal is last, so it is the state the API is left holding.
    CHECK(sent.back() == "withdraw");
}

TEST_CASE("SerialTaskQueue preserves submission order across a longer backlog")
{
    std::vector<std::string> ran;
    SerialTaskQueue queue {[](scorbit::detail::task_t task) { task(); }};

    for (const auto *name : {"1", "2", "3", "4"}) {
        queue.submit("sdk", [&ran, name] { ran.push_back(name); });
    }

    CHECK(ran == std::vector<std::string> {"1"});
    CHECK(queue.pendingCount("sdk") == 3);

    queue.finish("sdk");
    queue.finish("sdk");
    queue.finish("sdk");

    CHECK(ran == std::vector<std::string> {"1", "2", "3", "4"});
}

TEST_CASE("SerialTaskQueue keys are independent of each other")
{
    std::vector<std::string> ran;
    SerialTaskQueue queue {[](scorbit::detail::task_t task) { task(); }};

    queue.submit("capture_retained", [&ran] { ran.push_back("retained"); });
    // A different type must not be held up behind an in-flight one.
    queue.submit("sdk", [&ran] { ran.push_back("sdk"); });

    CHECK(ran == std::vector<std::string> {"retained", "sdk"});
    CHECK(queue.pendingCount("capture_retained") == 0);
    CHECK(queue.pendingCount("sdk") == 0);
}

TEST_CASE("SerialTaskQueue finish on an idle or unknown key is a no-op")
{
    SerialTaskQueue queue {[](scorbit::detail::task_t task) { task(); }};

    queue.finish("never_submitted");
    CHECK(queue.pendingCount("never_submitted") == 0);
    CHECK_FALSE(queue.isInFlight("never_submitted"));

    std::vector<std::string> ran;
    queue.submit("sdk", [&ran] { ran.push_back("A"); });
    queue.finish("sdk"); // releases the slot
    queue.finish("sdk"); // second release must not corrupt the slot

    queue.submit("sdk", [&ran] { ran.push_back("B"); });
    CHECK(ran == std::vector<std::string> {"A", "B"});
}

TEST_CASE("SerialTaskQueue releases the slot so a later task can be dispatched")
{
    std::vector<std::string> ran;
    SerialTaskQueue queue {[](scorbit::detail::task_t task) { task(); }};

    queue.submit("sdk", [&ran] { ran.push_back("A"); });
    queue.finish("sdk");
    CHECK_FALSE(queue.isInFlight("sdk"));

    queue.submit("sdk", [&ran] { ran.push_back("B"); });
    CHECK(ran == std::vector<std::string> {"A", "B"});
    CHECK(queue.isInFlight("sdk"));
}
