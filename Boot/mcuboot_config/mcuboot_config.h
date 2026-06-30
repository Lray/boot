/* Copyright 2019 Cypress Semiconductor Corporation
 *
 * Copyright (c) 2018 Open Source Foundries Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef MCUBOOT_CONFIG_H
#define MCUBOOT_CONFIG_H

#define MCUBOOT_HAVE_LOGGING 1

#define MCUBOOT_DIRECT_XIP 1
#define MCUBOOT_DIRECT_XIP_REVERT 1
#define MCUBOOT_BOOT_MAX_ALIGN 16
#define MCUBOOT_HW_ROLLBACK_PROT 1

#define MCUBOOT_SIGN_EC256 1
#define MCUBOOT_USE_TINYCRYPT 1

 #define MCUBOOT_SHA256_BACKEND_STM32_HASH 1
 #define MCUBOOT_ECDSA_BACKEND_STM32U5_PKA 1

#ifndef MCUBOOT_SHA256_BACKEND_STM32_HASH
#define MCUBOOT_SHA256_BACKEND_TINYCRYPT 1
#endif

#ifndef MCUBOOT_ECDSA_BACKEND_STM32U5_PKA
#define MCUBOOT_ECDSA_BACKEND_TINYCRYPT 1
#endif

#if defined(MCUBOOT_SHA256_BACKEND_STM32_HASH) && defined(MCUBOOT_SHA256_BACKEND_TINYCRYPT)
#error "Select exactly one SHA-256 backend"
#endif

#if (defined(MCUBOOT_ECDSA_BACKEND_STM32U5_PKA) + \
     defined(MCUBOOT_ECDSA_BACKEND_TINYCRYPT)) != 1
#error "Select exactly one ECDSA backend"
#endif

#endif /* MCUBOOT_CONFIG_H */
