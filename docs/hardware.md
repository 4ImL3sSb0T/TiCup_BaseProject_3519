# 硬件资源与引脚手册（必读）

> **强制规则**  
> 任何涉及 **引脚分配、定时器/UART/SPI 复用、外设接线、改电机/编码器/串口/IMU/屏/WiFi** 的开发，**必须先读并更新本文件**，再改代码。  
> 禁止在未查本表的情况下“先改宏再试”；禁止占用「禁用引脚」与「已占用资源」。  
> 代码与本文件冲突时：**以本文件 + 实际主板丝印为准**，改完代码必须同步回写本文件。

| 项 | 值 |
|----|-----|
| 平台（固件） | MSPM0G3519 + 逐飞 SEEKFREE 开源库（**3519 专用，勿混 3507 库**） |
| **实物主板** | **目前使用 MSPM0G3507 主板**；**尚无 3519 专用主板**（核心板可上 3519，底板按 3507 图） |
| 主板全引脚图 | 见 **`docs/motherboard_3507_pinout.md`**（丝印 / 可接外设全集） |
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
| 日志通道 | `project/code/service/sys/sys_log.c` / `sys_log.h` |
| 无线转串口 | `libraries/zf_device/zf_device_wireless_uart.h` |
| 调试串口 | `libraries/zf_common/zf_common_debug.h` |
| IMU963RA | `libraries/zf_device/zf_device_imu963ra.h` |
| WiFi SPI | `libraries/zf_device/zf_device_wifi_spi.h`（**当前未用**） |
| 屏 IPS200/114 | `libraries/zf_device/zf_device_ips200.h` 等 |

---

## 1. 当前生效的资源总表（本工程在用）

| 功能 | 外设 / 定时器 | 引脚 | 说明 | 官方例程参考 |
|------|---------------|------|------|--------------|
| ~~左电机 PWM~~ | TIM_A0 CH0 | ~~A0~~ | **暂关闭** | E3_04_drv8701e_double |
| ~~左电机 DIR~~ | GPIO | ~~A1~~ | **暂关闭** | 同上 |
| ~~右电机 PWM~~ | TIM_A0 CH2 | ~~B12~~ | **暂关闭** | 同上 |
| ~~右电机 DIR~~ | GPIO | ~~B13~~ | **暂关闭** | 同上 |
| ~~左编码器 A/B~~ | TIM_G8 正交 | ~~A26 / A27~~ | **暂关闭** | E2_01_encoder_quadrature |
| ~~右编码器 A/B~~ | TIM_G9 正交 | ~~B7 / B9~~ | **暂关闭**（B7 让给无线 RX） | 同上 |
| ~~底盘~~ | software | — | **暂关闭**（`main` 不 init / 不轮询） | — |
| 系统 1ms 节拍 | PIT_TIM_G12 | —（无 GPIO） | `sys_time_ms` | — |
| 调试串口 | UART0 | **A10 TX / A11 RX** | 115200；兼命令入口 | 核心板 debug |
| 核心板蓝灯 | GPIO | **A14** | 500ms soft_timer 翻转 | E01_gpio_demo |
| 命令 + 日志 | UART1 无线转串口 | **B6 TX / B7 RX / B2 RTS** | `SYS_LOG_WIRELESS`；ISR 走 `wireless_module_uart_handler` | 无线串口例程 |
| 命令辅入口 | UART0 | A10/A11 | `debug_read_ring_buffer` | — |
| ~~命令/日志 WiFi SPI~~ | SPI0 | — | **暂不可用**，勿接线依赖 | — |
| IMU | SPI1 | B23/B22/B21 + CS B19 | IMU963RA 默认 SPI | E4_03_imu963ra |

电机 PWM 频率：`MOTOR_PWM_FREQ_HZ = 17000`。

---

## 2. 定时器占用（严禁重复）

| 定时器 | 用途 | 可否再占用 |
|--------|------|------------|
| TIM_A0 | 双电机 PWM（**固件暂未 init**） | 可临时挪用；恢复电机后否 |
| TIM_G8 | 左正交编码器（**暂未 init**） | 可临时挪用；恢复编码器后否 |
| TIM_G9 | 右正交编码器（**暂未 init**） | 可临时挪用；恢复编码器后否 |
| **PIT_TIM_G12** | 系统 1ms | 否 |
| **UART1** | 无线转串口（命令/日志） | 否 |
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
| A10 | UART0 TX（调试 + 命令辅入口） |
| A11 | UART0 RX（调试 + 命令辅入口） |
| A14 | **核心板 LED**（心跳） |
| **B6** | **无线串口 MCU TX**（UART1） |
| **B7** | **无线串口 MCU RX**（UART1） |
| **B2** | **无线串口 RTS** |
| B19 | IMU CS |
| B21 | IMU SPI1 MISO |
| B22 | IMU SPI1 MOSI |
| B23 | IMU SPI1 SCK |

### 3.1b 暂释放（代码保留宏，main 未 init）

| 引脚 | 原功能 | 说明 |
|------|--------|------|
| A0 / A1 | 左电机 PWM/DIR | 底盘关闭期间未驱动 |
| B12 / B13 | 右电机 PWM/DIR | 同上 |
| A26 / A27 | 左编码器 | 同上 |
| B9 | 右编码器 CH2 | 右 CH1 曾为 B7，与无线 RX 冲突 |
| A9 / A12 / A13 / A16 / A17 / B20 | WiFi SPI | **模块暂不可用** |

### 3.2 核心板 / 主板「尽量不要用」

来自 `project/尽量不要使用的引脚.txt` 与逐飞说明：

| 引脚 | 原因 |
|------|------|
| **A19, A20, A5, A6, A4, A3** | 与核心板特殊功能相关，使用可能导致异常 |
| **A14** | 核心板蓝色 LED；IO 富余时应优先留给状态指示 |

### 3.3 已知冲突（必须处理）

| 冲突 | 引脚 | 现状与策略 |
|------|------|------------|
| **无线 RX vs 右编码器 CH1** | **B7** | **当前优先无线串口**；电机/编码器/底盘在 `main` 中关闭。恢复底盘须先改编码器脚（勿再用 B7）或改通信脚。 |
| **UART3 暂禁用** | A14/A13 | 原命令串口 UART3 已关闭；A14 给 LED。 |
| **WiFi SPI 暂不可用** | SPI0 组 | 命令/日志已改无线 UART；勿再依赖 `SYS_LOG_WIFI` / `wifi_spi_read_buffer`。 |
| **右电机 DIR vs GUI 按键例程** | B13 | 恢复电机后与 `mjc_input_button` 示例冲突，启用屏按键前改脚。 |
| **SPI0 总线** | A9/A12/A13… | WiFi SPI 与 IPS 屏常共用 SPI0 不同 CS。 |
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

- **当前固件：底盘关闭**（`chassis_init` / `chassis_update` 未调用）。
- 差速：`chassis` 依赖 motor + encoder；IMU 可选（航向/角速度闭环）。
- 恢复步骤：改编码器脚避开 B6/B7/B2 → 恢复 `motor_init` / `chassis_init` 与 soft_timer → 回写本文。

---

## 5. 通信

| 通道 | 外设 | 引脚 | 波特率 / 速率 | 用途 |
|------|------|------|---------------|------|
| **命令 + 日志（主）** | **UART1 无线转串口** | **B6 TX / B7 RX / B2 RTS** | 115200 | `sys_log_init(SYS_LOG_WIRELESS)` + `wireless_uart_read_buffer` |
| 调试 + 命令（辅） | UART0 | A10/A11 | 115200 | `debug_init` / printf / `cmd_service` |
| ~~命令/日志 WiFi SPI~~ | SPI0 | A12/A9/A13/B20/A17/A16 | — | **暂不可用** |
| ~~命令 UART3~~ | ~~UART3~~ | ~~A14/A13~~ | — | **暂禁用** |

日志类型（`sys_log.h`）：

| 枚举 | 后端 |
|------|------|
| `SYS_LOG_WIRELESS` | `wireless_uart_*`（**当前默认**） |
| `SYS_LOG_UART` | Debug UART0 |
| `SYS_LOG_WIFI` | WiFi SPI（保留，硬件可用后再开） |

无线脚在 `zf_device_wireless_uart.h`：`WIRELESS_UART_RX_PIN=UART1_TX_B6`，`WIRELESS_UART_TX_PIN=UART1_RX_B7`（命名：宏指「接模块 TX/RX 的 MCU 侧」）。

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
**启用 GUI 前**：对照 §3 占用表，避免与 WiFi / 电机冲突。

### 6.3 GUI 按键示例（未与电机共存）

`mjc_input_button.c` 曾用 B13/B12/B14/B15 —— **B12/B13 已被右电机占用**，启用前必须重映射。

---

## 7. 软件定时与中断节拍（非引脚，但属“硬件节拍”）

硬件 PIT 只做 1ms 时间基准；业务用 soft_timer：

| 周期 | 任务 |
|------|------|
| 1 ms | `imu_update` |
| ~~2 ms~~ | ~~`encoder_update` + `motor_update`~~（底盘关闭） |
| ~~10 ms~~ | ~~`chassis_update`~~（底盘关闭） |
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
| 2026-07-18 | 命令/日志改无线串口 B6/B7/B2；WiFi SPI 停用；电机/编码器/底盘暂关（B7 冲突） |
| 2026-07-18 | 增加 A14 LED soft_timer 心跳（优先于 UART3_TX） |
| 2026-07-18 | 禁用命令 UART3；命令仅 WiFi SPI + Debug UART0；A13 专供 WiFi MISO |

---

## 10. 相关文档

- **`docs/motherboard_3507_pinout.md`** — **3507 主板引脚原图修正表**（现用底板；无 3519 主板）  
- `docs/MSPM0G3507_vs_MSPM0G3519.md` — 芯片与资源差异  
- `docs/PORTING_NOTES.md` — 从 3507 工程移植步骤  
- `CLAUDE.md` — 工程总览（**软件已启用资源**以本文为准；**主板丝印**以 `motherboard_3507_pinout.md` 为准）  
- `project/尽量不要使用的引脚.txt` — 逐飞核心板禁用脚原文  
)
