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

#include <filesystem>
#include <string>

namespace scorbit {
namespace detail {

/** Writes @p content to @p file and flushes it to disk; false if any step fails. */
bool writeDurably(const std::filesystem::path &file, const std::string &content);

/** Flushes @p dir's entries to disk: a rename is only durable once its directory is. */
bool syncDirectory(const std::filesystem::path &dir);

/** Flushes a file written elsewhere, such as a download, to disk. */
bool syncFile(const std::filesystem::path &file);

/**
 * Replaces @p file with @p content so that it survives a power cut whole or not at all: written
 * next to it, flushed, renamed into place, then its directory flushed. Creates the directory.
 */
bool replaceDurably(const std::filesystem::path &file, const std::string &content);

} // namespace detail
} // namespace scorbit
