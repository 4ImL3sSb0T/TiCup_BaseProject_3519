/*********************************************************************************************************************
* Jupiter Project - 智能车竞赛
*
* 文件名称          storage.h
* 功能说明          Flash底层存储接口 - 提供Flash读写和CRC32校验功能
* 作者              Jupiter Team
* 创建日期          2025-12-13
* 修改日期          2025-01-09
*
* 使用说明：
*   1. 提供Flash的底层读写接口，不涉及参数管理
*   2. 支持CRC32数据完整性校验
*   3. 由Param模块调用以实现参数持久化
*
* 示例代码：
*   storage_init();                                          // 初始化Flash
*   uint32 crc = storage_crc32(data, length);               // 计算CRC32
*   storage_write_data(data, length);                        // 写入数据
*   storage_read_data(buffer, length);                       // 读取数据
*   storage_clear();                                         // 擦除Flash
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
