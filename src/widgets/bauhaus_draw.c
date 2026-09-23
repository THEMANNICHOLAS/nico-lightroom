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

#include "widgets/bauhaus_draw.h"

#include "widgets/draw.h" // dt_draw_rounded_rectangle_path()

#include <math.h>

/* Alphas for the state-independent parts of the draw. */
#define BH_TRACK_ALPHA 0.30          /* recessed rail, rgba(0,0,0,.) */
#define BH_NOTCH_ALPHA 0.30          /* origin notch, rgba(1,1,1,.) */
#define BH_RING_ALPHA 0.92           /* enabled ring, scaled from its theme colour */
#define BH_HALO_ALPHA 0.16           /* hot halo */
#define BH_SHADOW_ALPHA 0.45         /* drop shadow ring */
#define BH_DISABLED_ALPHA 0.42       /* dims rail, ramp, fill and notch when insensitive */
#define BH_DISABLED_RING_ALPHA 0.35  /* insensitive ring is white at 35% */
#define BH_SHADOW_Y_OFFSET 0.5

/** Set the cairo source to @p c with its alpha scaled by @p a. */
static void _bh_set_color(cairo_t *cr, const GdkRGBA *c, const double a)
{
  cairo_set_source_rgba(cr, c->red, c->green, c->blue, c->alpha * a);
}

/** Map a normalised value @p p to the rail x, leaving room for the ring centre at both ends.
 *
 * This is the geometry the widget and the hit test share; @p p is expected pre-clamped. */
double dt_bauhaus_pos_to_x(const BhMetrics *m, const double p)
{
  return m->inset + p * (m->width - 2.0 * m->inset);
}

/** Like dt_bauhaus_pos_to_x() but snapping the extremes to the widget edges.
 *
 * A bipolar fill drawn between two pos_to_x() points stops BH_MARKER / 2 short of each edge,
 * leaving a dead stub at full fill. The snap only fires at (near) the exact ends, so every
 * interior position still uses the rail mapping the ring and the hit test agree on. */
double dt_bauhaus_fill_x(const BhMetrics *m, const double p)
{
  if(p <= 0.0005) return 0.0;
  if(p >= 0.9995) return m->width;
  return dt_bauhaus_pos_to_x(m, p);
}

/** Clamped inverse of dt_bauhaus_pos_to_x(): pointer input outside the rail pins to 0 or 1. */
double dt_bauhaus_x_to_pos(const BhMetrics *m, const double x)
{
  const double p = (x - m->inset) / (m->width - 2.0 * m->inset);
  return p < 0.0 ? 0.0 : (p > 1.0 ? 1.0 : p);
}

/** Right-aligned integer rectangle reserved for the value field.
 *
 * The field is reserved, so it can only exist where the rail is wider than it: when the measured
 * value string does not fit, an empty rect at the rail's right edge is reported instead of one
 * that starts left of the rail. y and h stay pinned to the label row. */
void dt_bauhaus_value_rect(const BhMetrics *m, int *x, int *y, int *w, int *h)
{
  const int fits = m->value_w < m->width;
  *x = fits ? (int)(m->width - m->value_w) : (int)m->width;
  *y = (int)BH_PAD;
  *w = fits ? (int)m->value_w : 0;
  *h = (int)m->line_h;
}

/** Parse a value typed in the entry field and convert it back to domain units.
 *
 * The field displays `val * factor + offset`, so the inverse mapping is applied before
 * clamping to [min, max]. g_ascii_strtod is used instead of strtod so "1.5" keeps its
 * meaning under a comma-decimal locale. Returns 1 on commit, 0 on reject. */
int dt_bauhaus_value_parse(const char *text, const double factor, const double offset,
                           const double min, const double max, double *out)
{
  const char *start = text;
  while(g_ascii_isspace(*start)) start++;
  if(*start == '\0') return 0;

  char *end = NULL;
  const double display = g_ascii_strtod(start, &end);
  if(end == start) return 0;

  // anything but whitespace after the number rejects the whole input ("1.2.3", "45abc")
  while(g_ascii_isspace(*end)) end++;
  if(*end != '\0') return 0;

  // g_ascii_strtod yields NaN/Inf for "nan"/"inf", which must not reach the slider
  if(!isfinite(display)) return 0;

  const double v = (display - offset) / factor;
  *out = v < min ? min : (v > max ? max : v);
  return 1;
}

/** True when a point in metrics space falls inside the reserved value field.
 *
 * Edges are inclusive so the field's right edge belongs to the value, not to the rail. A rail that
 * cannot carry a reserved field has no value region at all, so the parameter name keeps its no-op
 * click behaviour. */
int dt_bauhaus_value_hit(const BhMetrics *m, const double x, const double y)
{
  if(m->value_w >= m->width) return 0;
  int rx, ry, rw, rh;
  dt_bauhaus_value_rect(m, &rx, &ry, &rw, &rh);
  return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
}

/** Draw the recessed rail and whatever fills it.
 *
 * Order is load-bearing: the rounded rail background is filled, then kept current as the clip
 * for the fill/ramp, so neither can bleed above or below the rail. The origin notch is drawn
 * after the clip is released, because it deliberately overshoots the rail top and bottom. */
void dt_bauhaus_draw_track(cairo_t *cr, const BhMetrics *m, const BhTrackState *s)
{
  const double alpha = s->disabled ? BH_DISABLED_ALPHA : 1.0;
  const double y = m->track_top;

  cairo_save(cr);

  /* recessed rail: filled, then kept as the clip for everything inside it */
  dt_draw_rounded_rectangle_path(cr, 0.0f, (float)y, (float)m->width, BH_BASELINE, BH_RADIUS);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, BH_TRACK_ALPHA * alpha);
  cairo_fill_preserve(cr);
  cairo_clip(cr);

  if(s->grad_cnt > 0)
  {
    /* the ramp replaces the fill entirely */
    cairo_pattern_t *pattern = cairo_pattern_create_linear(0.0, 0.0, m->width, 0.0);
    for(int k = 0; k < s->grad_cnt; k++)
      cairo_pattern_add_color_stop_rgba(pattern, s->grad[k].pos, s->grad[k].r, s->grad[k].g,
                                        s->grad[k].b, BH_RAMP_ALPHA * alpha);
    cairo_set_source(cr, pattern);
    cairo_rectangle(cr, 0.0, y, m->width, BH_BASELINE);
    cairo_fill(cr);
    cairo_pattern_destroy(pattern);
  }
  else if(s->fill_feedback)
  {
    /* bipolar fill: grows from the origin towards the current value */
    const double x0 = dt_bauhaus_fill_x(m, s->origin);
    const double x1 = dt_bauhaus_fill_x(m, s->frac);
    _bh_set_color(cr, s->fill, alpha);
    cairo_rectangle(cr, fmin(x0, x1), y, fabs(x1 - x0), BH_BASELINE);
    cairo_fill(cr);
  }

  cairo_restore(cr);

  /* origin notch: only where an origin is meaningful, and not clipped by the rail */
  if(s->origin > 0.001 && s->origin < 0.999)
  {
    const double xo = round(dt_bauhaus_pos_to_x(m, s->origin)) + 0.5;
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, BH_NOTCH_ALPHA * alpha);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, xo, y - BH_NOTCH_OVER);
    cairo_line_to(cr, xo, y + BH_BASELINE + BH_NOTCH_OVER);
    cairo_stroke(cr);
  }
}

/** Draw the value indicator as a hollow ring centred on the current value.
 *
 * The ring is always stroked, never filled, so the rail or ramp shows through its centre and
 * it reads as a grip rather than a plain knob. The drop shadow is skipped when insensitive and
 * the hover halo only appears when hot *and* enabled, so an insensitive slider cannot glow. */
void dt_bauhaus_draw_ring(cairo_t *cr, const BhMetrics *m, const BhTrackState *s)
{
  const double cx = dt_bauhaus_pos_to_x(m, s->frac);
  const double cy = m->track_cy;
  const double r = (BH_MARKER - BH_RING_WIDTH) / 2.0;

  cairo_save(cr);

  if(!s->disabled)
  {
    cairo_set_line_width(cr, BH_RING_WIDTH + 1.5);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, BH_SHADOW_ALPHA);
    cairo_arc(cr, cx, cy + BH_SHADOW_Y_OFFSET, r, 0.0, 2.0 * M_PI);
    cairo_stroke(cr);
  }

  if(s->hot && !s->disabled)
  {
    _bh_set_color(cr, s->halo, BH_HALO_ALPHA);
    cairo_set_line_width(cr, BH_HALO);
    cairo_arc(cr, cx, cy, r + BH_RING_WIDTH / 2.0 + BH_HALO / 2.0, 0.0, 2.0 * M_PI);
    cairo_stroke(cr);
  }

  cairo_set_line_width(cr, BH_RING_WIDTH);
  if(s->disabled)
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, BH_DISABLED_RING_ALPHA);
  else if(s->hot)
    _bh_set_color(cr, s->ring_hover, 1.0);
  else
    _bh_set_color(cr, s->ring, BH_RING_ALPHA);
  cairo_arc(cr, cx, cy, r, 0.0, 2.0 * M_PI);
  cairo_stroke(cr);

  cairo_restore(cr);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
