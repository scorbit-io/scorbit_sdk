/*
 * Scorbit SDK
 *
 * (c) 2026 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

// SB-6054: the pairing short code never reaches a log line.

#include <logger/logger.h>
#include <scorbit_sdk/log.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using logger::redact;
using namespace std::chrono_literals;

TEST_CASE("redact: replaces the short code in a JSON reply", "[logger][redact]")
{
    CHECK(redact(R"({"shortcode":"AB12CD","paired":false})")
          == R"({"shortcode":"<redacted>","paired":false})");
    CHECK(redact(R"({ "shortcode" : "AB12CD" })") == R"({ "shortcode" : "<redacted>" })");
    CHECK(redact(R"(API set x: ok, {"a":{"shortcode":"X1"},"b":{"shortcode":"Y2"}})")
          == R"(API set x: ok, {"a":{"shortcode":"<redacted>"},"b":{"shortcode":"<redacted>"}})");
}

TEST_CASE("redact: replaces the short code in JSON escaped inside a string", "[logger][redact]")
{
    CHECK(redact(R"({"detail":"{\"shortcode\":\"AB12CD\"}"})")
          == R"({"detail":"{\"shortcode\":\"<redacted>\"}"})");
}

TEST_CASE("redact: any JSON whitespace around the colon", "[logger][redact]")
{
    CHECK(redact("{\"shortcode\"\n:\r\n\t\"AB12CD\"}") == "{\"shortcode\"\n:\r\n\t\"<redacted>\"}");
    // Escaped inside a string, the same layout carries \n and \t escapes.
    CHECK(redact(R"({"detail":"{\"shortcode\"\n:\t\"AB12CD\"}"})")
          == R"({"detail":"{\"shortcode\"\n:\t\"<redacted>\"}"})");
}

TEST_CASE("redact: the whole value, through its own escapes", "[logger][redact]")
{
    CHECK(redact(R"({"shortcode":"\u0041B12CD","x":1})") == R"({"shortcode":"<redacted>","x":1})");
    CHECK(redact(R"({"shortcode":"AB\"12CD"})") == R"({"shortcode":"<redacted>"})");
    // Escaped inside a string, the value's escapes have a doubled backslash.
    CHECK(redact(R"({"detail":"{\"shortcode\":\"\\u0041B12CD\",\"x\":1}"})")
          == R"({"detail":"{\"shortcode\":\"<redacted>\",\"x\":1}"})");
}

TEST_CASE("redact: only the key itself, not a longer one ending in it", "[logger][redact]")
{
    const std::string text = R"({"previous_shortcode":"diagnostic-value"})";
    CHECK(redact(text) == text);
    CHECK(redact(R"({"previous_shortcode":"v","shortcode":"AB12CD"})")
          == R"({"previous_shortcode":"v","shortcode":"<redacted>"})");
}

TEST_CASE("redact: a cut-off value is redacted to the end", "[logger][redact]")
{
    CHECK(redact(R"(reply: {"shortcode":"AB12)") == R"(reply: {"shortcode":"<redacted>)");
}

TEST_CASE("redact: leaves everything else alone", "[logger][redact]")
{
    for (const std::string text : {
                 R"({"machine":"m1","paired":true})",
                 R"({"shortcode":null})",
                 R"({"shortcode_ttl":"60"})",
                 "the shortcode is requested",
                 "",
         }) {
        CHECK(redact(text) == text);
    }
}

TEST_CASE("redact: a logged reply reaches the host without the short code", "[logger][redact]")
{
    std::mutex mutex;
    std::vector<std::string> lines;
    scorbit::addLoggerCallback(
            [&](std::string_view msg, scorbit::LogLevel, const char *, int, int64_t) {
                std::lock_guard lock(mutex);
                lines.emplace_back(msg);
            },
            512);

    INF("API initial Scorbitron object sent: ok, {}", R"({"shortcode":"ZX81QQ","paired":false})");
    ERR("API set {}: failed, error code: {}, reply: {}", "name", 400, R"({"shortcode":"ZX81QQ"})");
    DBG("API {} request OK, {}", "PATCH", R"({"shortcode":"ZX81QQ"})");

    // Delivery is asynchronous; poll rather than sleep a fixed time.
    std::vector<std::string> seen;
    for (auto deadline = std::chrono::steady_clock::now() + 5s;
         std::chrono::steady_clock::now() < deadline; std::this_thread::sleep_for(5ms)) {
        std::lock_guard lock(mutex);
        seen = lines;
        if (seen.size() >= 2) {
            break;
        }
    }
    scorbit::resetLogger();

    REQUIRE(seen.size() >= 2);
    for (const auto &line : seen) {
        INFO(line);
        CHECK(line.find("ZX81QQ") == std::string::npos);
    }
    CHECK(seen[0].find(R"("shortcode":"<redacted>")") != std::string::npos);
}
