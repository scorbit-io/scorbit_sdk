/****************************************************************************
 *
 * Sep 2026
 *
 ****************************************************************************/

#pragma once

#include <utils/bytearray.h>

#include <cstdint>

namespace tpm_identity {

/**
 * @brief Whether a chip that opened is the one this device was provisioned with.
 *
 * A reused ttyACM node or a fallback discovery can open another TPM, such as
 * the NFC probe's (SB-4407). An unknown expected identity matches nothing.
 */
inline bool isSameChip(uint64_t expectedSerial, const utils::ByteArray &expectedUuid,
                       uint64_t serial, const utils::ByteArray &uuid)
{
    return !expectedUuid.empty() && serial == expectedSerial && uuid == expectedUuid;
}

} // namespace tpm_identity
