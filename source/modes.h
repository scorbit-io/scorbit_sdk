/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include "change_tracker.h"
#include <vector>
#include <string>
#include <string_view>

namespace scorbit {
namespace detail {

class Modes
{
public:
    Modes(ChangeTracker &tracker);

    void addMode(std::string mode);
    void removeMode(std::string_view mode);
    void clearModes();

    std::string str() const;

private:
    std::vector<std::string> m_modes;
    ChangeTracker &m_tracker;

    // Declare friend for operator==
    friend bool operator==(const Modes &, const Modes &);
};

inline bool operator==(const scorbit::detail::Modes &lhs, const scorbit::detail::Modes &rhs)
{
    return lhs.m_modes == rhs.m_modes;
}

} // namespace detail
} // namespace scorbit
