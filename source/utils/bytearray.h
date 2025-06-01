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

#include <vector>
#include <cstdint>
#include <string>

namespace scorbit {
namespace detail {

using ByteArrayBase = std::vector<uint8_t>;
class ByteArray : public ByteArrayBase
{
public:
    ByteArray() = default;
    explicit ByteArray(size_type count, const uint8_t defaultValue = 0);
    ByteArray(std::initializer_list<uint8_t> init);
    ByteArray(const ByteArray &other);
    ByteArray(ByteArray &&other);
    ByteArray(const ByteArrayBase &other);
    ByteArray(ByteArrayBase &&other);
    ByteArray& operator=(const ByteArray &other);
    ByteArray& operator=(ByteArray &&other);

    explicit ByteArray(const uint8_t array[], size_t length);
    explicit ByteArray(std::string hexArray);
    explicit ByteArray(const std::string::const_iterator &cBegin,
                       const std::string::const_iterator &cEnd);

    std::string hex(const std::string &separator = "", const std::string &prefix="") const;
    std::string string() const;

    void initialize(const ByteArray &initArray);
};

} // namespace detail
} // namespace scorbit
