# Command Service 与命令解析说明

## 概述

命令服务负责从多个输入源接收文本命令，按行切分后交给解析器执行，并输出统一 ACK。

- 位置: `project/code/module/communication/`
- 关键文件:
  - `cmd_service.c/.h`: 输入聚合、分行、调度、ACK 发射
  - `parser.c` + `parse.h`: 命令分词、命令表匹配、处理函数分发
  - `param.c/.h`: `set/get/show/save/load` 参数系统

## 主要事项

### 1) 输入源与调度

当前支持 4 路输入源:

- WiFi SPI
- Debug 串口环形缓冲
- UART2 中断接收
- UART3 中断接收

调度方式:

- `cmd_service_init()` 初始化 FIFO 与 UART2/UART3 接收中断
- `cmd_service_task()` 周期运行（当前工程由 soft timer 每 30ms 异步触发）

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

### 1) 并发模型（UART 通道）

UART2/UART3 由中断写入 FIFO，`cmd_service_task` 读取 FIFO。
为避免并发读写同一 FIFO 元数据导致竞争，当前实现采用最小临界区策略:

- 在任务侧处理 UART 通道前临时 `interrupt_disable(LPUARTx_IRQn)`
- 完成“取一条完整命令行”后立即 `interrupt_enable(LPUARTx_IRQn)`
- `parse_command()` 与 `cmd_ack_emit()` 在临界区外执行，减少关中断时间

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

- 参数命令: `set/get/show/save/load`
- 帮助命令: `help`
- 运动相关: `chassis`, `motor`
- 状态估计: `set_position`, `estimator`
  `estimator mode <inertial|external|fusion|0|1|2>` 用于切换/查询模式；
  `estimator init_pos <x> <y> <yaw_deg>` 用于校准惯性初始位置偏移
  `estimator reset` 在 fusion 模式下硬重置 EKF，并吸附到最新视觉坐标
- OpenART: `openart init_pos <status|update|refresh|send>`
  OpenART 返回 `{init_pos_evt} state=ready ctx=x_mm=...;y_mm=...;yaw_deg=...` 时，MCU 会自动调用 estimator 初始位姿校准
- 导航: `navigator`
- 任务模块: `sokoban`
- 计时器: `timer`

## 扩展新命令

1. 实现 handler: `cmd_exec_result_t xxx_handler(i32 seq, int argc, char **argv)`
2. 在 `parser.c` 的 `command_list[]` 注册命令名与 handler
3. 若需可调参数，接入 `param.c` 的参数注册与存储体系
4. 通过串口/WiFi 发送命令验证 ACK 与返回码

## 联调检查清单

- 是否以 `\n` 结束（最常见问题）
- 是否超出参数个数上限（`COMMAND_MAX_ARGS`）
- 是否超出命令长度限制（`COMMAND_MAX_LEN`）
- 是否收到 `{cmd_ack}` 且 `seq` 与上位机请求一致
- UART 高流量下是否出现 FIFO 丢包日志

## 相关文档

- [主项目文档](../../../../AGENTS.md)
- [Navigator 文档](../../algorithm/navigator/README.md)
- [事件系统](../../common/event/README.md)
- [应用层代码入口](../../application/mode_sokoban/)

