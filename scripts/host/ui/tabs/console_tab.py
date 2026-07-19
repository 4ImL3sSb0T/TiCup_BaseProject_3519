"""Console tab: serial log + free command entry."""

from __future__ import annotations

from typing import TYPE_CHECKING

import customtkinter as ctk

from host import commands as cmd

if TYPE_CHECKING:
    from host.ui.app import HostApp

LOG_MAX_LINES = 2000


class ConsoleTab:
    def __init__(self, app: HostApp, parent: ctk.CTkFrame) -> None:
        self.app = app
        self._build(parent)

    def _build(self, tab: ctk.CTkFrame) -> None:
        tab.grid_columnconfigure(0, weight=1)
        tab.grid_rowconfigure(1, weight=1)

        head = ctk.CTkFrame(tab, fg_color="transparent")
        head.grid(row=0, column=0, sticky="ew", padx=8, pady=(8, 2))
        ctk.CTkLabel(head, text="串口日志", font=ctk.CTkFont(size=15, weight="bold")).pack(side="left")
        self.filter_ack = ctk.BooleanVar(value=False)
        ctk.CTkCheckBox(head, text="仅 ACK/发送", variable=self.filter_ack).pack(side="left", padx=12)
        ctk.CTkButton(head, text="清空", width=60, command=self.clear_log).pack(side="right", padx=4)
        ctk.CTkButton(
            head, text="help", width=60, command=lambda: self.app.send(cmd.help_cmd())
        ).pack(side="right", padx=4)

        self.log_box = ctk.CTkTextbox(tab, font=ctk.CTkFont(family="Consolas", size=12))
        self.log_box.grid(row=1, column=0, sticky="nsew", padx=8, pady=4)
        self.log_box.configure(state="disabled")

        send_row = ctk.CTkFrame(tab, fg_color="transparent")
        send_row.grid(row=2, column=0, sticky="ew", padx=8, pady=(2, 8))
        send_row.grid_columnconfigure(0, weight=1)
        self.cmd_entry = ctk.CTkEntry(send_row, placeholder_text="自由命令，例如: chassis status")
        self.cmd_entry.grid(row=0, column=0, sticky="ew", padx=(0, 6))
        self.cmd_entry.bind("<Return>", lambda _e: self.send_free_cmd())
        self.use_seq = ctk.BooleanVar(value=True)
        ctk.CTkCheckBox(send_row, text="@seq", variable=self.use_seq, width=60).grid(
            row=0, column=1, padx=4
        )
        ctk.CTkButton(send_row, text="发送", width=80, command=self.send_free_cmd).grid(
            row=0, column=2, padx=4
        )

    def text_entries(self) -> list:
        return [self.cmd_entry]

    def clear_log(self) -> None:
        self.log_box.configure(state="normal")
        self.log_box.delete("1.0", "end")
        self.log_box.configure(state="disabled")

    def _trim_log(self) -> None:
        try:
            end_index = self.log_box.index("end-1c")
            line_count = int(end_index.split(".")[0])
        except Exception:
            return
        if line_count <= LOG_MAX_LINES:
            return
        drop = line_count - (LOG_MAX_LINES // 2)
        self.log_box.delete("1.0", f"{drop}.0")

    def log(self, text: str, kind: str = "rx") -> None:
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

    def send_free_cmd(self) -> None:
        text = self.cmd_entry.get().strip()
        if not text:
            return
        if text.startswith("@"):
            if not self.app.client.is_open:
                self.app.log("未连接，无法发送", "error")
                return
            try:
                self.app.client.send_raw(text, with_seq=False)
                self.app.note_manual_status(text)
                self.app.log(text, "tx")
            except Exception as e:
                self.app.log(f"发送失败: {e}", "error")
        else:
            self.app.send(text)
        self.cmd_entry.delete(0, "end")
