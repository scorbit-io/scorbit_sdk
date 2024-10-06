/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include <vector>
#include <string>
#include <string_view>

namespace scorbit {
namespace detail {

class Modes
{
public:
    Modes() = default;

    bool isChanged() const { return m_isChanged; }
    void clearChanged() { m_isChanged = false; }

    void addMode(std::string mode);
    void removeMode(std::string_view mode);
    void clearModes();

    std::string str() const;

private:
    void setChanged() { m_isChanged = true; }

private:
    std::vector<std::string> m_modes;
    bool m_isChanged {false};
};

} // namespace detail
} // namespace scorbit
