/*
 * MSPM0G3519 LittleFS Port — 片内 DATA Flash via bsp_flash
 *
 * 几何由 bsp_flash / 工厂区 DATA 大小决定：
 *   erase block = 1 KB 扇区
 *   prog unit   = 8 B  (64-bit flash word + ECC)
 *   capacity    = 全部 DATA Flash（通常 16 KB → 16 blocks）
 *
 * 调用顺序：lfs_port_init() → lfs_mount() / lfs_format()
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef LFS_PORT_H
#define LFS_PORT_H

#include "lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 与 MSPM0 DATA Flash 对齐的默认几何（init 时会按实际 size 校正 block_count） */

#define LFS_FLASH_READ_SIZE       1
#define LFS_FLASH_PROG_SIZE       8
#define LFS_FLASH_BLOCK_SIZE      1024
#define LFS_FLASH_BLOCK_COUNT     16    /* 16 × 1 KB = 16 KB；init 会覆盖 */

#define LFS_FLASH_CACHE_SIZE      256
#define LFS_FLASH_LOOKAHEAD_SIZE  16    /* 覆盖 16*8=128 blocks 位图，16 块绰绰有余 */

/* ── 静态缓冲（lfs_port.c） ─────────────────────────────────────── */

extern uint8_t lfs_read_buf[LFS_FLASH_CACHE_SIZE];
extern uint8_t lfs_prog_buf[LFS_FLASH_CACHE_SIZE];
extern uint8_t lfs_lookahead_buf[LFS_FLASH_LOOKAHEAD_SIZE];

/* ── 全局配置（非 const：block_count 运行时填充） ───────────────── */

extern struct lfs_config g_lfs_cfg;

/* ── Public API ─────────────────────────────────────────────────── */

/**
 * @brief 初始化 bsp_flash 并填充 g_lfs_cfg
 * @return 0 成功，负值为 LFS 错误码
 */
int lfs_port_init(void);

int lfs_port_read (const struct lfs_config *c, lfs_block_t block,
                   lfs_off_t off, void *buffer, lfs_size_t size);
int lfs_port_prog (const struct lfs_config *c, lfs_block_t block,
                   lfs_off_t off, const void *buffer, lfs_size_t size);
int lfs_port_erase(const struct lfs_config *c, lfs_block_t block);
int lfs_port_sync (const struct lfs_config *c);

#ifdef __cplusplus
}
#endif

#endif /* LFS_PORT_H */
