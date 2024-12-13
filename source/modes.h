/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include <boost/flyweight.hpp>
#include <vector>
#include <string>
#include <string_view>

namespace scorbit {
namespace detail {

class Modes
{
public:
    Modes() = default;

    void addMode(std::string mode);
    void removeMode(std::string_view mode);
    void clear();
    bool isEmpty() const;
    bool contains(std::string_view mode) const;

    std::string str() const;

private:
    std::vector<boost::flyweight<std::string>> m_modes;

    // Declare friend for operator==
    friend bool operator==(const Modes &, const Modes &);
};

inline bool operator==(const scorbit::detail::Modes &lhs, const scorbit::detail::Modes &rhs)
{
    return lhs.m_modes == rhs.m_modes;
}

} // namespace detail
} // namespace scorbit
