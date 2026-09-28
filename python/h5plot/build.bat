@echo off
rem SPDX-FileCopyrightText: 2026 Andreas Martin
rem SPDX-License-Identifier: GPL-3.0-only
rem
rem Build h5plot.dll with TDM GCC. CMake is optional; this calls g++ directly.
setlocal
set ROOT=%~dp0..\..
set SRC=%ROOT%\src
set OUT=%~dp0h5plot.dll
set CXX=g++
where g++ >nul 2>nul
if errorlevel 1 (
  if exist C:\TDM-GCC-64\bin\g++.exe set CXX=C:\TDM-GCC-64\bin\g++.exe
)
echo Using %CXX%
"%CXX%" --version

"%CXX%" -shared -O2 -std=c++20 -DH5PLOT_EXPORT ^
  -I "%SRC%\plotcore\stubs" -I "%SRC%" ^
  "%SRC%\gui\PlotProjection.cpp" ^
  "%SRC%\gui\PlotLevels.cpp" ^
  "%SRC%\gui\PlotPyramid.cpp" ^
  "%SRC%\plotcore\LineStore.cpp" ^
  "%SRC%\plotcore\View.cpp" ^
  "%SRC%\plotcore\plotcore_c.cpp" ^
  -static-libgcc -static-libstdc++ ^
  -o "%OUT%"
if errorlevel 1 exit /b 1
echo Built %OUT%
dir "%OUT%"
