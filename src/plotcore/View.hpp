// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Zoom, pan and padded extents, with no widget and no Qt.
//
// PlotSurface.qml owns this arithmetic in the application; PlotWidget.cpp
// used to own a second copy. The Python host cannot call either of those, so
// the math lives here once. A pan is still in the axis's own scale (decades
// while logarithmic), and a quarter of the window stays over the data.

#include "gui/PlotProjection.hpp"

namespace gui {

class PlotCamera
{
public:
    struct Span
    {
        double low = 0.0;
        double high = 1.0;
    };

    static constexpr int kMaxVertices = 2 << 20;
    static constexpr double kPanKeep = 0.25;
    static constexpr double kMinimumBand = 12.0;

    void setDataExtent(double xMin, double xMax, double yMin, double yMax, double xPositiveMin,
                       double yPositiveMin, long long samples);
    void setXLog(bool on);
    void setYLog(bool on);
    void setXLogBase(double base);
    void setYLogBase(double base);
    void reset();

    [[nodiscard]] bool xLog() const { return xLog_; }
    [[nodiscard]] bool yLog() const { return yLog_; }
    [[nodiscard]] double xLogBase() const { return xLogBase_; }
    [[nodiscard]] double yLogBase() const { return yLogBase_; }

    [[nodiscard]] double viewMinX() const;
    [[nodiscard]] double viewMaxX() const;
    [[nodiscard]] double viewMinY() const;
    [[nodiscard]] double viewMaxY() const;

    /// `fx`/`fy` are fractions of the pane (y up). `onlyX` / `onlyY` are the
    /// Shift / Ctrl modifiers PlotSurface uses.
    void zoomAt(double fx, double fy, double factor, bool onlyX, bool onlyY);
    void panBy(double dx, double dy, double areaWidth, double areaHeight);

    /// Pane-local pixels, origin at the top left, y downward. A band under
    /// `kMinimumBand` pixels on either side is a slip, not a window, and is
    /// refused whole: a two-pixel-tall band is a magnification the reader
    /// never asked for, and which axis they meant is not something to guess.
    /// The same refusal as PlotSurface.zoomToRegion.
    bool zoomToRegion(double px0, double py0, double px1, double py1, double areaWidth,
                      double areaHeight);

    /// The same window a rectangle would open, named in data values. A bound
    /// at or below zero on a logarithmic axis is clipped to the part that
    /// exists; a window with nothing above zero is the whole axis.
    void setViewRange(double x0, double x1, double y0, double y1);

    [[nodiscard]] PlotView frame(double width, double height, double pixelRatio,
                                 int lineCount) const;

    /// The linear window a line on its own axis shows. The same share of its
    /// padded extent that the common axis is showing of its own, then `shift`
    /// in the line's own units. One gesture still zooms every axis; the shift
    /// is the part that moves only this line. PlotSurface.separateAxes is the
    /// share.
    [[nodiscard]] Span ownSpan(double lineLow, double lineHigh, double shift) const;

    /// The common window, shifted by `shift` in the units a pan is measured in.
    /// A line drawn in this window sits that far off the common axis, and the
    /// numbers on the window are the values the line is drawn at. The scale
    /// stays the common axis's, logarithm included, so a shift does not change
    /// what a reading means.
    [[nodiscard]] Span shiftedSpan(double shift) const;

    /// The current window, in the units a pan is measured in.
    [[nodiscard]] double xSpan() const;
    [[nodiscard]] double ySpan() const;

    [[nodiscard]] Span paddedX() const;
    [[nodiscard]] Span paddedY() const;

    /// The data value under a pane-local pixel. zoomToRegion resolves a band
    /// through these, so a readout of that band has to ask them too.
    [[nodiscard]] double dataXAt(double px, double areaWidth) const;
    [[nodiscard]] double dataYAt(double py, double areaHeight) const;

private:
    [[nodiscard]] static Span padded(double low, double high, bool logarithmic, double base);
    [[nodiscard]] double axisPosition(double value, bool logarithmic, double base) const;
    [[nodiscard]] double axisValue(double position, bool logarithmic, double base) const;
    [[nodiscard]] double clampPan(double pan, double zoom, double low, double high, bool logarithmic,
                                  double base) const;
    void zoomedAxis(double& zoom, double& pan, double low, double high, double fraction,
                    double factor, bool logarithmic, double base, double minimumSpan);
    [[nodiscard]] double valueAlong(double low, double high, double at, bool logarithmic,
                                    double base) const;
    [[nodiscard]] double maxZoom() const;
    [[nodiscard]] double minimumSpanX() const;
    [[nodiscard]] double logZoomCeiling(double full, double held, double fraction,
                                        double minimumSpan, double base) const;

    struct AxisPlacement
    {
        double zoom = 1.0;
        double pan = 0.0;
    };

    [[nodiscard]] AxisPlacement viewedAxis(double low, double high, double from, double to,
                                           bool logarithmic, double base, double minimumSpan) const;

    double dataXMin_ = 0.0;
    double dataXMax_ = 1.0;
    double dataYMin_ = 0.0;
    double dataYMax_ = 1.0;
    double dataXPositive_ = 0.0;
    double dataYPositive_ = 0.0;
    /// Elements along x. The zoom ceiling is counted in these, not in the
    /// width of the axis. Zero means the extent itself is the only length.
    long long samples_ = 0;
    bool xLog_ = false;
    bool yLog_ = false;
    double xLogBase_ = 10.0;
    double yLogBase_ = 10.0;
    double zoomX_ = 1.0;
    double zoomY_ = 1.0;
    double panX_ = 0.0;
    double panY_ = 0.0;
};

/// The line whose stroke passes closest to `(px, py)`, or -1 when none comes
/// within `maxPixels`. `lineRuns` holds one past the last run of each line,
/// the same sentinel projectLine's callers keep.
[[nodiscard]] int nearestLine(const std::vector<QPointF>& points, const std::vector<PlotRun>& runs,
                              const std::vector<int>& lineRuns, double px, double py,
                              double maxPixels);

} // namespace gui
