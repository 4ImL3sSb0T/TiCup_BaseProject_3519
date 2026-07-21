# 串口调试命令手册

> **职责**：固件当前已注册、可在串口调试中使用的命令速查。  
> 权威命令表以 `project/code/service/com/parser.c` 的 `command_list[]` 为准。  
> 协议与架构细节见 `project/code/service/com/README.md`；引脚见 `docs/hardware.md`。

---

## 1. 连接参数

| 项 | 值 |
|----|-----|
| 外设 | UART0（Debug） |
| 引脚 | **A10 TX / A11 RX** |
| 波特率 | **115200** 8N1 |
| 输入源 | `debug_read_ring_buffer`（无线通道当前关闭） |
| 调度 | `cmd_service_task` 每 **20 ms** |
| 行结束 | 必须以 **`\n`** 结束（兼容 `\r\n`） |
| 最大长度 | `COMMAND_MAX_LEN` = **128** |
| 最大参数数 | `COMMAND_MAX_ARGS` = **8**（含命令名本身） |

串口助手建议：

- 发送时勾选「发送新行」或手动加 `\n`
- 工具：SSCOM / PuTTY / MobaXterm / Python `pyserial` 均可

---

## 2. 帧协议与 ACK

### 普通命令

```text
help\n
chassis status\n
```

### 带序号（可选，便于上位机对齐 ACK）

```text
@<seq> <command...>\n
```

示例：

```text
@12 chassis status\n
@13 motor 0x3 status\n
```

- `seq` 为非负整数；无序号时 ACK 中 `seq=-1`

### ACK 格式（每条命令执行后都会发）

```text
{cmd_ack} seq=<n> result=<EXIT_NAME> [ctx=<context>]
```

常见 `result`：

| result | 含义 |
|--------|------|
| `EXIT_OK` | 成功 |
| `EXIT_IN_PROGRESS` | 已接受，异步进行中 |
| `EXIT_INVALID_PARAM` | 参数错误 / 用法不对 |
| `EXIT_NOT_SUPPORTED` | 未知命令或不支持 |
| `EXIT_DOES_NOT_EXIST` | 参数名不存在 |
| `EXIT_NOT_INITIALIZED` | 模块未初始化（如 chassis/IMU） |
| `EXIT_FAIL` | 通用失败 |

`ctx` 为可选机器可读上下文（非法字符会被替换为 `_`）。

业务日志与 ACK 都走同一 Debug UART，调试时注意区分。

---

## 3. 命令一览（当前固件）

| 命令 | 作用 | 出处 |
|------|------|------|
| `help` | 列出已注册命令名 | `parser.c` |
| `set` / `get` / `show` / `export` / `save` / `load` | 参数系统 | `param.c` |
| `motor` | 单/双电机控制 | `motor.c` |
| `chassis` | 差速底盘控制 | `chassis.c` |
| `track` | 光电循迹（传感+闭环） | `track_follow.c` + `driver/track.c` |

> 旧文档中的 `navigator` / `estimator` / `openart` / `sokoban` / `timer` 等 **当前未注册**，发送会返回 `EXIT_NOT_SUPPORTED`。

上电后可先发：

```text
help
```

期望日志类似：

```text
Available commands:
 - set
 - get
 - show
 - export
 - save
 - load
 - motor
 - chassis
 - track
 - help
{cmd_ack} seq=-1 result=EXIT_OK ctx=help_listed
```

---

## 4. 参数命令

运行时改的是 **RAM 注册表**；`save`/`load` 读写 LittleFS **`/param.txt`**（DATA Flash）。

| 语法 | 说明 |
|------|------|
| `set <name> <value>` | 设置参数（仅 RAM） |
| `get <name>` | 读取单个参数 |
| `show [prefix]` | 列表；可选前缀过滤 |
| `export` | 打印可存储参数的 `key=value`（不写 Flash） |
| `save` | RAM → `/param.txt` |
| `load` | `/param.txt` → RAM |

### 示例

```text
show
show motor
show chassis
get motor_kp
set motor_kp 150
set chassis_max_v 15
export
save
load
```

### 当前已注册参数（默认值）

| 名称 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `motor_kp` | FLOAT | 120 | 速度环 Kp |
| `motor_ki` | FLOAT | 200 | 速度环 Ki |
| `chassis_half_track` | FLOAT | 2.0 | 半轮距（运动学） |
| `chassis_max_v` | FLOAT | 18.0 | 线速度限幅 |
| `chassis_max_omega` | FLOAT | 80.0 | 角速度限幅（°/s，yaw_rate） |
| `chassis_ol_v_scale` | FLOAT | 340 | 开环 v→duty 比例 |
| `chassis_ol_w_scale` | FLOAT | 250 | 开环 ω→duty 比例 |
| `chassis_omega_to_wheel` | FLOAT | 0.15 | IMU deg/s → 轮速域 |
| `chassis_imu_yaw_sign` | FLOAT | -1.0 | 航向/陀螺符号（乘到 gyro.z / yaw）；方向反了改符号 |
| `chassis_yaw_rate_kp` | FLOAT | 1.2 | 角速度环 Kp |
| `chassis_yaw_rate_ki` | FLOAT | 0.3 | 角速度环 Ki |
| `chassis_heading_kp` | FLOAT | 2.0 | 航向环 Kp（对角度误差） |
| `chassis_heading_ki` | FLOAT | 0.0 | 航向环 Ki（建议 0，归正易极限环） |
| `chassis_heading_kd` | FLOAT | 0.15 | 航向环 Kd（对 gyro.z 阻尼，非 d(err)/dt） |

**注意：**

1. `set` 只改 RAM；`motor` / `chassis` 的 PID 要生效还需对应 `param` 子命令（见下）。
2. 上电顺序：`param_init` → 各模块 `param_add` → `param_load`，故掉电后靠 `save` 的值会在下次启动 `load` 回来。
3. `set` 后立刻 `save` 可持久化。

---

## 5. motor 命令

```text
motor <mask> <stop|set|mode|status|param> [val]
```

### mask（可多选，按位）

| mask | 含义 |
|------|------|
| `0x1` / `1` | 左电机 |
| `0x2` / `2` | 右电机 |
| `0x3` / `3` | 左右同时 |

支持 `0x` 十六进制或十进制。

### 子命令

| 语法 | 说明 |
|------|------|
| `motor <mask> stop` | 停止 |
| `motor <mask> set <val>` | 按当前模式设目标 |
| `motor <mask> mode speed\|position\|openloop` | 切换模式 |
| `motor <mask> status` | 打印位置/速度/目标/模式/死区 |
| `motor <mask> param` | 将 `motor_kp/ki` 应用到速度 PID |

`set` 的 `val` 含义取决于模式：

| 模式 | `set` 单位 / 建议 |
|------|-------------------|
| `speed`（默认） | 编码器 counts/2ms；建议先 **5~12**，上限约 20 |
| `position` | 目标位置（编码器计数） |
| `openloop` | PWM duty（整数）；限幅约 **7200**（≈8.6 V @12 V，勿长期堵转） |

### 示例

```text
motor 0x3 status
motor 0x1 mode speed
motor 0x1 set 8
motor 0x2 set -8
motor 0x3 stop

motor 0x3 mode openloop
motor 0x1 set 1500
motor 0x3 stop

set motor_kp 150
motor 0x3 param
```

**安全：** 勿长时间堵转；开环/速度目标从小到大摸。

---

## 6. chassis 命令

```text
chassis <mode|set|heading|stop|status|param> ...
```

### 模式

| 模式 | 含义 | 依赖 |
|------|------|------|
| `idle` | 停车 | — |
| `openloop` | v,ω → 左右 PWM（电机开环） | — |
| `speed` | v,ω → 左右轮速（编码器速度环） | — |
| `yaw_rate` | 速度 + gyro.z 角速度内环 | **IMU 就绪** |
| `heading` | 航向外环 → 角速度内环 → 速度 | **IMU 就绪** |

### 子命令

| 语法 | 说明 |
|------|------|
| `chassis mode <name>` | 切模式 |
| `chassis set <v> <omega>` | 设线速度 / 角速度目标 |
| `chassis heading <deg>` | 设航向目标（度） |
| `chassis stop` | 停车并切 idle |
| `chassis status` | 打印模式、目标、yaw、轮速等 |
| `chassis param` | 将 chassis_* PID 参数应用到环 |

速度单位与电机一致：**counts/2ms**（MG310 空载约 18）。  
`v` / `omega` 为抽象量，需结合 `chassis_half_track` 等参数实车标定。

### 示例

```text
chassis status
chassis mode openloop
chassis set 5 0
chassis set 0 3
chassis stop

chassis mode speed
chassis set 8 0
chassis stop

chassis mode yaw_rate
chassis set 6 0
chassis stop

chassis mode heading
chassis heading 90
chassis set 5 0
chassis stop

set chassis_yaw_rate_kp 1.5
chassis param
```

若 IMU 未就绪就进 `yaw_rate` / `heading`，会返回 `EXIT_NOT_INITIALIZED`（`ctx=imu_not_ready`）。

---

## 7. track 命令（光电循迹）

> **分层**：`driver/track` 只做采样/极性/偏差；`service/motion/track_follow` 做状态机 + PID(ω) + 写 chassis。  
> **启用**：`driver/track.h` 中 `TRACK_ENABLE=1` 且定脚后才会 `track_follow_init` + 10 ms 任务。默认 `TRACK_ENABLE=0` 时命令返回 `track_not_init`。  
> **后端**：`TRACK_SENSOR_BACKEND` — `0` 五路数字 GPIO / `1` 八路 GS08RA。  
> **冲突**：循迹运行时勿并行 `chassis openloop/set` 等，以免抢写底盘。

```text
track <status|start|stop|polarity|cal|param> ...
```

| 语法 | 说明 |
|------|------|
| `track status` | 状态 / mask / error / backend / 极性 |
| `track start [v]` | 开始循迹（SPEED 模式）；可选线速度 |
| `track stop` | 停止并 chassis idle |
| `track polarity 0\|1` | 赛道有效电平（数字）/ 黑白极性（GS08） |
| `track cal max` / `track cal min` | **仅 GS08**：标定白/黑 |
| `track param` | 将 `track_kp/ki/kd` 等刷进 PID |

相关 param：`track_base_v`、`track_kp`/`ki`/`kd`、`track_lost_v`/`track_lost_w`/`track_lost_ms`、`track_omega_max`、`track_polarity`；GS08 另有 `track_gs_thr`。

### 示例

```text
track status
track polarity 0
track start 8
track stop

set track_kp 2.0
track param
```

---

## 8. 推荐调试流程

### 8.1 通路自检

```text
help
show
chassis status
motor 0x3 status
```

能收到 `help` 列表和 `{cmd_ack}` 即串口与命令服务正常。

### 8.2 单电机开环

```text
motor 0x1 mode openloop
motor 0x1 set 1200
motor 0x1 status
motor 0x1 stop
```

确认左右转向是否与预期一致（不对可查 DIR / `dir_reverse`）。

### 8.3 单电机速度环

```text
motor 0x1 mode speed
motor 0x1 set 8
motor 0x1 status
motor 0x1 stop
```

### 8.4 底盘开环 / 速度

```text
chassis mode openloop
chassis set 5 0
chassis status
chassis stop

chassis mode speed
chassis set 6 1
chassis stop
```

### 8.5 改参并持久化

```text
set motor_kp 140
set motor_ki 180
motor 0x3 param
save
```

重启后确认：

```text
get motor_kp
```

---

## 9. 常见问题

| 现象 | 排查 |
|------|------|
| 无任何回显 | 波特率 115200？TX/RX 交叉？是否发了 `\n`？ |
| 无 ACK | 行尾是否为 `\n`；命令是否超长（>128） |
| `unknown_command` | 命令名拼写；是否用了未移植的旧命令 |
| `usage_motor` / `usage_chassis` | 参数个数不够，先看 Usage 日志 |
| `imu_not_ready` | 等 IMU 初始化成功后再切 yaw_rate/heading |
| 改了 PID 无变化 | `set` 后是否执行了 `motor … param` 或 `chassis param` |
| 重启参数丢了 | 是否 `save` 成功；LFS `/param.txt` 是否可用 |
| 电机不转 / 狂转 | 从 `openloop` 小 duty 或 `speed` 小目标起步；检查接线座 M2/M4 |

---

## 10. 带序号联调示例

```text
@1 help
@2 chassis status
@3 motor 0x3 status
@4 chassis mode speed
@5 chassis set 5 0
@6 chassis stop
```

期望每条后有：

```text
{cmd_ack} seq=1 result=EXIT_OK ...
{cmd_ack} seq=2 result=EXIT_OK ...
...
```

---

## 相关文档

- 协议 / 服务架构：`project/code/service/com/README.md`
- 已启用硬件：`docs/hardware.md`
- 电机引脚与座：`docs/motherboard_3507_pinout.md`
- 命令表源码：`project/code/service/com/parser.c`
- 处理函数：`motor.c` / `chassis.c` / `param.c`
