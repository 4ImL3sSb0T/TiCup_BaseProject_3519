"""Main CustomTkinter host application shell (connection + dispatch)."""

from __future__ import annotations

import time
from typing import Optional

import customtkinter as ctk

from host import commands as cmd
from host.config_store import load_config, save_config
from host.protocol import Ack, strip_log_tag
from host.serial_client import DEFAULT_BAUD, SerialClient, list_serial_ports
from host.ui.help_text import HELP_TEXT
from host.ui.tabs import ConsoleTab, DriveTab, MotorTab, ParamTab, TrackMissionTab
from host.ui.widgets import StatusDot

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")


class HostApp(ctk.CTk):
    """Window shell: connection bar, tab host, queue poll, emergency stop."""

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
        self._poll_job: Optional[str] = None
        self._ui_job: Optional[str] = None
        self.typing = False
        self.control_owner = "idle"  # idle | chassis | motor | track | mission
        self.chassis_mode = "idle"
        # Allow a few status lines through the log after manual status requests.
        self._status_log_budget = 0

        # Tab panels (assigned in _build_ui).
        self.drive: DriveTab
        self.motor: MotorTab
        self.track_mission: TrackMissionTab
        self.param: ParamTab
        self.console: ConsoleTab

        self._build_ui()
        self._bind_entry_focus()
        self._refresh_ports()
        if self.cfg.get("port"):
            ports = list(self.port_menu.cget("values") or [])
            if self.cfg["port"] in ports:
                self.port_var.set(self.cfg["port"])

        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.bind("<KeyPress>", self.drive.on_key_press)
        self.bind("<KeyRelease>", self.drive.on_key_release)

        self._ui_job = self.after(50, self._poll_queues)
        self._schedule_status_poll()
        self._update_owner_label()
        self.drive.update_vw_status()

    # ================================================================== UI
    def _build_ui(self) -> None:
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        self._build_connection_bar()

        self.tabs = ctk.CTkTabview(self)
        self.tabs.grid(row=1, column=0, sticky="nsew", padx=10, pady=(0, 10))
        tab_drive = self.tabs.add("驾驶")
        tab_motor = self.tabs.add("电机")
        tab_track = self.tabs.add("循迹/任务")
        tab_param = self.tabs.add("参数")
        tab_console = self.tabs.add("控制台")
        tab_help = self.tabs.add("帮助")

        # Motor before param: param "应用 motor param" calls motor.motor_param.
        self.drive = DriveTab(self, tab_drive)
        self.motor = MotorTab(self, tab_motor)
        self.track_mission = TrackMissionTab(self, tab_track)
        self.param = ParamTab(self, tab_param)
        self.console = ConsoleTab(self, tab_console)
        self._build_help_tab(tab_help)

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
            command=self.emergency_stop,
        )
        self.estop_btn.grid(row=0, column=10, padx=10, pady=8)

    def _build_help_tab(self, tab: ctk.CTkFrame) -> None:
        box = ctk.CTkTextbox(tab, font=ctk.CTkFont(family="Microsoft YaHei UI", size=13))
        box.pack(fill="both", expand=True, padx=8, pady=8)
        box.insert("1.0", HELP_TEXT)
        box.configure(state="disabled")

    # ------------------------------------------------------- focus / ports
    def _bind_entry_focus(self) -> None:
        def on_in(_e=None) -> None:
            self.drive.on_typing_focus()
            self.typing = True

        def on_out(_e=None) -> None:
            self.typing = False
            self.drive.on_typing_focus()

        widgets = [self.baud_entry]
        widgets.extend(self.drive.text_entries())
        widgets.extend(self.motor.text_entries())
        widgets.extend(self.track_mission.text_entries())
        widgets.extend(self.param.text_entries())
        widgets.extend(self.console.text_entries())
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
            self.log("请选择有效串口", "error")
            return
        try:
            baud = int(self.baud_var.get().strip())
        except ValueError:
            self.log("波特率无效", "error")
            return
        try:
            self.client.open(port, baud)
        except Exception as e:
            self.status_dot.set_state(False, error=True)
            self.status_lbl.configure(text="打开失败")
            self.log(f"打开 {port} 失败: {e}", "error")
            return

        self.cfg["port"] = port
        self.cfg["baud"] = baud
        save_config(self.cfg)

        self.connect_btn.configure(text="断开")
        self.status_dot.set_state(True)
        self.status_lbl.configure(text=f"{port} @ {baud}")
        self.log(f"已连接 {port} @ {baud}", "tx")
        try:
            self.send(cmd.help_cmd())
        except Exception:
            pass
        self.after(250, self.param.request_param_table)

    def _disconnect(self, send_stop: bool = True, reason: str = "已断开", error: bool = False) -> None:
        self.motor.cancel_jobs()
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
        self.drive.reset_state()
        self.param.cancel_param_sync()
        self.log(reason, "error" if error else "tx")

    def _on_close(self) -> None:
        self.cfg["poll_enabled"] = bool(self.poll_var.get())
        save_config(self.cfg)
        for job in (self._poll_job, self._ui_job):
            if job is not None:
                try:
                    self.after_cancel(job)
                except Exception:
                    pass
        self.motor.cancel_jobs()
        self.param.cancel_jobs()
        self._disconnect(send_stop=True)
        self.destroy()

    # ----------------------------------------------------- public helpers
    def log(self, text: str, kind: str = "rx") -> None:
        self.console.log(text, kind)

    def note_manual_status(self, command: str) -> None:
        low = command.lower()
        if "status" in low:
            self._status_log_budget = max(self._status_log_budget, 8)

    def send(
        self,
        command: str,
        force_seq: Optional[bool] = None,
        silent: bool = False,
    ) -> None:
        if not self.client.is_open:
            self.log("未连接，无法发送", "error")
            return
        use_seq = self.console.use_seq.get() if force_seq is None else force_seq
        try:
            seq = self.client.send_raw(command, with_seq=use_seq)
        except Exception as e:
            self.log(f"发送失败: {e}", "error")
            return
        if not silent:
            self.note_manual_status(command)
            if seq >= 0:
                self.log(f"@{seq} {command}", "tx")
            else:
                self.log(command, "tx")

    def set_control_owner(self, owner: str) -> None:
        self.control_owner = owner
        self._update_owner_label()

    def _update_owner_label(self) -> None:
        colors = {
            "idle": "#95a5a6",
            "chassis": "#5dade2",
            "motor": "#f0ad4e",
            "track": "#58d68d",
            "mission": "#af7ac5",
        }
        self.owner_lbl.configure(
            text=f"主控: {self.control_owner}",
            text_color=colors.get(self.control_owner, "#95a5a6"),
        )

    def emergency_stop(self) -> None:
        self.motor.cancel_jog()
        self.drive.reset_state()
        self.set_control_owner("idle")
        if not self.client.is_open:
            self.log("急停：未连接", "error")
            return
        for c in cmd.emergency_stop_cmds():
            try:
                self.client.send_raw(c, with_seq=False)
                self.log(f"{c}  [ESTOP]", "tx")
            except Exception as e:
                self.log(f"急停发送失败: {e}", "error")

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
            self.client.send_raw(cmd.chassis_status(), with_seq=False)
            self.client.send_raw(cmd.motor_status(0x3), with_seq=False)
        except Exception as e:
            self._put_disconnect(str(e))

    def _put_disconnect(self, msg: str) -> None:
        self._disconnect(send_stop=False, reason=f"串口异常: {msg}", error=True)

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
            self.drive.handle_chassis(ch)

        while True:
            try:
                m = self.client.motor_queue.get_nowait()
            except Exception:
                break
            self.motor.handle_motor(m)

        while True:
            try:
                tr = self.client.track_queue.get_nowait()
            except Exception:
                break
            self.track_mission.handle_track(tr)

        while True:
            try:
                td = self.client.track_detail_queue.get_nowait()
            except Exception:
                break
            self.track_mission.handle_track_detail(td)

        while True:
            try:
                mi = self.client.mission_queue.get_nowait()
            except Exception:
                break
            self.track_mission.handle_mission(mi)

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

        self.param.feed_rx_line(line)

        is_status = (
            "chassis:" in line
            or "Left:" in line
            or "Right:" in line
            or line.lstrip().startswith("track:")
            or "{terminal}track:" in line
            or "mission:" in line
        )
        if self.poll_var.get() and is_status:
            if self._status_log_budget > 0:
                self._status_log_budget -= 1
            else:
                return
        if self.console.filter_ack.get() and "{cmd_ack}" not in line:
            return
        tag, body = strip_log_tag(line)
        display = line if not tag else f"{{{tag}}}{body}"
        if "{cmd_ack}" in line:
            return
        self.log(display, "rx")

    def _handle_ack(self, ack: Ack) -> None:
        if ack.ok and ack.ctx == "status_printed":
            return

        self.param.handle_ack(ack)

        kind = "ack_ok" if ack.ok else "ack_err"
        extra = f" ctx={ack.ctx}" if ack.ctx else ""
        self.log(f"seq={ack.seq} {ack.result}{extra}", kind)
        if not ack.ok:
            self.status_lbl.configure(text=f"ACK {ack.result}")
