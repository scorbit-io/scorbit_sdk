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

#include <algorithm>
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
    constexpr auto npos = std::string::npos;
    const auto at = [&message](size_t i, char c) { return i < message.size() && message[i] == c; };
    // JSON whitespace; in JSON escaped inside a string, also the \n, \r and \t escapes.
    const auto skipBlanks = [&](size_t i, bool escaped) {
        while (i < message.size()) {
            const char c = message[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++i;
            } else if (escaped && c == '\\'
                       && (at(i + 1, 'n') || at(i + 1, 'r') || at(i + 1, 't'))) {
                i += 2;
            } else {
                break;
            }
        }
        return i;
    };
    // A quote, as \" in escaped JSON; returns the index after it, or npos.
    const auto quote = [&](size_t i, bool escaped) {
        if (escaped && !at(i++, '\\')) {
            return npos;
        }
        return at(i, '"') ? i + 1 : npos;
    };

    for (const auto key : kRedactedKeys) {
        size_t pos = 0;
        while ((pos = message.find(key, pos)) != npos) {
            const size_t keyStart = pos;
            pos += key.size();
            if (keyStart == 0 || message[keyStart - 1] != '"') {
                continue; // a longer key that merely ends in this one
            }
            const bool escaped = keyStart >= 2 && message[keyStart - 2] == '\\';
            size_t i = quote(pos, escaped);
            if (i == npos || !at(i = skipBlanks(i, escaped), ':')) {
                continue;
            }
            const size_t start = quote(skipBlanks(i + 1, escaped), escaped);
            if (start == npos) {
                continue;
            }
            // Up to the value's closing quote, stepping over the value's own escapes. Escaped
            // JSON is read one outer escape at a time (\\ is a backslash, \" a quote), so a
            // quote closes the value only when no unpaired backslash comes before it.
            size_t end = start;
            bool innerEscape = false;
            while (end < message.size()) {
                char c = message[end];
                size_t width = 1;
                if (escaped && c == '\\' && end + 1 < message.size()) {
                    const char next = message[end + 1];
                    c = (next == '\\' || next == '"') ? next : 'x'; // \n and the like: content
                    width = 2;
                }
                if (innerEscape) {
                    innerEscape = false;
                } else if (c == '\\') {
                    innerEscape = true;
                } else if (c == '"') {
                    break;
                }
                end += width;
            }
            end = std::min(end, message.size());
            message.replace(start, end - start, kRedactedValue);
            pos = start + kRedactedValue.size();
        }
    }
    return message;
}

} // namespace logger
