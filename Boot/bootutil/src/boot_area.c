#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "boot_area.h"
#include "bootutil/boot_public.h"
#include "bootutil/bootutil_log.h"
#include "bootutil/bootutil_macros.h"
#include "flash_map_backend.h"
int
boot_erase_region(const struct flash_area *fa, uint32_t off, uint32_t size, bool backwards)
{
    int rc = 0;

    BOOT_LOG_DBG("boot_erase_region: flash_area %p, offset %" PRIu32 ""
                 ", size %" PRIu32 ", backwards == %" PRIu8,
                 fa, off, size, (int)backwards);

    if (off >= flash_area_get_size(fa) || (flash_area_get_size(fa) - off) < size) {
        rc = -1;
        goto end;
    } else if (device_requires_erase(fa)) {
        uint32_t end_offset = 0;
        struct flash_sector sector;

        BOOT_LOG_DBG("boot_erase_region: device with erase");

        if (backwards) {
            /* Get the lowest page offset first */
            rc = flash_area_get_sector(fa, off, &sector);

            if (rc < 0) {
                goto end;
            }

            end_offset = flash_sector_get_off(&sector);

            /* Set boundary condition, the highest probable offset to erase, within
             * last sector to erase
             */
            off += size - 1;
        } else {
            /* Get the highest page offset first */
            rc = flash_area_get_sector(fa, (off + size - 1), &sector);

            if (rc < 0) {
                goto end;
            }

            end_offset = flash_sector_get_off(&sector);
        }

        while (true) {
            /* Size to read in this iteration */
            size_t csize;

            /* Get current sector and, also, correct offset */
            rc = flash_area_get_sector(fa, off, &sector);

            if (rc < 0) {
                goto end;
            }

            /* Corrected offset and size of current sector to erase */
            off = flash_sector_get_off(&sector);
            csize = flash_sector_get_size(&sector);

            rc = flash_area_erase(fa, off, csize);

            if (rc < 0) {
                goto end;
            }


            if (backwards) {
                if (end_offset >= off) {
                    /* Reached the first offset in range and already erased it */
                    break;
                }

                /* Move down to previous sector, the flash_area_get_sector will
                 * correct the value to real page offset
                 */
                off -= 1;
            } else {
                /* Move up to next sector */
                off += csize;

                if (off > end_offset) {
                    /* Reached the end offset in range and already erased it */
                    break;
                }

                /* Workaround for flash_sector_get_off() being broken in mynewt, hangs with
                 * infinite loop if this is not present, should be removed if bug is fixed.
                 */
                off += 1;
            }
        }
    } else {
        BOOT_LOG_DBG("boot_erase_region: device without erase");
    }

end:
    return rc;
}

int
boot_scramble_region(const struct flash_area *fa, uint32_t off, uint32_t size, bool backwards)
{
    int rc = 0;

    BOOT_LOG_DBG("boot_scramble_region: %p %" PRIu32 " %" PRIu32 " %d",
                 fa, off, size, (int)backwards);

    if (size == 0) {
        goto done;
    }

    if (device_requires_erase(fa)) {
        rc = boot_erase_region(fa, off, size, backwards);
    } else if (off >= flash_area_get_size(fa) || (flash_area_get_size(fa) - off) < size) {
        rc = -1;
        goto done;
    } else {
        uint8_t buf[BOOT_MAX_ALIGN];
        const size_t write_block = flash_area_align(fa);
        uint32_t end_offset;

        BOOT_LOG_DBG("boot_scramble_region: device without erase, overwriting");
        memset(buf, flash_area_erased_val(fa), sizeof(buf));

        if (backwards) {
            end_offset = ALIGN_DOWN(off, write_block);
            /* Starting at the last write block in range */
            off += size - write_block;
        } else {
            end_offset = ALIGN_DOWN((off + size), write_block);
        }
        BOOT_LOG_DBG("boot_scramble_region: start offset %" PRIu32 ", "
                     "end offset %" PRIu32, off, end_offset);

        while (off != end_offset) {
            /* Write over the area to scramble data that is there */
            rc = flash_area_write(fa, off, buf, write_block);
            if (rc != 0) {
                BOOT_LOG_DBG("boot_scramble_region: error %d for %p "
                             "%" PRIu32 " %u",
                             rc, fa, off, (unsigned int)write_block);
                break;
            }

            if (backwards) {
                if (end_offset >= off) {
                    /* Reached the first offset in range and already scrambled it */
                    break;
                }

                off -= write_block;
            } else {
                off += write_block;

                if (end_offset <= off) {
                    /* Reached the end offset in range and already scrambled it */
                    break;
                }
            }
        }
    }

done:
    return rc;
}


int
boot_scramble_slot(const struct flash_area *fa, int slot)
{
    size_t size;
    int ret = 0;

    (void)slot;

    /* Without minimal entire area needs to be scrambled */

    size = flash_area_get_size(fa);
    ret = boot_scramble_region(fa, 0, size, false);

    return ret;
}

