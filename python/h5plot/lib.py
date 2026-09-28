# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""ctypes loader for h5plot.dll. The numpy buffer must stay alive."""

from __future__ import annotations

import ctypes
from ctypes import POINTER, c_double, c_int, c_longlong, c_void_p
from pathlib import Path


class H5PlotRun(ctypes.Structure):
    _fields_ = [
        ("first", c_int),
        ("count", c_int),
        ("red", ctypes.c_ubyte),
        ("green", ctypes.c_ubyte),
        ("blue", ctypes.c_ubyte),
        ("alpha", ctypes.c_ubyte),
        ("width", ctypes.c_float),
    ]


class H5PlotTick(ctypes.Structure):
    _fields_ = [
        ("x", c_double),
        ("y", c_double),
        ("value", c_double),
        ("axis", c_int),
    ]


# The same cycle LineStore uses when a line brings no colour of its own.
_CYCLE = (
    (230, 159, 0),
    (86, 180, 233),
    (0, 158, 115),
    (240, 228, 66),
    (0, 114, 178),
    (213, 94, 0),
    (204, 121, 167),
)


def _load() -> ctypes.CDLL:
    here = Path(__file__).resolve().parent
    for name in ("h5plot.dll", "libh5plot.dll", "libh5plot.so"):
        path = here / name
        if path.exists():
            return ctypes.CDLL(str(path))
    raise FileNotFoundError(
        f"h5plot.dll not next to {here}. Run python/h5plot/build.bat with TDM GCC."
    )


_lib = _load()
_lib.h5plot_create.restype = c_void_p
_lib.h5plot_destroy.argtypes = [c_void_p]
_lib.h5plot_add_line.argtypes = [c_void_p, ctypes.POINTER(c_double), c_longlong, c_int, c_int, c_int]
_lib.h5plot_add_line.restype = c_int
_lib.h5plot_clear.argtypes = [c_void_p]
_lib.h5plot_set_pane.argtypes = [c_void_p, c_int, c_int, c_double]
_lib.h5plot_set_ylog.argtypes = [c_void_p, c_int]
_lib.h5plot_reset_view.argtypes = [c_void_p]
_lib.h5plot_wheel.argtypes = [c_void_p, c_double, c_double, c_double, c_int, c_int]
_lib.h5plot_pan.argtypes = [c_void_p, c_double, c_double]
_lib.h5plot_zoom_rect.argtypes = [c_void_p, c_double, c_double, c_double, c_double]
_lib.h5plot_zoom_rect.restype = c_int
_lib.h5plot_view_min_x.argtypes = [c_void_p]
_lib.h5plot_view_min_x.restype = c_double
_lib.h5plot_view_max_x.argtypes = [c_void_p]
_lib.h5plot_view_max_x.restype = c_double
_lib.h5plot_view_min_y.argtypes = [c_void_p]
_lib.h5plot_view_min_y.restype = c_double
_lib.h5plot_view_max_y.argtypes = [c_void_p]
_lib.h5plot_view_max_y.restype = c_double
_lib.h5plot_project.argtypes = [c_void_p]
_lib.h5plot_project.restype = c_int
_lib.h5plot_point_count.argtypes = [c_void_p]
_lib.h5plot_point_count.restype = c_int
_lib.h5plot_copy_points.argtypes = [c_void_p, POINTER(c_double)]
_lib.h5plot_run_count.argtypes = [c_void_p]
_lib.h5plot_run_count.restype = c_int
_lib.h5plot_copy_runs.argtypes = [c_void_p, POINTER(H5PlotRun)]
_lib.h5plot_tick_count.argtypes = [c_void_p]
_lib.h5plot_tick_count.restype = c_int
_lib.h5plot_copy_ticks.argtypes = [c_void_p, POINTER(H5PlotTick)]


class Plot:
    def __init__(self) -> None:
        self._handle = _lib.h5plot_create()
        if not self._handle:
            raise RuntimeError("h5plot_create failed")
        self._keep: list = []

    def close(self) -> None:
        if self._handle:
            _lib.h5plot_destroy(self._handle)
            self._handle = None
        self._keep.clear()

    def __del__(self) -> None:
        self.close()

    def add_line(self, y, colour=None) -> int:
        import numpy as np

        if colour is None:
            colour = _CYCLE[len(self._keep) % len(_CYCLE)]
        array = np.ascontiguousarray(y, dtype=np.float64)
        self._keep.append(array)
        ptr = array.ctypes.data_as(POINTER(c_double))
        return int(_lib.h5plot_add_line(self._handle, ptr, array.size, colour[0], colour[1], colour[2]))

    def clear(self) -> None:
        _lib.h5plot_clear(self._handle)
        self._keep.clear()

    def set_pane(self, width: int, height: int, pixel_ratio: float = 1.0) -> None:
        _lib.h5plot_set_pane(self._handle, int(width), int(height), float(pixel_ratio))

    def set_y_log(self, on: bool) -> None:
        _lib.h5plot_set_ylog(self._handle, 1 if on else 0)

    def reset_view(self) -> None:
        _lib.h5plot_reset_view(self._handle)

    def wheel(self, px: float, py: float, factor: float, shift: bool, control: bool) -> None:
        _lib.h5plot_wheel(self._handle, px, py, factor, 1 if shift else 0, 1 if control else 0)

    def pan(self, dx: float, dy: float) -> None:
        _lib.h5plot_pan(self._handle, dx, dy)

    def zoom_rect(self, x0: float, y0: float, x1: float, y1: float) -> bool:
        return bool(_lib.h5plot_zoom_rect(self._handle, x0, y0, x1, y1))

    def view_min_x(self) -> float:
        return float(_lib.h5plot_view_min_x(self._handle))

    def view_max_x(self) -> float:
        return float(_lib.h5plot_view_max_x(self._handle))

    def view_min_y(self) -> float:
        return float(_lib.h5plot_view_min_y(self._handle))

    def view_max_y(self) -> float:
        return float(_lib.h5plot_view_max_y(self._handle))

    def project(self):
        n = int(_lib.h5plot_project(self._handle))
        import numpy as np

        xy = np.empty(n * 2, dtype=np.float64)
        if n:
            _lib.h5plot_copy_points(self._handle, xy.ctypes.data_as(POINTER(c_double)))
        runs_n = int(_lib.h5plot_run_count(self._handle))
        runs = (H5PlotRun * max(runs_n, 1))()
        if runs_n:
            _lib.h5plot_copy_runs(self._handle, runs)
        tick_n = int(_lib.h5plot_tick_count(self._handle))
        ticks = (H5PlotTick * max(tick_n, 1))()
        if tick_n:
            _lib.h5plot_copy_ticks(self._handle, ticks)
        points = xy.reshape(n, 2) if n else xy.reshape(0, 2)
        return points, list(runs[:runs_n]), list(ticks[:tick_n])
