/*
 * LittleFS ARMCLANG V6 compatibility shim.
 *
 * Include this file via -DLFS_DEFINES=lfs_armclang_port.h in the
 * Keil project settings (C/C++ → Define) so that LittleFS picks up
 * the GD32H7 memory allocator and disables debug logging.
 *
 * ARMCLANG V6.22 Clang intrinsics note:
 *   lfs_util.h gates __builtin_clz / __builtin_popcount on
 *   __GNUC__ || __CC_ARM.  ARMCLANG V6 does NOT define __CC_ARM
 *   (that was ARMCC V5), so some intrinsics fall back to the
 *   portable C implementation.  This is correct and complete;
 *   the C fallback is 10–30 % slower than the builtin but the
 *   OSPI flash latency completely dominates.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef LFS_ARMCLANG_PORT_H
#define LFS_ARMCLANG_PORT_H

#include <stddef.h>   /* size_t */

/* ── Use FreeRTOS heap (pvPortMalloc / vPortFree) ─────────────────
 *
 * If LFS_NO_MALLOC is defined instead, LittleFS can operate entirely
 * from static buffers — each open file must then be opened via
 * lfs_file_opencfg(&lfs, &file, path, flags, &file_cfg) where
 * file_cfg.buffer points to a caller-provided cache.
 */

#if !defined(LFS_NO_MALLOC)
void *pvPortMalloc(size_t size);
void  vPortFree(void *ptr);

#define LFS_MALLOC(sz)    pvPortMalloc(sz)
#define LFS_FREE(ptr)     vPortFree(ptr)
#endif

/* ── Turn off debug/warn/error logging to reduce ROM ──────────────
 *
 * Comment out any of these to re-enable the corresponding printf()
 * for debugging.  Even with all three disabled, LFS_TRACE remains
 * off (requires explicit LFS_YES_TRACE).
 */

#define LFS_NO_DEBUG
#define LFS_NO_WARN
#define LFS_NO_ERROR

/* ── ARMCLANG V6.22 asserts are available (C99 <assert.h>) ────────
 *
 * To strip asserts for production, define LFS_NO_ASSERT here.
 * ARMCLANG V6.22 in bare-metal mode does not link __aeabi_assert
 * (the C library runtime assert handler), so asserts must be
 * disabled to avoid linker errors.
 */
#define LFS_NO_ASSERT

/* ── Thread safety ────────────────────────────────────────────────
 *
 * Enable LFS_THREADSAFE to allow LittleFS to call lock/unlock
 * callbacks around every filesystem operation.  The actual mutex
 * implementation is in lfs_port.c (FreeRTOS mutex).
 */
#define LFS_THREADSAFE

#endif /* LFS_ARMCLANG_PORT_H */
