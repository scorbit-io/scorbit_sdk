/*
 * Scorbit SDK
 *
 * (c) 2026 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace scorbit {
namespace detail {

/// Which redirects a download may follow.
enum class RedirectPolicy {
    Follow, ///< any, as curl does by default
    None,   ///< none: a redirect is refused
    Hosts,  ///< only to a host in DownloadOptions::redirectHosts, never from https to http
};

/// Limits on one file download (SB-5526). Default-constructed, a download behaves as it always has.
struct DownloadOptions {
    /// The most bytes the body may have; a larger one is aborted and no file is left.
    std::optional<uint64_t> maxBytes;
    RedirectPolicy redirect {RedirectPolicy::Follow};
    /// The hosts RedirectPolicy::Hosts may redirect to, lower-case.
    std::vector<std::string> redirectHosts;
    /// Whether the caller gave options at all; the reply then also names the final URL.
    bool given {false};
};

/**
 * @p json, as sb_download_with_options() takes it, as DownloadOptions; nullopt when it is not a
 * JSON object or carries a key this SDK does not know. An unknown key is refused, not ignored, so
 * a limit asked of an older SDK fails instead of being silently dropped. An empty string is no
 * options.
 */
std::optional<DownloadOptions> parseDownloadOptions(std::string_view json);

} // namespace detail
} // namespace scorbit
