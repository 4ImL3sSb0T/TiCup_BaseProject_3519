"""Matplotlib telemetry strip chart for the drive tab."""

from __future__ import annotations

from collections import deque
from typing import Deque

from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

PLOT_MAX_POINTS = 120


class TelemetryPlot:
    """Rolling multi-series plot (v / wheel / motor spd)."""

    def __init__(self, master, max_points: int = PLOT_MAX_POINTS) -> None:
        self.max_points = max_points
        self.t: Deque[float] = deque(maxlen=max_points)
        self.v: Deque[float] = deque(maxlen=max_points)
        self.wl: Deque[float] = deque(maxlen=max_points)
        self.wr: Deque[float] = deque(maxlen=max_points)
        self.l_spd: Deque[float] = deque(maxlen=max_points)
        self.r_spd: Deque[float] = deque(maxlen=max_points)

        self._fig = Figure(figsize=(5, 2.6), dpi=100, facecolor="#1a1a1a")
        self._ax = self._fig.add_subplot(111)
        self._ax.set_facecolor("#1a1a1a")
        self._ax.tick_params(colors="#bbbbbb", labelsize=8)
        for spine in self._ax.spines.values():
            spine.set_color("#555555")
        self._ax.set_ylabel("value", color="#bbbbbb", fontsize=8)
        self._ax.set_xlabel("t (s)", color="#bbbbbb", fontsize=8)
        (self._line_v,) = self._ax.plot([], [], label="v", color="#3498db", lw=1.4)
        (self._line_wl,) = self._ax.plot([], [], label="wl", color="#2ecc71", lw=1.2)
        (self._line_wr,) = self._ax.plot([], [], label="wr", color="#e67e22", lw=1.2)
        (self._line_ls,) = self._ax.plot([], [], label="L", color="#9b59b6", lw=1.0, ls="--")
        (self._line_rs,) = self._ax.plot([], [], label="R", color="#e74c3c", lw=1.0, ls="--")
        self._ax.legend(loc="upper right", fontsize=7, facecolor="#2b2b2b", labelcolor="#dddddd")
        self._fig.tight_layout()
        self._canvas = FigureCanvasTkAgg(self._fig, master=master)
        self.widget = self._canvas.get_tk_widget()

    def append(
        self,
        t: float,
        v: float,
        wl: float,
        wr: float,
        l_spd: float,
        r_spd: float,
    ) -> None:
        self.t.append(t)
        self.v.append(v)
        self.wl.append(wl)
        self.wr.append(wr)
        self.l_spd.append(l_spd)
        self.r_spd.append(r_spd)

    def redraw(
        self,
        show_v: bool = True,
        show_wl: bool = True,
        show_wr: bool = True,
        show_l: bool = False,
        show_r: bool = False,
    ) -> None:
        if not self.t:
            return
        xs = list(self.t)

        def set_line(line, enabled: bool, ys: Deque[float]) -> None:
            if enabled:
                line.set_data(xs, list(ys))
            else:
                line.set_data([], [])

        set_line(self._line_v, show_v, self.v)
        set_line(self._line_wl, show_wl, self.wl)
        set_line(self._line_wr, show_wr, self.wr)
        set_line(self._line_ls, show_l, self.l_spd)
        set_line(self._line_rs, show_r, self.r_spd)
        self._ax.relim()
        self._ax.autoscale_view()
        self._canvas.draw_idle()
