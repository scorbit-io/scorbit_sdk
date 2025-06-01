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

#include "utils/lru_cache.hpp"

#include <boost/json.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace scorbit;
using namespace scorbit::detail;

TEST_CASE("LRUCache basic put/get operations", "[lru]") {
    LRUCache<std::string, std::vector<uint8_t>> cache(2);

    std::vector<uint8_t> data1 = {1, 2, 3};
    std::vector<uint8_t> data2 = {4, 5, 6};

    SECTION("Insert and retrieve single item") {
        cache.put("url1", data1);

        std::vector<uint8_t> result;
        REQUIRE(cache.get("url1", result));
        CHECK(result == data1);
    }

    SECTION("Overwrite existing item") {
        cache.put("url1", data1);
        std::vector<uint8_t> newData = {9, 9, 9};
        cache.put("url1", newData);

        std::vector<uint8_t> result;
        REQUIRE(cache.get("url1", result));
        CHECK(result == newData);
    }

    SECTION("Eviction after exceeding capacity") {
        cache.put("url1", data1);
        cache.put("url2", data2);
        cache.put("url3", std::vector<uint8_t>{7, 8, 9}); // Should evict url1

        std::vector<uint8_t> result;
        CHECK_FALSE(cache.get("url1", result));
        CHECK(cache.get("url2", result));
        CHECK(cache.get("url3", result));
    }

    SECTION("Access refreshes usage order") {
        cache.put("url1", data1);
        cache.put("url2", data2);
        std::vector<uint8_t> dummy;
        cache.get("url1", dummy); // refresh usage of url1
        cache.put("url3", std::vector<uint8_t>{10, 11}); // Should evict url2

        CHECK(cache.get("url1", dummy)); // url1 should still be present
        CHECK_FALSE(cache.get("url2", dummy)); // url2 should be evicted
        CHECK(cache.get("url3", dummy)); // url3 should be present
    }

    SECTION("Check has() method") {
        cache.put("url1", data1);
        CHECK(cache.has("url1"));
        CHECK_FALSE(cache.has("url2"));
    }

    SECTION("Clear specific item") {
        cache.put("url1", data1);
        cache.put("url2", data2);
        cache.erase("url1");

        CHECK_FALSE(cache.has("url1"));
        CHECK(cache.has("url2"));
    }
}
