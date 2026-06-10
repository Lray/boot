/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BOOTUTIL_CRYPTO_ECDSA_H_
#define __BOOTUTIL_CRYPTO_ECDSA_H_

#include <stdint.h>
#include <stddef.h>

#include "mcuboot_config.h"

#if !defined(MCUBOOT_SIGN_EC256)
#error "ECDSA backend requires MCUBOOT_SIGN_EC256"
#endif

#define BOOTUTIL_CRYPTO_ECDSA_P256_BYTES     (32U)
#define BOOTUTIL_CRYPTO_ECDSA_P256_HASH_SIZE (32U)
#define BOOTUTIL_CRYPTO_ECDSA_P256_SIG_SIZE  (64U)

typedef struct {
    uint8_t initialized;
} bootutil_ecdsa_context;

#ifdef __cplusplus
extern "C" {
#endif

void bootutil_ecdsa_init(bootutil_ecdsa_context *ctx);
void bootutil_ecdsa_drop(bootutil_ecdsa_context *ctx);

int bootutil_ecdsa_parse_public_key(bootutil_ecdsa_context *ctx,
                                    uint8_t **cp, uint8_t *end);

int bootutil_ecdsa_verify(bootutil_ecdsa_context *ctx,
                          uint8_t *pk, size_t pk_len,
                          uint8_t *hash, size_t hash_len,
                          uint8_t *sig, size_t sig_len);

#ifdef __cplusplus
}
#endif

#endif /* __BOOTUTIL_CRYPTO_ECDSA_H_ */
