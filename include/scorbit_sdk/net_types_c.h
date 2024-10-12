/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SB_DIGEST_LENGTH = 32,
    SB_UUID_LENGTH = 16,
    SB_SIGNATURE_MAX_LENGTH = 72, // ECDSA signature length for P-256
    SB_KEY_LENGTH = 32,           // ECDSA key length for P-256
};

typedef bool (*sb_signer_callback_t)(uint8_t signature[SB_SIGNATURE_MAX_LENGTH], int *signature_len,
                                     const uint8_t digest[SB_DIGEST_LENGTH],
                                     const uint8_t key[SB_KEY_LENGTH], void *user_data);

#ifdef __cplusplus
}
#endif
