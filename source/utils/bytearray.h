/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Jun 2020
 *
 ****************************************************************************/

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
    ByteArray();
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
