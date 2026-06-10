#ifndef H_BOOTUTIL_PRIV_H
#define H_BOOTUTIL_PRIV_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bootutil/boot_public.h"
#include "bootutil/bootutil.h"
#include "bootutil/bootutil_log.h"
#include "flash_map_backend.h"
#include "bootutil/image.h"

#if (defined(MCUBOOT_OVERWRITE_ONLY) + \
     defined(MCUBOOT_SWAP_USING_MOVE) + \
     defined(MCUBOOT_SWAP_USING_OFFSET) + \
     defined(MCUBOOT_DIRECT_XIP) + \
     defined(MCUBOOT_RAM_LOAD) + \
     defined(MCUBOOT_FIRMWARE_LOADER) + \
     defined(MCUBOOT_SWAP_USING_SCRATCH)) > 1
#error "Please enable only one MCUboot upgrade mode"
#endif

#if !defined(MCUBOOT_DIRECT_XIP) && defined(MCUBOOT_DIRECT_XIP_REVERT)
#error "MCUBOOT_DIRECT_XIP_REVERT cannot be enabled unless MCUBOOT_DIRECT_XIP is used"
#endif

/**
 * End-of-image slot structure.
 *
 *   0                   1                   2                   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  ~                                                               ~
 *  ~    Swap status (BOOT_MAX_IMG_SECTORS * min-write-size * 3)    ~
 *  ~                                                               ~
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                 Encryption key 0 (16 octets) [*]              |
 *  |                                                               |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                    0xff padding as needed                     |
 *  |  (BOOT_MAX_ALIGN minus 16 octets from Encryption key 0) [*]   |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                 Encryption key 1 (16 octets) [*]              |
 *  |                                                               |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                    0xff padding as needed                     |
 *  |  (BOOT_MAX_ALIGN minus 16 octets from Encryption key 1) [*]   |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                      Swap size (4 octets)                     |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                    0xff padding as needed                     |
 *  |        (BOOT_MAX_ALIGN minus 4 octets from Swap size)         |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |   Swap info   |  0xff padding (BOOT_MAX_ALIGN minus 1 octet)  |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |   Copy done   |  0xff padding (BOOT_MAX_ALIGN minus 1 octet)  |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |   Image OK    |  0xff padding (BOOT_MAX_ALIGN minus 1 octet)  |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                    0xff padding as needed                     |
 *  |         (BOOT_MAX_ALIGN minus 16 octets from MAGIC)           |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                       MAGIC (16 octets)                       |
 *  |                                                               |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * [*]: Only present if the encryption option is enabled
 *      (`MCUBOOT_ENC_IMAGES`).
 */

union boot_img_magic_t
{
    struct {
        uint16_t align;
        uint8_t magic[14];
    };
    uint8_t val[16];
};

extern const union boot_img_magic_t boot_img_magic;

#define BOOT_IMG_MAGIC  (boot_img_magic.val)

#if BOOT_MAX_ALIGN == 8
#define BOOT_IMG_ALIGN  (BOOT_MAX_ALIGN)
#else
#define BOOT_IMG_ALIGN  (boot_img_magic.align)
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(boot_img_magic) == BOOT_MAGIC_SZ, "Invalid size for image magic");
#endif

#if !defined(MCUBOOT_DIRECT_XIP) && !defined(MCUBOOT_RAM_LOAD)
#define ARE_SLOTS_EQUIVALENT()    0
#else
#define ARE_SLOTS_EQUIVALENT()    1
#endif

#define BOOT_LOG_IMAGE_INFO(slot, hdr)                                    \
    BOOT_LOG_INF("%-9s slot: version=%u.%u.%u+%u",                        \
                 ((slot) == BOOT_SLOT_PRIMARY) ? "Primary" : "Secondary", \
                 (hdr)->ih_ver.iv_major,                                  \
                 (hdr)->ih_ver.iv_minor,                                  \
                 (hdr)->ih_ver.iv_revision,                               \
                 (hdr)->ih_ver.iv_build_num)

/**
 * Safe (non-overflowing) uint32_t addition.  Returns true, and stores
 * the result in *dest if it can be done without overflow.  Otherwise,
 * returns false.
 */
static inline bool boot_u32_safe_add(uint32_t *dest, uint32_t a, uint32_t b)
{
    /*
     * "a + b <= UINT32_MAX", subtract 'b' from both sides to avoid
     * the overflow.
     */
    if (a > UINT32_MAX - b)
    {
        return false;
    }
    else
    {
        *dest = a + b;
        return true;
    }
}

#ifndef BOOT_STATUS_DEFINED
#define BOOT_STATUS_DEFINED
struct boot_status
{
    uint8_t _unused;
};
#endif
#define BOOT_CURR_IMG(state) 0
#define BOOT_IMG(state, slot) ((state)->imgs[BOOT_CURR_IMG(state)][(slot)])
#define BOOT_IMG_AREA(state, slot) (BOOT_IMG(state, slot).area)
#define BOOT_NUM_SLOTS 2

/**
 * Compatibility shim for flash sector type.
 *
 * This can be deleted when flash_area_to_sectors() is removed.
 */
#ifdef MCUBOOT_USE_FLASH_AREA_GET_SECTORS
typedef struct flash_sector boot_sector_t;
#else
typedef struct flash_area boot_sector_t;
#endif

/** Private state maintained during boot. */
struct boot_loader_state
{
    struct
    {
        struct image_header hdr;
        const struct flash_area *area;
        boot_sector_t *sectors;
        uint32_t num_sectors;
    } imgs[BOOT_IMAGE_NUMBER][BOOT_NUM_SLOTS];

    uint8_t swap_type[BOOT_IMAGE_NUMBER];
    uint32_t write_sz[BOOT_IMAGE_NUMBER];

    struct slot_usage_t
    {
        /* Index of the slot chosen to be loaded */
        uint32_t active_slot;
        bool slot_available[BOOT_NUM_SLOTS];

        /* Swap status for the active slot */
        struct boot_swap_state swap_state;

    } slot_usage[BOOT_IMAGE_NUMBER];
};

static inline struct image_header *
boot_img_hdr(struct boot_loader_state *state, size_t slot)
{
    return &BOOT_IMG(state, slot).hdr;
}

/*
 * Offset of the slot from the beginning of the flash device.
 */
static inline uint32_t
boot_img_slot_off(struct boot_loader_state *state, size_t slot)
{
    return flash_area_get_off(BOOT_IMG_AREA(state, slot));
}

#define LOAD_IMAGE_DATA(hdr, fap, start, output, size) \
    (flash_area_read((fap), (start), (output), (size)))

#define BOOT_TMPBUF_SZ 256

bool bootutil_buffer_is_erased(const struct flash_area *area,
                               const void *buffer, size_t len);
int boot_read_swap_state(const struct flash_area *fap,
                         struct boot_swap_state *state);
int boot_read_image_ok(const struct flash_area *fap, uint8_t *image_ok);
int boot_write_copy_done(const struct flash_area *fap);
int boot_write_image_ok(const struct flash_area *fap);
uint32_t boot_swap_info_off(const struct flash_area *fap);
uint8_t boot_flag_decode(uint8_t flag);
int boot_write_trailer_flag(const struct flash_area *fap, uint32_t off,
                            uint8_t val);

uint32_t bootutil_max_image_size(struct boot_loader_state *state, const struct flash_area *fap);
int bootutil_find_key(uint8_t *keyhash, uint8_t keyhash_len);
fih_ret bootutil_verify_sig(uint8_t *hash, uint32_t hlen, uint8_t *sig,
                            size_t slen, uint8_t key_id);

#endif /* H_BOOTUTIL_PRIV_H */
