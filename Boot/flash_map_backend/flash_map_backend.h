#ifndef H_FLASH_MAP_BACKEND_H
#define H_FLASH_MAP_BACKEND_H

#include <stdint.h>

/**
 * @brief Structure describing a sector within a flash area.
 *
 * Each sector has an offset relative to the start of its flash area
 * (NOT relative to the start of its flash device), and a size. A
 * flash area may contain sectors with different sizes.
 */
struct flash_sector {
    /**
     * Offset of this sector, from the start of its flash area (not device).
     */
    uint32_t fs_off;

    /**
     * Size of this sector, in bytes.
     */
    uint32_t fs_size;
};


static inline uint32_t flash_sector_get_off(const struct flash_sector *fs)
{
    return fs->fs_off;
}

static inline uint32_t flash_sector_get_size(const struct flash_sector *fs)
{
    return fs->fs_size;
}


/**
 * @brief Structure describing an area on a flash device.
 *
 * Multiple flash devices may be available in the system, each of
 * which may have its own areas. For this reason, flash areas track
 * which flash device they are part of.
 */
struct flash_area {
    /**
     * This flash area's ID; unique in the system.
     */
    uint8_t fa_id;

    /**
     * ID of the flash device this area is a part of.
     */
    uint8_t fa_device_id;

    uint16_t pad16;

    /**
     * This area's offset, relative to the beginning of its flash
     * device's storage.
     */
    uint32_t fa_off;

    /**
     * This area's size, in bytes.
     */
    uint32_t fa_size;
};

static inline uint32_t flash_area_get_size(const struct flash_area *fa)
{
    return fa->fa_size;
}

static inline uint32_t flash_area_get_off(const struct flash_area *fa)
{
    return fa->fa_off;
}

static inline uint8_t flash_area_get_device_id(const struct flash_area *fa)
{
    return fa->fa_device_id;
}

static inline uint8_t flash_area_get_id(const struct flash_area *fa)
{
    return fa->fa_id;
}



int flash_area_open(uint8_t id, const struct flash_area **fa);
void flash_area_close(const struct flash_area *fa);
int flash_area_id_from_multi_image_slot(int image_index, int slot);
int flash_area_get_sector(const struct flash_area *fa, uint32_t off,
                          struct flash_sector *sector);
uint8_t flash_area_erased_val(const struct flash_area *fap);
int flash_area_erase(const struct flash_area *fa, uint32_t off, uint32_t len);
uint32_t flash_area_align(const struct flash_area *fa);
int flash_area_write(const struct flash_area *fa, uint32_t off,
                     const void *src, uint32_t len);
int flash_area_read(const struct flash_area *fa, uint32_t off, void *dst,
                    uint32_t len);

#endif /* H_FLASH_MAP_BACKEND_H */


