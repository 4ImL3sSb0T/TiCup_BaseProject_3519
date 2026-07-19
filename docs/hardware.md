# 硬件资源手册（固件已启用）

> **职责（仅此）**  
> 记录 **本工程固件当前已初始化、已占用** 的引脚 / 定时器 / 通信通道。  
> **不写** 主板丝印全集、未接线外设、芯片规格对比——那些见文末「相关文档」。
>
> **强制规则**  
> 改脚 / 改外设前先读本文；改完代码必须回写本文。  
> 代码与本文冲突时，以 **本文 + 主板丝印** 为准。  
> 新功能选脚还须避开核心板慎用脚（见 `project/尽量不要使用的引脚.txt`，丝印侧见 `motherboard_3507_pinout.md`）。

| 项 | 值 |
|----|-----|
| 平台 | MSPM0G3519 + 逐飞 3519 库（**勿混 3507 库**） |
| 实物底板 | MSPM0G3507 主板（尚无 3519 专用主板） |
| 主时钟 | 80 MHz |
| 工程 | `BaseProject_3519` |

**配置源文件（改脚时改这些并回写本文）：**

| 模块 | 位置 |
|------|------|
| 入口 / LED | `project/user/src/main.c` |
| 电机 | `service/motion/motor.h` |
| 编码器 | `service/motion/encoder.h` |
| 命令 | `service/com/cmd_service.c` |
| 日志 | `service/sys/sys_log.*` |
| 参数 / LFS | `service/com/param.*`、`service/fs/*`、`bsp/flash` |
| GUI | `service/gui/gui_app.c`、`MujicaUI_Lite/mjc_input_button.c` |
| IMU 库默认 | `libraries/zf_device/zf_device_imu963ra.h` |
| 调试串口 | `libraries/zf_common/zf_common_debug.h` |

---

## 1. 已启用资源总表

| 功能 | 外设 / 定时器 | 引脚 | 说明 |
|------|---------------|------|------|
| 左电机 PWM | TIM_G0 CH0 | **B10** | `PWM_TIM_G0_CH0_B10`；电机座 **M4** |
| 左电机 DIR | GPIO | **B11** | M4 同组 |
| 右电机 PWM | TIM_A0 CH2 | **B12** | `PWM_TIM_A0_CH2_B12`；电机座 **M2** |
| 右电机 DIR | GPIO | **B13** | M2 同组 |
| 左编码器 | TIM_G8 正交 | **A26 / A27** | polarity=0 |
| 右编码器 | TIM_G9 正交 | **B7 / B9** | polarity=1；B7=CH1 |
| 底盘 | software | — | `chassis_init` + 10 ms `chassis_update` |
| 系统 1 ms | PIT_TIM_G12 | — | `sys_time_ms` |
| 命令 + 日志 | UART0 | **A10 TX / A11 RX** | 115200；`SYS_LOG_UART` |
| 核心板 LED | GPIO | **A14** | 500 ms 心跳 |
| IMU963RA | SPI1 | **B23/B22/B21 + CS B19** | `imu_init` |
| IPS200 | SPI0 | **A12/A9/A7/A15/A8/A13** | SCK/MOSI/RST/DC/CS/BLK |
| GUI 按键 | GPIO | **A30/A31/B0/B1** | UP/DOWN/MAIN/AUX |
| 参数持久化 | DATA Flash + LFS | — | `/param.txt`；非 MAIN `storage` |

- 电机 PWM 频率：`MOTOR_PWM_FREQ_HZ = 17000`
- 电机 duty 上限：`MOTOR_MAX_DUTY = 7200`（勿长期满占空堵转）
- 速度环误差死区：`MOTOR_DEADSPEED_THRESHOLD_DEFAULT = 0.4`（counts/2ms）
- 电机座物理分组（M1–M4）见 **`motherboard_3507_pinout.md`**；本文只记本工程接了哪两组。

---

## 2. 定时器占用

| 定时器 | 用途 | 可否再占 |
|--------|------|----------|
| TIM_A0 | 右电机 PWM（CH2=B12） | CH2 否；其它 CH 可规划 |
| TIM_G0 | 左电机 PWM（B10） | 否 |
| TIM_G8 | 左正交编码器 | 否 |
| TIM_G9 | 右正交编码器 | 否 |
| PIT_TIM_G12 | 系统 1 ms | 否 |

库限制：`encoder_quad_init` **仅** TIM_G8 / TIM_G9。

---

## 3. 引脚占用（已启用）

| 引脚 | 功能 |
|------|------|
| A7 | IPS200 RST |
| A8 | IPS200 CS |
| A9 | IPS200 MOSI（SPI0） |
| A10 | UART0 TX |
| A11 | UART0 RX |
| A12 | IPS200 SCK（SPI0） |
| A13 | IPS200 BLK |
| A14 | 核心板 LED |
| A15 | IPS200 DC |
| A26 / A27 | 左编码器 A/B |
| A30 / A31 / B0 / B1 | GUI 按键 |
| B7 / B9 | 右编码器 CH1 / CH2 |
| B10 / B11 | 左电机 PWM / DIR |
| B12 / B13 | 右电机 PWM / DIR |
| B19 / B21 / B22 / B23 | IMU CS / MISO / MOSI / SCK |

因占用产生的约束（选脚时注意）：

| 约束 | 说明 |
|------|------|
| **B7** | 已给右编码器；无线 UART1 RX 同脚，**无线未 init** |
| **SPI0** | 已给 IPS200；WiFi SPI 同总线，**未启用** |
| **A14** | 已给 LED；勿再叠 UART3_TX 等业务 |

---

## 4. 模块接线摘要

### 4.1 电机 + 编码器 + 底盘

| 轮 | PWM | DIR | 编码器 | 定时器 |
|----|-----|-----|--------|--------|
| 左 | B10 | B11 | A26/A27 | TIM_G0 + TIM_G8 |
| 右 | B12 | B13 | B7/B9 | TIM_A0 + TIM_G9 |

- 源码：`motor.*` / `encoder.*` / `chassis.*`
- 命令 mask：`bit0=左`，`bit1=右`
- 上电默认 `CHASSIS_MODE_IDLE`；串口：`chassis status` / `chassis openloop ...`
- 方向反了：编码器改 `polarity`，电机机械反向改 `dir_reverse`；勿乱换左右脚
- 上电时序：`motor_init` 在 `param_init` 之后、**`fs_init` 之前**（缩短 LFS mount/format 期间电机脚悬空）。`pwm_init(duty=0)` + `motor_stop_all()` 后 PWM 强制低并按 `dir_reverse` 写 DIR

### 4.2 通信（仅 UART0）

| 通道 | 外设 | 引脚 | 用途 |
|------|------|------|------|
| 命令 + 日志 | UART0 | A10/A11 | `sys_log_init(SYS_LOG_UART)` + `cmd_service` |

### 4.3 IMU / 屏 / 按键

| 模块 | 要点 |
|------|------|
| IMU | SPI1 @ ~8 MHz；脚见 §1 |
| IPS200 | `gui_app` → MujicaUI；竖屏 240×320；100 ms 刷新 |
| 按键 | 上拉、低有效；`mjc_input_button.c` 与 `KEY_LIST` 一致 |

---

## 5. 软件节拍

PIT 仅提供 1 ms；业务 soft_timer：

| 周期 | 任务 |
|------|------|
| 1 ms | `imu_update` |
| 2 ms | `encoder_update` + `motor_update` |
| 5 ms | GUI 按键 |
| 10 ms | `chassis_update` |
| 20 ms | `cmd_service_task` |
| 100 ms | GUI 刷新 |
| 500 ms | LED 心跳 |

主循环：`soft_timer_process()` + `event_process_async()`。

---

## 6. 改脚检查清单

1. 读本文 §1–3，确认目标脚空闲且不与上表冲突  
2. 查 `project/尽量不要使用的引脚.txt` 与 `motherboard_3507_pinout.md`（丝印/慎用脚）  
3. 查逐飞库是否存在目标 `PWM_TIM_*` / `UART*` / 正交脚组合  
4. 改对应模块宏 → **回写本文** → 同步 `main.c` 资源注释  
5. 动 UART/定时器向量时查 `isr.c`  
6. 编译 + 上电自检：LED / 串口 / 编码器 / 电机点动  

---

## 7. 变更记录

| 日期 | 变更 |
|------|------|
| 2026-07-19 | `motor_init` 提前到 `fs_init` 之前；init 末尾 `motor_stop_all` |
| 2026-07-19 | 对照代码复核：补方向/上电时序说明；修正 main.c 过时电机脚注释（曾误写 B10=DIR） |
| 2026-07-19 | 文档职责收敛：本文仅保留**已启用**资源；丝印/慎用脚全文迁出 |
| 2026-07-19 | 左电机 M4（B10 TIM_G0 + B11）；右 M2（B12/B13）；底盘/编码器启用；无线关 |
| 2026-07-19 | GUI IPS200 + 按键 A30/A31/B0/B1；命令/日志仅 UART0 |
| 2026-07-18 | 初版与正交编码器、UART/LED 等策略 |

---

## 8. 相关文档

| 文档 | 职责 |
|------|------|
| [`motherboard_3507_pinout.md`](motherboard_3507_pinout.md) | 3507 主板丝印 / 可接外设全集 / 电机座分组 |
| [`MSPM0G3507_vs_MSPM0G3519.md`](MSPM0G3507_vs_MSPM0G3519.md) | 芯片规格与库差异 |
| [`PORTING_NOTES.md`](PORTING_NOTES.md) | 从 3507 移植步骤（历史） |
| [`../CLAUDE.md`](../CLAUDE.md) | Agent / 工程导读 |
| `project/尽量不要使用的引脚.txt` | 核心板慎用脚官方列表副本 |
