#ifndef H_BOOTUTIL_MISC_H
#define H_BOOTUTIL_MISC_H

#include <stdint.h>
#include <string.h>

#include "bootutil/boot_public.h"
#include "bootutil/bootutil_macros.h"
#include "bootutil_priv.h"
#include "flash_map_backend.h"

static inline uint32_t
boot_magic_off(const struct flash_area *fap)
{
    return flash_area_get_size(fap) - BOOT_MAGIC_SZ;
}

static inline int
boot_magic_decode(const uint8_t *magic)
{
    if (memcmp(magic, BOOT_IMG_MAGIC, BOOT_MAGIC_SZ) == 0) {
        return BOOT_MAGIC_GOOD;
    }
    return BOOT_MAGIC_BAD;
}

static inline uint32_t
boot_image_ok_off(const struct flash_area *fap)
{
    return ALIGN_DOWN(boot_magic_off(fap) - BOOT_MAX_ALIGN, BOOT_MAX_ALIGN);
}

static inline uint32_t
boot_copy_done_off(const struct flash_area *fap)
{
    return boot_image_ok_off(fap) - BOOT_MAX_ALIGN;
}

#endif /* H_BOOTUTIL_MISC_H */

