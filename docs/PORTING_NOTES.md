# BaseProject → BaseProject_3519 移植说明

> **职责（仅此）**  
> 记录 **从 3507 工程迁到 3519 时做了什么**、如何编译自检。  
> **当前接线 / 已启用外设** 不在本文维护 → 见 **`docs/hardware.md`**。

---

## 做了什么

1. 将 `BaseProject/project/code/` 全量复制到 `BaseProject_3519/project/code/`
2. 用 3507 业务逻辑重写 `project/user/src/main.c`（平台改为 MSPM0G3519）
3. 保留 3519 的 `isr.c` 框架，修复 TIMG9/G12/G14 的 PIT 回调指针下标
4. 在 Keil 工程 `SeekFree_MSPM0G3519_Device_Library.uvprojx` 的 **code** 组中加入与 3507 相同的应用源文件
5. 注释/文档中的芯片名改为 3519；补充芯片对比文档

**未替换** 的部分（应保持 3519）：

- `libraries/`（SDK + 逐飞 zf_* 驱动）
- `project/mdk/mspm0g3519.sct`、启动文件、设备包配置

---

## 编译与烧录

1. 打开 `project/mdk/SeekFree_MSPM0G3519_Device_Library.uvprojx`
2. 工程若曾移动路径：`Project → Clean`
3. Build（F7）
4. 使用支持 MSPM0G3519 的调试器烧录（CMSIS-DAP / XDS110 等）

---

## 运行后自检

串口日志应出现类似：

```text
==== BaseProject_3519 boot ====
fs_init ok
param_init ok
cmd_service_init ok ...
imu_init ok ...
param_load[boot]: ... (or file not found, using defaults)
sys tick 1ms on PIT_G12
init done. try: help / show / export / save
```

命令示例：`help`、`show`、`export`、`set <name> <value>`、`save`、`load`  
参数落盘：LittleFS `/param.txt`（DATA Flash）。  
**当前串口脚、电机/无线是否启用**：以 **`docs/hardware.md`** 为准。

---

## 移植时注意点（芯片/架构，非实时占用表）

| 项 | 说明 |
|----|------|
| 库不可混用 | 只用 3519 的 `libraries/` |
| 无 UART2 | 同脚位兼容用 **UART7** |
| PIT 下标 | 使用枚举名；isr 中 G9/G12/G14 回调下标已修 |
| 参数持久化 | `fs_service` + LFS（DATA Flash）；旧 MAIN 扇区 `storage` 已废弃 |
| 慎用脚 | 见 `project/尽量不要使用的引脚.txt` |
| 芯片对比清单 | 见 `docs/MSPM0G3507_vs_MSPM0G3519.md` §9 |

---

## 目录结构

```text
BaseProject_3519/
  docs/
    hardware.md                   # 固件已启用资源（权威）
    motherboard_3507_pinout.md    # 主板丝印
    MSPM0G3507_vs_MSPM0G3519.md   # 芯片对比
    PORTING_NOTES.md              # 本文件
  libraries/                      # 3519 逐飞 + TI SDK
  project/
    code/                         # 应用层（从 3507 移植）
    user/src/main.c               # 入口
    user/src/isr.c                # 3519 中断
    mdk/                          # Keil 工程
```
