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

    void setDataExtent(double xMin, double xMax, double yMin, double yMax, double xPositiveMin,
                       double yPositiveMin);
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

    [[nodiscard]] PlotView frame(double width, double height, double pixelRatio,
                                 int lineCount) const;

    [[nodiscard]] Span paddedX() const;
    [[nodiscard]] Span paddedY() const;

private:
    [[nodiscard]] static Span padded(double low, double high, bool logarithmic, double base);
    [[nodiscard]] double axisPosition(double value, bool logarithmic, double base) const;
    [[nodiscard]] double axisValue(double position, bool logarithmic, double base) const;
    [[nodiscard]] double clampPan(double pan, double zoom, double low, double high, bool logarithmic,
                                  double base) const;
    void zoomedAxis(double& zoom, double& pan, double low, double high, double fraction,
                    double factor, bool logarithmic, double base, double minimumSpan);
    [[nodiscard]] double maxZoom() const;
    [[nodiscard]] double minimumSpanX() const;

    double dataXMin_ = 0.0;
    double dataXMax_ = 1.0;
    double dataYMin_ = 0.0;
    double dataYMax_ = 1.0;
    double dataXPositive_ = 0.0;
    double dataYPositive_ = 0.0;
    bool xLog_ = false;
    bool yLog_ = false;
    double xLogBase_ = 10.0;
    double yLogBase_ = 10.0;
    double zoomX_ = 1.0;
    double zoomY_ = 1.0;
    double panX_ = 0.0;
    double panY_ = 0.0;
};

} // namespace gui
