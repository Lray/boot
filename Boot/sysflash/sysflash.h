/* Manual version of auto-generated version. */

#ifndef H_SYSFLASH_H
#define H_SYSFLASH_H

#define FLASH_DEVICE_INTERNAL_FLASH (0x7F)

#define FLASH_AREA_BOOTLOADER 0
#define FLASH_AREA_IMAGE_0 1
#define FLASH_AREA_IMAGE_1 2
#define FLASH_AREA_BOOT_USER 3

#define CY_BOOTLOADER_START_ADDRESS (0x08000000U)
#define CY_FLASH_BASE (0x08000000U)

/* STM32U5A9NJH6Q debug layout using the lower 2MB Flash window. */
/* Flash partition layout (U5 2MB Flash window, 8KB page size):
 * Bootloader:  64KB (0x10000) 0x08000000 - 0x0800FFFF
 * Primary:     24KB (0x6000)  0x08010000 - 0x08015FFF
 * Secondary:   24KB (0x6000)  0x08016000 - 0x0801BFFF
 * Reserved:  1928KB (0x1E2000) 0x0801C000 - 0x081FDFFF
 * User:         8KB (0x2000)  0x081FE000 - 0x081FFFFF
 */
#define CY_FLASH_SIZE (0x200000U)
#define CY_BOOT_BOOTLOADER_SIZE (0x10000)
#define CY_BOOT_PRIMARY_1_SIZE (0x6000)
#define CY_BOOT_SECONDARY_1_SIZE (0x6000)
#define CY_BOOT_USER_AREA_SIZE (0x2000U)
#define CY_BOOT_USER_AREA_OFFSET (CY_FLASH_SIZE - CY_BOOT_USER_AREA_SIZE)

#define CY_IMG_HDR_SIZE 0x200

#ifndef CY_FLASH_MAP_EXT_DESC
/* Uncomment in case you want to use separately defined table of flash area descriptors */
/* #define CY_FLASH_MAP_EXT_DESC */
#endif

#endif /* H_SYSFLASH_H */
