"""Track + Mission tab: line-follow debug and mission start/stop."""

from __future__ import annotations

from typing import TYPE_CHECKING

import customtkinter as ctk

from host import commands as cmd
from host.protocol import MissionStatus, TrackDetail, TrackStatus
from host.ui.widgets import LabeledEntry, MetricCard

if TYPE_CHECKING:
    from host.ui.app import HostApp

MISSION_HINTS = {
    "1": "循迹到丢线 → 直行撞线 → 停",
    "2": "直行 + 弧转 180° 往返",
    "3": "航向直行 AC/BD + 弧转",
    "4": "同 3 + 多圈 LAP（可设 laps）",
}


class TrackMissionTab:
    def __init__(self, app: HostApp, parent: ctk.CTkFrame) -> None:
        self.app = app
        self._build(parent)

    def _build(self, tab: ctk.CTkFrame) -> None:
        tab.grid_columnconfigure(0, weight=1)
        tab.grid_columnconfigure(1, weight=1)
        tab.grid_rowconfigure(1, weight=1)

        tip = ctk.CTkLabel(
            tab,
            text="track 与 mission 互斥；线速度共用 track_app_v。操作前会尽量停掉对方。急停含 mission/track stop。",
            text_color="#f0ad4e",
            wraplength=1100,
            justify="left",
        )
        tip.grid(row=0, column=0, columnspan=2, sticky="ew", padx=12, pady=(10, 4))

        left = ctk.CTkFrame(tab)
        left.grid(row=1, column=0, sticky="nsew", padx=(8, 4), pady=4)
        right = ctk.CTkFrame(tab)
        right.grid(row=1, column=1, sticky="nsew", padx=(4, 8), pady=4)

        self._build_track(left)
        self._build_mission(right)

    def _build_track(self, frame: ctk.CTkFrame) -> None:
        ctk.CTkLabel(frame, text="循迹 track", font=ctk.CTkFont(size=15, weight="bold")).pack(
            anchor="w", padx=10, pady=(10, 4)
        )

        btn1 = ctk.CTkFrame(frame, fg_color="transparent")
        btn1.pack(fill="x", padx=8, pady=4)
        ctk.CTkButton(btn1, text="scan", width=70, command=self.track_scan).pack(side="left", padx=3)
        ctk.CTkButton(btn1, text="status", width=70, command=self.track_status).pack(
            side="left", padx=3
        )
        ctk.CTkButton(
            btn1,
            text="start",
            width=70,
            fg_color="#1e8449",
            hover_color="#27ae60",
            command=self.track_start,
        ).pack(side="left", padx=3)
        ctk.CTkButton(
            btn1,
            text="stop",
            width=70,
            fg_color="#922b21",
            hover_color="#c0392b",
            command=self.track_stop,
        ).pack(side="left", padx=3)

        pol_row = ctk.CTkFrame(frame, fg_color="transparent")
        pol_row.pack(fill="x", padx=8, pady=6)
        ctk.CTkLabel(pol_row, text="极性 pol").pack(side="left", padx=4)
        self.pol_var = ctk.StringVar(value="0")
        ctk.CTkOptionMenu(
            pol_row, variable=self.pol_var, values=["0", "1"], width=60
        ).pack(side="left", padx=4)
        ctk.CTkButton(pol_row, text="应用", width=60, command=self.track_pol_apply).pack(
            side="left", padx=4
        )
        ctk.CTkLabel(pol_row, text="0=黑线/低有效", text_color="gray").pack(side="left", padx=6)

        cal_row = ctk.CTkFrame(frame, fg_color="transparent")
        cal_row.pack(fill="x", padx=8, pady=4)
        ctk.CTkLabel(cal_row, text="GS08 标定").pack(side="left", padx=4)
        ctk.CTkButton(
            cal_row, text="cal max", width=80, command=lambda: self.app.send(cmd.track_cal("max"))
        ).pack(side="left", padx=3)
        ctk.CTkButton(
            cal_row, text="cal min", width=80, command=lambda: self.app.send(cmd.track_cal("min"))
        ).pack(side="left", padx=3)
        ctk.CTkLabel(cal_row, text="五路 GPIO 可忽略", text_color="gray").pack(side="left", padx=6)

        ctk.CTkLabel(frame, text="常用参数（set RAM）", text_color="gray").pack(
            anchor="w", padx=12, pady=(10, 2)
        )
        p1 = ctk.CTkFrame(frame, fg_color="transparent")
        p1.pack(fill="x", padx=8, pady=2)
        self.v_entry = LabeledEntry(p1, "track_app_v", width=70, default="6")
        self.v_entry.pack(side="left", padx=4)
        ctk.CTkButton(p1, text="set v", width=60, command=self.set_track_v).pack(side="left", padx=3)

        p2 = ctk.CTkFrame(frame, fg_color="transparent")
        p2.pack(fill="x", padx=8, pady=2)
        self.sign_entry = LabeledEntry(p2, "sign ±1", width=50, default="1")
        self.sign_entry.pack(side="left", padx=4)
        ctk.CTkButton(p2, text="set sign", width=70, command=self.set_track_sign).pack(
            side="left", padx=3
        )
        self.kp_entry = LabeledEntry(p2, "kp", width=60, default="40")
        self.kp_entry.pack(side="left", padx=8)
        ctk.CTkButton(p2, text="set kp", width=60, command=self.set_track_kp).pack(
            side="left", padx=3
        )

        p3 = ctk.CTkFrame(frame, fg_color="transparent")
        p3.pack(fill="x", padx=8, pady=2)
        self.lost_entry = LabeledEntry(p3, "lost_ms", width=70, default="0")
        self.lost_entry.pack(side="left", padx=4)
        ctk.CTkButton(p3, text="set lost", width=70, command=self.set_track_lost).pack(
            side="left", padx=3
        )
        ctk.CTkButton(
            p3,
            text="show track_app",
            width=120,
            command=lambda: self.app.send(cmd.param_show("track_app")),
        ).pack(side="left", padx=8)
        ctk.CTkButton(
            p3, text="save", width=60, command=lambda: self.app.send(cmd.param_save())
        ).pack(side="left", padx=3)

        cards = ctk.CTkFrame(frame, fg_color="transparent")
        cards.pack(fill="x", padx=6, pady=(12, 4))
        for i in range(4):
            cards.grid_columnconfigure(i, weight=1)
        self.card_tr_state = MetricCard(cards, "状态")
        self.card_tr_state.grid(row=0, column=0, sticky="ew", padx=3, pady=2)
        self.card_tr_mask = MetricCard(cards, "mask / on")
        self.card_tr_mask.grid(row=0, column=1, sticky="ew", padx=3, pady=2)
        self.card_tr_err = MetricCard(cards, "err / lost")
        self.card_tr_err.grid(row=0, column=2, sticky="ew", padx=3, pady=2)
        self.card_tr_v = MetricCard(cards, "v / ω_cmd")
        self.card_tr_v.grid(row=0, column=3, sticky="ew", padx=3, pady=2)

        cards2 = ctk.CTkFrame(frame, fg_color="transparent")
        cards2.pack(fill="x", padx=6, pady=2)
        for i in range(2):
            cards2.grid_columnconfigure(i, weight=1)
        self.card_tr_backend = MetricCard(cards2, "backend / pol")
        self.card_tr_backend.grid(row=0, column=0, sticky="ew", padx=3, pady=2)
        self.card_tr_sign = MetricCard(cards2, "sign")
        self.card_tr_sign.grid(row=0, column=1, sticky="ew", padx=3, pady=2)

        ctk.CTkLabel(
            frame,
            text="建议：scan 确认传感 → pol → 小 v 后 start；方向反了改 sign=-1。",
            text_color="gray",
            wraplength=520,
            justify="left",
        ).pack(anchor="w", padx=12, pady=(8, 12))

    def _build_mission(self, frame: ctk.CTkFrame) -> None:
        ctk.CTkLabel(frame, text="任务 mission", font=ctk.CTkFont(size=15, weight="bold")).pack(
            anchor="w", padx=10, pady=(10, 4)
        )

        id_row = ctk.CTkFrame(frame, fg_color="transparent")
        id_row.pack(fill="x", padx=8, pady=6)
        ctk.CTkLabel(id_row, text="任务 ID").pack(side="left", padx=4)
        self.mission_id_var = ctk.StringVar(value="1")
        ctk.CTkOptionMenu(
            id_row,
            variable=self.mission_id_var,
            values=["1", "2", "3", "4"],
            width=70,
            command=self._on_mission_id,
        ).pack(side="left", padx=4)
        self.laps_entry = LabeledEntry(id_row, "laps(仅4)", width=50, default="4")
        self.laps_entry.pack(side="left", padx=8)

        self.mission_hint = ctk.CTkLabel(
            frame,
            text=MISSION_HINTS["1"],
            text_color="gray",
            wraplength=500,
            justify="left",
        )
        self.mission_hint.pack(anchor="w", padx=14, pady=(0, 6))

        btn = ctk.CTkFrame(frame, fg_color="transparent")
        btn.pack(fill="x", padx=8, pady=6)
        ctk.CTkButton(
            btn,
            text="start",
            width=80,
            fg_color="#1e8449",
            hover_color="#27ae60",
            command=self.mission_start,
        ).pack(side="left", padx=3)
        ctk.CTkButton(
            btn,
            text="stop",
            width=80,
            fg_color="#922b21",
            hover_color="#c0392b",
            command=self.mission_stop,
        ).pack(side="left", padx=3)
        ctk.CTkButton(btn, text="status", width=80, command=self.mission_status).pack(
            side="left", padx=3
        )

        both = ctk.CTkFrame(frame, fg_color="transparent")
        both.pack(fill="x", padx=8, pady=8)
        ctk.CTkButton(
            both,
            text="停 track + mission",
            width=150,
            fg_color="#7f8c8d",
            hover_color="#95a5a6",
            command=self.stop_both,
        ).pack(side="left", padx=3)

        cards = ctk.CTkFrame(frame, fg_color="transparent")
        cards.pack(fill="x", padx=6, pady=(16, 4))
        for i in range(3):
            cards.grid_columnconfigure(i, weight=1)
        self.card_mi_state = MetricCard(cards, "状态")
        self.card_mi_state.grid(row=0, column=0, sticky="ew", padx=3, pady=2)
        self.card_mi_id = MetricCard(cards, "任务 / 步")
        self.card_mi_id.grid(row=0, column=1, sticky="ew", padx=3, pady=2)
        self.card_mi_lap = MetricCard(cards, "圈 / v")
        self.card_mi_lap.grid(row=0, column=2, sticky="ew", padx=3, pady=2)

        ctk.CTkLabel(
            frame,
            text="先用 track 把线跟稳再 mission。\n"
            "start 1 最短；4 可设 laps。\n"
            "日志看 step type=notify|track_to_lost|straight|arc|lap。\n"
            "BUSY：先 stop track 或等 mission 结束。",
            text_color="gray",
            justify="left",
            wraplength=500,
        ).pack(anchor="w", padx=12, pady=(12, 12))

    # ------------------------------------------------------------------ entries
    def text_entries(self) -> list:
        return [
            self.v_entry.entry,
            self.sign_entry.entry,
            self.kp_entry.entry,
            self.lost_entry.entry,
            self.laps_entry.entry,
        ]

    def _on_mission_id(self, value: str) -> None:
        self.mission_hint.configure(text=MISSION_HINTS.get(value, ""))

    # ------------------------------------------------------------------ track
    def track_scan(self) -> None:
        self.app.send(cmd.track_scan())

    def track_status(self) -> None:
        self.app.send(cmd.track_status())

    def track_start(self) -> None:
        # Firmware rejects mission start while track runs; stop mission first for safety.
        if self.app.control_owner == "mission":
            self.app.send(cmd.mission_stop(), force_seq=False)
        self.app.set_control_owner("track")
        self.app.send(cmd.track_start())

    def track_stop(self) -> None:
        self.app.send(cmd.track_stop())
        if self.app.control_owner == "track":
            self.app.set_control_owner("idle")

    def track_pol_apply(self) -> None:
        try:
            pol = int(self.pol_var.get())
        except ValueError:
            self.app.log("极性无效", "error")
            return
        self.app.send(cmd.track_pol(pol))

    def set_track_v(self) -> None:
        v = self.v_entry.get()
        if not v:
            self.app.log("track_app_v 为空", "error")
            return
        self.app.send(cmd.param_set("track_app_v", v))

    def set_track_sign(self) -> None:
        s = self.sign_entry.get()
        if not s:
            self.app.log("sign 为空", "error")
            return
        self.app.send(cmd.param_set("track_app_sign", s))

    def set_track_kp(self) -> None:
        k = self.kp_entry.get()
        if not k:
            self.app.log("kp 为空", "error")
            return
        self.app.send(cmd.param_set("track_app_kp", k))

    def set_track_lost(self) -> None:
        m = self.lost_entry.get()
        if not m:
            self.app.log("lost_ms 为空", "error")
            return
        self.app.send(cmd.param_set("track_app_lost_ms", m))

    # ---------------------------------------------------------------- mission
    def mission_start(self) -> None:
        try:
            mid = int(self.mission_id_var.get())
        except ValueError:
            self.app.log("任务 ID 无效", "error")
            return
        if mid < 1 or mid > 4:
            self.app.log("任务 ID 需 1–4", "error")
            return

        laps: int | None = None
        if mid == 4:
            laps_s = self.laps_entry.get()
            if laps_s:
                try:
                    laps = int(laps_s)
                except ValueError:
                    self.app.log("laps 无效", "error")
                    return
                if laps < 0:
                    self.app.log("laps 不能为负", "error")
                    return

        if self.app.control_owner == "track":
            self.app.send(cmd.track_stop(), force_seq=False)
        self.app.set_control_owner("mission")
        self.app.send(cmd.mission_start(mid, laps))

    def mission_stop(self) -> None:
        self.app.send(cmd.mission_stop())
        if self.app.control_owner == "mission":
            self.app.set_control_owner("idle")

    def mission_status(self) -> None:
        self.app.send(cmd.mission_status())

    def stop_both(self) -> None:
        self.app.send(cmd.mission_stop(), force_seq=False)
        self.app.send(cmd.track_stop(), force_seq=False)
        if self.app.control_owner in ("track", "mission"):
            self.app.set_control_owner("idle")

    # --------------------------------------------------------------- telemetry
    def handle_track(self, st: TrackStatus) -> None:
        self.card_tr_state.set_value(st.state)
        self.card_tr_mask.set_value(f"0x{st.mask:02X} / {st.on}")
        self.card_tr_err.set_value(f"{st.err:.3f} / {st.lost}")

    def handle_track_detail(self, d: TrackDetail) -> None:
        if d.v is not None and d.omega_cmd is not None:
            self.card_tr_v.set_value(f"{d.v:.1f} / {d.omega_cmd:.1f}")
        if d.sign is not None:
            self.card_tr_sign.set_value(f"{d.sign:.0f}")
        if d.backend is not None and d.pol is not None:
            ch = d.ch if d.ch is not None else "—"
            self.card_tr_backend.set_value(f"{d.backend} pol={d.pol} ch={ch}")

    def handle_mission(self, st: MissionStatus) -> None:
        self.card_mi_state.set_value(st.state)
        self.card_mi_id.set_value(f"#{st.mission_id}  {st.step}/{st.step_n}")
        self.card_mi_lap.set_value(f"{st.lap}/{st.laps_total}  v={st.v:.1f}")
        if st.state in ("completed", "failed", "aborted", "idle"):
            if self.app.control_owner == "mission":
                self.app.set_control_owner("idle")
        elif st.state == "running":
            self.app.set_control_owner("mission")
