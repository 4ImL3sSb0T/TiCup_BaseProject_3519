# BaseProject → BaseProject_3519 移植说明

## 做了什么

1. 将 `BaseProject/project/code/` 全量复制到 `BaseProject_3519/project/code/`
2. 用 3507 业务逻辑重写 `project/user/src/main.c`（平台改为 MSPM0G3519）
3. 保留 3519 的 `isr.c` 框架，修复 TIMG9/G12/G14 的 PIT 回调指针下标
4. 在 Keil 工程 `SeekFree_MSPM0G3519_Device_Library.uvprojx` 的 **code** 组中加入与 3507 相同的应用源文件
5. 注释/文档中的芯片名改为 3519；补充芯片对比文档

**未替换** 的部分（应保持 3519）：

- `libraries/`（SDK + 逐飞 zf_* 驱动）
- `project/mdk/mspm0g3519.sct`、启动文件、设备包配置

## 编译与烧录

1. 打开 `project/mdk/SeekFree_MSPM0G3519_Device_Library.uvprojx`
2. 工程若曾移动路径：`Project → Clean`
3. Build（F7）
4. 使用支持 MSPM0G3519 的调试器烧录（CMSIS-DAP / XDS110 等）

## 运行后自检

串口日志应出现：

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

命令通道：

- 无线串口 UART1（B6/B7）+ Debug UART0
- 示例：`help`、`show`、`export`、`set <name> <value>`、`save`、`load`
- 参数落盘：LittleFS `/param.txt`（key=value 文本，DATA Flash 16 KB）

## 已知注意点

| 项 | 说明 |
|----|------|
| A14 | 核心板 LED；UART3 已禁用，见 `尽量不要使用的引脚.txt` |
| 无 UART2 | 3519 硬件无 UART2；同脚位用 UART7 |
| 参数持久化 | `fs_service` + LFS（DATA Flash）；旧 `storage`（MAIN 扇区）已废弃，勿再接入 param |
| 电机/底盘 | 当前禁用（B7 无线 RX）；恢复后其 `param_add` 会自动参与 save/load |
| GUI/按键 | `mjc_input_button` 已用主板 **A30/A31/B0/B1**（与 `KEY_LIST` 一致） |
| 日志默认 | `main.c` 中 `sys_log_init(SYS_LOG_WIFI)` 与 3507 一致；无 WiFi 时可改为 `SYS_LOG_UART` |

## 目录结构

```text
BaseProject_3519/
  docs/
    MSPM0G3507_vs_MSPM0G3519.md   # 芯片详细对比
    PORTING_NOTES.md              # 本文件
  libraries/                      # 3519 逐飞 + TI SDK
  project/
    code/                         # 应用层（从 3507 移植）
    user/src/main.c               # 入口
    user/src/isr.c                # 3519 中断
    mdk/                          # Keil 工程
```
