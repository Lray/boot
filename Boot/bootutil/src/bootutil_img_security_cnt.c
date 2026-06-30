/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "flash_map_backend.h"
#include "bootutil/image.h"
#include "bootutil/security_cnt.h"
#include "bootutil_priv.h"
#include "mcuboot_config.h"

int32_t
bootutil_get_img_security_cnt(struct boot_loader_state *state, int slot,
                              const struct flash_area *fap,
                              uint32_t *img_security_cnt)
{
    struct image_tlv_iter it;
    uint32_t off;
    uint16_t len;
    int32_t rc;

    if ((state == NULL) ||
        (boot_img_hdr(state, slot) == NULL) ||
        (fap == NULL) ||
        (img_security_cnt == NULL)) {
        return BOOT_EBADIMAGE;
    }

    if (boot_img_hdr(state, slot)->ih_protect_tlv_size == 0U) {
        return BOOT_EBADIMAGE;
    }

    rc = bootutil_tlv_iter_begin(&it, boot_img_hdr(state, slot), fap,
                                 IMAGE_TLV_SEC_CNT, true);
    if (rc != 0) {
        return rc;
    }

    rc = bootutil_tlv_iter_next(&it, &off, &len, NULL);
    if (rc != 0) {
        return BOOT_EBADIMAGE;
    }

    if (len != sizeof(*img_security_cnt)) {
        return BOOT_EBADIMAGE;
    }

    rc = LOAD_IMAGE_DATA(boot_img_hdr(state, slot), fap, off,
                         img_security_cnt, len);
    if (rc != 0) {
        return BOOT_EFLASH;
    }

    return 0;
}
