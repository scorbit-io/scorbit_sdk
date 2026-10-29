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

#include <boost/predef.h>
#include <cstddef>
#include <cstdint>
#include <string>

constexpr std::uint16_t TEST_ELF_MACHINE = 183; // EM_AARCH64

// Writes @p value as a little endian field of @p size bytes at @p offset
inline void putLittleEndian(std::string &bytes, std::size_t offset, std::uint64_t value,
                            std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i) {
        bytes[offset + i] = static_cast<char>((value >> (8 * i)) & 0xff);
    }
}

// A minimal little endian shared object: the ELF header, one program header that loads the whole
// file, then @p payload
inline std::string elfBytes(const std::string &payload, std::uint16_t machine = TEST_ELF_MACHINE,
                            bool is64 = true)
{
    const std::size_t ehdrSize = is64 ? 64 : 52;
    const std::size_t phdrSize = is64 ? 56 : 32;
    const std::size_t addr = is64 ? 8 : 4;

    std::string bytes(ehdrSize + phdrSize, '\0');
    bytes[0] = '\x7f';
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = is64 ? 2 : 1;                                   // class
    bytes[5] = 1;                                              // little endian
    bytes[6] = 1;                                              // version
    putLittleEndian(bytes, 16, 3, 2);                          // e_type: ET_DYN
    putLittleEndian(bytes, 18, machine, 2);                    // e_machine
    putLittleEndian(bytes, 20, 1, 4);                          // e_version
    putLittleEndian(bytes, is64 ? 32 : 28, ehdrSize, addr);    // e_phoff
    putLittleEndian(bytes, is64 ? 52 : 40, ehdrSize, 2);       // e_ehsize
    putLittleEndian(bytes, is64 ? 54 : 42, phdrSize, 2);       // e_phentsize
    putLittleEndian(bytes, is64 ? 56 : 44, 1, 2);              // e_phnum
    putLittleEndian(bytes, is64 ? 58 : 46, is64 ? 64 : 40, 2); // e_shentsize

    const auto total = bytes.size() + payload.size();
    putLittleEndian(bytes, ehdrSize, 1, 4);                           // p_type: PT_LOAD
    putLittleEndian(bytes, ehdrSize + (is64 ? 8 : 4), 0, addr);       // p_offset
    putLittleEndian(bytes, ehdrSize + (is64 ? 32 : 16), total, addr); // p_filesz

    return bytes + payload;
}

// File contents that pass as an executable image of this platform's format, then @p payload
inline std::string executableBytes(const std::string &payload)
{
#if BOOST_OS_WINDOWS
    return std::string("MZ\x90\0", 4) + payload;
#elif BOOST_OS_MACOS
    return std::string("\xcf\xfa\xed\xfe", 4) + payload;
#else
    return elfBytes(payload);
#endif
}
