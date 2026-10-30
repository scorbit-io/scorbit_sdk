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

#include "progress.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

/** One item of `POST /achievements/report/` (§10.5). */
struct ReportItem {
    std::string key;

    /** The claim: true asks for the grant, false says "evaluated, not earned", nullopt = no verdict. */
    std::optional<bool> achieved;

    /** Sparse: only rules the machine could judge. */
    RuleProgressMap rules;
};

/** One `POST /achievements/report/` request: one player's items in one session. */
struct ReportRequest {
    std::string userId;
    std::string sessionUuid;

    /** Increases monotonically within the session, across all its players (§10.1). */
    int64_t sequence {0};

    std::vector<ReportItem> items;
};

/** Per-item report result (§10.5). */
enum class ReportStatus { Unlocked, InProgress, AlreadyHeld, Rejected };

struct ReportOutcome {
    std::string key;
    ReportStatus status {ReportStatus::InProgress};
    std::string code;
    std::string detail;
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
