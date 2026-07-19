"""Param tab: get/set/show + dynamic board param table."""

from __future__ import annotations

from typing import TYPE_CHECKING, Dict, Optional

import customtkinter as ctk

from host import commands as cmd
from host.protocol import (
    Ack,
    ParamEntry,
    parse_ctx_fields,
    parse_export_end,
    parse_export_kv,
    parse_get_terminal,
    parse_param_ack_ctx,
    parse_set_terminal,
    parse_show_end,
    parse_show_param_row,
)
from host.ui.widgets import LabeledEntry

if TYPE_CHECKING:
    from host.ui.app import HostApp

PARAM_SYNC_TIMEOUT_MS = 3500


class ParamTab:
    def __init__(self, app: HostApp, parent: ctk.CTkFrame) -> None:
        self.app = app
        self._params: Dict[str, ParamEntry] = {}
        self._param_sync_active = False
        self._param_sync_buf: Dict[str, ParamEntry] = {}
        self._param_sync_mode = ""
        self._param_sync_job: Optional[str] = None
        self._param_sync_seq = -1
        self._build(parent)

    def _build(self, tab: ctk.CTkFrame) -> None:
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
        ctk.CTkButton(row2, text="get", width=60, command=self.param_get).pack(side="left", padx=3)
        ctk.CTkButton(row2, text="set", width=60, command=self.param_set).pack(side="left", padx=3)
        self.param_prefix = LabeledEntry(row2, "过滤", width=90, default="")
        self.param_prefix.pack(side="left", padx=8)
        self.param_prefix.entry.bind("<KeyRelease>", lambda _e: self.rebuild_param_list())
        ctk.CTkButton(row2, text="show", width=60, command=self.param_show).pack(side="left", padx=3)

        row3 = ctk.CTkFrame(left, fg_color="transparent")
        row3.pack(fill="x", padx=8, pady=8)
        ctk.CTkButton(row3, text="export", width=70, command=self.param_export_console).pack(
            side="left", padx=3
        )
        ctk.CTkButton(
            row3, text="save", width=70, command=lambda: self.app.send(cmd.param_save())
        ).pack(side="left", padx=3)
        ctk.CTkButton(row3, text="load", width=70, command=self.param_load).pack(side="left", padx=3)

        row4 = ctk.CTkFrame(left, fg_color="transparent")
        row4.pack(fill="x", padx=8, pady=4)
        ctk.CTkButton(row4, text="应用 motor param", command=self.app.motor.motor_param).pack(
            side="left", padx=4
        )
        ctk.CTkButton(
            row4, text="应用 chassis param", command=lambda: self.app.send(cmd.chassis_param())
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
        ctk.CTkButton(head, text="刷新", width=60, command=self.request_param_table).pack(
            side="right", padx=2
        )
        self.param_status_lbl = ctk.CTkLabel(head, text="未拉取", text_color="gray")
        self.param_status_lbl.pack(side="right", padx=8)

        self.param_list_frame = ctk.CTkScrollableFrame(right, height=360)
        self.param_list_frame.pack(fill="both", expand=True, padx=8, pady=8)
        self.rebuild_param_list()

    def text_entries(self) -> list:
        return [self.param_name.entry, self.param_value.entry, self.param_prefix.entry]

    def cancel_jobs(self) -> None:
        self.cancel_param_sync()

    def pick_param(self, name: str, fetch: bool = True) -> None:
        self.param_name.set(name)
        entry = self._params.get(name)
        if entry is not None and entry.value:
            self.param_value.set(entry.value)
        if fetch and self.app.client.is_open:
            self.app.send(cmd.param_get(name), silent=True)
            self.app.log(f"get {name}", "tx")

    def param_get(self) -> None:
        name = self.param_name.get()
        if not name:
            return
        self.app.send(cmd.param_get(name))

    def param_set(self) -> None:
        name = self.param_name.get()
        value = self.param_value.get()
        if not name or not value:
            self.app.log("参数名/值不能为空", "error")
            return
        self.app.send(cmd.param_set(name, value))
        old = self._params.get(name)
        self._params[name] = ParamEntry(
            name=name,
            type_name=old.type_name if old else "",
            value=value,
        )
        self.rebuild_param_list()

    def param_show(self) -> None:
        self.app.send(cmd.param_show(self.param_prefix.get()))

    def param_export_console(self) -> None:
        self.app.send(cmd.param_export())

    def param_load(self) -> None:
        self.app.send(cmd.param_load())
        if self.app.client.is_open:
            self.app.after(400, self.request_param_table)

    # ------------------------------------------------------ param table sync
    def cancel_param_sync(self) -> None:
        self._param_sync_active = False
        self._param_sync_buf.clear()
        self._param_sync_mode = ""
        self._param_sync_seq = -1
        if self._param_sync_job is not None:
            try:
                self.app.after_cancel(self._param_sync_job)
            except Exception:
                pass
            self._param_sync_job = None

    def request_param_table(self) -> None:
        if not self.app.client.is_open:
            self.app.log("未连接，无法拉取参数表", "error")
            return
        self.cancel_param_sync()
        self._param_sync_active = True
        self._param_sync_mode = "show"
        self._param_sync_buf = {}
        self.param_status_lbl.configure(text="拉取中…", text_color="#f0ad4e")
        try:
            seq = self.app.client.send_raw(cmd.param_show(""), with_seq=True)
            self._param_sync_seq = seq
            self.app.log(f"@{seq} show  [param table]", "tx")
        except Exception as e:
            self.cancel_param_sync()
            self.param_status_lbl.configure(text="拉取失败", text_color="#e74c3c")
            self.app.log(f"拉取参数表失败: {e}", "error")
            return
        self._param_sync_job = self.app.after(PARAM_SYNC_TIMEOUT_MS, self._param_sync_timeout)

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
                self.app.after_cancel(self._param_sync_job)
            except Exception:
                pass
            self._param_sync_job = None

        if ok and buf:
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
            self.rebuild_param_list()
            msg = note or f"已同步 {len(self._params)} 项"
            self.param_status_lbl.configure(text=msg, text_color="#2ecc71")
            self.app.log(f"参数表: {msg}", "tx")
        elif ok and not buf:
            self.param_status_lbl.configure(text="空表", text_color="gray")
            self.app.log("参数表: 板端无参数或解析失败", "error")
        else:
            self.param_status_lbl.configure(text=note or "失败", text_color="#e74c3c")
            if note:
                self.app.log(f"参数表: {note}", "error")

    def rebuild_param_list(self) -> None:
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
                command=lambda n=name: self.pick_param(n),
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
            self.rebuild_param_list()

    def feed_rx_line(self, line: str) -> None:
        """Consume show/export rows and get/set terminal lines."""
        if self._param_sync_active:
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

        got = parse_get_terminal(line)
        if got is not None:
            self.apply_param_value_update(got)
            return
        setted = parse_set_terminal(line)
        if setted is not None:
            self.apply_param_value_update(setted)

    def handle_ack(self, ack: Ack) -> None:
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
                self._finish_param_sync(
                    ok=True, note=f"共 {total} 项" if n == 0 else f"共 {n} 项"
                )

        if ack.ok and ack.ctx:
            pa = parse_param_ack_ctx(ack.ctx)
            if pa is not None:
                self.apply_param_value_update(pa)

    def apply_param_value_update(self, entry: ParamEntry) -> None:
        self._upsert_param(entry, rebuild=True)
        if self.param_name.get() == entry.name and entry.value != "":
            self.param_value.set(entry.value)
