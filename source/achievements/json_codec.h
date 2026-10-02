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
#include "report.h"
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>

/**
 * Wire format of the achievements API ⇄ domain types, as the API serves it today (reads follow its
 * serializers, the report follows §10.5). A JSON `null` is treated exactly like an absent key;
 * every number is read as `int64`.
 */
namespace scorbit {
namespace detail {
namespace achievements {

/**
 * Parses the device definitions response, `GET achievements/scorbitron/`: a bare array of
 * achievements, each with its `rules`.
 *
 * A definition that is malformed, or that the machine cannot evaluate, is skipped with a warning
 * and the rest are kept — one bad definition must not cost the device its whole cache (§6.7).
 * Achievements depending on a cycle of `ACHIEVEMENT` rules are skipped too.
 *
 * @return std::nullopt when the document itself is not a definitions response.
 */
std::optional<DefinitionSet> parseDefinitionsResponse(const nlohmann::json &json);

/** Parses one achievement object; std::nullopt, with @p error set, if malformed. */
std::optional<Definition> parseDefinition(const nlohmann::json &json, std::string &error);

/**
 * Parses the device progress response, `GET achievements/scorbitron/progress/`: a sparse, bare
 * array of `{achievement: {key, ...}, achieved, ...}`. The API carries no per-rule values there,
 * so every @ref Baseline has only its `achieved` latch. Malformed entries are skipped with a
 * warning.
 *
 * @return std::nullopt when the document itself is not a progress response.
 */
std::optional<Baselines> parseProgressResponse(const nlohmann::json &json);

/**
 * Encodes a `POST /achievements/report/` body (§10.5). `rule_progress` is sparse and names each
 * rule by `index`; `achieved` is omitted when the machine has no verdict.
 */
std::string encodeReportRequest(const ReportRequest &request);

/**
 * Parses the report response (§10.5): `{results: [{key, status, code?, detail?}...]}`. An item
 * with an unknown status is skipped with a warning.
 *
 * @return std::nullopt when the document itself is not a report response.
 */
std::optional<std::vector<ReportOutcome>> parseReportResponse(const nlohmann::json &json);

} // namespace achievements
} // namespace detail
} // namespace scorbit
