# CLAUDE.md

This file provides guidance when working with code in this repository.

## Project Overview

TI 杯基地项目 —— **MSPM0G3519** (ARM Cortex-M0+) 版本，由 `BaseProject`（MSPM0G3507）移植而来。  
使用 **逐飞科技 SEEKFREE 开源库** + TI DriverLib。

芯片对比与移植说明见：

- **`docs/hardware.md`（软件已占用资源必读，改脚前必须查阅并回写）**
- **`docs/motherboard_3507_pinout.md`（现用 3507 主板丝印/全引脚表；尚无 3519 主板）**
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
| 电机/编码器/底盘 | **暂关闭**（B7 让给无线串口） |
| 1ms 节拍 | PIT_TIM_G12 |
| LED | A14（500ms 心跳） |
| 调试 | UART0（A10/A11） |
| 命令/日志 | **无线串口 UART1：B6 TX / B7 RX / B2 RTS** + UART0（WiFi SPI 暂不可用） |
| IMU | SPI1：B23/B22/B21 + CS B19 |

**尽量不要使用的引脚（核心板官方 · 重点）**：**A19、A20、A5、A6、A4、A3**（特殊功能脚，占用可能导致核心板异常）；**A14** 优先留给板载 LED。  
来源：`MSPM0G3519_Library-master\Example\Coreboard_Demo\E01_gpio_demo\尽量不要使用的引脚.txt`；工程约定见 `docs/hardware.md` §3.2。

## 3519 注意

- **无 UART2**，兼容引脚用 **UART7**
- Flash 512KB / RAM 128KB（见 `mspm0g3519.sct`）
- 预定义宏：`__MSPM0G3519__`
- 新增 `project/code` 源文件需加入 uvprojx 的 `code` 组

## 参考例程

`D:\Project\TI_Cup\MSPM0G3519_Library-master\Example\`
