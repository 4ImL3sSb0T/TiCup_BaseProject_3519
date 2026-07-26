"""In-app help text (Chinese) for BaseProject host UI."""

HELP_TEXT = """
BaseProject_3519 上位机 — 使用说明
================================

一、连接
--------
1. 板子 Debug 串口：UART0，115200 8N1（TX/RX 交叉）。
2. 点「刷新」选 COM 口 →「连接」。
3. 成功后会自动发 help；控制台应出现命令列表与 ACK。
4. 勾选「轮询」可更新底盘/电机遥测（默认开，不刷 status 日志）。

命令完整语法见仓库：docs/serial_commands.md（track/mission 以固件 track_app / mission 为准）。


二、推荐联调顺序
----------------
1) 通路
   连接 → help / ACK 正常 → 遥测卡片有数据。

2) 单电机（电机 Tab，建议架空轮子）
   · 先急停，保证底盘 idle
   · 勾选左或右 → mode openloop → 目标 800~1500 → set
   · status 看转速 → stop
   · 再 mode speed → 目标 5~8 → set → stop

3) 底盘（驾驶 Tab）
   · 急停确认能停
   · mode openloop 或 speed
   · 滑条调小 v 后松开（自动下发 set），或点预设档
   · WASD 遥控；空格 = 急停

4) 循迹 / 任务（循迹/任务 Tab）
   · track scan → status：确认 mask/on/err
   · pol 0（黑线）→ 小 track_app_v → start
   · 方向反了 set sign=-1；跟稳后 stop
   · 再 mission start 1→2→3→4（4 可设 laps）
   · track 与 mission 互斥；线速度共用 track_app_v

5) 改参（参数 Tab）
   · 连接后自动 `show` 拉取板端参数表（右侧列表，可「刷新」）
   · 左侧「过滤」只筛列表显示；点行填入名/值并 get 刷新
   · set 只改板子 RAM
   · 电机 PID：set 后点「应用 motor param」
   · 底盘 PID：set 后点「应用 chassis param」
   · 循迹：set track_app_*（无需 param 应用，下一周期生效）
   · 掉电保存：save；重载：load


三、控制权分工
--------------
· 调单轮 / 扫 PID → 「电机」Tab
· 整车差速走 → 「驾驶」Tab
· 光电循迹调试 / 赛题任务 → 「循迹/任务」Tab
· 顶栏「主控」显示当前占用：idle / chassis / motor / track / mission
· 会抢同一套电机与底盘：跑 track/mission 时不要再 chassis set / motor set


四、安全
--------
· 顶栏大红「急停 STOP」与空格：
  mission stop → track stop → chassis stop → motor 0x3 stop
· 断开连接 / 关闭窗口：尽力停车
· 开环 duty 勿长期堵转；速度从小到大
· 输入框聚焦时禁用 WASD，避免打字误触


五、快捷键（非输入框聚焦）
--------------------------
  W / S   前进 / 后退
  A / D   左转 / 右转
  空格    急停


六、界面 Tab
------------
  驾驶      底盘模式、v/ω、预设、遥测、曲线
  电机      单/双轮 mode·set·点动
  循迹/任务 track start/stop/scan/pol/cal + mission 1–4
  参数      动态参数表 + get/set/show/save/load
  控制台    原始日志与自由命令
  帮助      本页


七、track / mission 命令速查
----------------------------
  track scan | status | start | stop
  track pol 0|1
  track cal max|min          （仅 GS08）
  set track_app_v / kp / sign / lost_ms …
  mission start <1-4> [laps]
  mission stop | status


八、常见问题
------------
· 无回显：波特率、TX/RX、是否发了换行（本工具自动加 \\n）
· 参数表空：未连接 / 点「刷新」/ 看控制台 show 是否有回显
· imu_not_ready：等 IMU 就绪再进 yaw_rate / heading / mission 航向步
· 改了 kp 无变化：电机/底盘是否点了对应 param 应用；track_app 则直接生效
· 重启参数丢：是否 save 成功
· mission BUSY：先 track stop 或 mission stop
· track 方向反：set track_app_sign -1
""".strip()
