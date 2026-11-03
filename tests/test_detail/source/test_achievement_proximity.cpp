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

#include "achievement_test_support.h"
#include <../source/achievements/unlock_proximity.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace achievement_test;

TEST_CASE("Unlock proximity", "[achievements]")
{
    const auto spins = rule(RuleType::Event, Comparison::Ge, 100, "spins");
    const auto balloon = rule(RuleType::Mode, Comparison::Ge, 1, "balloon");
    const auto drainLe = rule(RuleType::Event, Comparison::Le, 2, "drains");

    SECTION("A >= rule scores its share of the target")
    {
        const auto definition = inSession("a", {spins});
        CHECK(unlockProximity(definition, {{0, {50, false}}}).score == Catch::Approx(0.5));
        CHECK_FALSE(unlockProximity(definition, {{0, {79, false}}}).near);
        CHECK(unlockProximity(definition, {{0, {80, false}}}).near);
    }

    SECTION("An unlock already decided is not near: nothing remains to fetch ahead for")
    {
        const auto definition = inSession("a", {spins});
        const auto proximity = unlockProximity(definition, {{0, {100, true}}});
        CHECK(proximity.score == Catch::Approx(1.0));
        CHECK_FALSE(proximity.near);
    }

    SECTION("A yes-or-no rule alone is never near: nothing tells it is close")
    {
        CHECK_FALSE(unlockProximity(inSession("a", {drainLe}), {{0, {1, false}}}).near);
        CHECK_FALSE(unlockProximity(inSession("a", {balloon}), {{0, {0, false}}}).near);
    }

    SECTION("One yes-or-no rule left of several is near")
    {
        const auto definition = inSession("a", {spins, balloon});
        CHECK(unlockProximity(definition, {{0, {100, true}}, {1, {0, false}}}).near);
        CHECK(unlockProximity(definition, {{0, {100, true}}, {1, {0, false}}}).score
              == Catch::Approx(0.5));
        CHECK(unlockProximity(definition, {{0, {85, false}}, {1, {0, false}}}).near);
        CHECK_FALSE(unlockProximity(definition, {{0, {10, false}}, {1, {0, false}}}).near);

        const auto twoLeft = inSession("a", {spins, balloon, drainLe});
        CHECK_FALSE(unlockProximity(twoLeft, {{0, {100, true}}, {1, {0, false}}, {2, {5, false}}})
                            .near);
    }

    SECTION("Several rules left are near only when each is close")
    {
        const auto definition = inSession(
                "a", {spins, rule(RuleType::Event, Comparison::Ge, 10, "ramps"), balloon});
        CHECK(unlockProximity(definition, {{0, {90, false}}, {1, {9, false}}, {2, {1, true}}})
                      .near);
        CHECK_FALSE(unlockProximity(definition, {{0, {90, false}}, {1, {5, false}}, {2, {1, true}}})
                            .near);
    }

    SECTION("A rule not judged yet scores 0")
    {
        const auto definition = inSession("a", {spins, balloon});
        CHECK_FALSE(unlockProximity(definition, {{1, {1, true}}, {0, {50, false}}}).near);
        CHECK_FALSE(unlockProximity(definition, {{1, {1, true}}}).near);
        CHECK(unlockProximity(definition, {{0, {100, true}}}).near); // the mode is all that's left
        CHECK(unlockProximity(definition, {}).score == Catch::Approx(0.0));
    }
}
