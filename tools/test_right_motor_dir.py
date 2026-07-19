#!/usr/bin/env python3
"""Right motor direction check via debug UART."""

import re
import serial
import sys
import time

PORT = "COM8"
BAUD = 115200


def drain(ser: serial.Serial, wait: float = 0.15) -> str:
    time.sleep(wait)
    data = b""
    t0 = time.time()
    while time.time() - t0 < wait:
        n = ser.in_waiting
        if n:
            data += ser.read(n)
        time.sleep(0.02)
    return data.decode("utf-8", errors="replace")


def send(ser: serial.Serial, cmd: str, wait: float = 0.35) -> str:
    print(f">>> {cmd}")
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode("utf-8"))
    ser.flush()
    text = drain(ser, wait)
    # keep only terminal/ack lines for readability
    lines = []
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("{terminal}") or s.startswith("{cmd_ack}") or "Right" in s:
            lines.append(s)
        elif s.startswith("{cmd_ack}"):
            lines.append(s)
    for s in lines:
        print(s)
    return text


def parse_status(text: str):
    # Right: pos=-3957 spd=-3.33 tgt_spd=0.00 tgt_pwm=1500 mode=OpenLoop dz=0
    m = re.search(
        r"Right:\s*pos=([-\d.]+)\s+spd=([-\d.]+)\s+tgt_spd=([-\d.]+)\s+tgt_pwm=([-\d.]+)",
        text,
    )
    if not m:
        return None
    return {
        "pos": float(m.group(1)),
        "spd": float(m.group(2)),
        "tgt_spd": float(m.group(3)),
        "tgt_pwm": float(m.group(4)),
    }


def status(ser: serial.Serial):
    text = send(ser, "motor 0x2 status", wait=0.4)
    return parse_status(text)


def run_phase(ser: serial.Serial, duty: int, seconds: float = 2.0):
    send(ser, f"motor 0x2 set {duty}", wait=0.3)
    # sample mid-run
    time.sleep(0.6)
    mid = status(ser)
    time.sleep(max(0.0, seconds - 0.6))
    end = status(ser)
    return mid, end


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else PORT
    try:
        ser = serial.Serial(port, BAUD, timeout=0.2)
    except Exception as e:
        print(f"Open {port} failed: {e}")
        return 1

    print(f"Opened {port} @ {BAUD}")
    print("Note: continuous {motor_l} plot spam is filtered out.\n")
    time.sleep(0.2)
    drain(ser, 0.2)

    send(ser, "motor 0x2 mode openloop", wait=0.35)
    send(ser, "motor 0x2 stop", wait=0.3)
    time.sleep(0.3)

    print("=== baseline (stopped) ===")
    base = status(ser)
    print(f"parsed: {base}\n")

    print("=== phase A: duty = +2000 (~2s) ===")
    mid_a, end_a = run_phase(ser, 2000, 2.0)
    print(f"mid: {mid_a}")
    print(f"end: {end_a}\n")

    send(ser, "motor 0x2 stop", wait=0.3)
    time.sleep(0.5)

    print("=== phase B: duty = -2000 (~2s) ===")
    mid_b, end_b = run_phase(ser, -2000, 2.0)
    print(f"mid: {mid_b}")
    print(f"end: {end_b}\n")

    send(ser, "motor 0x2 stop", wait=0.3)
    time.sleep(0.4)
    print("=== after stop ===")
    final = status(ser)
    print(f"parsed: {final}\n")

    # Direction summary
    print("======== DIRECTION SUMMARY ========")
    if mid_a and mid_b and base:
        dpos_a = end_a["pos"] - base["pos"] if end_a else None
        dpos_b = end_b["pos"] - (end_a["pos"] if end_a else base["pos"]) if end_b else None
        print(f"duty +2000  ->  mid spd={mid_a['spd']:+.2f}  end pos={end_a['pos']:.0f}  dpos≈{dpos_a:+.0f}")
        print(f"duty -2000  ->  mid spd={mid_b['spd']:+.2f}  end pos={end_b['pos']:.0f}  dpos≈{dpos_b:+.0f}")
        print()
        if mid_a["spd"] * mid_b["spd"] < 0:
            print("OK: +duty and -duty produce opposite encoder speed signs.")
        else:
            print("WARN: +duty and -duty did NOT produce opposite speed signs.")
        if mid_a["spd"] > 0:
            print("Convention: positive duty  =>  positive encoder speed.")
        elif mid_a["spd"] < 0:
            print("Convention: positive duty  =>  negative encoder speed (signs inverted).")
            print("If physical 'forward' should match +duty, flip motor dir_reverse or encoder polarity.")
        else:
            print("WARN: +duty produced ~0 speed (motor not moving / not connected / duty too low).")
    else:
        print("Could not parse enough status lines.")

    ser.close()
    print("\nDone.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
