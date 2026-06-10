/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef H_BOOTUTIL_ECDSA_ASN1_
#define H_BOOTUTIL_ECDSA_ASN1_

#include <stdint.h>
#include <stddef.h>

#include "bootutil/crypto/ecdsa.h"

#ifdef __cplusplus
extern "C" {
#endif

int bootutil_ecdsa_asn1_parse_public_key(uint8_t **cp, uint8_t *end);

int bootutil_ecdsa_asn1_decode_sig(uint8_t signature[BOOTUTIL_CRYPTO_ECDSA_P256_SIG_SIZE],
                                   uint8_t *sig, size_t sig_len);

#ifdef __cplusplus
}
#endif

#endif /* H_BOOTUTIL_ECDSA_ASN1_ */
