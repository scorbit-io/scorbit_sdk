/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Jun 2020
 *
 ****************************************************************************/

#include "bytearray.h"

#include <fmt/format.h>
#include <algorithm>

namespace scorbit {
namespace detail {

ByteArray::ByteArray(ByteArrayBase::size_type count, const uint8_t defaultValue)
    : ByteArrayBase(count, defaultValue)
{
}

ByteArray::ByteArray(std::initializer_list<uint8_t> initList)
    : ByteArrayBase(initList)
{
}

ByteArray::ByteArray(const ByteArray &other)
    : ByteArrayBase(other)
{
}

ByteArray::ByteArray(ByteArray &&other)
    : ByteArrayBase(std::move(other))
{
}

ByteArray::ByteArray(const ByteArrayBase &other)
    : ByteArrayBase(other)
{
}

ByteArray::ByteArray(ByteArrayBase &&other)
    : ByteArrayBase(std::move(other))
{
}

ByteArray &ByteArray::operator=(const ByteArray &other)
{
    ByteArrayBase::operator=(other);
    return *this;
}

ByteArray &ByteArray::operator=(ByteArray &&other)
{
    ByteArrayBase::operator=(std::move(other));
    return *this;
}

ByteArray::ByteArray(const uint8_t array[], size_t length)
{
    resize(length);
    auto it = &array[0];
    for (auto &a : *this) {
        a = *it++;
    }
}

ByteArray::ByteArray(std::string hexArray)
{
    hexArray.erase(std::remove_if(hexArray.begin(), hexArray.end(),
                                  [](unsigned char c) { return std::isspace(c); }),
                   hexArray.end());

    if (hexArray.length() % 2 == 1)
        return;

    try {
        size_t length = hexArray.length();
        for (size_t i = 0; i < length; i += 2) {
            uint8_t a = static_cast<uint8_t>(std::stoi(hexArray.substr(i, 2), nullptr, 16));
            push_back(a);
        }
    } catch (...) {
        clear();
    }
}

ByteArray::ByteArray(const std::string::const_iterator &cBegin,
                     const std::string::const_iterator &cEnd)
{
    auto it = cBegin;
    while (it != cEnd) {
        push_back(static_cast<uint8_t>(*it++));
    }
}

std::string ByteArray::hex(const std::string &separator, const std::string &prefix) const
{
    std::string result;
    bool isSeparate = false;
    for (auto a : *this) {
        if (isSeparate) {
            result += fmt::format("{}{}{:02X}", separator, prefix, a);
        } else {
            isSeparate = true;
            result += fmt::format("{}{:02X}", prefix, a);
        }
    }

    return result;
}

std::string ByteArray::string() const
{
    std::string result;
    for (auto a : *this) {
        result += static_cast<char>(a);
    }
    return result;
}

void ByteArray::initialize(const ByteArray &initArray)
{
    if (initArray.empty())
        return;

    auto it = initArray.cbegin();
    for (auto &a : *this) {
        if (it == initArray.cend()) {
            it = initArray.cbegin();
        }
        a ^= *it++;
    }
}

} // namespace detail
} // namespace scorbit
