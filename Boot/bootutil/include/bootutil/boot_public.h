#ifndef H_BOOT_PUBLIC_H
#define H_BOOT_PUBLIC_H

#include <stdint.h>

#include <mcuboot_config.h>

#include "bootutil/bootutil_macros.h"

#define BOOT_MAGIC_SZ           16

#ifdef MCUBOOT_BOOT_MAX_ALIGN
#define BOOT_MAX_ALIGN          MCUBOOT_BOOT_MAX_ALIGN
#define BOOT_MAGIC_ALIGN_SIZE   ALIGN_UP(BOOT_MAGIC_SZ, BOOT_MAX_ALIGN)
#else
#define BOOT_MAX_ALIGN          8
#define BOOT_MAGIC_ALIGN_SIZE   BOOT_MAGIC_SZ
#endif

#define BOOT_MAGIC_GOOD     1
#define BOOT_MAGIC_BAD      2
#define BOOT_MAGIC_UNSET    3
#define BOOT_MAGIC_ANY      4  /* NOTE: control only, not dependent on sector */
#define BOOT_MAGIC_NOTGOOD  5  /* NOTE: control only, not dependent on sector */


/*
 * NOTE: leave BOOT_FLAG_SET equal to one, this is written to flash!
 */
#define BOOT_FLAG_SET       1
#define BOOT_FLAG_BAD       2
#define BOOT_FLAG_UNSET     3
#define BOOT_FLAG_ANY       4  /* NOTE: control only, not dependent on sector */

#define BOOT_EFLASH      1
#define BOOT_EBADIMAGE   2
#define BOOT_EBADVECT    3
#define BOOT_EBADSTATUS  4

#define BOOT_GET_SWAP_TYPE(swap_info)    ((swap_info) & 0x0F)
#define BOOT_GET_IMAGE_NUM(swap_info)    ((swap_info) >> 4)

#define FLASH_AREA_IMAGE_PRIMARY(image_index)   FLASH_AREA_IMAGE_0
#define FLASH_AREA_IMAGE_SECONDARY(image_index) FLASH_AREA_IMAGE_1

/** Attempt to boot the contents of the primary slot. */
#define BOOT_SWAP_TYPE_NONE     1
/** Swap to secondary slot. Absent a confirm command, revert back on next boot. */
#define BOOT_SWAP_TYPE_TEST     2
/** Swap to secondary slot, and permanently switch to booting its contents. */
#define BOOT_SWAP_TYPE_PERM     3

/** Swap back to alternate slot.  A confirm changes this state to NONE. */
#define BOOT_SWAP_TYPE_REVERT   4

enum boot_slot {
    BOOT_SLOT_PRIMARY = 0,      /* Primary slot */
    BOOT_SLOT_SECONDARY = 1,    /* Secondary slot */
    BOOT_SLOT_COUNT = 2,        /* Number of slots */
    BOOT_SLOT_NONE = UINT32_MAX,      /* special value representing no active slot */
};

struct boot_swap_state {
    uint8_t magic;      /* One of the BOOT_MAGIC_[...] values. */
    uint8_t swap_type;  /* One of the BOOT_SWAP_TYPE_[...] values. */
    uint8_t copy_done;  /* One of the BOOT_FLAG_[...] values. */
    uint8_t image_ok;   /* One of the BOOT_FLAG_[...] values. */
    uint8_t image_num;  /* Boot status belongs to this image */
};

#endif /* H_BOOT_PUBLIC_H */





