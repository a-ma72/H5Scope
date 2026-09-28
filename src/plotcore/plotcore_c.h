/* SPDX-FileCopyrightText: 2026 Andreas Martin
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef H5PLOT_C_H
#define H5PLOT_C_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#ifdef H5PLOT_EXPORT
#define H5PLOT_API __declspec(dllexport)
#else
#define H5PLOT_API __declspec(dllimport)
#endif
#else
#define H5PLOT_API
#endif

typedef struct H5Plot H5Plot;

typedef struct H5PlotRun
{
    int first;
    int count;
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;
    float width;
} H5PlotRun;

/* A tick in the pane set by h5plot_set_pane. y grows downward, as the
 * projected points do. axis is 0 along x and 1 up y. */
typedef struct H5PlotTick
{
    double x;
    double y;
    double value;
    int axis;
} H5PlotTick;

H5PLOT_API H5Plot* h5plot_create(void);
H5PLOT_API void h5plot_destroy(H5Plot* plot);

/* Borrow `n` doubles. The caller must keep the buffer alive. */
H5PLOT_API int h5plot_add_line(H5Plot* plot, const double* y, long long n, int red, int green,
                               int blue);
H5PLOT_API void h5plot_clear(H5Plot* plot);

H5PLOT_API void h5plot_set_pane(H5Plot* plot, int width, int height, double pixel_ratio);
H5PLOT_API void h5plot_set_ylog(H5Plot* plot, int on);
H5PLOT_API void h5plot_reset_view(H5Plot* plot);
H5PLOT_API void h5plot_wheel(H5Plot* plot, double px, double py, double factor, int shift,
                             int control);
H5PLOT_API void h5plot_pan(H5Plot* plot, double dx, double dy);

H5PLOT_API double h5plot_view_min_x(const H5Plot* plot);
H5PLOT_API double h5plot_view_max_x(const H5Plot* plot);
H5PLOT_API double h5plot_view_min_y(const H5Plot* plot);
H5PLOT_API double h5plot_view_max_y(const H5Plot* plot);

/* Fold + project. Returns the number of points. */
H5PLOT_API int h5plot_project(H5Plot* plot);
H5PLOT_API int h5plot_point_count(const H5Plot* plot);
H5PLOT_API void h5plot_copy_points(const H5Plot* plot, double* xy);
H5PLOT_API int h5plot_run_count(const H5Plot* plot);
H5PLOT_API void h5plot_copy_runs(const H5Plot* plot, H5PlotRun* runs);

H5PLOT_API int h5plot_tick_count(const H5Plot* plot);
H5PLOT_API void h5plot_copy_ticks(const H5Plot* plot, H5PlotTick* ticks);

#ifdef __cplusplus
}
#endif

#endif
