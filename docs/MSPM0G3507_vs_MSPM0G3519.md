# MSPM0G3507 与 MSPM0G3519 对比文档

> 对应工程：`BaseProject`（3507）与 `BaseProject_3519`（3519）  
> 更新日期：2026-07-18

---

## 1. 定位一览

| 项目 | MSPM0G3507 | MSPM0G3519 |
|------|------------|------------|
| 系列 | MSPM0G350x | MSPM0Gx51x |
| 定位 | 性价比、资料成熟 | 大存储、多外设、双 CAN-FD |
| 本仓库工程 | `BaseProject` | `BaseProject_3519` |
| 逐飞库版本（本仓库） | 约 V3.3.x | 约 V3.0.x |
| Keil 设备 | `MSPM0G3507` | `MSPM0G3519` |
| 预定义宏 | `__MSPM0G3507__` | `__MSPM0G3519__` |

两者均为 **Arm Cortex-M0+ @ 最高 80MHz**，均带 **CAN-FD、双 12-bit 4Msps ADC、MathACL**。

---

## 2. 存储与链接脚本

| 项目 | G3507 | G3519 |
|------|-------|-------|
| Flash | **128KB** | **512KB**（×4） |
| Flash 结构 | 单 bank 为主 | **双 bank + 地址交换**（便于 OTA） |
| Data Flash | 无独立 16KB 描述 | 额外 **16KB data flash** |
| SRAM | **32KB** | **128KB**（×4） |
| SRAM 分区 | 连续 32KB | Bank0 64KB（可保留到 STANDBY）+ Bank1 64KB（可保留到 STOP/SLEEP） |
| 本工程 SCT | `mspm0g3507.sct`：IROM `0x20000`，IRAM `0x8000` | `mspm0g3519.sct`：IROM `0x80000`，IRAM `0x20000` |

应用含义：

- 3519 更适合大缓冲（图像行缓存、滤波历史、RTOS 多任务栈）
- 低功耗场景注意 3519 的 **Bank1 在深睡时可能不保留**（链接脚本中有注释）

---

## 3. 通信外设

| 外设 | G3507 | G3519 |
|------|-------|-------|
| UART | 4 路：UART0–3（**有 UART2**） | 7 路：UART0/1/3/4/5/6/7（**无 UART2**） |
| I2C | 2 | 3 |
| SPI | 2 | 3 |
| CAN-FD | 1 | **2** |

### 逐飞库 UART 枚举差异（移植必读）

**G3507：**

```c
UART_0, UART_1, UART_2, UART_3   // 共 4，UART_NUM=4
```

**G3519：**

```c
UART_0, UART_1, /* 无 UART_2 */, UART_3, UART_4, UART_5, UART_6, UART_7
// UART_NUM=7；枚举值连续：UART_3==2, UART_7==6
// 驱动内部 uart_list[] = {UART0, UART1, UART3, ...}
// 官方注释：原 UART2 引脚可用 UART7 兼容
```

本工程命令串口使用 **UART3（A14/A13）**，两端均存在，无需改通道号。

---

## 4. 定时器 / DMA / 看门狗

| 项目 | G3507 | G3519 |
|------|-------|-------|
| 定时器数量 | 7 | 9（多 TIMG9、TIMG14） |
| PWM 通道上限 | ~22 | ~28 |
| QEI | 1 个定时器 | 2 个定时器 |
| DMA | 7 通道 | **12 通道** |
| 看门狗 | 2× WWDT | 2× WWDT + **IWDT** |

### 本工程 PIT 映射

| 用途 | 使用的定时器 | 说明 |
|------|--------------|------|
| 电机 PWM | TIM_A0 | 两端一致 |
| 左/右编码器 | TIM_G8 / TIM_G9（正交） | 两端一致 |
| 1ms 系统节拍 | **PIT_TIM_G12** | 3519 中 PIT 枚举含 G9/G12/G14，G12 下标为 7 |

---

## 5. 模拟与安全

| 项目 | G3507 | G3519 |
|------|-------|-------|
| ADC | 2×4Msps，约 17 通道 | 2×4Msps，约 **27** 通道 |
| DAC | 1×12-bit | 1×12-bit |
| COMP | 3 | 3 |
| **OPA / GPAMP** | **有（零漂移运放）** | **无** |
| AES | 基础 AES | **AESADV**（GCM 等模式更全） |
| 密钥存储 | 弱 | KEYSTORE |
| 安全启动 / 防火墙 | 基础 | 更完善，PSA-L1 目标 |

结论：精密片内运放调理选 **3507**；多通信、大存储、安全/OTA 选 **3519**。

---

## 6. GPIO 与封装

| 项目 | G3507 | G3519 |
|------|-------|-------|
| GPIO | 最多约 60，GPIOA/B | 最多约 **94**，**GPIOA/B/C** |
| 典型封装 | 28/32/48/64 | 32/48/64/**80/100** |

### 尽量不要使用的引脚（3519 核心板）

见 `project/尽量不要使用的引脚.txt`：

- 特殊功能：`A19, A20, A5, A6, A4, A3`
- **A14**：核心板 LED，本工程 UART3 TX 也用到；若 LED 与串口冲突需改引脚或改灯控

---

## 7. 中断向量与 isr.c

| 中断 | G3507 | G3519 |
|------|-------|-------|
| TIMG9 / TIMG14 | 无 | 有 |
| UART2 | 有 | **注释掉** |
| UART4–7 | 无 | 有 |
| CANFD1 | 无 | 有 |

`BaseProject_3519` 保留 3519 官方 isr 框架，并修正了 TIMG9/G12/G14 的 `pit_callback_ptr_list` 下标错误（原版 ptr 下标与 list 不一致，会导致 G12 1ms 节拍回调参数异常）。

---

## 8. 软件栈 / 工程差异

| 项目 | BaseProject (3507) | BaseProject_3519 |
|------|--------------------|------------------|
| 业务代码 | `project/code/**` | 从 3507 **同步移植** |
| 启动文件 | `startup_mspm0g350x_*.s` | `startup_mspm0g351x_*.s` |
| 设备头 | `mspm0g350x.h` | `mspm0g351x.h` |
| SysCtl | `dl_sysctl_mspm0g1x0x_g3x0x` | `dl_sysctl_mspm0gx51x` |
| libraries | 3507 逐飞库 | **3519 逐飞库（不可混用）** |

### 本工程硬件资源（移植后保持）

| 资源 | 配置 |
|------|------|
| 电机左 | DIR=`A1`，PWM=`PWM_TIM_A0_CH0_A0` |
| 电机右 | DIR=`B13`，PWM=`PWM_TIM_A0_CH2_B12` |
| 编码器左 | TIM_G8，CH1=`A26`，CH2=`A27`（正交） |
| 编码器右 | TIM_G9，CH1=`B7`，CH2=`B9`（正交） |
| 系统 1ms | `PIT_TIM_G12` |
| 调试串口 | UART0（A10/A11） |
| 命令串口 | UART3（A14/A13） |

与逐飞 3519 主板电机/编码器例程引脚一致，无需为换芯改脚（除非板级丝印不同）。

---

## 9. 移植检查清单

从 3507 工程迁到 3519 时按此核对：

1. **只用 3519 的 `libraries/`**，不要拷 3507 的 SDK/driver
2. **搜索 `UART_2` / `UART2`** → 改为 `UART_7`（及对应引脚宏）
3. **PIT 枚举**：3519 多了 `PIT_TIM_G9`、`PIT_TIM_G14`，`PIT_TIM_G12` 序号变化，使用枚举名不要写死数字下标
4. **isr.c** 必须使用 3519 向量（无 UART2，有 UART4–7 / TIMG9/14）
5. **链接脚本** 使用 512KB Flash / 128KB RAM
6. **Keil 设备包** 使用 `MSPM0GX51X_DFP`，宏 `__MSPM0G3519__`
7. 新增 `project/code` 文件后必须在 `.uvprojx` 的 `code` 组中添加
8. 工程移动路径后执行 **Project → Clean** 再全量编译

---

## 10. 选型建议（电赛 / 本仓库）

| 需求 | 建议 |
|------|------|
| 跟现有 3507 主板/例程完全一致 | **BaseProject / G3507** |
| 需要更大 RAM/Flash、双 CAN、更多串口 | **BaseProject_3519 / G3519** |
| 需要片上 OPA 精密调理 | **G3507** |
| 需要 OTA 双 bank / 更强安全 | **G3519** |

---

## 11. 参考资料

- TI 产品页：[MSPM0G3507](https://www.ti.com/product/MSPM0G3507) / [MSPM0G3519](https://www.ti.com/product/MSPM0G3519)
- 技术参考手册：MSPM0 G-Series 80MHz TRM（SLAU846）
- 逐飞例程：`MSPM0G3507_Library-master/Example`、`MSPM0G3519_Library-master/Example`
