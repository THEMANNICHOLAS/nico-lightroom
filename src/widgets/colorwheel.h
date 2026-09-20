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

#ifndef DT_WIDGETS_COLORWHEEL_H
#define DT_WIDGETS_COLORWHEEL_H

/* A hue/chroma colour wheel: a disc of chroma inside a ring of hue.
 *
 * The widget knows no colour science. Which display colour a (hue, chroma) pair paints is
 * answered by the callback its owner hands it, so the same widget serves whatever space the
 * consuming module works in -- the widget only ever deals in an angle and a fraction.
 *
 * Its geometry and its background raster are plain functions of a size, with no GTK object
 * involved, because that is the whole of what can be got wrong here and it is what a test can
 * hold. Angles run CLOCKWISE from 12 o'clock, on a screen whose y grows downwards; radii and
 * coordinates are LOGICAL pixels, which the raster builder scales by the device ratio itself. */

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define DT_TYPE_COLOR_WHEEL (dt_color_wheel_get_type())
#define DT_COLOR_WHEEL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), DT_TYPE_COLOR_WHEEL, DtColorWheel))
#define DT_IS_COLOR_WHEEL(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), DT_TYPE_COLOR_WHEEL))

/* Opaque: the instance and class layouts live in colorwheel.c. The cast macros above work on
 * an incomplete type, so nothing outside needs to see the fields. */
typedef struct _DtColorWheel DtColorWheel;
typedef struct _DtColorWheelClass DtColorWheelClass;

GType dt_color_wheel_get_type(void);

/* The background raster samples the colour callback on a fixed table and interpolates between
 * its entries, so the callback costs the table and never a pixel. */
#define DT_COLOR_WHEEL_LUT_HUES 360
#define DT_COLOR_WHEEL_LUT_CHROMAS 33

/** What a press landed on, and therefore what a drag edits. */
typedef enum dt_color_wheel_drag_t
{
  DT_COLOR_WHEEL_DRAG_NONE = 0,
  DT_COLOR_WHEEL_DRAG_HUE,
  DT_COLOR_WHEEL_DRAG_CHROMA
} dt_color_wheel_drag_t;

/** The display colour of a wheel position. `hue_deg` is in [0, 360) clockwise from 12 o'clock,
 * `chroma_frac` in [0, 1], and `rgb_out` is display RGB in [0, 1]. */
typedef void (*dt_color_wheel_color_fn)(float hue_deg, float chroma_frac, float rgb_out[3],
                                        gpointer user_data);

/** The wheel's layout inside a `width` x `height` allocation, in LOGICAL pixels: the centre,
 * the chroma disc's radius, and the hue track's inner and outer radii. The two are separated
 * by a gap that belongs to neither. */
typedef struct dt_color_wheel_geometry_t
{
  float cx, cy, r_disc, r_track_in, r_track_out;
} dt_color_wheel_geometry_t;

/** The layout for an allocation, in logical pixels. Every field is finite and non-negative,
 * a degenerate size included. */
dt_color_wheel_geometry_t dt_color_wheel_geometry(int width, int height);

/** The hue of an offset from the centre, in degrees clockwise from 12 o'clock, [0, 360).
 * Screen convention: y grows downwards, so (0, -1) is 0 and (1, 0) is 90. */
float dt_color_wheel_point_to_hue(float dx, float dy);

/** The point at `hue_deg` and `radius` around (`cx`, `cy`), in the same screen convention. */
void dt_color_wheel_hue_to_point(float hue_deg, float radius, float cx, float cy, float *x, float *y);

/** What the logical-pixel point (`x`, `y`) lands on: the chroma disc, the hue track, or
 * neither -- the gap between them and everything outside. */
dt_color_wheel_drag_t dt_color_wheel_hit_test(const dt_color_wheel_geometry_t *g, float x, float y);

/** The wheel's background: an ARGB32 premultiplied surface of `width` x `height` LOGICAL
 * pixels, allocated at `ppd` device pixels per logical one and carrying that device scale,
 * transparent outside the disc and the track. `g` is in logical units and scaled here. Never
 * returns NULL. The caller destroys the surface. */
cairo_surface_t *dt_color_wheel_raster_build(int width, int height, double ppd,
                                             const dt_color_wheel_geometry_t *g,
                                             dt_color_wheel_color_fn fn, gpointer user_data);

/** A wheel painting the colours `fn` answers with. */
GtkWidget *dt_color_wheel_new(dt_color_wheel_color_fn fn, gpointer user_data);

/** Set the puck's position: hue wrapped into [0, 360), chroma clamped to [0, 1]. Redraws and
 * emits NOTHING -- "value-changed" is the user's edits alone, and of those only the press and
 * the release: the puck follows a drag's motion events silently. */
void dt_color_wheel_set_hue_chroma(DtColorWheel *w, float hue_deg, float chroma_frac);

/** The current hue, in degrees clockwise from 12 o'clock. */
float dt_color_wheel_get_hue(DtColorWheel *w);

/** The current chroma, in [0, 1]. */
float dt_color_wheel_get_chroma(DtColorWheel *w);

/** Drop the cached background raster, so the next draw asks the colour callback again. */
void dt_color_wheel_invalidate_background(DtColorWheel *w);

G_END_DECLS

#endif // DT_WIDGETS_COLORWHEEL_H

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
