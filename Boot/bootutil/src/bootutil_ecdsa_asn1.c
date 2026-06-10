/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "bootutil_ecdsa_asn1.h"

static const uint8_t ec_pubkey_oid[] = {
    0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01
};

static const uint8_t ec_secp256r1_oid[] = {
    0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07
};

static int bootutil_asn1_get_len(uint8_t **cp, uint8_t *end, size_t *len)
{
    uint8_t c;
    size_t n;
    size_t value = 0U;

    if (*cp >= end) {
        return -1;
    }

    c = *(*cp)++;
    if ((c & 0x80U) == 0U) {
        *len = c;
        return 0;
    }

    n = c & 0x7fU;
    if (n == 0U || n > sizeof(size_t) || (size_t)(end - *cp) < n) {
        return -1;
    }

    while (n-- > 0U) {
        value = (value << 8) | *(*cp)++;
    }

    *len = value;
    return 0;
}

static int bootutil_asn1_get_tag(uint8_t **cp, uint8_t *end, size_t *len,
                                 uint8_t tag)
{
    if (*cp >= end || *(*cp)++ != tag) {
        return -1;
    }
    if (bootutil_asn1_get_len(cp, end, len) != 0) {
        return -1;
    }
    if ((size_t)(end - *cp) < *len) {
        return -1;
    }
    return 0;
}

static int bootutil_read_bigint(uint8_t i[BOOTUTIL_CRYPTO_ECDSA_P256_BYTES],
                                uint8_t **cp, uint8_t *end)
{
    size_t len;

    if (bootutil_asn1_get_tag(cp, end, &len, 0x02) != 0) {
        return -1;
    }

    memset(i, 0, BOOTUTIL_CRYPTO_ECDSA_P256_BYTES);
    if (len >= BOOTUTIL_CRYPTO_ECDSA_P256_BYTES) {
        memcpy(i, *cp + len - BOOTUTIL_CRYPTO_ECDSA_P256_BYTES,
               BOOTUTIL_CRYPTO_ECDSA_P256_BYTES);
    } else {
        memcpy(i + BOOTUTIL_CRYPTO_ECDSA_P256_BYTES - len, *cp, len);
    }
    *cp += len;

    return 0;
}

int bootutil_ecdsa_asn1_parse_public_key(uint8_t **cp, uint8_t *end)
{
    size_t len;
    uint8_t *seq_end;
    uint8_t *alg_end;

    if (bootutil_asn1_get_tag(cp, end, &len, 0x30) != 0) {
        return -1;
    }
    seq_end = *cp + len;
    if (seq_end != end) {
        return -1;
    }

    if (bootutil_asn1_get_tag(cp, seq_end, &len, 0x30) != 0) {
        return -1;
    }
    alg_end = *cp + len;

    if (bootutil_asn1_get_tag(cp, alg_end, &len, 0x06) != 0 ||
        len != sizeof(ec_pubkey_oid) ||
        memcmp(*cp, ec_pubkey_oid, sizeof(ec_pubkey_oid)) != 0) {
        return -1;
    }
    *cp += len;

    if (bootutil_asn1_get_tag(cp, alg_end, &len, 0x06) != 0 ||
        len != sizeof(ec_secp256r1_oid) ||
        memcmp(*cp, ec_secp256r1_oid, sizeof(ec_secp256r1_oid)) != 0) {
        return -1;
    }
    *cp += len;

    if (*cp != alg_end) {
        return -1;
    }

    if (bootutil_asn1_get_tag(cp, seq_end, &len, 0x03) != 0 ||
        len != ((2U * BOOTUTIL_CRYPTO_ECDSA_P256_BYTES) + 2U)) {
        return -1;
    }
    if (*(*cp)++ != 0x00) {
        return -1;
    }
    if (*cp + ((2U * BOOTUTIL_CRYPTO_ECDSA_P256_BYTES) + 1U) != seq_end) {
        return -1;
    }

    return 0;
}

int bootutil_ecdsa_asn1_decode_sig(uint8_t signature[BOOTUTIL_CRYPTO_ECDSA_P256_SIG_SIZE],
                                   uint8_t *sig, size_t sig_len)
{
    uint8_t *cp;
    uint8_t *end;
    size_t len;

    if (signature == NULL || sig == NULL || sig_len == 0U) {
        return -1;
    }

    cp = sig;
    end = sig + sig_len;

    if (bootutil_asn1_get_tag(&cp, end, &len, 0x30) != 0) {
        return -1;
    }
    end = cp + len;

    if (bootutil_read_bigint(signature, &cp, end) != 0) {
        return -1;
    }
    if (bootutil_read_bigint(signature + BOOTUTIL_CRYPTO_ECDSA_P256_BYTES,
                             &cp, end) != 0) {
        return -1;
    }
    if (cp != end) {
        return -1;
    }

    return 0;
}
