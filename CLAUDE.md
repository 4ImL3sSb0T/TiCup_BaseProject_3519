# CLAUDE.md

This file provides guidance when working with code in this repository.

## Project Overview

TI 杯基地项目 —— **MSPM0G3519** (ARM Cortex-M0+) 版本，由 `BaseProject`（MSPM0G3507）移植而来。  
使用 **逐飞科技 SEEKFREE 开源库** + TI DriverLib。

芯片对比与移植说明见：

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

| 功能 | 资源 |
|------|------|
| 电机 | TIM_A0 PWM + DIR GPIO |
| 编码器 | TIM_G7 / TIM_G6 |
| 1ms 节拍 | PIT_TIM_G12 |
| 调试 | UART0 |
| 命令 | UART3（A14/A13） |

## 3519 注意

- **无 UART2**，兼容引脚用 **UART7**
- Flash 512KB / RAM 128KB（见 `mspm0g3519.sct`）
- 预定义宏：`__MSPM0G3519__`
- 新增 `project/code` 源文件需加入 uvprojx 的 `code` 组

## 参考例程

`D:\Project\TI_Cup\MSPM0G3519_Library-master\Example\`
