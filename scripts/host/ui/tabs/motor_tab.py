"""Motor tab: single/dual wheel mode, set, jog."""

from __future__ import annotations

from typing import TYPE_CHECKING, Optional

import customtkinter as ctk

from host import commands as cmd
from host.protocol import MotorStatus
from host.ui.widgets import LabeledEntry

if TYPE_CHECKING:
    from host.ui.app import HostApp

MOTOR_MODES = ("speed", "openloop", "position")


class MotorTab:
    def __init__(self, app: HostApp, parent: ctk.CTkFrame) -> None:
        self.app = app
        self._jog_job: Optional[str] = None

        self.left_spd = 0.0
        self.right_spd = 0.0
        self._left_tgt = 0.0
        self._right_tgt = 0.0
        self._left_tgt_pwm = 0.0
        self._right_tgt_pwm = 0.0
        self._left_pos = 0.0
        self._right_pos = 0.0
        self._left_mode = "—"
        self._right_mode = "—"

        self._build(parent)

    def _build(self, tab: ctk.CTkFrame) -> None:
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
        ctk.CTkButton(mode_row, text="应用模式", width=90, command=self.apply_mode).pack(
            side="left", padx=4
        )

        set_row = ctk.CTkFrame(tab, fg_color="transparent")
        set_row.pack(fill="x", padx=12, pady=4)
        self.motor_val = LabeledEntry(set_row, "目标", width=100, default="5")
        self.motor_val.pack(side="left", padx=4)
        ctk.CTkButton(set_row, text="set", width=64, command=self.motor_set).pack(side="left", padx=3)
        ctk.CTkButton(set_row, text="stop", width=64, command=self.motor_stop).pack(side="left", padx=3)
        ctk.CTkButton(set_row, text="status", width=70, command=self.motor_status).pack(
            side="left", padx=3
        )
        ctk.CTkButton(set_row, text="param", width=70, command=self.motor_param).pack(
            side="left", padx=3
        )

        jog_row = ctk.CTkFrame(tab, fg_color="transparent")
        jog_row.pack(fill="x", padx=12, pady=8)
        ctk.CTkLabel(jog_row, text="开环点动").pack(side="left", padx=4)
        self.jog_duty = LabeledEntry(jog_row, "duty", width=80, default="1200")
        self.jog_duty.pack(side="left", padx=4)
        self.jog_sec = LabeledEntry(jog_row, "秒", width=50, default="1.5")
        self.jog_sec.pack(side="left", padx=4)
        ctk.CTkButton(jog_row, text="执行点动", width=90, command=self.motor_jog).pack(
            side="left", padx=6
        )
        ctk.CTkButton(
            jog_row,
            text="取消点动",
            width=90,
            fg_color="#7f8c8d",
            command=self.cancel_jog,
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

    def text_entries(self) -> list:
        return [self.motor_val.entry, self.jog_duty.entry, self.jog_sec.entry]

    def cancel_jobs(self) -> None:
        self.cancel_jog()

    def motor_mask(self) -> int:
        m = 0
        if self.mask_l.get():
            m |= 0x1
        if self.mask_r.get():
            m |= 0x2
        return m

    def ensure_motor_owner(self) -> bool:
        """Stop chassis if board mode is active before motor commands."""
        if not self.app.client.is_open:
            self.app.log("未连接，无法发送", "error")
            return False
        mode = (self.app.chassis_mode or "idle").lower()
        if mode not in ("idle", "", "unknown"):
            self.app.log(f"电机操作：先停止底盘 (mode={mode})", "tx")
            try:
                self.app.client.send_raw(cmd.chassis_stop(), with_seq=False)
            except Exception as e:
                self.app.log(f"停底盘失败: {e}", "error")
                return False
            self.app.chassis_mode = "idle"
            self.app.drive.force_idle_sliders()
        self.app.set_control_owner("motor")
        return True

    def apply_mode(self) -> None:
        if not self.ensure_motor_owner():
            return
        mask = self.motor_mask()
        if mask == 0:
            self.app.log("请至少选择一个电机", "error")
            return
        self.app.send(cmd.motor_mode(mask, self.motor_mode_var.get()))

    def motor_set(self) -> None:
        if not self.ensure_motor_owner():
            return
        mask = self.motor_mask()
        if mask == 0:
            self.app.log("请至少选择一个电机", "error")
            return
        try:
            val = float(self.motor_val.get())
        except ValueError:
            self.app.log("电机目标值无效", "error")
            return
        self.app.send(cmd.motor_set(mask, val))

    def motor_stop(self) -> None:
        if not self.app.client.is_open:
            self.app.log("未连接，无法发送", "error")
            return
        mask = self.motor_mask() or 0x3
        self.app.send(cmd.motor_stop(mask))
        if self.app.control_owner == "motor":
            self.app.set_control_owner("idle")

    def motor_status(self) -> None:
        mask = self.motor_mask() or 0x3
        self.app.send(cmd.motor_status(mask))

    def motor_param(self) -> None:
        mask = self.motor_mask() or 0x3
        self.app.send(cmd.motor_param(mask))

    def motor_jog(self) -> None:
        if not self.ensure_motor_owner():
            return
        mask = self.motor_mask()
        if mask == 0:
            self.app.log("请至少选择一个电机", "error")
            return
        try:
            duty = float(self.jog_duty.get())
            sec = float(self.jog_sec.get())
        except ValueError:
            self.app.log("点动参数无效", "error")
            return
        if sec <= 0 or sec > 10:
            self.app.log("点动时间限制 0~10 秒", "error")
            return
        self.cancel_jog()
        self.app.send(cmd.motor_mode(mask, "openloop"))
        self.app.send(cmd.motor_set(mask, duty))
        self.app.log(f"点动中 {sec:.1f}s …", "tx")
        self._jog_job = self.app.after(int(sec * 1000), lambda: self._jog_finish(mask))

    def _jog_finish(self, mask: int) -> None:
        self._jog_job = None
        if self.app.client.is_open:
            self.app.send(cmd.motor_stop(mask))
        self.app.log("点动结束", "tx")

    def cancel_jog(self) -> None:
        if self._jog_job is not None:
            try:
                self.app.after_cancel(self._jog_job)
            except Exception:
                pass
            self._jog_job = None
            if self.app.client.is_open:
                try:
                    self.app.client.send_raw(
                        cmd.motor_stop(self.motor_mask() or 0x3), with_seq=False
                    )
                    self.app.log("点动已取消", "tx")
                except Exception:
                    pass

    @staticmethod
    def _motor_tgt_text(mode: str, tgt_spd: float, tgt_pwm: float) -> str:
        m = (mode or "").lower()
        if "open" in m:
            return f"pwm={tgt_pwm:+.0f}"
        if "pos" in m:
            return f"pos_tgt={tgt_spd:+.1f}"
        return f"spd={tgt_spd:+.2f}"

    def _refresh_info(self) -> None:
        lt = self._motor_tgt_text(self._left_mode, self._left_tgt, self._left_tgt_pwm)
        rt = self._motor_tgt_text(self._right_mode, self._right_tgt, self._right_tgt_pwm)
        self.motor_info.configure(
            text=(
                f"L: mode={self._left_mode}  spd={self.left_spd:+.2f}  "
                f"tgt[{lt}]  pos={self._left_pos:+.0f}\n"
                f"R: mode={self._right_mode}  spd={self.right_spd:+.2f}  "
                f"tgt[{rt}]  pos={self._right_pos:+.0f}"
            )
        )

    def handle_motor(self, m: MotorStatus) -> None:
        if m.name == "Left":
            self.left_spd = m.spd
            self._left_tgt = m.tgt_spd
            self._left_tgt_pwm = m.tgt_pwm
            self._left_pos = m.pos
            self._left_mode = m.mode
        else:
            self.right_spd = m.spd
            self._right_tgt = m.tgt_spd
            self._right_tgt_pwm = m.tgt_pwm
            self._right_pos = m.pos
            self._right_mode = m.mode
        self._refresh_info()
