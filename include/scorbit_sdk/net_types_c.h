/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include <stddef.h>
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

/**
 * Signer callback function type.
 *
 * Sign digest using given key and store the result in signature, the length of signature store in
 * signature_len. Key and digest are fixed length long byte arrays. The user_data is the data passed
 * to the signer. It can be used to pass the context to the signer or NULL if not used.
 *
 * @param signature The buffer to store the signature.
 * @param signature_len The length of the signature.
 * @param digest The digest to sign.
 * @param key The key to sign the digest with.
 * @param user_data The user data passed to the signer.
 *
 * @return true if the signing was successful, false otherwise.
 */
typedef bool (*sb_signer_callback_t)(uint8_t signature[SB_SIGNATURE_MAX_LENGTH],
                                     size_t *signature_len, const uint8_t digest[SB_DIGEST_LENGTH],
                                     const uint8_t key[SB_KEY_LENGTH], void *user_data);

#ifdef __cplusplus
}
#endif
