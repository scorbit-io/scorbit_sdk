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

#include "definition.h"
#include "progress.h"
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>

/**
 * Wire format of the achievements API (§10) ⇄ domain types. A JSON `null` is treated exactly like
 * an absent key; every number is read as `int64`.
 */
namespace scorbit {
namespace detail {
namespace achievements {

/**
 * Parses the device definitions response (§10.3): `{game, frames_version, results: [...]}`.
 *
 * A definition that is malformed, or that the machine cannot evaluate, is skipped with a warning
 * and the rest are kept — one bad definition must not cost the device its whole cache (§6.7).
 * Achievements depending on a cycle of `ACHIEVEMENT` rules are skipped too.
 *
 * @return std::nullopt when the document itself is not a definitions response.
 */
std::optional<DefinitionSet> parseDefinitionsResponse(const nlohmann::json &json);

/** Parses one Achievement object (§10.2); std::nullopt, with @p error set, if malformed. */
std::optional<Definition> parseDefinition(const nlohmann::json &json, std::string &error);

/**
 * Parses the device progress response (§10.4): `{user_id, results: [UserAchievement...]}`.
 * Malformed entries are skipped with a warning.
 *
 * @return std::nullopt when the document itself is not a progress response.
 */
std::optional<Baselines> parseProgressResponse(const nlohmann::json &json);

/**
 * Parses the `rule_progress` array of a UserAchievement. Entries whose `value` is null — never
 * reported — are left out, so an absent index always means "no measurement".
 */
RuleProgressMap parseRuleProgress(const nlohmann::json &json);

} // namespace achievements
} // namespace detail
} // namespace scorbit
