"""Drive tab: chassis control, WASD, telemetry cards, plot."""

from __future__ import annotations

import time
from typing import TYPE_CHECKING

import customtkinter as ctk

from host import commands as cmd
from host.protocol import ChassisStatus
from host.ui.plot import TelemetryPlot
from host.ui.widgets import LabeledEntry, MetricCard

if TYPE_CHECKING:
    from host.ui.app import HostApp

CHASSIS_MODES = ("idle", "openloop", "speed", "yaw_rate", "heading")
WASD_SEND_MIN_DT = 0.08


class DriveTab:
    def __init__(self, app: HostApp, parent: ctk.CTkFrame) -> None:
        self.app = app
        self._keys_down: set[str] = set()
        self._wasd_v = 0.0
        self._wasd_w = 0.0
        self._last_wasd_send = 0.0
        self._last_live_send = 0.0
        self._vw_dirty = False
        self._vw_sent_v = 0.0
        self._vw_sent_w = 0.0
        self._t0 = time.time()
        self.plot: TelemetryPlot | None = None

        self._build(parent)

    def _build(self, tab: ctk.CTkFrame) -> None:
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
        ctk.CTkButton(mode_row, text="应用", width=70, command=self.apply_chassis_mode).pack(
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
        ctk.CTkButton(btn_row, text="发送 set", width=80, command=self.send_chassis_set).pack(
            side="left", padx=3
        )
        ctk.CTkButton(btn_row, text="清零", width=60, command=self.zero_vw).pack(side="left", padx=3)
        ctk.CTkButton(
            btn_row, text="status", width=70, command=lambda: self.app.send(cmd.chassis_status())
        ).pack(side="left", padx=3)
        ctk.CTkButton(
            btn_row,
            text="底盘停",
            width=70,
            fg_color="#922b21",
            hover_color="#c0392b",
            command=self.chassis_stop,
        ).pack(side="left", padx=3)

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
                command=lambda vv=v, ww=w: self.preset_vw(vv, ww),
            ).pack(side="left", padx=2)

        hdg_row = ctk.CTkFrame(left, fg_color="transparent")
        hdg_row.pack(fill="x", padx=8, pady=6)
        self.hdg_entry = LabeledEntry(hdg_row, "航向°", width=70, default="0")
        self.hdg_entry.pack(side="left", padx=4)
        ctk.CTkButton(hdg_row, text="设航向", width=70, command=self.set_heading).pack(
            side="left", padx=4
        )

        ctk.CTkLabel(
            left,
            text="WASD 遥控 · 空格急停\n（输入框聚焦时禁用键盘）",
            justify="left",
            text_color="gray",
        ).pack(anchor="w", padx=12, pady=(4, 10))

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
            ctk.CTkCheckBox(ch_row, text=text, variable=var, width=70, command=self.redraw_plot).pack(
                side="left", padx=4
            )

        self.plot = TelemetryPlot(plot_wrap)
        self.plot.widget.grid(row=1, column=0, sticky="nsew")

    def text_entries(self) -> list:
        return [self.hdg_entry.entry]

    def update_vw_status(self) -> None:
        if self._vw_dirty:
            self.vw_status_lbl.configure(
                text=(
                    f"未下发  当前滑条 v={float(self.v_slider.get()):.2f} "
                    f"ω={float(self.w_slider.get()):.2f}"
                ),
                text_color="#f0ad4e",
            )
        else:
            self.vw_status_lbl.configure(
                text=f"已下发 v={self._vw_sent_v:.2f} ω={self._vw_sent_w:.2f}",
                text_color="gray",
            )

    def force_idle_sliders(self) -> None:
        """Zero sliders after chassis stop (motor preemption)."""
        self.v_slider.set(0)
        self.w_slider.set(0)
        self._on_vw_slide(0)
        self._vw_sent_v = 0.0
        self._vw_sent_w = 0.0
        self._vw_dirty = False
        self.update_vw_status()

    def reset_state(self) -> None:
        """Clear local drive/WASD state after stop or disconnect."""
        self._keys_down.clear()
        self._wasd_v = 0.0
        self._wasd_w = 0.0
        self._vw_dirty = False
        self._vw_sent_v = 0.0
        self._vw_sent_w = 0.0
        try:
            self.v_slider.set(0)
            self.w_slider.set(0)
            self.v_lbl.configure(text="v = 0.00")
            self.w_lbl.configure(text="ω = 0.00")
            self.update_vw_status()
        except Exception:
            pass
        self.app.chassis_mode = "idle"
        self.app.set_control_owner("idle")

    def _on_vw_slide(self, _value=None) -> None:
        v = float(self.v_slider.get())
        w = float(self.w_slider.get())
        self.v_lbl.configure(text=f"v = {v:.2f}")
        self.w_lbl.configure(text=f"ω = {w:.2f}")
        if abs(v - self._vw_sent_v) > 1e-3 or abs(w - self._vw_sent_w) > 1e-3:
            self._vw_dirty = True
            self.update_vw_status()
        if self.live_send_var.get() and self.app.client.is_open:
            now = time.time()
            if now - self._last_live_send >= 0.1:
                self._last_live_send = now
                self.send_chassis_set(silent=True)

    def _on_slider_release(self, _event=None) -> None:
        if self._vw_dirty:
            self.send_chassis_set()

    def send_chassis_set(self, silent: bool = False) -> None:
        v = float(self.v_slider.get())
        w = float(self.w_slider.get())
        self.app.set_control_owner("chassis")
        self.app.send(
            cmd.chassis_set(v, w),
            force_seq=False if silent else None,
            silent=silent,
        )
        self._vw_sent_v = v
        self._vw_sent_w = w
        self._vw_dirty = False
        self.update_vw_status()

    def zero_vw(self) -> None:
        self.v_slider.set(0)
        self.w_slider.set(0)
        self._on_vw_slide(0)
        if self.app.client.is_open:
            self.send_chassis_set()

    def preset_vw(self, v: float, w: float) -> None:
        self.v_slider.set(v)
        self.w_slider.set(w)
        self._on_vw_slide(0)
        if abs(v) < 1e-6 and abs(w) < 1e-6:
            self.chassis_stop()
        else:
            self.send_chassis_set()

    def apply_chassis_mode(self) -> None:
        name = self.chassis_mode_var.get()
        self.app.set_control_owner("chassis")
        self.app.send(cmd.chassis_mode(name))
        self.app.chassis_mode = name
        if name == "idle":
            self.app.set_control_owner("idle")

    def chassis_stop(self) -> None:
        self.v_slider.set(0)
        self.w_slider.set(0)
        self._on_vw_slide(0)
        self._vw_sent_v = 0.0
        self._vw_sent_w = 0.0
        self._vw_dirty = False
        self.update_vw_status()
        self.app.send(cmd.chassis_stop())
        self.app.chassis_mode = "idle"
        self.app.set_control_owner("idle")

    def set_heading(self) -> None:
        try:
            deg = float(self.hdg_entry.get())
        except ValueError:
            self.app.log("航向角度无效", "error")
            return
        self.app.set_control_owner("chassis")
        self.app.send(cmd.chassis_heading(deg))

    # ---------------------------------------------------------------- WASD
    def on_key_press(self, event) -> None:
        if self.app.typing:
            return
        key = (event.keysym or "").lower()
        if key == "space":
            self.app.emergency_stop()
            return
        if key in ("w", "a", "s", "d"):
            self._keys_down.add(key)
            self._update_wasd()

    def on_key_release(self, event) -> None:
        key = (event.keysym or "").lower()
        if key not in ("w", "a", "s", "d"):
            return
        if key in self._keys_down:
            self._keys_down.discard(key)
            self._update_wasd(force_send=True)

    def on_typing_focus(self) -> None:
        if self._keys_down:
            self._keys_down.clear()
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
        if stopped:
            force_send = True

        now = time.time()
        if not force_send and (now - self._last_wasd_send) < WASD_SEND_MIN_DT:
            if abs(v - self._vw_sent_v) > 1e-3 or abs(w - self._vw_sent_w) > 1e-3:
                self._vw_dirty = True
                self.update_vw_status()
            return

        self._last_wasd_send = now
        if self.app.client.is_open:
            try:
                self.app.set_control_owner("chassis")
                self.app.client.send_raw(cmd.chassis_set(v, w), with_seq=False)
                self._vw_sent_v = v
                self._vw_sent_w = w
                self._vw_dirty = False
                self.update_vw_status()
            except Exception as e:
                self.app.log(f"WASD 发送失败: {e}", "error")
        else:
            self._vw_sent_v = v
            self._vw_sent_w = w
            self._vw_dirty = False
            self.update_vw_status()

    # ------------------------------------------------------------ telemetry
    def handle_chassis(self, ch: ChassisStatus) -> None:
        self.card_mode.set_value(ch.mode)
        self.card_vw.set_value(f"{ch.v:.2f} / {ch.w:.2f}")
        self.card_yaw.set_value(f"{ch.yaw:.1f} / {ch.gz:.1f}")
        self.card_wheel.set_value(f"{ch.wl:.2f} / {ch.wr:.2f}")
        self.card_imu.set_value("ready" if ch.imu else "no")
        self.app.chassis_mode = ch.mode
        if self.plot is None:
            return
        t = time.time() - self._t0
        motor = self.app.motor
        self.plot.append(t, ch.v, ch.wl, ch.wr, motor.left_spd, motor.right_spd)
        self.redraw_plot()

    def redraw_plot(self) -> None:
        if self.plot is None:
            return
        self.plot.redraw(
            show_v=self.plot_v_var.get(),
            show_wl=self.plot_wl_var.get(),
            show_wr=self.plot_wr_var.get(),
            show_l=self.plot_l_var.get(),
            show_r=self.plot_r_var.get(),
        )
