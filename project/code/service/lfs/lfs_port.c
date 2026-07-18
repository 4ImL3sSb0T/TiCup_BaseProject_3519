/*
 * MSPM0G3519 LittleFS Port — DATA Flash via bsp_flash
 *
 * bare-metal：静态缓冲，无 malloc，无线程锁。
 * LFS 负责 erase-before-prog；prog 只写已擦区域。
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "lfs_port.h"
#include "bsp/flash/bsp_flash.h"

#include <stddef.h>
#include <string.h>

/* ── Static buffers ─────────────────────────────────────────────── */

uint8_t lfs_read_buf     [LFS_FLASH_CACHE_SIZE];
uint8_t lfs_prog_buf     [LFS_FLASH_CACHE_SIZE];
uint8_t lfs_lookahead_buf[LFS_FLASH_LOOKAHEAD_SIZE];

/* ── Global configuration ───────────────────────────────────────── */

struct lfs_config g_lfs_cfg = {
    .context        = NULL,
    .read           = lfs_port_read,
    .prog           = lfs_port_prog,
    .erase          = lfs_port_erase,
    .sync           = lfs_port_sync,

    .read_size      = LFS_FLASH_READ_SIZE,
    .prog_size      = LFS_FLASH_PROG_SIZE,
    .block_size     = LFS_FLASH_BLOCK_SIZE,
    .block_count    = LFS_FLASH_BLOCK_COUNT,
    .block_cycles   = 500,

    .cache_size     = LFS_FLASH_CACHE_SIZE,
    .lookahead_size = LFS_FLASH_LOOKAHEAD_SIZE,
    .read_buffer    = lfs_read_buf,
    .prog_buffer    = lfs_prog_buf,
    .lookahead_buffer = lfs_lookahead_buf,

    .name_max       = 0,
    .file_max       = 0,
    .attr_max       = 0,
    .metadata_max   = 0,
    .compact_thresh = 0,
    .inline_max     = 0,
};

/* ── Public API ─────────────────────────────────────────────────── */

int lfs_port_init(void)
{
    uint32_t size;
    uint32_t sectors;

    if (bsp_flash_init() != BSP_FLASH_OK) {
        return LFS_ERR_IO;
    }

    size    = bsp_flash_size();
    sectors = bsp_flash_sector_count();

    if ((size == 0u) || (sectors == 0u)) {
        return LFS_ERR_IO;
    }

    /* 几何校验：LFS block = 物理扇区 */
    if (bsp_flash_sector_size() != LFS_FLASH_BLOCK_SIZE) {
        return LFS_ERR_IO;
    }
    if (bsp_flash_prog_size() != LFS_FLASH_PROG_SIZE) {
        return LFS_ERR_IO;
    }

    g_lfs_cfg.block_count = sectors;
    g_lfs_cfg.block_size  = LFS_FLASH_BLOCK_SIZE;
    g_lfs_cfg.prog_size   = LFS_FLASH_PROG_SIZE;
    g_lfs_cfg.read_size   = LFS_FLASH_READ_SIZE;

    return 0;
}

/* ── Block-device callbacks ───────────────────────────────────────
 *
 * physical_offset = block * block_size + off
 */

int lfs_port_read(const struct lfs_config *c, lfs_block_t block,
                  lfs_off_t off, void *buffer, lfs_size_t size)
{
    uint32_t offset;

    (void)c;
    if ((block >= g_lfs_cfg.block_count) || (buffer == NULL)) {
        return LFS_ERR_IO;
    }

    offset = (uint32_t)block * LFS_FLASH_BLOCK_SIZE + (uint32_t)off;
    if (bsp_flash_read(offset, buffer, (uint32_t)size) != BSP_FLASH_OK) {
        return LFS_ERR_IO;
    }
    return 0;
}

int lfs_port_prog(const struct lfs_config *c, lfs_block_t block,
                  lfs_off_t off, const void *buffer, lfs_size_t size)
{
    uint32_t offset;

    (void)c;
    if ((block >= g_lfs_cfg.block_count) || (buffer == NULL)) {
        return LFS_ERR_IO;
    }
    if (((off & (LFS_FLASH_PROG_SIZE - 1u)) != 0u) ||
        ((size & (LFS_FLASH_PROG_SIZE - 1u)) != 0u)) {
        return LFS_ERR_IO;
    }

    offset = (uint32_t)block * LFS_FLASH_BLOCK_SIZE + (uint32_t)off;
    if (bsp_flash_write(offset, buffer, (uint32_t)size) != BSP_FLASH_OK) {
        return LFS_ERR_IO;
    }
    return 0;
}

int lfs_port_erase(const struct lfs_config *c, lfs_block_t block)
{
    (void)c;
    if (block >= g_lfs_cfg.block_count) {
        return LFS_ERR_IO;
    }

    if (bsp_flash_erase_sector((uint32_t)block) != BSP_FLASH_OK) {
        return LFS_ERR_IO;
    }
    return 0;
}

int lfs_port_sync(const struct lfs_config *c)
{
    (void)c;
    return 0;
}
