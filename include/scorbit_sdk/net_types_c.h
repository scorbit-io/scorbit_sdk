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

// IMPORTANT: always add new error codes at the end of the list and sync with scorbit::Error
typedef enum {
    SB_EC_SUCCESS = 0,     // Success
    SB_EC_UNKNOWN = 1,     // Unknown error
    SB_EC_AUTH_FAILED = 2, // Authentication failed
    SB_EC_NOT_PAIRED = 3,  // Device is not paired
    SB_EC_API_ERROR = 4,   // API call error (e.g., HTTP error code != 200)
} sb_error_t;

typedef enum {
    SB_NET_NOT_AUTHENTICATED = 0,              // NotAuthenticated,
    SB_NET_AUTHENTICATING = 1,                 // Authenticating,
    SB_NET_AUTHENTICATED_CHECKING_PAIRING = 2, // AuthenticatedCheckingPairing,
    SB_NET_AUTHENTICATED_UNPAIRED = 3,         // AuthenticatedUnpaired,
    SB_NET_AUTHENTICATED_PAIRED = 4,           // AuthenticatedPaired,
    SB_NET_AUTHENTICATION_FAILED = 5,          // AuthenticationFailed,
} sb_auth_status_t;

typedef struct {
    /** Mandatory. The provider name, e.g., "scorbitron", "vpin". */
    const char *provider;

    /** Mandatory for manufacturers. The Machine ID assigned by Scorbit, ex. 4419. */
    int32_t machine_id;

    /** Mandatory. The game code version, e.g., "1.12.3". */
    const char *game_code_version;

    /**
     * Optional. The hostname of the server to connect to.
     * If set to `NULL`, the default `"production"` hostname will be used. The two standard options
     * are `"production"` and `"staging"`, each mapped to pre-defined URLs. To use a custom URL,
     * provide it in the format `"http[s]://<url>[:port]"`.
     *
     * Examples: `"production"`, `"staging"`, `"https://api.scorbit.io"`,
     * `"https://staging.scorbit.io"`, `"http://localhost:8080"`
     */
    const char *hostname;

    /**
     * Optional. The device's UUID.
     * If set to `NULL`, the UUID will be derived from the device's MAC address.
     *
     * Examples: `"f0b188f8-9f2d-4f8d-abe4-c3107516e7ce"`, `"f0b188f89f2d4f8dabe4c3107516e7ce"`,
     * `"F0B188F8-9F2D-4F8D-ABE4-C3107516E7CE"`, `"F0B188F89F2D4F8DABE4C3107516E7CE"`
     */
    const char *uuid;

    /** Optional. The serial number of the device. Set to 0 if unavailable. */
    uint64_t serial_number;

} sb_device_info_t;

/**
 * Signer callback function type.
 *
 * Sign digest using given key and store the result in signature, the length of signature store in
 * signature_len. Key and digest are fixed length long byte arrays. The user_data is the data passed
 * to the signer. It can be used to pass the context to the signer or NULL if not used.
 *
 * - **signature**: The buffer to store the signature.
 * - **signature_len**: The length of the signature.
 * - **digest**: The digest to sign.
 * - **user_data**: The user data passed to the signer.
 *
 * - returns 0 if the signing was successful
 */
typedef int (*sb_signer_callback_t)(uint8_t signature[SB_SIGNATURE_MAX_LENGTH],
                                    size_t *signature_len, const uint8_t digest[SB_DIGEST_LENGTH],
                                    void *user_data);

typedef void (*sb_string_callback_t)(sb_error_t error, const char *reply, void *user_data);

#ifdef __cplusplus
}
#endif
