# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""PySide widget: C++ folds the line, QPainter strokes the runs."""

from __future__ import annotations

import ctypes

import numpy as np
import shiboken6
from PySide6.QtCore import Qt, QRect
from PySide6.QtGui import (
    QColor,
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
            if tick.axis == 0:
                x = left + int(round(tick.x))
                painter.drawLine(x, top, x, top + height)
            else:
                y = top + int(round(tick.y))
                painter.drawLine(left, y, left + width, y)
        painter.setPen(QPen(ink, 1.0))
        painter.drawRect(area.adjusted(0, 0, -1, -1))
        slots = {}
        for tick in self._ticks:
            if tick.axis == 2 and tick.series not in slots:
                slots[tick.series] = len(slots)
        for tick in self._ticks:
            text = _label(tick.value)
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
