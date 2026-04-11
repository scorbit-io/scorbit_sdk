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
#include <vector>

namespace scorbit {
namespace detail {

bool extract(const std::string &archivePath, const std::string &outputDir);

struct ArchiveFileEntry {
    std::string archivePath; // path inside archive (e.g. "logs/scorbitd.log")
    std::string sourcePath;  // filesystem path
};

struct ArchiveMemoryEntry {
    std::string archivePath; // path inside archive (e.g. "logs/extra.log")
    std::string data;        // in-memory content
};

bool createTarGz(const std::string &outputPath, const std::vector<ArchiveFileEntry> &files,
                 const std::vector<ArchiveMemoryEntry> &memoryEntries = {});

} // namespace detail
} // namespace scorbit
