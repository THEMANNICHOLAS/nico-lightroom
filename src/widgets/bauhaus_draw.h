/*
    This file is part of Ansel,
    Copyright (C) 2026 Aurélien PIERRE.

    Ansel is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Ansel is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Ansel.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * Ring-grip slider raster: geometry and drawing for the bauhaus slider track and indicator.
 *
 * Split out of widgets/bauhaus.c so the raster can be exercised on a cairo image surface
 * with no display and no GTK widget. Everything here is pure geometry plus cairo; text,
 * theme lookup and widget metrics stay in bauhaus.c, which populates BhMetrics/BhTrackState.
 */
#ifndef DT_WIDGETS_BAUHAUS_DRAW_H
#define DT_WIDGETS_BAUHAUS_DRAW_H

#include <cairo.h>
#include <gdk/gdk.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compact geometry (PLAN-ring-grip-slider.md, D3). Do not shrink BH_MARKER below 14:
 * the hollow centre closes up. */
#define BH_BASELINE 8.0    /* rail thickness */
#define BH_RADIUS 4.0      /* rail corner radius = BH_BASELINE / 2 */
#define BH_MARKER 14.0     /* ring outer diameter */
#define BH_RING_WIDTH 2.0  /* ring stroke, always hollow */
#define BH_HALO 4.0        /* hover halo stroke, outside the ring */
#define BH_NOTCH_OVER 2.0  /* origin notch overshoot, top and bottom */
#define BH_GAP 4.0         /* label row -> track top */
#define BH_PAD 3.0         /* widget padding, top and bottom */
#define BH_RAMP_ALPHA 0.55 /* gradient ramp stop alpha (was a 0.4f literal in bauhaus.c) */

/** One gradient ramp stop, position normalised to 0..1. */
typedef struct
{
  double pos;
  double r, g, b;
} BhGradStop;

/** Everything the track/ring raster and the hit test need, populated by bauhaus.c. */
typedef struct
{
  double line_h;    /* label / value row height */
  double track_top; /* y of the rail's top edge */
  double track_cy;  /* y of the rail's vertical centre */
  double inset;     /* BH_MARKER / 2: x of the ring centre at position 0 and 1 */
  double width;     /* widget width */
  double value_w;   /* reserved value-field width, measured by bauhaus.c */
} BhMetrics;

/** The per-widget draw state. All colours are owned by the caller. */
typedef struct
{
  double frac;   /* current value as 0..1 */
  double origin; /* 0..1, where the bipolar fill grows from */
  int disabled;
  int hot;                /* pointer over the track, or dragging */
  int grad_cnt;           /* > 0 => draw the ramp instead of the bipolar fill */
  const BhGradStop *grad; /* grad_cnt stops */
  const GdkRGBA *fill;    /* bipolar fill colour */
  const GdkRGBA *ring;    /* enabled ring colour */
  const GdkRGBA *ring_hover;
  const GdkRGBA *halo; /* hot halo colour */
} BhTrackState;

void dt_bauhaus_draw_track(cairo_t *cr, const BhMetrics *m, const BhTrackState *s);
void dt_bauhaus_draw_ring(cairo_t *cr, const BhMetrics *m, const BhTrackState *s);
void dt_bauhaus_value_rect(const BhMetrics *m, int *x, int *y, int *w, int *h);
int dt_bauhaus_value_parse(const char *text, double factor, double offset, double min,
                           double max, double *out);
int dt_bauhaus_value_hit(const BhMetrics *m, double x, double y);
double dt_bauhaus_pos_to_x(const BhMetrics *m, double p);
double dt_bauhaus_fill_x(const BhMetrics *m, double p);
double dt_bauhaus_x_to_pos(const BhMetrics *m, double x);

#ifdef __cplusplus
}
#endif

#endif
