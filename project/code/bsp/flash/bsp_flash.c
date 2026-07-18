/*********************************************************************************************************************
 * bsp_flash — MSPM0G3519 DATA Flash 底层实现
 *
 * 基于 TI DriverLib FLASHCTL：
 *   - 擦除：unprotect + erase sector + wait
 *   - 编程：64-bit + 硬件生成 ECC（programMemoryBlocking64WithECCGenerated）
 *   - DATA bank 与 MAIN 分离，可从 MAIN 运行时擦写 DATA（不必 FromRAM）
 ********************************************************************************************************************/

#include "bsp_flash.h"

#include <string.h>

#include "zf_common_interrupt.h"
#include "zf_common_typedef.h"

/* TI DriverLib（FLASHCTL / DATA 区） */
#include <ti/driverlib/dl_flashctl.h>
#include <ti/driverlib/m0p/dl_factoryregion.h>

/* 写缓冲：一次最多按扇区对齐编程用本地对齐缓冲（8 字节步进） */
#define BSP_FLASH_WRITE_CHUNK_WORDS  (16u)  /* 16 * 4 = 64 B per iteration */

static uint8_t  s_inited;
static uint32_t s_base;
static uint32_t s_size;
static uint32_t s_sector_count;

static int offset_in_range(uint32_t offset, uint32_t len)
{
    if (len == 0u) {
        return 0;
    }
    if (offset >= s_size) {
        return 0;
    }
    if (len > (s_size - offset)) {
        return 0;
    }
    return 1;
}

uint8_t bsp_flash_init(void)
{
    uint8_t kb;

    if (s_inited) {
        return BSP_FLASH_OK;
    }

    kb = DL_FactoryRegion_getDATAFlashSize();
    if (kb == 0u) {
        /* 无 DATA Flash 的芯片变体 */
        s_base         = BSP_FLASH_BASE;
        s_size         = 0u;
        s_sector_count = 0u;
        return BSP_FLASH_ERR;
    }

    s_base         = FLASHCTL_DATA_ADDRESS; /* 0x41D00000 */
    s_size         = (uint32_t)kb * 1024u;
    s_sector_count = s_size / BSP_FLASH_SECTOR_SIZE;
    s_inited       = 1u;

    return BSP_FLASH_OK;
}

uint32_t bsp_flash_base(void)
{
    return s_base ? s_base : BSP_FLASH_BASE;
}

uint32_t bsp_flash_size(void)
{
    return s_size;
}

uint32_t bsp_flash_sector_size(void)
{
    return BSP_FLASH_SECTOR_SIZE;
}

uint32_t bsp_flash_prog_size(void)
{
    return BSP_FLASH_PROG_SIZE;
}

uint32_t bsp_flash_sector_count(void)
{
    return s_sector_count;
}

uint8_t bsp_flash_read(uint32_t offset, void *buf, uint32_t len)
{
    const uint8_t *src;

    if ((buf == NULL) || !s_inited || !offset_in_range(offset, len)) {
        return BSP_FLASH_ERR;
    }

    src = (const uint8_t *)(s_base + offset);
    memcpy(buf, src, len);
    return BSP_FLASH_OK;
}

uint8_t bsp_flash_write(uint32_t offset, const void *data, uint32_t len)
{
    const uint8_t *src;
    uint32_t       addr;
    uint32_t       remain;
    uint32_t       primask;
    uint8_t        ret = BSP_FLASH_OK;

    /* 本地 4 字节对齐缓冲，供 DriverLib 使用 */
    uint32_t word_buf[BSP_FLASH_WRITE_CHUNK_WORDS];

    if ((data == NULL) || !s_inited || !offset_in_range(offset, len)) {
        return BSP_FLASH_ERR;
    }
    if (((offset & (BSP_FLASH_PROG_SIZE - 1u)) != 0u) ||
        ((len & (BSP_FLASH_PROG_SIZE - 1u)) != 0u)) {
        return BSP_FLASH_ERR;
    }

    src    = (const uint8_t *)data;
    addr   = s_base + offset;
    remain = len;

    primask = interrupt_global_disable();

    while (remain > 0u) {
        uint32_t chunk = remain;
        uint32_t words;
        bool     ok;

        if (chunk > (BSP_FLASH_WRITE_CHUNK_WORDS * 4u)) {
            chunk = BSP_FLASH_WRITE_CHUNK_WORDS * 4u;
        }
        /* 保持 8 字节对齐块 */
        chunk &= ~(BSP_FLASH_PROG_SIZE - 1u);
        if (chunk == 0u) {
            ret = BSP_FLASH_ERR;
            break;
        }

        memcpy(word_buf, src, chunk);
        words = chunk / 4u; /* DriverLib：32-bit word 个数，须为偶数 */

        ok = DL_FlashCTL_programMemoryBlocking64WithECCGenerated(
            FLASHCTL,
            addr,
            word_buf,
            words,
            DL_FLASHCTL_REGION_SELECT_MAIN);

        if (!ok) {
            ret = BSP_FLASH_ERR;
            break;
        }

        src    += chunk;
        addr   += chunk;
        remain -= chunk;
    }

    interrupt_global_enable(primask);
    return ret;
}

uint8_t bsp_flash_erase_sector(uint32_t sector_index)
{
    uint32_t addr;
    uint32_t primask;
    bool     ok;

    if (!s_inited || (sector_index >= s_sector_count)) {
        return BSP_FLASH_ERR;
    }

    addr = s_base + sector_index * BSP_FLASH_SECTOR_SIZE;

    primask = interrupt_global_disable();

    /* 与 TI eraseDataBank 一致：DATA 区 unprotect 使用 REGION_SELECT_MAIN */
    DL_FlashCTL_executeClearStatus(FLASHCTL);
    DL_FlashCTL_unprotectSector(FLASHCTL, addr, DL_FLASHCTL_REGION_SELECT_MAIN);
    DL_FlashCTL_eraseMemory(FLASHCTL, addr, DL_FLASHCTL_COMMAND_SIZE_SECTOR);
    ok = DL_FlashCTL_waitForCmdDone(FLASHCTL);
    DL_FlashCTL_executeClearStatus(FLASHCTL);

    interrupt_global_enable(primask);

    return ok ? BSP_FLASH_OK : BSP_FLASH_ERR;
}

uint8_t bsp_flash_erase_at(uint32_t offset)
{
    if (!s_inited || (offset >= s_size)) {
        return BSP_FLASH_ERR;
    }
    return bsp_flash_erase_sector(offset / BSP_FLASH_SECTOR_SIZE);
}
