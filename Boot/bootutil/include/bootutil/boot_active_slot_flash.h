#ifndef H_BOOT_ACTIVE_SLOT_FLASH_H
#define H_BOOT_ACTIVE_SLOT_FLASH_H

#include <stdbool.h>
#include <stdint.h>

bool boot_active_slot_read(uint8_t *active_slot);
void boot_active_slot_write(uint8_t active_slot);

#endif /* H_BOOT_ACTIVE_SLOT_FLASH_H */
