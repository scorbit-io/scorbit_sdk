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

#include <boost/filesystem/path.hpp>
#include <functional>
#include <string>

namespace scorbit {
namespace detail {

/// Extra check of the installed binary, run before the original is dropped.
using InstalledCheck = std::function<bool(const boost::filesystem::path &installed)>;

/// True if @p path is a non-empty executable image of this platform's format: ELF on Linux,
/// Mach-O on macOS, PE on Windows. On failure @p error says why.
bool isExecutableImage(const boost::filesystem::path &path, std::string &error);

/// Replaces the binary at @p target with @p candidate, so that whatever happens (an error, a
/// crash or a power cut) @p target is either the complete original or the complete new binary.
///
/// @p candidate must be on the same filesystem as @p target, it is moved rather than copied. It is
/// checked with isExecutableImage() first, and the installed file is checked again, together with
/// @p check if given; a failed check puts the original back. On failure @p error says why.
bool installBinary(const boost::filesystem::path &target, const boost::filesystem::path &candidate,
                   std::string &error, const InstalledCheck &check = {});

} // namespace detail
} // namespace scorbit
