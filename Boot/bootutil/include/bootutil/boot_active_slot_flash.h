#ifndef H_BOOT_ACTIVE_SLOT_FLASH_H
#define H_BOOT_ACTIVE_SLOT_FLASH_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BOOT_ACTIVE_SLOT_WRITE_OK = 0,
    BOOT_ACTIVE_SLOT_WRITE_BAD_SLOT,
    BOOT_ACTIVE_SLOT_WRITE_OPEN_FAILED,
    BOOT_ACTIVE_SLOT_WRITE_ERASE_FAILED,
    BOOT_ACTIVE_SLOT_WRITE_PROGRAM_FAILED,
    BOOT_ACTIVE_SLOT_WRITE_READBACK_FAILED,
} boot_active_slot_write_result_t;

bool boot_active_slot_read(uint8_t *active_slot);
boot_active_slot_write_result_t boot_active_slot_write(uint8_t active_slot);

#endif /* H_BOOT_ACTIVE_SLOT_FLASH_H */
