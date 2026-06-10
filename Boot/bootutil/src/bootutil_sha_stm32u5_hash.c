/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bootutil/crypto/sha.h"

#if defined(MCUBOOT_SHA256_BACKEND_STM32_HASH)

#include "hash.h"

#include <stdint.h>
#include <string.h>

#define BOOTUTIL_SHA_HW_TIMEOUT_MS 1000U

static uint8_t *bootutil_sha_tail_bytes(bootutil_sha_context *ctx)
{
    return (uint8_t *)&ctx->tail_word;
}

static const uint8_t *bootutil_sha_const_tail_bytes(const bootutil_sha_context *ctx)
{
    return (const uint8_t *)&ctx->tail_word;
}

static int bootutil_sha_hw_accumulate(bootutil_sha_context *ctx,
                                      const uint8_t *data,
                                      uint32_t data_len)
{
    uint32_t off;

    if (data_len == 0U) {
        return 0;
    }

    if ((data == NULL) || ((data_len % 4U) != 0U)) {
        return -1;
    }

    if ((((uintptr_t)data) & 3U) == 0U) {
        if (HAL_HASHEx_SHA256_Accmlt(&hhash, data, data_len) != HAL_OK) {
            return -1;
        }
        ctx->accumulated = 1U;
        return 0;
    }

    for (off = 0U; off < data_len; off += 4U) {
        ctx->tail_word = 0U;
        memcpy(bootutil_sha_tail_bytes(ctx), &data[off], 4U);
        if (HAL_HASHEx_SHA256_Accmlt(&hhash,
                                     bootutil_sha_tail_bytes(ctx),
                                     4U) != HAL_OK) {
            return -1;
        }
    }

    ctx->tail_word = 0U;
    ctx->accumulated = 1U;
    return 0;
}

int bootutil_sha_init(bootutil_sha_context *ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));

    if (HAL_HASH_Init(&hhash) != HAL_OK) {
        return -1;
    }

    ctx->initialized = 1U;
    return 0;
}

int bootutil_sha_drop(bootutil_sha_context *ctx)
{
    if (ctx == NULL) {
        return 0;
    }

    if (ctx->initialized != 0U) {
        (void)HAL_HASH_DeInit(&hhash);
    }

    memset(ctx, 0, sizeof(*ctx));
    return 0;
}

int bootutil_sha_update(bootutil_sha_context *ctx,
                        const void *data,
                        uint32_t data_len)
{
    const uint8_t *p;
    uint32_t remaining;
    uint32_t fill_len;
    uint32_t aligned_len;

    if ((ctx == NULL) || (ctx->initialized == 0U)) {
        return -1;
    }

    if (data_len == 0U) {
        return 0;
    }

    if (data == NULL) {
        return -1;
    }

    p = (const uint8_t *)data;
    remaining = data_len;

    if (ctx->tail_len != 0U) {
        fill_len = 4U - ctx->tail_len;
        if (fill_len > remaining) {
            fill_len = remaining;
        }

        memcpy(&bootutil_sha_tail_bytes(ctx)[ctx->tail_len], p, fill_len);
        ctx->tail_len += fill_len;
        p += fill_len;
        remaining -= fill_len;

        if (ctx->tail_len == 4U) {
            if (bootutil_sha_hw_accumulate(ctx, bootutil_sha_tail_bytes(ctx), 4U) != 0) {
                return -1;
            }
            ctx->tail_word = 0U;
            ctx->tail_len = 0U;
        }
    }

    aligned_len = remaining - (remaining % 4U);
    if (aligned_len != 0U) {
        if (bootutil_sha_hw_accumulate(ctx, p, aligned_len) != 0) {
            return -1;
        }
        p += aligned_len;
        remaining -= aligned_len;
    }

    if (remaining != 0U) {
        ctx->tail_word = 0U;
        memcpy(bootutil_sha_tail_bytes(ctx), p, remaining);
        ctx->tail_len = remaining;
    }

    return 0;
}

int bootutil_sha_finish(bootutil_sha_context *ctx,
                        uint8_t *output)
{
    HAL_StatusTypeDef status;
    uint32_t digest_words[BOOTUTIL_CRYPTO_SHA256_DIGEST_SIZE / sizeof(uint32_t)];
    uint8_t *digest;
    const uint8_t *final_data;
    uint32_t final_len;

    if ((ctx == NULL) || (output == NULL) || (ctx->initialized == 0U)) {
        return -1;
    }

    final_data = bootutil_sha_const_tail_bytes(ctx);
    final_len = ctx->tail_len;
    digest = (uint8_t *)digest_words;

    if (ctx->accumulated != 0U) {
        status = HAL_HASHEx_SHA256_Accmlt_End(&hhash,
                                              final_data,
                                              final_len,
                                              digest,
                                              BOOTUTIL_SHA_HW_TIMEOUT_MS);
    } else {
        status = HAL_HASHEx_SHA256_Start(&hhash,
                                         final_data,
                                         final_len,
                                         digest,
                                         BOOTUTIL_SHA_HW_TIMEOUT_MS);
    }

    if (status != HAL_OK) {
        return -1;
    }

    memcpy(output, digest, BOOTUTIL_CRYPTO_SHA256_DIGEST_SIZE);
    return 0;
}

#endif /* MCUBOOT_SHA256_BACKEND_STM32_HASH */
