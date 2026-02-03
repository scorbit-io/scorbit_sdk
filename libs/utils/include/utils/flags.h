/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2025
 *
 ****************************************************************************/

#pragma once

#include <type_traits>

namespace utils {

template<typename EnumType, typename UnderlyingType = std::underlying_type_t<EnumType>>
class Flags
{
public:
    using enum_t = EnumType;
    using type_t = UnderlyingType;

    // Default constructor
    constexpr Flags() noexcept
        : m_flags(0)
    {
    }

    // Constructor from enum value
    constexpr Flags(enum_t flag) noexcept
        : m_flags(static_cast<type_t>(flag))
    {
    }

    // Delete constructor from raw integer type for type safety
    Flags(type_t) = delete;

    // Query methods
    constexpr bool hasFlag(enum_t flag) const noexcept
    {
        return (m_flags & static_cast<type_t>(flag)) != 0;
    }

    constexpr bool hasAnyFlag(const Flags &other) const noexcept
    {
        return (m_flags & other.m_flags) != 0;
    }

    constexpr bool hasAllFlags(const Flags &other) const noexcept
    {
        return (m_flags & other.m_flags) == other.m_flags;
    }

    constexpr bool isEmpty() const noexcept { return m_flags == 0; }

    // Modification methods
    constexpr void setFlag(enum_t flag) noexcept { m_flags |= static_cast<type_t>(flag); }

    constexpr void clearFlag(enum_t flag) noexcept { m_flags &= ~static_cast<type_t>(flag); }

    constexpr void toggleFlag(enum_t flag) noexcept { m_flags ^= static_cast<type_t>(flag); }

    constexpr void clear() noexcept { m_flags = 0; }

    // Get raw value
    constexpr type_t value() const noexcept { return m_flags; }

    // Bitwise operators with enum
    constexpr Flags operator|(enum_t flag) const noexcept
    {
        return Flags(m_flags | static_cast<type_t>(flag), tag {});
    }

    constexpr Flags operator&(enum_t flag) const noexcept
    {
        return Flags(m_flags & static_cast<type_t>(flag), tag {});
    }

    constexpr Flags operator^(enum_t flag) const noexcept
    {
        return Flags(m_flags ^ static_cast<type_t>(flag), tag {});
    }

    // Bitwise operators with Flags
    constexpr Flags operator|(const Flags &other) const noexcept
    {
        return Flags(m_flags | other.m_flags, tag {});
    }

    constexpr Flags operator&(const Flags &other) const noexcept
    {
        return Flags(m_flags & other.m_flags, tag {});
    }

    constexpr Flags operator^(const Flags &other) const noexcept
    {
        return Flags(m_flags ^ other.m_flags, tag {});
    }

    constexpr Flags operator~() const noexcept { return Flags(~m_flags, tag {}); }

    // Compound assignment operators
    constexpr Flags &operator|=(enum_t flag) noexcept
    {
        m_flags |= static_cast<type_t>(flag);
        return *this;
    }

    constexpr Flags &operator&=(enum_t flag) noexcept
    {
        m_flags &= static_cast<type_t>(flag);
        return *this;
    }

    constexpr Flags &operator^=(enum_t flag) noexcept
    {
        m_flags ^= static_cast<type_t>(flag);
        return *this;
    }

    constexpr Flags &operator|=(const Flags &other) noexcept
    {
        m_flags |= other.m_flags;
        return *this;
    }

    constexpr Flags &operator&=(const Flags &other) noexcept
    {
        m_flags &= other.m_flags;
        return *this;
    }

    constexpr Flags &operator^=(const Flags &other) noexcept
    {
        m_flags ^= other.m_flags;
        return *this;
    }

    // Comparison operators
    constexpr bool operator==(const Flags &other) const noexcept
    {
        return m_flags == other.m_flags;
    }

    constexpr bool operator!=(const Flags &other) const noexcept
    {
        return m_flags != other.m_flags;
    }

    // Explicit bool conversion
    constexpr explicit operator bool() const noexcept { return m_flags != 0; }

private:
    struct tag {
    };

    // Private constructor for internal use
    constexpr explicit Flags(type_t flags, tag) noexcept
        : m_flags(flags)
    {
    }

    type_t m_flags;

    template<typename E, typename U>
    friend constexpr Flags<E, U> operator|(E lhs, E rhs) noexcept;
};

// Free function for combining enum values
template<typename EnumType>
constexpr Flags<EnumType> operator|(EnumType lhs, EnumType rhs) noexcept
{
    using type_t = std::underlying_type_t<EnumType>;
    return Flags<EnumType>(static_cast<type_t>(lhs) | static_cast<type_t>(rhs),
                           typename Flags<EnumType>::tag {});
}

} // namespace utils
