#!/usr/bin/env python3
"""Left motor speed step response with current motor_kp/ki."""

import re
import serial
import time

PORT = "COM8"
BAUD = 115200
TARGET = 8.0
SETTLE_S = 4.0
SAMPLE_DT = 0.25


def main() -> None:
    ser = serial.Serial(PORT, BAUD, timeout=0.2, write_timeout=1)
    time.sleep(0.12)
    ser.reset_input_buffer()

    def xfer(cmd: str, wait: float = 0.45) -> str:
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

    def get_param(name: str):
        text = xfer("get " + name, 0.5)
        m = re.search(r"value:\s*([-\d.]+)", text)
        return float(m.group(1)) if m else None

    def parse_left(text: str):
        m = re.search(
            r"Left:\s*pos=(-?\d+)\s+spd=(-?[\d.]+)\s+tgt_spd=(-?[\d.]+)", text
        )
        if not m:
            return None
        return int(m.group(1)), float(m.group(2)), float(m.group(3))

    kp = get_param("motor_kp")
    ki = get_param("motor_ki")
    print(f"params: motor_kp={kp}  motor_ki={ki}")

    xfer("motor 0x1 mode speed", 0.4)
    xfer("motor 0x1 stop", 0.4)
    time.sleep(0.3)
    # ensure RAM PID matches params
    print(xfer("motor 0x1 param", 0.45).strip())

    def run_step(target: float):
        print(f"\n=== step {target:+.1f} ===")
        xfer(f"motor 0x1 set {target}", 0.35)
        samples = []
        n = int(SETTLE_S / SAMPLE_DT)
        for i in range(n):
            time.sleep(SAMPLE_DT)
            st = parse_left(xfer("motor 0x1 status", 0.35))
            t = (i + 1) * SAMPLE_DT
            if st:
                samples.append((t, st[1], st[0]))
                print(f"  t={t:4.2f}s  spd={st[1]:7.2f}  pos={st[0]}")
            else:
                print(f"  t={t:4.2f}s  (parse fail)")
        return samples

    def metrics(samples, target: float) -> None:
        if not samples:
            print("  no samples")
            return
        last = samples[-4:] if len(samples) >= 4 else samples
        avg = sum(s[1] for s in last) / len(last)
        thr63 = 0.63 * abs(target)
        thr90 = 0.90 * abs(target)
        t63 = t90 = None
        for t, spd, _ in samples:
            if t63 is None and abs(spd) >= thr63:
                t63 = t
            if t90 is None and abs(spd) >= thr90:
                t90 = t
        peak = max(samples, key=lambda x: abs(x[1]))
        if target >= 0:
            overshoot = max(0.0, peak[1] - target)
        else:
            overshoot = max(0.0, (-peak[1]) - abs(target))
        err = avg - target
        print(
            f"  steady_avg={avg:.2f}  err={err:+.2f} "
            f"({100.0 * err / target:+.1f}%)"
        )
        print(
            f"  t63={t63}  t90={t90}  peak={peak[1]:.2f}  "
            f"overshoot~{overshoot:.2f}"
        )

    fwd = run_step(TARGET)
    rev = run_step(-TARGET)

    xfer("motor 0x1 stop", 0.4)
    time.sleep(0.25)
    st = parse_left(xfer("motor 0x1 status", 0.35))
    if st:
        print(f"\nstop: spd={st[1]:.2f} pos={st[0]}")
    else:
        print("\nstop: status parse fail")

    print("\n--- metrics +target ---")
    metrics(fwd, TARGET)
    print("--- metrics -target ---")
    metrics(rev, -TARGET)

    ser.close()
    print("\ndone")


if __name__ == "__main__":
    main()
