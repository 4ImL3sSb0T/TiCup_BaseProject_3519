# AGENTS.md

Guidance for coding agents working in this repository.

## Project

TI 杯基地工程 **BaseProject_3519**（MSPM0G3519），由 `BaseProject`（MSPM0G3507）移植。  
逐飞 SEEKFREE 库 + TI DriverLib；**勿混用 3507 的 `libraries/`**。

更完整的工程导读见 **`CLAUDE.md`**（架构、boot、Build）。本文侧重 **文档职责与改脚规则**。

---

## 文档职责要点（必守）

| 文档 | 职责 | 不要写进该文档 |
|------|------|----------------|
| **`docs/hardware.md`** | **固件当前已启用** 的引脚 / 定时器 / 通道 / soft_timer | 主板丝印全集、未启用外设详表、芯片规格对比 |
| **`docs/motherboard_3507_pinout.md`** | 3507 主板丝印、可接外设、共用总线、电机座 M1–M4 物理分组 | 固件实时占用表（链到 hardware） |
| `docs/MSPM0G3507_vs_MSPM0G3519.md` | 3507 vs 3519 片内资源、库枚举、移植清单 | 本仓库当前接线 / 启用状态 |
| `docs/PORTING_NOTES.md` | 从 3507 迁过来做了什么、编译自检 | 实时硬件占用（链到 hardware） |
| `project/尽量不要使用的引脚.txt` | 核心板慎用脚官方列表副本 | 业务占用说明 |
| `CLAUDE.md` / `AGENTS.md` | 工程导读、本文档职责、boot 摘要 | 完整引脚表（只摘要 + 链接） |
| 模块 `README.md`（com/gui/…） | 该模块 API / 协议 / 用法 | 引脚占用现状（链到 hardware） |

### 规则

1. 改脚 / 改外设 / 改定时器：**先读并回写 `docs/hardware.md`**，再改代码。
2. 查丝印 / 能不能接某座：看 **`motherboard_3507_pinout.md`**，再对照 hardware 是否已被占。
3. 慎用脚：`A19 A20 A5 A6 A4 A3` 尽量不用；`A14` 优先留给 LED → 见 `project/尽量不要使用的引脚.txt`。
4. 禁止在多份文档里复制完整占用表；摘要可以，权威只在一处。
5. 代码与文档冲突时：以 **`hardware.md` + 主板丝印** 为准，并同步回写。

### 权威分工（一句话）

- **已启用资源** → `docs/hardware.md`
- **主板物理能力** → `docs/motherboard_3507_pinout.md`
- **芯片差异** → `docs/MSPM0G3507_vs_MSPM0G3519.md`
- **移植历史** → `docs/PORTING_NOTES.md`

---

## Build（速查）

- Keil：`project/mdk/SeekFree_MSPM0G3519_Device_Library.uvprojx`
- 移动工程后：`Project → Clean` 再编译
- 新增 `project/code` 源文件须加入 uvprojx 的 **code** 组

## 3519 注意

- 无 UART2，兼容用 **UART7**
- Flash 512KB / RAM 128KB；宏 `__MSPM0G3519__`
- 参考例程：`D:\Project\TI_Cup\MSPM0G3519_Library-master\Example\`
