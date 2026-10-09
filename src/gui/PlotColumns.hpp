// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Pixel columns of the axis, not index buckets, decide what is drawn.
//
// A monitor has discrete columns. Sub-pixel detail is invisible, so a long
// line is reduced to what fills each column faithfully: a vertical run, in
// the extreme a single pixel. The time axis (linear or log) partitions the
// view into adjacent intervals — one per column — independent of the samples.
//
// Samples are cached in a LinePyramid (min/max in occurrence order). Buckets
// answer multi-sample columns. Zero or one sample in a column is a plain
// polyline between samples. More than one sample uses entry → extrema → exit,
// with y at the column edges interpolated so a steep approach paints the
// previous column correctly. That sub-pixel edge is the difference between a
// min–max bar and a true stroke.
//
// Replaces the earlier index-bucket polyline plus chord repair: the pane's
// columns are the unit of visibility, so they are the unit of the fold.

#include "gui/PlotPyramid.hpp"

#include <span>
#include <vector>

namespace gui {

/// One line reduced to what the pane's pixel columns need.
struct ColumnStroke
{
    std::vector<double> values;
    /// Data-x of each value (sample index, or time). Same length as `values`.
    std::vector<double> xs;
    /// True when any column held more than one sample and used extrema.
    bool summarised = false;
};

/// Edges of `columns` pixel columns covering `xMin`..`xMax`.
///
/// Linear: equal width in x. Log: equal width in log2(x); requires both ends
/// above zero. `edges` has `columns + 1` ascending values.
void columnEdges(double xMin, double xMax, int columns, bool xLog, std::vector<double>& edges);

/// Which column of `edges` holds `x`, or -1 when outside `[edges.front(), edges.back())`.
[[nodiscard]] int columnOf(std::span<const double> edges, double x);

/// Raster one line onto the view's pixel columns.
///
/// `raw` is the sample row when held (preferred). Absent, a pyramid whose base
/// is one element is read as the row. Coarser bases answer multi-sample columns
/// through `extremesOver`; a column that cuts the finest held bucket without a
/// raw row cannot resolve single samples and is skipped for those.
///
/// `times` non-empty: one monotonic time per sample. Otherwise x is
/// `indexStart + i * indexStep` (the Plot tab's stated axis).
///
/// False when nothing drawable can be produced (empty pyramid, bad view, no
/// columns).
[[nodiscard]] bool rasterColumns(const LinePyramid& pyramid, const double* raw, long long count,
                                 std::span<const double> times, double indexStart, double indexStep,
                                 double xMin, double xMax, int columns, bool xLog, ColumnStroke& out);

/// Index axis (`x = i`). Same as `rasterColumns(..., 0, 1, ...)`.
[[nodiscard]] inline bool rasterColumns(const LinePyramid& pyramid, const double* raw,
                                        long long count, std::span<const double> times, double xMin,
                                        double xMax, int columns, bool xLog, ColumnStroke& out)
{
    return rasterColumns(pyramid, raw, count, times, 0.0, 1.0, xMin, xMax, columns, xLog, out);
}

} // namespace gui
