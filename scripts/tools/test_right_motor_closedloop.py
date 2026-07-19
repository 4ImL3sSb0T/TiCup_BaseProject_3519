#!/usr/bin/env python3
"""Right motor speed closed-loop smoke test via debug UART."""

import re
import serial
import sys
import time

PORT = "COM8"
BAUD = 115200


def drain(ser: serial.Serial, wait: float = 0.12) -> str:
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
    for line in text.splitlines():
        s = line.strip()
        if (
            s.startswith("{terminal}")
            or s.startswith("{cmd_ack}")
            or "Right" in s
            or "Chassis" in s
            or "mode" in s.lower()
        ):
            # filter pure motor_l plots
            if s.startswith("{motor_l}"):
                continue
            print(s)
    return text


def parse_status(text: str):
    m = re.search(
        r"Right:\s*pos=([-\d.]+)\s+spd=([-\d.]+)\s+tgt_spd=([-\d.]+)\s+tgt_pwm=([-\d.]+)\s+mode=(\w+)",
        text,
    )
    if not m:
        return None
    return {
        "pos": float(m.group(1)),
        "spd": float(m.group(2)),
        "tgt_spd": float(m.group(3)),
        "tgt_pwm": float(m.group(4)),
        "mode": m.group(5),
    }


def status(ser: serial.Serial):
    text = send(ser, "motor 0x2 status", wait=0.4)
    return parse_status(text)


def sample_while_running(ser: serial.Serial, seconds: float = 2.5, period: float = 0.35):
    samples = []
    t_end = time.time() + seconds
    while time.time() < t_end:
        st = status(ser)
        if st:
            samples.append(st)
            print(f"  sample: spd={st['spd']:+.2f} tgt={st['tgt_spd']:+.2f} mode={st['mode']}")
        time.sleep(period)
    return samples


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else PORT
    try:
        ser = serial.Serial(port, BAUD, timeout=0.2)
    except Exception as e:
        print(f"Open {port} failed: {e}")
        return 1

    print(f"Opened {port} @ {BAUD}")
    print("Closed-loop speed test: right motor only\n")
    time.sleep(0.2)
    drain(ser, 0.2)

    # Chassis must not fight motor command
    send(ser, "chassis stop", wait=0.4)
    send(ser, "motor 0x1 stop", wait=0.3)  # leave left idle
    send(ser, "motor 0x2 stop", wait=0.3)

    print("\n=== enter speed mode ===")
    send(ser, "motor 0x2 mode speed", wait=0.4)
    base = status(ser)
    print(f"baseline: {base}\n")

    # modest target first
    targets = [5.0, -5.0, 8.0]
    results = []

    for tgt in targets:
        print(f"=== target speed = {tgt:+.1f} counts/2ms ===")
        send(ser, f"motor 0x2 set {tgt}", wait=0.35)
        samples = sample_while_running(ser, seconds=2.2, period=0.4)
        send(ser, "motor 0x2 stop", wait=0.35)
        time.sleep(0.4)

        if not samples:
            print("  FAIL: no status samples")
            results.append((tgt, None))
            continue

        # ignore first sample (startup), use last half
        half = samples[len(samples) // 2 :]
        avg_spd = sum(s["spd"] for s in half) / len(half)
        err = avg_spd - tgt
        ok = abs(err) < max(1.5, abs(tgt) * 0.35) and (avg_spd * tgt > 0 or abs(tgt) < 0.5)
        results.append((tgt, avg_spd, err, ok))
        print(f"  steady avg spd={avg_spd:+.2f}  err={err:+.2f}  => {'PASS' if ok else 'WEAK/FAIL'}\n")

    print("=== final stop / status ===")
    send(ser, "motor 0x2 stop", wait=0.3)
    final = status(ser)
    print(f"final: {final}\n")

    print("======== CLOSED-LOOP SUMMARY ========")
    all_ok = True
    for r in results:
        if r[1] is None:
            print(f"tgt {r[0]:+.1f}: no data")
            all_ok = False
            continue
        tgt, avg, err, ok = r
        print(f"tgt {tgt:+.1f}  avg_spd {avg:+.2f}  err {err:+.2f}  {'OK' if ok else 'CHECK'}")
        all_ok = all_ok and ok

    if all_ok:
        print("\nResult: closed-loop speed tracking looks usable.")
    else:
        print("\nResult: tracking weak — check encoder wiring, PID (motor_kp/ki), or load.")

    ser.close()
    print("Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
