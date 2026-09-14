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

/* The colour wheel's geometry and its background raster.
 *
 * The widget is a disc of chroma inside a ring of hue, and everything a caller can get wrong
 * about it is an angle convention or a radius: hue runs clockwise from 12 o'clock on a screen
 * whose y grows downwards, the disc is chroma 0 at its centre and 1 at its rim, and the ring
 * is separated from it by a gap that belongs to neither. So the pure functions are pinned by
 * round trips and by direct compass points, and the raster by pixels read back from the
 * surface: the colour callback writes the hue and the chroma it was handed into the red and
 * green channels, which makes every pixel state what the builder asked for it.
 *
 * The callback is also the budget check. It is sampled onto a fixed lookup table once per
 * build, so the number of calls is a property of the table and not of the surface -- a
 * 400x400 wheel must cost exactly what a 140x140 one costs. */

#include "widgets/colorwheel.h"

#include <cairo.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>

static uint32_t _pixel(cairo_surface_t *surface, const int x, const int y)
{
  cairo_surface_flush(surface);
  const uint8_t *data = cairo_image_surface_get_data(surface);
  const int stride = cairo_image_surface_get_stride(surface);
  return *(const uint32_t *)(data + (size_t)y * stride + (size_t)x * 4);
}

/** A channel of a packed ARGB32 word: R at 16, G at 8, B at 0, A at 24. */
static int _chan(const uint32_t word, const int shift)
{
  return (int)((word >> shift) & 0xffu);
}

/** Writes what it was handed into the pixel: red carries the hue, green the chroma. */
static void _encode_fn(float hue, float chroma, float rgb[3], gpointer ud)
{
  assert_true(hue >= 0.f && hue < 360.f);
  assert_true(chroma >= 0.f && chroma <= 1.f);
  rgb[0] = hue / 360.f;
  rgb[1] = chroma;
  rgb[2] = 0.f;
  int *count = (int *)ud;
  if(count) (*count)++;
}

static void _hue_round_trips_and_runs_clockwise(void **state)
{
  (void)state;
  const float hues[] = { 0.f, 90.f, 180.f, 270.f, 359.5f };
  for(int i = 0; i < 5; i++)
  {
    float x = 0.f, y = 0.f;
    dt_color_wheel_hue_to_point(hues[i], 50.f, 100.f, 100.f, &x, &y);
    const float d = dt_color_wheel_point_to_hue(x - 100.f, y - 100.f);
    assert_true(fabsf(d - hues[i]) < 0.01f || fabsf(d - hues[i] - 360.f) < 0.01f
                || fabsf(d - hues[i] + 360.f) < 0.01f);
  }

  /* the compass, on a screen whose y grows downwards */
  assert_true(fabsf(dt_color_wheel_point_to_hue(0.f, -1.f) - 0.f) < 1e-3f);
  assert_true(fabsf(dt_color_wheel_point_to_hue(1.f, 0.f) - 90.f) < 1e-3f);
  assert_true(fabsf(dt_color_wheel_point_to_hue(0.f, 1.f) - 180.f) < 1e-3f);
  assert_true(fabsf(dt_color_wheel_point_to_hue(-1.f, 0.f) - 270.f) < 1e-3f);

  float x = 0.f, y = 0.f;
  dt_color_wheel_hue_to_point(90.f, 50.f, 100.f, 100.f, &x, &y);
  assert_true(fabsf(x - 150.f) < 1e-3f);
  assert_true(fabsf(y - 100.f) < 1e-3f);
}

static void _hit_test_classifies_disc_track_gap_and_outside(void **state)
{
  (void)state;
  const dt_color_wheel_geometry_t g = dt_color_wheel_geometry(200, 200);
  assert_true(g.r_disc < g.r_track_in);
  assert_true(g.r_track_in < g.r_track_out);

  assert_int_equal(dt_color_wheel_hit_test(&g, g.cx, g.cy), DT_COLOR_WHEEL_DRAG_CHROMA);
  assert_int_equal(dt_color_wheel_hit_test(&g, g.cx + 0.5f * g.r_disc, g.cy), DT_COLOR_WHEEL_DRAG_CHROMA);
  assert_int_equal(dt_color_wheel_hit_test(&g, g.cx, g.cy - 0.5f * (g.r_track_in + g.r_track_out)),
                   DT_COLOR_WHEEL_DRAG_HUE);
  assert_int_equal(dt_color_wheel_hit_test(&g, g.cx + 0.5f * (g.r_disc + g.r_track_in), g.cy),
                   DT_COLOR_WHEEL_DRAG_NONE);
  assert_int_equal(dt_color_wheel_hit_test(&g, 0.f, 0.f), DT_COLOR_WHEEL_DRAG_NONE);
}

static void _geometry_centres_a_square_and_survives_degenerate_sizes(void **state)
{
  (void)state;
  const dt_color_wheel_geometry_t g = dt_color_wheel_geometry(300, 200);
  assert_true(fabsf(g.cx - 150.f) < 1e-3f);
  assert_true(fabsf(g.cy - 100.f) < 1e-3f);
  assert_true(g.r_track_out <= 100.f);

  const dt_color_wheel_geometry_t zero = dt_color_wheel_geometry(0, 0);
  const dt_color_wheel_geometry_t one = dt_color_wheel_geometry(1, 1);
  const dt_color_wheel_geometry_t *cases[] = { &zero, &one };
  for(int i = 0; i < 2; i++)
  {
    const float fields[] = { cases[i]->cx,         cases[i]->cy,          cases[i]->r_disc,
                             cases[i]->r_track_in, cases[i]->r_track_out };
    for(int f = 0; f < 5; f++)
    {
      assert_true(isfinite(fields[f]));
      assert_true(fields[f] >= 0.f);
    }
  }
}

static void _raster_encodes_hue_and_chroma_and_is_transparent_off_wheel(void **state)
{
  (void)state;
  int count = 0;
  const dt_color_wheel_geometry_t g = dt_color_wheel_geometry(140, 140);
  cairo_surface_t *s = dt_color_wheel_raster_build(140, 140, 1.0, &g, _encode_fn, &count);
  assert_non_null(s);
  assert_int_equal(cairo_image_surface_get_format(s), CAIRO_FORMAT_ARGB32);
  assert_int_equal(cairo_image_surface_get_width(s), 140);
  assert_int_equal(cairo_image_surface_get_height(s), 140);

  /* the track, at 3 o'clock: hue 90, chroma pinned at 1 */
  const int tx = (int)lrintf(g.cx + 0.5f * (g.r_track_in + g.r_track_out));
  const int ty = (int)lrintf(g.cy);
  const uint32_t track = _pixel(s, tx, ty);
  assert_int_equal(_chan(track, 24), 255);
  assert_true(_chan(track, 16) >= 63 && _chan(track, 16) <= 65);
  assert_int_equal(_chan(track, 8), 255);

  /* the centre of the disc: chroma 0 */
  const uint32_t centre = _pixel(s, (int)g.cx, (int)g.cy);
  assert_int_equal(_chan(centre, 24), 255);
  assert_int_equal(_chan(centre, 8), 0);

  /* just inside the rim of the disc, at 3 o'clock: chroma near 1, hue 90 */
  const uint32_t rim = _pixel(s, (int)lrintf(g.cx + g.r_disc - 2.f), (int)g.cy);
  assert_int_equal(_chan(rim, 24), 255);
  assert_true(_chan(rim, 8) >= 240);
  assert_true(_chan(rim, 16) >= 63 && _chan(rim, 16) <= 65);

  /* the gap belongs to neither, and neither does the corner */
  assert_int_equal(_pixel(s, (int)lrintf(g.cx + 0.5f * (g.r_disc + g.r_track_in)), (int)g.cy), 0u);
  assert_int_equal(_pixel(s, 0, 0), 0u);

  cairo_surface_destroy(s);
}

static void _raster_at_ppd_2_doubles_pixels_and_sets_device_scale(void **state)
{
  (void)state;
  int count = 0;
  const dt_color_wheel_geometry_t g = dt_color_wheel_geometry(140, 140);
  const int tx = (int)lrintf(g.cx + 0.5f * (g.r_track_in + g.r_track_out));
  const int ty = (int)lrintf(g.cy);

  cairo_surface_t *one = dt_color_wheel_raster_build(140, 140, 1.0, &g, _encode_fn, &count);
  const int red_1 = _chan(_pixel(one, tx, ty), 16);
  cairo_surface_destroy(one);

  cairo_surface_t *s = dt_color_wheel_raster_build(140, 140, 2.0, &g, _encode_fn, &count);
  assert_non_null(s);
  assert_int_equal(cairo_image_surface_get_width(s), 280);
  assert_int_equal(cairo_image_surface_get_height(s), 280);
  double sx = 0.0, sy = 0.0;
  cairo_surface_get_device_scale(s, &sx, &sy);
  assert_true(fabs(sx - 2.0) < 1e-9);
  assert_true(fabs(sy - 2.0) < 1e-9);

  const uint32_t track = _pixel(s, 2 * tx, 2 * ty);
  assert_true(abs(_chan(track, 16) - red_1) <= 1);
  assert_int_equal(_chan(track, 8), 255);
  assert_int_equal(_chan(_pixel(s, 2 * (int)g.cx, 2 * (int)g.cy), 8), 0);

  cairo_surface_destroy(s);
}

static void _raster_calls_the_callback_at_most_lut_size_times(void **state)
{
  (void)state;
  const int budget = DT_COLOR_WHEEL_LUT_HUES * DT_COLOR_WHEEL_LUT_CHROMAS;

  int small_count = 0;
  const dt_color_wheel_geometry_t small = dt_color_wheel_geometry(140, 140);
  cairo_surface_t *s = dt_color_wheel_raster_build(140, 140, 1.0, &small, _encode_fn, &small_count);
  cairo_surface_destroy(s);
  assert_true(small_count > 0);
  assert_true(small_count <= budget);

  int big_count = 0;
  const dt_color_wheel_geometry_t big = dt_color_wheel_geometry(400, 400);
  cairo_surface_t *b = dt_color_wheel_raster_build(400, 400, 1.0, &big, _encode_fn, &big_count);
  cairo_surface_destroy(b);
  assert_true(big_count > 0);
  assert_true(big_count <= budget);

  /* the cost is the table's, not the surface's */
  assert_int_equal(small_count, big_count);
}

static void _count(GtkWidget *widget, gpointer user_data)
{
  (void)widget;
  int *n = (int *)user_data;
  (*n)++;
}

static void _setter_emits_nothing_and_getters_wrap_and_clamp(void **state)
{
  (void)state;
  int calls = 0;
  GtkWidget *w = dt_color_wheel_new(_encode_fn, &calls);
  assert_non_null(w);
  g_object_ref_sink(w);

  int emitted = 0;
  g_signal_connect(G_OBJECT(w), "value-changed", G_CALLBACK(_count), &emitted);

  dt_color_wheel_set_hue_chroma(DT_COLOR_WHEEL(w), 370.f, 1.5f);
  assert_int_equal(emitted, 0);
  assert_true(fabsf(dt_color_wheel_get_hue(DT_COLOR_WHEEL(w)) - 10.f) < 1e-3f);
  assert_true(fabsf(dt_color_wheel_get_chroma(DT_COLOR_WHEEL(w)) - 1.f) < 1e-6f);

  dt_color_wheel_set_hue_chroma(DT_COLOR_WHEEL(w), -30.f, -0.5f);
  assert_true(fabsf(dt_color_wheel_get_hue(DT_COLOR_WHEEL(w)) - 330.f) < 1e-3f);
  assert_true(fabsf(dt_color_wheel_get_chroma(DT_COLOR_WHEEL(w))) < 1e-6f);
  assert_int_equal(emitted, 0);

  gtk_widget_destroy(w);
  g_object_unref(w);
}

int main(int argc, char **argv)
{
  const struct CMUnitTest pure[] = {
    cmocka_unit_test(_hue_round_trips_and_runs_clockwise),
    cmocka_unit_test(_hit_test_classifies_disc_track_gap_and_outside),
    cmocka_unit_test(_geometry_centres_a_square_and_survives_degenerate_sizes),
    cmocka_unit_test(_raster_encodes_hue_and_chroma_and_is_transparent_off_wheel),
    cmocka_unit_test(_raster_at_ppd_2_doubles_pixels_and_sets_device_scale),
    cmocka_unit_test(_raster_calls_the_callback_at_most_lut_size_times),
  };
  const int rc = cmocka_run_group_tests_name("colorwheel-pure", pure, NULL, NULL);
  if(rc) return rc;

  if(!gtk_init_check(&argc, &argv))
  {
    fprintf(stderr, "no display: widget group skipped\n");
    return 77;
  }

  const struct CMUnitTest widget[] = {
    cmocka_unit_test(_setter_emits_nothing_and_getters_wrap_and_clamp),
  };
  return cmocka_run_group_tests_name("colorwheel-widget", widget, NULL, NULL);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
