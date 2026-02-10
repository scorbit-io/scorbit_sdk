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

#pragma once

#include <string>
#include <chrono>
#include <optional>

namespace scorbit {
namespace detail {

/**
 * @brief Parse JWT token to extract expiration time
 * @param jwtToken The JWT token string
 * @return Optional containing the expiration time as system_clock time_point, or nullopt if parsing fails
 */
std::optional<std::chrono::system_clock::time_point> parseJwtExpiration(const std::string& jwtToken);

/**
 * @brief Check if JWT token is expired
 * @param jwtToken The JWT token string
 * @return true if token is expired or invalid, false if still valid
 */
bool isJwtTokenExpired(const std::string& jwtToken);

/**
 * @brief Get time until JWT token expires
 * @param jwtToken The JWT token string
 * @return Optional containing the duration until expiration, or nullopt if parsing fails
 */
std::optional<std::chrono::seconds> getJwtTokenTimeUntilExpiration(const std::string& jwtToken);

} // namespace detail
} // namespace scorbit
