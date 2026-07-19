# CLAUDE.md

This file provides guidance when working with code in this repository.

## Project Overview

TI 杯基地项目 —— **MSPM0G3519** (ARM Cortex-M0+) 版本，由 `BaseProject`（MSPM0G3507）移植而来。  
使用 **逐飞科技 SEEKFREE 开源库** + TI DriverLib。

## 文档职责（改脚前必读）

| 文档 | 职责 |
|------|------|
| **`docs/hardware.md`** | **固件已启用** 的引脚/定时器/通道（改脚必改此文） |
| **`docs/motherboard_3507_pinout.md`** | 3507 主板丝印 / 可接外设全集（尚无 3519 主板） |
| `docs/MSPM0G3507_vs_MSPM0G3519.md` | 芯片规格与库差异 |
| `docs/PORTING_NOTES.md` | 从 3507 移植步骤（历史） |
| `project/尽量不要使用的引脚.txt` | 核心板慎用脚官方列表副本 |

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
2. event / soft_timer / log(UART0) / **fs_init (LFS)** / param_init / cmd / **motor+encoder** / imu / **chassis** / **param_load** / **gui_app**
3. `pit_ms_init(PIT_TIM_G12, 1, ...)` 系统 1ms
4. soft_timer：imu 1ms / motion 2ms / **GUI 按键 5ms** / chassis 10ms / cmd 20ms / **GUI 刷新 100ms** / LED 500ms + `event_process_async()` 主循环

### Hardware（摘要）

完整占用表见 **`docs/hardware.md`**。当前大致：电机+编码器+底盘已启用；命令/日志 **UART0**；无线/WiFi 未开；IMU SPI1；GUI IPS200 SPI0 + 按键 A30/A31/B0/B1；参数 LFS `/param.txt`。

## 3519 注意

- **无 UART2**，兼容引脚用 **UART7**
- Flash 512KB / RAM 128KB（见 `mspm0g3519.sct`）
- 预定义宏：`__MSPM0G3519__`
- 新增 `project/code` 源文件需加入 uvprojx 的 `code` 组

## 参考例程

`D:\Project\TI_Cup\MSPM0G3519_Library-master\Example\`
