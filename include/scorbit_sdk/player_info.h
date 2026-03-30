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

#include <cstdint>
#include <string>
#include <vector>

namespace scorbit {

using Picture = std::vector<uint8_t>; // The profile picture binary (jpg)

struct PlayerInfo {
    bool hasInfo() const { return !id.empty(); }

    std::string id;            /// The player's ID
    std::string preferredName; /// Either name or initials
    std::string name;          /// The player's name to display
    std::string initials;      /// The player's initials, e.g. "DTM"
    std::string pictureUrl;    /// The URL of the profile picture
    std::string claimDeeplink; /// Claim URL for unclaimed player slots (empty if claimed)
};

}
