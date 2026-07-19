"""Main CustomTkinter host application (v0.2 tabbed UI)."""

from __future__ import annotations

import time
from collections import deque
from typing import Deque, Dict, Optional

import customtkinter as ctk
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

from host import commands as cmd
from host.config_store import load_config, save_config
from host.protocol import (
    Ack,
    ChassisStatus,
    MotorStatus,
    ParamEntry,
    parse_ctx_fields,
    parse_export_end,
    parse_export_kv,
    parse_get_terminal,
    parse_param_ack_ctx,
    parse_set_terminal,
    parse_show_end,
    parse_show_param_row,
    strip_log_tag,
)
from host.serial_client import DEFAULT_BAUD, SerialClient, list_serial_ports
from host.ui.help_text import HELP_TEXT
from host.ui.widgets import LabeledEntry, MetricCard, StatusDot

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")

CHASSIS_MODES = ("idle", "openloop", "speed", "yaw_rate", "heading")
MOTOR_MODES = ("speed", "openloop", "position")
PLOT_MAX_POINTS = 120
LOG_MAX_LINES = 2000
WASD_SEND_MIN_DT = 0.08
PARAM_SYNC_TIMEOUT_MS = 3500


class HostApp(ctk.CTk):
    def __init__(self, default_port: str = "", baud: int = DEFAULT_BAUD) -> None:
        super().__init__()
        self.title("BaseProject_3519 Host")
        self.geometry("1200x760")
        self.minsize(960, 640)

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
        self._last_live_send = 0.0
        self._poll_job: Optional[str] = None
        self._ui_job: Optional[str] = None
        self._jog_job: Optional[str] = None
        self._typing = False
        self._control_owner = "idle"  # idle | chassis | motor
        self._chassis_mode = "idle"
        self._vw_dirty = False  # slider changed but not sent
        self._vw_sent_v = 0.0
        self._vw_sent_w = 0.0

        self._t0 = time.time()
        self._plot_t: Deque[float] = deque(maxlen=PLOT_MAX_POINTS)
        self._plot_v: Deque[float] = deque(maxlen=PLOT_MAX_POINTS)
        self._plot_wl: Deque[float] = deque(maxlen=PLOT_MAX_POINTS)
        self._plot_wr: Deque[float] = deque(maxlen=PLOT_MAX_POINTS)
        self._plot_l_spd: Deque[float] = deque(maxlen=PLOT_MAX_POINTS)
        self._plot_r_spd: Deque[float] = deque(maxlen=PLOT_MAX_POINTS)
        self._left_spd = 0.0
        self._right_spd = 0.0
        self._left_tgt = 0.0
        self._right_tgt = 0.0
        self._left_tgt_pwm = 0.0
        self._right_tgt_pwm = 0.0
        self._left_pos = 0.0
        self._right_pos = 0.0
        self._left_mode = "—"
        self._right_mode = "—"
        # Allow a few status lines through the log after manual status requests.
        self._status_log_budget = 0

        # Dynamic param table from board `show` / `export`.
        self._params: Dict[str, ParamEntry] = {}
        self._param_sync_active = False
        self._param_sync_buf: Dict[str, ParamEntry] = {}
        self._param_sync_mode = ""  # "show" | "export"
        self._param_sync_job: Optional[str] = None
        self._param_sync_seq = -1

        self._build_ui()
        self._bind_entry_focus()
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
        self._update_owner_label()
        self._update_vw_status()

    # ================================================================== UI
    def _build_ui(self) -> None:
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        self._build_connection_bar()

        self.tabs = ctk.CTkTabview(self)
        self.tabs.grid(row=1, column=0, sticky="nsew", padx=10, pady=(0, 10))
        self.tab_drive = self.tabs.add("驾驶")
        self.tab_motor = self.tabs.add("电机")
        self.tab_param = self.tabs.add("参数")
        self.tab_console = self.tabs.add("控制台")
        self.tab_help = self.tabs.add("帮助")

        self._build_drive_tab()
        self._build_motor_tab()
        self._build_param_tab()
        self._build_console_tab()
        self._build_help_tab()

    def _build_connection_bar(self) -> None:
        bar = ctk.CTkFrame(self)
        bar.grid(row=0, column=0, sticky="ew", padx=10, pady=(10, 6))
        bar.grid_columnconfigure(8, weight=1)

        ctk.CTkLabel(bar, text="串口").grid(row=0, column=0, padx=(10, 4), pady=8)
        self.port_var = ctk.StringVar(value="")
        self.port_menu = ctk.CTkOptionMenu(bar, variable=self.port_var, values=["—"], width=110)
        self.port_menu.grid(row=0, column=1, padx=4, pady=8)
        ctk.CTkButton(bar, text="刷新", width=56, command=self._refresh_ports).grid(
            row=0, column=2, padx=4, pady=8
        )

        ctk.CTkLabel(bar, text="波特率").grid(row=0, column=3, padx=(10, 4), pady=8)
        self.baud_var = ctk.StringVar(value=str(self.cfg.get("baud", DEFAULT_BAUD)))
        self.baud_entry = ctk.CTkEntry(bar, textvariable=self.baud_var, width=72)
        self.baud_entry.grid(row=0, column=4, padx=4, pady=8)

        self.connect_btn = ctk.CTkButton(bar, text="连接", width=72, command=self._toggle_connect)
        self.connect_btn.grid(row=0, column=5, padx=6, pady=8)

        self.status_dot = StatusDot(bar)
        self.status_dot.grid(row=0, column=6, padx=2, pady=8)
        self.status_lbl = ctk.CTkLabel(bar, text="未连接", width=160, anchor="w")
        self.status_lbl.grid(row=0, column=7, padx=2, pady=8, sticky="w")

        self.owner_lbl = ctk.CTkLabel(bar, text="主控: idle", text_color="#f0ad4e")
        self.owner_lbl.grid(row=0, column=8, padx=8, pady=8, sticky="e")

        self.poll_var = ctk.BooleanVar(value=bool(self.cfg.get("poll_enabled", True)))
        ctk.CTkCheckBox(
            bar, text="轮询", variable=self.poll_var, command=self._on_poll_toggle, width=70
        ).grid(row=0, column=9, padx=4, pady=8)

        self.estop_btn = ctk.CTkButton(
            bar,
            text="急停 STOP",
            width=110,
            height=34,
            fg_color="#c0392b",
            hover_color="#e74c3c",
            font=ctk.CTkFont(size=14, weight="bold"),
            command=self._emergency_stop,
        )
        self.estop_btn.grid(row=0, column=10, padx=10, pady=8)

    # -------------------------------------------------------------- 驾驶
    def _build_drive_tab(self) -> None:
        tab = self.tab_drive
        tab.grid_columnconfigure(0, weight=1)
        tab.grid_columnconfigure(1, weight=2)
        tab.grid_rowconfigure(0, weight=1)

        left = ctk.CTkFrame(tab)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 6), pady=4)

        ctk.CTkLabel(left, text="底盘", font=ctk.CTkFont(size=15, weight="bold")).pack(
            anchor="w", padx=10, pady=(10, 4)
        )

        mode_row = ctk.CTkFrame(left, fg_color="transparent")
        mode_row.pack(fill="x", padx=8, pady=4)
        ctk.CTkLabel(mode_row, text="模式").pack(side="left", padx=4)
        self.chassis_mode_var = ctk.StringVar(value="speed")
        ctk.CTkOptionMenu(
            mode_row, variable=self.chassis_mode_var, values=list(CHASSIS_MODES), width=120
        ).pack(side="left", padx=4)
        ctk.CTkButton(mode_row, text="应用", width=70, command=self._apply_chassis_mode).pack(
            side="left", padx=4
        )

        self.v_slider = ctk.CTkSlider(
            left, from_=-18, to=18, number_of_steps=360, command=self._on_vw_slide
        )
        self.v_slider.set(0)
        self.v_slider.pack(fill="x", padx=12, pady=(10, 2))
        self.v_lbl = ctk.CTkLabel(left, text="v = 0.00")
        self.v_lbl.pack(anchor="w", padx=12)
        self.v_slider.bind("<ButtonRelease-1>", self._on_slider_release)

        self.w_slider = ctk.CTkSlider(
            left, from_=-12, to=12, number_of_steps=240, command=self._on_vw_slide
        )
        self.w_slider.set(0)
        self.w_slider.pack(fill="x", padx=12, pady=(8, 2))
        self.w_lbl = ctk.CTkLabel(left, text="ω = 0.00")
        self.w_lbl.pack(anchor="w", padx=12)
        self.w_slider.bind("<ButtonRelease-1>", self._on_slider_release)

        self.vw_status_lbl = ctk.CTkLabel(left, text="已下发 v=0 ω=0", text_color="gray")
        self.vw_status_lbl.pack(anchor="w", padx=12, pady=(2, 4))

        self.live_send_var = ctk.BooleanVar(value=False)
        ctk.CTkCheckBox(
            left, text="拖动时连续发送 (~10Hz)", variable=self.live_send_var
        ).pack(anchor="w", padx=12, pady=2)

        btn_row = ctk.CTkFrame(left, fg_color="transparent")
        btn_row.pack(fill="x", padx=8, pady=6)
        ctk.CTkButton(btn_row, text="发送 set", width=80, command=self._send_chassis_set).pack(
            side="left", padx=3
        )
        ctk.CTkButton(btn_row, text="清零", width=60, command=self._zero_vw).pack(side="left", padx=3)
        ctk.CTkButton(
            btn_row, text="status", width=70, command=lambda: self._send(cmd.chassis_status())
        ).pack(side="left", padx=3)
        ctk.CTkButton(
            btn_row,
            text="底盘停",
            width=70,
            fg_color="#922b21",
            hover_color="#c0392b",
            command=self._chassis_stop,
        ).pack(side="left", padx=3)

        # presets
        pre = ctk.CTkFrame(left, fg_color="transparent")
        pre.pack(fill="x", padx=8, pady=4)
        ctk.CTkLabel(pre, text="预设").pack(side="left", padx=4)
        for label, v, w in (
            ("停", 0.0, 0.0),
            ("慢", 4.0, 0.0),
            ("中", 8.0, 0.0),
            ("左转", 0.0, 3.0),
            ("右转", 0.0, -3.0),
        ):
            ctk.CTkButton(
                pre,
                text=label,
                width=52,
                command=lambda vv=v, ww=w: self._preset_vw(vv, ww),
            ).pack(side="left", padx=2)

        hdg_row = ctk.CTkFrame(left, fg_color="transparent")
        hdg_row.pack(fill="x", padx=8, pady=6)
        self.hdg_entry = LabeledEntry(hdg_row, "航向°", width=70, default="0")
        self.hdg_entry.pack(side="left", padx=4)
        ctk.CTkButton(hdg_row, text="设航向", width=70, command=self._set_heading).pack(
            side="left", padx=4
        )

        ctk.CTkLabel(
            left,
            text="WASD 遥控 · 空格急停\n（输入框聚焦时禁用键盘）",
            justify="left",
            text_color="gray",
        ).pack(anchor="w", padx=12, pady=(4, 10))

        # right: telemetry + plot
        right = ctk.CTkFrame(tab)
        right.grid(row=0, column=1, sticky="nsew", padx=(6, 0), pady=4)
        right.grid_columnconfigure(0, weight=1)
        right.grid_rowconfigure(2, weight=1)

        ctk.CTkLabel(right, text="遥测", font=ctk.CTkFont(size=15, weight="bold")).grid(
            row=0, column=0, sticky="w", padx=10, pady=(10, 2)
        )

        cards = ctk.CTkFrame(right, fg_color="transparent")
        cards.grid(row=1, column=0, sticky="ew", padx=6, pady=4)
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

        plot_wrap = ctk.CTkFrame(right)
        plot_wrap.grid(row=2, column=0, sticky="nsew", padx=8, pady=8)
        plot_wrap.grid_columnconfigure(0, weight=1)
        plot_wrap.grid_rowconfigure(1, weight=1)

        ch_row = ctk.CTkFrame(plot_wrap, fg_color="transparent")
        ch_row.grid(row=0, column=0, sticky="ew", padx=4, pady=2)
        self.plot_v_var = ctk.BooleanVar(value=True)
        self.plot_wl_var = ctk.BooleanVar(value=True)
        self.plot_wr_var = ctk.BooleanVar(value=True)
        self.plot_l_var = ctk.BooleanVar(value=False)
        self.plot_r_var = ctk.BooleanVar(value=False)
        for text, var in (
            ("v", self.plot_v_var),
            ("wl", self.plot_wl_var),
            ("wr", self.plot_wr_var),
            ("L spd", self.plot_l_var),
            ("R spd", self.plot_r_var),
        ):
            ctk.CTkCheckBox(ch_row, text=text, variable=var, width=70, command=self._redraw_plot).pack(
                side="left", padx=4
            )

        self._fig = Figure(figsize=(5, 2.6), dpi=100, facecolor="#1a1a1a")
        self._ax = self._fig.add_subplot(111)
        self._ax.set_facecolor("#1a1a1a")
        self._ax.tick_params(colors="#bbbbbb", labelsize=8)
        for spine in self._ax.spines.values():
            spine.set_color("#555555")
        self._ax.set_ylabel("value", color="#bbbbbb", fontsize=8)
        self._ax.set_xlabel("t (s)", color="#bbbbbb", fontsize=8)
        (self._line_v,) = self._ax.plot([], [], label="v", color="#3498db", lw=1.4)
        (self._line_wl,) = self._ax.plot([], [], label="wl", color="#2ecc71", lw=1.2)
        (self._line_wr,) = self._ax.plot([], [], label="wr", color="#e67e22", lw=1.2)
        (self._line_ls,) = self._ax.plot([], [], label="L", color="#9b59b6", lw=1.0, ls="--")
        (self._line_rs,) = self._ax.plot([], [], label="R", color="#e74c3c", lw=1.0, ls="--")
        self._ax.legend(loc="upper right", fontsize=7, facecolor="#2b2b2b", labelcolor="#dddddd")
        self._fig.tight_layout()
        self._canvas = FigureCanvasTkAgg(self._fig, master=plot_wrap)
        self._canvas.get_tk_widget().grid(row=1, column=0, sticky="nsew")

    # -------------------------------------------------------------- 电机
    def _build_motor_tab(self) -> None:
        tab = self.tab_motor
        tab.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(
            tab,
            text="电机单测（会与底盘抢控制权；操作前自动 chassis stop）",
            text_color="#f0ad4e",
        ).pack(anchor="w", padx=12, pady=(12, 4))

        mask_row = ctk.CTkFrame(tab, fg_color="transparent")
        mask_row.pack(fill="x", padx=12, pady=6)
        self.mask_l = ctk.BooleanVar(value=True)
        self.mask_r = ctk.BooleanVar(value=True)
        ctk.CTkCheckBox(mask_row, text="左 L (0x1)", variable=self.mask_l).pack(side="left", padx=8)
        ctk.CTkCheckBox(mask_row, text="右 R (0x2)", variable=self.mask_r).pack(side="left", padx=8)

        mode_row = ctk.CTkFrame(tab, fg_color="transparent")
        mode_row.pack(fill="x", padx=12, pady=4)
        ctk.CTkLabel(mode_row, text="模式").pack(side="left", padx=4)
        self.motor_mode_var = ctk.StringVar(value="speed")
        ctk.CTkOptionMenu(
            mode_row, variable=self.motor_mode_var, values=list(MOTOR_MODES), width=120
        ).pack(side="left", padx=4)
        ctk.CTkButton(mode_row, text="应用模式", width=90, command=self._apply_motor_mode).pack(
            side="left", padx=4
        )

        set_row = ctk.CTkFrame(tab, fg_color="transparent")
        set_row.pack(fill="x", padx=12, pady=4)
        self.motor_val = LabeledEntry(set_row, "目标", width=100, default="5")
        self.motor_val.pack(side="left", padx=4)
        ctk.CTkButton(set_row, text="set", width=64, command=self._motor_set).pack(side="left", padx=3)
        ctk.CTkButton(set_row, text="stop", width=64, command=self._motor_stop).pack(side="left", padx=3)
        ctk.CTkButton(set_row, text="status", width=70, command=self._motor_status).pack(
            side="left", padx=3
        )
        ctk.CTkButton(set_row, text="param", width=70, command=self._motor_param).pack(
            side="left", padx=3
        )

        jog_row = ctk.CTkFrame(tab, fg_color="transparent")
        jog_row.pack(fill="x", padx=12, pady=8)
        ctk.CTkLabel(jog_row, text="开环点动").pack(side="left", padx=4)
        self.jog_duty = LabeledEntry(jog_row, "duty", width=80, default="1200")
        self.jog_duty.pack(side="left", padx=4)
        self.jog_sec = LabeledEntry(jog_row, "秒", width=50, default="1.5")
        self.jog_sec.pack(side="left", padx=4)
        ctk.CTkButton(jog_row, text="执行点动", width=90, command=self._motor_jog).pack(
            side="left", padx=6
        )
        ctk.CTkButton(
            jog_row,
            text="取消点动",
            width=90,
            fg_color="#7f8c8d",
            command=self._cancel_jog,
        ).pack(side="left", padx=3)

        self.motor_info = ctk.CTkLabel(
            tab,
            text="L: —\nR: —",
            justify="left",
            font=ctk.CTkFont(family="Consolas", size=14),
        )
        self.motor_info.pack(anchor="w", padx=16, pady=16)

        ctk.CTkLabel(
            tab,
            text="speed 目标约 5~12 counts/2ms；openloop duty 建议先 <2000，勿堵转。",
            text_color="gray",
        ).pack(anchor="w", padx=12, pady=8)

    # -------------------------------------------------------------- 参数
    def _build_param_tab(self) -> None:
        tab = self.tab_param
        tab.grid_columnconfigure(0, weight=1)
        tab.grid_columnconfigure(1, weight=1)
        tab.grid_rowconfigure(0, weight=1)

        left = ctk.CTkFrame(tab)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 6), pady=4)
        ctk.CTkLabel(left, text="读写", font=ctk.CTkFont(size=15, weight="bold")).pack(
            anchor="w", padx=10, pady=(10, 4)
        )

        row1 = ctk.CTkFrame(left, fg_color="transparent")
        row1.pack(fill="x", padx=8, pady=4)
        self.param_name = LabeledEntry(row1, "名", width=150, default="")
        self.param_name.pack(side="left", padx=4)
        self.param_value = LabeledEntry(row1, "值", width=90, default="")
        self.param_value.pack(side="left", padx=4)

        row2 = ctk.CTkFrame(left, fg_color="transparent")
        row2.pack(fill="x", padx=8, pady=4)
        ctk.CTkButton(row2, text="get", width=60, command=self._param_get).pack(side="left", padx=3)
        ctk.CTkButton(row2, text="set", width=60, command=self._param_set).pack(side="left", padx=3)
        self.param_prefix = LabeledEntry(row2, "过滤", width=90, default="")
        self.param_prefix.pack(side="left", padx=8)
        self.param_prefix.entry.bind("<KeyRelease>", lambda _e: self._rebuild_param_list())
        ctk.CTkButton(row2, text="show", width=60, command=self._param_show).pack(side="left", padx=3)

        row3 = ctk.CTkFrame(left, fg_color="transparent")
        row3.pack(fill="x", padx=8, pady=8)
        ctk.CTkButton(row3, text="export", width=70, command=self._param_export_console).pack(
            side="left", padx=3
        )
        ctk.CTkButton(row3, text="save", width=70, command=lambda: self._send(cmd.param_save())).pack(
            side="left", padx=3
        )
        ctk.CTkButton(row3, text="load", width=70, command=self._param_load).pack(side="left", padx=3)

        row4 = ctk.CTkFrame(left, fg_color="transparent")
        row4.pack(fill="x", padx=8, pady=4)
        ctk.CTkButton(row4, text="应用 motor param", command=self._motor_param).pack(side="left", padx=4)
        ctk.CTkButton(
            row4, text="应用 chassis param", command=lambda: self._send(cmd.chassis_param())
        ).pack(side="left", padx=4)

        ctk.CTkLabel(
            left,
            text="右侧列表从板端 show 动态拉取；set 只改 RAM；PID 需 apply；掉电用 save。",
            text_color="gray",
        ).pack(anchor="w", padx=12, pady=(8, 12))

        right = ctk.CTkFrame(tab)
        right.grid(row=0, column=1, sticky="nsew", padx=(6, 0), pady=4)
        right.grid_columnconfigure(0, weight=1)
        right.grid_rowconfigure(1, weight=1)

        head = ctk.CTkFrame(right, fg_color="transparent")
        head.pack(fill="x", padx=10, pady=(10, 4))
        ctk.CTkLabel(head, text="板端参数表", font=ctk.CTkFont(size=15, weight="bold")).pack(
            side="left"
        )
        ctk.CTkButton(head, text="刷新", width=60, command=self._request_param_table).pack(
            side="right", padx=2
        )
        self.param_status_lbl = ctk.CTkLabel(head, text="未拉取", text_color="gray")
        self.param_status_lbl.pack(side="right", padx=8)

        self.param_list_frame = ctk.CTkScrollableFrame(right, height=360)
        self.param_list_frame.pack(fill="both", expand=True, padx=8, pady=8)
        self._rebuild_param_list()

    # -------------------------------------------------------------- 控制台
    def _build_console_tab(self) -> None:
        tab = self.tab_console
        tab.grid_columnconfigure(0, weight=1)
        tab.grid_rowconfigure(1, weight=1)

        head = ctk.CTkFrame(tab, fg_color="transparent")
        head.grid(row=0, column=0, sticky="ew", padx=8, pady=(8, 2))
        ctk.CTkLabel(head, text="串口日志", font=ctk.CTkFont(size=15, weight="bold")).pack(side="left")
        self.filter_ack = ctk.BooleanVar(value=False)
        ctk.CTkCheckBox(head, text="仅 ACK/发送", variable=self.filter_ack).pack(side="left", padx=12)
        ctk.CTkButton(head, text="清空", width=60, command=self._clear_log).pack(side="right", padx=4)
        ctk.CTkButton(head, text="help", width=60, command=lambda: self._send(cmd.help_cmd())).pack(
            side="right", padx=4
        )

        self.log_box = ctk.CTkTextbox(tab, font=ctk.CTkFont(family="Consolas", size=12))
        self.log_box.grid(row=1, column=0, sticky="nsew", padx=8, pady=4)
        self.log_box.configure(state="disabled")

        send_row = ctk.CTkFrame(tab, fg_color="transparent")
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

    def _build_help_tab(self) -> None:
        box = ctk.CTkTextbox(self.tab_help, font=ctk.CTkFont(family="Microsoft YaHei UI", size=13))
        box.pack(fill="both", expand=True, padx=8, pady=8)
        box.insert("1.0", HELP_TEXT)
        box.configure(state="disabled")

    # ------------------------------------------------------- focus / ports
    def _bind_entry_focus(self) -> None:
        def on_in(_e=None) -> None:
            # Drop sticky WASD keys when entering a text field.
            if self._keys_down:
                self._keys_down.clear()
                self._update_wasd(force_send=True)
            self._typing = True

        def on_out(_e=None) -> None:
            self._typing = False
            # Discard any keys that stuck while typing (no KeyRelease delivered).
            if self._keys_down:
                self._keys_down.clear()
                self._update_wasd(force_send=True)

        widgets = [
            self.cmd_entry,
            self.baud_entry,
            self.hdg_entry.entry,
            self.motor_val.entry,
            self.jog_duty.entry,
            self.jog_sec.entry,
            self.param_name.entry,
            self.param_value.entry,
            self.param_prefix.entry,
        ]
        for w in widgets:
            w.bind("<FocusIn>", on_in)
            w.bind("<FocusOut>", on_out)

    def _refresh_ports(self) -> None:
        ports = list_serial_ports()
        if not ports:
            ports = ["—"]
        self.port_menu.configure(values=ports)
        cur = self.port_var.get()
        if cur not in ports:
            self.port_var.set(ports[0])

    # ----------------------------------------------------------- connection
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
            self.status_lbl.configure(text=f"打开失败")
            self._log_ui(f"打开 {port} 失败: {e}", "error")
            return

        self.cfg["port"] = port
        self.cfg["baud"] = baud
        save_config(self.cfg)

        self.connect_btn.configure(text="断开")
        self.status_dot.set_state(True)
        self.status_lbl.configure(text=f"{port} @ {baud}")
        self._log_ui(f"已连接 {port} @ {baud}", "tx")
        try:
            self._send(cmd.help_cmd())
        except Exception:
            pass
        # Pull param table after help traffic settles a bit.
        self.after(250, self._request_param_table)

    def _reset_drive_state(self) -> None:
        """Clear local drive/WASD state after stop or disconnect."""
        self._keys_down.clear()
        self._wasd_v = 0.0
        self._wasd_w = 0.0
        self._control_owner = "idle"
        self._chassis_mode = "idle"
        self._vw_dirty = False
        self._vw_sent_v = 0.0
        self._vw_sent_w = 0.0
        try:
            self.v_slider.set(0)
            self.w_slider.set(0)
            self.v_lbl.configure(text="v = 0.00")
            self.w_lbl.configure(text="ω = 0.00")
            self._update_vw_status()
            self._update_owner_label()
        except Exception:
            pass

    def _disconnect(self, send_stop: bool = True, reason: str = "已断开", error: bool = False) -> None:
        self._cancel_jog()
        if send_stop and self.client.is_open:
            try:
                for c in cmd.emergency_stop_cmds():
                    self.client.send_raw(c, with_seq=False)
            except Exception:
                pass
            time.sleep(0.05)
        try:
            self.client.close()
        except Exception:
            pass
        self.connect_btn.configure(text="连接")
        self.status_dot.set_state(False, error=error)
        self.status_lbl.configure(text="连接异常" if error else "未连接")
        self._reset_drive_state()
        self._cancel_param_sync()
        self._log_ui(reason, "error" if error else "tx")

    def _on_close(self) -> None:
        self.cfg["poll_enabled"] = bool(self.poll_var.get())
        save_config(self.cfg)
        for job in (self._poll_job, self._ui_job, self._jog_job, self._param_sync_job):
            if job is not None:
                try:
                    self.after_cancel(job)
                except Exception:
                    pass
        self._disconnect(send_stop=True)
        self.destroy()

    # --------------------------------------------------------------- send
    def _note_manual_status(self, command: str) -> None:
        """Let a few status response lines appear in the console log."""
        low = command.lower()
        if "status" in low:
            self._status_log_budget = max(self._status_log_budget, 8)

    def _send(self, command: str, force_seq: Optional[bool] = None, silent: bool = False) -> None:
        if not self.client.is_open:
            self._log_ui("未连接，无法发送", "error")
            return
        use_seq = self.use_seq.get() if force_seq is None else force_seq
        try:
            seq = self.client.send_raw(command, with_seq=use_seq)
        except Exception as e:
            self._log_ui(f"发送失败: {e}", "error")
            return
        if not silent:
            self._note_manual_status(command)
            if seq >= 0:
                self._log_ui(f"@{seq} {command}", "tx")
            else:
                self._log_ui(command, "tx")

    def _send_free_cmd(self) -> None:
        text = self.cmd_entry.get().strip()
        if not text:
            return
        if text.startswith("@"):
            if not self.client.is_open:
                self._log_ui("未连接，无法发送", "error")
                return
            try:
                self.client.send_raw(text, with_seq=False)
                self._note_manual_status(text)
                self._log_ui(text, "tx")
            except Exception as e:
                self._log_ui(f"发送失败: {e}", "error")
        else:
            self._send(text)
        self.cmd_entry.delete(0, "end")

    def _emergency_stop(self) -> None:
        self._cancel_jog()
        self._reset_drive_state()
        if not self.client.is_open:
            self._log_ui("急停：未连接", "error")
            return
        for c in cmd.emergency_stop_cmds():
            try:
                self.client.send_raw(c, with_seq=False)
                self._log_ui(f"{c}  [ESTOP]", "tx")
            except Exception as e:
                self._log_ui(f"急停发送失败: {e}", "error")

    def _ensure_motor_owner(self) -> bool:
        """Stop chassis if board mode is active before motor commands."""
        if not self.client.is_open:
            self._log_ui("未连接，无法发送", "error")
            return False
        # Trust telemetry mode, not only UI ownership — free-cmd / external control
        # can leave owner=idle while the board is still in speed/openloop/etc.
        mode = (self._chassis_mode or "idle").lower()
        if mode not in ("idle", "", "unknown"):
            self._log_ui(f"电机操作：先停止底盘 (mode={mode})", "tx")
            try:
                self.client.send_raw(cmd.chassis_stop(), with_seq=False)
            except Exception as e:
                self._log_ui(f"停底盘失败: {e}", "error")
                return False
            self._chassis_mode = "idle"
            self.v_slider.set(0)
            self.w_slider.set(0)
            self._on_vw_slide(0)
            self._vw_sent_v = 0.0
            self._vw_sent_w = 0.0
            self._vw_dirty = False
            self._update_vw_status()
        self._control_owner = "motor"
        self._update_owner_label()
        return True

    def _set_chassis_owner(self) -> None:
        self._control_owner = "chassis"
        self._update_owner_label()

    def _update_owner_label(self) -> None:
        colors = {"idle": "#95a5a6", "chassis": "#5dade2", "motor": "#f0ad4e"}
        self.owner_lbl.configure(
            text=f"主控: {self._control_owner}",
            text_color=colors.get(self._control_owner, "#95a5a6"),
        )

    # -------------------------------------------------------------- chassis
    def _on_vw_slide(self, _value=None) -> None:
        v = float(self.v_slider.get())
        w = float(self.w_slider.get())
        self.v_lbl.configure(text=f"v = {v:.2f}")
        self.w_lbl.configure(text=f"ω = {w:.2f}")
        if abs(v - self._vw_sent_v) > 1e-3 or abs(w - self._vw_sent_w) > 1e-3:
            self._vw_dirty = True
            self._update_vw_status()
        if self.live_send_var.get() and self.client.is_open:
            now = time.time()
            if now - self._last_live_send >= 0.1:
                self._last_live_send = now
                self._send_chassis_set(silent=True)

    def _on_slider_release(self, _event=None) -> None:
        if self._vw_dirty:
            self._send_chassis_set()

    def _update_vw_status(self) -> None:
        if self._vw_dirty:
            self.vw_status_lbl.configure(
                text=f"未下发  当前滑条 v={float(self.v_slider.get()):.2f} ω={float(self.w_slider.get()):.2f}",
                text_color="#f0ad4e",
            )
        else:
            self.vw_status_lbl.configure(
                text=f"已下发 v={self._vw_sent_v:.2f} ω={self._vw_sent_w:.2f}",
                text_color="gray",
            )

    def _send_chassis_set(self, silent: bool = False) -> None:
        v = float(self.v_slider.get())
        w = float(self.w_slider.get())
        self._set_chassis_owner()
        self._send(cmd.chassis_set(v, w), force_seq=False if silent else None, silent=silent)
        self._vw_sent_v = v
        self._vw_sent_w = w
        self._vw_dirty = False
        self._update_vw_status()

    def _zero_vw(self) -> None:
        self.v_slider.set(0)
        self.w_slider.set(0)
        self._on_vw_slide(0)
        if self.client.is_open:
            self._send_chassis_set()

    def _preset_vw(self, v: float, w: float) -> None:
        self.v_slider.set(v)
        self.w_slider.set(w)
        self._on_vw_slide(0)
        if abs(v) < 1e-6 and abs(w) < 1e-6:
            self._chassis_stop()
        else:
            self._send_chassis_set()

    def _apply_chassis_mode(self) -> None:
        name = self.chassis_mode_var.get()
        self._set_chassis_owner()
        self._send(cmd.chassis_mode(name))
        self._chassis_mode = name
        if name == "idle":
            self._control_owner = "idle"
            self._update_owner_label()

    def _chassis_stop(self) -> None:
        self.v_slider.set(0)
        self.w_slider.set(0)
        self._on_vw_slide(0)
        self._vw_sent_v = 0.0
        self._vw_sent_w = 0.0
        self._vw_dirty = False
        self._update_vw_status()
        self._send(cmd.chassis_stop())
        self._chassis_mode = "idle"
        self._control_owner = "idle"
        self._update_owner_label()

    def _set_heading(self) -> None:
        try:
            deg = float(self.hdg_entry.get())
        except ValueError:
            self._log_ui("航向角度无效", "error")
            return
        self._set_chassis_owner()
        self._send(cmd.chassis_heading(deg))

    # ---------------------------------------------------------------- motor
    def _motor_mask(self) -> int:
        m = 0
        if self.mask_l.get():
            m |= 0x1
        if self.mask_r.get():
            m |= 0x2
        return m

    def _apply_motor_mode(self) -> None:
        if not self._ensure_motor_owner():
            return
        mask = self._motor_mask()
        if mask == 0:
            self._log_ui("请至少选择一个电机", "error")
            return
        self._send(cmd.motor_mode(mask, self.motor_mode_var.get()))

    def _motor_set(self) -> None:
        if not self._ensure_motor_owner():
            return
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
        if not self.client.is_open:
            self._log_ui("未连接，无法发送", "error")
            return
        mask = self._motor_mask() or 0x3
        self._send(cmd.motor_stop(mask))
        if self._control_owner == "motor":
            self._control_owner = "idle"
            self._update_owner_label()

    def _motor_status(self) -> None:
        mask = self._motor_mask() or 0x3
        self._send(cmd.motor_status(mask))

    def _motor_param(self) -> None:
        mask = self._motor_mask() or 0x3
        self._send(cmd.motor_param(mask))

    def _motor_jog(self) -> None:
        if not self._ensure_motor_owner():
            return
        mask = self._motor_mask()
        if mask == 0:
            self._log_ui("请至少选择一个电机", "error")
            return
        try:
            duty = float(self.jog_duty.get())
            sec = float(self.jog_sec.get())
        except ValueError:
            self._log_ui("点动参数无效", "error")
            return
        if sec <= 0 or sec > 10:
            self._log_ui("点动时间限制 0~10 秒", "error")
            return
        self._cancel_jog()
        self._send(cmd.motor_mode(mask, "openloop"))
        self._send(cmd.motor_set(mask, duty))
        self._log_ui(f"点动中 {sec:.1f}s …", "tx")
        self._jog_job = self.after(int(sec * 1000), lambda: self._jog_finish(mask))

    def _jog_finish(self, mask: int) -> None:
        self._jog_job = None
        if self.client.is_open:
            self._send(cmd.motor_stop(mask))
        self._log_ui("点动结束", "tx")

    def _cancel_jog(self) -> None:
        if self._jog_job is not None:
            try:
                self.after_cancel(self._jog_job)
            except Exception:
                pass
            self._jog_job = None
            if self.client.is_open:
                try:
                    self.client.send_raw(cmd.motor_stop(self._motor_mask() or 0x3), with_seq=False)
                    self._log_ui("点动已取消", "tx")
                except Exception:
                    pass

    # ---------------------------------------------------------------- param
    def _pick_param(self, name: str, fetch: bool = True) -> None:
        self.param_name.set(name)
        entry = self._params.get(name)
        if entry is not None and entry.value:
            self.param_value.set(entry.value)
        if fetch and self.client.is_open:
            # Refresh live value from board RAM.
            self._send(cmd.param_get(name), silent=True)
            self._log_ui(f"get {name}", "tx")

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
        # Optimistic local cache update; confirmed by set terminal / ACK if any.
        old = self._params.get(name)
        self._params[name] = ParamEntry(
            name=name,
            type_name=old.type_name if old else "",
            value=value,
        )
        self._rebuild_param_list()

    def _param_show(self) -> None:
        """Console dump with optional filter prefix (does not replace the table)."""
        self._send(cmd.param_show(self.param_prefix.get()))

    def _param_export_console(self) -> None:
        self._send(cmd.param_export())

    def _param_load(self) -> None:
        self._send(cmd.param_load())
        # Flash values applied on board; refresh table shortly after.
        if self.client.is_open:
            self.after(400, self._request_param_table)

    # ------------------------------------------------------ param table sync
    def _cancel_param_sync(self) -> None:
        self._param_sync_active = False
        self._param_sync_buf.clear()
        self._param_sync_mode = ""
        self._param_sync_seq = -1
        if self._param_sync_job is not None:
            try:
                self.after_cancel(self._param_sync_job)
            except Exception:
                pass
            self._param_sync_job = None

    def _request_param_table(self) -> None:
        """Send `show` and rebuild the right-hand list from board response."""
        if not self.client.is_open:
            self._log_ui("未连接，无法拉取参数表", "error")
            return
        self._cancel_param_sync()
        self._param_sync_active = True
        self._param_sync_mode = "show"
        self._param_sync_buf = {}
        self.param_status_lbl.configure(text="拉取中…", text_color="#f0ad4e")
        try:
            # Prefer seq so we can detect show ACK; lines are still collected from RX.
            seq = self.client.send_raw(cmd.param_show(""), with_seq=True)
            self._param_sync_seq = seq
            self._log_ui(f"@{seq} show  [param table]", "tx")
        except Exception as e:
            self._cancel_param_sync()
            self.param_status_lbl.configure(text="拉取失败", text_color="#e74c3c")
            self._log_ui(f"拉取参数表失败: {e}", "error")
            return
        self._param_sync_job = self.after(PARAM_SYNC_TIMEOUT_MS, self._param_sync_timeout)

    def _param_sync_timeout(self) -> None:
        self._param_sync_job = None
        if not self._param_sync_active:
            return
        n = len(self._param_sync_buf)
        if n > 0:
            self._finish_param_sync(ok=True, note=f"超时，已收 {n} 项")
        else:
            self._finish_param_sync(ok=False, note="超时无数据")

    def _finish_param_sync(self, ok: bool, note: str = "") -> None:
        buf = dict(self._param_sync_buf)
        mode = self._param_sync_mode
        self._param_sync_active = False
        self._param_sync_mode = ""
        self._param_sync_seq = -1
        if self._param_sync_job is not None:
            try:
                self.after_cancel(self._param_sync_job)
            except Exception:
                pass
            self._param_sync_job = None

        if ok and buf:
            # show replaces full table; export only merges storage keys if we ever use it.
            if mode == "export" and self._params:
                merged = dict(self._params)
                for name, entry in buf.items():
                    old = merged.get(name)
                    merged[name] = ParamEntry(
                        name=name,
                        type_name=entry.type_name or (old.type_name if old else ""),
                        value=entry.value if entry.value else (old.value if old else ""),
                    )
                self._params = merged
            else:
                self._params = buf
            self._rebuild_param_list()
            msg = note or f"已同步 {len(self._params)} 项"
            self.param_status_lbl.configure(text=msg, text_color="#2ecc71")
            self._log_ui(f"参数表: {msg}", "tx")
        elif ok and not buf:
            self.param_status_lbl.configure(text="空表", text_color="gray")
            self._log_ui("参数表: 板端无参数或解析失败", "error")
        else:
            self.param_status_lbl.configure(text=note or "失败", text_color="#e74c3c")
            if note:
                self._log_ui(f"参数表: {note}", "error")

    def _rebuild_param_list(self) -> None:
        frame = getattr(self, "param_list_frame", None)
        if frame is None:
            return
        for child in frame.winfo_children():
            child.destroy()

        prefix = ""
        if hasattr(self, "param_prefix"):
            prefix = self.param_prefix.get()

        names = sorted(self._params.keys())
        if prefix:
            names = [n for n in names if n.startswith(prefix)]

        if not names:
            tip = "连接后自动拉取，或点「刷新」"
            if self._params and prefix:
                tip = f"无匹配前缀 “{prefix}” 的参数"
            ctk.CTkLabel(frame, text=tip, text_color="gray").pack(anchor="w", padx=6, pady=8)
            return

        for name in names:
            entry = self._params[name]
            type_s = f"  [{entry.type_name}]" if entry.type_name else ""
            val_s = f"  = {entry.value}" if entry.value else ""
            label = f"{name}{type_s}{val_s}"
            ctk.CTkButton(
                frame,
                text=label,
                anchor="w",
                fg_color="transparent",
                border_width=1,
                command=lambda n=name: self._pick_param(n),
            ).pack(fill="x", pady=2)

    def _upsert_param(self, entry: ParamEntry, rebuild: bool = False) -> None:
        if not entry.name:
            return
        old = self._params.get(entry.name)
        self._params[entry.name] = ParamEntry(
            name=entry.name,
            type_name=entry.type_name or (old.type_name if old else ""),
            value=entry.value if entry.value != "" else (old.value if old else ""),
        )
        if rebuild:
            self._rebuild_param_list()

    def _feed_param_sync_line(self, line: str) -> None:
        if not self._param_sync_active:
            return

        if self._param_sync_mode == "show":
            row = parse_show_param_row(line)
            if row is not None:
                self._param_sync_buf[row.name] = row
                return
            end = parse_show_end(line)
            if end is not None:
                self._finish_param_sync(ok=True, note=f"共 {end[1]} 项 (shown={end[0]})")
                return
        elif self._param_sync_mode == "export":
            row = parse_export_kv(line)
            if row is not None:
                self._param_sync_buf[row.name] = row
                return
            n = parse_export_end(line)
            if n is not None:
                self._finish_param_sync(ok=True, note=f"export {n} 项")
                return

    def _apply_param_value_update(self, entry: ParamEntry) -> None:
        """Update cache + value entry when get/set reports a value."""
        self._upsert_param(entry, rebuild=True)
        if self.param_name.get() == entry.name and entry.value != "":
            self.param_value.set(entry.value)

    # ----------------------------------------------------------------- WASD
    def _on_key_press(self, event) -> None:
        if self._typing:
            return
        key = (event.keysym or "").lower()
        if key == "space":
            self._emergency_stop()
            return
        if key in ("w", "a", "s", "d"):
            self._keys_down.add(key)
            self._update_wasd()

    def _on_key_release(self, event) -> None:
        key = (event.keysym or "").lower()
        if key not in ("w", "a", "s", "d"):
            return
        # Always process release (even while typing) so keys cannot stick.
        if key in self._keys_down:
            self._keys_down.discard(key)
            self._update_wasd(force_send=True)

    def _update_wasd(self, force_send: bool = False) -> None:
        v = 0.0
        w = 0.0
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
        self.v_lbl.configure(text=f"v = {v:.2f}")
        self.w_lbl.configure(text=f"ω = {w:.2f}")

        stopped = abs(v) < 1e-6 and abs(w) < 1e-6
        # Stop must never be dropped by rate-limit (safety).
        if stopped:
            force_send = True

        now = time.time()
        if not force_send and (now - self._last_wasd_send) < WASD_SEND_MIN_DT:
            if abs(v - self._vw_sent_v) > 1e-3 or abs(w - self._vw_sent_w) > 1e-3:
                self._vw_dirty = True
                self._update_vw_status()
            return

        self._last_wasd_send = now
        if self.client.is_open:
            try:
                self._set_chassis_owner()
                self.client.send_raw(cmd.chassis_set(v, w), with_seq=False)
                self._vw_sent_v = v
                self._vw_sent_w = w
                self._vw_dirty = False
                self._update_vw_status()
            except Exception as e:
                self._log_ui(f"WASD 发送失败: {e}", "error")
        else:
            self._vw_sent_v = v
            self._vw_sent_w = w
            self._vw_dirty = False
            self._update_vw_status()

    # ---------------------------------------------------------- poll / log
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
            # silent, no seq — reduce ACK noise (B1)
            self.client.send_raw(cmd.chassis_status(), with_seq=False)
            self.client.send_raw(cmd.motor_status(0x3), with_seq=False)
        except Exception as e:
            self._put_disconnect(str(e))

    def _put_disconnect(self, msg: str) -> None:
        # Same cleanup path as manual disconnect (cancel jog, clear WASD, etc.).
        self._disconnect(send_stop=False, reason=f"串口异常: {msg}", error=True)

    def _clear_log(self) -> None:
        self.log_box.configure(state="normal")
        self.log_box.delete("1.0", "end")
        self.log_box.configure(state="disabled")

    def _trim_log(self) -> None:
        """Keep log_box from growing without bound (long sessions)."""
        try:
            end_index = self.log_box.index("end-1c")
            line_count = int(end_index.split(".")[0])
        except Exception:
            return
        if line_count <= LOG_MAX_LINES:
            return
        # Drop oldest half when over limit.
        drop = line_count - (LOG_MAX_LINES // 2)
        self.log_box.delete("1.0", f"{drop}.0")

    def _log_ui(self, text: str, kind: str = "rx") -> None:
        prefixes = {
            "tx": "[TX]  ",
            "ack_ok": "[ACK] ",
            "ack_err": "[ERR] ",
            "error": "[ERR] ",
            "rx": "[RX]  ",
        }
        prefix = prefixes.get(kind, "[RX]  ")
        line = prefix + text
        if self.filter_ack.get() and kind == "rx":
            return
        self.log_box.configure(state="normal")
        self.log_box.insert("end", line + "\n")
        self._trim_log()
        self.log_box.see("end")
        self.log_box.configure(state="disabled")

    def _poll_queues(self) -> None:
        n = 0
        while n < 100:
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
            self._put_disconnect(err)

        self._ui_job = self.after(50, self._poll_queues)

    def _handle_rx_line(self, line: str) -> None:
        if "{motor_l}" in line:
            return

        # Feed dynamic param table collector (before status filtering).
        self._feed_param_sync_line(line)

        # Live get/set terminal lines update cache + value box.
        got = parse_get_terminal(line)
        if got is not None:
            self._apply_param_value_update(got)
        else:
            setted = parse_set_terminal(line)
            if setted is not None:
                self._apply_param_value_update(setted)

        is_status = "chassis:" in line or "Left:" in line or "Right:" in line
        # Hide background poll noise, but show manual status replies.
        if self.poll_var.get() and is_status:
            if self._status_log_budget > 0:
                self._status_log_budget -= 1
            else:
                return
        if self.filter_ack.get() and "{cmd_ack}" not in line:
            return
        tag, body = strip_log_tag(line)
        display = line if not tag else f"{{{tag}}}{body}"
        # cmd_ack lines handled also in _handle_ack — skip duplicate RX if pure ack
        if "{cmd_ack}" in line:
            return
        self._log_ui(display, "rx")

    def _handle_ack(self, ack: Ack) -> None:
        if ack.ok and ack.ctx == "status_printed":
            return

        # show ACK: count=N — finish sync if still waiting (rows may already be done via shown=).
        if (
            self._param_sync_active
            and self._param_sync_mode == "show"
            and ack.ok
            and (self._param_sync_seq < 0 or ack.seq == self._param_sync_seq)
        ):
            fields = parse_ctx_fields(ack.ctx)
            if "count" in fields:
                n = len(self._param_sync_buf)
                total = fields["count"]
                self._finish_param_sync(ok=True, note=f"共 {total} 项" if n == 0 else f"共 {n} 项")

        # get ACK ctx carries structured value.
        if ack.ok and ack.ctx:
            pa = parse_param_ack_ctx(ack.ctx)
            if pa is not None:
                self._apply_param_value_update(pa)

        kind = "ack_ok" if ack.ok else "ack_err"
        extra = f" ctx={ack.ctx}" if ack.ctx else ""
        self._log_ui(f"seq={ack.seq} {ack.result}{extra}", kind)
        if not ack.ok:
            self.status_lbl.configure(text=f"ACK {ack.result}")

    def _handle_chassis(self, ch: ChassisStatus) -> None:
        self.card_mode.set_value(ch.mode)
        self.card_vw.set_value(f"{ch.v:.2f} / {ch.w:.2f}")
        self.card_yaw.set_value(f"{ch.yaw:.1f} / {ch.gz:.1f}")
        self.card_wheel.set_value(f"{ch.wl:.2f} / {ch.wr:.2f}")
        self.card_imu.set_value("ready" if ch.imu else "no")
        self._chassis_mode = ch.mode
        t = time.time() - self._t0
        self._plot_t.append(t)
        self._plot_v.append(ch.v)
        self._plot_wl.append(ch.wl)
        self._plot_wr.append(ch.wr)
        self._plot_l_spd.append(self._left_spd)
        self._plot_r_spd.append(self._right_spd)
        self._redraw_plot()

    @staticmethod
    def _motor_tgt_text(mode: str, tgt_spd: float, tgt_pwm: float) -> str:
        m = (mode or "").lower()
        if "open" in m:
            return f"pwm={tgt_pwm:+.0f}"
        if "pos" in m:
            return f"pos_tgt={tgt_spd:+.1f}"
        return f"spd={tgt_spd:+.2f}"

    def _refresh_motor_info(self) -> None:
        lt = self._motor_tgt_text(self._left_mode, self._left_tgt, self._left_tgt_pwm)
        rt = self._motor_tgt_text(self._right_mode, self._right_tgt, self._right_tgt_pwm)
        self.motor_info.configure(
            text=(
                f"L: mode={self._left_mode}  spd={self._left_spd:+.2f}  "
                f"tgt[{lt}]  pos={self._left_pos:+.0f}\n"
                f"R: mode={self._right_mode}  spd={self._right_spd:+.2f}  "
                f"tgt[{rt}]  pos={self._right_pos:+.0f}"
            )
        )

    def _handle_motor(self, m: MotorStatus) -> None:
        if m.name == "Left":
            self._left_spd = m.spd
            self._left_tgt = m.tgt_spd
            self._left_tgt_pwm = m.tgt_pwm
            self._left_pos = m.pos
            self._left_mode = m.mode
        else:
            self._right_spd = m.spd
            self._right_tgt = m.tgt_spd
            self._right_tgt_pwm = m.tgt_pwm
            self._right_pos = m.pos
            self._right_mode = m.mode
        self._refresh_motor_info()

    def _redraw_plot(self) -> None:
        if not self._plot_t:
            return
        xs = list(self._plot_t)

        def set_line(line, enabled: bool, ys: Deque[float]) -> None:
            if enabled:
                line.set_data(xs, list(ys))
            else:
                line.set_data([], [])

        set_line(self._line_v, self.plot_v_var.get(), self._plot_v)
        set_line(self._line_wl, self.plot_wl_var.get(), self._plot_wl)
        set_line(self._line_wr, self.plot_wr_var.get(), self._plot_wr)
        set_line(self._line_ls, self.plot_l_var.get(), self._plot_l_spd)
        set_line(self._line_rs, self.plot_r_var.get(), self._plot_r_spd)
        self._ax.relim()
        self._ax.autoscale_view()
        self._canvas.draw_idle()
