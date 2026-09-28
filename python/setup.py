# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only

"""Build the fold into h5plot.dll, then install the package around it.

The DLL is gitignored. A wheel still has to carry it, or an installed
import looks for a library that was never copied. build.bat is that
compile; this runs it before setuptools collects package data.
"""

import subprocess
from pathlib import Path

from setuptools import setup
from setuptools.command.build_py import build_py


class build_with_fold(build_py):
    def run(self):
        here = Path(__file__).resolve().parent
        script = here / "h5plot" / "build.bat"
        subprocess.check_call(["cmd", "/c", str(script)], cwd=here.parent)
        super().run()


setup(cmdclass={"build_py": build_with_fold})
