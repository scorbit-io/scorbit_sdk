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

#include "identifiers.h"

#include <catch2/catch_test_macros.hpp>

using scorbit::detail::isScorbitronClass;

TEST_CASE("provider class: a Scorbitron, a virtual one and the VPIN daemon need no machine id")
{
    CHECK(isScorbitronClass("scorbitron"));
    CHECK(isScorbitronClass("vscorbitron"));
    CHECK(isScorbitronClass("vpxplugin"));
}

TEST_CASE("provider class: a game's own provider is not a Scorbitron")
{
    CHECK_FALSE(isScorbitronClass("spooky"));
    CHECK_FALSE(isScorbitronClass(""));
    CHECK_FALSE(isScorbitronClass("VPXPLUGIN")); // slugs are exact
}
