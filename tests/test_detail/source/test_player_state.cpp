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

#include <../source/player_state.h>
#include <catch2/catch_test_macros.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;

TEST_CASE("Newly created player score 0", "[PlayerState]")
{
    PlayerState ps {1};
    CHECK(ps.player() == 1);
    CHECK(ps.score() == 0);
}

TEST_CASE("Player number", "[PlayerState]")
{
    PlayerState ps {1};
    CHECK(ps.player() == 1);

    PlayerState ps2 {2};
    CHECK(ps2.player() == 2);
}

TEST_CASE("Score", "[PlayerState]")
{
    PlayerState ps {1};

    ps.setScore(100);
    CHECK(ps.score() == 100);
    CHECK(ps.scoreFeature() == 0);

    ps.setScore(200, 10);
    CHECK(ps.score() == 200);
    CHECK(ps.scoreFeature() == 10);
}

TEST_CASE("Compare two players", "[PlayerState]")
{
    PlayerState ps1 {1};
    PlayerState ps2 {1};
    REQUIRE(ps1 == ps2);

    ps1.setScore(100);
    ps2.setScore(100);
    CHECK(ps1 == ps2);

    ps1.setScore(100, 10);
    ps2.setScore(100, 10);
    CHECK(ps1 == ps2);

    ps1.setScore(100);
    ps2.setScore(200);
    CHECK_FALSE(ps1 == ps2);
}

TEST_CASE("Compare two similar players but with different score features", "[PlayerState]")
{
    PlayerState ps1 {1};
    PlayerState ps2 {1};
    REQUIRE(ps1 == ps2);

    ps1.setScore(100, 1);
    ps2.setScore(100, 2);
    CHECK_FALSE(ps1 == ps2);
}
