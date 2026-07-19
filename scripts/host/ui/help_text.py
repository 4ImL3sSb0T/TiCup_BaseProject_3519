"""In-app help text (Chinese) for BaseProject host UI."""

HELP_TEXT = """
BaseProject_3519 上位机 — 使用说明
================================

一、连接
--------
1. 板子 Debug 串口：UART0，115200 8N1（TX/RX 交叉）。
2. 点「刷新」选 COM 口 →「连接」。
3. 成功后会自动发 help；控制台应出现命令列表与 ACK。
4. 勾选「轮询状态」可更新遥测（默认开，不刷 status 日志）。

命令完整语法见仓库：docs/serial_commands.md


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

4) 改参（参数 Tab）
   · set 只改板子 RAM
   · 电机 PID：set 后点「应用 motor param」
   · 底盘 PID：set 后点「应用 chassis param」
   · 掉电保存：save；重载：load


三、chassis 与 motor 分工
------------------------
· 调单轮 / 扫 PID → 用「电机」Tab
· 整车差速走 → 用「驾驶」Tab
· 二者会抢同一套电机：
  - 进电机操作时，若底盘非 idle，会先停底盘
  - 跑车时不要再 motor set


四、安全
--------
· 顶栏大红「急停 STOP」与空格：chassis stop + motor 0x3 stop
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
  驾驶    底盘模式、v/ω、预设、遥测、曲线
  电机    单/双轮 mode·set·点动
  参数    get/set/show/save/load + 常用名
  控制台  原始日志与自由命令
  帮助    本页


七、常见问题
------------
· 无回显：波特率、TX/RX、是否发了换行（本工具自动加 \\n）
· imu_not_ready：等 IMU 就绪再进 yaw_rate / heading
· 改了 kp 无变化：是否点了对应 param 应用
· 重启参数丢：是否 save 成功
""".strip()


# Common param names for quick pick
COMMON_PARAMS = [
    "motor_kp",
    "motor_ki",
    "chassis_half_track",
    "chassis_max_v",
    "chassis_max_omega",
    "chassis_ol_v_scale",
    "chassis_ol_w_scale",
    "chassis_omega_to_wheel",
    "chassis_imu_yaw_sign",
    "chassis_yaw_rate_kp",
    "chassis_yaw_rate_ki",
    "chassis_heading_kp",
    "chassis_heading_ki",
    "chassis_heading_kd",
]
