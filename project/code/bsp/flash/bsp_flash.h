/*********************************************************************************************************************
 * bsp_flash — MSPM0G3519 DATA Flash 底层
 *
 * 用途：仅供 LittleFS 使用（不替代 zf_driver_flash / storage）。
 *
 * 介质：片内 DATA Flash（0x41D00000，16 KB，扇区 1 KB）
 *   - 与 MAIN 程序区分离，擦写不会误伤代码
 *   - 扇区大小见 DL_FLASHCTL_SECTOR_SIZE
 *
 * 约定：
 *   - erase 最小单位 = 1 个扇区 (1 KB)
 *   - program 最小单位 = 8 字节（64-bit flash word，带硬件 ECC）
 *   - 地址与长度须满足上述对齐；read 可为任意字节
 ********************************************************************************************************************/

#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DATA Flash 基址与几何（与 TI DriverLib / scatter 一致） */
#define BSP_FLASH_BASE          (0x41D00000u)
#define BSP_FLASH_SECTOR_SIZE   (1024u)     /* 擦除单位 */
#define BSP_FLASH_PROG_SIZE     (8u)        /* 编程单位：64-bit word */
#define BSP_FLASH_SIZE_DEFAULT  (16u * 1024u)

/* 返回值：0 成功，非 0 失败 */
#define BSP_FLASH_OK            (0)
#define BSP_FLASH_ERR           (1)

/**
 * @brief 初始化 DATA Flash 驱动（幂等）
 * @return BSP_FLASH_OK / BSP_FLASH_ERR（无 DATA Flash 时返回 ERR）
 */
uint8_t bsp_flash_init(void);

/** @return DATA Flash 起始物理地址 */
uint32_t bsp_flash_base(void);

/** @return 可用总字节数（通常 16 KB） */
uint32_t bsp_flash_size(void);

/** @return 扇区（擦除）大小，字节 */
uint32_t bsp_flash_sector_size(void);

/** @return 编程最小单位，字节 */
uint32_t bsp_flash_prog_size(void);

/** @return 扇区个数 */
uint32_t bsp_flash_sector_count(void);

/**
 * @brief 从 DATA Flash 读取
 * @param offset  相对 BSP_FLASH_BASE 的偏移
 * @param buf     目标缓冲
 * @param len     字节数
 */
uint8_t bsp_flash_read(uint32_t offset, void *buf, uint32_t len);

/**
 * @brief 编程（不自动擦除；目标区须已擦为 0xFF）
 * @param offset  相对基址，须 8 字节对齐
 * @param data    源数据
 * @param len     字节数，须为 8 的倍数
 */
uint8_t bsp_flash_write(uint32_t offset, const void *data, uint32_t len);

/**
 * @brief 擦除一个扇区
 * @param sector_index  0 .. sector_count-1
 */
uint8_t bsp_flash_erase_sector(uint32_t sector_index);

/**
 * @brief 按偏移擦除所在扇区（offset 对齐到扇区即可，不必严格对齐）
 * @param offset  相对基址的任意偏移（落在目标扇区内）
 */
uint8_t bsp_flash_erase_at(uint32_t offset);

#ifdef __cplusplus
}
#endif

#endif /* BSP_FLASH_H */
