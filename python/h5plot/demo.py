# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""Three million-point curves, one of them with a one-sample spike.

Build the DLL first:  python\\h5plot\\build.bat
Then:  C:\\Pythonuser\\venv\\Py311-EISB1\\Scripts\\python.exe python\\h5plot\\demo.py
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import numpy as np
from PySide6.QtWidgets import QApplication, QCheckBox, QHBoxLayout, QLabel, QVBoxLayout, QWidget

from h5plot import PlotWidget


def main() -> int:
    app = QApplication(sys.argv)
    n = 2_000_000
    sample = np.arange(n)
    # One second per 4000 samples, so a period of the first curve is one
    # second and the axis is that time rather than the sample index.
    time = sample / 4000.0
    sine = np.sin(2.0 * np.pi * sample / 4000.0)
    sine[n // 2] = 8.0
    cosine = 0.65 * np.cos(2.0 * np.pi * sample / 9000.0)
    slow = 0.35 * np.sin(2.0 * np.pi * sample / 1500.0) + 0.2

    window = QWidget()
    window.setWindowTitle("h5plot demo")
    window.resize(960, 540)
    plot = PlotWidget()
    log_y = QCheckBox("Log y")
    log_y.toggled.connect(plot.set_y_log)
    log_x = QCheckBox("Log x")
    log_x.toggled.connect(plot.set_x_log)
    scales = QWidget()
    scales_layout = QHBoxLayout(scales)
    scales_layout.setContentsMargins(8, 0, 8, 0)
    scales_layout.addWidget(log_y)
    scales_layout.addWidget(log_x)
    scales_layout.addStretch(1)
    hint = QLabel("Links verschiebt die Ansicht, rechts zieht ein Rechteck "
                  "und nennt den Bereich, den es öffnet. "
                  "Alt oder die mittlere Taste verschiebt die Kurve unter dem Zeiger "
                  "in Y und gibt ihr eine eigene Achse. Der Zeiger liest den Wert "
                  "der nächsten Kurve.")
    hint.setWordWrap(True)
    layout = QVBoxLayout(window)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.addWidget(plot, 1)
    layout.addWidget(scales)
    layout.addWidget(hint)
    plot.set_x(time)
    plot.add_line(sine, name="sine")
    plot.add_line(cosine, name="cosine")
    plot.add_line(slow, name="slow")
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
