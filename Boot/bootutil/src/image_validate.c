/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "bootutil/image.h"
#include "bootutil/crypto/sha.h"
#include "bootutil/sign_key.h"
#include "bootutil/fault_injection_hardening.h"
#include "bootutil_priv.h"
#include "mcuboot_config.h"
#include "bootutil/bootutil_log.h"

BOOT_LOG_MODULE_DECLARE(mcuboot);

#if defined(MCUBOOT_SIGN_EC256)
#define EXPECTED_SIG_TLV IMAGE_TLV_ECDSA_SIG
#define SIG_BUF_SIZE 128
#define EXPECTED_SIG_LEN(x) (1)
#else
#define SIG_BUF_SIZE 32
#endif

#if (defined(MCUBOOT_HW_KEY) + defined(MCUBOOT_BUILTIN_KEY)) > 1
#error "Please use either MCUBOOT_HW_KEY or the MCUBOOT_BUILTIN_KEY feature."
#endif

#ifdef EXPECTED_SIG_TLV
#if !defined(MCUBOOT_BUILTIN_KEY)
#if !defined(MCUBOOT_HW_KEY)
#define EXPECTED_KEY_TLV IMAGE_TLV_KEYHASH
#define KEY_BUF_SIZE IMAGE_HASH_SIZE
#else
#define EXPECTED_KEY_TLV IMAGE_TLV_PUBKEY
#define KEY_BUF_SIZE (SIG_BUF_SIZE + 24)
#endif /* !MCUBOOT_HW_KEY */
#endif /* !MCUBOOT_BUILTIN_KEY */
#endif /* EXPECTED_SIG_TLV */

#if defined(EXPECTED_SIG_TLV) || defined(EXPECTED_HASH_TLV)
#define BOOTUTIL_VALIDATE_HASH
#endif

/*
 * Verify the integrity of the image.
 * Return non-zero if image could not be validated/does not validate.
 */
fih_ret
bootutil_img_validate(struct boot_loader_state *state,
                      struct image_header *hdr, const struct flash_area *fap,
                      uint8_t *tmp_buf, uint32_t tmp_buf_sz, uint8_t *seed,
                      int seed_len, uint8_t *out_hash)
{
    uint32_t off;
    uint16_t len;
    uint16_t type;
    uint32_t img_sz;
#ifdef EXPECTED_SIG_TLV
    FIH_DECLARE(valid_signature, FIH_FAILURE);
#ifndef MCUBOOT_BUILTIN_KEY
    int key_id = -1;
#else
    int key_id = 0;
#endif /* !MCUBOOT_BUILTIN_KEY */
#endif /* EXPECTED_SIG_TLV */
    struct image_tlv_iter it;
    uint8_t buf[SIG_BUF_SIZE];
#if defined(EXPECTED_HASH_TLV)
    int image_hash_valid = 0;
#endif
#if defined(BOOTUTIL_VALIDATE_HASH)
    uint8_t hash[IMAGE_HASH_SIZE];
#endif
    int rc = 0;
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    BOOT_LOG_DBG("bootutil_img_validate: flash area %p", fap);

#if defined(BOOTUTIL_VALIDATE_HASH)

    rc = bootutil_img_hash(state, hdr, fap, tmp_buf, tmp_buf_sz, hash, seed,
                           seed_len);

    if (rc) {
        goto out;
    }

    if (out_hash != NULL) {
        memcpy(out_hash, hash, IMAGE_HASH_SIZE);
    }
#endif /* BOOTUTIL_VALIDATE_HASH */

    rc = bootutil_tlv_iter_begin(&it, hdr, fap, IMAGE_TLV_ANY, false);
    if (rc) {
        BOOT_LOG_DBG("bootutil_img_validate: TLV iteration failed %d", rc);
        goto out;
    }

    img_sz = it.tlv_end;
    BOOT_LOG_DBG("bootutil_img_validate: TLV off %" PRIu32 ", end %" PRIu32,
                 it.tlv_off, it.tlv_end);

    if (img_sz > bootutil_max_image_size(state, fap)) {
        rc = -1;
        BOOT_LOG_DBG("bootutil_img_validate: TLV beyond image size");
        goto out;
    }

    while (true) {
        rc = bootutil_tlv_iter_next(&it, &off, &len, &type);
        if (rc < 0) {
            goto out;
        } else if (rc > 0) {
            break;
        }

        switch (type) {
#if defined(EXPECTED_HASH_TLV)
        case EXPECTED_HASH_TLV:
            BOOT_LOG_DBG("bootutil_img_validate: EXPECTED_HASH_TLV == %d",
                         EXPECTED_HASH_TLV);
            if (len != sizeof(hash)) {
                rc = -1;
                goto out;
            }
            rc = LOAD_IMAGE_DATA(hdr, fap, off, buf, sizeof(hash));
            if (rc) {
                goto out;
            }

            if (memcmp(hash, buf, sizeof(hash)) != 0) {
                rc = -1;
                goto out;
            }

            image_hash_valid = 1;
            break;
#endif /* EXPECTED_HASH_TLV */
#ifdef EXPECTED_KEY_TLV
        case EXPECTED_KEY_TLV:
            BOOT_LOG_DBG("bootutil_img_validate: EXPECTED_KEY_TLV == %d",
                         EXPECTED_KEY_TLV);
            if (len != KEY_BUF_SIZE) {
                rc = -1;
                goto out;
            }
#ifndef MCUBOOT_HW_KEY
            rc = LOAD_IMAGE_DATA(hdr, fap, off, buf, len);
            if (rc) {
                goto out;
            }
            key_id = bootutil_find_key(buf, len);
#else
            rc = -1;
            goto out;
#endif /* !MCUBOOT_HW_KEY */
            break;
#endif /* EXPECTED_KEY_TLV */
#ifdef EXPECTED_SIG_TLV
        case EXPECTED_SIG_TLV:
            BOOT_LOG_DBG("bootutil_img_validate: EXPECTED_SIG_TLV == %d",
                         EXPECTED_SIG_TLV);
            if (key_id < 0 || key_id >= bootutil_key_cnt) {
                key_id = -1;
                continue;
            }
            if (!EXPECTED_SIG_LEN(len) || len > sizeof(buf)) {
                rc = -1;
                goto out;
            }
            rc = LOAD_IMAGE_DATA(hdr, fap, off, buf, len);
            if (rc) {
                goto out;
            }
            FIH_CALL(bootutil_verify_sig, valid_signature, hash, sizeof(hash),
                     buf, len, (uint8_t)key_id);
            key_id = -1;
            break;
#endif /* EXPECTED_SIG_TLV */
        default:
            break;
        }
    }

#if defined(EXPECTED_HASH_TLV)
    rc = !image_hash_valid;
    if (rc) {
        goto out;
    }
#endif
#ifdef EXPECTED_SIG_TLV
    FIH_SET(fih_rc, valid_signature);
#else
    FIH_SET(fih_rc, FIH_SUCCESS);
#endif

out:
    if (rc) {
        FIH_SET(fih_rc, FIH_FAILURE);
    }

    FIH_RET(fih_rc);
}
