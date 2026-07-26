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


# --- track (app/track_app) -------------------------------------------------

def track_start() -> str:
    return "track start"


def track_stop() -> str:
    return "track stop"


def track_status() -> str:
    return "track status"


def track_scan() -> str:
    return "track scan"


def track_pol(polarity: int) -> str:
    """polarity: 0 = black line / dig low; 1 = inverted."""
    return f"track pol {int(polarity) & 1}"


def track_cal(kind: str) -> str:
    """kind: max | min (GS08 only)."""
    return f"track cal {kind}"


# --- mission (app/mission) -------------------------------------------------

def mission_start(mission_id: int, laps: int | None = None) -> str:
    """mission_id: 1..4; laps only used for mission 4 (omit/0 → firmware default)."""
    mid = int(mission_id)
    if laps is None:
        return f"mission start {mid}"
    return f"mission start {mid} {int(laps)}"


def mission_stop() -> str:
    return "mission stop"


def mission_status() -> str:
    return "mission status"


def emergency_stop_cmds() -> list[str]:
    """Stop mission/track first, then chassis + both motors."""
    return [mission_stop(), track_stop(), chassis_stop(), motor_stop(0x3)]
