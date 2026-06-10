/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "bootutil/crypto/sha.h"
#include "bootutil/image.h"
#include "bootutil_priv.h"
#include "bootutil/bootutil_log.h"
#include "iwdg.h"

/*
 * Compute SHA hash over the image.
 * SHA-256 is used for EC256 images.
 */
int
bootutil_img_hash(struct boot_loader_state *state,
                  struct image_header *hdr, const struct flash_area *fap,
                  uint8_t *tmp_buf, uint32_t tmp_buf_sz, uint8_t *hash_result,
                  uint8_t *seed, int seed_len)
{
    bootutil_sha_context sha_ctx;
    uint32_t size;
    uint32_t off;
    uint32_t blk_sz;
    int rc;

    (void)state;

    BOOT_LOG_DBG("bootutil_img_hash");

    if (tmp_buf == NULL || tmp_buf_sz == 0U || hash_result == NULL) {
        return -1;
    }

    if (!boot_u32_safe_add(&size, hdr->ih_hdr_size, hdr->ih_img_size)) {
        return -1;
    }
    if (!boot_u32_safe_add(&size, size, hdr->ih_protect_tlv_size)) {
        return -1;
    }
    if (size > flash_area_get_size(fap)) {
        return -1;
    }

    rc = bootutil_sha_init(&sha_ctx);
    if (rc != 0) {
        return rc;
    }

    if (seed != NULL && seed_len > 0) {
        rc = bootutil_sha_update(&sha_ctx, seed, (uint32_t)seed_len);
        if (rc != 0) {
            bootutil_sha_drop(&sha_ctx);
            return rc;
        }
    }

    for (off = 0U; off < size; off += blk_sz) {
        blk_sz = size - off;
        if (blk_sz > tmp_buf_sz) {
            blk_sz = tmp_buf_sz;
        }

        rc = LOAD_IMAGE_DATA(hdr, fap, off, tmp_buf, blk_sz);
        if (rc != 0) {
            bootutil_sha_drop(&sha_ctx);
            BOOT_LOG_DBG("bootutil_img_hash: read failed %d", rc);
            return rc;
        }

        IWDG_Feed();

        rc = bootutil_sha_update(&sha_ctx, tmp_buf, blk_sz);
        if (rc != 0) {
            bootutil_sha_drop(&sha_ctx);
            return rc;
        }
    }

    rc = bootutil_sha_finish(&sha_ctx, hash_result);
    bootutil_sha_drop(&sha_ctx);

    return rc;
}
