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

#include "unlock_proximity.h"
#include <algorithm>

namespace scorbit {
namespace detail {
namespace achievements {

namespace {

/** Whether @p rule's progress is a share of its target, rather than yes-or-no. */
bool hasShare(const Rule &rule)
{
    return rule.comparison == Comparison::Ge && rule.target > 1;
}

double ruleScore(const Rule &rule, const RuleProgress *progress)
{
    if (!progress) {
        return 0.0;
    }
    if (progress->satisfied) {
        return 1.0;
    }
    if (hasShare(rule)) {
        return std::clamp(static_cast<double>(progress->value) / static_cast<double>(rule.target),
                          0.0, 1.0);
    }
    return 0.0;
}

} // namespace

UnlockProximity unlockProximity(const Definition &definition, const RuleProgressMap &rules)
{
    if (definition.rules.empty()) {
        return {};
    }

    double total = 0.0;
    size_t unsatisfied = 0;
    size_t yesOrNoLeft = 0;
    bool sharesClose = true;
    for (size_t index = 0; index < definition.rules.size(); ++index) {
        const auto &rule = definition.rules[index];
        const auto it = rules.find(index);
        const auto *progress = it == rules.end() ? nullptr : &it->second;
        const auto score = ruleScore(rule, progress);
        total += score;

        if (progress && progress->satisfied) {
            continue;
        }
        ++unsatisfied;
        if (hasShare(rule)) {
            sharesClose = sharesClose && score >= UnlockProximity::NEAR_SHARE;
        } else {
            ++yesOrNoLeft;
        }
    }

    UnlockProximity proximity;
    proximity.score = total / static_cast<double>(definition.rules.size());
    proximity.imminent = unsatisfied > 0 && sharesClose
                      && yesOrNoLeft <= (definition.rules.size() > 1 ? 1U : 0U);
    return proximity;
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
