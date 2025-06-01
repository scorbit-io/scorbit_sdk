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

#include <utils/date_time_parser.h>
#include <catch2/catch_test_macros.hpp>

using namespace scorbit::detail;

TEST_CASE("parseHttpDateToUnixTimestamp - Valid Dates") {
    CHECK(parseHttpDateToUnixTimestamp("Fri, 21 Mar 2025 12:34:56 GMT") == 1742560496);
    CHECK(parseHttpDateToUnixTimestamp("Wed, 01 Jan 2020 00:00:00 GMT") == 1577836800);
    CHECK(parseHttpDateToUnixTimestamp("Fri, 31 Dec 1999 23:59:59 GMT") == 946684799);
}

TEST_CASE("parseHttpDateToUnixTimestamp - Invalid Dates") {
    CHECK(parseHttpDateToUnixTimestamp("Invalid Date String") == -1);
    CHECK(parseHttpDateToUnixTimestamp("2025-03-21T12:34:56Z") == -1);
    CHECK(parseHttpDateToUnixTimestamp("21 Mar 2025 12:34:56") == -1);
    CHECK(parseHttpDateToUnixTimestamp("") == -1);
}
