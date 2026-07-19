"""Build firmware command strings (no trailing newline)."""

from __future__ import annotations


def chassis_mode(name: str) -> str:
    return f"chassis mode {name}"


def chassis_set(v: float, omega: float) -> str:
    return f"chassis set {v:.3f} {omega:.3f}"


def chassis_heading(deg: float) -> str:
    return f"chassis heading {deg:.2f}"


def chassis_stop() -> str:
    return "chassis stop"


def chassis_status() -> str:
    return "chassis status"


def chassis_param() -> str:
    return "chassis param"


def motor_cmd(mask: int, sub: str, val: str | float | int | None = None) -> str:
    """mask: bit0=Left bit1=Right (1/2/3)."""
    base = f"motor 0x{mask:X} {sub}"
    if val is None:
        return base
    if isinstance(val, float):
        return f"{base} {val:.3f}"
    return f"{base} {val}"


def motor_stop(mask: int = 0x3) -> str:
    return motor_cmd(mask, "stop")


def motor_status(mask: int = 0x3) -> str:
    return motor_cmd(mask, "status")


def motor_mode(mask: int, mode: str) -> str:
    return motor_cmd(mask, "mode", mode)


def motor_set(mask: int, value: float) -> str:
    return motor_cmd(mask, "set", value)


def motor_param(mask: int = 0x3) -> str:
    return motor_cmd(mask, "param")


def param_set(name: str, value: str) -> str:
    return f"set {name} {value}"


def param_get(name: str) -> str:
    return f"get {name}"


def param_show(prefix: str = "") -> str:
    if prefix:
        return f"show {prefix}"
    return "show"


def param_export() -> str:
    return "export"


def param_save() -> str:
    return "save"


def param_load() -> str:
    return "load"


def help_cmd() -> str:
    return "help"


def emergency_stop_cmds() -> list[str]:
    """Prefer chassis stop first, then force both motors off."""
    return [chassis_stop(), motor_stop(0x3)]
