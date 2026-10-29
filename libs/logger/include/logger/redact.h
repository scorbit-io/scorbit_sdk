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

#include <cstddef>
#include <string>
#include <string_view>

namespace logger {

// JSON keys whose string values never reach a log line, at any level (SB-6054). The pairing
// short code arrives in whole API replies that the SDK logs.
inline constexpr std::string_view kRedactedKeys[] = {"shortcode"};
inline constexpr std::string_view kRedactedValue = "<redacted>";

/**
 * Replaces the string value of each key in @ref kRedactedKeys, in plain JSON and in JSON
 * escaped inside another string. Anything else is left as it is.
 */
inline std::string redact(std::string message)
{
    auto skipBlanks = [&message](size_t i) {
        while (i < message.size() && (message[i] == ' ' || message[i] == '\t')) {
            ++i;
        }
        return i;
    };
    // A quote, optionally escaped; returns the index after it, or npos.
    auto quote = [&message](size_t i) {
        if (i < message.size() && message[i] == '\\') {
            ++i;
        }
        return i < message.size() && message[i] == '"' ? i + 1 : std::string::npos;
    };

    for (const auto key : kRedactedKeys) {
        size_t pos = 0;
        while ((pos = message.find(key, pos)) != std::string::npos) {
            pos += key.size();
            size_t i = quote(pos);
            if (i == std::string::npos) {
                continue;
            }
            i = skipBlanks(i);
            if (i >= message.size() || message[i] != ':') {
                continue;
            }
            const size_t start = quote(skipBlanks(i + 1));
            if (start == std::string::npos) {
                continue;
            }
            size_t end = start;
            while (end < message.size() && message[end] != '"' && message[end] != '\\') {
                ++end;
            }
            message.replace(start, end - start, kRedactedValue);
            pos = start + kRedactedValue.size();
        }
    }
    return message;
}

} // namespace logger
