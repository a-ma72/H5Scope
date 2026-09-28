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


class H5PlotSample(ctypes.Structure):
    _fields_ = [
        ("line", c_int),
        ("x", c_double),
        ("y", c_double),
        ("px", c_double),
        ("py", c_double),
        ("red", ctypes.c_ubyte),
        ("green", ctypes.c_ubyte),
        ("blue", ctypes.c_ubyte),
        ("alpha", ctypes.c_ubyte),
    ]


class H5PlotTick(ctypes.Structure):
    _fields_ = [
        ("x", c_double),
        ("y", c_double),
        ("value", c_double),
        ("axis", c_int),
        ("series", c_int),
        ("red", ctypes.c_ubyte),
        ("green", ctypes.c_ubyte),
        ("blue", ctypes.c_ubyte),
        ("alpha", ctypes.c_ubyte),
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
_lib.h5plot_begin_line.argtypes = [c_void_p, c_longlong, c_int, c_int, c_int]
_lib.h5plot_begin_line.restype = c_int
_lib.h5plot_add_samples.argtypes = [c_void_p, c_int, ctypes.POINTER(c_double), c_longlong]
_lib.h5plot_finish_line.argtypes = [c_void_p, c_int]
_READ = ctypes.CFUNCTYPE(c_int, c_void_p, c_longlong, c_longlong, POINTER(c_double))
_lib.h5plot_set_reader.argtypes = [c_void_p, c_int, _READ, c_void_p]
_lib.h5plot_set_axis.argtypes = [c_void_p, POINTER(c_double), c_longlong]
_lib.h5plot_begin_axis.argtypes = [c_void_p, c_longlong]
_lib.h5plot_add_axis_samples.argtypes = [c_void_p, POINTER(c_double), c_longlong]
_lib.h5plot_finish_axis.argtypes = [c_void_p]
_lib.h5plot_set_axis_reader.argtypes = [c_void_p, _READ, c_void_p]
_lib.h5plot_clear.argtypes = [c_void_p]
_lib.h5plot_set_pane.argtypes = [c_void_p, c_int, c_int, c_double]
_lib.h5plot_set_ylog.argtypes = [c_void_p, c_int]
_lib.h5plot_reset_view.argtypes = [c_void_p]
_lib.h5plot_wheel.argtypes = [c_void_p, c_double, c_double, c_double, c_int, c_int]
_lib.h5plot_pan.argtypes = [c_void_p, c_double, c_double]
_lib.h5plot_zoom_rect.argtypes = [c_void_p, c_double, c_double, c_double, c_double]
_lib.h5plot_zoom_rect.restype = c_int
_lib.h5plot_set_own_axis.argtypes = [c_void_p, c_int, c_int]
_lib.h5plot_line_count.argtypes = [c_void_p]
_lib.h5plot_line_count.restype = c_int
_lib.h5plot_own_axis.argtypes = [c_void_p, c_int]
_lib.h5plot_own_axis.restype = c_int
_lib.h5plot_own_count.argtypes = [c_void_p]
_lib.h5plot_own_count.restype = c_int
_lib.h5plot_shared_count.argtypes = [c_void_p]
_lib.h5plot_shared_count.restype = c_int
_lib.h5plot_nearest.argtypes = [c_void_p, c_double, c_double]
_lib.h5plot_nearest.restype = c_int
_lib.h5plot_sample.argtypes = [c_void_p, c_double, c_double, POINTER(H5PlotSample)]
_lib.h5plot_sample.restype = c_int
_lib.h5plot_shift_line.argtypes = [c_void_p, c_int, c_double, c_double]
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
        self._x = None
        self._sources: dict = {}
        self._tokens: dict = {}
        self._read_cb = _READ(self._read_window)

    def close(self) -> None:
        if self._handle:
            _lib.h5plot_destroy(self._handle)
            self._handle = None
        for handle, _data in self._sources.values():
            handle.close()
        self._sources.clear()
        self._tokens.clear()
        self._keep.clear()
        self._x = None

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

    def set_x(self, x) -> None:
        """Borrow one x per sample. None puts the sample index back.

        The array has to go one way. A range of x is then a range of
        positions, and a zoom reads that run. One that doubles back is drawn,
        and a zoom stretches the summary.
        """
        import numpy as np

        if x is None:
            self._drop_x_file()
            self._x = None
            _lib.h5plot_set_axis(self._handle, None, 0)
            return
        self._drop_x_file()
        array = np.ascontiguousarray(x, dtype=np.float64)
        self._x = array
        _lib.h5plot_set_axis(self._handle, array.ctypes.data_as(POINTER(c_double)), array.size)

    def set_x_hdf5(self, path, dataset: str) -> None:
        """Stream a 1-D numeric dataset in as the shared x.

        Folded in the same pieces as a curve, then the chunk is dropped.
        The file stays open so a closer look can read that window back.
        The values have to go one way, as with `set_x`.
        """
        import h5py
        import numpy as np

        self._drop_x_file()
        self._x = None
        handle = h5py.File(path, "r")
        try:
            data = handle[dataset]
            if getattr(data, "ndim", None) != 1 or not np.issubdtype(data.dtype, np.number):
                raise ValueError(f"{dataset} is not a 1-D numeric dataset")
            count = int(data.shape[0])
            if count < 2:
                raise ValueError(f"{dataset} has fewer than two samples")
            _lib.h5plot_begin_axis(self._handle, count)
            step = 1 << 16
            for start in range(0, count, step):
                block = np.ascontiguousarray(data[start:start + step], dtype=np.float64)
                _lib.h5plot_add_axis_samples(self._handle, block.ctypes.data_as(POINTER(c_double)),
                                             int(block.size))
            _lib.h5plot_finish_axis(self._handle)
        except Exception:
            handle.close()
            raise
        self._sources["x"] = (handle, data)
        token = (self, "x")
        self._tokens["x"] = token
        _lib.h5plot_set_axis_reader(self._handle, self._read_cb, c_void_p(id(token)))

    def _drop_x_file(self) -> None:
        held = self._sources.pop("x", None)
        self._tokens.pop("x", None)
        if held is not None:
            held[0].close()

    def add_hdf5(self, path, dataset: str, colour=None) -> int:
        """Stream a 1-D numeric dataset into the pyramid, one read at a time.

        The chunk is the viewer's read (`kReadRun`, 65536). It is converted
        and folded, then dropped. The file stays open: a closer look finer
        than the pyramid's base reads that window back from it.
        """
        import h5py
        import numpy as np

        if colour is None:
            colour = _CYCLE[len(self._keep) % len(_CYCLE)]
        handle = h5py.File(path, "r")
        try:
            data = handle[dataset]
            if getattr(data, "ndim", None) != 1 or not np.issubdtype(data.dtype, np.number):
                raise ValueError(f"{dataset} is not a 1-D numeric dataset")
            count = int(data.shape[0])
            index = int(_lib.h5plot_begin_line(self._handle, count, colour[0], colour[1], colour[2]))
            if index < 0:
                raise RuntimeError("h5plot_begin_line failed")
            self._keep.append(None)
            step = 1 << 16
            for start in range(0, count, step):
                block = np.ascontiguousarray(data[start:start + step], dtype=np.float64)
                _lib.h5plot_add_samples(self._handle, index, block.ctypes.data_as(POINTER(c_double)),
                                        int(block.size))
            _lib.h5plot_finish_line(self._handle, index)
        except Exception:
            handle.close()
            raise
        self._sources[index] = (handle, data)
        token = (self, index)
        self._tokens[index] = token
        # id() is the PyObject*. The token stays in _tokens, which is the
        # reference that keeps that pointer live for the callback.
        _lib.h5plot_set_reader(self._handle, index, self._read_cb, c_void_p(id(token)))
        return index

    def _read_window(self, user, first: int, count: int, out) -> int:
        import numpy as np

        try:
            _plot, index = ctypes.cast(user, ctypes.py_object).value
            _handle, data = self._sources[index]
            block = np.ascontiguousarray(data[int(first):int(first) + int(count)], dtype=np.float64)
            if block.size != int(count):
                return 0
            ctypes.memmove(out, int(block.ctypes.data), int(count) * 8)
            return 1
        except Exception:
            return 0

    def clear(self) -> None:
        _lib.h5plot_clear(self._handle)
        for handle, _data in self._sources.values():
            handle.close()
        self._sources.clear()
        self._tokens.clear()
        self._keep.clear()
        self._x = None

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

    def line_count(self) -> int:
        return int(_lib.h5plot_line_count(self._handle))

    def set_own_axis(self, index: int, on: bool) -> None:
        _lib.h5plot_set_own_axis(self._handle, int(index), 1 if on else 0)

    def own_axis(self, index: int) -> bool:
        return bool(_lib.h5plot_own_axis(self._handle, int(index)))

    def own_count(self) -> int:
        return int(_lib.h5plot_own_count(self._handle))

    def shared_count(self) -> int:
        return int(_lib.h5plot_shared_count(self._handle))

    def nearest(self, px: float, py: float) -> int:
        return int(_lib.h5plot_nearest(self._handle, px, py))

    def sample(self, px: float, py: float):
        """The drawn sample closest to a pane-local pixel, or None.

        x and y are that sample's values on the axis the line was drawn
        against. A shifted line answers in its own y.
        """
        found = H5PlotSample()
        if not _lib.h5plot_sample(self._handle, float(px), float(py), found):
            return None
        return found

    def shift_line(self, index: int, dx: float, dy: float) -> None:
        _lib.h5plot_shift_line(self._handle, int(index), dx, dy)

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
