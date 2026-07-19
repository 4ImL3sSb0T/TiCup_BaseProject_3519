# Command Service 与命令解析说明

## 概述

命令服务负责从输入源接收文本命令，按行切分后交给解析器执行，并输出统一 ACK。

- 位置: `project/code/service/com/`
- 相关文件:
  - `cmd_service.c/.h`: 输入聚合、分行、调度、ACK 发射
  - `parser.c` + `parse.h`: 命令分词、命令表匹配、处理函数分发
  - `param.c/.h`: `set/get/show/export/save/load` 参数系统（LFS `/param.txt` key=value）

引脚与通道占用：见 **`docs/hardware.md`**（本文只写协议与软件行为）。

## 主要事项

### 1) 输入源与调度

**当前固件主输入：** Debug 串口环形缓冲（**UART0**）。

代码中仍保留 WiFi SPI / 无线等通道宏，是否启用以 `hardware.md` 与 `cmd_service` 编译开关为准（现状：无线关、WiFi 未用）。

调度方式:

- `cmd_service_init()` 初始化各通道 FIFO
- `cmd_service_task()` 周期运行（soft timer 每 20ms）

### 2) 命令帧协议（行协议）

- 一条命令以 `\n` 结束
- 兼容 `\r\n`，解析前会去掉末尾 `\r`
- 命令最大长度由 `COMMAND_MAX_LEN` 限制（当前为 `128`）

可选序号封装（用于 ACK 对齐）:

```text
@<seq> <command...>\n
```

示例:

```text
@12 navigator goto 1.0 2.0 90\n
navigator status\n
```

### 3) ACK 输出规则

每条命令执行后输出统一 ACK:

```text
{cmd_ack} seq=<n> result=<EXIT_NAME> [ctx=<context>]
```

说明:

- 无序号命令默认 `seq=-1`
- `ctx` 会做安全字符净化（非法字符替换为 `_`）
- 解析错误或协议错误也会产生 ACK，便于上位机闭环

### 4) 解析器行为

`parse_command(i32 seq, char *input)` 流程:

1. 用 `_strtok_r` 按空白分词
2. 参数数量上限 `COMMAND_MAX_ARGS`（当前为 `8`）
3. 在 `command_list[]` 中按命令名匹配
4. 调用对应 handler，返回 `cmd_exec_result_t`
5. 未匹配命令走 `default_handler`

## 技术要点

### 1) 输入轮询模型

`cmd_service_task` 轮询各已启用通道 FIFO，再按行解析。  
（若日后恢复 UART 中断接收，需在任务侧对对应 IRQ 做短临界区保护 FIFO。）

### 2) 长命令保护

若缓冲区前段数据持续无换行且达到长度阈值，将触发超长保护并清理该通道 FIFO，防止解析器被垃圾流阻塞。

### 3) 返回码语义

- `EXIT_OK`: 执行成功
- `EXIT_IN_PROGRESS`: 已接受，异步进行中
- 其他错误码: 参数错误、未知命令、不支持等

命令服务会对所有返回码发 ACK；除 `OK/IN_PROGRESS` 外会记录错误日志。

### 4) 实时性建议

- ISR 中只做收字节入队，避免复杂逻辑和日志
- 命令处理函数避免阻塞，长任务建议返回 `EXIT_IN_PROGRESS` 并走异步事件

## 当前内置命令族（见 `parser.c`）

**调试速查（语法 / 示例 / 参数表）：`docs/serial_commands.md`**

| 命令 | 作用 |
|------|------|
| `help` | 列出已注册命令 |
| `set` / `get` / `show` / `export` / `save` / `load` | 参数系统（RAM + LFS `/param.txt`） |
| `motor` | 电机：mask + stop/set/mode/status/param |
| `chassis` | 底盘：mode/set/heading/stop/status/param |

参数要点：

- 运行时改查：`set` / `get` / `show [prefix]`（内存注册表）
- 备份可读文本：`export`（打印 key=value，不写 Flash）
- 持久化：`save` / `load` → LittleFS `/param.txt`（DATA Flash，经 `fs_service`）
- 启动顺序：`fs_init` → `param_init` → 各模块 `param_add` → `param_load`

> 旧工程中的 `navigator` / `estimator` / `openart` / `sokoban` / `timer` 等 **本固件未注册**。

## 扩展新命令

1. 实现 handler: `cmd_exec_result_t xxx_handler(i32 seq, int argc, char **argv)`
2. 在 `parser.c` 的 `command_list[]` 注册命令名与 handler
3. 若需可调参数，接入 `param.c` 的参数注册与存储体系
4. 通过串口发送命令验证 ACK 与返回码

## 联调检查清单

- 是否以 `\n` 结束（最常见问题）
- 是否超出参数个数上限（`COMMAND_MAX_ARGS`）
- 是否超出命令长度限制（`COMMAND_MAX_LEN`）
- 是否收到 `{cmd_ack}` 且 `seq` 与上位机请求一致
- UART 高流量下是否出现 FIFO 丢包日志

## 相关文档

- [串口调试命令手册](../../../../docs/serial_commands.md)（语法、示例、参数表）
- [硬件已启用资源](../../../../docs/hardware.md)
- [事件系统](../../common/event/README.md)
