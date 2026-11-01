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

#include "key_store.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace scorbit_godot {

namespace {

// The narrow path constructors would read UTF-8 in the local code page on Windows.
fs::path toPath(const std::string &utf8)
{
    return fs::u8path(utf8);
}

} // namespace

bool saveKey(const std::string &path, const std::string &key)
{
    if (path.empty()) {
        return false;
    }
    const auto file = toPath(path);
    std::error_code ec;
    if (fs::create_directories(file.parent_path(), ec)) {
        fs::permissions(file.parent_path(), fs::perms::owner_all, fs::perm_options::replace, ec);
    }

    // Restricted before the first byte is written, then renamed over any old key, so a key is
    // never readable by other accounts, even briefly. (On Windows the user's profile ACL governs.)
    auto tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace, ec);
        if (ec) {
            out.close();
            fs::remove(tmp, ec);
            return false;
        }
        out << key;
        if (!out.flush()) {
            out.close();
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(tmp, ignored);
        return false;
    }
    return true;
}

std::optional<std::string> loadKey(const std::string &path)
{
    if (path.empty()) {
        return std::nullopt;
    }
    std::ifstream in(toPath(path), std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::stringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

} // namespace scorbit_godot
