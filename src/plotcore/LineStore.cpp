// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/LineStore.hpp"

#include "gui/PlotLevels.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace gui {
namespace {

const QColor kCycle[] = {
    QColor(230, 159, 0),   QColor(86, 180, 233),  QColor(0, 158, 115), QColor(240, 228, 66),
    QColor(0, 114, 178),   QColor(213, 94, 0),    QColor(204, 121, 167),
};

QColor colourAt(int index, const QColor& given)
{
    if (given.isValid()) {
        return given;
    }
    return kCycle[static_cast<std::size_t>(index) % (sizeof(kCycle) / sizeof(kCycle[0]))];
}

} // namespace

void LineStore::emitChanged()
{
    if (onChanged) {
        onChanged();
    }
}

int LineStore::addLine(const double* values, qsizetype count, const QColor& colour)
{
    if (values == nullptr || count <= 0) {
        return -1;
    }

    Entry entry;
    entry.values = values;
    entry.count = count;
    entry.colour = colourAt(nextColour_++, colour);
    const long long length = static_cast<long long>(count);
    const long long base = baseBucketFor(length, kHoldDoubles);
    entry.pyramid = pyramidOf(values, length, base);
    lines_.push_back(std::move(entry));

    length_ = 0;
    for (const Entry& line : lines_) {
        length_ = std::max(length_, static_cast<long long>(line.count));
    }
    cap_ = pointsFor();
    rebuildWhole(lines_.back());
    recount();
    emitChanged();
    return static_cast<int>(lines_.size()) - 1;
}

void LineStore::clearLines()
{
    lines_.clear();
    retired_.clear();
    length_ = 0;
    minimum_ = 0.0;
    maximum_ = 1.0;
    positiveMinimum_ = 0.0;
    hasPositive_ = false;
    nextColour_ = 0;
    emitChanged();
}

void LineStore::setPaneColumns(int columns)
{
    const int quantised =
        std::max(kColumnQuantum, (std::max(columns, 1) / kColumnQuantum) * kColumnQuantum);
    if (quantised == columns_) {
        return;
    }
    columns_ = quantised;
    const int cap = pointsFor();
    if (cap == cap_) {
        return;
    }
    cap_ = cap;
    for (Entry& entry : lines_) {
        if (!entry.whole.empty()) {
            retired_.push_back(std::move(entry.whole));
        }
        if (!entry.closer.empty()) {
            retired_.push_back(std::move(entry.closer));
        }
        entry.closerValid = false;
        rebuildWhole(entry);
    }
    emitChanged();
}

void LineStore::setVisibleRange(double xMin, double xMax)
{
    if (viewMin_ == xMin && viewMax_ == xMax) {
        return;
    }
    const auto previous = [this]() -> std::optional<PlotWindow> {
        if (lines_.empty() || !lines_.front().closerValid) {
            return std::nullopt;
        }
        return lines_.front().closerWindow;
    };
    const std::optional<PlotWindow> before = previous();
    viewMin_ = xMin;
    viewMax_ = xMax;
    refreshCloser();
    const std::optional<PlotWindow> after = previous();
    if (after != before) {
        emitChanged();
    }
}

void LineStore::fillInto(std::vector<PlotLine>& lines, PlotAxis& axis)
{
    lines.clear();
    lines.reserve(lines_.size());
    for (const Entry& entry : lines_) {
        lines.push_back(lineOf(entry));
    }
    axis = PlotAxis{};
    axis.start = 0.0;
    axis.step = 1.0;
}

void LineStore::releaseRetired()
{
    retired_.clear();
}

int LineStore::pointsFor() const
{
    const int pane = 2 * columns_;
    const int lines = std::max(lineCount(), 1);
    return std::clamp(std::min(kDrawBudget / lines, pane), kMinPoints, kMaxPoints);
}

void LineStore::rebuildWhole(Entry& entry)
{
    long long stride = 1;
    double step = 1.0;
    if (!fillWhole(entry.pyramid, cap_ / 2, entry.whole, stride, step)) {
        entry.whole.clear();
        entry.wholeStride = 1;
        entry.wholeStep = 1.0;
        entry.wholeSummarised = false;
        return;
    }
    entry.wholeStride = stride;
    entry.wholeStep = step;
    entry.wholeSummarised = stride > 1;
}

void LineStore::refreshCloser()
{
    if (lines_.empty() || length_ <= 0) {
        return;
    }
    const std::optional<PlotWindow> wanted = windowFor(viewMin_, viewMax_, length_, cap_ / 2);
    for (Entry& entry : lines_) {
        if (!wanted.has_value()) {
            if (entry.closerValid) {
                retired_.push_back(std::move(entry.closer));
                entry.closerValid = false;
            }
            continue;
        }
        if (entry.closerValid && entry.closerWindow == *wanted) {
            continue;
        }
        std::vector<double> folded;
        if (!fillWindow(entry.pyramid, *wanted, folded)) {
            if (entry.closerValid) {
                retired_.push_back(std::move(entry.closer));
                entry.closerValid = false;
            }
            continue;
        }
        if (entry.closerValid) {
            retired_.push_back(std::move(entry.closer));
        }
        entry.closer = std::move(folded);
        entry.closerWindow = *wanted;
        entry.closerStep = wanted->bucket == 1 ? 1.0 : static_cast<double>(wanted->bucket) / 2.0;
        entry.closerValid = true;
    }
}

void LineStore::recount()
{
    minimum_ = 0.0;
    maximum_ = 1.0;
    positiveMinimum_ = 0.0;
    hasPositive_ = false;
    bool any = false;
    for (const Entry& entry : lines_) {
        const Extremes extremes = extremesOver(entry.pyramid, 0, entry.pyramid.length);
        if (!extremes.found()) {
            continue;
        }
        if (!any) {
            minimum_ = extremes.lowest;
            maximum_ = extremes.highest;
            any = true;
        } else {
            minimum_ = std::min(minimum_, extremes.lowest);
            maximum_ = std::max(maximum_, extremes.highest);
        }
        double positive = 0.0;
        if (smallestPositive(entry.pyramid, positive)) {
            positiveMinimum_ = hasPositive_ ? std::min(positiveMinimum_, positive) : positive;
            hasPositive_ = true;
        }
    }
    if (!any) {
        minimum_ = 0.0;
        maximum_ = 1.0;
    }
}

PlotLine LineStore::lineOf(const Entry& entry) const
{
    PlotLine line;
    line.colour = entry.colour;
    if (entry.closerValid && !entry.closer.empty()) {
        line.values = entry.closer.data();
        line.count = static_cast<qsizetype>(entry.closer.size());
        line.positionStart = static_cast<double>(entry.closerWindow.first);
        line.positionStep = entry.closerStep;
        line.summarised = entry.closerWindow.bucket > 1;
        return line;
    }
    if (entry.whole.empty()) {
        return line;
    }
    line.values = entry.whole.data();
    line.count = static_cast<qsizetype>(entry.whole.size());
    line.positionStep = entry.wholeStep;
    line.summarised = entry.wholeSummarised;
    return line;
}

double LineStore::xPositiveMinimum() const
{
    return length_ > 1 ? 1.0 : 0.0;
}

std::span<const double> LineStore::drawnValues(int index) const
{
    if (index < 0 || index >= lineCount()) {
        return {};
    }
    const Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (entry.closerValid && !entry.closer.empty()) {
        return entry.closer;
    }
    return entry.whole;
}

} // namespace gui
