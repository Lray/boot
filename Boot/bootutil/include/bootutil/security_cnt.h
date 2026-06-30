/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __SECURITY_CNT_H__
#define __SECURITY_CNT_H__

#include <stdint.h>

#include "bootutil/fault_injection_hardening.h"

#ifdef __cplusplus
extern "C" {
#endif

fih_ret boot_nv_security_counter_init(void);
fih_ret boot_nv_security_counter_get(uint32_t image_id, fih_int *security_cnt);
int32_t boot_nv_security_counter_update(uint32_t image_id,
                                        uint32_t img_security_cnt);
fih_ret boot_nv_security_counter_is_update_possible(uint32_t image_id,
                                                    uint32_t img_security_cnt);

#ifdef MCUBOOT_HW_ROLLBACK_PROT_LOCK
int32_t boot_nv_security_counter_lock(uint32_t image_id);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __SECURITY_CNT_H__ */
