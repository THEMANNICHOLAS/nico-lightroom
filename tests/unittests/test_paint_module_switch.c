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

/* The module enable switch, pixel by pixel.
 *
 * The glyph is the whole contract of these two paint functions: an enabled module shows a
 * filled disc, a disabled one a hollow ring of the same outer edge, and a module whose
 * switch is hidden shows a padlock that stays inside its 26 px box. The colour is the
 * widget's business (CSS), so only alpha is read here; and a prelight must change nothing
 * about the glyph, since the hover feedback belongs to the button, not to the drawing. */

#include "widgets/paint.h"

#include <cairo.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#define BOX 26

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

/* Paint one glyph on its own 26x26 surface. The default source is opaque black: the paint
 * functions never set a colour, so alpha is all there is to read. */
static cairo_surface_t *_paint(void (*fn)(cairo_t *, gint, gint, gint, gint, gint, void *), const gint flags)
{
  cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, BOX, BOX);
  cairo_t *cr = cairo_create(s);
  fn(cr, 0, 0, BOX, BOX, flags, NULL);
  cairo_destroy(cr);
  return s;
}

static void _active_switch_paints_a_filled_disc(void **state)
{
  (void)state;
  cairo_surface_t *s = _paint(dtgtk_cairo_paint_module_switch, CPF_ACTIVE);
  assert_true(_alpha(s, 13, 13) > 200);   /* the centre of a disc of r = 0.21 * 26 = 5.46 px */
  assert_true(_alpha(s, 17, 13) > 200);   /* spans r 4.03..5.10: wholly inside the disc */
  assert_int_equal(_alpha(s, 21, 13), 0); /* r = 8.02: outside it */
  cairo_surface_destroy(s);
}

static void _inactive_switch_paints_a_hollow_ring(void **state)
{
  (void)state;
  cairo_surface_t *s = _paint(dtgtk_cairo_paint_module_switch, 0);
  /* the hollow: the stroke band is [3.98, 5.48] px from the centre */
  assert_int_equal(_alpha(s, 13, 13), 0);
  assert_int_equal(_alpha(s, 15, 13), 0); /* r = 2.06 */
  assert_true(_alpha(s, 17, 13) > 200);   /* spans r 4.03..5.10: wholly inside the band */
  assert_int_equal(_alpha(s, 21, 13), 0); /* outside it */
  cairo_surface_destroy(s);
}

static void _prelight_does_not_change_the_inactive_glyph(void **state)
{
  (void)state;
  cairo_surface_t *plain = _paint(dtgtk_cairo_paint_module_switch, 0);
  cairo_surface_t *lit = _paint(dtgtk_cairo_paint_module_switch, CPF_PRELIGHT);
  cairo_surface_flush(plain);
  cairo_surface_flush(lit);
  const int stride = cairo_image_surface_get_stride(plain);
  assert_int_equal(stride, cairo_image_surface_get_stride(lit));
  assert_int_equal(memcmp(cairo_image_surface_get_data(plain), cairo_image_surface_get_data(lit),
                          (size_t)stride * BOX),
                   0);
  cairo_surface_destroy(plain);
  cairo_surface_destroy(lit);
}

static void _locked_switch_paints_a_padlock_inside_the_box(void **state)
{
  (void)state;
  cairo_surface_t *s = _paint(dtgtk_cairo_paint_module_switch_on, 0);
  assert_true(_alpha(s, 13, 16) > 200);   /* the padlock body */
  assert_int_equal(_alpha(s, 13, 10), 0); /* the shackle's opening */
  assert_true(_alpha(s, 13, 7) > 0);      /* some ink at the shackle's apex */

  /* nothing painted may touch the edges of the 26 px box */
  int min_x = BOX, min_y = BOX, max_x = -1, max_y = -1;
  for(int y = 0; y < BOX; y++)
    for(int x = 0; x < BOX; x++)
      if(_alpha(s, x, y) > 0)
      {
        if(x < min_x) min_x = x;
        if(y < min_y) min_y = y;
        if(x > max_x) max_x = x;
        if(y > max_y) max_y = y;
      }
  assert_true(min_x >= 1);
  assert_true(min_y >= 1);
  assert_true(max_x <= 24);
  assert_true(max_y <= 24);
  cairo_surface_destroy(s);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(_active_switch_paints_a_filled_disc),
    cmocka_unit_test(_inactive_switch_paints_a_hollow_ring),
    cmocka_unit_test(_prelight_does_not_change_the_inactive_glyph),
    cmocka_unit_test(_locked_switch_paints_a_padlock_inside_the_box),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
