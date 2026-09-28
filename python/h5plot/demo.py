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
from PySide6.QtWidgets import QApplication, QCheckBox, QVBoxLayout, QWidget

from h5plot import PlotWidget


def main() -> int:
    app = QApplication(sys.argv)
    n = 2_000_000
    x = np.arange(n)
    sine = np.sin(2.0 * np.pi * x / 4000.0)
    sine[n // 2] = 8.0
    cosine = 0.65 * np.cos(2.0 * np.pi * x / 9000.0)
    slow = 0.35 * np.sin(2.0 * np.pi * x / 1500.0) + 0.2

    window = QWidget()
    window.setWindowTitle("h5plot demo")
    window.resize(960, 540)
    plot = PlotWidget()
    log_y = QCheckBox("Log y")
    log_y.toggled.connect(plot.set_y_log)
    layout = QVBoxLayout(window)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.addWidget(plot, 1)
    layout.addWidget(log_y)
    plot.add_line(sine)
    plot.add_line(cosine)
    plot.add_line(slow)
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
