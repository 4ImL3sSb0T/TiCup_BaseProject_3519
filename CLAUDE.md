# CLAUDE.md

This file provides guidance when working with code in this repository.

## Project Overview

TI 杯基地项目 —— **MSPM0G3519** (ARM Cortex-M0+) 版本，由 `BaseProject`（MSPM0G3507）移植而来。  
使用 **逐飞科技 SEEKFREE 开源库** + TI DriverLib。

芯片对比与移植说明见：

- **`docs/hardware.md`（硬件/引脚必读，改脚前必须查阅并回写）**
- `docs/MSPM0G3507_vs_MSPM0G3519.md`
- `docs/PORTING_NOTES.md`

## Build

- IDE：Keil MDK 5.38（µVision）
- 工程：`project/mdk/SeekFree_MSPM0G3519_Device_Library.uvprojx`
- 清理：`project/mdk/MDK删除临时文件.bat`
- 移动工程后先 `Project → Clean` 再编译

## Architecture

- `project/user/` — 入口 `main.c`、中断 `isr.c`
- `project/code/` — 业务：event/soft_timer、motion、imu、cmd、gui 等
- `libraries/zf_*` — 逐飞驱动与器件（**3519 专用，勿与 3507 库混用**）
- `libraries/sdk/` — TI CMSIS + DriverLib（`mspm0g351x`）

### Boot flow

1. `clock_init(SYSTEM_CLOCK_80M)` + `debug_init()`
2. event / soft_timer / log / param / cmd / motor / imu / chassis
3. `pit_ms_init(PIT_TIM_G12, 1, ...)` 系统 1ms
4. soft_timer 周期任务 + `event_process_async()` 主循环

### Hardware (current)

完整占用表、冲突与改脚检查清单见 **`docs/hardware.md`**（唯一权威硬件文档）。

| 功能 | 资源 |
|------|------|
| 电机 | TIM_A0：左 A0/A1，右 B12/B13 |
| 编码器 | TIM_G8/G9 正交：A26/A27、B7/B9 |
| 1ms 节拍 | PIT_TIM_G12 |
| LED | A14（500ms 心跳，优先于 UART3_TX） |
| 调试 | UART0（A10/A11） |
| 命令 | UART3（A14/A13，与 LED 冲突） |

## 3519 注意

- **无 UART2**，兼容引脚用 **UART7**
- Flash 512KB / RAM 128KB（见 `mspm0g3519.sct`）
- 预定义宏：`__MSPM0G3519__`
- 新增 `project/code` 源文件需加入 uvprojx 的 `code` 组

## 参考例程

`D:\Project\TI_Cup\MSPM0G3519_Library-master\Example\`
