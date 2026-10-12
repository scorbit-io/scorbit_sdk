/****************************************************************************
 *
 * Sep 2026
 *
 ****************************************************************************/

#pragma once

#include "tpm/tpm.h"

#include <functional>
#include <string>

namespace tpm_locate {

/// Finds the CDC node the TPM answers on now (empty if there is none), or reports the tap pad busy.
using UsbLocator = std::function<TpmLocation()>;

/**
 * @brief The CDC node to retry on after discovery failed on @p tried.
 *
 * Empty when there is no locator, it finds nothing, it points back at the
 * node that just failed, or it could not look because the tap pad was busy
 * (then @p busy is set), so a retry is only made where the chip has moved.
 */
inline std::string movedPath(const std::string &tried, const UsbLocator &locate,
                             bool *busy = nullptr)
{
    if (busy) {
        *busy = false;
    }
    if (!locate) {
        return {};
    }
    const auto located = locate();
    if (located.busy) {
        if (busy) {
            *busy = true;
        }
        return {};
    }
    return located.path == tried ? std::string {} : located.path;
}

} // namespace tpm_locate
