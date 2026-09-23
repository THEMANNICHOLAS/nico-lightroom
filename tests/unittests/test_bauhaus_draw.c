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

/* The ring-grip slider raster, pixel by pixel.
 *
 * The raster unit owns the geometry contract the widget and the hit test share: pos_to_x maps a
 * normalised value onto the rail, fill_x widens that mapping to the widget edges at the extremes
 * so a full bipolar fill has no dead stub, value_rect reserves the right-aligned value field, and
 * x_to_pos is the clamped inverse used for pointer input. The drawing itself is probed on a cairo
 * image surface: a zero-length fill must leave the recessed rail alone, a gradient ramp must
 * replace the fill, clip to the rail and show through the hollow ring, and an insensitive widget
 * must lose both its drop shadow and its hover halo. */

#include "widgets/bauhaus_draw.h"

#include <cairo.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#define BOX_W 200
#define BOX_H 44

/* Theme tokens as resolved by the widget: fill is @orange_dark, ring is @grey_95, the hover halo
 * is @orange_light and the hover ring is white. They are plain values here: the unit never does a
 * theme lookup, the caller resolves and owns them. */
static const GdkRGBA FILL = { 0.749, 0.502, 0.0, 1.0 };
static const GdkRGBA RING = { 0.945, 0.945, 0.945, 1.0 };
static const GdkRGBA WHITE = { 1.0, 1.0, 1.0, 1.0 };
static const GdkRGBA HALO = { 1.0, 0.749, 0.251, 1.0 };

typedef void (*_draw_fn)(cairo_t *, const BhMetrics *, const BhTrackState *);

static uint32_t _pixel(cairo_surface_t *surface, const int x, const int y)
{
  cairo_surface_flush(surface);
  const uint8_t *data = cairo_image_surface_get_data(surface);
  const int stride = cairo_image_surface_get_stride(surface);
  return *(const uint32_t *)(data + (size_t)y * stride + (size_t)x * 4);
}

static int _alpha(cairo_surface_t *surface, const int x, const int y)
{
  return (int)(_pixel(surface, x, y) >> 24);
}

/* Integer components differing by at most 3: 8-bit rounding of a grey stroke. */
static int _near(const int a, const int b)
{
  return a - b <= 3 && b - a <= 3;
}

/* The metrics the widget would measure: a 12 px label row, 4 px gap, then an 8 px rail. inset is
 * BH_MARKER / 2, so the ring centre never leaves the widget at either extreme. A 200x44 surface
 * leaves room for the 11 px halo below the rail. */
static BhMetrics _metrics(void)
{
  BhMetrics m;
  memset(&m, 0, sizeof(BhMetrics));
  m.line_h = 12.0;
  m.track_top = BH_PAD + m.line_h + BH_GAP;      /* 19 */
  m.track_cy = m.track_top + BH_BASELINE / 2.0;  /* 23 */
  m.inset = BH_MARKER / 2.0;                     /* 7 */
  m.width = BOX_W;
  m.value_w = 40.0;
  return m;
}

/* A zeroed state carrying the four theme colours; grad stays NULL until a test sets it. */
static void _state(BhTrackState *s, const double frac, const double origin)
{
  memset(s, 0, sizeof(BhTrackState));
  s->frac = frac;
  s->origin = origin;
  /* Matches bauhaus.c:1697, where the widget is created with feedback = 1; a zeroed struct would
   * silently disable the fill and change what the fill assertions prove. */
  s->fill_feedback = 1;
  s->fill = &FILL;
  s->ring = &RING;
  s->ring_hover = &WHITE;
  s->halo = &HALO;
}

static cairo_surface_t *_render(_draw_fn fn, const BhMetrics *m, const BhTrackState *s)
{
  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, BOX_W, BOX_H);
  cairo_t *cr = cairo_create(surface);
  fn(cr, m, s);
  cairo_destroy(cr);
  return surface;
}

static void _zero_fill_draws_no_fill(void **state)
{
  (void)state;
  const BhMetrics m = _metrics();
  BhTrackState s;

  /* frac == origin: the fill has zero width and must not paint over the rail background */
  _state(&s, 0.5, 0.5);
  cairo_surface_t *zero = _render(dt_bauhaus_draw_track, &m, &s);
  assert_int_equal(_pixel(zero, 150, (int)m.track_cy) & 0x00FFFFFF, 0);
  cairo_surface_destroy(zero);

  /* control: the same probe with a full fill is opaque @orange_dark */
  _state(&s, 1.0, 0.5);
  cairo_surface_t *full = _render(dt_bauhaus_draw_track, &m, &s);
  assert_int_equal(_pixel(full, 150, (int)m.track_cy), 0xFFBF8000);
  cairo_surface_destroy(full);
}

static void _unipolar_max_fills_both_ends(void **state)
{
  (void)state;
  const BhMetrics m = _metrics();
  BhTrackState s;

  /* origin 0, value 1: no notch, and the fill snaps to both widget edges */
  _state(&s, 1.0, 0.0);
  cairo_surface_t *full = _render(dt_bauhaus_draw_track, &m, &s);
  assert_int_equal(_pixel(full, 3, (int)m.track_cy), 0xFFBF8000);
  assert_int_equal(_pixel(full, BOX_W - 4, (int)m.track_cy), 0xFFBF8000);
  cairo_surface_destroy(full);

  /* the extreme snap: 0 and 1 reach the widget edges, everything else is on the rail */
  assert_true(dt_bauhaus_fill_x(&m, 0.0) == 0.0);
  assert_true(dt_bauhaus_fill_x(&m, 1.0) == m.width);
  assert_true(dt_bauhaus_fill_x(&m, 0.5) == dt_bauhaus_pos_to_x(&m, 0.5));

  /* control: half a unipolar fill must leave the right end unpainted */
  _state(&s, 0.5, 0.0);
  cairo_surface_t *half = _render(dt_bauhaus_draw_track, &m, &s);
  assert_int_equal(_pixel(half, BOX_W - 4, (int)m.track_cy) & 0x00FFFFFF, 0);
  cairo_surface_destroy(half);
}

static void _fill_feedback_off_suppresses_bipolar_fill(void **state)
{
  (void)state;
  const BhMetrics m = _metrics();
  BhTrackState s;

  /* frac != origin and no ramp: without fill_feedback the bipolar fill must not be painted,
   * only the recessed rail background is left */
  _state(&s, 1.0, 0.5);
  s.fill_feedback = 0;
  cairo_surface_t *off = _render(dt_bauhaus_draw_track, &m, &s);
  assert_int_equal(_pixel(off, 150, (int)m.track_cy) & 0x00FFFFFF, 0);
  cairo_surface_destroy(off);

  /* control: the same geometry with the fill enabled paints opaque @orange_dark */
  _state(&s, 1.0, 0.5);
  cairo_surface_t *on = _render(dt_bauhaus_draw_track, &m, &s);
  assert_int_equal(_pixel(on, 150, (int)m.track_cy), 0xFFBF8000);
  cairo_surface_destroy(on);
}

static void _gradient_ramp_clipped_and_through_ring(void **state)
{
  (void)state;
  const BhMetrics m = _metrics();
  const BhGradStop stops[2] = { { 0.0, 1.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0, 1.0 } };

  /* origin 0 means no notch: the point of this render is the ramp, not the origin mark */
  BhTrackState s;
  _state(&s, 0.5, 0.0);
  s.grad_cnt = 2;
  s.grad = stops;

  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, BOX_W, BOX_H);
  cairo_t *cr = cairo_create(surface);
  dt_bauhaus_draw_track(cr, &m, &s);
  dt_bauhaus_draw_ring(cr, &m, &s);
  cairo_destroy(cr);

  /* the ring centre (100, 23) is the hollow: the ramp must show through it, not the ring stroke */
  const uint32_t centre = _pixel(surface, (int)dt_bauhaus_pos_to_x(&m, 0.5), (int)m.track_cy);
  assert_true((int)(centre >> 24) > 100);
  assert_true((int)((centre >> 16) & 0xFF) > 0);
  assert_int_equal((int)((centre >> 8) & 0xFF), 0);
  assert_true((int)(centre & 0xFF) > 0);

  /* the ramp is clipped to the rail: nothing is painted above it */
  assert_int_equal(_alpha(surface, 40, (int)m.track_top - 3), 0);
  cairo_surface_destroy(surface);
}

static void _origin_notch_drawn_above_the_rail(void **state)
{
  (void)state;
  const BhMetrics m = _metrics();
  BhTrackState s;

  /* origin strictly inside 0..1, the only range where the notch exists; frac differs from it so
   * the fill is drawn too, but the fill is clipped to the rail and never reaches the sampled row */
  _state(&s, 0.5, 0.25);
  cairo_surface_t *surface = _render(dt_bauhaus_draw_track, &m, &s);

  /* the notch is a 1 px white line at round(pos_to_x(origin)) + 0.5, overshooting the rail top
   * by BH_NOTCH_OVER. Its upper overshoot covers the rail-free row between track_top - BH_NOTCH_OVER
   * and track_top, so that row isolates the notch from the rail and the fill. */
  const int notch_x = (int)round(dt_bauhaus_pos_to_x(&m, s.origin));
  const int notch_y = (int)m.track_top - 1;
  const int above_y = (int)(m.track_top - BH_NOTCH_OVER) - 1;

  /* the notch pixel exists and is white at BH_NOTCH_ALPHA (premultiplied: r = g = b = alpha) */
  const uint32_t notch = _pixel(surface, notch_x, notch_y);
  assert_in_range((int)(notch >> 24), 70, 84); /* 0.30 x 255 = 77 */
  const int nr = (int)((notch >> 16) & 0xFF);
  const int ng = (int)((notch >> 8) & 0xFF);
  const int nb = (int)(notch & 0xFF);
  assert_true(_near(nr, ng));
  assert_true(_near(ng, nb));
  assert_true(_near(nr, nb));

  /* the overshoot is bounded: one row above its upper end nothing is painted */
  assert_int_equal(_alpha(surface, notch_x, above_y), 0);

  /* the notch is local: the same row, between the fill and the rail's right end, is untouched */
  assert_int_equal(_alpha(surface, (int)dt_bauhaus_pos_to_x(&m, 0.75), notch_y), 0);

  cairo_surface_destroy(surface);
}

static void _disabled_ring_is_35pct_white(void **state)
{
  (void)state;
  const BhMetrics m = _metrics();

  /* row 28 is dead centre of the ring stroke below the rail, so only the ring is there */
  BhTrackState s;
  _state(&s, 0.5, 0.0);
  s.disabled = 1;
  s.hot = 1;
  cairo_surface_t *surface = _render(dt_bauhaus_draw_ring, &m, &s);

  const uint32_t px = _pixel(surface, 100, 28);
  assert_in_range((int)(px >> 24), 82, 96); /* 0.35 x 255 = 89 */

  /* white at 35 %: the three components are equal up to 8-bit rounding */
  const int r = (int)((px >> 16) & 0xFF);
  const int g = (int)((px >> 8) & 0xFF);
  const int b = (int)(px & 0xFF);
  assert_true(_near(r, g));
  assert_true(_near(g, b));
  assert_true(_near(r, b));
  cairo_surface_destroy(surface);
}

static void _disabled_suppresses_shadow_and_halo(void **state)
{
  (void)state;
  const BhMetrics m = _metrics();
  BhTrackState s;

  _state(&s, 0.5, 0.0);
  cairo_surface_t *plain = _render(dt_bauhaus_draw_ring, &m, &s);

  _state(&s, 0.5, 0.0);
  s.hot = 1;
  cairo_surface_t *glowing = _render(dt_bauhaus_draw_ring, &m, &s);

  _state(&s, 0.5, 0.0);
  s.hot = 1;
  s.disabled = 1;
  cairo_surface_t *muted = _render(dt_bauhaus_draw_ring, &m, &s);

  /* with cx = 100, cy = 23: the ring's outer edge is cy + 7 = 30 and the shadow's is
   * cy + 0.5 + 7.75 = 31.25, so row 31 is reachable only by the shadow */
  assert_true(_alpha(plain, 100, 31) >= 5);
  assert_int_equal(_alpha(muted, 100, 31), 0);

  /* row 32 is the halo stroke's centre, past the shadow, so it is halo-only */
  assert_true(_alpha(glowing, 100, 32) > 0);
  assert_int_equal(_alpha(muted, 100, 32), 0);

  cairo_surface_destroy(plain);
  cairo_surface_destroy(glowing);
  cairo_surface_destroy(muted);
}

static void _value_rect_right_aligned(void **state)
{
  (void)state;
  const BhMetrics m = _metrics();

  int x, y, w, h;
  dt_bauhaus_value_rect(&m, &x, &y, &w, &h);
  assert_int_equal(x, 160);
  assert_int_equal(y, (int)BH_PAD);
  assert_int_equal(w, 40);
  assert_int_equal(h, 12);
  assert_int_equal(x + w, (int)m.width);
}

static void _value_rect_clamped_when_it_does_not_fit(void **state)
{
  (void)state;
  BhMetrics m = _metrics();
  int x, y, w, h;

  /* a value string wider than the rail: empty rect at the rail's right edge, no value region
   * anywhere in the row, including the rail's left edge */
  m.value_w = 250.0;
  dt_bauhaus_value_rect(&m, &x, &y, &w, &h);
  assert_int_equal(x, (int)m.width);
  assert_int_equal(y, (int)BH_PAD);
  assert_int_equal(w, 0);
  assert_int_equal(h, 12);
  assert_int_equal(dt_bauhaus_value_hit(&m, 0.0, 8.0), 0);
  assert_int_equal(dt_bauhaus_value_hit(&m, 100.0, 8.0), 0);
  assert_int_equal(dt_bauhaus_value_hit(&m, 170.0, 8.0), 0);

  /* value_w == width is also no room: the whole row belongs to the rail */
  m.value_w = m.width;
  dt_bauhaus_value_rect(&m, &x, &y, &w, &h);
  assert_int_equal(x, (int)m.width);
  assert_int_equal(w, 0);
  assert_int_equal(dt_bauhaus_value_hit(&m, 0.0, 8.0), 0);
  assert_int_equal(dt_bauhaus_value_hit(&m, BOX_W, 8.0), 0);
}

static void _pos_x_roundtrip_and_clamp(void **state)
{
  (void)state;
  const BhMetrics m = _metrics();
  const double ps[] = { 0.0, 0.25, 0.5, 0.75, 1.0 };

  for(size_t i = 0; i < sizeof(ps) / sizeof(ps[0]); i++)
  {
    const double d = dt_bauhaus_x_to_pos(&m, dt_bauhaus_pos_to_x(&m, ps[i])) - ps[i];
    assert_true(d < 1e-9 && d > -1e-9);
  }

  assert_true(dt_bauhaus_x_to_pos(&m, -100.0) == 0.0);
  assert_true(dt_bauhaus_x_to_pos(&m, 500.0) == 1.0);
  assert_true(dt_bauhaus_pos_to_x(&m, 0.0) == m.inset);
  assert_true(dt_bauhaus_pos_to_x(&m, 1.0) == m.width - m.inset);
}

static void _value_parse_plain_commits(void **state)
{
  (void)state;
  double v = 0.0;
  assert_int_equal(dt_bauhaus_value_parse("45", 1.0, 0.0, 0.0, 100.0, &v), 1);
  assert_true(v == 45.0);
}

static void _value_parse_percent_display_units(void **state)
{
  (void)state;
  double v = 0.0;

  /* "45" is a display-unit percent: factor 100 / offset 0 maps it back to the 0..1 domain */
  assert_int_equal(dt_bauhaus_value_parse("45", 100.0, 0.0, 0.0, 1.0, &v), 1);
  assert_true(v > 0.45 - 1e-9 && v < 0.45 + 1e-9);
}

static void _value_parse_clamps_to_bounds(void **state)
{
  (void)state;
  double v = 0.0;
  assert_int_equal(dt_bauhaus_value_parse("-10", 1.0, 0.0, 0.0, 100.0, &v), 1);
  assert_true(v == 0.0);
  assert_int_equal(dt_bauhaus_value_parse("500", 1.0, 0.0, 0.0, 100.0, &v), 1);
  assert_true(v == 100.0);
}

static void _value_parse_rejects_garbage(void **state)
{
  (void)state;

  /* A rejected parse must leave the caller's output untouched: a sentinel proves it. */
  const double sentinel = 1234.5;
  double v = sentinel;
  assert_int_equal(dt_bauhaus_value_parse("", 1.0, 0.0, 0.0, 100.0, &v), 0);
  assert_true(v == sentinel);
  v = sentinel;
  assert_int_equal(dt_bauhaus_value_parse("abc", 1.0, 0.0, 0.0, 100.0, &v), 0);
  assert_true(v == sentinel);
  v = sentinel;
  assert_int_equal(dt_bauhaus_value_parse("1.2.3", 1.0, 0.0, 0.0, 100.0, &v), 0);
  assert_true(v == sentinel);
  v = sentinel;
  assert_int_equal(dt_bauhaus_value_parse("nan", 1.0, 0.0, 0.0, 100.0, &v), 0);
  assert_true(v == sentinel);
  v = sentinel;
  assert_int_equal(dt_bauhaus_value_parse("inf", 1.0, 0.0, 0.0, 100.0, &v), 0);
  assert_true(v == sentinel);
}

static void _value_parse_accepts_space_and_sign(void **state)
{
  (void)state;
  double v = 0.0;
  assert_int_equal(dt_bauhaus_value_parse(" +45 ", 1.0, 0.0, 0.0, 100.0, &v), 1);
  assert_true(v == 45.0);
  assert_int_equal(dt_bauhaus_value_parse("-3.5", 1.0, 0.0, -10.0, 10.0, &v), 1);
  assert_true(v == -3.5);
}

static void _value_hit_value_vs_main(void **state)
{
  (void)state;
  const BhMetrics m = _metrics();

  /* the 200 px metrics put the value rect at x 160..200, y 3..15 */
  assert_int_equal(dt_bauhaus_value_hit(&m, 170.0, 8.0), 1);
  assert_int_equal(dt_bauhaus_value_hit(&m, 150.0, 8.0), 0);
  assert_int_equal(dt_bauhaus_value_hit(&m, 170.0, 20.0), 0);
  assert_int_equal(dt_bauhaus_value_hit(&m, 200.0, 3.0), 1);
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(_zero_fill_draws_no_fill),
    cmocka_unit_test(_unipolar_max_fills_both_ends),
    cmocka_unit_test(_fill_feedback_off_suppresses_bipolar_fill),
    cmocka_unit_test(_gradient_ramp_clipped_and_through_ring),
    cmocka_unit_test(_origin_notch_drawn_above_the_rail),
    cmocka_unit_test(_disabled_ring_is_35pct_white),
    cmocka_unit_test(_disabled_suppresses_shadow_and_halo),
    cmocka_unit_test(_value_rect_right_aligned),
    cmocka_unit_test(_value_rect_clamped_when_it_does_not_fit),
    cmocka_unit_test(_pos_x_roundtrip_and_clamp),
    cmocka_unit_test(_value_parse_plain_commits),
    cmocka_unit_test(_value_parse_percent_display_units),
    cmocka_unit_test(_value_parse_clamps_to_bounds),
    cmocka_unit_test(_value_parse_rejects_garbage),
    cmocka_unit_test(_value_parse_accepts_space_and_sign),
    cmocka_unit_test(_value_hit_value_vs_main),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
