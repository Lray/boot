/* Helper macro to avoid compile errors with systems that do not
 * provide function to check device type.
 * Note: it used to be inline, but somehow compiler would not
 * optimize out branches that were impossible when this evaluated to
 * just "true".
 */
#ifndef H_BOOT_AREA_H
#define H_BOOT_AREA_H

#include <stdbool.h>
#include <stdint.h>

#include "flash_map_backend.h"

#if defined(MCUBOOT_SUPPORT_DEV_WITHOUT_ERASE) && defined(MCUBOOT_SUPPORT_DEV_WITH_ERASE)
#define device_requires_erase(fa) (flash_area_erase_required(fa))
#elif defined(MCUBOOT_SUPPORT_DEV_WITHOUT_ERASE)
#define device_requires_erase(fa) (false)
#else
#define device_requires_erase(fa) (true)
#endif

int boot_erase_region(const struct flash_area *fa, uint32_t off,
                      uint32_t size, bool backwards);
int boot_scramble_region(const struct flash_area *fa, uint32_t off,
                         uint32_t size, bool backwards);
int boot_scramble_slot(const struct flash_area *fa, int slot);

#endif /* H_BOOT_AREA_H */

