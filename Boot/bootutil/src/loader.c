#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "stm32u5xx_hal.h"
#include "boot_area.h"
#include "bootutil/bootutil.h"
#include "bootutil_loader.h"
#include "bootutil_priv.h"
#include "bootutil/bootutil_log.h"
#include "mcuboot_config.h"
#ifdef MCUBOOT_HW_ROLLBACK_PROT
#include "bootutil/security_cnt.h"
#endif
#include "boot_jump.h"
#include "sysflash.h"
// #include "boot_public.h"

static struct boot_loader_state boot_data;

static int
boot_update_hw_rollback_protection(struct boot_loader_state *state)
{
#ifdef MCUBOOT_HW_ROLLBACK_PROT
    int rc;

    /* Update the stored security counter with the newer (active) image's
     * security counter value.
     */
#if defined(MCUBOOT_DIRECT_XIP) && defined(MCUBOOT_DIRECT_XIP_REVERT)
    /* When the 'revert' mechanism is enabled in direct-xip mode, the
     * security counter can be increased only after reboot, if the image
     * has been confirmed at runtime (the image_ok flag has been set).
     * This way a 'revert' can be performed when it's necessary.
     */
    if (state->slot_usage[BOOT_CURR_IMG(state)].swap_state.image_ok == BOOT_FLAG_SET) {
#endif
        rc = boot_update_security_counter(state,
                                          state->slot_usage[BOOT_CURR_IMG(state)].active_slot,
                                          state->slot_usage[BOOT_CURR_IMG(state)].active_slot);
        if (rc != 0) {
            BOOT_LOG_ERR("Security counter update failed after image %d validation: %d",
                         BOOT_CURR_IMG(state), rc);
            return rc;
        }

#ifdef MCUBOOT_HW_ROLLBACK_PROT_LOCK
        rc = boot_nv_security_counter_lock(BOOT_CURR_IMG(state));
        if (rc != 0) {
            BOOT_LOG_ERR("Security counter lock failed after image %d validation: %d",
                         BOOT_CURR_IMG(state), rc);
            return rc;
        }
#endif /* MCUBOOT_HW_ROLLBACK_PROT_LOCK */
#if defined(MCUBOOT_DIRECT_XIP) && defined(MCUBOOT_DIRECT_XIP_REVERT)
    }
#endif

    return 0;
#else
    (void)state;
    return 0;
#endif
}

/*
 * Check that there is a valid image in a slot
 *
 * @returns
 *         FIH_SUCCESS                      if image was successfully validated
 *         FIH_NO_BOOTABLE_IMAGE            if no bootloable image was found
 *         FIH_FAILURE                      on any errors
 */
static int
boot_validate_slot(struct boot_loader_state *state, int slot,
                   struct boot_status *bs, int expected_swap_type)
{
    const struct flash_area *fap;
    struct image_header *hdr;
    int fih_rc = -1;

    BOOT_LOG_DBG("boot_validate_slot: slot %d, expected_swap_type %d",
                 slot, expected_swap_type);

    fap = BOOT_IMG_AREA(state, slot);
    assert(fap != NULL);

    hdr = boot_img_hdr(state, slot);
    if (boot_check_header_erased(state, slot) || (hdr->ih_flags & IMAGE_F_NON_BOOTABLE))
    {

        /* No bootable image in slot; continue booting from the primary slot. */
        fih_rc = 1;
        goto out;
    }

    if (!boot_check_header_valid(state, slot))
    {
        BOOT_LOG_DBG("boot_validate_slot: header validation failed %d", slot);
        fih_rc = FIH_FAILURE;
    }
    else
    {

        fih_rc = boot_check_image(state, bs, slot);
    }

    if (fih_rc != 0)
    {

        BOOT_LOG_ERR("Image in the %s slot is not valid!",
                     (slot == BOOT_SLOT_PRIMARY) ? "primary" : "secondary");

        if (slot != BOOT_SLOT_PRIMARY)
        {
            boot_scramble_slot(fap, slot);
            /* Image is invalid, erase it to prevent further unnecessary
             * attempts to validate and boot it.
             */
        }
        fih_rc = 1;
        goto out;
    }

out:
    return fih_rc;
}

static int boot_get_slot_usage(struct boot_loader_state *state)
{
    uint32_t slot;
    int rc;
    struct image_header *hdr = NULL;

    /* Attempt to read an image header from each slot. */
    rc = boot_read_image_headers(state, false, NULL);
    if (rc != 0)
    {
        BOOT_LOG_WRN("Failed reading image headers.");
        return rc;
    }

    /* Check headers in all slots */
    for (slot = 0; slot < BOOT_NUM_SLOTS; slot++)
    {
        hdr = boot_img_hdr(state, slot);

        if (boot_check_header_valid(state, slot))
        {
            state->slot_usage[BOOT_CURR_IMG(state)].slot_available[slot] = true;
            BOOT_LOG_IMAGE_INFO(slot, hdr);
        }
        else
        {
            state->slot_usage[BOOT_CURR_IMG(state)].slot_available[slot] = false;
            BOOT_LOG_INF("Image %d %s slot: Image not found",
                         BOOT_CURR_IMG(state),
                         (slot == BOOT_SLOT_PRIMARY)
                             ? "Primary"
                             : "Secondary");
        }
    }

    state->slot_usage[BOOT_CURR_IMG(state)].active_slot = BOOT_SLOT_NONE;
    return 0;
}

/**
 * Finds the slot containing the image with the highest version number for the
 * current image.
 *
 * @param  state        Boot loader status information.
 *
 * @return              BOOT_SLOT_NONE if no available slot found, number of
 *                      the found slot otherwise.
 */
static uint32_t
find_slot_with_highest_version(struct boot_loader_state *state)
{
    uint32_t slot;
    uint32_t candidate_slot = BOOT_SLOT_NONE;
    int rc;

    for (slot = 0; slot < BOOT_NUM_SLOTS; slot++)
    {
        if (state->slot_usage[BOOT_CURR_IMG(state)].slot_available[slot])
        {
            if (candidate_slot == BOOT_SLOT_NONE)
            {
                candidate_slot = slot;
            }
            else
            {
                rc = boot_compare_version(
                    &boot_img_hdr(state, slot)->ih_ver,
                    &boot_img_hdr(state, candidate_slot)->ih_ver);
                if (rc == 1)
                {
                    /* The version of the image being examined is greater than
                     * the version of the current candidate.
                     */
                    candidate_slot = slot;
                }
            }
        }
    }

    return candidate_slot;
}

/**
 * Checks whether the active slot of the current image was previously selected
 * to run. Erases the image if it was selected but its execution failed,
 * otherwise marks it as selected if it has not been before.
 *
 * @param  state        Boot loader status information.
 *
 * @return              0 on success; nonzero on failure.
 */
#if defined(MCUBOOT_DIRECT_XIP_REVERT) || defined(MCUBOOT_RAM_LOAD_REVERT)
static int
boot_select_or_erase(struct boot_loader_state *state)
{
    const struct flash_area *fap = NULL;
    int rc;
    uint32_t active_slot;
    struct boot_swap_state *active_swap_state;

    active_slot = state->slot_usage[BOOT_CURR_IMG(state)].active_slot;

    fap = BOOT_IMG_AREA(state, active_slot);
    assert(fap != NULL);

    active_swap_state = &(state->slot_usage[BOOT_CURR_IMG(state)].swap_state);

    memset(active_swap_state, 0, sizeof(struct boot_swap_state));
    rc = boot_read_swap_state(fap, active_swap_state);
    assert(rc == 0);
    BOOT_LOG_INF("Slot %lu trailer: magic=%u copy_done=%u image_ok=%u",
                 (unsigned long)active_slot,
                 active_swap_state->magic,
                 active_swap_state->copy_done,
                 active_swap_state->image_ok);

    if (active_swap_state->magic != BOOT_MAGIC_GOOD ||
        (active_swap_state->copy_done == BOOT_FLAG_SET &&
         active_swap_state->image_ok != BOOT_FLAG_SET))
    {
        /*
         * A reboot happened without the image being confirmed at
         * runtime or its trailer is corrupted/invalid. Erase the image
         * to prevent it from being selected again on the next reboot.
         */
        BOOT_LOG_DBG("Erasing faulty image in the %s slot.",
                     (active_slot == BOOT_SLOT_PRIMARY) ? "primary" : "secondary");
        rc = boot_scramble_region(fap, 0, flash_area_get_size(fap), false);
        assert(rc == 0);
        rc = -1;
    }
    else
    {
        if (active_swap_state->copy_done != BOOT_FLAG_SET)
        {
            if (active_swap_state->copy_done == BOOT_FLAG_BAD)
            {
                BOOT_LOG_DBG("The copy_done flag had an unexpected value. Its "
                             "value was neither 'set' nor 'unset', but 'bad'.");
            }
            /*
             * Set the copy_done flag, indicating that the image has been
             * selected to boot. It can be set in advance, before even
             * validating the image, because in case the validation fails, the
             * entire image slot will be erased (including the trailer).
             */
            rc = boot_write_copy_done(fap);
            if (rc != 0)
            {
                /*
                 * Writing copy_done failed. This can happen when the flash
                 * destination is not in erased state, typically after a
                 * power loss during a previous copy_done write left the
                 * QUADWORD partially programmed.
                 *
                 * Erase the entire slot to recover. The slot will not be
                 * selected again, and can be re-populated via YMODEM.
                 */
                BOOT_LOG_WRN("Failed to set copy_done flag of the image in "
                             "the %s slot, erasing slot to recover.",
                             (active_slot == BOOT_SLOT_PRIMARY) ? "primary" : "secondary");
                rc = boot_scramble_region(fap, 0, flash_area_get_size(fap), false);
                assert(rc == 0);
                rc = -1;
            }
        }
    }

    return rc;
}
#endif

/**
 * Tries to load a slot for all the images with validation.
 *
 * @param  state        Boot loader status information.
 *
 * @return              0 on success; nonzero on failure.
 */
int boot_load_and_validate_images(struct boot_loader_state *state)
{
    uint32_t active_slot;
    int rc;
    int fih_rc;

    /* Go over all the images and try to load one */

    /* All slots tried until a valid image found. Breaking from this loop
     * means that a valid image found or already loaded. If no slot is
     * found the function returns with error code. */
    while (true)
    {
        /* Go over all the slots and try to load one */
        active_slot = state->slot_usage[BOOT_CURR_IMG(state)].active_slot;
        if (active_slot != BOOT_SLOT_NONE)
        {
            /* A slot is already active, go to next image. */
            break;
        }

        active_slot = find_slot_with_highest_version(state);

        if (active_slot == BOOT_SLOT_NONE)
        {
            BOOT_LOG_INF("No slot to load for image %d",
                         BOOT_CURR_IMG(state));
            return FIH_FAILURE;
        }

        /* Save the number of the active slot. */
        state->slot_usage[BOOT_CURR_IMG(state)].active_slot = active_slot;
        BOOT_LOG_INF("Selected slot %lu for validation", (unsigned long)active_slot);

#if defined(MCUBOOT_DIRECT_XIP_REVERT) || defined(MCUBOOT_RAM_LOAD_REVERT)
        rc = boot_select_or_erase(state);
        BOOT_LOG_INF("select_or_erase rc=%d", rc);

        if (rc != 0)
        {
            /* The selected image slot has been erased. */
            state->slot_usage[BOOT_CURR_IMG(state)].slot_available[active_slot] = false;
            state->slot_usage[BOOT_CURR_IMG(state)].active_slot = BOOT_SLOT_NONE;
            continue;
        }
#endif

        fih_rc = boot_validate_slot(state, active_slot, NULL, 0);
        BOOT_LOG_INF("validate slot %lu rc=%d", (unsigned long)active_slot, fih_rc);

        if (fih_rc != 0)
        {
            /* Image is invalid. */

            state->slot_usage[BOOT_CURR_IMG(state)].slot_available[active_slot] = false;
            state->slot_usage[BOOT_CURR_IMG(state)].active_slot = BOOT_SLOT_NONE;
            continue;
        }

        rc = boot_update_hw_rollback_protection(state);
        if (rc != 0)
        {
            state->slot_usage[BOOT_CURR_IMG(state)].slot_available[active_slot] = false;
            state->slot_usage[BOOT_CURR_IMG(state)].active_slot = BOOT_SLOT_NONE;
            continue;
        }

        /* Valid image loaded from a slot, go to next image. */
        break;
    }

    return 0;
}

/**
 * Fills rsp to indicate how booting should occur.
 *
 * @param  state        Boot loader status information.
 * @param  rsp          boot_rsp struct to fill.
 */
static void
fill_rsp(struct boot_loader_state *state, struct boot_rsp *rsp)
{
    uint32_t active_slot;

    active_slot = state->slot_usage[BOOT_CURR_IMG(state)].active_slot;

    rsp->br_flash_dev_id = flash_area_get_device_id(BOOT_IMG_AREA(state, active_slot));
    rsp->br_image_off = boot_img_slot_off(state, active_slot);
    rsp->br_hdr = boot_img_hdr(state, active_slot);
}

#ifdef MCUBOOT_HAVE_LOGGING
/**
 * Prints the state of the loaded images.
 *
 * @param  state        Boot loader status information.
 */
static void
print_loaded_images(struct boot_loader_state *state)
{
    uint32_t active_slot;

    (void)state;

    active_slot = state->slot_usage[BOOT_CURR_IMG(state)].active_slot;

    BOOT_LOG_INF("Image %d loaded from the %s slot",
                 BOOT_CURR_IMG(state),
                 (active_slot == BOOT_SLOT_PRIMARY) ? "primary" : "secondary");
}
#endif

int context_boot_go(struct boot_loader_state *state, struct boot_rsp *rsp)
{
    int rc;
    int fih_rc = -1;

    rc = boot_open_all_flash_areas(state);
    if (rc != 0)
    {
        goto out;
    }

    rc = boot_get_slot_usage(state);
    if (rc != 0)
    {
        goto close;
    }

    fih_rc = boot_load_and_validate_images(state);
    if (fih_rc != 0)
    {
        fih_rc = -1;
        goto close;
    }
#ifdef MCUBOOT_HAVE_LOGGING
    /* All image loaded successfully. */
    print_loaded_images(state);
#endif
    fill_rsp(state, rsp);

close:
    boot_close_all_flash_areas(state);

out:
    if (rc != 0)
    {
        fih_rc = -1;
    }

    return fih_rc;
}

/**
 * Prepares the booting process. This function moves images around in flash as
 * appropriate, and tells you what address to boot from.
 *
 * @param rsp                   On success, indicates how booting should occur.
 *
 * @return                      FIH_SUCCESS on success; nonzero on failure.
 */

int boot_go(struct boot_rsp *rsp)
{
    int fih_rc = -1;

    boot_state_init(&boot_data);

    fih_rc = context_boot_go(&boot_data, rsp);

    boot_state_clear(&boot_data);

    return fih_rc;
}

/*
 * Transfer control without executing a C function epilogue after MSP changes.
 * The AAPCS passes app_sp in r0 and app_reset in r1.
 */
__attribute__((naked, noreturn))
static void boot_jump_transfer(uint32_t app_sp __attribute__((unused)),
                               uint32_t app_reset __attribute__((unused)))
{
    __asm volatile(
        "msr msp, r0\n"
        "isb\n"
        "cpsie i\n"
        "bx r1\n");
}

void do_boot(struct boot_rsp *rsp)
{
    uint32_t app_addr = 0;
    uint32_t sp;
    uint32_t reset;
    uint32_t image_size;

    if (rsp == NULL || rsp->br_hdr == NULL)
    {
        BOOT_LOG_ERR("Invalid boot response");
        return;
    }

    app_addr = (rsp->br_image_off + rsp->br_hdr->ih_hdr_size);
    image_size = rsp->br_hdr->ih_img_size;

    sp = *(__IO uint32_t *)app_addr;
    reset = *(__IO uint32_t *)(app_addr + 4U);

    if (!boot_jump_vectors_valid(app_addr, image_size, sp, reset))
    {
        BOOT_LOG_ERR("Invalid APP vectors: APP=0x%08lx SP=0x%08lx Reset=0x%08lx",
                     app_addr, sp, reset);
        return;
    }

    BOOT_LOG_INF("Starting User Application on Cortex-M33...");
    BOOT_LOG_INF("Start Address: 0x%08lx", app_addr);
    BOOT_LOG_INF("Deinitializing hardware...");
    BOOT_LOG_INF("SP:0x%08lx", sp);
    BOOT_LOG_INF("Reset:0x%08lx", reset);
    __disable_irq();
    SysTick->CTRL = 0X00;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

    /* STM32U5A9 has 139 external IRQs, so eight NVIC register words cover all of them. */
    for (uint32_t i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    HAL_DeInit();

    __set_BASEPRI(0U);
    __set_FAULTMASK(0U);
    __set_PSP(0U);
    __set_CONTROL(0U);
    __set_MSPLIM(0U);
    __set_PSPLIM(0U);

    SCB->VTOR = app_addr;
    __DSB();
    __ISB();
    boot_jump_transfer(sp, reset);
}
