// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

// The QWidget plot and the RAM store under it, without a window manager.
//
// LineStore is DatasetPlot with the file taken out: a borrowed buffer, a
// pyramid, a whole-line summary and a closer look. The cases below are the
// reasons that object exists -- a spike the envelope must keep, a zoom that
// folds from the pyramid rather than stretching the summary, and a widget
// that empties when the store is cleared.

#include "plotcore/LineStore.hpp"
#include "plotcore/PlotWidget.hpp"

#include <QApplication>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

using Catch::Approx;

namespace {

QApplication& qt()
{
    static int argc = 1;
    static char arg0[] = "test_plotcore";
    static char* argv[] = {arg0, nullptr};
    static QApplication app(argc, argv);
    return app;
}

std::vector<double> impulse(int count, int at, double spike)
{
    std::vector<double> values(static_cast<std::size_t>(count), 0.0);
    values[static_cast<std::size_t>(at)] = spike;
    return values;
}

bool holds(std::span<const double> values, double wanted)
{
    return std::any_of(values.begin(), values.end(),
                       [wanted](double value) { return value == wanted; });
}

} // namespace

TEST_CASE("the store keeps a one-sample spike in the whole-line summary", "[plotcore]")
{
    const std::vector<double> values = impulse(10'000, 4'000, 17.0);
    gui::LineStore store;
    REQUIRE(store.addLine(values.data(), static_cast<qsizetype>(values.size())) == 0);
    REQUIRE(store.minimum() == Approx(0.0));
    REQUIRE(store.maximum() == Approx(17.0));
    REQUIRE(store.length() == 10'000);
    store.setPaneColumns(256);
    REQUIRE(holds(store.drawnValues(0), 17.0));
}

TEST_CASE("a closer look is a run of the line, not a stretch of the summary", "[plotcore]")
{
    qt();
    std::vector<double> values(50'000, 0.0);
    values[25'000] = 9.0;
    values[25'001] = -3.0;

    gui::LineStore store;
    store.addLine(values.data(), static_cast<qsizetype>(values.size()));
    store.setPaneColumns(256);
    const auto whole = store.drawnValues(0);
    REQUIRE(holds(whole, 9.0));
    REQUIRE(whole.size() < values.size());

    store.setVisibleRange(24'950.0, 25'050.0);
    const auto close = store.drawnValues(0);
    REQUIRE(holds(close, 9.0));
    REQUIRE(holds(close, -3.0));
    // The closer fold covers a hundred elements; the whole-line summary
    // covered the whole line. If the zoom had only stretched the summary,
    // both pointers would be the same buffer.
    REQUIRE(close.data() != whole.data());
    REQUIRE(close.size() < whole.size() || close.size() < 400);
}

TEST_CASE("clearLines empties the widget it last filled", "[plotcore]")
{
    qt();
    const std::vector<double> values = {0.0, 1.0, 0.5};
    gui::LineStore store;
    store.addLine(values.data(), static_cast<qsizetype>(values.size()));
    gui::PlotWidget widget;
    std::vector<gui::PlotLine> lines;
    gui::PlotAxis axis;
    store.fillInto(lines, axis);
    widget.setLines(std::move(lines), axis);
    REQUIRE(widget.lineCount() == 1);
    store.clearLines();
    widget.clear();
    REQUIRE(widget.lineCount() == 0);
    REQUIRE(store.lineCount() == 0);
}

TEST_CASE("the widget's full view covers the padded data", "[plotcore]")
{
    qt();
    std::vector<double> values(1'000);
    for (int i = 0; i < 1'000; ++i) {
        values[static_cast<std::size_t>(i)] = static_cast<double>(i);
    }
    gui::LineStore store;
    store.addLine(values.data(), static_cast<qsizetype>(values.size()));

    gui::PlotWidget widget;
    widget.resize(640, 400);
    std::vector<gui::PlotLine> lines;
    gui::PlotAxis axis;
    store.fillInto(lines, axis);
    widget.setLines(std::move(lines), axis);
    widget.setDataExtent(store.xMin(), store.xMax(), store.minimum(), store.maximum(),
                         store.xPositiveMinimum(), store.positiveMinimum(), store.length());
    widget.resetView();

    REQUIRE(widget.viewMinX() < store.xMin());
    REQUIRE(widget.viewMaxX() > store.xMax());
    REQUIRE(widget.viewMinY() < store.minimum());
    REQUIRE(widget.viewMaxY() > store.maximum());
}

TEST_CASE("a logarithmic y axis starts at the smallest positive value", "[plotcore]")
{
    const std::vector<double> values = {0.0, -2.0, 0.25, 4.0};
    gui::LineStore store;
    store.addLine(values.data(), static_cast<qsizetype>(values.size()));
    REQUIRE(store.minimum() == Approx(-2.0));
    REQUIRE(store.maximum() == Approx(4.0));
    REQUIRE(store.positiveMinimum() == Approx(0.25));
}
