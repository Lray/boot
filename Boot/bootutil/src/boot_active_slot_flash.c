#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bootutil/boot_active_slot_flash.h"
#include "bootutil/boot_public.h"
#include "flash_map_backend.h"
#include "sysflash.h"

#define BOOT_ACTIVE_SLOT_MAGIC 0x41534C54U



typedef struct {
    uint32_t magic;
    uint8_t active_slot;
    uint8_t reserved0;
    uint16_t reserved;
    uint32_t checksum;
    uint32_t reserved2;
} boot_active_slot_record_t;



static uint32_t
boot_active_slot_checksum(uint8_t active_slot)
{
    return BOOT_ACTIVE_SLOT_MAGIC ^ (uint32_t)active_slot ^ 0x5A5AA5A5U;
}

static bool
boot_active_slot_record_valid(const boot_active_slot_record_t *record)
{
    if (record->magic != BOOT_ACTIVE_SLOT_MAGIC) {
        return false;
    }

    if (record->active_slot != BOOT_SLOT_PRIMARY &&
        record->active_slot != BOOT_SLOT_SECONDARY) {
        return false;
    }

    if (record->reserved0 != 0xffU ||
        record->reserved != 0xffffU ||
        record->reserved2 != 0xffffffffU) {
        return false;
    }

    if (record->checksum != boot_active_slot_checksum(record->active_slot)) {
        return false;
    }

    return true;
}

bool
boot_active_slot_read(uint8_t *active_slot)
{
    const struct flash_area *fa;
    boot_active_slot_record_t record;

    if (active_slot == NULL) {
        return false;
    }

    if (flash_area_open(FLASH_AREA_BOOT_USER, &fa) != 0) {
        return false;
    }

    if (flash_area_read(fa, 0, &record, sizeof(record)) != 0) {
        flash_area_close(fa);
        return false;
    }

    flash_area_close(fa);

    if (!boot_active_slot_record_valid(&record)) {
        return false;
    }

    *active_slot = record.active_slot;
    return true;
}

boot_active_slot_write_result_t
boot_active_slot_write(uint8_t active_slot)
{
    const struct flash_area *fa;
    boot_active_slot_record_t record;
    boot_active_slot_record_t verify;
    boot_active_slot_write_result_t result = BOOT_ACTIVE_SLOT_WRITE_OK;

    if (active_slot != BOOT_SLOT_PRIMARY && active_slot != BOOT_SLOT_SECONDARY) {
        return BOOT_ACTIVE_SLOT_WRITE_BAD_SLOT;
    }

    if (flash_area_open(FLASH_AREA_BOOT_USER, &fa) != 0) {
        return BOOT_ACTIVE_SLOT_WRITE_OPEN_FAILED;
    }


    memset(&record, 0xff, sizeof(record));
    record.magic = BOOT_ACTIVE_SLOT_MAGIC;
    record.active_slot = active_slot;
    record.checksum = boot_active_slot_checksum(active_slot);

    if (flash_area_erase(fa, 0, flash_area_get_size(fa)) != 0) {
        result = BOOT_ACTIVE_SLOT_WRITE_ERASE_FAILED;
        goto close;
    }

    if (flash_area_write(fa, 0, &record, sizeof(record)) != 0) {
        result = BOOT_ACTIVE_SLOT_WRITE_PROGRAM_FAILED;
        goto close;
    }

    if (flash_area_read(fa, 0, &verify, sizeof(verify)) != 0 ||
        !boot_active_slot_record_valid(&verify) ||
        verify.active_slot != active_slot) {
        result = BOOT_ACTIVE_SLOT_WRITE_READBACK_FAILED;
    }

close:
    flash_area_close(fa);
    return result;
}
