/**
 * @file tuya_mem.h
 * @brief Tuya memory allocation compatibility layer for gnuboy
 *
 * Redirects standard malloc/free to Tuya's TKL APIs
 * Note: realloc is not used - we check file size and malloc at once instead
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef TUYA_MEM_H
#define TUYA_MEM_H

#include "tuya_cloud_types.h"
#include "tal_api.h"

// Redefine malloc/free to use Tuya's TKL APIs
// This must be included before any gnuboy files that use malloc/free

#ifdef malloc
#undef malloc
#endif
#define malloc(size) tal_malloc(size)

#ifdef free
#undef free
#endif
#define free(ptr) tal_free(ptr)

// realloc is not used - we check file size and malloc at once instead
// If realloc is accidentally called, it will cause a compile error
// (we don't define it, so the compiler will catch any usage)

#endif /* TUYA_MEM_H */
