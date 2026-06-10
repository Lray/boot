#ifndef H_BOOT_JUMP_H
#define H_BOOT_JUMP_H

#include <stdbool.h>
#include <stdint.h>

#define BOOT_JUMP_VTOR_ALIGN 128U

static inline bool boot_jump_addr_in_range(uint32_t addr, uint32_t base, uint32_t size)
{
    if (size == 0U || base > (UINT32_MAX - size)) {
        return false;
    }

    return addr >= base && addr < (base + size);
}

static inline bool boot_jump_sram_addr_valid(uint32_t addr)
{
#if defined(SRAM1_BASE) && defined(SRAM1_SIZE)
    if (boot_jump_addr_in_range(addr, SRAM1_BASE, SRAM1_SIZE)) {
        return true;
    }
#endif
#if defined(SRAM2_BASE) && defined(SRAM2_SIZE)
    if (boot_jump_addr_in_range(addr, SRAM2_BASE, SRAM2_SIZE)) {
        return true;
    }
#endif
#if defined(SRAM3_BASE) && defined(SRAM3_SIZE)
    if (boot_jump_addr_in_range(addr, SRAM3_BASE, SRAM3_SIZE)) {
        return true;
    }
#endif
#if defined(SRAM4_BASE) && defined(SRAM4_SIZE)
    if (boot_jump_addr_in_range(addr, SRAM4_BASE, SRAM4_SIZE)) {
        return true;
    }
#endif
#if defined(SRAM5_BASE) && defined(SRAM5_SIZE)
    if (boot_jump_addr_in_range(addr, SRAM5_BASE, SRAM5_SIZE)) {
        return true;
    }
#endif

    return false;
}

static inline bool boot_jump_vectors_valid(uint32_t app_addr,
                                           uint32_t image_size,
                                           uint32_t sp,
                                           uint32_t reset)
{
    uint32_t reset_addr;

    if ((app_addr & (BOOT_JUMP_VTOR_ALIGN - 1U)) != 0U) {
        return false;
    }

    if (image_size < 8U || app_addr > (UINT32_MAX - image_size)) {
        return false;
    }

    if ((sp & 0x7U) != 0U || !boot_jump_sram_addr_valid(sp)) {
        return false;
    }

    if ((reset & 1U) == 0U) {
        return false;
    }

    reset_addr = reset & ~1U;
    if (reset_addr < app_addr || reset_addr >= (app_addr + image_size)) {
        return false;
    }

    return true;
}

#endif /* H_BOOT_JUMP_H */
