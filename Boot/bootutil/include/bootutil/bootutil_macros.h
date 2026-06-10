/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 */

#ifndef H_BOOTUTIL_MACROS
#define H_BOOTUTIL_MACROS

#ifndef ALIGN_UP
#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))
#endif

#ifndef ALIGN_DOWN
#define ALIGN_DOWN(num, align)  ((num) & ~((align) - 1))
#endif

#ifndef STRUCT_PACKED
#if defined(__clang__) && defined(__ARMCC_VERSION)
/* ARMCLANG (Keil V6) uses __attribute__((packed)), not __packed keyword */
#define STRUCT_PACKED struct __attribute__((packed))
#elif defined(__CC_ARM) || defined(__ARMCC_VERSION)
#define STRUCT_PACKED __packed struct
#elif defined(__GNUC__)
#define STRUCT_PACKED struct __attribute__((packed))
#else
#define STRUCT_PACKED struct
#endif
#endif

#endif

