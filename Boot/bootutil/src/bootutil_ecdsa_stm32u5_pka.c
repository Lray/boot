/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stddef.h>

#include "mcuboot_config.h"
#include "bootutil/crypto/ecdsa.h"
#include "bootutil_ecdsa_asn1.h"

#if defined(MCUBOOT_ECDSA_BACKEND_STM32U5_PKA)

#include "stm32u5xx_hal.h"
#include "pka.h"
#include "iwdg.h"

#if !defined(HAL_PKA_MODULE_ENABLED)
#error "HAL_PKA_MODULE_ENABLED must be enabled before using STM32U5 PKA ECDSA backend"
#endif

#define BOOTUTIL_PKA_TIMEOUT_MS       (5000U)
#define BOOTUTIL_PKA_COEF_NEGATIVE   (1U)

static const uint8_t p256_p[BOOTUTIL_CRYPTO_ECDSA_P256_BYTES] = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static const uint8_t p256_a_abs[BOOTUTIL_CRYPTO_ECDSA_P256_BYTES] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
};

static const uint8_t p256_gx[BOOTUTIL_CRYPTO_ECDSA_P256_BYTES] = {
    0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47,
    0xf8, 0xbc, 0xe6, 0xe5, 0x63, 0xa4, 0x40, 0xf2,
    0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33, 0xa0,
    0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96,
};

static const uint8_t p256_gy[BOOTUTIL_CRYPTO_ECDSA_P256_BYTES] = {
    0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b,
    0x8e, 0xe7, 0xeb, 0x4a, 0x7c, 0x0f, 0x9e, 0x16,
    0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e, 0xce,
    0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5,
};

static const uint8_t p256_n[BOOTUTIL_CRYPTO_ECDSA_P256_BYTES] = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84,
    0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51,
};

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
    PKA_ECDSAVerifInTypeDef in = {0};
    HAL_StatusTypeDef hal_rc;

    (void)ctx;

    if (pk == NULL ||
        pk_len != ((2U * BOOTUTIL_CRYPTO_ECDSA_P256_BYTES) + 1U) ||
        hash == NULL ||
        hash_len != BOOTUTIL_CRYPTO_ECDSA_P256_HASH_SIZE ||
        sig == NULL ||
        sig_len == 0U) {
        return -1;
    }

    if (pk[0] != 0x04) {
        return -1;
    }

    if (bootutil_ecdsa_asn1_decode_sig(signature, sig, sig_len) != 0) {
        return -1;
    }

    in.primeOrderSize = BOOTUTIL_CRYPTO_ECDSA_P256_BYTES;
    in.modulusSize = BOOTUTIL_CRYPTO_ECDSA_P256_BYTES;
    in.coefSign = BOOTUTIL_PKA_COEF_NEGATIVE;
    in.coef = p256_a_abs;
    in.modulus = p256_p;
    in.basePointX = p256_gx;
    in.basePointY = p256_gy;
    in.pPubKeyCurvePtX = pk + 1U;
    in.pPubKeyCurvePtY = pk + 1U + BOOTUTIL_CRYPTO_ECDSA_P256_BYTES;
    in.RSign = signature;
    in.SSign = signature + BOOTUTIL_CRYPTO_ECDSA_P256_BYTES;
    in.hash = hash;
    in.primeOrder = p256_n;

    IWDG_Feed();
    hal_rc = HAL_PKA_ECDSAVerif(&hpka, &in, BOOTUTIL_PKA_TIMEOUT_MS);
    IWDG_Feed();

    if (hal_rc != HAL_OK) {
        return -1;
    }

    return (HAL_PKA_ECDSAVerif_IsValidSignature(&hpka) == 1UL) ? 0 : -1;
}

#endif /* MCUBOOT_ECDSA_BACKEND_STM32U5_PKA */
