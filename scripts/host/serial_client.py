"""Threaded serial client with seq envelope and line dispatch."""

from __future__ import annotations

import queue
import threading
import time
from typing import Callable, Optional

import serial
from serial.tools import list_ports

from host.protocol import (
    Ack,
    ChassisStatus,
    MissionStatus,
    MotorStatus,
    TrackDetail,
    TrackStatus,
    parse_ack,
    parse_chassis_status,
    parse_mission_status,
    parse_motor_status,
    parse_track_detail,
    parse_track_status,
)

DEFAULT_BAUD = 115200
MAX_CMD_LEN = 128
# Bound pending ACK map so UI-only sessions (no wait_ack) do not grow forever.
_MAX_ACK_RESULTS = 64


def list_serial_ports() -> list[str]:
    return [p.device for p in list_ports.comports()]


class SerialClient:
    """Background RX reader; UI should poll queues via after()."""

    def __init__(self) -> None:
        self._ser: Optional[serial.Serial] = None
        self._rx_thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._write_lock = threading.Lock()
        self._seq = 0
        self._seq_lock = threading.Lock()

        self.line_queue: queue.Queue[str] = queue.Queue(maxsize=2000)
        self.ack_queue: queue.Queue[Ack] = queue.Queue(maxsize=500)
        self.chassis_queue: queue.Queue[ChassisStatus] = queue.Queue(maxsize=200)
        self.motor_queue: queue.Queue[MotorStatus] = queue.Queue(maxsize=200)
        self.track_queue: queue.Queue[TrackStatus] = queue.Queue(maxsize=200)
        self.track_detail_queue: queue.Queue[TrackDetail] = queue.Queue(maxsize=200)
        self.mission_queue: queue.Queue[MissionStatus] = queue.Queue(maxsize=200)
        self.error_queue: queue.Queue[str] = queue.Queue(maxsize=50)

        self._ack_events: dict[int, threading.Event] = {}
        self._ack_results: dict[int, Ack] = {}
        self._ack_lock = threading.Lock()

        self.on_line: Optional[Callable[[str], None]] = None

    @property
    def is_open(self) -> bool:
        return self._ser is not None and self._ser.is_open

    @property
    def port(self) -> str:
        if self._ser is None:
            return ""
        return self._ser.port or ""

    def open(self, port: str, baud: int = DEFAULT_BAUD) -> None:
        if self.is_open:
            self.close()
        ser = serial.Serial(port=port, baudrate=baud, timeout=0.05)
        self._ser = ser
        self._stop.clear()
        self._rx_thread = threading.Thread(target=self._rx_loop, name="serial-rx", daemon=True)
        self._rx_thread.start()

    def close(self) -> None:
        self._stop.set()
        thr = self._rx_thread
        if thr is not None and thr.is_alive():
            thr.join(timeout=1.0)
        self._rx_thread = None
        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:
                pass
            self._ser = None
        with self._ack_lock:
            for ev in self._ack_events.values():
                ev.set()
            self._ack_events.clear()
            self._ack_results.clear()

    def _next_seq(self) -> int:
        with self._seq_lock:
            self._seq += 1
            return self._seq

    def send_raw(self, cmd: str, with_seq: bool = False) -> int:
        """Send command. Returns seq used (-1 if no seq)."""
        if not self.is_open or self._ser is None:
            raise RuntimeError("serial not open")

        cmd = cmd.strip()
        if not cmd:
            raise ValueError("empty command")

        seq = -1
        if with_seq:
            seq = self._next_seq()
            line = f"@{seq} {cmd}"
        else:
            line = cmd

        if len(line) > MAX_CMD_LEN:
            raise ValueError(f"command too long ({len(line)} > {MAX_CMD_LEN})")

        payload = (line + "\n").encode("utf-8")
        with self._write_lock:
            self._ser.write(payload)
            self._ser.flush()
        return seq

    def send_seq(self, cmd: str) -> int:
        return self.send_raw(cmd, with_seq=True)

    def wait_ack(self, seq: int, timeout: float = 1.0) -> Optional[Ack]:
        """Wait for ACK of *seq*. Safe if ACK arrives before/after registration."""
        if seq < 0:
            return None
        with self._ack_lock:
            if seq in self._ack_results:
                return self._ack_results.pop(seq)
            ev = threading.Event()
            self._ack_events[seq] = ev
            # ACK may have landed between the check and registering the event.
            if seq in self._ack_results:
                self._ack_events.pop(seq, None)
                return self._ack_results.pop(seq)
        ev.wait(timeout)
        with self._ack_lock:
            self._ack_events.pop(seq, None)
            # Return result if present even when wait timed out (late race).
            return self._ack_results.pop(seq, None)

    def _put(self, q: queue.Queue, item) -> None:
        try:
            q.put_nowait(item)
        except queue.Full:
            try:
                q.get_nowait()
            except queue.Empty:
                pass
            try:
                q.put_nowait(item)
            except queue.Full:
                pass

    def _store_ack(self, ack: Ack) -> None:
        with self._ack_lock:
            self._ack_results[ack.seq] = ack
            while len(self._ack_results) > _MAX_ACK_RESULTS:
                # dict preserves insertion order (3.7+)
                self._ack_results.pop(next(iter(self._ack_results)))
            ev = self._ack_events.get(ack.seq)
            if ev is not None:
                ev.set()

    def _handle_line(self, line: str) -> None:
        line = line.rstrip("\r\n")
        if not line:
            return

        self._put(self.line_queue, line)
        if self.on_line is not None:
            try:
                self.on_line(line)
            except Exception:
                pass

        ack = parse_ack(line)
        if ack is not None:
            self._put(self.ack_queue, ack)
            self._store_ack(ack)

        ch = parse_chassis_status(line)
        if ch is not None:
            self._put(self.chassis_queue, ch)

        m = parse_motor_status(line)
        if m is not None:
            self._put(self.motor_queue, m)

        tr = parse_track_status(line)
        if tr is not None:
            self._put(self.track_queue, tr)

        td = parse_track_detail(line)
        if td is not None:
            self._put(self.track_detail_queue, td)

        mi = parse_mission_status(line)
        if mi is not None:
            self._put(self.mission_queue, mi)

    def _rx_loop(self) -> None:
        buf = bytearray()
        while not self._stop.is_set():
            ser = self._ser
            if ser is None or not ser.is_open:
                break
            try:
                chunk = ser.read(256)
                if not chunk:
                    continue
                buf.extend(chunk)
                while True:
                    nl = buf.find(b"\n")
                    if nl < 0:
                        # guard against garbage flood without newline
                        if len(buf) > 4096:
                            buf.clear()
                        break
                    raw = bytes(buf[:nl])
                    del buf[: nl + 1]
                    try:
                        text = raw.decode("utf-8", errors="replace")
                    except Exception:
                        text = repr(raw)
                    self._handle_line(text)
            except serial.SerialException as e:
                self._put(self.error_queue, f"serial error: {e}")
                break
            except Exception as e:
                self._put(self.error_queue, f"rx error: {e}")
                time.sleep(0.05)
