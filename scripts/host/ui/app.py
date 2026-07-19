"""Main CustomTkinter host application."""

from __future__ import annotations

import time
from collections import deque
from typing import Deque, Optional

import customtkinter as ctk
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

from host import commands as cmd
from host.config_store import load_config, save_config
from host.protocol import Ack, ChassisStatus, MotorStatus, strip_log_tag
from host.serial_client import DEFAULT_BAUD, SerialClient, list_serial_ports
from host.ui.widgets import LabeledEntry, MetricCard, StatusDot

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")

CHASSIS_MODES = ("idle", "openloop", "speed", "yaw_rate", "heading")
MOTOR_MODES = ("speed", "openloop", "position")
PLOT_MAX_POINTS = 120


class HostApp(ctk.CTk):
    def __init__(self, default_port: str = "", baud: int = DEFAULT_BAUD) -> None:
        super().__init__()
        self.title("BaseProject_3519 Host")
        self.geometry("1280x820")
        self.minsize(1024, 700)

        self.cfg = load_config()
        if default_port:
            self.cfg["port"] = default_port
        if baud:
            self.cfg["baud"] = baud

        self.client = SerialClient()
        self._keys_down: set[str] = set()
        self._wasd_v = 0.0
        self._wasd_w = 0.0
        self._last_wasd_send = 0.0
        self._poll_job: Optional[str] = None
        self._ui_job: Optional[str] = None

        # Telemetry history for plot
        self._t0 = time.time()
        self._plot_t: Deque[float] = deque(maxlen=PLOT_MAX_POINTS)
        self._plot_wl: Deque[float] = deque(maxlen=PLOT_MAX_POINTS)
        self._plot_wr: Deque[float] = deque(maxlen=PLOT_MAX_POINTS)
        self._plot_v: Deque[float] = deque(maxlen=PLOT_MAX_POINTS)
        self._left_spd = 0.0
        self._right_spd = 0.0
        self._left_tgt = 0.0
        self._right_tgt = 0.0

        self._build_ui()
        self._refresh_ports()
        if self.cfg.get("port"):
            ports = list(self.port_menu.cget("values") or [])
            if self.cfg["port"] in ports:
                self.port_var.set(self.cfg["port"])

        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.bind("<KeyPress>", self._on_key_press)
        self.bind("<KeyRelease>", self._on_key_release)

        self._ui_job = self.after(50, self._poll_queues)
        self._schedule_status_poll()

    # ------------------------------------------------------------------ UI
    def _build_ui(self) -> None:
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)
        self.grid_rowconfigure(2, weight=1)
        self.grid_rowconfigure(3, weight=2)

        self._build_connection_bar()
        self._build_mid_row()
        self._build_lower_row()
        self._build_console()

    def _build_connection_bar(self) -> None:
        bar = ctk.CTkFrame(self)
        bar.grid(row=0, column=0, sticky="ew", padx=10, pady=(10, 6))
        bar.grid_columnconfigure(8, weight=1)

        ctk.CTkLabel(bar, text="串口").grid(row=0, column=0, padx=(10, 4), pady=8)
        self.port_var = ctk.StringVar(value="")
        self.port_menu = ctk.CTkOptionMenu(bar, variable=self.port_var, values=["—"], width=120)
        self.port_menu.grid(row=0, column=1, padx=4, pady=8)

        ctk.CTkButton(bar, text="刷新", width=60, command=self._refresh_ports).grid(
            row=0, column=2, padx=4, pady=8
        )

        ctk.CTkLabel(bar, text="波特率").grid(row=0, column=3, padx=(12, 4), pady=8)
        self.baud_var = ctk.StringVar(value=str(self.cfg.get("baud", DEFAULT_BAUD)))
        ctk.CTkEntry(bar, textvariable=self.baud_var, width=80).grid(
            row=0, column=4, padx=4, pady=8
        )

        self.connect_btn = ctk.CTkButton(bar, text="连接", width=80, command=self._toggle_connect)
        self.connect_btn.grid(row=0, column=5, padx=8, pady=8)

        self.status_dot = StatusDot(bar)
        self.status_dot.grid(row=0, column=6, padx=4, pady=8)
        self.status_lbl = ctk.CTkLabel(bar, text="未连接")
        self.status_lbl.grid(row=0, column=7, padx=4, pady=8, sticky="w")

        self.estop_btn = ctk.CTkButton(
            bar,
            text="急停 STOP",
            width=120,
            height=36,
            fg_color="#c0392b",
            hover_color="#e74c3c",
            font=ctk.CTkFont(size=14, weight="bold"),
            command=self._emergency_stop,
        )
        self.estop_btn.grid(row=0, column=9, padx=10, pady=8)

        self.poll_var = ctk.BooleanVar(value=bool(self.cfg.get("poll_enabled", True)))
        ctk.CTkCheckBox(bar, text="轮询状态", variable=self.poll_var, command=self._on_poll_toggle).grid(
            row=0, column=8, padx=8, pady=8, sticky="e"
        )

    def _build_mid_row(self) -> None:
        mid = ctk.CTkFrame(self, fg_color="transparent")
        mid.grid(row=1, column=0, sticky="nsew", padx=10, pady=4)
        mid.grid_columnconfigure(0, weight=1)
        mid.grid_columnconfigure(1, weight=2)
        mid.grid_rowconfigure(0, weight=1)

        # ---- Chassis control ----
        left = ctk.CTkFrame(mid)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 6))
        ctk.CTkLabel(left, text="底盘控制", font=ctk.CTkFont(size=15, weight="bold")).pack(
            anchor="w", padx=10, pady=(10, 4)
        )

        mode_row = ctk.CTkFrame(left, fg_color="transparent")
        mode_row.pack(fill="x", padx=8, pady=4)
        ctk.CTkLabel(mode_row, text="模式").pack(side="left", padx=4)
        self.chassis_mode_var = ctk.StringVar(value="speed")
        ctk.CTkOptionMenu(
            mode_row, variable=self.chassis_mode_var, values=list(CHASSIS_MODES), width=120
        ).pack(side="left", padx=4)
        ctk.CTkButton(mode_row, text="应用模式", width=90, command=self._apply_chassis_mode).pack(
            side="left", padx=4
        )

        # sliders
        self.v_slider = ctk.CTkSlider(left, from_=-18, to=18, number_of_steps=360, command=self._on_vw_slide)
        self.v_slider.set(0)
        self.v_slider.pack(fill="x", padx=12, pady=(8, 2))
        self.v_lbl = ctk.CTkLabel(left, text="v = 0.00")
        self.v_lbl.pack(anchor="w", padx=12)

        self.w_slider = ctk.CTkSlider(left, from_=-12, to=12, number_of_steps=240, command=self._on_vw_slide)
        self.w_slider.set(0)
        self.w_slider.pack(fill="x", padx=12, pady=(8, 2))
        self.w_lbl = ctk.CTkLabel(left, text="ω = 0.00")
        self.w_lbl.pack(anchor="w", padx=12)

        btn_row = ctk.CTkFrame(left, fg_color="transparent")
        btn_row.pack(fill="x", padx=8, pady=8)
        ctk.CTkButton(btn_row, text="发送 set", command=self._send_chassis_set).pack(side="left", padx=4)
        ctk.CTkButton(btn_row, text="v/ω 清零", command=self._zero_vw).pack(side="left", padx=4)
        ctk.CTkButton(btn_row, text="status", command=lambda: self._send(cmd.chassis_status())).pack(
            side="left", padx=4
        )
        ctk.CTkButton(
            btn_row, text="底盘停", fg_color="#922b21", hover_color="#c0392b", command=self._chassis_stop
        ).pack(side="left", padx=4)

        hdg_row = ctk.CTkFrame(left, fg_color="transparent")
        hdg_row.pack(fill="x", padx=8, pady=4)
        self.hdg_entry = LabeledEntry(hdg_row, "航向°", width=70, default="0")
        self.hdg_entry.pack(side="left", padx=4)
        ctk.CTkButton(hdg_row, text="设航向", width=70, command=self._set_heading).pack(side="left", padx=4)

        ctk.CTkLabel(
            left,
            text="键盘: W/S 前进后退  A/D 转向  空格急停\n（输入框聚焦时禁用键盘控车）",
            justify="left",
            text_color="gray",
        ).pack(anchor="w", padx=12, pady=(4, 10))

        # ---- Telemetry + plot ----
        right = ctk.CTkFrame(mid)
        right.grid(row=0, column=1, sticky="nsew", padx=(6, 0))
        right.grid_columnconfigure(0, weight=1)
        right.grid_rowconfigure(1, weight=1)

        ctk.CTkLabel(right, text="遥测", font=ctk.CTkFont(size=15, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=10, pady=(10, 4)
        )

        cards = ctk.CTkFrame(right, fg_color="transparent")
        cards.grid(row=0, column=0, sticky="ew", padx=6, pady=(36, 4))
        for i in range(5):
            cards.grid_columnconfigure(i, weight=1)

        self.card_mode = MetricCard(cards, "模式")
        self.card_vw = MetricCard(cards, "v / ω")
        self.card_yaw = MetricCard(cards, "yaw / gz")
        self.card_wheel = MetricCard(cards, "wl / wr")
        self.card_imu = MetricCard(cards, "IMU")
        self.card_mode.grid(row=0, column=0, sticky="ew", padx=3)
        self.card_vw.grid(row=0, column=1, sticky="ew", padx=3)
        self.card_yaw.grid(row=0, column=2, sticky="ew", padx=3)
        self.card_wheel.grid(row=0, column=3, sticky="ew", padx=3)
        self.card_imu.grid(row=0, column=4, sticky="ew", padx=3)

        plot_frame = ctk.CTkFrame(right)
        plot_frame.grid(row=1, column=0, sticky="nsew", padx=8, pady=8)
        self._fig = Figure(figsize=(5, 2.4), dpi=100, facecolor="#1a1a1a")
        self._ax = self._fig.add_subplot(111)
        self._ax.set_facecolor("#1a1a1a")
        self._ax.tick_params(colors="#bbbbbb", labelsize=8)
        for spine in self._ax.spines.values():
            spine.set_color("#555555")
        self._ax.set_ylabel("speed", color="#bbbbbb", fontsize=8)
        self._ax.set_xlabel("t (s)", color="#bbbbbb", fontsize=8)
        (self._line_v,) = self._ax.plot([], [], label="v tgt", color="#3498db", lw=1.5)
        (self._line_wl,) = self._ax.plot([], [], label="wl", color="#2ecc71", lw=1.2)
        (self._line_wr,) = self._ax.plot([], [], label="wr", color="#e67e22", lw=1.2)
        self._ax.legend(loc="upper right", fontsize=7, facecolor="#2b2b2b", labelcolor="#dddddd")
        self._fig.tight_layout()
        self._canvas = FigureCanvasTkAgg(self._fig, master=plot_frame)
        self._canvas.get_tk_widget().pack(fill="both", expand=True)

    def _build_lower_row(self) -> None:
        lower = ctk.CTkFrame(self, fg_color="transparent")
        lower.grid(row=2, column=0, sticky="nsew", padx=10, pady=4)
        lower.grid_columnconfigure(0, weight=1)
        lower.grid_columnconfigure(1, weight=1)
        lower.grid_rowconfigure(0, weight=1)

        # Motor
        motor = ctk.CTkFrame(lower)
        motor.grid(row=0, column=0, sticky="nsew", padx=(0, 6))
        ctk.CTkLabel(motor, text="电机", font=ctk.CTkFont(size=15, weight="bold")).pack(
            anchor="w", padx=10, pady=(10, 4)
        )

        mask_row = ctk.CTkFrame(motor, fg_color="transparent")
        mask_row.pack(fill="x", padx=8, pady=4)
        self.mask_l = ctk.BooleanVar(value=True)
        self.mask_r = ctk.BooleanVar(value=True)
        ctk.CTkCheckBox(mask_row, text="左 L (0x1)", variable=self.mask_l).pack(side="left", padx=6)
        ctk.CTkCheckBox(mask_row, text="右 R (0x2)", variable=self.mask_r).pack(side="left", padx=6)

        mode_row = ctk.CTkFrame(motor, fg_color="transparent")
        mode_row.pack(fill="x", padx=8, pady=4)
        ctk.CTkLabel(mode_row, text="模式").pack(side="left", padx=4)
        self.motor_mode_var = ctk.StringVar(value="speed")
        ctk.CTkOptionMenu(mode_row, variable=self.motor_mode_var, values=list(MOTOR_MODES), width=110).pack(
            side="left", padx=4
        )
        ctk.CTkButton(mode_row, text="应用", width=70, command=self._apply_motor_mode).pack(
            side="left", padx=4
        )

        set_row = ctk.CTkFrame(motor, fg_color="transparent")
        set_row.pack(fill="x", padx=8, pady=4)
        self.motor_val = LabeledEntry(set_row, "目标", width=90, default="5")
        self.motor_val.pack(side="left", padx=4)
        ctk.CTkButton(set_row, text="set", width=60, command=self._motor_set).pack(side="left", padx=4)
        ctk.CTkButton(set_row, text="stop", width=60, command=self._motor_stop).pack(side="left", padx=4)
        ctk.CTkButton(set_row, text="status", width=70, command=self._motor_status).pack(side="left", padx=4)
        ctk.CTkButton(set_row, text="param", width=70, command=self._motor_param).pack(side="left", padx=4)

        self.motor_info = ctk.CTkLabel(
            motor,
            text="L: —\nR: —",
            justify="left",
            font=ctk.CTkFont(family="Consolas", size=12),
        )
        self.motor_info.pack(anchor="w", padx=12, pady=(6, 10))

        # Param
        param = ctk.CTkFrame(lower)
        param.grid(row=0, column=1, sticky="nsew", padx=(6, 0))
        ctk.CTkLabel(param, text="参数", font=ctk.CTkFont(size=15, weight="bold")).pack(
            anchor="w", padx=10, pady=(10, 4)
        )

        row1 = ctk.CTkFrame(param, fg_color="transparent")
        row1.pack(fill="x", padx=8, pady=4)
        self.param_name = LabeledEntry(row1, "名", width=140, default="motor_kp")
        self.param_name.pack(side="left", padx=4)
        self.param_value = LabeledEntry(row1, "值", width=90, default="120")
        self.param_value.pack(side="left", padx=4)
        ctk.CTkButton(row1, text="get", width=50, command=self._param_get).pack(side="left", padx=2)
        ctk.CTkButton(row1, text="set", width=50, command=self._param_set).pack(side="left", padx=2)

        row2 = ctk.CTkFrame(param, fg_color="transparent")
        row2.pack(fill="x", padx=8, pady=4)
        self.param_prefix = LabeledEntry(row2, "前缀", width=100, default="motor")
        self.param_prefix.pack(side="left", padx=4)
        ctk.CTkButton(row2, text="show", width=60, command=self._param_show).pack(side="left", padx=2)
        ctk.CTkButton(row2, text="export", width=70, command=lambda: self._send(cmd.param_export())).pack(
            side="left", padx=2
        )
        ctk.CTkButton(row2, text="save", width=60, command=lambda: self._send(cmd.param_save())).pack(
            side="left", padx=2
        )
        ctk.CTkButton(row2, text="load", width=60, command=lambda: self._send(cmd.param_load())).pack(
            side="left", padx=2
        )

        row3 = ctk.CTkFrame(param, fg_color="transparent")
        row3.pack(fill="x", padx=8, pady=8)
        ctk.CTkButton(row3, text="应用 motor param", command=self._motor_param).pack(side="left", padx=4)
        ctk.CTkButton(row3, text="应用 chassis param", command=lambda: self._send(cmd.chassis_param())).pack(
            side="left", padx=4
        )
        ctk.CTkLabel(
            param,
            text="提示: set 只改 RAM；PID 需再点 apply；持久化用 save",
            text_color="gray",
        ).pack(anchor="w", padx=12, pady=(0, 10))

    def _build_console(self) -> None:
        cons = ctk.CTkFrame(self)
        cons.grid(row=3, column=0, sticky="nsew", padx=10, pady=(4, 10))
        cons.grid_columnconfigure(0, weight=1)
        cons.grid_rowconfigure(1, weight=1)

        head = ctk.CTkFrame(cons, fg_color="transparent")
        head.grid(row=0, column=0, sticky="ew", padx=8, pady=(8, 2))
        ctk.CTkLabel(head, text="控制台", font=ctk.CTkFont(size=15, weight="bold")).pack(side="left")
        self.filter_ack = ctk.BooleanVar(value=False)
        ctk.CTkCheckBox(head, text="仅 ACK/发送", variable=self.filter_ack).pack(side="left", padx=12)
        ctk.CTkButton(head, text="清空", width=60, command=self._clear_log).pack(side="right", padx=4)
        ctk.CTkButton(head, text="help", width=60, command=lambda: self._send(cmd.help_cmd())).pack(
            side="right", padx=4
        )

        self.log_box = ctk.CTkTextbox(cons, font=ctk.CTkFont(family="Consolas", size=12))
        self.log_box.grid(row=1, column=0, sticky="nsew", padx=8, pady=4)
        self.log_box.configure(state="disabled")

        send_row = ctk.CTkFrame(cons, fg_color="transparent")
        send_row.grid(row=2, column=0, sticky="ew", padx=8, pady=(2, 8))
        send_row.grid_columnconfigure(0, weight=1)
        self.cmd_entry = ctk.CTkEntry(send_row, placeholder_text="自由命令，例如: chassis status")
        self.cmd_entry.grid(row=0, column=0, sticky="ew", padx=(0, 6))
        self.cmd_entry.bind("<Return>", lambda _e: self._send_free_cmd())
        self.use_seq = ctk.BooleanVar(value=True)
        ctk.CTkCheckBox(send_row, text="@seq", variable=self.use_seq, width=60).grid(
            row=0, column=1, padx=4
        )
        ctk.CTkButton(send_row, text="发送", width=80, command=self._send_free_cmd).grid(
            row=0, column=2, padx=4
        )

    # ----------------------------------------------------------- connection
    def _refresh_ports(self) -> None:
        ports = list_serial_ports()
        if not ports:
            ports = ["—"]
        self.port_menu.configure(values=ports)
        cur = self.port_var.get()
        if cur not in ports:
            self.port_var.set(ports[0])

    def _toggle_connect(self) -> None:
        if self.client.is_open:
            self._disconnect()
        else:
            self._connect()

    def _connect(self) -> None:
        port = self.port_var.get().strip()
        if not port or port == "—":
            self._log_ui("请选择有效串口", "error")
            return
        try:
            baud = int(self.baud_var.get().strip())
        except ValueError:
            self._log_ui("波特率无效", "error")
            return
        try:
            self.client.open(port, baud)
        except Exception as e:
            self.status_dot.set_state(False, error=True)
            self.status_lbl.configure(text=f"打开失败: {e}")
            self._log_ui(f"打开 {port} 失败: {e}", "error")
            return

        self.cfg["port"] = port
        self.cfg["baud"] = baud
        save_config(self.cfg)

        self.connect_btn.configure(text="断开")
        self.status_dot.set_state(True)
        self.status_lbl.configure(text=f"已连接 {port} @ {baud}")
        self._log_ui(f"已连接 {port} @ {baud}", "tx")
        # probe
        try:
            self._send(cmd.help_cmd())
        except Exception:
            pass

    def _disconnect(self, send_stop: bool = True) -> None:
        if send_stop and self.client.is_open:
            try:
                for c in cmd.emergency_stop_cmds():
                    self.client.send_raw(c, with_seq=False)
            except Exception:
                pass
            time.sleep(0.05)
        self.client.close()
        self.connect_btn.configure(text="连接")
        self.status_dot.set_state(False)
        self.status_lbl.configure(text="未连接")
        self._log_ui("已断开", "tx")

    def _on_close(self) -> None:
        self.cfg["poll_enabled"] = bool(self.poll_var.get())
        save_config(self.cfg)
        if self._poll_job is not None:
            try:
                self.after_cancel(self._poll_job)
            except Exception:
                pass
        if self._ui_job is not None:
            try:
                self.after_cancel(self._ui_job)
            except Exception:
                pass
        self._disconnect(send_stop=True)
        self.destroy()

    # -------------------------------------------------------------- send API
    def _send(self, command: str, force_seq: Optional[bool] = None) -> None:
        if not self.client.is_open:
            self._log_ui("未连接，无法发送", "error")
            return
        use_seq = self.use_seq.get() if force_seq is None else force_seq
        try:
            seq = self.client.send_raw(command, with_seq=use_seq)
        except Exception as e:
            self._log_ui(f"发送失败: {e}", "error")
            return
        if seq >= 0:
            self._log_ui(f">>> @{seq} {command}", "tx")
        else:
            self._log_ui(f">>> {command}", "tx")

    def _send_free_cmd(self) -> None:
        text = self.cmd_entry.get().strip()
        if not text:
            return
        # allow user to type @n already
        if text.startswith("@"):
            if not self.client.is_open:
                self._log_ui("未连接，无法发送", "error")
                return
            try:
                self.client.send_raw(text, with_seq=False)
                self._log_ui(f">>> {text}", "tx")
            except Exception as e:
                self._log_ui(f"发送失败: {e}", "error")
        else:
            self._send(text)
        self.cmd_entry.delete(0, "end")

    def _emergency_stop(self) -> None:
        self._keys_down.clear()
        self._wasd_v = 0.0
        self._wasd_w = 0.0
        self.v_slider.set(0)
        self.w_slider.set(0)
        self._on_vw_slide(0)
        if not self.client.is_open:
            self._log_ui("急停：未连接", "error")
            return
        for c in cmd.emergency_stop_cmds():
            try:
                # estop without waiting; no seq for speed
                self.client.send_raw(c, with_seq=False)
                self._log_ui(f">>> {c}  [ESTOP]", "tx")
            except Exception as e:
                self._log_ui(f"急停发送失败: {e}", "error")

    # ----------------------------------------------------------- chassis UI
    def _on_vw_slide(self, _value=None) -> None:
        v = float(self.v_slider.get())
        w = float(self.w_slider.get())
        self.v_lbl.configure(text=f"v = {v:.2f}")
        self.w_lbl.configure(text=f"ω = {w:.2f}")

    def _zero_vw(self) -> None:
        self.v_slider.set(0)
        self.w_slider.set(0)
        self._on_vw_slide(0)
        if self.client.is_open:
            self._send(cmd.chassis_set(0, 0))

    def _send_chassis_set(self) -> None:
        v = float(self.v_slider.get())
        w = float(self.w_slider.get())
        self._send(cmd.chassis_set(v, w))

    def _apply_chassis_mode(self) -> None:
        self._send(cmd.chassis_mode(self.chassis_mode_var.get()))

    def _chassis_stop(self) -> None:
        self.v_slider.set(0)
        self.w_slider.set(0)
        self._on_vw_slide(0)
        self._send(cmd.chassis_stop())

    def _set_heading(self) -> None:
        try:
            deg = float(self.hdg_entry.get())
        except ValueError:
            self._log_ui("航向角度无效", "error")
            return
        self._send(cmd.chassis_heading(deg))

    # ------------------------------------------------------------- motor UI
    def _motor_mask(self) -> int:
        m = 0
        if self.mask_l.get():
            m |= 0x1
        if self.mask_r.get():
            m |= 0x2
        return m

    def _apply_motor_mode(self) -> None:
        mask = self._motor_mask()
        if mask == 0:
            self._log_ui("请至少选择一个电机", "error")
            return
        self._send(cmd.motor_mode(mask, self.motor_mode_var.get()))

    def _motor_set(self) -> None:
        mask = self._motor_mask()
        if mask == 0:
            self._log_ui("请至少选择一个电机", "error")
            return
        try:
            val = float(self.motor_val.get())
        except ValueError:
            self._log_ui("电机目标值无效", "error")
            return
        self._send(cmd.motor_set(mask, val))

    def _motor_stop(self) -> None:
        mask = self._motor_mask() or 0x3
        self._send(cmd.motor_stop(mask))

    def _motor_status(self) -> None:
        mask = self._motor_mask() or 0x3
        self._send(cmd.motor_status(mask))

    def _motor_param(self) -> None:
        mask = self._motor_mask() or 0x3
        self._send(cmd.motor_param(mask))

    # ------------------------------------------------------------- param UI
    def _param_get(self) -> None:
        name = self.param_name.get()
        if not name:
            return
        self._send(cmd.param_get(name))

    def _param_set(self) -> None:
        name = self.param_name.get()
        value = self.param_value.get()
        if not name or not value:
            self._log_ui("参数名/值不能为空", "error")
            return
        self._send(cmd.param_set(name, value))

    def _param_show(self) -> None:
        self._send(cmd.param_show(self.param_prefix.get()))

    # ---------------------------------------------------------------- WASD
    def _focus_is_entry(self) -> bool:
        w = self.focus_get()
        if w is None:
            return False
        cls = w.winfo_class()
        # CTk Entry / Text use various class names
        name = str(type(w))
        return "Entry" in cls or "Text" in cls or "Entry" in name or "Text" in name

    def _on_key_press(self, event) -> None:
        if self._focus_is_entry():
            return
        key = (event.keysym or "").lower()
        if key == "space":
            self._emergency_stop()
            return
        if key in ("w", "a", "s", "d"):
            self._keys_down.add(key)
            self._update_wasd()

    def _on_key_release(self, event) -> None:
        if self._focus_is_entry():
            return
        key = (event.keysym or "").lower()
        if key in self._keys_down:
            self._keys_down.discard(key)
            self._update_wasd()

    def _update_wasd(self) -> None:
        v = 0.0
        w = 0.0
        # moderate keyboard defaults
        if "w" in self._keys_down:
            v += 6.0
        if "s" in self._keys_down:
            v -= 6.0
        if "a" in self._keys_down:
            w += 4.0
        if "d" in self._keys_down:
            w -= 4.0
        self._wasd_v = v
        self._wasd_w = w
        self.v_slider.set(v)
        self.w_slider.set(w)
        self._on_vw_slide(0)
        now = time.time()
        if now - self._last_wasd_send < 0.08:
            return
        self._last_wasd_send = now
        if self.client.is_open:
            try:
                # no seq / no log spam for teleop
                self.client.send_raw(cmd.chassis_set(v, w), with_seq=False)
            except Exception as e:
                self._log_ui(f"WASD 发送失败: {e}", "error")

    # ----------------------------------------------------------- poll / log
    def _on_poll_toggle(self) -> None:
        self.cfg["poll_enabled"] = bool(self.poll_var.get())
        save_config(self.cfg)

    def _schedule_status_poll(self) -> None:
        self._do_status_poll()
        ms = int(self.cfg.get("poll_ms", 400))
        self._poll_job = self.after(ms, self._schedule_status_poll)

    def _do_status_poll(self) -> None:
        if not self.poll_var.get() or not self.client.is_open:
            return
        try:
            # silent-ish: use seq but don't flood log for poll — log only TX optionally filtered
            self.client.send_raw(cmd.chassis_status(), with_seq=True)
            self.client.send_raw(cmd.motor_status(0x3), with_seq=True)
        except Exception:
            pass

    def _clear_log(self) -> None:
        self.log_box.configure(state="normal")
        self.log_box.delete("1.0", "end")
        self.log_box.configure(state="disabled")

    def _log_ui(self, text: str, kind: str = "rx") -> None:
        if self.filter_ack.get() and kind == "rx":
            # only show ack lines when filter is on
            if "{cmd_ack}" not in text and not text.startswith(">>>"):
                return
        colors = {
            "tx": "#5dade2",
            "ack_ok": "#58d68d",
            "ack_err": "#f1948a",
            "error": "#e74c3c",
            "rx": "#d5d8dc",
        }
        color = colors.get(kind, colors["rx"])
        self.log_box.configure(state="normal")
        self.log_box.insert("end", text + "\n")
        # tag last line color approximately by inserting with tags is complex in CTkTextbox;
        # keep simple mono log — color via prefix
        self.log_box.see("end")
        self.log_box.configure(state="disabled")
        # silence unused
        _ = color

    def _poll_queues(self) -> None:
        # lines
        n = 0
        while n < 80:
            try:
                line = self.client.line_queue.get_nowait()
            except Exception:
                break
            n += 1
            self._handle_rx_line(line)

        while True:
            try:
                ack = self.client.ack_queue.get_nowait()
            except Exception:
                break
            self._handle_ack(ack)

        while True:
            try:
                ch = self.client.chassis_queue.get_nowait()
            except Exception:
                break
            self._handle_chassis(ch)

        while True:
            try:
                m = self.client.motor_queue.get_nowait()
            except Exception:
                break
            self._handle_motor(m)

        while True:
            try:
                err = self.client.error_queue.get_nowait()
            except Exception:
                break
            self.status_dot.set_state(False, error=True)
            self.status_lbl.configure(text=err)
            self._log_ui(err, "error")

        self._ui_job = self.after(50, self._poll_queues)

    def _handle_rx_line(self, line: str) -> None:
        # skip pure motor plot spam if present
        if "{motor_l}" in line or line.startswith("{motor_l}"):
            return
        # status polling is noisy; still parse via queues, just hide from log
        if self.poll_var.get() and (
            "chassis:" in line
            or "Left:" in line
            or "Right:" in line
            or "status_printed" in line
        ):
            return
        tag, body = strip_log_tag(line)
        display = line if not tag else f"{{{tag}}}{body}"
        if self.filter_ack.get():
            if "{cmd_ack}" not in line:
                return
        self._log_ui(display, "rx")

    def _handle_ack(self, ack: Ack) -> None:
        # hide routine poll ACKs when polling is on
        if self.poll_var.get() and ack.ok and ack.ctx in ("status_printed", ""):
            # still show non-status ACKs; empty ctx appears often — only skip status_printed
            if ack.ctx == "status_printed":
                return
        kind = "ack_ok" if ack.ok else "ack_err"
        extra = f" ctx={ack.ctx}" if ack.ctx else ""
        self._log_ui(f"ACK seq={ack.seq} {ack.result}{extra}", kind)
        if not ack.ok:
            self.status_lbl.configure(text=f"ACK {ack.result}")

    def _handle_chassis(self, ch: ChassisStatus) -> None:
        self.card_mode.set_value(ch.mode)
        self.card_vw.set_value(f"{ch.v:.2f} / {ch.w:.2f}")
        self.card_yaw.set_value(f"{ch.yaw:.1f} / {ch.gz:.1f}")
        self.card_wheel.set_value(f"{ch.wl:.2f} / {ch.wr:.2f}")
        self.card_imu.set_value("ready" if ch.imu else "no")
        t = time.time() - self._t0
        self._plot_t.append(t)
        self._plot_v.append(ch.v)
        self._plot_wl.append(ch.wl)
        self._plot_wr.append(ch.wr)
        self._redraw_plot()

    def _handle_motor(self, m: MotorStatus) -> None:
        if m.name == "Left":
            self._left_spd = m.spd
            self._left_tgt = m.tgt_spd
        else:
            self._right_spd = m.spd
            self._right_tgt = m.tgt_spd
        self.motor_info.configure(
            text=(
                f"L: spd={self._left_spd:+.2f} tgt={self._left_tgt:+.2f}\n"
                f"R: spd={self._right_spd:+.2f} tgt={self._right_tgt:+.2f}"
            )
        )

    def _redraw_plot(self) -> None:
        if not self._plot_t:
            return
        xs = list(self._plot_t)
        self._line_v.set_data(xs, list(self._plot_v))
        self._line_wl.set_data(xs, list(self._plot_wl))
        self._line_wr.set_data(xs, list(self._plot_wr))
        self._ax.relim()
        self._ax.autoscale_view()
        self._canvas.draw_idle()
