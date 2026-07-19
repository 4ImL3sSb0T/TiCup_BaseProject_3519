# BaseProject_3519

基于 **MSPM0G3519** 的 TI 杯基地工程，由 `BaseProject`（MSPM0G3507）业务代码移植而来。

## 文档

| 文档 | 职责 |
|------|------|
| [docs/hardware.md](docs/hardware.md) | **固件已启用** 资源（改脚权威） |
| [docs/motherboard_3507_pinout.md](docs/motherboard_3507_pinout.md) | 3507 主板丝印 / 可接外设 |
| [docs/MSPM0G3507_vs_MSPM0G3519.md](docs/MSPM0G3507_vs_MSPM0G3519.md) | 芯片规格与库差异 |
| [docs/PORTING_NOTES.md](docs/PORTING_NOTES.md) | 移植步骤与自检 |
| [CLAUDE.md](CLAUDE.md) | Agent / 开发者工程导读 |

## 快速开始

1. 用 Keil 打开 `project/mdk/SeekFree_MSPM0G3519_Device_Library.uvprojx`
2. Clean → Build → 下载到 MSPM0G3519 板
3. 串口查看 `BaseProject_3519 boot` 日志（引脚见 `docs/hardware.md`）

## 与 BaseProject 关系

- **共享**：`project/code` 应用架构（事件、底盘、电机、IMU、命令服务等）
- **不共享**：`libraries/`、启动文件、链接脚本、设备头文件（必须用 3519 版本）
