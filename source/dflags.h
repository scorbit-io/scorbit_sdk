/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Dec 2025
 *
 ****************************************************************************/

#pragma once

#include <type_traits>

// Usage examples:
/*
enum SomeFlag : uint32_t {
    Upload = 1u << 0,
    Debounce = 1u << 1,
    Compress = 1u << 2,
    Encrypt = 1u << 3
};

void example()
{
    Flags<SomeFlag> f;

    if (f.empty()) {
        // Flags are empty
    }

    f = SomeFlag::Upload | SomeFlag::Debounce;

    if (f.has(SomeFlag::Upload)) {
        // Has Upload flag
    }

    if (f.has(SomeFlag::Debounce)) {
        // Has Debounce flag
    }

    // Additional operations:
    f.set(SomeFlag::Compress);
    f.clear(SomeFlag::Upload);
    f.toggle(SomeFlag::Encrypt);

    Flags<SomeFlag> f2 = SomeFlag::Upload;
    Flags<SomeFlag> f3 = f | f2;

    if (f) {
        // f is not empty
    }
}
*/

// Helper to get underlying type of enum
template<typename E>
using underlying_type_t = typename std::underlying_type<E>::type;

// SFINAE helper to enable only for enum types
template<typename E>
using enable_if_enum_t = typename std::enable_if<std::is_enum<E>::value, E>::type;

// DFlags template class
template<typename E>
class DFlags
{
    static_assert(std::is_enum<E>::value, "Template parameter must be an enum type");

    using underlying_t = underlying_type_t<E>;
    underlying_t value_;

public:
    // Default constructor - creates empty flags
    constexpr DFlags() noexcept
        : value_(0)
    {
    }

    // Constructor from enum value
    constexpr DFlags(E flag) noexcept
        : value_(static_cast<underlying_t>(flag))
    {
    }

    // Constructor from underlying type (for OR operations result)
    constexpr explicit DFlags(underlying_t val) noexcept
        : value_(val)
    {
    }

    // Check if flags are empty
    constexpr bool empty() const noexcept { return value_ == 0; }

    // Check if specific flag is set
    constexpr bool has(E flag) const noexcept
    {
        underlying_t f = static_cast<underlying_t>(flag);
        return (value_ & f) == f;
    }

    // Set a flag
    constexpr DFlags &set(E flag) noexcept
    {
        value_ |= static_cast<underlying_t>(flag);
        return *this;
    }

    // Clear a flag
    constexpr DFlags &clear(E flag) noexcept
    {
        value_ &= ~static_cast<underlying_t>(flag);
        return *this;
    }

    // Toggle a flag
    constexpr DFlags &toggle(E flag) noexcept
    {
        value_ ^= static_cast<underlying_t>(flag);
        return *this;
    }

    // Clear all flags
    constexpr void reset() noexcept { value_ = 0; }

    // Get underlying value
    constexpr underlying_t value() const noexcept { return value_; }

    // Assignment from enum
    constexpr DFlags &operator=(E flag) noexcept
    {
        value_ = static_cast<underlying_t>(flag);
        return *this;
    }

    // Bitwise OR with another DFlags
    constexpr DFlags operator|(const DFlags &other) const noexcept
    {
        return DFlags(value_ | other.value_);
    }

    // Bitwise OR with enum
    constexpr DFlags operator|(E flag) const noexcept
    {
        return DFlags(value_ | static_cast<underlying_t>(flag));
    }

    // Bitwise AND
    constexpr DFlags operator&(const DFlags &other) const noexcept
    {
        return DFlags(value_ & other.value_);
    }

    constexpr DFlags operator&(E flag) const noexcept
    {
        return DFlags(value_ & static_cast<underlying_t>(flag));
    }

    // Compound assignment operators
    constexpr DFlags &operator|=(const DFlags &other) noexcept
    {
        value_ |= other.value_;
        return *this;
    }

    constexpr DFlags &operator|=(E flag) noexcept
    {
        value_ |= static_cast<underlying_t>(flag);
        return *this;
    }

    constexpr DFlags &operator&=(const DFlags &other) noexcept
    {
        value_ &= other.value_;
        return *this;
    }

    constexpr DFlags &operator&=(E flag) noexcept
    {
        value_ &= static_cast<underlying_t>(flag);
        return *this;
    }

    // Comparison operators
    constexpr bool operator==(const DFlags &other) const noexcept { return value_ == other.value_; }

    constexpr bool operator!=(const DFlags &other) const noexcept { return value_ != other.value_; }

    // Boolean conversion for if statements
    constexpr explicit operator bool() const noexcept { return value_ != 0; }
};

// Free function to enable enum | enum operations
// template<typename E, typename = enable_if_enum_t<E>>
// constexpr DFlags<E> operator|(E lhs, E rhs) noexcept
// {
//     return DFlags<E>(lhs) | rhs;
// }
