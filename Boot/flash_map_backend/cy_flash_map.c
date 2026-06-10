#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "stm32u5xx_hal.h"
#include "stm32u5xx_hal_flash_ex.h"
#include "iwdg.h"

#include "bootutil/bootutil_log.h"
#include "flash_map_backend.h"
#include "sysflash.h"

#define FLASH_PROGRAM_UNIT                 (16U)

static bool flash_program_unit_is_erased(const uint8_t *src)
{
    for (uint32_t i = 0; i < FLASH_PROGRAM_UNIT; i++) {
        if (src[i] != 0xffU) {
            return false;
        }
    }

    return true;
}

static struct flash_area bootloader =
{
    .fa_id = FLASH_AREA_BOOTLOADER,
    .fa_device_id = FLASH_DEVICE_INTERNAL_FLASH,
    .fa_off = CY_BOOTLOADER_START_ADDRESS,
    .fa_size = CY_BOOT_BOOTLOADER_SIZE
};

static struct flash_area primary_1 =
{
    .fa_id = FLASH_AREA_IMAGE_0,
    .fa_device_id = FLASH_DEVICE_INTERNAL_FLASH,
    .fa_off = CY_FLASH_BASE + CY_BOOT_BOOTLOADER_SIZE,
    .fa_size = CY_BOOT_PRIMARY_1_SIZE
};
static struct flash_area secondary_1 =
{
    .fa_id = FLASH_AREA_IMAGE_1,
    .fa_device_id = FLASH_DEVICE_INTERNAL_FLASH,
    .fa_off = CY_FLASH_BASE +
              CY_BOOT_BOOTLOADER_SIZE +
              CY_BOOT_PRIMARY_1_SIZE,
    .fa_size = CY_BOOT_SECONDARY_1_SIZE
};

static struct flash_area boot_user =
{
    .fa_id = FLASH_AREA_BOOT_USER,
    .fa_device_id = FLASH_DEVICE_INTERNAL_FLASH,
    .fa_off = CY_FLASH_BASE + CY_BOOT_USER_AREA_OFFSET,
    .fa_size = CY_BOOT_USER_AREA_SIZE
};


struct flash_area *boot_area_descs[] =
{
    &bootloader,
    &primary_1,
    &secondary_1,
    &boot_user,
    NULL
};


/* Opens the area for use. id is one of the `fa_id`s */
int flash_area_open(uint8_t id, const struct flash_area **fa)
{
    int ret = -1;
    uint32_t i = 0;

    while(NULL != boot_area_descs[i])
    {
        if(id == boot_area_descs[i]->fa_id)
        {
            *fa = boot_area_descs[i];
            ret = 0;
            break;
        }
        i++;
    }
    return ret;
}

void flash_area_close(const struct flash_area *fa)
{
    (void)fa;/* Nothing to do there */
}


/*
 * This depends on the mappings defined in sysflash.h.
 * MCUBoot uses continuous numbering for the primary slot, the secondary slot,
 * and the scratch while zephyr might number it differently.
 */
int flash_area_id_from_multi_image_slot(int image_index, int slot)
{
    switch (slot) {
    case 0: return FLASH_AREA_IMAGE_0;
    case 1: return FLASH_AREA_IMAGE_1;
    }

    return -1; /* flash_area_open will fail on that */
}

uint8_t flash_area_erased_val(const struct flash_area *fap)
{
    (void)fap;
    return 0xff;
}


/*< Erases `len` bytes of flash memory at `off` */
int flash_area_erase(const struct flash_area *fa, uint32_t off, uint32_t len)
{
    uint32_t erase_start_addr;
    uint32_t erase_end_addr;
    uint32_t page_error = 0;
    HAL_StatusTypeDef rc;
    FLASH_EraseInitTypeDef erase = {0};

    if (len == 0U) {
        return 0;
    }

    if ((off + len) > fa->fa_size) {
        return -1;
    }

    erase_start_addr = fa->fa_off + off;
    erase_end_addr = erase_start_addr + len;

    if ((erase_start_addr % FLASH_PAGE_SIZE) != 0U ||
        (len % FLASH_PAGE_SIZE) != 0U) {
        return -1;
    }

    if (fa->fa_device_id != FLASH_DEVICE_INTERNAL_FLASH) {
        return -1;
    }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    /* Current bootloader layout lives in the lower 2 MB flash window / Bank 1.
     * Revisit this if slot addresses move across bank boundaries. */
    erase.Banks = FLASH_BANK_1;
    erase.Page = (erase_start_addr - FLASH_BASE) / FLASH_PAGE_SIZE;
    erase.NbPages = (erase_end_addr - erase_start_addr) / FLASH_PAGE_SIZE;

    HAL_FLASH_Unlock();
    for (uint32_t p = 0; p < erase.NbPages; p++) {
        FLASH_EraseInitTypeDef single = erase;
        single.Page = erase.Page + p;
        single.NbPages = 1;
        page_error = 0;

        rc = HAL_FLASHEx_Erase(&single, &page_error);
        if (rc != HAL_OK) {
            HAL_FLASH_Lock();
            return -1;
        }

        IWDG_Feed();
    }
    HAL_FLASH_Lock();

    return 0;
}


/*< Returns this `flash_area`s alignment */
uint32_t flash_area_align(const struct flash_area *fa)
{
    (void)fa;
    return FLASH_PROGRAM_UNIT;
}


/*
* Writes `len` bytes of flash memory at `off` from the buffer at `src`
* Uses STM32U5 QUADWORD (128-bit / 16-byte) programming.
* HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, addr, data_addr) expects
* data_addr to be a 32-bit aligned pointer to 16 bytes of data.
 */
int flash_area_write(const struct flash_area *fa, uint32_t off,
                     const void *src, uint32_t len)
{
    uint32_t write_start_addr;
    HAL_StatusTypeDef rc = HAL_OK;
    const uint8_t *bytes = (const uint8_t *)src;

    if ((off + len) > fa->fa_size) {
        return -1;
    }

    if (fa->fa_device_id != FLASH_DEVICE_INTERNAL_FLASH) {
        return -1;
    }

    write_start_addr = fa->fa_off + off;

    if ((write_start_addr % FLASH_PROGRAM_UNIT) != 0U ||
        (len % FLASH_PROGRAM_UNIT) != 0U) {
        return -1;
    }

    HAL_FLASH_Unlock();

    for (uint32_t i = 0; i < len; i += FLASH_PROGRAM_UNIT) {
        uint32_t quadword[4];
        if (flash_program_unit_is_erased(&bytes[i])) {
            continue;
        }

        /* Verify destination is erased before programming.
         * STM32U5 QUADWORD programming requires the target to be in
         * erased state (all 0xFF). If not, programming will raise PROGERR.
         * This can happen after a power loss during a previous write.
         */
        {
            uint8_t dest[FLASH_PROGRAM_UNIT];
            memcpy(dest, (const void *)(write_start_addr + i), FLASH_PROGRAM_UNIT);
            if (!flash_program_unit_is_erased(dest)) {
                rc = HAL_ERROR;
                break;
            }
        }

        memcpy(quadword, &bytes[i], FLASH_PROGRAM_UNIT);
        rc = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
                               write_start_addr + i,
                               (uint32_t)quadword);
        if (rc != HAL_OK) {
            break;
        }
    }

    HAL_FLASH_Lock();

    return (rc == HAL_OK) ? 0 : -1;
}

/*
* Reads `len` bytes of flash memory at `off` to the buffer at `dst`
*/
int flash_area_read(const struct flash_area *fa, uint32_t off, void *dst,
                     uint32_t len)
{
    uint32_t addr;

    if ((off + len) > fa->fa_size) {
        return -1;
    }

    if (fa->fa_device_id != FLASH_DEVICE_INTERNAL_FLASH) {
        return -1;
    }

    addr = fa->fa_off + off;
    memcpy(dst, (const void *)addr, len);
    return 0;
}

int flash_area_get_sector(const struct flash_area *fa, uint32_t off,
                          struct flash_sector *sector)
{
    if (off >= fa->fa_size) {
        return -1;
    }

    sector->fs_off = (off / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
    sector->fs_size = FLASH_PAGE_SIZE;

    return 0;
}
