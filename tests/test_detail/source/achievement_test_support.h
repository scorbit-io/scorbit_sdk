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

#include <../source/achievements/definition.h>
#include <../source/achievements/player_facts.h>
#include <string>
#include <utility>
#include <vector>

namespace achievement_test {

using namespace scorbit::detail::achievements;

/** Builds timeline rows fluently; each row carries over the state of the previous one. */
class Timeline
{
public:
    Timeline &at(int64_t timeMs)
    {
        m_row.timeMs = timeMs;
        return *this;
    }
    Timeline &player(PlayerNumber p)
    {
        m_row.player = p;
        return *this;
    }
    Timeline &ball(BallNumber b)
    {
        m_row.ball = b;
        return *this;
    }
    Timeline &score(PlayerNumber p, int64_t s)
    {
        m_row.scores[p] = s;
        return *this;
    }
    Timeline &modes(std::vector<std::string> m)
    {
        m_row.modes = std::move(m);
        return *this;
    }
    Timeline &completed(std::vector<std::string> m)
    {
        m_row.completedModes = std::move(m);
        return *this;
    }
    Timeline &event(std::string name, int64_t value)
    {
        m_row.events.emplace_back(std::move(name), value);
        return *this;
    }
    Timeline &commit()
    {
        facts.apply(m_row);
        m_row.completedModes.clear();
        m_row.events.clear();
        return *this;
    }

    const TimelineRow &row() const { return m_row; }

    SessionFacts facts;

private:
    TimelineRow m_row {0, 1, 1, {{1, 0}}, {}, {}, {}};
};

inline Rule rule(RuleType type, Comparison comparison, int64_t target, std::string reference = {})
{
    Rule r {type, comparison, target, std::move(reference), {}};
    if (type == RuleType::ModeStack) {
        size_t start = 0;
        for (size_t comma; (comma = r.reference.find(',', start)) != std::string::npos;
             start = comma + 1) {
            r.stackModes.push_back(r.reference.substr(start, comma - start));
        }
        r.stackModes.push_back(r.reference.substr(start));
    }
    return r;
}

inline Definition definition(std::string key, EvaluationClass evaluation, std::vector<Rule> rules)
{
    Definition d;
    d.key = std::move(key);
    d.name = d.key;
    d.evaluation = evaluation;
    d.rules = std::move(rules);
    return d;
}

inline Definition inSession(std::string key, std::vector<Rule> rules)
{
    return definition(std::move(key), EvaluationClass::InSession, std::move(rules));
}

inline Definition unlimited(std::string key, std::vector<Rule> rules)
{
    return definition(std::move(key), EvaluationClass::Unlimited, std::move(rules));
}

} // namespace achievement_test
