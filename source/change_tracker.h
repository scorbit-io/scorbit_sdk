/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

namespace scorbit {
namespace detail {

class ChangeTracker
{
public:
    bool isChanged() const { return m_isChanged; }
    void clearChanged() { m_isChanged = false; }

protected:
    void setChanged() { m_isChanged = true; }

private:
    bool m_isChanged {false};
};

} // namespace detail
} // namespace scorbit
