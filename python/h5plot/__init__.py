# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""Drop-in package: from h5plot import PlotWidget"""

from .widget import PlotWidget
from .lib import Plot

__all__ = ["PlotWidget", "Plot"]
