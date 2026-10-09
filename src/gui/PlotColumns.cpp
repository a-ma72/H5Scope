// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#include "gui/PlotColumns.hpp"

#include "gui/PlotLevels.hpp"

#include <algorithm>
#include <cmath>

namespace gui {
namespace {

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

Extremes extremesIn(const LinePyramid& pyramid, const double* raw, long long count, long long first,
                    long long last)
{
    first = std::max(first, 0LL);
    last = std::min(last, count > 0 ? count : pyramid.length);
    if (first >= last) {
        return {};
    }
    if (raw != nullptr) {
        return extremesOf(raw, first, last);
    }
    return extremesOver(pyramid, first, last);
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
    // Coarse base without raw: the bucket that holds i is the only answer.
    const Extremes hit = extremesOver(pyramid, i, i + 1);
    if (!hit.found()) {
        return false;
    }
    out = hit.first();
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
    if (edges.size() < 2 || !std::isfinite(x) || x < edges.front() || x > edges.back()) {
        return -1;
    }
    // The last edge is inclusive so a sample sitting on xMax (the usual full
    // view for an index axis) still belongs to the last column.
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

    struct ColumnInfo
    {
        long long first = -1;
        long long last = -1;
        int samples = 0;
        Extremes extremes;
    };
    std::vector<ColumnInfo> cols(static_cast<std::size_t>(columns));

    // Walk every sample once. O(n + columns).
    for (long long i = 0; i < count; ++i) {
        const double x = sampleX(times, i, indexStart, indexStep);
        if (!std::isfinite(x)) {
            continue;
        }
        const int c = columnOf(edges, x);
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

    // Extrema per column: raw walk when held, else the pyramid over the run.
    for (ColumnInfo& col : cols) {
        if (col.samples < 2 || col.first < 0) {
            continue;
        }
        col.extremes = extremesIn(pyramid, row, count, col.first, col.last + 1);
    }

    auto appendSample = [&](long long i) {
        double y = 0.0;
        if (!yAt(pyramid, row, count, i, y)) {
            return;
        }
        emitPoint(out, sampleX(times, i, indexStart, indexStep), y);
    };

    for (int c = 0; c < columns; ++c) {
        const ColumnInfo& col = cols[static_cast<std::size_t>(c)];
        const double t0 = edges[static_cast<std::size_t>(c)];
        const double t1 = edges[static_cast<std::size_t>(c) + 1];

        if (col.samples <= 0 || col.first < 0) {
            continue;
        }

        if (col.samples == 1) {
            // Zero or one sample per column: polyline through the sample.
            appendSample(col.first);
            continue;
        }

        // Several samples: entry → extrema (occurrence order) → exit.
        out.summarised = true;
        if (!col.extremes.found()) {
            continue;
        }

        const long long at0 = col.extremes.firstAt();
        const long long at1 = col.extremes.secondAt();
        const double xExt0 = sampleX(times, at0, indexStart, indexStep);
        const double xExt1 = sampleX(times, at1, indexStart, indexStep);

        const long long prev = col.first - 1;
        if (prev >= 0) {
            double yPrev = 0.0;
            if (yAt(pyramid, row, count, prev, yPrev)) {
                const double xPrev = sampleX(times, prev, indexStart, indexStep);
                const int cPrev = columnOf(edges, xPrev);
                // More than one column between samples → plain chord; no edge
                // entry. Adjacent / off-pane → entry from prev toward the first
                // extreme, so the previous column follows the true slope.
                if (cPrev < 0 || c - cPrev <= 1) {
                    emitPoint(out, t0, lerpY(xPrev, yPrev, xExt0, col.extremes.first(), t0));
                }
            }
        }

        emitPoint(out, xExt0, col.extremes.first());
        if (at0 != at1) {
            emitPoint(out, xExt1, col.extremes.second());
        }

        const long long next = col.last + 1;
        if (next < count) {
            double yNext = 0.0;
            if (yAt(pyramid, row, count, next, yNext)) {
                const double xNext = sampleX(times, next, indexStart, indexStep);
                const int cNext = columnOf(edges, xNext);
                if (cNext < 0 || cNext - c <= 1) {
                    emitPoint(out, t1, lerpY(xExt1, col.extremes.second(), xNext, yNext, t1));
                }
            }
        }
    }

    return !out.values.empty();
}

} // namespace gui
