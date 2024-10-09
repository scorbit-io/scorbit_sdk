/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include "modes.h"
#include "logger.h"
#include <algorithm>
#include <numeric>

namespace scorbit {
namespace detail {

using namespace std;

Modes::Modes(ChangeTracker &tracker)
    : m_tracker(tracker)
{
}

void Modes::addMode(std::string mode)
{
    // TODO check if mode is valid

    if (find(begin(m_modes), end(m_modes), mode) != end(m_modes)) {
        DBG("Skipping addition: mode '{}' already exists.", mode);
        return;
    }

    m_modes.emplace_back(std::move(mode));
    m_tracker.setChanged();
}

void Modes::removeMode(std::string_view mode)
{
    const auto oldSize = m_modes.size();
    m_modes.erase(remove(begin(m_modes), end(m_modes), mode), end(m_modes));

    if (oldSize != m_modes.size()) {
        m_tracker.setChanged();
    } else {
        DBG("Skipping removal of mode '{}': not found.", mode);
    }
}

void Modes::clearModes()
{
    if (m_modes.empty())
        return;

    m_modes.clear();
    m_tracker.setChanged();
}

string Modes::str() const
{
    if (m_modes.empty())
        return string {};

    return accumulate(next(begin(m_modes)), end(m_modes), m_modes.front(),
                      [](std::string a, const std::string &b) { return std::move(a) + ';' + b; });
}

} // namespace detail
} // namespace scorbit
