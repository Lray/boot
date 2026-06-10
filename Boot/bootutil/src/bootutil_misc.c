#include "bootutil/bootutil.h"
#include "bootutil_misc.h"
#include "bootutil_priv.h"

uint32_t bootutil_max_image_size(struct boot_loader_state *state, const struct flash_area *fap)
{
    (void)state;
    return boot_swap_info_off(fap);
}

/**
 * Clears the boot state, so that previous operations have no effect on new
 * ones.
 *
 * @param state                 The state that should be cleared.
 */
void boot_state_clear(struct boot_loader_state *state)
{
    (void)state;
}
