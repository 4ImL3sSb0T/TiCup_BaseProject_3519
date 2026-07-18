/*
 * STM32H750 LittleFS Port — SPI NOR Flash via SFUD
 *
 * Provides the lfs_config structure and block-device callbacks
 * bridging LittleFS to the SFUD universal flash driver.
 *
 * Call lfs_port_init() once before lfs_mount().  SFUD auto-detects
 * the flash chip via JEDEC ID / SFDP, so this port works with any
 * SPI NOR Flash (W25Q, GD25Q, etc.) without manual geometry changes.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef LFS_PORT_H
#define LFS_PORT_H

#include "lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Flash geometry ───────────────────────────────────────────────
 *
 * These must match the physical flash.  SFUD fills them via SFDP:
 *   Page (program unit):    256 bytes  → lfs_config.prog_size
 *   Sector (erase unit):   4096 bytes  → lfs_config.block_size
 *
 * read_size = 1 (SPI supports byte-granular reads).
 */

#define LFS_FLASH_READ_SIZE    1
#define LFS_FLASH_PROG_SIZE    256
#define LFS_FLASH_BLOCK_SIZE   4096
#define LFS_FLASH_BLOCK_COUNT  2048   /* 2048 × 4 KB = 8 MB */

/* ── Cache / lookahead sizes ────────────────────────────────────── */

#define LFS_FLASH_CACHE_SIZE      LFS_FLASH_PROG_SIZE   /* 256 B */
#define LFS_FLASH_LOOKAHEAD_SIZE  32                    /* tracks 256 blocks */

/* ── Static buffers (defined in lfs_port.c) ─────────────────────── */

extern uint8_t lfs_read_buf[LFS_FLASH_CACHE_SIZE];
extern uint8_t lfs_prog_buf[LFS_FLASH_CACHE_SIZE];
extern uint8_t lfs_lookahead_buf[LFS_FLASH_LOOKAHEAD_SIZE];

/* ── Global config (non-const: context set at runtime) ──────────── */

extern struct lfs_config g_lfs_cfg;

/* ── Public API ─────────────────────────────────────────────────── */

void lfs_port_init(void);   /* call once before mount */

int lfs_port_read (const struct lfs_config *c, lfs_block_t block,
                   lfs_off_t off, void *buffer, lfs_size_t size);
int lfs_port_prog (const struct lfs_config *c, lfs_block_t block,
                   lfs_off_t off, const void *buffer, lfs_size_t size);
int lfs_port_erase(const struct lfs_config *c, lfs_block_t block);
int lfs_port_sync (const struct lfs_config *c);

#ifdef LFS_THREADSAFE
int lfs_port_lock  (const struct lfs_config *c);
int lfs_port_unlock(const struct lfs_config *c);
#endif

#ifdef __cplusplus
}
#endif

#endif /* LFS_PORT_H */
