#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "bootutil/boot_public.h"
#include "bootutil/boot_active_slot_flash.h"
#include "bootutil_misc.h"
#include "bootutil_priv.h"
#include "bootutil/bootutil_log.h"
#include "flash_map_backend.h"
#include "sysflash.h"
#include <inttypes.h>

#if BOOT_MAX_ALIGN == 8
const union boot_img_magic_t boot_img_magic = {
    .val = {
        0x77, 0xc2, 0x95, 0xf3,
        0x60, 0xd2, 0xef, 0x7f,
        0x35, 0x52, 0x50, 0x0f,
        0x2c, 0xb6, 0x79, 0x80}};
#else
const union boot_img_magic_t boot_img_magic = {
    .align = BOOT_MAX_ALIGN,
    .magic = {
        0x2d, 0xe1,
        0x5d, 0x29, 0x41, 0x0b,
        0x8d, 0x77, 0x67, 0x9c,
        0x11, 0x0f, 0x1f, 0x8a}};
#endif

static int
boot_read_flag(const struct flash_area *fap, uint8_t *flag, uint32_t off)
{
    int rc;

    rc = flash_area_read(fap, off, flag, sizeof *flag);
    if (rc < 0)
    {
        return BOOT_EFLASH;
    }
    if (bootutil_buffer_is_erased(fap, flag, sizeof *flag))
    {
        *flag = BOOT_FLAG_UNSET;
    }
    else
    {
        *flag = boot_flag_decode(*flag);
    }

    return 0;
}

uint8_t
boot_flag_decode(uint8_t flag)
{
    if (flag == BOOT_FLAG_SET)
    {
        return BOOT_FLAG_SET;
    }
    if (flag == BOOT_FLAG_BAD)
    {
        return BOOT_FLAG_BAD;
    }
    return BOOT_FLAG_UNSET;
}

/**
 * Write trailer data; status bytes, swap_size, etc
 *
 * @returns 0 on success, != 0 on error.
 */
int boot_write_trailer(const struct flash_area *fap, uint32_t off,
                       const uint8_t *inbuf, uint8_t inlen)
{
    uint8_t buf[BOOT_MAX_ALIGN];
    uint8_t erased_val;
    uint32_t align;
    int rc;

    BOOT_LOG_DBG("boot_write_trailer: for %p at %" PRIu32 ", size = %d",
                 fap, off, inlen);

    align = flash_area_align(fap);
    align = ALIGN_UP(inlen, align);
    if (align > BOOT_MAX_ALIGN)
    {
        /* This should never happen */
        assert(0);
        return -1;
    }
    erased_val = flash_area_erased_val(fap);

    memcpy(buf, inbuf, inlen);
    memset(&buf[inlen], erased_val, align - inlen);

    rc = flash_area_write(fap, off, buf, align);
    if (rc != 0)
    {
        return BOOT_EFLASH;
    }

    return 0;
}

int boot_write_trailer_flag(const struct flash_area *fap, uint32_t off,
                            uint8_t flag_val)
{
    const uint8_t buf[1] = {flag_val};
    return boot_write_trailer(fap, off, buf, 1);
}

int boot_read_image_ok(const struct flash_area *fap, uint8_t *image_ok)
{
    return boot_read_flag(fap, image_ok, boot_image_ok_off(fap));
}

int boot_write_copy_done(const struct flash_area *fap)
{
    uint32_t off;

    off = boot_copy_done_off(fap);
    BOOT_LOG_DBG("writing copy_done; fa_id=%d off=0x%lx (0x%lx)",
                 flash_area_get_id(fap), (unsigned long)off,
                 (unsigned long)(flash_area_get_off(fap) + off));
    return boot_write_trailer_flag(fap, off, BOOT_FLAG_SET);
}

int boot_write_image_ok(const struct flash_area *fap)
{
    uint32_t off;

    off = boot_image_ok_off(fap);
    BOOT_LOG_DBG("writing image_ok; fa_id=%d off=0x%lx (0x%lx)",
                 flash_area_get_id(fap), (unsigned long)off,
                 (unsigned long)(flash_area_get_off(fap) + off));
    return boot_write_trailer_flag(fap, off, BOOT_FLAG_SET);
}

static inline int
boot_read_copy_done(const struct flash_area *fap, uint8_t *copy_done)
{
    return boot_read_flag(fap, copy_done, boot_copy_done_off(fap));
}

bool bootutil_buffer_is_erased(const struct flash_area *area,
                               const void *buffer, size_t len)
{
    size_t i;
    uint8_t *u8b;
    uint8_t erased_val;

    if (buffer == NULL || len == 0)
    {
        return false;
    }

    erased_val = flash_area_erased_val(area);
    for (i = 0, u8b = (uint8_t *)buffer; i < len; i++)
    {
        if (u8b[i] != erased_val)
        {
            return false;
        }
    }

    return true;
}

uint32_t
boot_swap_info_off(const struct flash_area *fap)
{
    return boot_copy_done_off(fap) - BOOT_MAX_ALIGN;
}

int boot_read_swap_state(const struct flash_area *fap,
                         struct boot_swap_state *state)
{
    uint8_t magic[BOOT_MAGIC_SZ];
    uint32_t off;
    uint8_t swap_info;
    int rc;

    off = boot_magic_off(fap);
    rc = flash_area_read(fap, off, magic, BOOT_MAGIC_SZ);
    if (rc < 0)
    {
        return BOOT_EFLASH;
    }
    if (bootutil_buffer_is_erased(fap, magic, BOOT_MAGIC_SZ))
    {
        state->magic = BOOT_MAGIC_UNSET;
    }
    else
    {
        state->magic = boot_magic_decode(magic);
    }

    off = boot_swap_info_off(fap);
    rc = flash_area_read(fap, off, &swap_info, sizeof swap_info);
    if (rc < 0)
    {
        return BOOT_EFLASH;
    }

    /* Extract the swap type and image number */
    state->swap_type = BOOT_GET_SWAP_TYPE(swap_info);
    state->image_num = BOOT_GET_IMAGE_NUM(swap_info);

    if (bootutil_buffer_is_erased(fap, &swap_info, sizeof swap_info) ||
        state->swap_type > BOOT_SWAP_TYPE_REVERT)
    {
        state->swap_type = BOOT_SWAP_TYPE_NONE;
        state->image_num = 0;
    }

    rc = boot_read_copy_done(fap, &state->copy_done);
    if (rc)
    {
        return BOOT_EFLASH;
    }

    return boot_read_image_ok(fap, &state->image_ok);
}

int boot_write_magic(const struct flash_area *fap)
{
    uint32_t off;
    uint32_t pad_off;
    int rc;
    uint8_t magic[BOOT_MAGIC_ALIGN_SIZE];
    uint8_t erased_val;

    off = boot_magic_off(fap);

    /* image_trailer structure was modified with additional padding such that
     * the pad+magic ends up in a flash minimum write region. The address
     * returned by boot_magic_off() is the start of magic which is not the
     * start of the flash write boundary and thus writes to the magic will fail.
     * To account for this change, write to magic is first padded with 0xFF
     * before writing to the trailer.
     */
    pad_off = ALIGN_DOWN(off, BOOT_MAX_ALIGN);

    erased_val = flash_area_erased_val(fap);

    memset(&magic[0], erased_val, sizeof(magic));
    memcpy(&magic[BOOT_MAGIC_ALIGN_SIZE - BOOT_MAGIC_SZ], BOOT_IMG_MAGIC, BOOT_MAGIC_SZ);

    BOOT_LOG_DBG("boot_write_magic: fa_id=%d off=0x%lx (0x%lx)",
                 flash_area_get_id(fap), (unsigned long)off,
                 (unsigned long)(flash_area_get_off(fap) + off));
    rc = flash_area_write(fap, pad_off, &magic[0], BOOT_MAGIC_ALIGN_SIZE);

    if (rc != 0)
    {
        return BOOT_EFLASH;
    }

    return 0;
}

int boot_write_swap_info(const struct flash_area *fap, uint8_t swap_type,
                         uint8_t image_num)
{
    uint8_t swap_info;
    uint32_t off;

    swap_info = (uint8_t)((image_num << 4) | (swap_type & 0x0f));
    off = boot_swap_info_off(fap);
    BOOT_LOG_DBG("writing swap_info; fa_id=%d off=0x%lx (0x%lx)",
                 flash_area_get_id(fap), (unsigned long)off,
                 (unsigned long)(flash_area_get_off(fap) + off));
    return boot_write_trailer_flag(fap, off, swap_info);
}

static int flash_area_to_image(const struct flash_area *fa)
{
    (void)fa;
    return 0;
}



