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
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace scorbit {
namespace detail {

/// Extra check of the installed binary, run before the original is dropped.
using InstalledCheck = std::function<bool(const boost::filesystem::path &installed)>;

/// What an ELF image is built for
struct ElfInfo {
    std::uint8_t elfClass;  // 1: 32-bit, 2: 64-bit
    std::uint8_t byteOrder; // 1: little endian, 2: big endian
    std::uint16_t machine;  // e_machine

    bool operator==(const ElfInfo &) const = default;
};

/// Reads the header of the ELF image at @p path, and checks that the file holds all that the
/// header refers to: the program headers, the section headers and every loadable segment. A
/// truncated image fails. On failure @p error says why.
std::optional<ElfInfo> readElfImage(const boost::filesystem::path &path, std::string &error);

/// True if @p path is a non-empty executable image of this platform's format: ELF on Linux, which
/// must also be complete (see readElfImage()), Mach-O on macOS, PE on Windows. On failure @p error
/// says why.
bool isExecutableImage(const boost::filesystem::path &path, std::string &error);

/// Replaces the binary at @p target with @p candidate, so that whatever happens (an error, a
/// crash or a power cut) @p target is either the complete original or the complete new binary.
///
/// @p candidate must be on the same filesystem as @p target, it is moved rather than copied. It is
/// checked with isExecutableImage() first, and on Linux it must be built for the same
/// architecture as @p target. The installed file is checked again, together with @p check if
/// given; a failed check puts the original back. On failure @p error says why.
bool installBinary(const boost::filesystem::path &target, const boost::filesystem::path &candidate,
                   std::string &error, const InstalledCheck &check = {});

} // namespace detail
} // namespace scorbit
