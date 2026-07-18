# 硬件资源与引脚手册（必读）

> **强制规则**  
> 任何涉及 **引脚分配、定时器/UART/SPI 复用、外设接线、改电机/编码器/串口/IMU/屏/WiFi** 的开发，**必须先读并更新本文件**，再改代码。  
> 禁止在未查本表的情况下“先改宏再试”；禁止占用「禁用引脚」与「已占用资源」。  
> 代码与本文件冲突时：**以本文件 + 实际主板丝印为准**，改完代码必须同步回写本文件。

| 项 | 值 |
|----|-----|
| 平台 | MSPM0G3519 + 逐飞 SEEKFREE 开源库（**3519 专用，勿混 3507 库**） |
| 工程 | `BaseProject_3519` |
| 主时钟 | 80 MHz（`clock_init(SYSTEM_CLOCK_80M)`） |
| 参考库例程 | `D:\Project\TI_Cup\MSPM0G3519_Library-master\Example\` |
| 芯片差异 | 见 `docs/MSPM0G3507_vs_MSPM0G3519.md` |
| 移植说明 | 见 `docs/PORTING_NOTES.md` |

**配置源文件（改脚时优先改这些，并回写本文）：**

| 模块 | 头文件 / 位置 |
|------|----------------|
| 总览与 LED 心跳 | `project/user/src/main.c` |
| 电机 | `project/code/service/motion/motor.h` |
| 编码器 | `project/code/service/motion/encoder.h` |
| 命令串口 | `project/code/service/com/cmd_service.c` |
| 调试串口 | `libraries/zf_common/zf_common_debug.h` |
| IMU963RA | `libraries/zf_device/zf_device_imu963ra.h` |
| WiFi SPI | `libraries/zf_device/zf_device_wifi_spi.h` |
| 屏 IPS200/114 | `libraries/zf_device/zf_device_ips200.h` 等 |

---

## 1. 当前生效的资源总表（本工程在用）

| 功能 | 外设 / 定时器 | 引脚 | 说明 | 官方例程参考 |
|------|---------------|------|------|--------------|
| 左电机 PWM | TIM_A0 CH0 | **A0** | DRV8701 | E3_04_drv8701e_double |
| 左电机 DIR | GPIO | **A1** | | 同上 |
| 右电机 PWM | TIM_A0 CH2 | **B12** | | 同上 |
| 右电机 DIR | GPIO | **B13** | | 同上 |
| 左编码器 A/B | TIM_G8 正交 | **A26 / A27** | `encoder_quad_init` | E2_01_encoder_quadrature |
| 右编码器 A/B | TIM_G9 正交 | **B7 / B9** | 仅 G8/G9 支持正交 | 同上 |
| 系统 1ms 节拍 | PIT_TIM_G12 | —（无 GPIO） | `sys_time_ms` | — |
| 调试串口 | UART0 | **A10 TX / A11 RX** | 115200 | 核心板 debug |
| 核心板蓝灯 | GPIO | **A14** | 500ms soft_timer 翻转 | E01_gpio_demo |
| 命令串口 UART3 | UART3 | A14 TX / A13 RX | **与 LED 冲突，LED 优先时 TX 不可用** | — |
| 日志默认 | WiFi SPI 或 UART | 见 §5 | `sys_log_init(SYS_LOG_WIFI)` | — |
| IMU | SPI1 | B23/B22/B21 + CS B19 | IMU963RA 默认 SPI | E4_03_imu963ra |

电机 PWM 频率：`MOTOR_PWM_FREQ_HZ = 17000`。

---

## 2. 定时器占用（严禁重复）

| 定时器 | 用途 | 可否再占用 |
|--------|------|------------|
| **TIM_A0** | 双电机 PWM | 否（CH0/CH2 已用） |
| **TIM_G8** | 左正交编码器 | 否 |
| **TIM_G9** | 右正交编码器 | 否 |
| **PIT_TIM_G12** | 系统 1ms | 否 |
| TIM_G6 / TIM_G7 | 方向编码器例程用，**本工程正交模式未用** | 可规划，勿与其它冲突 |
| TIM_A1、TIM_G0、TIM_G14 等 | 未占用 | 新功能优先从这里选 |

**正交编码器硬性限制（逐飞库）：**

- `encoder_quad_init` **仅允许** `TIM_G8`、`TIM_G9`（库内 `zf_assert`）。
- 方向模式 `encoder_dir_init` 可用 G6/G7 等，引脚与正交不同，**不要混用两种模式的脚**。

**方向模式（备用，非当前配置）：**

| 编码器 | 定时器 | LSB | DIR |
|--------|--------|-----|-----|
| 左 | TIM_G7 | A26 | B27 |
| 右 | TIM_G6 | B10 | B11 |

例程：`E2_02_encoder_dir_demo`。若切回方向模式，必须改 `encoder.h` / `encoder.c` **并更新本文件 §1**。

---

## 3. 引脚占用一览

### 3.1 已占用（本工程业务）

| 引脚 | 功能 |
|------|------|
| A0 | 左电机 PWM |
| A1 | 左电机 DIR |
| A10 | UART0 TX（调试） |
| A11 | UART0 RX（调试） |
| A14 | **核心板 LED**（心跳；与 UART3_TX 互斥） |
| A13 | UART3 RX / 亦可能被屏 BL、WiFi MISO 等占用，见 §5–6 |
| A26 | 左编码器 CH1 |
| A27 | 左编码器 CH2 |
| B7 | 右编码器 CH1 |
| B9 | 右编码器 CH2 |
| B12 | 右电机 PWM |
| B13 | 右电机 DIR |
| B19 | IMU CS |
| B21 | IMU SPI1 MISO |
| B22 | IMU SPI1 MOSI |
| B23 | IMU SPI1 SCK |
| A9 / A12 / A16 / A17 / B20 等 | WiFi SPI 默认脚（启用日志 WiFi 时占用） |

### 3.2 核心板 / 主板「尽量不要用」

来自 `project/尽量不要使用的引脚.txt` 与逐飞说明：

| 引脚 | 原因 |
|------|------|
| **A19, A20, A5, A6, A4, A3** | 与核心板特殊功能相关，使用可能导致异常 |
| **A14** | 核心板蓝色 LED；IO 富余时应优先留给状态指示 |

### 3.3 已知冲突（必须处理）

| 冲突 | 引脚 | 现状与策略 |
|------|------|------------|
| **LED vs UART3_TX** | A14 | `main` 在 `cmd_service_init` 后 `gpio_init(A14)`，**LED 心跳优先**。命令改走 UART0 / WiFi。若必须用 UART3 TX，关掉 LED 定时器并更新本文。 |
| **右电机 DIR vs GUI 按键例程** | B13 | `mjc_input_button.c` 示例用 B13 等，与右电机 DIR **冲突**。启用屏按键前必须改脚并更新本文。 |
| **SPI0 总线** | A9/A12/A13… | WiFi SPI 与 IPS 屏常共用 SPI0 不同 CS，同时启用前查库头文件。 |
| **3519 无 UART2** | — | 原 3507 的 UART2 场景改 **UART7** 或其它实例。 |

---

## 4. 运动系统接线

### 4.1 电机（DRV8701，灰排线）

| 轮 | PWM 宏 | 引脚 | DIR |
|----|--------|------|-----|
| 左 `motor_left` | `PWM_TIM_A0_CH0_A0` | A0 | A1 |
| 右 `motor_right` | `PWM_TIM_A0_CH2_B12` | B12 | B13 |

- 命令 mask：`bit0=左`，`bit1=右`（0x1 / 0x2 / 0x3）。
- 源码：`motor.h` / `motor.c`。

### 4.2 编码器（正交）

| 轮 | 定时器 | CH1 (A 相) | CH2 (B 相) | 极性（软件） |
|----|--------|------------|------------|--------------|
| 左 `encoder_left` | TIM_G8 | A26 | A27 | polarity=0 |
| 右 `encoder_right` | TIM_G9 | B7 | B9 | polarity=1（方向取反） |

- 初始化：`encoder_quad_init`。
- 若实车方向反了：只改 `polarity`，不要乱换左右脚除非硬件重接。
- 源码：`encoder.h` / `encoder.c`。

### 4.3 底盘

- 差速：`chassis` 依赖 motor + encoder；IMU 可选（航向/角速度闭环）。
- 改轮距、减速比等运动学参数在 `chassis` 相关头文件，**不改引脚**；若改了传感器接线仍须更新本文件。

---

## 5. 通信

| 通道 | 外设 | 引脚 | 波特率 / 速率 | 用途 |
|------|------|------|---------------|------|
| 调试 | UART0 | A10/A11 | 115200 | `debug_init` / printf |
| 命令 | UART3 | A14/A13 | 115200 | `cmd_service`（TX 与 LED 冲突） |
| 日志 WiFi | SPI0 + 控制脚 | SCK A12, MOSI A9, MISO A13, CS B20, INT A17, RST A16 | 30 MHz SPI | `sys_log` / `WIFI_SPI_*` |

日志类型：`SYS_LOG_UART` / `SYS_LOG_WIFI`（见 `sys_log.h`）。当前 `main` 使用 `sys_log_init(SYS_LOG_WIFI)` 时需接 WiFi SPI 模块。

---

## 6. 传感器与显示（库默认，启用前核对）

### 6.1 IMU963RA（本工程 `imu_init` 使用）

| 信号 | 配置 |
|------|------|
| 接口 | SPI1，约 8 MHz |
| SCK | B23 (`SPI1_SCK_B23`) |
| MOSI | B22 |
| MISO | B21 |
| CS | B19 |

同组脚也用于 660RA/RB/RC 等模块，**主板 IMU 座固定时勿改**。

### 6.2 显示屏（库默认，本工程未必初始化）

典型 IPS200 SPI：`SPI0`，SCK A12，MOSI A9，RST A7，DC A15，CS A8，BLK A13 等。  
**启用 GUI 前**：对照 §3 占用表，避免与 WiFi / UART3 / 电机冲突。

### 6.3 GUI 按键示例（未与电机共存）

`mjc_input_button.c` 曾用 B13/B12/B14/B15 —— **B12/B13 已被右电机占用**，启用前必须重映射。

---

## 7. 软件定时与中断节拍（非引脚，但属“硬件节拍”）

硬件 PIT 只做 1ms 时间基准；业务用 soft_timer：

| 周期 | 任务 |
|------|------|
| 1 ms | `imu_update` |
| 2 ms | `encoder_update` + `motor_update` |
| 10 ms | `chassis_update` |
| 20 ms | `cmd_service_task` |
| 500 ms | LED 心跳 `gpio_toggle_level(A14)` |

主循环：`soft_timer_process()` + `event_process_async()`。

---

## 8. 修改硬件配置的检查清单

改任何引脚 / 定时器前按顺序执行：

1. **读本文件** §1–3，确认目标脚空闲、无禁用、无总线冲突。  
2. 查逐飞库枚举：`zf_driver_*.h` 中是否存在该 `PWM_TIM_*` / `UARTx_*` / `encoder` 复用组合。  
3. 改对应模块宏（§ 文首表格中的源文件）。  
4. **同步更新本文件** §1、§2、§3 及冲突表。  
5. 更新 `main.c` 顶部资源注释（与本文保持一致）。  
6. 若动 UART/定时器向量：检查 `project/user/src/isr.c`。  
7. 全量编译；上电自检 LED / 串口 / 编码器计数 / 电机点动。

---

## 9. 变更记录

| 日期 | 变更 |
|------|------|
| 2026-07-18 | 初版：汇总电机/正交编码器/PIT/UART/LED/IMU/WiFi 及冲突策略 |
| 2026-07-18 | 编码器由方向模式(G6/G7)改为正交(G8/G9, A26/A27, B7/B9) |
| 2026-07-18 | 增加 A14 LED soft_timer 心跳（优先于 UART3_TX） |

---

## 10. 相关文档

- `docs/MSPM0G3507_vs_MSPM0G3519.md` — 芯片与资源差异  
- `docs/PORTING_NOTES.md` — 从 3507 工程移植步骤  
- `CLAUDE.md` — 工程总览（硬件细节以**本文**为准）  
- `project/尽量不要使用的引脚.txt` — 逐飞核心板禁用脚原文  
)
