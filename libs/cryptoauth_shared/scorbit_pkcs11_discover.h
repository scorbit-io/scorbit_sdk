/****************************************************************************
 * Scorbit PKCS#11 CDC TPM discovery (matches SDK CdcTpm::DiscoverTpmDevice).
 ****************************************************************************/

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Discover Scorbit TPM CDC device path. Returns 0 on success. */
int scorbit_pkcs11_discover_cdc(char *path_out, size_t path_out_len);

#ifdef __cplusplus
}
#endif
