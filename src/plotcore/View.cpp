// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/View.hpp"

#include <algorithm>
#include <cmath>

namespace gui {
namespace {

bool usableBase(double base)
{
    return std::isfinite(base) && base > 1.0;
}

} // namespace

void PlotCamera::setDataExtent(double xMin, double xMax, double yMin, double yMax,
                               double xPositiveMin, double yPositiveMin)
{
    dataXMin_ = xMin;
    dataXMax_ = xMax;
    dataYMin_ = yMin;
    dataYMax_ = yMax;
    dataXPositive_ = xPositiveMin;
    dataYPositive_ = yPositiveMin;
}

void PlotCamera::setXLog(bool on)
{
    if (xLog_ == on) {
        return;
    }
    xLog_ = on;
    reset();
}

void PlotCamera::setYLog(bool on)
{
    if (yLog_ == on) {
        return;
    }
    yLog_ = on;
    reset();
}

void PlotCamera::setXLogBase(double base)
{
    xLogBase_ = base;
    if (xLog_) {
        reset();
    }
}

void PlotCamera::setYLogBase(double base)
{
    yLogBase_ = base;
    if (yLog_) {
        reset();
    }
}

void PlotCamera::reset()
{
    zoomX_ = 1.0;
    zoomY_ = 1.0;
    panX_ = 0.0;
    panY_ = 0.0;
}

PlotCamera::Span PlotCamera::padded(double low, double high, bool logarithmic, double base)
{
    if (logarithmic) {
        if (!(low > 0.0) || !(high > 0.0) || !usableBase(base)) {
            return {1.0, usableBase(base) ? base : 10.0};
        }
        double from = logOf(low, base);
        double to = logOf(high, base);
        if (!(to > from)) {
            const double at = std::abs(from - std::round(from)) < 1e-10 ? std::round(from) : from;
            const bool exact = std::abs(at - std::round(at)) < 1e-10;
            from = exact ? at - 1.0 : std::floor(at);
            to = exact ? at + 1.0 : std::ceil(at);
        }
        const double air = (to - from) * 0.05;
        return {std::pow(base, from - air), std::pow(base, to + air)};
    }
    const double air = high > low ? (high - low) * 0.05 : 1.0;
    return {low - air, high + air};
}

PlotCamera::Span PlotCamera::paddedX() const
{
    if (xLog_) {
        return padded(dataXPositive_, dataXMax_, true, xLogBase_);
    }
    return padded(dataXMin_, dataXMax_, false, xLogBase_);
}

PlotCamera::Span PlotCamera::paddedY() const
{
    if (yLog_) {
        return padded(dataYPositive_, dataYMax_, true, yLogBase_);
    }
    return padded(dataYMin_, dataYMax_, false, yLogBase_);
}

double PlotCamera::axisPosition(double value, bool logarithmic, double base) const
{
    return logarithmic ? logOf(value, base) : value;
}

double PlotCamera::axisValue(double position, bool logarithmic, double base) const
{
    return logarithmic ? std::pow(base, position) : position;
}

double PlotCamera::clampPan(double pan, double zoom, double low, double high, bool logarithmic,
                            double base) const
{
    const double full = axisPosition(high, logarithmic, base) - axisPosition(low, logarithmic, base);
    const double span = full / zoom;
    const double room = full / 2.0 + span * (0.5 - kPanKeep);
    return std::clamp(pan, -room, room);
}

double PlotCamera::viewMinX() const
{
    const Span axis = paddedX();
    const double from = axisPosition(axis.low, xLog_, xLogBase_);
    const double to = axisPosition(axis.high, xLog_, xLogBase_);
    const double span = (to - from) / zoomX_;
    return axisValue((from + to) / 2.0 + panX_ - span / 2.0, xLog_, xLogBase_);
}

double PlotCamera::viewMaxX() const
{
    const Span axis = paddedX();
    const double from = axisPosition(axis.low, xLog_, xLogBase_);
    const double to = axisPosition(axis.high, xLog_, xLogBase_);
    const double span = (to - from) / zoomX_;
    return axisValue((from + to) / 2.0 + panX_ + span / 2.0, xLog_, xLogBase_);
}

double PlotCamera::viewMinY() const
{
    const Span axis = paddedY();
    const double from = axisPosition(axis.low, yLog_, yLogBase_);
    const double to = axisPosition(axis.high, yLog_, yLogBase_);
    const double span = (to - from) / zoomY_;
    return axisValue((from + to) / 2.0 + panY_ - span / 2.0, yLog_, yLogBase_);
}

double PlotCamera::viewMaxY() const
{
    const Span axis = paddedY();
    const double from = axisPosition(axis.low, yLog_, yLogBase_);
    const double to = axisPosition(axis.high, yLog_, yLogBase_);
    const double span = (to - from) / zoomY_;
    return axisValue((from + to) / 2.0 + panY_ + span / 2.0, yLog_, yLogBase_);
}

void PlotCamera::zoomedAxis(double& zoom, double& pan, double low, double high, double fraction,
                            double factor, bool logarithmic, double base, double minimumSpan)
{
    const double from = axisPosition(low, logarithmic, base);
    const double to = axisPosition(high, logarithmic, base);
    const double full = to - from;
    const double span = full / zoom;
    const double held = (from + to) / 2.0 + pan - span / 2.0 + fraction * span;
    double ceiling = maxZoom();
    if (logarithmic && minimumSpan > 0.0 && usableBase(base) && full > 0.0) {
        const auto width = [&](double nextSpan) {
            return std::pow(base, held + (1.0 - fraction) * nextSpan) -
                   std::pow(base, held - fraction * nextSpan);
        };
        if (!(width(full) > minimumSpan)) {
            ceiling = 1.0;
        } else {
            double narrow = 0.0;
            double wide = full;
            for (int i = 0; i < 60; ++i) {
                const double mid = (narrow + wide) / 2.0;
                if (width(mid) >= minimumSpan) {
                    wide = mid;
                } else {
                    narrow = mid;
                }
            }
            ceiling = full / wide;
        }
    }
    const double next = std::max(1.0, std::min(std::max(ceiling, zoom), zoom * factor));
    const double nextSpan = full / next;
    const double centre = held - fraction * nextSpan + nextSpan / 2.0;
    zoom = next;
    pan = clampPan(centre - (from + to) / 2.0, next, low, high, logarithmic, base);
}

void PlotCamera::zoomAt(double fx, double fy, double factor, bool onlyX, bool onlyY)
{
    fx = std::clamp(fx, 0.0, 1.0);
    fy = std::clamp(fy, 0.0, 1.0);
    const Span xAxis = paddedX();
    const Span yAxis = paddedY();
    if (!onlyY) {
        zoomedAxis(zoomX_, panX_, xAxis.low, xAxis.high, fx, factor, xLog_, xLogBase_,
                   minimumSpanX());
    }
    if (!onlyX) {
        zoomedAxis(zoomY_, panY_, yAxis.low, yAxis.high, fy, factor, yLog_, yLogBase_, 0.0);
    }
}

void PlotCamera::panBy(double dx, double dy, double areaWidth, double areaHeight)
{
    if (!(areaWidth > 0.0) || !(areaHeight > 0.0)) {
        return;
    }
    const Span xAxis = paddedX();
    const Span yAxis = paddedY();
    const double spanX =
        (axisPosition(xAxis.high, xLog_, xLogBase_) - axisPosition(xAxis.low, xLog_, xLogBase_)) /
        zoomX_;
    const double spanY =
        (axisPosition(yAxis.high, yLog_, yLogBase_) - axisPosition(yAxis.low, yLog_, yLogBase_)) /
        zoomY_;
    panX_ = clampPan(panX_ - dx * spanX / areaWidth, zoomX_, xAxis.low, xAxis.high, xLog_,
                     xLogBase_);
    panY_ = clampPan(panY_ + dy * spanY / areaHeight, zoomY_, yAxis.low, yAxis.high, yLog_,
                     yLogBase_);
}

double PlotCamera::maxZoom() const
{
    const double length = std::abs(dataXMax_ - dataXMin_) + 1.0;
    return std::max(256.0, length / 16.0);
}

double PlotCamera::minimumSpanX() const
{
    return std::abs(paddedX().high - paddedX().low) / maxZoom();
}

PlotView PlotCamera::frame(double width, double height, double pixelRatio, int lineCount) const
{
    PlotView view;
    view.xMin = viewMinX();
    view.xMax = viewMaxX();
    view.yMin = viewMinY();
    view.yMax = viewMaxY();
    view.xLog = xLog_;
    view.yLog = yLog_;
    view.xLogBase = xLogBase_;
    view.yLogBase = yLogBase_;
    view.width = width;
    view.height = height;
    view.pixelRatio = pixelRatio;
    const int lines = std::max(lineCount, 1);
    view.maxColumns = std::max(1, kMaxVertices / (4 * lines));
    return view;
}

} // namespace gui
