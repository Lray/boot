/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bootutil/security_cnt.h"

#include <stdint.h>
#include <string.h>

#include "flash_map_backend.h"
#include "sysflash.h"

#define BOOT_SECURITY_COUNTER_MAGIC 0x53454356U
#define BOOT_SECURITY_COUNTER_FORMAT 1U

typedef struct {
    uint32_t magic;
    uint16_t format;
    uint16_t record_size;
    uint32_t sequence;
    uint32_t security_counter;
    uint32_t flags;
    uint32_t checksum;
    uint32_t reserved[10];
} boot_security_counter_record_t;

typedef struct {
    uint8_t area_id;
    boot_security_counter_record_t record;
    uint8_t valid;
} boot_security_counter_area_record_t;

static uint32_t
boot_security_counter_checksum(const boot_security_counter_record_t *record)
{
    return record->magic ^ record->format ^ record->record_size ^
           record->sequence ^ record->security_counter ^
           record->flags ^ 0x5A5AA5A5U;
}

static int
boot_security_counter_reserved_valid(const boot_security_counter_record_t *record)
{
    uint32_t i;

    for (i = 0U; i < (sizeof(record->reserved) / sizeof(record->reserved[0])); i++) {
        if (record->reserved[i] != 0xFFFFFFFFU) {
            return 0;
        }
    }
    return 1;
}

static int
boot_security_counter_record_valid(const boot_security_counter_record_t *record)
{
    if (record->magic != BOOT_SECURITY_COUNTER_MAGIC) {
        return 0;
    }
    if (record->format != BOOT_SECURITY_COUNTER_FORMAT) {
        return 0;
    }
    if (record->record_size != sizeof(*record)) {
        return 0;
    }
    if (!boot_security_counter_reserved_valid(record)) {
        return 0;
    }
    return record->checksum == boot_security_counter_checksum(record);
}

static int
boot_security_counter_load_area(uint8_t area_id,
                                boot_security_counter_area_record_t *area_record)
{
    const struct flash_area *fa;
    int rc;

    rc = flash_area_open(area_id, &fa);
    if (rc != 0) {
        return -1;
    }

    memset(area_record, 0, sizeof(*area_record));
    area_record->area_id = area_id;
    rc = flash_area_read(fa, 0U, &area_record->record,
                         sizeof(area_record->record));
    flash_area_close(fa);
    if (rc != 0) {
        return -1;
    }

    area_record->valid =
        boot_security_counter_record_valid(&area_record->record) ? 1U : 0U;
    return 0;
}

static int
boot_security_counter_read(uint32_t *security_counter, uint8_t *active_area_id)
{
    boot_security_counter_area_record_t area0;
    boot_security_counter_area_record_t area1;
    const boot_security_counter_area_record_t *selected;
    int rc;

    if (security_counter == NULL) {
        return -1;
    }

    rc = boot_security_counter_load_area(FLASH_AREA_BOOT_SECURITY_STATE_0, &area0);
    if (rc != 0) {
        return rc;
    }
    rc = boot_security_counter_load_area(FLASH_AREA_BOOT_SECURITY_STATE_1, &area1);
    if (rc != 0) {
        return rc;
    }

    if (!area0.valid && !area1.valid) {
        return -1;
    }
    if (area0.valid && area1.valid) {
        selected = (area1.record.sequence > area0.record.sequence) ? &area1 : &area0;
    } else {
        selected = area0.valid ? &area0 : &area1;
    }

    *security_counter = selected->record.security_counter;
    if (active_area_id != NULL) {
        *active_area_id = selected->area_id;
    }
    return 0;
}

static void
boot_security_counter_prepare_record(boot_security_counter_record_t *record,
                                     uint32_t sequence,
                                     uint32_t security_counter,
                                     uint32_t flags)
{
    memset(record, 0xFF, sizeof(*record));
    record->magic = BOOT_SECURITY_COUNTER_MAGIC;
    record->format = BOOT_SECURITY_COUNTER_FORMAT;
    record->record_size = sizeof(*record);
    record->sequence = sequence;
    record->security_counter = security_counter;
    record->flags = flags;
    record->checksum = boot_security_counter_checksum(record);
}

static int
boot_security_counter_write_area(uint8_t area_id,
                                 const boot_security_counter_record_t *record)
{
    const struct flash_area *fa;
    boot_security_counter_record_t verify;
    int rc;

    rc = flash_area_open(area_id, &fa);
    if (rc != 0) {
        return -1;
    }

    rc = flash_area_erase(fa, 0U, CY_BOOT_SECURITY_STATE_SIZE);
    if (rc == 0) {
        rc = flash_area_write(fa, 0U, record, sizeof(*record));
    }
    if (rc == 0) {
        rc = flash_area_read(fa, 0U, &verify, sizeof(verify));
    }
    flash_area_close(fa);

    if (rc != 0) {
        return -1;
    }
    if (!boot_security_counter_record_valid(&verify) ||
        memcmp(&verify, record, sizeof(verify)) != 0) {
        return -1;
    }
    return 0;
}

fih_ret
boot_nv_security_counter_init(void)
{
    return FIH_SUCCESS;
}

fih_ret
boot_nv_security_counter_get(uint32_t image_id, fih_int *security_cnt)
{
    uint32_t counter;
    int rc;

    (void)image_id;

    if (security_cnt == NULL) {
        return FIH_FAILURE;
    }

    rc = boot_security_counter_read(&counter, NULL);
    if (rc != 0) {
        return FIH_FAILURE;
    }

    *security_cnt = (fih_int)counter;
    return FIH_SUCCESS;
}

int32_t
boot_nv_security_counter_update(uint32_t image_id, uint32_t img_security_cnt)
{
    boot_security_counter_area_record_t area0;
    boot_security_counter_area_record_t area1;
    const boot_security_counter_area_record_t *selected;
    boot_security_counter_record_t next_record;
    uint8_t active_area_id;
    uint8_t inactive_area_id;
    uint32_t current_counter;
    int rc;

    (void)image_id;

    rc = boot_security_counter_read(&current_counter, &active_area_id);
    if (rc != 0) {
        return -1;
    }

    if (img_security_cnt < current_counter) {
        return -1;
    }
    if (img_security_cnt == current_counter) {
        return 0;
    }

    rc = boot_security_counter_load_area(FLASH_AREA_BOOT_SECURITY_STATE_0, &area0);
    if (rc != 0) {
        return -1;
    }
    rc = boot_security_counter_load_area(FLASH_AREA_BOOT_SECURITY_STATE_1, &area1);
    if (rc != 0) {
        return -1;
    }
    selected = (active_area_id == FLASH_AREA_BOOT_SECURITY_STATE_0) ? &area0 : &area1;
    inactive_area_id = (active_area_id == FLASH_AREA_BOOT_SECURITY_STATE_0) ?
        FLASH_AREA_BOOT_SECURITY_STATE_1 : FLASH_AREA_BOOT_SECURITY_STATE_0;

    boot_security_counter_prepare_record(&next_record,
                                         selected->record.sequence + 1U,
                                         img_security_cnt,
                                         selected->record.flags);

    return boot_security_counter_write_area(inactive_area_id, &next_record);
}

fih_ret
boot_nv_security_counter_is_update_possible(uint32_t image_id,
                                            uint32_t img_security_cnt)
{
    (void)image_id;
    (void)img_security_cnt;

    return FIH_SUCCESS;
}

#ifdef MCUBOOT_HW_ROLLBACK_PROT_LOCK
int32_t
boot_nv_security_counter_lock(uint32_t image_id)
{
    (void)image_id;
    return 0;
}
#endif
