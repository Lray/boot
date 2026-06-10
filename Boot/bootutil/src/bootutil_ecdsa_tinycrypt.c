/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stddef.h>

#include "mcuboot_config.h"
#include "bootutil/crypto/ecdsa.h"
#include "bootutil_ecdsa_asn1.h"

#if defined(MCUBOOT_ECDSA_BACKEND_TINYCRYPT)

#include <tinycrypt/constants.h>
#include <tinycrypt/ecc_dsa.h>

void bootutil_ecdsa_init(bootutil_ecdsa_context *ctx)
{
    if (ctx != NULL) {
        ctx->initialized = 1U;
    }
}

void bootutil_ecdsa_drop(bootutil_ecdsa_context *ctx)
{
    if (ctx != NULL) {
        ctx->initialized = 0U;
    }
}

int bootutil_ecdsa_parse_public_key(bootutil_ecdsa_context *ctx,
                                    uint8_t **cp, uint8_t *end)
{
    (void)ctx;

    return bootutil_ecdsa_asn1_parse_public_key(cp, end);
}

int bootutil_ecdsa_verify(bootutil_ecdsa_context *ctx,
                          uint8_t *pk, size_t pk_len,
                          uint8_t *hash, size_t hash_len,
                          uint8_t *sig, size_t sig_len)
{
    uint8_t signature[BOOTUTIL_CRYPTO_ECDSA_P256_SIG_SIZE];
    int rc;

    (void)ctx;

    if (pk == NULL ||
        pk_len != ((2U * BOOTUTIL_CRYPTO_ECDSA_P256_BYTES) + 1U) ||
        hash == NULL ||
        hash_len != BOOTUTIL_CRYPTO_ECDSA_P256_HASH_SIZE ||
        sig == NULL ||
        sig_len == 0U) {
        return -1;
    }

    rc = bootutil_ecdsa_asn1_decode_sig(signature, sig, sig_len);
    if (rc != 0) {
        return -1;
    }

    if (pk[0] != 0x04) {
        return -1;
    }
    pk++;

    rc = uECC_verify(pk, hash, BOOTUTIL_CRYPTO_ECDSA_P256_HASH_SIZE,
                     signature, uECC_secp256r1());
    return (rc == TC_CRYPTO_SUCCESS) ? 0 : -1;
}

#endif /* MCUBOOT_ECDSA_BACKEND_TINYCRYPT */
