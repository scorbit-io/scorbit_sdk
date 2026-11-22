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

#include <nlohmann/json.hpp>
#include <string>
#include <chrono>
#include <optional>
#include <vector>

namespace scorbit {
namespace detail {

/**
 * @brief Decode a JWT's payload, without verifying its signature
 * @param jwtToken The JWT token string
 * @return The payload as JSON, or nullopt if the token can't be decoded
 */
std::optional<nlohmann::json> parseJwtPayload(const std::string &jwtToken);

/**
 * @brief The JWT's "channels" claim, as Centrifugo connection tokens carry it
 * @param jwtToken The JWT token string
 * @return Each string in the claim, in order; empty when the token has none or can't be decoded
 */
std::vector<std::string> parseJwtChannels(const std::string &jwtToken);

/**
 * @brief Parse JWT token to extract expiration time
 * @param jwtToken The JWT token string
 * @return Optional containing the expiration time as system_clock time_point, or nullopt if parsing
 * fails
 */
std::optional<std::chrono::system_clock::time_point>
parseJwtExpiration(const std::string &jwtToken);

/**
 * @brief Check if JWT token is expired
 * @param jwtToken The JWT token string
 * @return true if token is expired or invalid, false if still valid
 */
bool isJwtTokenExpired(const std::string &jwtToken);

/**
 * @brief Get time until JWT token expires
 * @param jwtToken The JWT token string
 * @return Optional containing the duration until expiration, or nullopt if parsing fails
 */
std::optional<std::chrono::seconds> getJwtTokenTimeUntilExpiration(const std::string &jwtToken);

} // namespace detail
} // namespace scorbit
