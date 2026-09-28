// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// A line that is already in RAM, held the way DatasetPlot holds one it read.
//
// The Plot tab's model is a file, a thread and a table. What it *does* with a
// line after the elements are in hand is the same question this answers: build
// a LinePyramid once, fold a whole-line summary out of it, and fold a closer
// look when the reader zooms in. The file and the thread are not here, so a
// numpy array -- or any other contiguous buffer of doubles -- can sit where
// a hyperslab used to.
//
// No QObject: the MinGW DLL that PySide paints through cannot link Qt, and a
// signal would have pulled it in. `onChanged` is an ordinary callback.
//
// `values` is borrowed. The store never copies the raw line; the pyramid is
// the copy, at the finest bucket the hold-budget affords. The owner of those
// doubles must keep them alive until clearLines() or the store is destroyed.

#include "gui/PlotLevels.hpp"
#include "gui/PlotPyramid.hpp"
#include "gui/PlotProjection.hpp"

#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace gui {

class LineStore
{
public:
    static constexpr long long kHoldDoubles = 8 << 20;
    static constexpr int kDrawBudget = 1 << 21;

    /// Called after add/clear/resize/zoom actually changes a fold. Not during
    /// fillInto: that would refill from inside a refill.
    std::function<void()> onChanged;

    /// Borrow `count` doubles as one line. An invalid colour takes the next
    /// entry of a short cycle.
    int addLine(const double* values, qsizetype count, const QColor& colour = {});

    /// A line that arrives in pieces, folded as each piece lands.
    ///
    /// `addLine` wants the whole buffer, because that is what a numpy array
    /// already is. A dataset is not: PyramidBuilder takes each read and keeps
    /// the pyramid, which is the copy, and the raw samples are not held. The
    /// line is absent from the picture until `finishLine`. A closer look finer
    /// than the pyramid's base has nothing left to read — there is no file
    /// behind this store — and the whole-line summary is what is drawn then.
    int beginLine(long long count, const QColor& colour = {});
    void addSamples(int index, const double* values, long long count);
    void finishLine(int index);

    void clearLines();
    void setPaneColumns(int columns);
    void setVisibleRange(double xMin, double xMax);

    /// A line on an axis of its own leaves the common extent. The request is
    /// kept on the line; the common minimum and maximum are only the lines
    /// still on the common axis.
    void setOwnAxis(int index, bool on);
    [[nodiscard]] bool ownAxis(int index) const;
    [[nodiscard]] int sharedCount() const { return shared_; }
    [[nodiscard]] bool lineExtent(int index, double& low, double& high) const;

    /// Write the current folds into `lines` / `axis`. Pointers stay valid
    /// until the next fillInto, clearLines, or a rebuild that retires them --
    /// call releaseRetired() only after the renderer has been handed the new
    /// pointers.
    void fillInto(std::vector<PlotLine>& lines, PlotAxis& axis);
    void releaseRetired();

    [[nodiscard]] int lineCount() const { return static_cast<int>(lines_.size()); }
    [[nodiscard]] int paneColumns() const { return columns_; }
    [[nodiscard]] long long length() const { return length_; }

    [[nodiscard]] double xMin() const { return 0.0; }
    [[nodiscard]] double xMax() const
    {
        return length_ > 1 ? static_cast<double>(length_ - 1) : 1.0;
    }
    [[nodiscard]] double xPositiveMinimum() const;
    [[nodiscard]] double minimum() const { return minimum_; }
    [[nodiscard]] double maximum() const { return maximum_; }
    [[nodiscard]] double positiveMinimum() const { return hasPositive_ ? positiveMinimum_ : 0.0; }
    [[nodiscard]] std::span<const double> drawnValues(int index) const;

private:
    struct Entry
    {
        const double* values = nullptr;
        qsizetype count = 0;
        QColor colour;
        LinePyramid pyramid;
        std::vector<double> whole;
        std::vector<double> closer;
        long long wholeStride = 1;
        double wholeStep = 1.0;
        bool wholeSummarised = false;
        PlotWindow closerWindow;
        double closerStep = 1.0;
        bool closerValid = false;
        bool ownAxis = false;
        bool finite = false;
        std::unique_ptr<PyramidBuilder> building;
        double low = 0.0;
        double high = 1.0;
    };

    [[nodiscard]] int pointsFor() const;
    void adopt(Entry& entry);
    void rebuildWhole(Entry& entry);
    void refreshCloser();
    void recount();
    [[nodiscard]] PlotLine lineOf(const Entry& entry) const;
    void emitChanged();

    std::vector<Entry> lines_;
    std::vector<std::vector<double>> retired_;
    int columns_ = kDefaultColumns;
    int cap_ = kMinPoints;
    double viewMin_ = 0.0;
    double viewMax_ = 1.0;
    long long length_ = 0;
    double minimum_ = 0.0;
    double maximum_ = 1.0;
    double positiveMinimum_ = 0.0;
    bool hasPositive_ = false;
    int shared_ = 0;
    int nextColour_ = 0;
};

} // namespace gui
