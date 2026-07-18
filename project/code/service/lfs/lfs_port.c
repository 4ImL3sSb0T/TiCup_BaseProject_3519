/*
 * STM32H750 LittleFS Port — SPI NOR Flash via SFUD
 *
 * Block-device callbacks bridging LittleFS to the SFUD universal
 * flash driver.  SFUD auto-detects the flash chip, so this port
 * works with any supported SPI NOR Flash without manual changes.
 *
 * Call lfs_port_init() once during system startup (before mounting).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "lfs_port.h"
#include "sfud.h"
#include <stddef.h>

/* FreeRTOS mutex for thread safety */
#include "FreeRTOS.h"
#include "semphr.h"

static SemaphoreHandle_t g_lfs_mutex = NULL;

/* ── Static buffers ─────────────────────────────────────────────── */

uint8_t lfs_read_buf     [LFS_FLASH_CACHE_SIZE];
uint8_t lfs_prog_buf     [LFS_FLASH_CACHE_SIZE];
uint8_t lfs_lookahead_buf[LFS_FLASH_LOOKAHEAD_SIZE];

/* ── Global configuration ───────────────────────────────────────── */

struct lfs_config g_lfs_cfg = {
    .context       = NULL,
    .read          = lfs_port_read,
    .prog          = lfs_port_prog,
    .erase         = lfs_port_erase,
    .sync          = lfs_port_sync,
#ifdef LFS_THREADSAFE
    .lock          = lfs_port_lock,
    .unlock        = lfs_port_unlock,
#endif

    .read_size     = LFS_FLASH_READ_SIZE,      /*   1 */
    .prog_size     = LFS_FLASH_PROG_SIZE,      /* 256 */
    .block_size    = LFS_FLASH_BLOCK_SIZE,     /* 4096 */
    .block_count   = LFS_FLASH_BLOCK_COUNT,
    .block_cycles  = 500,

    .cache_size    = LFS_FLASH_CACHE_SIZE,     /* 256 */
    .lookahead_size= LFS_FLASH_LOOKAHEAD_SIZE, /*  32 */
    .read_buffer   = lfs_read_buf,
    .prog_buffer   = lfs_prog_buf,
    .lookahead_buffer = lfs_lookahead_buf,

    .name_max       = 0,
    .file_max       = 0,
    .attr_max       = 0,
    .metadata_max   = 0,
    .compact_thresh = 0,
    .inline_max     = 0,
};

/* ── Public API ─────────────────────────────────────────────────── */

void lfs_port_init(void)
{
    sfud_init();

    g_lfs_cfg.context = sfud_get_device(0);

    if (g_lfs_mutex == NULL) {
        g_lfs_mutex = xSemaphoreCreateMutex();
    }
}

/* ── Block-device callbacks ───────────────────────────────────────
 *
 * Address translation:  physical_addr = block * block_size + off
 *
 * LFS handles erase-before-prog — prog callback writes to already-
 * erased pages only, so sfud_write() (no auto-erase) is correct.
 */

static sfud_flash *get_flash(const struct lfs_config *c)
{
    return (sfud_flash *)c->context;
}

int lfs_port_read(const struct lfs_config *c, lfs_block_t block,
                  lfs_off_t off, void *buffer, lfs_size_t size)
{
    sfud_flash *flash = get_flash(c);
    uint32_t addr = (uint32_t)block * c->block_size + off;
    return sfud_read(flash, addr, size, (uint8_t *)buffer) == SFUD_SUCCESS ? 0 : LFS_ERR_IO;
}

int lfs_port_prog(const struct lfs_config *c, lfs_block_t block,
                  lfs_off_t off, const void *buffer, lfs_size_t size)
{
    sfud_flash *flash = get_flash(c);
    uint32_t addr = (uint32_t)block * c->block_size + off;
    return sfud_write(flash, addr, size, (const uint8_t *)buffer) == SFUD_SUCCESS ? 0 : LFS_ERR_IO;
}

int lfs_port_erase(const struct lfs_config *c, lfs_block_t block)
{
    sfud_flash *flash = get_flash(c);
    uint32_t addr = (uint32_t)block * c->block_size;
    return sfud_erase(flash, addr, c->block_size) == SFUD_SUCCESS ? 0 : LFS_ERR_IO;
}

int lfs_port_sync(const struct lfs_config *c)
{
    (void)c;
    return 0;
}

/* ── Thread safety callbacks ──────────────────────────────────────── */

int lfs_port_lock(const struct lfs_config *c)
{
    (void)c;
    if (g_lfs_mutex != NULL) {
        return xSemaphoreTake(g_lfs_mutex, portMAX_DELAY) == pdTRUE ? 0 : LFS_ERR_IO;
    }
    return 0;  /* No mutex created, assume single-threaded */
}

int lfs_port_unlock(const struct lfs_config *c)
{
    (void)c;
    if (g_lfs_mutex != NULL) {
        return xSemaphoreGive(g_lfs_mutex) == pdTRUE ? 0 : LFS_ERR_IO;
    }
    return 0;  /* No mutex created, assume single-threaded */
}
