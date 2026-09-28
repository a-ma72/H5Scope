# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""PySide widget: C++ folds the line, QPainter strokes the runs."""

from __future__ import annotations

import ctypes
import math

import numpy as np
import shiboken6
from PySide6.QtCore import Qt, QRect
from PySide6.QtGui import (
    QColor,
    QFontMetrics,
    QImage,
    QMouseEvent,
    QPaintEvent,
    QPainter,
    QPen,
    QPolygonF,
    QResizeEvent,
    QWheelEvent,
)
from PySide6.QtWidgets import QWidget

from .lib import Plot

# Room for the numbers. The curve is projected into what is left.
_TOP = 8
_RIGHT = 12
_BOTTOM = 28
_OWN_COLUMN = 64


def _label(value: float) -> str:
    if value == 0.0 or abs(value) < 1e-12:
        return "0"
    return format(value, ".6g")


_SUP = "⁰¹²³⁴⁵⁶⁷⁸⁹"


def _raised(exponent: int) -> str:
    sign = "⁻" if exponent < 0 else ""
    return sign + "".join(_SUP[int(digit)] for digit in str(abs(exponent)))


def _log_label(value: float, base: float = 10.0) -> str:
    """10³ at a power, 2×10³ at a numbered multiple of one."""
    if not (value > 0.0) or not (base > 1.0):
        return _label(value)
    fx = math.log(value) / math.log(base)
    decade = abs(fx - round(fx)) < 1e-10
    exponent = int(round(fx) if decade else math.floor(fx))
    power = "e" if abs(base - math.e) < 1e-12 else format(base, ".6g")
    if decade:
        return power + _raised(exponent)
    coefficient = value / (base ** exponent)
    if abs(coefficient - round(coefficient)) < 1e-8:
        coefficient = int(round(coefficient))
        lead = str(coefficient)
    else:
        lead = format(coefficient, ".6g")
    return lead + "×" + power + _raised(exponent)


def _tick_text(tick) -> str:
    if tick.logarithmic:
        return _log_label(tick.value)
    return _label(tick.value)


def _trimmed(text: str) -> str:
    """Drop trailing zeros, and the point with them. An exponent keeps its part."""
    at = text.find("e")
    mantissa = text if at < 0 else text[:at]
    if "." not in mantissa:
        return text
    mantissa = mantissa.rstrip("0").rstrip(".")
    if at < 0:
        return mantissa
    exponent = text[at + 1 :]
    sign = ""
    if exponent[:1] in "+-":
        sign = exponent[0]
        exponent = exponent[1:]
    return mantissa + "e" + sign + (exponent.lstrip("0") or "0")


def _label_for(value: float, span: float) -> str:
    """A linear tick: enough figures to tell it from the next, judged by the span."""
    width = abs(span)
    if width >= 1e6 or (0.0 < width < 1e-4):
        if value == 0.0:
            return "0"
        figures = 1
        if value != 0.0 and width > 0.0:
            figures = math.ceil(math.log10(abs(value) / width)) + 2
        figures = max(1, min(15, figures))
        return _trimmed(format(value, f".{figures}e"))
    places = 0
    if width > 0.0:
        places = max(0, min(6, math.ceil(-math.log10(width)) + 2))
    return _trimmed(format(value, f".{places}f"))


def _log_label_for(value: float) -> str:
    """A value between logarithmic ticks: three figures, zeros taken off."""
    if not (value > 0.0) or not math.isfinite(value):
        return _label(value)
    exponent = math.floor(math.log10(value))
    if exponent >= 6 or exponent < -4:
        return _trimmed(format(value, ".2e"))
    places = max(0, min(8, 2 - exponent))
    return _trimmed(format(value, f".{places}f"))


def _axis_number(value: float, low: float, high: float, logarithmic: bool) -> str:
    if logarithmic:
        return _log_label_for(value)
    return _label_for(value, high - low)


# The air a band's number keeps from the rectangle, the pane's edge and the
# pointer. One distance, because they are one rule.
_CLEAR = 6
_POINTER = 16


def _clears(a, b) -> bool:
    return (a[0] + a[2] + _CLEAR <= b[0] or b[0] + b[2] + _CLEAR <= a[0]
            or a[1] + a[3] + _CLEAR <= b[1] or b[1] + b[3] + _CLEAR <= a[1])


def _inside(box, pane_w: float, pane_h: float) -> bool:
    return (box[0] >= 0 and box[1] >= 0
            and box[0] + box[2] <= pane_w and box[1] + box[3] <= pane_h)


def _place_corner(cx, cy, away_x, away_y, tw, th, pane_w, pane_h, avoid):
    """Push a corner's number off the band, and off the pointer when that is where it would land."""
    out_x = _POINTER + _CLEAR if away_x > 0 and away_y > 0 else _CLEAR
    want_x = cx - _CLEAR - tw if away_x < 0 else cx + out_x
    want_y = cy - _CLEAR - th if away_y < 0 else cy + _CLEAR
    back_x = cx + _CLEAR if away_x < 0 else cx - _CLEAR - tw
    back_y = cy + _CLEAR if away_y < 0 else cy - _CLEAR - th

    def clamp(value, size, extent):
        return max(0.0, min(extent - size, value))

    def at(x, y):
        return (x, y, tw, th)

    tries = (
        at(clamp(want_x, tw, pane_w), want_y),
        at(want_x, clamp(want_y, th, pane_h)),
        at(clamp(want_x, tw, pane_w), back_y),
        at(back_x, clamp(want_y, th, pane_h)),
    )
    for box in tries:
        if _inside(box, pane_w, pane_h) and all(_clears(box, other) for other in avoid):
            return box
    return at(clamp(want_x, tw, pane_w), clamp(want_y, th, pane_h))


def _place_beside(box, pane_w, pane_h, avoid):
    if _inside(box, pane_w, pane_h) and all(_clears(box, other) for other in avoid):
        return box
    return None


def _polygon(xy, first: int, count: int, scale: float) -> QPolygonF:
    """Copy a run straight into a polygon, in the image's own pixels.

    A Python QPointF per sample costs more than the fold. A scale on the
    painter costs more than that: stroking an envelope through one takes
    hundreds of milliseconds, and the same stroke into an unscaled image does
    not.
    """
    poly = QPolygonF()
    if count <= 0:
        return poly
    poly.resize(count)
    block = np.ascontiguousarray(xy[first:first + count], dtype=np.float64)
    if scale != 1.0:
        block = block * scale
    ptr = shiboken6.getCppPointer(poly.data())[0]
    ctypes.memmove(ptr, int(block.ctypes.data), count * 16)
    return poly


class PlotWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._plot = Plot()
        self._xy = None
        self._runs = []
        self._ticks = []
        self._drag = None
        self._band = None
        self._shift = None
        self._reading = None
        self._stroke = QImage()
        self.setMinimumSize(240, 160)
        self.setMouseTracking(True)

    def _columns(self) -> tuple[int, int]:
        """Own-axis columns, then whether the common y axis still has a column."""
        own = self._plot.own_count()
        lines = self._plot.line_count()
        common = 0 if lines > 0 and own >= lines else 1
        return own, common

    def _pane(self) -> tuple[int, int, int, int]:
        own, common = self._columns()
        left = _OWN_COLUMN * (own + common)
        width = max(1, self.width() - left - _RIGHT)
        height = max(1, self.height() - _TOP - _BOTTOM)
        return left, _TOP, width, height

    def set_x(self, x):
        self._plot.set_x(x)
        self._plot.reset_view()
        self._reproject()

    def set_x_hdf5(self, path, dataset: str):
        self._plot.set_x_hdf5(path, dataset)
        self._plot.reset_view()
        self._reproject()

    def add_line(self, y, colour=None):
        self._plot.add_line(y, colour)
        self._plot.reset_view()
        self._reproject()

    def add_hdf5(self, path, dataset: str, colour=None):
        self._plot.add_hdf5(path, dataset, colour)
        self._plot.reset_view()
        self._reproject()

    def set_y_log(self, on: bool):
        self._plot.set_y_log(on)
        self._reproject()

    def set_x_log(self, on: bool):
        self._plot.set_x_log(on)
        self._reproject()

    def _reproject(self):
        _left, _top, width, height = self._pane()
        ratio = float(self.devicePixelRatioF())
        self._plot.set_pane(width, height, ratio)
        self._xy, self._runs, self._ticks = self._plot.project()
        self.update()

    def paintEvent(self, event: QPaintEvent):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.fillRect(self.rect(), QColor(0, 0, 0))
        left, top, width, height = self._pane()
        area = QRect(left, top, width, height)
        rule = QColor(40, 40, 40)
        ink = QColor(220, 220, 220)
        painter.setPen(QPen(rule, 1.0))
        for tick in self._ticks:
            if tick.axis == 2:
                continue
            # A minor on a logarithmic axis is a mark on the spine. A full
            # rule at every multiple of a power hatches the pane.
            minor = tick.logarithmic and not tick.labeled
            if tick.axis == 0:
                x = left + int(round(tick.x))
                if minor:
                    painter.drawLine(x, top + height - 6, x, top + height)
                else:
                    painter.drawLine(x, top, x, top + height)
            else:
                y = top + int(round(tick.y))
                if minor:
                    painter.drawLine(left, y, left + 6, y)
                else:
                    painter.drawLine(left, y, left + width, y)
        painter.setPen(QPen(ink, 1.0))
        painter.drawRect(area.adjusted(0, 0, -1, -1))
        slots = {}
        for tick in self._ticks:
            if tick.axis == 2 and tick.series not in slots:
                slots[tick.series] = len(slots)
        for tick in self._ticks:
            if not tick.labeled:
                continue
            text = _tick_text(tick)
            if tick.axis == 0:
                x = left + int(round(tick.x))
                painter.drawText(QRect(x - 48, top + height + 2, 96, _BOTTOM - 4),
                                 Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop, text)
            elif tick.axis == 1:
                y = top + int(round(tick.y))
                painter.drawText(QRect(left - _OWN_COLUMN, y - 8, _OWN_COLUMN - 6, 16),
                                 Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter, text)
            else:
                y = top + int(round(tick.y))
                slot = slots.get(tick.series, 0)
                painter.setPen(QColor(tick.red, tick.green, tick.blue))
                painter.drawText(QRect(slot * _OWN_COLUMN, y - 8, _OWN_COLUMN - 6, 16),
                                 Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter, text)
                painter.setPen(QPen(ink, 1.0))
        if self._xy is not None and len(self._runs) != 0:
            painter.drawImage(QRect(left, top, width, height), self._curves(width, height))
        if self._band is not None:
            start, end = self._band
            band = QRect(int(min(start.x(), end.x())), int(min(start.y(), end.y())),
                         int(abs(end.x() - start.x())), int(abs(end.y() - start.y()))).intersected(area)
            painter.setPen(QPen(ink, 1.0))
            painter.setBrush(QColor(ink.red(), ink.green(), ink.blue(), 48))
            painter.drawRect(band)
            self._paint_band_readout(painter, left, top, width, height, ink)
        if self._reading is not None:
            reading = self._reading
            hx = left + reading.px
            hy = top + reading.py
            painter.setPen(QPen(QColor(70, 70, 70), 1.0))
            painter.drawLine(left, int(round(hy)), left + width, int(round(hy)))
            painter.drawLine(int(round(hx)), top, int(round(hx)), top + height)
            painter.setPen(QColor(reading.red, reading.green, reading.blue))
            painter.drawLine(int(round(hx)) - 4, int(round(hy)), int(round(hx)) + 4, int(round(hy)))
            painter.drawLine(int(round(hx)), int(round(hy)) - 4, int(round(hx)), int(round(hy)) + 4)
            painter.setPen(ink)
            painter.drawText(QRect(left + 8, top + 4, width - 16, 18),
                             Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter,
                             f"{_label(reading.x)}    {_label(reading.y)}")

    def _paint_band_readout(self, painter: QPainter, left: int, top: int, width: int,
                             height: int, ink: QColor) -> None:
        # The corners are asked of the same mapping the zoom uses on release,
        # and written the way the ticks beside them are written. A second copy
        # of either would be a number that can disagree with the window it names.
        if width <= 0 or height <= 0 or self._band is None:
            return
        start, end = self._band

        def pane(pos):
            return (min(max(pos.x() - left, 0.0), float(width)),
                    min(max(pos.y() - top, 0.0), float(height)))

        sx, sy = pane(start)
        ex, ey = pane(end)
        x_log = any(tick.axis == 0 and tick.logarithmic for tick in self._ticks)
        y_log = any(tick.axis == 1 and tick.logarithmic for tick in self._ticks)
        view_x0 = self._plot.view_min_x()
        view_x1 = self._plot.view_max_x()
        view_y0 = self._plot.view_min_y()
        view_y1 = self._plot.view_max_y()
        common_y = self._plot.line_count() == 0 or self._plot.own_count() < self._plot.line_count()

        def nx(value: float) -> str:
            return _axis_number(value, view_x0, view_x1, x_log)

        def ny(value: float) -> str:
            return _axis_number(value, view_y0, view_y1, y_log)

        start_x = self._plot.data_x_at(sx)
        end_x = self._plot.data_x_at(ex)
        if common_y:
            start_text = f"{nx(start_x)}, {ny(self._plot.data_y_at(sy))}"
            end_text = f"{nx(end_x)}, {ny(self._plot.data_y_at(ey))}"
            height_text = "∆" + ny(abs(self._plot.data_y_at(ey) - self._plot.data_y_at(sy)))
        else:
            start_text = nx(start_x)
            end_text = nx(end_x)
            height_text = ""
        width_text = "∆" + nx(abs(end_x - start_x))

        metrics = QFontMetrics(painter.font())

        def box_size(text: str):
            return metrics.horizontalAdvance(text) + 8, metrics.height() + 4

        band = (min(sx, ex), min(sy, ey), abs(ex - sx), abs(ey - sy))
        pointer = (ex, ey, float(_POINTER), float(_POINTER))
        runs_right = 1 if ex >= sx else -1
        runs_down = 1 if ey >= sy else -1
        start_w, start_h = box_size(start_text)
        end_w, end_h = box_size(end_text)
        start_box = _place_corner(sx, sy, -runs_right, -runs_down, start_w, start_h,
                                   width, height, [band, pointer])
        end_box = _place_corner(ex, ey, runs_right, runs_down, end_w, end_h,
                                 width, height, [band, pointer])
        labels = [(start_box, start_text), (end_box, end_text)]
        occupied = [band, pointer, start_box, end_box]

        width_w, width_h = box_size(width_text)
        if width_w <= band[2]:
            centered = band[0] + (band[2] - width_w) / 2.0
            above = (centered, band[1] - _CLEAR - width_h, width_w, width_h)
            below = (centered, band[1] + band[3] + _CLEAR, width_w, width_h)
            placed = _place_beside(above, width, height, occupied) or _place_beside(
                below, width, height, occupied)
            if placed is not None:
                labels.append((placed, width_text))
                occupied.append(placed)
        if height_text:
            height_w, height_h = box_size(height_text)
            if height_h <= band[3]:
                centered = band[1] + (band[3] - height_h) / 2.0
                west = (band[0] - _CLEAR - height_w, centered, height_w, height_h)
                east = (band[0] + band[2] + _CLEAR, centered, height_w, height_h)
                placed = _place_beside(west, width, height, occupied) or _place_beside(
                    east, width, height, occupied)
                if placed is not None:
                    labels.append((placed, height_text))

        painter.setBrush(Qt.BrushStyle.NoBrush)
        for box, text in labels:
            rect = QRect(left + int(round(box[0])), top + int(round(box[1])),
                         int(round(box[2])), int(round(box[3])))
            painter.fillRect(rect, QColor(0, 0, 0, 210))
            painter.setPen(ink)
            painter.drawText(rect, Qt.AlignmentFlag.AlignCenter, text)

    def _curves(self, width: int, height: int) -> QImage:
        # Stroke in device pixels, with an integer pen width and no painter
        # transform. A whole-line envelope is thousands of segments. Qt's
        # raster engine draws that in a few milliseconds on a plain image and
        # in hundreds when the painter carries a scale — a device-pixel-ratio
        # on the image installs one — or when the pen width is not a whole
        # number of pixels. The finished image is copied into the pane.
        ratio = float(self.devicePixelRatioF())
        if ratio <= 0.0:
            ratio = 1.0
        pixels_w = max(1, int(round(width * ratio)))
        pixels_h = max(1, int(round(height * ratio)))
        image = self._stroke
        if image.width() != pixels_w or image.height() != pixels_h:
            image = QImage(pixels_w, pixels_h, QImage.Format.Format_ARGB32_Premultiplied)
            self._stroke = image
        image.fill(0)
        stroke = QPainter(image)
        for run in self._runs:
            colour = QColor(run.red, run.green, run.blue, run.alpha)
            dense = run.count > width * 1.5
            stroke.setRenderHint(QPainter.RenderHint.Antialiasing, not dense)
            stroke.setPen(QPen(colour, max(1, int(round(run.width * ratio))), Qt.PenStyle.SolidLine,
                               Qt.PenCapStyle.FlatCap,
                               Qt.PenJoinStyle.MiterJoin if dense else Qt.PenJoinStyle.RoundJoin))
            stroke.drawPolyline(_polygon(self._xy, run.first, run.count, ratio))
        stroke.end()
        return image

    def resizeEvent(self, event: QResizeEvent):
        super().resizeEvent(event)
        self._reproject()

    def wheelEvent(self, event: QWheelEvent):
        turned = event.angleDelta().y() or event.angleDelta().x()
        if turned == 0:
            return
        factor = 1.25 ** (turned / 120.0)
        mods = event.modifiers()
        left, top, _width, _height = self._pane()
        self._plot.wheel(event.position().x() - left, event.position().y() - top, factor,
                         bool(mods & Qt.KeyboardModifier.ShiftModifier),
                         bool(mods & Qt.KeyboardModifier.ControlModifier))
        self._reproject()

    def _pane_point(self, pos):
        left, top, _width, _height = self._pane()
        return pos.x() - left, pos.y() - top

    def mousePressEvent(self, event: QMouseEvent):
        alt = bool(event.modifiers() & Qt.KeyboardModifier.AltModifier)
        if (event.button() == Qt.MouseButton.MiddleButton or
                (event.button() == Qt.MouseButton.LeftButton and alt)) and self._band is None:
            index = self._plot.nearest(*self._pane_point(event.position()))
            if index >= 0:
                self._reading = None
                self._shift = (index, event.position())
                self.setCursor(Qt.CursorShape.SizeAllCursor)
            return
        if event.button() == Qt.MouseButton.RightButton and self._drag is None and self._shift is None:
            self._reading = None
            self._band = (event.position(), event.position())
            self.setCursor(Qt.CursorShape.CrossCursor)
            self.update()
            return
        if event.button() == Qt.MouseButton.LeftButton and self._band is None and self._shift is None:
            self._reading = None
            self._drag = event.position()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)

    def _read(self, pos) -> None:
        px, py = self._pane_point(pos)
        self._reading = self._plot.sample(px, py)
        self.update()

    def leaveEvent(self, event):
        self._reading = None
        self.update()
        super().leaveEvent(event)

    def mouseMoveEvent(self, event: QMouseEvent):
        if self._shift is not None:
            index, last = self._shift
            pos = event.position()
            self._plot.shift_line(index, 0.0, pos.y() - last.y())
            self._shift = (index, pos)
            self._reproject()
            return
        if self._band is not None:
            self._band = (self._band[0], event.position())
            self.update()
            return
        if self._drag is None:
            self._read(event.position())
            return
        self._reading = None
        pos = event.position()
        self._plot.pan(pos.x() - self._drag.x(), pos.y() - self._drag.y())
        self._drag = pos
        self._reproject()

    def mouseReleaseEvent(self, event: QMouseEvent):
        if self._shift is not None and event.button() in (
                Qt.MouseButton.MiddleButton, Qt.MouseButton.LeftButton):
            self._shift = None
            self.unsetCursor()
            return
        if event.button() == Qt.MouseButton.RightButton and self._band is not None:
            start, end = self._band
            self._band = None
            self.unsetCursor()
            left, top, _width, _height = self._pane()
            moved = self._plot.zoom_rect(start.x() - left, start.y() - top, end.x() - left,
                                         end.y() - top)
            if moved:
                self._reproject()
            else:
                self.update()
            return
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag = None
            self.unsetCursor()

    def mouseDoubleClickEvent(self, event: QMouseEvent):
        if event.button() == Qt.MouseButton.LeftButton:
            self._plot.reset_view()
            self._reproject()
