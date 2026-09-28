# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""A matplotlib host for the same fold the Qt window draws.

The axes are matplotlib's: its ticks, its logarithm, its limits taken from
the camera. The samples are not. What is drawn is the envelope the fold
already reduced the line to, in the data's own units, and a gap stays a gap
so matplotlib does not stitch across it.

The navigation toolbar stays off. A scroll, a drag and a rectangle go
through the same camera the window uses. Left to itself, matplotlib would
zoom whatever was last drawn, and that is a pane of the line, not the line.
"""

from __future__ import annotations

import numpy as np

from .lib import Plot

# How far an own axis sits out from the one beside it, in points.
_SPINE = 48


class MplView:
    def __init__(self, plot: Plot | None = None):
        try:
            import matplotlib.pyplot as plt
        except ImportError as error:
            raise ImportError(
                "matplotlib is not installed; pip install h5plot[matplotlib]"
            ) from error
        self.plot = plot if plot is not None else Plot()
        self.figure = plt.figure()
        self.ax = self.figure.add_subplot(111)
        self.ax.set_navigate(False)
        self._own: list = []
        self._artists: list = []
        self._band = None
        self._band_artist = None
        self._drag = None
        self._shift = None
        self._pane = (800, 400)
        self._drawing = False
        canvas = self.figure.canvas
        canvas.mpl_connect("scroll_event", self._scroll)
        canvas.mpl_connect("button_press_event", self._press)
        canvas.mpl_connect("motion_notify_event", self._motion)
        canvas.mpl_connect("button_release_event", self._release)
        canvas.mpl_connect("draw_event", self._drawn)
        canvas.mpl_connect("resize_event", self._resized)

    def set_x_label(self, text: str) -> None:
        self.ax.set_xlabel(text or "")

    def set_y_label(self, text: str) -> None:
        self.ax.set_ylabel(text or "")

    def show(self) -> None:
        import matplotlib.pyplot as plt

        self.redraw()
        plt.show(block=False)
        self._hide_toolbar()
        plt.show()

    def redraw(self) -> None:
        self._drawing = True
        try:
            self._paint()
        finally:
            self._drawing = False
        self.figure.canvas.draw_idle()

    def _paint(self) -> None:
        width, height = self._pane
        self.plot.set_pane(int(width), int(height), 1.0)
        _pixels, runs, _ticks = self.plot.project()
        data = self.plot.data_points()
        owners = self.plot.run_lines()
        for artist in self._artists:
            artist.remove()
        self._artists.clear()
        for axes in self._own:
            axes.remove()
        self._own.clear()

        grouped: dict[int, list] = {}
        for run, line in zip(runs, owners):
            grouped.setdefault(int(line), []).append(run)

        shared = any(not self.plot.own_axis(line) for line in grouped)
        self.ax.yaxis.set_visible(shared)
        self.ax.spines["left"].set_visible(shared)
        x0, x1 = self.plot.view_min_x(), self.plot.view_max_x()
        self.ax.set_xscale("linear")
        self.ax.set_xlim(x0, x1)
        if self.plot.x_log() and x0 > 0.0 and x1 > x0:
            self.ax.set_xscale("log")
        if shared:
            y0, y1 = self.plot.view_min_y(), self.plot.view_max_y()
            self.ax.set_yscale("linear")
            self.ax.set_ylim(y0, y1)
            if self.plot.y_log() and y0 > 0.0 and y1 > y0:
                self.ax.set_yscale("log")

        own_slot = 0
        handles = []
        for line, line_runs in grouped.items():
            if not line_runs:
                continue
            xs: list = []
            ys: list = []
            for run in line_runs:
                if xs:
                    xs.append(np.array([np.nan]))
                    ys.append(np.array([np.nan]))
                chunk = data[run.first:run.first + run.count]
                xs.append(chunk[:, 0])
                ys.append(chunk[:, 1])
            x = np.concatenate(xs)
            y = np.concatenate(ys)
            colour = (line_runs[0].red / 255.0, line_runs[0].green / 255.0, line_runs[0].blue / 255.0)
            if self.plot.own_axis(line):
                outward = _SPINE * own_slot
                if shared:
                    outward += _SPINE
                own_slot += 1
                axes = self._own_axes(colour, outward)
                self._own.append(axes)
                low, high = self.plot.line_y_range(line)
                axes.set_yscale("linear")
                axes.set_ylim(low, high)
                if self.plot.y_log() and low > 0.0 and high > low:
                    axes.set_yscale("log")
            else:
                axes = self.ax
            drawn, = axes.plot(x, y, color=colour, linewidth=max(float(line_runs[0].width), 0.8),
                               solid_capstyle="butt")
            self._artists.append(drawn)
            name = self.plot.line_name(line)
            if name:
                handles.append(drawn)
                drawn.set_label(name)
        if handles:
            legend = self.ax.legend(handles=handles, loc="upper right", frameon=True)
            self._artists.append(legend)
        self.figure.subplots_adjust(left=0.12 + 0.06 * own_slot)

    def _own_axes(self, colour, outward: float):
        # twinx is how the x axis is shared. The spine does not stay where
        # twinx puts it: an own y axis sits out to the left of the common one.
        axes = self.ax.twinx()
        axes.set_navigate(False)
        axes.spines["right"].set_visible(False)
        axes.spines["top"].set_visible(False)
        axes.spines["left"].set_position(("outward", outward))
        axes.yaxis.set_label_position("left")
        axes.yaxis.set_ticks_position("left")
        axes.spines["left"].set_color(colour)
        axes.tick_params(axis="y", colors=colour)
        axes.patch.set_visible(False)
        return axes

    def _drawn(self, _event) -> None:
        if self._drawing:
            return
        bbox = self.ax.get_window_extent()
        pane = (max(1, int(bbox.width)), max(1, int(bbox.height)))
        if pane != self._pane and pane[0] > 1 and pane[1] > 1:
            self._pane = pane
            self.redraw()

    def _resized(self, _event) -> None:
        self._drawn(_event)

    def _hide_toolbar(self) -> None:
        manager = getattr(self.figure.canvas, "manager", None)
        toolbar = getattr(manager, "toolbar", None) if manager is not None else None
        if toolbar is None:
            return
        hide = getattr(toolbar, "setVisible", None)
        if hide is not None:
            hide(False)
            return
        forget = getattr(toolbar, "pack_forget", None)
        if forget is not None:
            forget()

    def _at(self, event):
        if event.x is None or event.y is None:
            return None
        bbox = self.ax.bbox
        return event.x - bbox.x0, bbox.y1 - event.y

    def _scroll(self, event) -> None:
        at = self._at(event)
        if at is None or not event.step:
            return
        key = event.key or ""
        shift = "shift" in key
        control = "control" in key or "ctrl" in key
        self.plot.wheel(at[0], at[1], 1.25 ** float(event.step), shift and not control,
                        control and not shift)
        self.redraw()

    def _press(self, event) -> None:
        if getattr(event, "dblclick", False) and event.button == 1:
            self._drag = None
            self._band = None
            self._shift = None
            self.plot.reset_view()
            self.redraw()
            return
        at = self._at(event)
        if at is None:
            return
        if event.button == 2 or (event.button == 1 and event.key == "alt"):
            index = self.plot.nearest(at[0], at[1])
            if index >= 0:
                self._shift = (index, at[1])
            return
        if event.button == 3:
            self._band = (at, at)
            return
        if event.button == 1:
            self._drag = at

    def _motion(self, event) -> None:
        at = self._at(event)
        if at is None:
            return
        if self._shift is not None:
            index, last = self._shift
            self.plot.shift_line(index, 0.0, at[1] - last)
            self._shift = (index, at[1])
            self.redraw()
            return
        if self._band is not None:
            self._band = (self._band[0], at)
            self._show_band()
            return
        if self._drag is None:
            return
        self.plot.pan(at[0] - self._drag[0], at[1] - self._drag[1])
        self._drag = at
        self.redraw()

    def _release(self, event) -> None:
        if self._shift is not None and event.button in (1, 2):
            self._shift = None
            return
        if event.button == 3 and self._band is not None:
            start, end = self._band
            self._band = None
            self._hide_band()
            if self.plot.zoom_rect(start[0], start[1], end[0], end[1]):
                self.redraw()
            return
        if event.button == 1:
            self._drag = None

    def _show_band(self) -> None:
        from matplotlib.patches import Rectangle

        start, end = self._band
        x0 = self.plot.data_x_at(start[0])
        x1 = self.plot.data_x_at(end[0])
        y0 = self.plot.data_y_at(start[1])
        y1 = self.plot.data_y_at(end[1])
        if self._band_artist is None:
            self._band_artist = Rectangle(
                (0, 0), 0, 0, facecolor=(0.7, 0.7, 0.7, 0.25), edgecolor=(0.35, 0.35, 0.35),
                linewidth=1, zorder=5,
            )
            self.ax.add_patch(self._band_artist)
        self._band_artist.set_xy((min(x0, x1), min(y0, y1)))
        self._band_artist.set_width(abs(x1 - x0))
        self._band_artist.set_height(abs(y1 - y0))
        self._band_artist.set_visible(True)
        self.figure.canvas.draw_idle()

    def _hide_band(self) -> None:
        if self._band_artist is not None:
            self._band_artist.set_visible(False)
