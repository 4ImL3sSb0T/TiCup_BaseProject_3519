# Scripts / Host UI

Python 工具与可视化串口上位机，通过固件命令系统控制小车。

协议与命令语法见仓库 `docs/serial_commands.md`。

## 环境

```powershell
cd scripts
uv sync
```

Python ≥ 3.12，依赖由 `pyproject.toml` 管理（`pyserial` / `customtkinter` / `matplotlib`）。

## 上位机

```powershell
cd scripts
uv run python -m host
# 指定串口
uv run python -m host --port COM8
# 协议解析自检（无硬件）
uv run python -m host --self-check
```

功能摘要：

- 串口连接（默认 115200）、`@seq` 序号帧与 `{cmd_ack}` 显示
- 底盘：模式 / v·ω 滑条 / 航向 / 急停 / WASD（空格急停）
- 电机：mask 左右 / mode / set / stop / status / param
- 参数：get/set/show/export/save/load + apply PID
- 遥测卡片与速度曲线；可选状态轮询
- 底部自由命令控制台

急停会发送 `chassis stop` 与 `motor 0x3 stop`。断开或关闭窗口时同样尽力停车。

上次使用的 COM 口保存在 `host/host_config.json`。

## 其它脚本

| 路径 | 说明 |
|------|------|
| `analyze_map.py` | map 文件分析 |
| `tools/motor_*.py` | 电机扫参 / 阶跃等串口脚本 |
| `tools/test_right_motor_*.py` | 右电机冒烟测试 |

示例：

```powershell
uv run python tools/test_right_motor_serial.py COM8
```
