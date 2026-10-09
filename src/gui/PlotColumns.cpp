// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#include "gui/PlotColumns.hpp"

#include "gui/PlotLevels.hpp"

#include <algorithm>
#include <cmath>

namespace gui {
namespace {

/// Prefer the pyramid once a column holds at least this many samples. Below
/// that a raw walk is a handful of doubles already in cache; above it the
/// pyramid answers in O(log n) instead of O(samples).
constexpr int kPyramidPrefer = 32;

double sampleX(std::span<const double> times, long long i, double indexStart, double indexStep)
{
    if (!times.empty() && i >= 0 && static_cast<std::size_t>(i) < times.size()) {
        return times[static_cast<std::size_t>(i)];
    }
    return indexStart + static_cast<double>(i) * indexStep;
}

const double* rawRow(const LinePyramid& pyramid, const double* raw, long long count)
{
    if (raw != nullptr && count > 0) {
        return raw;
    }
    if (!pyramid.empty() && pyramid.baseBucket() == 1 &&
        static_cast<long long>(pyramid.levels.front().values.size()) >= pyramid.length) {
        return pyramid.levels.front().values.data();
    }
    return nullptr;
}

double lerpY(double x0, double y0, double x1, double y1, double at)
{
    const double dx = x1 - x0;
    if (!(std::abs(dx) > 0.0) || !std::isfinite(at)) {
        return y0;
    }
    const double t = (at - x0) / dx;
    return y0 + t * (y1 - y0);
}

void emitPoint(ColumnStroke& out, double x, double y)
{
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return;
    }
    if (!out.values.empty()) {
        const double px = out.xs.back();
        const double py = out.values.back();
        if (px == x && py == y) {
            return;
        }
    }
    out.xs.push_back(x);
    out.values.push_back(y);
}

bool yAt(const LinePyramid& pyramid, const double* raw, long long count, long long i, double& out)
{
    if (i < 0 || i >= (count > 0 ? count : pyramid.length)) {
        return false;
    }
    if (raw != nullptr) {
        out = raw[static_cast<std::size_t>(i)];
        return std::isfinite(out);
    }
    if (pyramid.baseBucket() == 1 && !pyramid.empty()) {
        out = pyramid.levels.front().values[static_cast<std::size_t>(i)];
        return std::isfinite(out);
    }
    const Extremes hit = extremesOver(pyramid, i, i + 1);
    if (!hit.found()) {
        return false;
    }
    out = hit.first();
    return true;
}

/// Linear axis: column index by division. Log keeps a bisect on `edges`.
int columnIndex(std::span<const double> edges, double x, bool xLog, double xMin, double invWidth,
                int columns)
{
    if (edges.size() < 2 || !std::isfinite(x) || x < edges.front() || x > edges.back()) {
        return -1;
    }
    if (x == edges.back()) {
        return columns - 1;
    }
    if (!xLog) {
        const int c = static_cast<int>((x - xMin) * invWidth);
        return std::clamp(c, 0, columns - 1);
    }
    const auto it = std::upper_bound(edges.begin(), edges.end(), x);
    const int c = static_cast<int>(it - edges.begin()) - 1;
    return std::clamp(c, 0, columns - 1);
}

/// First sample index with x >= t. Times must be non-decreasing; the index
/// axis uses a positive step (negative step takes the walk path below).
long long firstAtLeast(std::span<const double> times, long long count, double indexStart,
                       double indexStep, double t)
{
    if (!times.empty()) {
        return static_cast<long long>(std::lower_bound(times.begin(), times.end(), t) -
                                      times.begin());
    }
    const double at = (t - indexStart) / indexStep;
    return std::clamp(static_cast<long long>(std::ceil(at - 1e-15)), 0LL, count);
}

/// First sample index with x > t (inclusive right edge of the last column).
long long firstAfter(std::span<const double> times, long long count, double indexStart,
                     double indexStep, double t)
{
    if (!times.empty()) {
        return static_cast<long long>(std::upper_bound(times.begin(), times.end(), t) -
                                      times.begin());
    }
    const double at = (t - indexStart) / indexStep;
    return std::clamp(static_cast<long long>(std::floor(at + 1e-15)) + 1, 0LL, count);
}

Extremes extremesFor(const LinePyramid& pyramid, const double* raw, long long first, long long last)
{
    if (first >= last) {
        return {};
    }
    const long long n = last - first;
    // Wide column: pyramid. Narrow with raw in hand: one contiguous read.
    if (raw != nullptr && n < kPyramidPrefer) {
        return extremesOf(raw, first, last);
    }
    return extremesOver(pyramid, first, last);
}

/// Entry → extrema → exit for one multi-sample column. Returns false when the
/// run has no finite extreme (a gap).
bool emitSummarised(const LinePyramid& pyramid, const double* row, long long count,
                    std::span<const double> times, double indexStart, double indexStep,
                    std::span<const double> edges, bool xLog, double invWidth, int columns, int c,
                    long long first, long long lastEx, ColumnStroke& out)
{
    const Extremes ext = extremesFor(pyramid, row, first, lastEx);
    if (!ext.found()) {
        return false;
    }
    out.summarised = true;

    const double t0 = edges[static_cast<std::size_t>(c)];
    const double t1 = edges[static_cast<std::size_t>(c) + 1];
    const long long at0 = ext.firstAt();
    const long long at1 = ext.secondAt();
    const double xExt0 = sampleX(times, at0, indexStart, indexStep);
    const double xExt1 = sampleX(times, at1, indexStart, indexStep);

    const long long prev = first - 1;
    if (prev >= 0) {
        double yPrev = 0.0;
        if (yAt(pyramid, row, count, prev, yPrev)) {
            const double xPrev = sampleX(times, prev, indexStart, indexStep);
            const int cPrev = columnIndex(edges, xPrev, xLog, edges.front(), invWidth, columns);
            if (cPrev < 0 || c - cPrev <= 1) {
                emitPoint(out, t0, lerpY(xPrev, yPrev, xExt0, ext.first(), t0));
            }
        }
    }

    emitPoint(out, xExt0, ext.first());
    if (at0 != at1) {
        emitPoint(out, xExt1, ext.second());
    }

    if (lastEx < count) {
        double yNext = 0.0;
        if (yAt(pyramid, row, count, lastEx, yNext)) {
            const double xNext = sampleX(times, lastEx, indexStart, indexStep);
            const int cNext = columnIndex(edges, xNext, xLog, edges.front(), invWidth, columns);
            if (cNext < 0 || cNext - c <= 1) {
                emitPoint(out, t1, lerpY(xExt1, ext.second(), xNext, yNext, t1));
            }
        }
    }
    return true;
}

} // namespace

void columnEdges(double xMin, double xMax, int columns, bool xLog, std::vector<double>& edges)
{
    edges.clear();
    columns = std::max(columns, 1);
    if (!(xMax > xMin) || !std::isfinite(xMin) || !std::isfinite(xMax)) {
        return;
    }
    edges.resize(static_cast<std::size_t>(columns) + 1);
    if (xLog) {
        if (!(xMin > 0.0) || !(xMax > 0.0)) {
            edges.clear();
            return;
        }
        const double u0 = std::log2(xMin);
        const double u1 = std::log2(xMax);
        for (int i = 0; i <= columns; ++i) {
            const double u = u0 + (u1 - u0) * (static_cast<double>(i) / static_cast<double>(columns));
            edges[static_cast<std::size_t>(i)] = std::exp2(u);
        }
        return;
    }
    for (int i = 0; i <= columns; ++i) {
        edges[static_cast<std::size_t>(i)] =
            xMin + (xMax - xMin) * (static_cast<double>(i) / static_cast<double>(columns));
    }
}

int columnOf(std::span<const double> edges, double x)
{
    // Public helper: no linear/log flag, so bisect. The hot path inside
    // rasterColumns uses columnIndex with division on a linear axis.
    if (edges.size() < 2 || !std::isfinite(x) || x < edges.front() || x > edges.back()) {
        return -1;
    }
    if (x == edges.back()) {
        return static_cast<int>(edges.size()) - 2;
    }
    const auto it = std::upper_bound(edges.begin(), edges.end(), x);
    const int c = static_cast<int>(it - edges.begin()) - 1;
    return std::clamp(c, 0, static_cast<int>(edges.size()) - 2);
}

bool rasterColumns(const LinePyramid& pyramid, const double* raw, long long count,
                   std::span<const double> times, double indexStart, double indexStep, double xMin,
                   double xMax, int columns, bool xLog, ColumnStroke& out)
{
    out.values.clear();
    out.xs.clear();
    out.summarised = false;
    if (pyramid.empty() || columns < 1 || !std::isfinite(indexStart) || !std::isfinite(indexStep) ||
        !(std::abs(indexStep) > 0.0)) {
        return false;
    }
    if (count <= 0) {
        count = pyramid.length;
    }
    if (!times.empty() && static_cast<long long>(times.size()) != count) {
        return false;
    }

    std::vector<double> edges;
    columnEdges(xMin, xMax, columns, xLog, edges);
    if (edges.size() != static_cast<std::size_t>(columns) + 1) {
        return false;
    }

    const double* row = rawRow(pyramid, raw, count);
    const double span = edges.back() - edges.front();
    const double invWidth =
        (!xLog && span > 0.0) ? static_cast<double>(columns) / span : 0.0;

    auto appendSample = [&](long long i) {
        double y = 0.0;
        if (!yAt(pyramid, row, count, i, y)) {
            return;
        }
        emitPoint(out, sampleX(times, i, indexStart, indexStep), y);
    };

    // Negative index step: x decreases with i. Column ranges are still
    // ascending in x, so map each sample by columnIndex instead of inverting
    // the affine. Rare (stated axes run forward); keep it correct, not hot.
    if (times.empty() && indexStep < 0.0) {
        struct ColumnInfo
        {
            long long first = -1;
            long long last = -1;
            int samples = 0;
        };
        std::vector<ColumnInfo> cols(static_cast<std::size_t>(columns));
        for (long long i = 0; i < count; ++i) {
            const double x = sampleX(times, i, indexStart, indexStep);
            const int c = columnIndex(edges, x, xLog, edges.front(), invWidth, columns);
            if (c < 0) {
                continue;
            }
            ColumnInfo& col = cols[static_cast<std::size_t>(c)];
            if (col.first < 0) {
                col.first = i;
            }
            col.last = i;
            ++col.samples;
        }
        for (int c = 0; c < columns; ++c) {
            const ColumnInfo& col = cols[static_cast<std::size_t>(c)];
            if (col.samples <= 0 || col.first < 0) {
                continue;
            }
            if (col.samples == 1) {
                appendSample(col.first);
                continue;
            }
            emitSummarised(pyramid, row, count, times, indexStart, indexStep, edges, xLog, invWidth,
                           columns, c, col.first, col.last + 1, out);
        }
        return !out.values.empty();
    }

    // Per column: index range by bisect/arithmetic, then extrema from the
    // pyramid (wide) or a short raw run (narrow). O(columns · log n), not
    // O(visible samples).
    for (int c = 0; c < columns; ++c) {
        const double t0 = edges[static_cast<std::size_t>(c)];
        const double t1 = edges[static_cast<std::size_t>(c) + 1];
        const bool lastCol = (c + 1 == columns);

        const long long first = firstAtLeast(times, count, indexStart, indexStep, t0);
        const long long lastEx = lastCol ? firstAfter(times, count, indexStart, indexStep, t1)
                                         : firstAtLeast(times, count, indexStart, indexStep, t1);
        if (lastEx <= first) {
            continue;
        }
        if (lastEx - first == 1) {
            appendSample(first);
            continue;
        }
        emitSummarised(pyramid, row, count, times, indexStart, indexStep, edges, xLog, invWidth,
                       columns, c, first, lastEx, out);
    }

    return !out.values.empty();
}

} // namespace gui
