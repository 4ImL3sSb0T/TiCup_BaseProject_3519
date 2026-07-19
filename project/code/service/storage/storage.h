/*********************************************************************************************************************
* Jupiter Project - 智能车竞赛
*
* 文件名称          storage.h
* 功能说明          Flash底层存储接口 - 提供Flash读写和CRC32校验功能
* 作者              Jupiter Team
* 创建日期          2025-12-13
* 修改日期          2026-03-22
*
* @deprecated 参数持久化已改用 LittleFS（service/fs + DATA Flash /param.txt）。
*             本模块基于 MAIN Flash 单页，勿再接入 param；仅保留源码备查。
*             请使用：fs_init() + param_save/load。
*
* 使用说明（历史）：
*   1. 提供Flash的底层读写接口，不涉及参数管理
*   2. 支持CRC32数据完整性校验
*   3. 曾由 Param 调用；当前 Keil 工程未编入
********************************************************************************************************************/

#ifndef __STORAGE_H__
#define __STORAGE_H__

#include "zf_common_typedef.h"

// Flash存储位置配置
#define STORAGE_SECTOR          127
#define STORAGE_PAGE            FLASH_PAGE_0
#define STORAGE_MAGIC_NUMBER    0x53544F52  // "STOR" in ASCII

#ifdef __cplusplus
extern "C" {
#endif

// 初始化Flash驱动
uint8 storage_init(void);

// 擦除Flash扇区
uint8 storage_clear(void);

// 写入数据到Flash（data为数据指针，length为字节数）
uint8 storage_write_data(const uint8* data, uint32 length);

// 从Flash读取数据（buffer为接收缓冲区，length为字节数）
uint8 storage_read_data(uint8* buffer, uint32 length);

// 计算CRC32校验值（用于数据完整性验证）
uint32 storage_crc32(const uint8* data, uint32 length);

#ifdef __cplusplus
}
#endif

#endif // !__STORAGE_H__
