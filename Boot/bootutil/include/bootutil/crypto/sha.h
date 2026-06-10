/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BOOTUTIL_CRYPTO_SHA_H_
#define __BOOTUTIL_CRYPTO_SHA_H_

#include "mcuboot_config.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef IMAGE_HASH_SIZE
#define IMAGE_HASH_SIZE (32)
#endif
#define EXPECTED_HASH_TLV IMAGE_TLV_SHA256

#define BOOTUTIL_CRYPTO_SHA256_BLOCK_SIZE  (64)
#define BOOTUTIL_CRYPTO_SHA256_DIGEST_SIZE (32)

#if defined(MCUBOOT_SHA256_BACKEND_STM32_HASH)

typedef struct {
    uint32_t tail_word;
    uint32_t tail_len;
    uint8_t accumulated;
    uint8_t initialized;
} bootutil_sha_context;

int bootutil_sha_init(bootutil_sha_context *ctx);
int bootutil_sha_drop(bootutil_sha_context *ctx);
int bootutil_sha_update(bootutil_sha_context *ctx,
                        const void *data,
                        uint32_t data_len);
int bootutil_sha_finish(bootutil_sha_context *ctx,
                        uint8_t *output);

#elif defined(MCUBOOT_USE_TINYCRYPT)

#include <tinycrypt/constants.h>
#include <tinycrypt/sha256.h>

typedef struct tc_sha256_state_struct bootutil_sha_context;

static inline int bootutil_sha_init(bootutil_sha_context *ctx)
{
    tc_sha256_init(ctx);
    return 0;
}

static inline int bootutil_sha_drop(bootutil_sha_context *ctx)
{
    (void)ctx;
    return 0;
}

static inline int bootutil_sha_update(bootutil_sha_context *ctx,
                                      const void *data,
                                      uint32_t data_len)
{
    int rc;

    rc = tc_sha256_update(ctx, data, data_len);
    return rc == TC_CRYPTO_SUCCESS ? 0 : -1;
}

static inline int bootutil_sha_finish(bootutil_sha_context *ctx,
                                      uint8_t *output)
{
    int rc;

    rc = tc_sha256_final(output, ctx);
    return rc == TC_CRYPTO_SUCCESS ? 0 : -1;
}

#else
#error "No SHA-256 backend selected"
#endif

#ifdef __cplusplus
}
#endif

#endif /* __BOOTUTIL_CRYPTO_SHA_H_ */
