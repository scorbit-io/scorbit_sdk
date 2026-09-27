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
#include <optional>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

/**
 * Why the machine cannot evaluate @p definition, or std::nullopt when it can.
 *
 * This is the SDK side of §6.7: a definition that reaches a device has already passed the API's
 * publish-time validation, so only what the evaluator itself depends on is checked here — rule
 * references present, `MODE_STACK` naming two or more modes, qualifiers having instant-valued
 * siblings, at least one rule. Authoring checks (zero-satisfiable rule sets, LE/NE pairing, trophy
 * shape) belong to the API and are not repeated.
 */
std::optional<std::string> findEvaluationProblem(const Definition &definition);

/**
 * Removes from @p definitions every achievement that takes part in, or depends on, a cycle of
 * `ACHIEVEMENT` rules among them: such a chain can never reach a fixed point (§6.6, §7.4).
 *
 * @return The keys removed.
 */
std::vector<std::string> removeCyclicChains(std::vector<Definition> &definitions);

} // namespace achievements
} // namespace detail
} // namespace scorbit
