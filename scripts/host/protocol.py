"""Parse firmware serial protocol lines (cmd_ack / status / log tags / params)."""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Dict, Optional, Tuple

# {cmd_ack} seq=12 result=EXIT_OK [ctx=mode_set]
_RE_ACK = re.compile(
    r"\{cmd_ack\}\s+seq=(-?\d+)\s+result=(\S+)(?:\s+ctx=(\S+))?"
)

# chassis: mode=speed v=5.00 w=0.00 hdg_tgt=0.0 yaw=12.3 gz=-0.1 wl=5.00 wr=5.00 imu=1
_RE_CHASSIS = re.compile(
    r"chassis:\s*mode=(\w+)\s+"
    r"v=([-\d.]+)\s+w=([-\d.]+)\s+"
    r"hdg_tgt=([-\d.]+)\s+yaw=([-\d.]+)\s+gz=([-\d.]+)\s+"
    r"wl=([-\d.]+)\s+wr=([-\d.]+)\s+imu=(\d+)"
)

# Left: pos=123 spd=1.23 tgt_spd=8.00 tgt_pwm=0 mode=speed dz=0
_RE_MOTOR = re.compile(
    r"(Left|Right):\s*"
    r"pos=([-\d.]+)\s+spd=([-\d.]+)\s+"
    r"tgt_spd=([-\d.]+)\s+tgt_pwm=([-\d.]+)\s+"
    r"mode=(\w+)\s+dz=([-\d.]+)"
)

_RE_TAG = re.compile(r"^\{([a-zA-Z0-9_]+)\}(.*)$")

# show row: motor_kp             FLOAT      120.000000
_RE_SHOW_ROW = re.compile(
    r"^([A-Za-z_][A-Za-z0-9_]*)\s+"
    r"(UINT8|UINT16|UINT32|INT8|INT16|INT32|FLOAT|UNKNOWN)\s+"
    r"(\S+)"
)
_RE_SHOW_END = re.compile(r"^shown=(\d+)\s+total=(\d+)\s*$")

# export: motor_kp=120.000000  or  # export count=12
_RE_EXPORT_KV = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)=(.+)$")
_RE_EXPORT_END = re.compile(r"^#\s*export\s+count=(\d+)\s*$")

# terminal get: Parameter motor_kp value: 120.000000
_RE_GET_TERMINAL = re.compile(
    r"^Parameter\s+([A-Za-z_][A-Za-z0-9_]*)\s+value:\s*(.+)$"
)
# terminal set: Set motor_kp = 120.000000
_RE_SET_TERMINAL = re.compile(
    r"^Set\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$"
)

# ACK ctx get: name=motor_kp;type=float;value=120.000000
# ACK ctx set: name=motor_kp;updated=1
# ACK ctx show: count=14
_RE_CTX_KV = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([^;]+)")


@dataclass(frozen=True)
class Ack:
    seq: int
    result: str
    ctx: str = ""

    @property
    def ok(self) -> bool:
        return self.result in ("EXIT_OK", "EXIT_IN_PROGRESS")


@dataclass(frozen=True)
class ChassisStatus:
    mode: str
    v: float
    w: float
    hdg_tgt: float
    yaw: float
    gz: float
    wl: float
    wr: float
    imu: int


@dataclass(frozen=True)
class MotorStatus:
    name: str  # "Left" | "Right"
    pos: float
    spd: float
    tgt_spd: float
    tgt_pwm: float
    mode: str
    dz: float


@dataclass(frozen=True)
class ParamEntry:
    """One firmware-registered parameter (from show / export / get)."""

    name: str
    type_name: str = ""  # FLOAT / UINT32 / float / ...
    value: str = ""


def strip_log_tag(line: str) -> Tuple[str, str]:
    """Return (tag, body). tag is empty if no {tag} prefix."""
    s = line.strip()
    m = _RE_TAG.match(s)
    if not m:
        return "", s
    return m.group(1), m.group(2).strip()


def _line_body(line: str) -> str:
    _, body = strip_log_tag(line)
    return body if body else line.strip()


def parse_ack(line: str) -> Optional[Ack]:
    _, body = strip_log_tag(line)
    text = body if body else line.strip()
    # ACK may appear with or without a preceding tag in the same line body
    m = _RE_ACK.search(line) or _RE_ACK.search(text)
    if not m:
        return None
    return Ack(
        seq=int(m.group(1)),
        result=m.group(2),
        ctx=(m.group(3) or ""),
    )


def parse_chassis_status(line: str) -> Optional[ChassisStatus]:
    text = _line_body(line)
    m = _RE_CHASSIS.search(text) or _RE_CHASSIS.search(line)
    if not m:
        return None
    return ChassisStatus(
        mode=m.group(1),
        v=float(m.group(2)),
        w=float(m.group(3)),
        hdg_tgt=float(m.group(4)),
        yaw=float(m.group(5)),
        gz=float(m.group(6)),
        wl=float(m.group(7)),
        wr=float(m.group(8)),
        imu=int(m.group(9)),
    )


def parse_motor_status(line: str) -> Optional[MotorStatus]:
    text = _line_body(line)
    m = _RE_MOTOR.search(text) or _RE_MOTOR.search(line)
    if not m:
        return None
    return MotorStatus(
        name=m.group(1),
        pos=float(m.group(2)),
        spd=float(m.group(3)),
        tgt_spd=float(m.group(4)),
        tgt_pwm=float(m.group(5)),
        mode=m.group(6),
        dz=float(m.group(7)),
    )


def parse_show_param_row(line: str) -> Optional[ParamEntry]:
    """Parse one `show` table data row (Name Type Value)."""
    text = _line_body(line)
    m = _RE_SHOW_ROW.match(text)
    if not m:
        return None
    return ParamEntry(name=m.group(1), type_name=m.group(2), value=m.group(3))


def parse_show_end(line: str) -> Optional[Tuple[int, int]]:
    """Parse `shown=N total=M` end line. Returns (shown, total)."""
    text = _line_body(line)
    m = _RE_SHOW_END.match(text)
    if not m:
        return None
    return int(m.group(1)), int(m.group(2))


def parse_export_kv(line: str) -> Optional[ParamEntry]:
    """Parse one export `name=value` line (skips comments / headers)."""
    text = _line_body(line)
    if not text or text.startswith("#"):
        return None
    m = _RE_EXPORT_KV.match(text)
    if not m:
        return None
    # Avoid matching accidental status-like text without a param-ish name
    name, value = m.group(1), m.group(2).strip()
    if name.lower() in ("seq", "result", "ctx", "mode", "v", "w"):
        return None
    return ParamEntry(name=name, value=value)


def parse_export_end(line: str) -> Optional[int]:
    """Parse `# export count=N`. Returns count or None."""
    text = _line_body(line)
    m = _RE_EXPORT_END.match(text)
    if not m:
        return None
    return int(m.group(1))


def parse_get_terminal(line: str) -> Optional[ParamEntry]:
    """Parse terminal line: Parameter <name> value: <v>."""
    text = _line_body(line)
    m = _RE_GET_TERMINAL.match(text)
    if not m:
        return None
    return ParamEntry(name=m.group(1), value=m.group(2).strip())


def parse_set_terminal(line: str) -> Optional[ParamEntry]:
    """Parse terminal line: Set <name> = <v>."""
    text = _line_body(line)
    m = _RE_SET_TERMINAL.match(text)
    if not m:
        return None
    return ParamEntry(name=m.group(1), value=m.group(2).strip())


def parse_ctx_fields(ctx: str) -> Dict[str, str]:
    """Split ACK ctx `a=1;b=2` into a dict (empty if unusable)."""
    if not ctx or "=" not in ctx:
        return {}
    return {m.group(1): m.group(2) for m in _RE_CTX_KV.finditer(ctx)}


def parse_param_ack_ctx(ctx: str) -> Optional[ParamEntry]:
    """
    Parse get/set ACK ctx into a ParamEntry when name+value present.
    get: name=motor_kp;type=float;value=120.000000
    set: name=motor_kp;updated=1  (no value — returns None unless value given)
    """
    fields = parse_ctx_fields(ctx)
    name = fields.get("name")
    if not name:
        return None
    value = fields.get("value")
    if value is None:
        return None
    return ParamEntry(name=name, type_name=fields.get("type", ""), value=value)


def self_check() -> None:
    """Quick parse smoke test (no hardware)."""
    ack = parse_ack("{terminal}{cmd_ack} seq=12 result=EXIT_OK ctx=mode_set")
    assert ack is not None and ack.seq == 12 and ack.ok and ack.ctx == "mode_set"

    ch = parse_chassis_status(
        "{terminal}chassis: mode=speed v=5.00 w=0.00 hdg_tgt=0.0 "
        "yaw=12.3 gz=-0.1 wl=5.00 wr=5.00 imu=1"
    )
    assert ch is not None and ch.mode == "speed" and ch.v == 5.0 and ch.imu == 1

    m = parse_motor_status(
        "{terminal}Right: pos=10 spd=1.50 tgt_spd=8.00 tgt_pwm=0 mode=speed dz=0"
    )
    assert m is not None and m.name == "Right" and m.spd == 1.5

    tag, body = strip_log_tag("{info}hello")
    assert tag == "info" and body == "hello"

    row = parse_show_param_row(
        "{terminal}motor_kp             FLOAT      120.000000"
    )
    assert row is not None and row.name == "motor_kp" and row.type_name == "FLOAT"
    assert row.value == "120.000000"
    assert parse_show_param_row("{terminal}Name                 Type       Value") is None
    end = parse_show_end("{terminal}shown=14 total=14")
    assert end == (14, 14)

    exp = parse_export_kv("{terminal}chassis_max_v=1.500000")
    assert exp is not None and exp.name == "chassis_max_v" and exp.value == "1.500000"
    assert parse_export_end("{terminal}# export count=12") == 12

    g = parse_get_terminal("{terminal}Parameter motor_ki value: 0.850000")
    assert g is not None and g.name == "motor_ki" and g.value == "0.850000"
    s = parse_set_terminal("{terminal}Set motor_kp = 130.000000")
    assert s is not None and s.name == "motor_kp" and s.value == "130.000000"

    pa = parse_param_ack_ctx("name=motor_kp;type=float;value=120.000000")
    assert pa is not None and pa.name == "motor_kp" and pa.value == "120.000000"
    assert parse_param_ack_ctx("name=motor_kp;updated=1") is None
    assert parse_ctx_fields("count=14").get("count") == "14"

    print("protocol self_check OK")


if __name__ == "__main__":
    self_check()
