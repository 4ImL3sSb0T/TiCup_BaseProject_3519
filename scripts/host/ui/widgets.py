"""Reusable CustomTkinter helpers."""

from __future__ import annotations

import customtkinter as ctk


class StatusDot(ctk.CTkLabel):
    def __init__(self, master, **kwargs):
        super().__init__(master, text="●", width=24, font=ctk.CTkFont(size=16), **kwargs)
        self.set_state(False)

    def set_state(self, connected: bool, error: bool = False) -> None:
        if error:
            self.configure(text_color="#e74c3c")
        elif connected:
            self.configure(text_color="#2ecc71")
        else:
            self.configure(text_color="#7f8c8d")


class MetricCard(ctk.CTkFrame):
    def __init__(self, master, title: str, value: str = "—", **kwargs):
        super().__init__(master, **kwargs)
        self._title = ctk.CTkLabel(self, text=title, font=ctk.CTkFont(size=11), text_color="gray")
        self._title.pack(anchor="w", padx=8, pady=(6, 0))
        self._value = ctk.CTkLabel(self, text=value, font=ctk.CTkFont(size=16, weight="bold"))
        self._value.pack(anchor="w", padx=8, pady=(0, 6))

    def set_value(self, text: str) -> None:
        self._value.configure(text=text)


class LabeledEntry(ctk.CTkFrame):
    def __init__(self, master, label: str, width: int = 100, default: str = "", **kwargs):
        super().__init__(master, fg_color="transparent", **kwargs)
        ctk.CTkLabel(self, text=label).pack(side="left", padx=(0, 4))
        self.entry = ctk.CTkEntry(self, width=width)
        self.entry.pack(side="left")
        if default:
            self.entry.insert(0, default)

    def get(self) -> str:
        return self.entry.get().strip()

    def set(self, text: str) -> None:
        self.entry.delete(0, "end")
        self.entry.insert(0, text)
