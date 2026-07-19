"""Parse firmware serial protocol lines (cmd_ack / status / log tags)."""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Optional, Tuple

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


def strip_log_tag(line: str) -> Tuple[str, str]:
    """Return (tag, body). tag is empty if no {tag} prefix."""
    s = line.strip()
    m = _RE_TAG.match(s)
    if not m:
        return "", s
    return m.group(1), m.group(2).strip()


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
    _, body = strip_log_tag(line)
    text = body if body else line.strip()
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
    _, body = strip_log_tag(line)
    text = body if body else line.strip()
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
    print("protocol self_check OK")


if __name__ == "__main__":
    self_check()
