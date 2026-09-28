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

/// Which drawn point of one reading of a time base `x` falls on.
///
/// The same question as CustomPlot's positionOfX. A time base that only ever
/// goes one way is a map that can be run backwards; one that doubles back is
/// not, and `false` says so. The order is asked of this array, not of the
/// time base as a whole, because a summary can ascend while the elements
/// under one of its buckets do not.
bool positionOfX(const std::vector<double>& values, double start, double step, double x,
                 double& position)
{
    if (values.empty() || !(step > 0.0) || !std::isfinite(x)) {
        return false;
    }
    std::size_t at = 0;
    if (std::is_sorted(values.begin(), values.end())) {
        at = static_cast<std::size_t>(std::lower_bound(values.begin(), values.end(), x) -
                                      values.begin());
    } else if (std::is_sorted(values.rbegin(), values.rend())) {
        at = static_cast<std::size_t>(
            std::lower_bound(values.begin(), values.end(), x, std::greater<>{}) - values.begin());
    } else {
        return false;
    }
    position = start + static_cast<double>(std::min(at, values.size() - 1)) * step;
    return std::isfinite(position);
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
    adopt(lines_.back());
    return static_cast<int>(lines_.size()) - 1;
}

int LineStore::beginLine(long long count, const QColor& colour)
{
    if (count <= 0) {
        return -1;
    }
    Entry entry;
    entry.count = static_cast<qsizetype>(count);
    entry.colour = colourAt(nextColour_++, colour);
    entry.building = std::make_unique<PyramidBuilder>(count, baseBucketFor(count, kHoldDoubles));
    lines_.push_back(std::move(entry));
    return static_cast<int>(lines_.size()) - 1;
}

void LineStore::addSamples(int index, const double* values, long long count)
{
    if (index < 0 || index >= lineCount() || values == nullptr || count <= 0) {
        return;
    }
    Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (entry.building == nullptr) {
        return;
    }
    entry.building->add(values, count);
}

void LineStore::finishLine(int index)
{
    if (index < 0 || index >= lineCount()) {
        return;
    }
    Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (entry.building == nullptr) {
        return;
    }
    entry.pyramid = entry.building->finish();
    entry.building.reset();
    entry.count = static_cast<qsizetype>(entry.pyramid.length);
    adopt(entry);
}

void LineStore::setWindowReader(int index, WindowReader reader, void* user)
{
    if (index < 0 || index >= lineCount()) {
        return;
    }
    Entry& entry = lines_[static_cast<std::size_t>(index)];
    entry.reader = reader;
    entry.readerUser = user;
}

void LineStore::setOwnAxis(int index, bool on)
{
    if (index < 0 || index >= lineCount()) {
        return;
    }
    Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (entry.ownAxis == on) {
        return;
    }
    entry.ownAxis = on;
    recount();
    emitChanged();
}

bool LineStore::ownAxis(int index) const
{
    if (index < 0 || index >= lineCount()) {
        return false;
    }
    return lines_[static_cast<std::size_t>(index)].ownAxis;
}

bool LineStore::lineExtent(int index, double& low, double& high) const
{
    if (index < 0 || index >= lineCount()) {
        return false;
    }
    const Entry& entry = lines_[static_cast<std::size_t>(index)];
    if (!entry.finite) {
        return false;
    }
    low = entry.low;
    high = entry.high;
    return true;
}

void LineStore::dropAxis()
{
    if (!axis_.whole.empty()) {
        retired_.push_back(std::move(axis_.whole));
    }
    if (!axis_.closer.empty()) {
        retired_.push_back(std::move(axis_.closer));
    }
    axis_ = Entry{};
    hasAxis_ = false;
    // The units just changed, or went away. The window in hand was in the
    // old ones, so the next range is applied rather than matched.
    viewMax_ = viewMin_ - 1.0;
    asked_ = false;
}

void LineStore::acceptAxis()
{
    if (axis_.pyramid.length < 2) {
        hasAxis_ = false;
        emitChanged();
        return;
    }
    hasAxis_ = true;
    rebuildWhole(axis_);
    const Extremes extremes = extremesOver(axis_.pyramid, 0, axis_.pyramid.length);
    axis_.finite = extremes.found();
    if (axis_.finite) {
        axis_.low = extremes.lowest;
        axis_.high = extremes.highest;
    }
    emitChanged();
}

void LineStore::setAxis(const double* values, qsizetype count)
{
    dropAxis();
    if (values == nullptr || count < 2) {
        emitChanged();
        return;
    }
    axis_.values = values;
    axis_.count = count;
    const long long length = static_cast<long long>(count);
    axis_.pyramid = pyramidOf(values, length, baseBucketFor(length, kHoldDoubles));
    acceptAxis();
}

void LineStore::beginAxis(long long count)
{
    dropAxis();
    if (count < 2) {
        emitChanged();
        return;
    }
    axis_.count = static_cast<qsizetype>(count);
    axis_.building = std::make_unique<PyramidBuilder>(count, baseBucketFor(count, kHoldDoubles));
    emitChanged();
}

void LineStore::addAxisSamples(const double* values, long long count)
{
    if (axis_.building == nullptr || values == nullptr || count <= 0) {
        return;
    }
    axis_.building->add(values, count);
}

void LineStore::finishAxis()
{
    if (axis_.building == nullptr) {
        return;
    }
    axis_.pyramid = axis_.building->finish();
    axis_.building.reset();
    axis_.count = static_cast<qsizetype>(axis_.pyramid.length);
    axis_.values = nullptr;
    acceptAxis();
}

void LineStore::setAxisReader(WindowReader reader, void* user)
{
    axis_.reader = reader;
    axis_.readerUser = user;
}

void LineStore::clearLines()
{
    lines_.clear();
    axis_ = Entry{};
    hasAxis_ = false;
    retired_.clear();
    length_ = 0;
    minimum_ = 0.0;
    maximum_ = 1.0;
    positiveMinimum_ = 0.0;
    hasPositive_ = false;
    shared_ = 0;
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
    if (hasAxis_) {
        if (!axis_.whole.empty()) {
            retired_.push_back(std::move(axis_.whole));
        }
        if (!axis_.closer.empty()) {
            retired_.push_back(std::move(axis_.closer));
        }
        axis_.closerValid = false;
        rebuildWhole(axis_);
    }
    // The range has not moved, but the fold it was answered with has. The
    // next call has to run again rather than recognise the same window.
    asked_ = false;
    emitChanged();
}

void LineStore::setVisibleRange(double xMin, double xMax)
{
    // The same window, already turned into positions. Asking again would
    // re-read a time base that is not in memory, on a frame that did not move.
    if (asked_ && xMin == askedMin_ && xMax == askedMax_) {
        return;
    }
    const double askedMin = xMin;
    const double askedMax = xMax;
    double low = xMin;
    double high = xMax;
    if (hasAxis_) {
        double first = 0.0;
        double second = 0.0;
        double firstResolution = 1.0;
        double secondResolution = 1.0;
        if (positionOf(axis_, xMin, first, firstResolution) &&
            positionOf(axis_, xMax, second, secondResolution)) {
            if (first > second) {
                std::swap(first, second);
                std::swap(firstResolution, secondResolution);
            }
            // Opened by the resolution each end was settled at, so the run
            // does not stop short of a point the pane is still drawing.
            low = first - firstResolution;
            high = second + secondResolution;
        } else {
            // Not a map. The summary is drawn against the times it has, and
            // a zoom stretches it rather than being refused.
            low = 0.0;
            high = static_cast<double>(std::max(axis_.pyramid.length, length_));
        }
    }
    if (viewMin_ == low && viewMax_ == high) {
        asked_ = true;
        askedMin_ = askedMin;
        askedMax_ = askedMax;
        return;
    }
    xMin = low;
    xMax = high;
    const auto previous = [this]() -> std::optional<PlotWindow> {
        if (lines_.empty() || !lines_.front().closerValid) {
            return std::nullopt;
        }
        return lines_.front().closerWindow;
    };
    const std::optional<PlotWindow> before = previous();
    const bool axisBefore = hasAxis_ && axis_.closerValid;
    const PlotWindow axisWindowBefore = axis_.closerWindow;
    viewMin_ = xMin;
    viewMax_ = xMax;
    asked_ = true;
    askedMin_ = askedMin;
    askedMax_ = askedMax;
    refreshCloser();
    const std::optional<PlotWindow> after = previous();
    const bool axisAfter = hasAxis_ && axis_.closerValid;
    if (after != before || axisBefore != axisAfter ||
        (axisAfter && axis_.closerWindow != axisWindowBefore)) {
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
    if (!hasAxis_ || axis_.whole.empty()) {
        return;
    }
    axis.values = axis_.whole.data();
    axis.count = static_cast<qsizetype>(axis_.whole.size());
    axis.valueStep = axis_.wholeStep;
    if (axis_.closerValid && !axis_.closer.empty()) {
        axis.closerValues = axis_.closer.data();
        axis.closerCount = static_cast<qsizetype>(axis_.closer.size());
        axis.closerStart = static_cast<double>(axis_.closerWindow.first);
        axis.closerStep = axis_.closerStep;
    }
}

void LineStore::releaseRetired()
{
    retired_.clear();
}

void LineStore::adopt(Entry& entry)
{
    length_ = 0;
    for (const Entry& line : lines_) {
        if (line.building != nullptr) {
            continue;
        }
        length_ = std::max(length_, static_cast<long long>(line.count));
    }
    cap_ = pointsFor();
    rebuildWhole(entry);
    recount();
    emitChanged();
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
    const long long domain = hasAxis_ && axis_.pyramid.length > 0 ? axis_.pyramid.length : length_;
    const std::optional<PlotWindow> wanted = windowFor(viewMin_, viewMax_, domain, cap_ / 2);
    for (Entry& entry : lines_) {
        refreshEntry(entry, wanted);
    }
    if (hasAxis_) {
        refreshEntry(axis_, wanted);
    }
}

void LineStore::refreshEntry(Entry& entry, const std::optional<PlotWindow>& wanted)
{
    if (!wanted.has_value()) {
        if (entry.closerValid) {
            retired_.push_back(std::move(entry.closer));
            entry.closerValid = false;
        }
        return;
    }
    if (entry.closerValid && entry.closerWindow == *wanted) {
        return;
    }
    std::vector<double> folded;
    // The pyramid answers every window at or above its base. Below that
    // the samples are not in memory, and only the reader — the file, or
    // the borrowed buffer — still has them. Without one, the whole-line
    // summary stays up.
    if (!fillWindow(entry.pyramid, *wanted, folded) && !readWindow(entry, *wanted, folded)) {
        if (entry.closerValid) {
            retired_.push_back(std::move(entry.closer));
            entry.closerValid = false;
        }
        return;
    }
    if (entry.closerValid) {
        retired_.push_back(std::move(entry.closer));
    }
    entry.closer = std::move(folded);
    entry.closerWindow = *wanted;
    entry.closerStep = wanted->bucket == 1 ? 1.0 : static_cast<double>(wanted->bucket) / 2.0;
    entry.closerValid = true;
}

bool LineStore::readWindow(Entry& entry, const PlotWindow& window, std::vector<double>& folded)
{
    if (window.span <= 0 || window.first < 0) {
        return false;
    }
    const long long end = window.first + window.span;
    std::vector<double> raw;
    if (entry.values != nullptr && end <= static_cast<long long>(entry.count)) {
        // The borrowed buffer is the line. It is what a closer look reads
        // once the pyramid's base is coarser than the window.
        const double* from = entry.values + window.first;
        raw.assign(from, from + window.span);
    } else if (entry.reader != nullptr) {
        raw.resize(static_cast<std::size_t>(window.span));
        if (entry.reader(entry.readerUser, window.first, window.span, raw.data()) == 0) {
            return false;
        }
    } else {
        return false;
    }
    if (window.bucket <= 1) {
        folded = std::move(raw);
        return true;
    }
    folded.clear();
    reduceBuckets(raw.data(), window.span, window.bucket, folded);
    return !folded.empty();
}

bool LineStore::positionOf(Entry& entry, double x, double& position, double& resolution)
{
    if (!positionOfX(entry.whole, 0.0, entry.wholeStep, x, position)) {
        return false;
    }
    resolution = entry.wholeStep;
    const long long length = entry.pyramid.length;
    if (!(resolution > 1.0) || length <= 1 || (entry.values == nullptr && entry.reader == nullptr)) {
        return true;
    }
    // The summary's bracket is as wide as one of its drawn points. On a long
    // line that is thousands of elements, and the run that came back would
    // stay that coarse however far the reader zoomed. The samples around the
    // answer are the sharper reading.
    const auto half = static_cast<long long>(std::ceil(resolution));
    PlotWindow window;
    window.bucket = 1;
    const double origin = std::clamp(position - static_cast<double>(half), 0.0,
                                     static_cast<double>(length - 1));
    window.first = static_cast<long long>(origin);
    window.span = std::min(half * 2, length - window.first);
    std::vector<double> raw;
    if (window.span <= 0 || !readWindow(entry, window, raw)) {
        return true;
    }
    double closer = 0.0;
    if (positionOfX(raw, static_cast<double>(window.first), 1.0, x, closer)) {
        position = closer;
        resolution = 1.0;
    }
    return true;
}

void LineStore::recount()
{
    minimum_ = 0.0;
    maximum_ = 1.0;
    positiveMinimum_ = 0.0;
    hasPositive_ = false;
    shared_ = 0;
    bool any = false;
    for (Entry& entry : lines_) {
        const Extremes extremes = extremesOver(entry.pyramid, 0, entry.pyramid.length);
        entry.finite = extremes.found();
        if (entry.finite) {
            entry.low = extremes.lowest;
            entry.high = extremes.highest;
        }
        // A line on its own axis still has an extent. It is not part of the
        // common one, which would otherwise be stretched to fit a line the
        // common axis is no longer drawing.
        if (entry.ownAxis || !entry.finite) {
            continue;
        }
        ++shared_;
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
    if (hasAxis_) {
        double positive = 0.0;
        if (smallestPositive(axis_.pyramid, positive)) {
            return positive;
        }
        return 0.0;
    }
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
