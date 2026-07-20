#!/usr/bin/env python3
"""Sweep motor_ki with fixed motor_kp; left motor speed step response."""

import re
import serial
import time

PORT = "COM8"
BAUD = 115200
KP_FIXED = 150.0
KI_LIST = [200.0, 400.0, 600.0]
TARGET = 8.0
SETTLE_S = 4.0
SAMPLE_DT = 0.25
RESTORE_KP = 120.0
RESTORE_KI = 50.0  # last known user value; will overwrite if get succeeds first


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

    def ensure_stop() -> None:
        xfer("motor 0x1 stop", 0.4)
        time.sleep(0.3)

    def apply_pid(kp: float, ki: float) -> None:
        xfer(f"set motor_kp {kp}", 0.4)
        xfer(f"set motor_ki {ki}", 0.4)
        # stop resets integrator; then apply params
        ensure_stop()
        t = xfer("motor 0x1 param", 0.45)
        for ln in t.splitlines():
            if "PID" in ln or "cmd_ack" in ln:
                print(" ", ln)

    def run_step(target: float):
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
        return samples

    def metrics(samples, target: float):
        if not samples:
            return None
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
        return {
            "avg": avg,
            "err": avg - target,
            "err_pct": 100.0 * (avg - target) / target,
            "t63": t63,
            "t90": t90,
            "peak": peak[1],
            "overshoot": overshoot,
        }

    kp0 = get_param("motor_kp")
    ki0 = get_param("motor_ki")
    print(f"baseline on board: kp={kp0} ki={ki0}")
    restore_kp = kp0 if kp0 is not None else RESTORE_KP
    restore_ki = ki0 if ki0 is not None else RESTORE_KI

    xfer("motor 0x1 mode speed", 0.4)
    ensure_stop()

    rows = []
    for ki in KI_LIST:
        print(f"\n========== kp={KP_FIXED}  ki={ki} ==========")
        apply_pid(KP_FIXED, ki)

        print(f"--- step +{TARGET} ---")
        fwd = run_step(TARGET)
        mf = metrics(fwd, TARGET)
        if mf:
            print(
                f"  -> steady={mf['avg']:.2f} err={mf['err']:+.2f} "
                f"({mf['err_pct']:+.1f}%) t63={mf['t63']} t90={mf['t90']} "
                f"peak={mf['peak']:.2f} os={mf['overshoot']:.2f}"
            )

        print(f"--- step -{TARGET} ---")
        rev = run_step(-TARGET)
        mr = metrics(rev, -TARGET)
        if mr:
            print(
                f"  -> steady={mr['avg']:.2f} err={mr['err']:+.2f} "
                f"({mr['err_pct']:+.1f}%) t63={mr['t63']} t90={mr['t90']} "
                f"peak={mr['peak']:.2f} os={mr['overshoot']:.2f}"
            )

        ensure_stop()
        rows.append({"ki": ki, "fwd": mf, "rev": mr})

    print(f"\n=== restore kp={restore_kp} ki={restore_ki} ===")
    apply_pid(restore_kp, restore_ki)
    st = parse_left(xfer("motor 0x1 status", 0.4))
    if st:
        print(f"stop: spd={st[1]:.2f}")

    print(f"\n======== SUMMARY (kp={KP_FIXED}, target=±{TARGET}) ========")
    print(
        f"{'ki':>6}  {'+avg':>7}  {'+err%':>7}  {'+t90':>6}  "
        f"{'-avg':>7}  {'-err%':>7}  {'-t90':>6}"
    )
    for r in rows:
        f, v = r["fwd"], r["rev"]
        if not f or not v:
            continue
        ft = f"{f['t90']:.2f}" if f["t90"] is not None else "N/A"
        rt = f"{v['t90']:.2f}" if v["t90"] is not None else "N/A"
        print(
            f"{r['ki']:6.0f}  {f['avg']:7.2f}  {f['err_pct']:+6.1f}%  {ft:>6}  "
            f"{v['avg']:7.2f}  {v['err_pct']:+6.1f}%  {rt:>6}"
        )

    ser.close()
    print("\ndone")


if __name__ == "__main__":
    main()
