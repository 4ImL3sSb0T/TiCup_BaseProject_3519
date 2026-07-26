# Scripts / Host UI

Python 工具与可视化串口上位机，通过固件**命令系统**控制小车。

完整命令语法：仓库根目录 [`docs/serial_commands.md`](../docs/serial_commands.md)。

## 环境

```powershell
cd scripts
uv sync
```

- Python ≥ 3.12（`uv` 管理虚拟环境）
- 依赖：`pyserial` / `customtkinter` / `matplotlib`

## 启动上位机

推荐（在 `scripts` 目录下，用包方式）：

```powershell
cd scripts
uv run python -m host
uv run python -m host --port COM8
uv run python -m host --self-check   # 无硬件，协议解析自检
```

也可从仓库根目录直接跑入口（venv 已激活时）：

```powershell
python scripts/host/main.py
python scripts/host/main.py --port COM8
```

UI 结构（`host/ui/`）：`app.py` 壳 + 连接/分发；`tabs/` 驾驶·电机·循迹/任务·参数·控制台；`plot.py` 遥测曲线。

上次使用的 COM 口保存在 `host/host_config.json`（已 gitignore）。

---

## 怎么用（联调）

### 连接参数

| 项 | 值 |
|----|-----|
| 外设 | 板子 Debug **UART0** |
| 波特率 | **115200** 8N1 |
| 行协议 | 命令以 `\n` 结束（工具自动加） |
| 可选序号 | `@<seq> <cmd>`，ACK：`{cmd_ack} seq=... result=...` |

### 推荐顺序

1. **通路**  
   连接 → 自动 `help` → 控制台有命令列表与 ACK → 顶栏「轮询状态」打开时遥测更新。

2. **单电机**（「电机」Tab，建议架空轮）  
   - 急停，保证底盘不抢电机  
   - 勾选左/右 → `openloop` → 小 duty（800～1500）→ set → stop  
   - 再 `speed` → set 5～8 → stop  

3. **底盘**（「驾驶」Tab）  
   - `openloop` 或 `speed` → 滑条调小 **v** 后**松开**（自动下发）  
   - 或用预设：停 / 慢 / 中 / 左转 / 右转  
   - **WASD** 遥控；**空格** 急停  

4. **循迹 / 任务**（「循迹/任务」Tab）  
   - `track scan` → `status` 看 mask/on/err  
   - `pol` → 调小 `track_app_v` → `start`；方向反了改 `sign=-1`  
   - 跟稳后 `stop`，再 `mission start 1`…`4`（任务 4 可设 laps）  
   - **track 与 mission 互斥**；线速度共用 `track_app_v`  

5. **改参**（「参数」Tab）  
   - 连接后自动从板端 `show` 拉取参数表（右侧列表，可「刷新」）  
   - 点参数行填入名/值；`set` 只改 RAM  
   - 电机：`set motor_kp …` → **应用 motor param**  
   - 底盘：`set chassis_* …` → **应用 chassis param**  
   - 循迹：`set track_app_*`（下一控制周期生效，无需 param 应用）  
   - 持久化：`save`；重载：`load`  

### 控制权分工

| 场景 | 用哪个 |
|------|--------|
| 调单轮、扫 PID | **电机** Tab |
| 整车差速行驶 | **驾驶** Tab |
| 光电循迹 / 赛题任务 | **循迹/任务** Tab |

会抢同一套电机与底盘：进电机操作时若底盘非 idle，上位机会先 `chassis stop`；跑 track/mission 时不要再 motor/chassis set。

### 安全

- 顶栏 **急停 STOP** / 键盘 **空格**：`mission stop` → `track stop` → `chassis stop` → `motor 0x3 stop`
- 断开或关窗：尽力停车
- 开环勿长期堵转；速度从小到大
- 输入框聚焦时禁用 WASD

### 快捷键（非输入状态）

| 键 | 作用 |
|----|------|
| W / S | 前进 / 后退 |
| A / D | 左转 / 右转 |
| 空格 | 急停 |

应用内「帮助」Tab 与上文一致。

---

## 其它脚本

| 路径 | 说明 |
|------|------|
| `analyze_map.py` | map 分析 |
| `tools/motor_*.py` | 扫参 / 阶跃 |
| `tools/test_right_motor_*.py` | 右电机冒烟 |

```powershell
uv run python tools/test_right_motor_serial.py COM8
```
