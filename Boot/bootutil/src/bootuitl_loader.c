#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bootutil_loader.h"
#include "bootutil_misc.h"
#include "bootutil_priv.h"
#include "bootutil/bootutil_log.h"
#include "flash_map_backend.h"
#include "bootutil/image.h"
int boot_check_image(struct boot_loader_state *state, struct boot_status *bs, int slot)
{
    TARGET_STATIC uint8_t tmpbuf[BOOT_TMPBUF_SZ];
    int rc;

    const struct flash_area *fap = NULL;
    struct image_header *hdr;

    fap = BOOT_IMG_AREA(state, slot);
    assert(fap != NULL);

    hdr = boot_img_hdr(state, slot);

    (void)bs;
    (void)rc;

    /* In case of ram loading, is has already been decrypted as it is
     * decrypted when copied into ram
     */

    rc = bootutil_img_validate(state, hdr, fap, tmpbuf, BOOT_TMPBUF_SZ,
                               NULL, 0, NULL);

    return rc;
}

bool boot_check_header_erased(struct boot_loader_state *state, int slot)
{
    const struct flash_area *fap = NULL;
    struct image_header *hdr;

    fap = BOOT_IMG_AREA(state, slot);
    assert(fap != NULL);

    hdr = boot_img_hdr(state, slot);
    if (bootutil_buffer_is_erased(fap, (const void *)&hdr->ih_magic,
                                  sizeof(hdr->ih_magic)))
    {
        return true;
    }

    return false;
}

bool boot_check_header_valid(struct boot_loader_state *state, int slot)
{
    const struct flash_area *fap = NULL;
    struct image_header *hdr;
    uint32_t size;

    fap = BOOT_IMG_AREA(state, slot);
    assert(fap != NULL);

    hdr = boot_img_hdr(state, slot);
    if (hdr->ih_magic != IMAGE_MAGIC)
    {
        return false;
    }

    if (!boot_u32_safe_add(&size, hdr->ih_img_size, hdr->ih_hdr_size))
    {
        return false;
    }

    if (!boot_u32_safe_add(&size, size, hdr->ih_protect_tlv_size))
    {
        return false;
    }

    if (size >= flash_area_get_size(fap))
    {
        return false;
    }

    return true;
}

int boot_read_image_header(struct boot_loader_state *state,
                           int slot,
                           struct image_header *out_hdr,
                           struct boot_status *bs)
{
    const struct flash_area *fap;
    int rc;

    (void)bs;

    fap = BOOT_IMG_AREA(state, slot);
    if (fap == NULL)
    {
        return BOOT_EFLASH;
    }

    // Direct-XIP: header 濮嬬粓鍦?slot 璧峰浣嶇疆
    rc = flash_area_read(fap, 0, out_hdr, sizeof(struct image_header));
    if (rc != 0)
    {
        return BOOT_EFLASH;
    }

    return 0;
}

int boot_read_image_headers(struct boot_loader_state *state, bool require_all, struct boot_status *bs)
{
    int rc;
    int i;
    struct image_header *hdr;

    for (i = 0; i < BOOT_NUM_SLOTS; i++)
    {

        hdr = boot_img_hdr(state, i);
        rc = boot_read_image_header(state, i, hdr, bs);

        if (rc != 0)
        {
            /* If `require_all` is set, fail on any single fail, otherwise
             * if at least the first slot's header was read successfully,
             * then boot loader can attempt a boot.
             *
             * Failure to read any headers is a fatal error.
             */
            if (i > 0 && !require_all)
            {
                return 0;
            }
            else
            {
                return rc;
            }
        }
    }

    return 0;
}

int boot_compare_version(const struct image_version *ver1, const struct image_version *ver2)
{
#if !defined(MCUBOOT_VERSION_CMP_USE_BUILD_NUMBER)
    BOOT_LOG_DBG("boot_version_cmp: ver1 %u.%u.%u vs ver2 %u.%u.%u",
                 (unsigned)ver1->iv_major, (unsigned)ver1->iv_minor,
                 (unsigned)ver1->iv_revision, (unsigned)ver2->iv_major,
                 (unsigned)ver2->iv_minor, (unsigned)ver2->iv_revision);
#else
    BOOT_LOG_DBG("boot_version_cmp: ver1 %u.%u.%u vs ver2 %u.%u.%u.%u",
                 (unsigned)ver1->iv_major, (unsigned)ver1->iv_minor,
                 (unsigned)ver1->iv_revision, (unsigned)ver1->iv_build_num,
                 (unsigned)ver2->iv_major, (unsigned)ver2->iv_minor,
                 (unsigned)ver2->iv_revision, (unsigned)ver2->iv_build_num);
#endif

    if (ver1->iv_major > ver2->iv_major)
    {
        return 1;
    }
    if (ver1->iv_major < ver2->iv_major)
    {
        return -1;
    }
    /* The major version numbers are equal, continue comparison. */
    if (ver1->iv_minor > ver2->iv_minor)
    {
        return 1;
    }
    if (ver1->iv_minor < ver2->iv_minor)
    {
        return -1;
    }
    /* The minor version numbers are equal, continue comparison. */
    if (ver1->iv_revision > ver2->iv_revision)
    {
        return 1;
    }
    if (ver1->iv_revision < ver2->iv_revision)
    {
        return -1;
    }

#if defined(MCUBOOT_VERSION_CMP_USE_BUILD_NUMBER)
    /* The revisions are equal, continue comparison. */
    if (ver1->iv_build_num > ver2->iv_build_num)
    {
        return 1;
    }
    if (ver1->iv_build_num < ver2->iv_build_num)
    {
        return -1;
    }
#endif

    return 0;
}

int boot_open_all_flash_areas(struct boot_loader_state *state)
{
    size_t slot;
    int rc = 0;
    int fa_id;
    int image_index;

    image_index = BOOT_CURR_IMG(state);

    for (slot = 0; slot < BOOT_NUM_SLOTS; slot++)
    {
        fa_id = flash_area_id_from_multi_image_slot(image_index, slot);
        rc = flash_area_open(fa_id, &BOOT_IMG_AREA(state, slot));

        assert(rc == 0);

        if (rc != 0)
        {
            BOOT_LOG_ERR("Failed to open flash area ID %d (image %d slot %zu): %d",
                         fa_id, image_index, slot, rc);
            goto out;
        }
    }

out:
    if (rc != 0)
    {
        boot_close_all_flash_areas(state);
    }

    return rc;
}

void boot_close_all_flash_areas(struct boot_loader_state *state)
{
    uint32_t slot;

    for (slot = 0; slot < BOOT_NUM_SLOTS; slot++)
    {
        if (BOOT_IMG_AREA(state, BOOT_NUM_SLOTS - 1 - slot) != NULL)
        {
            flash_area_close(BOOT_IMG_AREA(state, BOOT_NUM_SLOTS - 1 - slot));
        }
    }
}

void boot_state_init(struct boot_loader_state *state)
{
    memset(state, 0, sizeof(*state));
}
