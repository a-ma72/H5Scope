# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""Three million-point curves on one clock, and a short burst on its own.

One of the long curves carries a one-sample spike. The burst is a different
length and only exists between 240 s and 260 s, so it sits in the middle of
the shared axis.

Build the DLL first:  python\\h5plot\\build.bat
Then:  C:\\Pythonuser\\venv\\Py311-EISB1\\Scripts\\python.exe python\\h5plot\\demo.py
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import math

import numpy as np
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

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
    # Eighty thousand samples across twenty seconds. Not a slice of `time`:
    # its own timestamps, so the curve is drawn where those seconds are.
    burst_n = 80_000
    burst_t = np.linspace(240.0, 260.0, burst_n)
    burst = 1.2 * np.sin(2.0 * np.pi * np.linspace(0.0, 30.0, burst_n))

    window = QWidget()
    window.setWindowTitle("h5plot demo")
    window.resize(960, 540)
    plot = PlotWidget()
    log_y = QCheckBox("Log y")
    log_y.toggled.connect(plot.set_y_log)
    log_x = QCheckBox("Log x")
    log_x.toggled.connect(plot.set_x_log)
    bases = (10.0, 2.0, math.e)
    base_names = ("base 10", "base 2", "base e")
    base_i = {"i": 0}
    base = QPushButton(base_names[0])

    def cycle_base() -> None:
        base_i["i"] = (base_i["i"] + 1) % len(bases)
        chosen = bases[base_i["i"]]
        plot.set_x_log_base(chosen)
        plot.set_y_log_base(chosen)
        base.setText(base_names[base_i["i"]])

    base.clicked.connect(cycle_base)
    scales = QWidget()
    scales_layout = QHBoxLayout(scales)
    scales_layout.setContentsMargins(8, 0, 8, 0)
    scales_layout.addWidget(log_y)
    scales_layout.addWidget(log_x)
    scales_layout.addWidget(base)
    scales_layout.addStretch(1)
    hint = QLabel("Links verschiebt die Ansicht, rechts zieht ein Rechteck "
                  "und nennt den Bereich, den es öffnet. "
                  "Das Rad mit Shift zoomt X, mit Strg Y. "
                  "Alt oder die mittlere Taste verschiebt die Kurve unter dem Zeiger "
                  "in Y und gibt ihr eine eigene Achse mit ihrem Namen. "
                  "Alt+Strg und das Rad skalieren nur diese Kurve in Y. "
                  "Strg-Klick legt sie zurück. Der Zeiger liest den Wert "
                  "der nächsten Kurve. pulse hat eine eigene Zeit und liegt "
                  "nur zwischen 240 s und 260 s.")
    hint.setWordWrap(True)
    layout = QVBoxLayout(window)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.addWidget(plot, 1)
    layout.addWidget(scales)
    layout.addWidget(hint)
    plot.set_x(time)
    plot.set_x_label("s")
    plot.add_line(sine, name="sine")
    plot.add_line(cosine, name="cosine")
    plot.add_line(slow, name="slow")
    plot.set_line_x(plot.add_line(burst, name="pulse"), burst_t)
    
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
