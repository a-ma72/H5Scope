# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""The same curves, drawn by matplotlib, including the burst on its own time.

The fold is the one the Qt window uses. Matplotlib draws that envelope and
numbers the axes itself. The toolbar stays off.

    C:\\Pythonuser\\venv\\Py311-EISB1\\Scripts\\python.exe python\\h5plot\\demo_mpl.py

Scroll zooms, Shift scrolls x, Ctrl scrolls y. The left button pans,
the right button pulls a rectangle and names the window it will open.
The pointer reads the nearest drawn sample. Log x and Log y sit under
the axes, and the base beside them numbers the powers. The pulse is a
different length and only exists between 240 s and 260 s.
Alt or the middle button shifts the curve under the pointer onto an axis
of its own. Alt+Ctrl and the wheel scale only that curve in y.
Ctrl-click puts it back. A double-click resets the window.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import matplotlib

matplotlib.use("QtAgg")

import numpy as np

from h5plot.mpl import MplView


def main() -> int:
    n = 2_000_000
    sample = np.arange(n)
    time = sample / 4000.0
    sine = np.sin(2.0 * np.pi * sample / 4000.0)
    sine[n // 2] = 8.0
    cosine = 0.65 * np.cos(2.0 * np.pi * sample / 9000.0)
    slow = 0.35 * np.sin(2.0 * np.pi * sample / 1500.0) + 0.2
    burst_n = 80_000
    burst_t = np.linspace(240.0, 260.0, burst_n)
    burst = 1.2 * np.sin(2.0 * np.pi * np.linspace(0.0, 30.0, burst_n))

    view = MplView()
    view.figure.set_size_inches(10, 5.5)
    manager = view.figure.canvas.manager
    if manager is not None:
        manager.set_window_title("h5plot matplotlib")
    view.set_x_label("s")
    view.plot.set_x(time)
    view.plot.add_line(sine, name="sine")
    view.plot.add_line(cosine, name="cosine")
    view.plot.add_line(slow, name="slow")
    view.plot.set_line_x(view.plot.add_line(burst, name="pulse"), burst_t)
    view.show()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
