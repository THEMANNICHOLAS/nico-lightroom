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

/** The one path from a D65 XYZ colour to a display RGB the GUI may paint.
 *
 * Every on-screen colour guide in this tree -- the colour equalizer's graph backgrounds, the
 * colour primaries' swatches, and now colour balance RGB's wheel -- ends in the same chain:
 * chromatic adaptation to D50, the display profile's matrix (or sRGB when there is none),
 * a max-normalisation of any channel pushed above 1, the display TRC, and a clamp. That chain
 * used to be reachable only through a HSB entry point, so a module working in Ych had to
 * either re-spell it or route through a colour space it does not use. It is now a function of
 * its own, and dt_colorrings_hsb_to_display_rgb() is one caller of it among others.
 *
 * So the first test pins the chain's own contract (white stays white, black stays black, an
 * out-of-gamut input still lands inside the unit cube), the second pins that the refactor
 * moved code and changed no arithmetic -- the six expected triples were measured from the
 * pre-refactor build and are asserted within 1e-6 (bit-identity would trip on the last bit of
 * powf() between mingw's libm and glibc; Linux CI runs this file) -- and the third walks the wheel's own
 * chain, the composition colour balance RGB's disc callback runs, to check that the hue order
 * the user sees is the hue order the parameters mean and that the searched rim chroma is what
 * keeps every hue off the display's clipping ends -- the Yrg cone gamut check does not, it
 * clips to a gamut far wider than any display, which is why the search exists.
 */

#include "pixel/colorequal_shared.h"
#include "common/colorspaces_inline_conversions.h"

#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>

/** Mirrors src/iop/colorbalancergb.c: the hue that sits at the wheel's 12 o'clock (D5, and
 * additive -- never a reflection), and the module's own Ych angle offset. */
// Mirrors WHEEL_DISC_Y / WHEEL_DISC_MAX_C in src/iop/colorbalancergb.c (the disc's Ych reference ramp).
#define TEST_WHEEL_DISC_Y 0.5f
#define TEST_WHEEL_DISC_MAX_C 0.2f
// Mirrors WHEEL_RIM_HUES in src/iop/colorbalancergb.c.
#define TEST_RIM_HUES 360
#define TEST_WHEEL_HUE_ORIGIN 0.f
#define TEST_CBRGB_ANGLE_SHIFT -30.f

static void _xyz_d65_white_maps_to_white_and_black_to_black_without_a_profile(void **state)
{
  (void)state;

  // D65 white, the illuminant Ych_to_XYZ() answers in, must come out as display white.
  dt_aligned_pixel_t white_XYZ = { 0.9505f, 1.0f, 1.089f, 0.f };
  dt_aligned_pixel_t white_RGB = { 0.f };
  dt_colorrings_xyz_d65_to_display_rgb(white_XYZ, NULL, white_RGB);
  for(int c = 0; c < 3; c++)
  {
    if(fabsf(white_RGB[c] - 1.f) > 1e-3f)
      fail_msg("white channel %d = %.9g, off 1 by %.9g", c, white_RGB[c], fabsf(white_RGB[c] - 1.f));
  }

  dt_aligned_pixel_t black_XYZ = { 0.f, 0.f, 0.f, 0.f };
  dt_aligned_pixel_t black_RGB = { -1.f, -1.f, -1.f, -1.f };
  dt_colorrings_xyz_d65_to_display_rgb(black_XYZ, NULL, black_RGB);
  for(int c = 0; c < 3; c++) assert_true(black_RGB[c] == 0.f);

  // Nothing may leave the unit cube, however far outside the display gamut the input sits.
  dt_aligned_pixel_t wide_XYZ = { 0.2f, 1.5f, 0.05f, 0.f };
  dt_aligned_pixel_t wide_RGB = { 0.f };
  dt_colorrings_xyz_d65_to_display_rgb(wide_XYZ, NULL, wide_RGB);
  for(int c = 0; c < 3; c++)
  {
    assert_true(wide_RGB[c] >= 0.f);
    assert_true(wide_RGB[c] <= 1.f);
  }
}

static void _hsb_to_display_rgb_is_unchanged_by_the_refactor(void **state)
{
  (void)state;

  // Measured from the build before dt_colorrings_hsb_to_display_rgb() was refactored to call
  // dt_colorrings_xyz_d65_to_display_rgb(). The refactor moves code; these must not move beyond
  // the last-bit powf() difference between C libraries.
  const float HSB_in[6][3] = { { 0.f, 0.f, 0.5f },    { 60.f, 0.5f, 0.6f },  { 120.f, 1.0f, 0.7f },
                               { 200.f, 0.3f, 0.4f }, { 300.f, 0.8f, 0.9f }, { 359.f, 0.5f, 0.5f } };
  const float expected[6][3] = {
    { 0.46899578f, 0.468915761f, 0.468901575f },  // HSB {0, 0, 0.5}
    { 0.f, 0.715689719f, 0.625904918f },          // HSB {60, 0.5, 0.6}
    { 0.99999994f, 0.f, 0.f },                    // HSB {120, 1, 0.7}
    { 0.488685727f, 0.25394091f, 0.666984916f },  // HSB {200, 0.3, 0.4}
    { 0.99999994f, 0.363548577f, 0.f },           // HSB {300, 0.8, 0.9}
    { 0.851111352f, 0.0680988133f, 0.f },         // HSB {359, 0.5, 0.5}
  };

  const float white = dt_colorrings_graph_white();

  for(int i = 0; i < 6; i++)
  {
    dt_aligned_pixel_t HSB = { HSB_in[i][0], HSB_in[i][1], HSB_in[i][2], 0.f };
    dt_aligned_pixel_t RGB = { 0.f };
    dt_colorrings_hsb_to_display_rgb(HSB, white, NULL, RGB);
    for(int c = 0; c < 3; c++)
    {
      if(fabsf(RGB[c] - expected[i][c]) > 1e-6f)
        fail_msg("HSB {%g, %g, %g} channel %d: %.9g, expected %.9g", HSB_in[i][0], HSB_in[i][1], HSB_in[i][2], c,
                 RGB[c], expected[i][c]);
    }
  }
}

/** The disc chain at one hue and chroma: wheel hue in, display RGB out. */
static void _disc_rim_rgb(const float wheel_hue, const float chroma, dt_aligned_pixel_t RGB)
{
  const float param_hue = fmodf(wheel_hue + TEST_WHEEL_HUE_ORIGIN, 360.f);
  const dt_aligned_pixel_t Ych
      = { TEST_WHEEL_DISC_Y, chroma, (param_hue + TEST_CBRGB_ANGLE_SHIFT) * (float)M_PI / 180.f, 0.f };
  dt_aligned_pixel_t XYZ = { 0.f };
  Ych_to_XYZ(Ych, XYZ);
  dt_colorrings_xyz_d65_to_display_rgb(XYZ, NULL, RGB);
}

/** Which entry of the rim table a wheel hue reads, i.e. the Ych hue _disc_rim_rgb() renders at. */
static int _rim_index_for_wheel_hue(const float wheel_hue)
{
  const float param_hue = fmodf(wheel_hue + TEST_WHEEL_HUE_ORIGIN, 360.f);
  float deg = fmodf(param_hue + TEST_CBRGB_ANGLE_SHIFT, 360.f);
  if(deg < 0.f) deg += 360.f;
  return ((int)lroundf(deg)) % TEST_RIM_HUES;
}

static void _wheel_disc_rim_chroma_fits_the_display_and_keeps_hue_order(void **state)
{
  (void)state;

  const float thetas[6] = { 0.f, 60.f, 120.f, 180.f, 240.f, 300.f };

  // One rim chroma per hue, each searched down from the slider-stop reference ramp's 0.2 until
  // that hue alone fits the display's linear cube.
  static float rim[TEST_RIM_HUES];
  dt_colorrings_ych_display_rim_chroma(TEST_WHEEL_DISC_Y, TEST_WHEEL_DISC_MAX_C, NULL, rim, TEST_RIM_HUES);

  float lowest = rim[0];
  float highest = rim[0];
  for(int h = 0; h < TEST_RIM_HUES; h++)
  {
    if(!(rim[h] > 0.f)) fail_msg("hue %d: rim chroma %.9g is not usable", h, rim[h]);
    if(!(rim[h] <= TEST_WHEEL_DISC_MAX_C))
      fail_msg("hue %d: rim chroma %.9g exceeds the reference ramp's %.9g", h, rim[h], TEST_WHEEL_DISC_MAX_C);
    lowest = fminf(lowest, rim[h]);
    highest = fmaxf(highest, rim[h]);
  }

  // THE regression guard for this table. A single shared chroma -- which is what this was, and
  // what the sRGB numbers make tempting again -- holds every hue at the most constrained hue's
  // ceiling and would make this ratio exactly 1. Measured in sRGB at Y = 0.5: 0.0879 at 163 deg
  // against 0.2 at 89 deg, i.e. 2.28.
  if(!(highest / lowest >= 2.0f))
    fail_msg("rim spread %.9g/%.9g = %.3fx: the table is not per-hue, so the disc is washed out",
             highest, lowest, highest / lowest);

  // Why a search exists at all: at the reference ramp's own 0.2 applied to every hue, some hues
  // clip a channel to 0 and paint a flat, hue-shifted arc. The Yrg cone gamut check does not
  // prevent this -- it clips to the cone, which is far wider than any display.
  int clipped_count = 0;
  for(int i = 0; i < 6; i++)
  {
    dt_aligned_pixel_t RGB = { 0.f };
    _disc_rim_rgb(thetas[i], TEST_WHEEL_DISC_MAX_C, RGB);
    if(fminf(RGB[0], fminf(RGB[1], RGB[2])) == 0.f) clipped_count++;
  }
  assert_true(clipped_count >= 1);

  // At its own rim every hue still reads as a colour. The min channel is deliberately NOT checked
  // for clipping here: sitting on the gamut boundary is the point of the per-hue search, and four
  // of these six hues reach it by driving a channel to exactly 0.
  float primaries[6][3] = { { 0.f } };
  for(int i = 0; i < 6; i++)
  {
    dt_aligned_pixel_t RGB = { 0.f };
    _disc_rim_rgb(thetas[i], rim[_rim_index_for_wheel_hue(thetas[i])], RGB);
    for(int c = 0; c < 3; c++) primaries[i][c] = RGB[c];

    const float hi = fmaxf(RGB[0], fmaxf(RGB[1], RGB[2]));
    if(!(hi >= 0.7f)) fail_msg("theta %g: max channel %.9g is too dark to read", thetas[i], hi);
  }

  // The three primaries must sit where the wheel puts them.
  assert_true(primaries[0][0] > primaries[0][1] && primaries[0][0] > primaries[0][2]); // 0 deg is red
  assert_true(primaries[2][1] > primaries[2][0] && primaries[2][1] > primaries[2][2]); // 120 deg is green
  assert_true(primaries[4][2] > primaries[4][0] && primaries[4][2] > primaries[4][1]); // 240 deg is blue
}

int main(void)
{
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(_xyz_d65_white_maps_to_white_and_black_to_black_without_a_profile),
    cmocka_unit_test(_hsb_to_display_rgb_is_unchanged_by_the_refactor),
    cmocka_unit_test(_wheel_disc_rim_chroma_fits_the_display_and_keeps_hue_order),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
