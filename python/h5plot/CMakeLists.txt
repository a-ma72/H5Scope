# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: GPL-3.0-only
#
# Standalone MinGW build of the Qt-free plot DLL. No vcpkg. From repo root:
#
#   python\h5plot\build.bat
#
# Or:
#
#   cmake -S python/h5plot -B python/h5plot/build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
#   cmake --build python/h5plot/build

cmake_minimum_required(VERSION 3.16)
project(h5plot LANGUAGES CXX)

set(H5SCOPE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../src")

add_library(h5plot SHARED
  "${H5SCOPE_SRC}/gui/PlotProjection.cpp"
  "${H5SCOPE_SRC}/gui/PlotLevels.cpp"
  "${H5SCOPE_SRC}/gui/PlotPyramid.cpp"
  "${H5SCOPE_SRC}/plotcore/LineStore.cpp"
  "${H5SCOPE_SRC}/plotcore/View.cpp"
  "${H5SCOPE_SRC}/plotcore/plotcore_c.cpp"
)

target_include_directories(h5plot PRIVATE
  "${H5SCOPE_SRC}/plotcore/stubs"
  "${H5SCOPE_SRC}"
)
target_compile_features(h5plot PRIVATE cxx_std_20)
target_compile_definitions(h5plot PRIVATE H5PLOT_EXPORT)
if(MINGW)
  target_link_options(h5plot PRIVATE -static-libgcc -static-libstdc++)
endif()

set_target_properties(h5plot PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  PREFIX ""
)
