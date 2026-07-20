#!/usr/bin/env python3
"""Sweep motor_kp on left motor speed loop via COM serial."""

import re
import serial
import time

PORT = "COM8"
BAUD = 921600
TARGET = 8.0
SETTLE_S = 3.2
SAMPLE_DT = 0.4
KP_LIST = [80, 100, 120, 150, 180, 220, 280]
RESTORE_KP = 120.0


def main() -> None:
    ser = serial.Serial(PORT, BAUD, timeout=0.2, write_timeout=1)
    time.sleep(0.15)
    ser.reset_input_buffer()

    def xfer(cmd: str, wait: float = 0.5) -> str:
        ser.write((cmd + "\n").encode("ascii"))
        ser.flush()
        t_end = time.time() + wait
        buf = b""
        while time.time() < t_end:
            n = ser.in_waiting
            if n:
                buf += ser.read(n)
            else:
                time.sleep(0.01)
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
        return buf.decode("utf-8", errors="replace")

    def parse_status(text: str):
        m = re.search(
            r"Left:\s*pos=(-?\d+)\s+spd=(-?[\d.]+)\s+tgt_spd=(-?[\d.]+)", text
        )
        if not m:
            return None
        return {
            "pos": int(m.group(1)),
            "spd": float(m.group(2)),
            "tgt": float(m.group(3)),
        }

    def ensure_stop() -> None:
        xfer("motor 0x1 stop", 0.4)
        time.sleep(0.25)

    print("=== baseline ===")
    print(xfer("get motor_kp", 0.5).strip())
    print(xfer("get motor_ki", 0.5).strip())
    print(xfer("motor 0x1 mode speed", 0.4).strip())
    ensure_stop()

    results = []

    for kp in KP_LIST:
        print(f"\n========== KP = {kp} ==========")
        t = xfer(f"set motor_kp {kp}", 0.5)
        for ln in t.splitlines():
            if "Set" in ln or "cmd_ack" in ln:
                print(" ", ln)
        t = xfer("motor 0x1 param", 0.5)
        for ln in t.splitlines():
            if "PID" in ln or "cmd_ack" in ln:
                print(" ", ln)

        ensure_stop()
        time.sleep(0.2)

        xfer(f"motor 0x1 set {TARGET}", 0.35)
        samples = []
        n = int(SETTLE_S / SAMPLE_DT)
        for i in range(n):
            time.sleep(SAMPLE_DT)
            st = parse_status(xfer("motor 0x1 status", 0.4))
            t_s = (i + 1) * SAMPLE_DT
            if st:
                samples.append((t_s, st["spd"], st["pos"]))
                print(f"  t={t_s:.1f}s  spd={st['spd']:6.2f}  pos={st['pos']}")
            else:
                print(f"  t={t_s:.1f}s  (parse fail)")

        ensure_stop()
        time.sleep(0.3)

        if not samples:
            print("  -> no samples")
            continue

        last = samples[-3:] if len(samples) >= 3 else samples
        avg = sum(s[1] for s in last) / len(last)
        thr = 0.9 * abs(TARGET)
        rise = None
        for t_s, spd, _ in samples:
            if abs(spd) >= thr:
                rise = t_s
                break
        peak = max(samples, key=lambda x: abs(x[1]))
        err = avg - TARGET
        row = {
            "kp": kp,
            "avg_spd": avg,
            "err": err,
            "err_pct": 100.0 * err / TARGET,
            "rise_0p9": rise,
            "peak_spd": peak[1],
        }
        results.append(row)
        rise_s = f"{rise:.1f}" if rise is not None else "N/A"
        print(
            f"  -> steady~{avg:.2f}  err={err:+.2f} ({row['err_pct']:+.1f}%)  "
            f"rise90={rise_s}  peak={peak[1]:.2f}"
        )

    print(f"\n=== restore kp={RESTORE_KP} + stop ===")
    xfer(f"set motor_kp {RESTORE_KP}", 0.4)
    xfer("motor 0x1 param", 0.4)
    ensure_stop()
    print(xfer("get motor_kp", 0.4).strip())
    print(xfer("motor 0x1 status", 0.4).strip())

    print(f"\n======== SUMMARY (target={TARGET:.1f}, ki unchanged) ========")
    print(f"{'kp':>6}  {'avg_spd':>8}  {'err':>7}  {'err%':>7}  {'rise90':>7}  {'peak':>7}")
    for r in results:
        rise_s = f"{r['rise_0p9']:.1f}" if r["rise_0p9"] is not None else "N/A"
        print(
            f"{r['kp']:6.0f}  {r['avg_spd']:8.2f}  {r['err']:+7.2f}  "
            f"{r['err_pct']:+6.1f}%  {rise_s:>7}  {r['peak_spd']:7.2f}"
        )

    ser.close()
    print("\ndone")


if __name__ == "__main__":
    main()
