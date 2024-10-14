/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#include "scorbit_crypt.h"

#include <openssl/sha.h>

#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

// To supresss OpenSSL 1.0.x API's deprecation warnings
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

/*****************************************
 * scorbit_sign
 *****************************************/
SignErrorCode scorbit_sign(uint8_t signature[SB_SIGNATURE_MAX_LENGTH], size_t *signature_len,
                           const uint8_t digest[SB_DIGEST_LENGTH], const uint8_t key[SB_KEY_LENGTH])
{
    SignErrorCode ret_code = SIGN_OK;
    EC_KEY *ec_key = NULL;
    BIGNUM *priv_key_bn = NULL;
    ECDSA_SIG *sig = NULL;
    EC_POINT *ec_point = NULL;

    // Initialize OpenSSL (Optional, depending on your setup)
    if (OpenSSL_add_all_algorithms() == 0) {
        // GCOV_EXCL_START
        ret_code = SIGN_ERR_OPENSSL_INIT;
        goto cleanup;
        // GCOV_EXCL_STOP
    }

    // Create a new EC_KEY object for the curve using prime256v1 (secp256r1)
    ec_key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (ec_key == NULL) {
        // GCOV_EXCL_START
        ret_code = SIGN_ERR_CREATE_EC;
        goto cleanup;
        // GCOV_EXCL_STOP
    }

    // Convert the key into a BIGNUM
    priv_key_bn = BN_bin2bn(key, SB_KEY_LENGTH, NULL);
    if (priv_key_bn == NULL) {
        // GCOV_EXCL_START
        ret_code = SIGN_ERR_CREATE_BIGNUM;
        goto cleanup;
        // GCOV_EXCL_STOP
    }

    // Set the private key into the EC_KEY object
    if (EC_KEY_set_private_key(ec_key, priv_key_bn) != 1) {
        // GCOV_EXCL_START
        ret_code = SIGN_ERR_SET_PRIV_KEY;
        goto cleanup;
        // GCOV_EXCL_STOP
    }

    // Compute the public key from the private key
    ec_point = EC_POINT_new(EC_KEY_get0_group(ec_key));
    if (ec_point == NULL) {
        // GCOV_EXCL_START
        ret_code = SIGN_ERR_CREATE_POINT;
        goto cleanup;
        // GCOV_EXCL_STOP
    }

    if (EC_POINT_mul(EC_KEY_get0_group(ec_key), ec_point, priv_key_bn, NULL, NULL, NULL) != 1) {
        // GCOV_EXCL_START
        ret_code = SIGN_ERR_CALC_PUB_KEY;
        goto cleanup;
        // GCOV_EXCL_STOP
    }

    // Set public key
    if (EC_KEY_set_public_key(ec_key, ec_point) != 1) {
        // GCOV_EXCL_START
        ret_code = SIGN_ERR_SET_PUB_KEY;
        goto cleanup;
        // GCOV_EXCL_STOP
    }

    // Check if given key is valid
    if (EC_KEY_check_key(ec_key) != 1) {
        // GCOV_EXCL_START
        ret_code = SIGN_ERR_INVALID_KEY;
        goto cleanup;
        // GCOV_EXCL_STOP
    }

    // Check if given can sign
    if (EC_KEY_can_sign(ec_key) != 1) {
        // GCOV_EXCL_START
        ret_code = SIGN_ERR_KEY_CANNOT_SIGN;
        goto cleanup;
        // GCOV_EXCL_STOP
    }

    // Sign the digest
    sig = ECDSA_do_sign(digest, SB_DIGEST_LENGTH, ec_key);
    if (sig == NULL) {
        // GCOV_EXCL_START
        ret_code = SIGN_ERR_SIGN_FAILURE;
        goto cleanup;
        // GCOV_EXCL_STOP
    }

    // Convert the signature into a byte array
    int len = i2d_ECDSA_SIG(sig, &signature);
    if (len < 0) {
        // GCOV_EXCL_START
        ret_code = SIGN_ERR_INVALID_SIGNATURE;
        goto cleanup;
        // GCOV_EXCL_STOP
    }

    *signature_len = (size_t)len;

cleanup:
    // Cleanup
    if (priv_key_bn) {
        BN_free(priv_key_bn);
    }

    if (ec_key) {
        EC_KEY_free(ec_key);
    }

    if (sig) {
        ECDSA_SIG_free(sig);
    }

    if (ec_point) {
        EC_POINT_free(ec_point);
    }

    return ret_code;
}
