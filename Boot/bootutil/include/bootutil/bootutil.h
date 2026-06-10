#ifndef H_BOOTUTIL_H
#define H_BOOTUTIL_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>



#define BOOT_IMAGE_NUMBER 1


struct image_header;
struct boot_loader_state;
struct flash_area;
/**
 * A response object provided by the boot loader code; indicates where to jump
 * to execute the main image.
 */
struct boot_rsp {
    /** A pointer to the header of the image to be executed. */
    const struct image_header *br_hdr;

    /**
     * The flash offset of the image to execute.  Indicates the position of
     * the image header within its flash device.
     */
    uint8_t br_flash_dev_id;
    uint32_t br_image_off;
};

int boot_go(struct boot_rsp *rsp);
void boot_state_init(struct boot_loader_state *state);
int context_boot_go(struct boot_loader_state *state, struct boot_rsp *rsp);
void boot_state_clear(struct boot_loader_state *state);


#endif /* H_BOOTUTIL_H */
