/****************************************************************************
 *
 * Sep 2026
 *
 ****************************************************************************/

#pragma once

#include <functional>
#include <string>

namespace tpm_locate {

/// Finds the CDC node the TPM answers on now, or empty if there is none.
using UsbLocator = std::function<std::string()>;

/**
 * @brief The CDC node to retry on after discovery failed on @p tried.
 *
 * Empty when there is no locator, it finds nothing, or it points back at the
 * node that just failed, so a retry is only made where the chip has moved.
 */
inline std::string movedPath(const std::string &tried, const UsbLocator &locate)
{
    if (!locate) {
        return {};
    }
    auto located = locate();
    return located == tried ? std::string {} : located;
}

} // namespace tpm_locate
