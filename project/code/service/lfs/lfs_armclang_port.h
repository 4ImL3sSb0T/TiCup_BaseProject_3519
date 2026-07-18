/*
 * LittleFS ARMCLANG V6 + MSPM0G3519 bare-metal 兼容配置
 *
 * 通过工程预定义宏使用（推荐，无需本头文件路径）：
 *   LFS_NO_MALLOC, LFS_NO_DEBUG, LFS_NO_WARN, LFS_NO_ERROR, LFS_NO_ASSERT
 *
 * 或：-DLFS_DEFINES=lfs_armclang_port.h 且把本目录加入 Include Path。
 *
 * 本工程为 bare-metal：
 *   - 无 FreeRTOS：使用静态缓冲（g_lfs_cfg 已提供），LFS_NO_MALLOC
 *   - 无线程锁：不定义 LFS_THREADSAFE
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef LFS_ARMCLANG_PORT_H
#define LFS_ARMCLANG_PORT_H

/* ── 禁止堆分配：一律走 lfs_config 静态缓冲 ─────────────────────── */
#ifndef LFS_NO_MALLOC
#define LFS_NO_MALLOC
#endif

/* ── 关闭日志以减 ROM（调试时可注释） ───────────────────────────── */
#ifndef LFS_NO_DEBUG
#define LFS_NO_DEBUG
#endif
#ifndef LFS_NO_WARN
#define LFS_NO_WARN
#endif
#ifndef LFS_NO_ERROR
#define LFS_NO_ERROR
#endif

/* ── bare-metal 无 __aeabi_assert ───────────────────────────────── */
#ifndef LFS_NO_ASSERT
#define LFS_NO_ASSERT
#endif

/* 不定义 LFS_THREADSAFE：无 RTOS 互斥 */

#endif /* LFS_ARMCLANG_PORT_H */
