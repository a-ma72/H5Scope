// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/plotcore_c.h"

#include "gui/PlotLevels.hpp"
#include "gui/PlotProjection.hpp"
#include "plotcore/LineStore.hpp"
#include "plotcore/View.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

double niceStep(double span, int target)
{
    if (!(span > 0.0) || !std::isfinite(span)) {
        return 0.0;
    }
    const double raw = span / static_cast<double>(std::max(1, target));
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    if (!(magnitude > 0.0)) {
        return 0.0;
    }
    const double scaled = raw / magnitude;
    const double factor = scaled <= 1.0 ? 1.0 : scaled <= 2.0 ? 2.0 : scaled <= 5.0 ? 5.0 : 10.0;
    return magnitude * factor;
}

void appendTicks(std::vector<H5PlotTick>& ticks, const gui::PlotView& view, bool vertical)
{
    const bool logarithmic = vertical ? view.yLog : view.xLog;
    const double base = vertical ? view.yLogBase : view.xLogBase;
    const double low = vertical ? view.yMin : view.xMin;
    const double high = vertical ? view.yMax : view.xMax;
    const gui::AxisMapping map = gui::mappingOver(low, high, logarithmic, base);
    if (!map.usable || !(view.width > 0.0) || !(view.height > 0.0)) {
        return;
    }

    std::vector<double> values;
    if (logarithmic && low > 0.0 && high > low) {
        const double from = gui::logOf(low, base);
        const double to = gui::logOf(high, base);
        const int first = static_cast<int>(std::ceil(from - 1e-9));
        const int last = static_cast<int>(std::floor(to + 1e-9));
        const int count = last - first + 1;
        if (count >= 2) {
            const int stride = std::max(1, (count + 5) / 6);
            for (int k = first; k <= last; k += stride) {
                values.push_back(std::pow(base, static_cast<double>(k)));
            }
        }
    }
    if (values.empty()) {
        const double step = niceStep(high - low, 6);
        if (!(step > 0.0)) {
            return;
        }
        const double first = std::ceil(low / step - 1e-9) * step;
        for (double value = first; value <= high + step * 0.5 && values.size() < 12; value += step) {
            if (std::isfinite(value)) {
                values.push_back(value);
            }
        }
    }

    for (double value : values) {
        if (!map.draws(value)) {
            continue;
        }
        const double fraction = map.fractionOf(value);
        if (fraction < -0.001 || fraction > 1.001) {
            continue;
        }
        H5PlotTick tick{};
        tick.value = value;
        tick.axis = vertical ? 1 : 0;
        if (vertical) {
            tick.x = 0.0;
            tick.y = view.height - fraction * view.height;
        } else {
            tick.x = fraction * view.width;
            tick.y = view.height;
        }
        ticks.push_back(tick);
    }
}

} // namespace

struct H5Plot
{
    gui::LineStore store;
    gui::PlotCamera camera;
    std::vector<gui::PlotLine> lines;
    gui::PlotAxis axis;
    std::vector<QPointF> points;
    std::vector<gui::PlotRun> runs;
    std::vector<int> lineRuns;
    std::vector<H5PlotRun> painted;
    std::vector<H5PlotTick> ticks;
    int width = 640;
    int height = 400;
    double pixelRatio = 1.0;

    void syncExtent()
    {
        camera.setDataExtent(store.xMin(), store.xMax(), store.minimum(), store.maximum(),
                             store.xPositiveMinimum(), store.positiveMinimum());
    }

    int paneColumns() const
    {
        const int pixels = std::max(1, static_cast<int>(std::lround(width * pixelRatio)));
        return std::max(gui::kColumnQuantum, (pixels / gui::kColumnQuantum) * gui::kColumnQuantum);
    }
};

extern "C" {

H5Plot* h5plot_create(void)
{
    return new H5Plot();
}

void h5plot_destroy(H5Plot* plot)
{
    delete plot;
}

int h5plot_add_line(H5Plot* plot, const double* y, long long n, int red, int green, int blue)
{
    if (plot == nullptr) {
        return -1;
    }
    const int index = plot->store.addLine(y, static_cast<qsizetype>(n), QColor(red, green, blue));
    plot->syncExtent();
    plot->camera.reset();
    return index;
}

void h5plot_clear(H5Plot* plot)
{
    if (plot == nullptr) {
        return;
    }
    plot->store.clearLines();
    plot->lines.clear();
    plot->points.clear();
    plot->runs.clear();
    plot->painted.clear();
    plot->ticks.clear();
}

void h5plot_set_pane(H5Plot* plot, int width, int height, double pixel_ratio)
{
    if (plot == nullptr) {
        return;
    }
    plot->width = std::max(1, width);
    plot->height = std::max(1, height);
    plot->pixelRatio = pixel_ratio > 0.0 ? pixel_ratio : 1.0;
}

void h5plot_set_ylog(H5Plot* plot, int on)
{
    if (plot != nullptr) {
        plot->camera.setYLog(on != 0);
    }
}

void h5plot_reset_view(H5Plot* plot)
{
    if (plot != nullptr) {
        plot->camera.reset();
    }
}

void h5plot_wheel(H5Plot* plot, double px, double py, double factor, int shift, int control)
{
    if (plot == nullptr || plot->width <= 0 || plot->height <= 0) {
        return;
    }
    const double fx = px / static_cast<double>(plot->width);
    const double fy = 1.0 - py / static_cast<double>(plot->height);
    plot->camera.zoomAt(fx, fy, factor, shift != 0 && control == 0, control != 0 && shift == 0);
}

void h5plot_pan(H5Plot* plot, double dx, double dy)
{
    if (plot != nullptr) {
        plot->camera.panBy(dx, dy, plot->width, plot->height);
    }
}

double h5plot_view_min_x(const H5Plot* plot)
{
    return plot != nullptr ? plot->camera.viewMinX() : 0.0;
}

double h5plot_view_max_x(const H5Plot* plot)
{
    return plot != nullptr ? plot->camera.viewMaxX() : 1.0;
}

double h5plot_view_min_y(const H5Plot* plot)
{
    return plot != nullptr ? plot->camera.viewMinY() : 0.0;
}

double h5plot_view_max_y(const H5Plot* plot)
{
    return plot != nullptr ? plot->camera.viewMaxY() : 1.0;
}

int h5plot_project(H5Plot* plot)
{
    if (plot == nullptr) {
        return 0;
    }
    plot->store.setPaneColumns(plot->paneColumns());
    plot->store.setVisibleRange(plot->camera.viewMinX(), plot->camera.viewMaxX());
    plot->store.fillInto(plot->lines, plot->axis);
    plot->syncExtent();

    plot->points.clear();
    plot->runs.clear();
    plot->painted.clear();
    plot->ticks.clear();
    const auto count = plot->lines.size();
    plot->lineRuns.assign(count + 1, 0);
    const gui::PlotView view =
        plot->camera.frame(plot->width, plot->height, plot->pixelRatio, static_cast<int>(count));
    for (std::size_t i = 0; i < count; ++i) {
        plot->lineRuns[i] = static_cast<int>(plot->runs.size());
        gui::projectLine(plot->lines[i], plot->axis, gui::lineView(plot->lines[i], view),
                         plot->points, plot->runs);
        const QColor colour = plot->lines[i].colour;
        const auto alpha = static_cast<unsigned char>(
            std::lround(std::clamp(colour.alphaF() * plot->lines[i].opacity, 0.0, 1.0) * 255.0));
        for (int r = plot->lineRuns[i]; r < static_cast<int>(plot->runs.size()); ++r) {
            const gui::PlotRun& run = plot->runs[static_cast<std::size_t>(r)];
            plot->painted.push_back(H5PlotRun{run.first, run.count, static_cast<unsigned char>(colour.red()),
                                              static_cast<unsigned char>(colour.green()),
                                              static_cast<unsigned char>(colour.blue()), alpha,
                                              static_cast<float>(plot->lines[i].width)});
        }
    }
    plot->lineRuns[count] = static_cast<int>(plot->runs.size());
    appendTicks(plot->ticks, view, false);
    appendTicks(plot->ticks, view, true);
    plot->store.releaseRetired();
    return static_cast<int>(plot->points.size());
}

int h5plot_point_count(const H5Plot* plot)
{
    return plot != nullptr ? static_cast<int>(plot->points.size()) : 0;
}

void h5plot_copy_points(const H5Plot* plot, double* xy)
{
    if (plot == nullptr || xy == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < plot->points.size(); ++i) {
        xy[i * 2] = plot->points[i].x();
        xy[i * 2 + 1] = plot->points[i].y();
    }
}

int h5plot_run_count(const H5Plot* plot)
{
    return plot != nullptr ? static_cast<int>(plot->painted.size()) : 0;
}

void h5plot_copy_runs(const H5Plot* plot, H5PlotRun* runs)
{
    if (plot == nullptr || runs == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < plot->painted.size(); ++i) {
        runs[i] = plot->painted[i];
    }
}

int h5plot_tick_count(const H5Plot* plot)
{
    return plot != nullptr ? static_cast<int>(plot->ticks.size()) : 0;
}

void h5plot_copy_ticks(const H5Plot* plot, H5PlotTick* ticks)
{
    if (plot == nullptr || ticks == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < plot->ticks.size(); ++i) {
        ticks[i] = plot->ticks[i];
    }
}

} // extern "C"
