#!/usr/bin/env python3
"""Quick right-motor serial smoke test (UART0 debug @ 921600)."""

import serial
import sys
import time

PORT = "COM8"
BAUD = 921600


def send_cmd(ser: serial.Serial, cmd: str, wait: float = 0.4) -> str:
    line = (cmd + "\n").encode("utf-8")
    print(f">>> {cmd}")
    ser.reset_input_buffer()
    ser.write(line)
    ser.flush()
    time.sleep(wait)
    data = ser.read(ser.in_waiting or 0)
    t0 = time.time()
    while time.time() - t0 < wait:
        n = ser.in_waiting
        if n:
            data += ser.read(n)
        time.sleep(0.05)
    text = data.decode("utf-8", errors="replace")
    if text.strip():
        print(text.rstrip())
    else:
        print("(no reply)")
    print("---")
    return text


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else PORT
    try:
        ser = serial.Serial(port, BAUD, timeout=0.2)
    except Exception as e:
        print(f"Open {port} failed: {e}")
        return 1

    print(f"Opened {port} @ {BAUD}")
    time.sleep(0.3)
    if ser.in_waiting:
        boot = ser.read(ser.in_waiting).decode("utf-8", errors="replace")
        print("[boot/pending]\n" + boot.rstrip() + "\n---")

    send_cmd(ser, "help", wait=0.5)
    send_cmd(ser, "motor 0x2 status", wait=0.5)

    send_cmd(ser, "motor 0x2 mode openloop", wait=0.4)
    send_cmd(ser, "motor 0x2 set 1500", wait=0.4)
    print("*** Right motor openloop duty=1500 for ~2s ***")
    time.sleep(2.0)
    if ser.in_waiting:
        print(ser.read(ser.in_waiting).decode("utf-8", errors="replace").rstrip())

    send_cmd(ser, "motor 0x2 status", wait=0.5)

    send_cmd(ser, "motor 0x2 set -1500", wait=0.4)
    print("*** Right motor reverse duty=-1500 for ~2s ***")
    time.sleep(2.0)

    send_cmd(ser, "motor 0x2 stop", wait=0.5)
    send_cmd(ser, "motor 0x2 status", wait=0.5)

    ser.close()
    print("Done. Port closed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
