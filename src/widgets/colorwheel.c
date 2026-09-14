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

#include "widgets/colorwheel.h"
#include "widgets/drawingarea.h"
#include "widgets/widget_settings.h"
#include "widgets/widget_style.h"

#include "math/math.h"
#include "system/macros.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

/* The background is a raster, not a cairo gradient: the colour of a point on the wheel is
 * whatever the owner's callback says, and there is no cairo primitive for "a field of colours
 * I have to ask about". Asking per pixel would be tens of thousands of calls per frame, so the
 * callback is sampled once onto a hue x chroma table and the pixels are bilinear over it. The
 * raster is cached and rebuilt only when the size or the device ratio moves. */

struct _DtColorWheel
{
  GtkDarktableDrawingArea parent;

  dt_color_wheel_color_fn color_fn;
  gpointer color_user_data;

  float hue;    /* degrees clockwise from 12 o'clock, [0, 360) */
  float chroma; /* [0, 1] */

  cairo_surface_t *raster;
  int raster_w, raster_h;
  double raster_ppd;

  dt_color_wheel_drag_t drag;
};

struct _DtColorWheelClass
{
  GtkDarktableDrawingAreaClass parent_class;
};

G_DEFINE_TYPE(DtColorWheel, dt_color_wheel, DTGTK_TYPE_DRAWING_AREA)

enum
{
  VALUE_CHANGED,
  LAST_SIGNAL
};

static guint _signals[LAST_SIGNAL] = { 0 };

/* --- pure geometry ------------------------------------------------------------------- */

dt_color_wheel_geometry_t dt_color_wheel_geometry(const int width, const int height)
{
  dt_color_wheel_geometry_t g = { 0.f, 0.f, 0.f, 0.f, 0.f };

  const float side = (float)MIN(width, height);
  const float half = 0.5f * side;

  g.cx = 0.5f * (float)width;
  g.cy = 0.5f * (float)height;
  g.r_track_out = fmaxf(0.98f * half, 0.f);

  const float track = 0.12f * half;
  const float gap = 0.06f * half;
  g.r_track_in = fmaxf(g.r_track_out - track, 0.f);
  g.r_disc = fmaxf(g.r_track_in - gap, 0.f);

  return g;
}

float dt_color_wheel_point_to_hue(const float dx, const float dy)
{
  /* screen y grows downwards: 12 o'clock is (0, -1) and reads 0, 3 o'clock is (1, 0) and reads 90 */
  float deg = atan2f(dx, -dy) * 180.f / M_PI_F;
  if(deg < 0.f) deg += 360.f;
  if(deg >= 360.f) deg -= 360.f;
  return deg;
}

void dt_color_wheel_hue_to_point(const float hue_deg, const float radius, const float cx, const float cy,
                                 float *x, float *y)
{
  const float rad = hue_deg * M_PI_F / 180.f;
  *x = cx + radius * sinf(rad);
  *y = cy - radius * cosf(rad);
}

dt_color_wheel_drag_t dt_color_wheel_hit_test(const dt_color_wheel_geometry_t *g, const float x, const float y)
{
  const float r = hypotf(x - g->cx, y - g->cy);
  if(r <= g->r_disc) return DT_COLOR_WHEEL_DRAG_CHROMA;
  if(r >= g->r_track_in && r <= g->r_track_out) return DT_COLOR_WHEEL_DRAG_HUE;
  return DT_COLOR_WHEEL_DRAG_NONE;
}

/* --- pure raster --------------------------------------------------------------------- */

static cairo_surface_t *_empty_surface(const double ppd)
{
  cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_surface_set_device_scale(s, ppd > 0.0 ? ppd : 1.0, ppd > 0.0 ? ppd : 1.0);
  return s;
}

cairo_surface_t *dt_color_wheel_raster_build(const int width, const int height, const double ppd,
                                             const dt_color_wheel_geometry_t *g,
                                             dt_color_wheel_color_fn fn, gpointer user_data)
{
  const int W = (int)lrint((double)width * ppd);
  const int H = (int)lrint((double)height * ppd);
  if(W <= 0 || H <= 0 || IS_NULL_PTR(fn)) return _empty_surface(ppd);

  /* the callback, once per table entry and never per pixel */
  float *lut = calloc((size_t)DT_COLOR_WHEEL_LUT_HUES * DT_COLOR_WHEEL_LUT_CHROMAS * 3, sizeof(float));

  for(int i = 0; i < DT_COLOR_WHEEL_LUT_HUES; i++)
    for(int j = 0; j < DT_COLOR_WHEEL_LUT_CHROMAS; j++)
    {
      float *entry = &lut[((size_t)i * DT_COLOR_WHEEL_LUT_CHROMAS + j) * 3];
      fn((float)i, (float)j / (float)(DT_COLOR_WHEEL_LUT_CHROMAS - 1), entry, user_data);
      for(int c = 0; c < 3; c++) entry[c] = CLAMPS(entry[c], 0.f, 1.f);
    }

  const float scale = (float)ppd;
  const float cx_d = g->cx * scale;
  const float cy_d = g->cy * scale;
  const float r_disc_d = g->r_disc * scale;
  const float r_track_in_d = g->r_track_in * scale;
  const float r_track_out_d = g->r_track_out * scale;

  cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  cairo_surface_flush(s);
  uint8_t *data = cairo_image_surface_get_data(s);
  const int stride = cairo_image_surface_get_stride(s);

  for(int py = 0; py < H; py++)
    for(int px = 0; px < W; px++)
    {
      uint32_t *out = (uint32_t *)(data + (size_t)py * stride + (size_t)px * 4);

      /* integer pixel coordinates, no half-pixel offset: that is what makes the centre pixel
       * chroma exactly 0 and the 3-o'clock pixel exactly hue 90 */
      const float dx = (float)px - cx_d;
      const float dy = (float)py - cy_d;
      const float rad = hypotf(dx, dy);

      float a = CLAMPS(r_disc_d - rad + 0.5f, 0.f, 1.f);
      float chroma = (r_disc_d > 0.f) ? CLAMPS(rad / r_disc_d, 0.f, 1.f) : 0.f;
      if(a <= 0.f)
      {
        a = fminf(CLAMPS(rad - r_track_in_d + 0.5f, 0.f, 1.f), CLAMPS(r_track_out_d - rad + 0.5f, 0.f, 1.f));
        chroma = 1.f;
      }
      if(a <= 0.f)
      {
        *out = 0u;
        continue;
      }

      const float hue = (rad > 1e-3f) ? dt_color_wheel_point_to_hue(dx, dy) : 0.f;

      const int i0 = ((int)floorf(hue)) % DT_COLOR_WHEEL_LUT_HUES;
      const int i1 = (i0 + 1) % DT_COLOR_WHEEL_LUT_HUES;
      const float th = hue - floorf(hue);
      const float cf = chroma * (float)(DT_COLOR_WHEEL_LUT_CHROMAS - 1);
      const int j0 = MIN((int)cf, DT_COLOR_WHEEL_LUT_CHROMAS - 2);
      const int j1 = j0 + 1;
      const float tc = cf - (float)j0;

      const float *e00 = &lut[((size_t)i0 * DT_COLOR_WHEEL_LUT_CHROMAS + j0) * 3];
      const float *e01 = &lut[((size_t)i0 * DT_COLOR_WHEEL_LUT_CHROMAS + j1) * 3];
      const float *e10 = &lut[((size_t)i1 * DT_COLOR_WHEEL_LUT_CHROMAS + j0) * 3];
      const float *e11 = &lut[((size_t)i1 * DT_COLOR_WHEEL_LUT_CHROMAS + j1) * 3];

      float rgb[3];
      for(int c = 0; c < 3; c++)
      {
        const float top = e00[c] * (1.f - tc) + e01[c] * tc;
        const float bottom = e10[c] * (1.f - tc) + e11[c] * tc;
        rgb[c] = CLAMPS(top * (1.f - th) + bottom * th, 0.f, 1.f);
      }

      /* premultiplied ARGB32 */
      const uint32_t A = (uint32_t)lrintf(a * 255.f);
      const uint32_t R = (uint32_t)lrintf(rgb[0] * a * 255.f);
      const uint32_t G = (uint32_t)lrintf(rgb[1] * a * 255.f);
      const uint32_t B = (uint32_t)lrintf(rgb[2] * a * 255.f);
      *out = (A << 24) | (R << 16) | (G << 8) | B;
    }

  cairo_surface_mark_dirty(s);
  cairo_surface_set_device_scale(s, ppd, ppd);
  free(lut);
  return s;
}

/* --- widget -------------------------------------------------------------------------- */

static void _ensure_raster(DtColorWheel *self, const int width, const int height, const double ppd)
{
  if(!IS_NULL_PTR(self->raster) && self->raster_w == width && self->raster_h == height
     && self->raster_ppd == ppd)
    return;

  if(!IS_NULL_PTR(self->raster)) cairo_surface_destroy(self->raster);

  const dt_color_wheel_geometry_t g = dt_color_wheel_geometry(width, height);
  self->raster = dt_color_wheel_raster_build(width, height, ppd, &g, self->color_fn, self->color_user_data);
  self->raster_w = width;
  self->raster_h = height;
  self->raster_ppd = ppd;
}

/** The two strokes every handle is drawn with: a wide dark edge under a narrow bright rim, so
 * the handle reads against any colour underneath it. */
static void _stroke_handle(cairo_t *cr, const GdkRGBA *rim, const GdkRGBA *edge, const float x,
                           const float y, const double radius)
{
  cairo_set_source_rgba(cr, edge->red, edge->green, edge->blue, edge->alpha);
  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(3.0));
  cairo_arc(cr, x, y, radius, 0.0, 2.0 * M_PI);
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, rim->red, rim->green, rim->blue, rim->alpha);
  cairo_set_line_width(cr, DT_PIXEL_APPLY_DPI(1.5));
  cairo_arc(cr, x, y, radius, 0.0, 2.0 * M_PI);
  cairo_stroke(cr);
}

static gboolean _draw(GtkWidget *widget, cairo_t *cr)
{
  DtColorWheel *self = DT_COLOR_WHEEL(widget);

  GtkAllocation alloc;
  gtk_widget_get_allocation(widget, &alloc);

  const double ppd = dt_widget_ppd();
  const dt_color_wheel_geometry_t g = dt_color_wheel_geometry(alloc.width, alloc.height);
  _ensure_raster(self, alloc.width, alloc.height, ppd);

  cairo_set_source_surface(cr, self->raster, 0, 0);
  cairo_paint(cr);

  GtkStyleContext *ctx = gtk_widget_get_style_context(widget);
  GdkRGBA rim, edge;
  if(!gtk_style_context_lookup_color(ctx, "colorwheel_handle_rim", &rim))
    rim = (GdkRGBA){ 1., 1., 1., 1. };
  if(!gtk_style_context_lookup_color(ctx, "colorwheel_handle_edge", &edge))
    edge = (GdkRGBA){ 0., 0., 0., .5 };

  /* the puck, in the disc */
  float px = 0.f, py = 0.f;
  dt_color_wheel_hue_to_point(self->hue, self->chroma * g.r_disc, g.cx, g.cy, &px, &py);
  _stroke_handle(cr, &rim, &edge, px, py, DT_PIXEL_APPLY_DPI(5.0));

  /* the hue marker, on the track */
  float hx = 0.f, hy = 0.f;
  dt_color_wheel_hue_to_point(self->hue, 0.5f * (g.r_track_in + g.r_track_out), g.cx, g.cy, &hx, &hy);
  _stroke_handle(cr, &rim, &edge, hx, hy, 0.5f * (g.r_track_out - g.r_track_in));

  return FALSE;
}

static void _set_from_point(DtColorWheel *self, const dt_color_wheel_geometry_t *g, const float x,
                            const float y, const dt_color_wheel_drag_t mode)
{
  const float dx = x - g->cx;
  const float dy = y - g->cy;
  const float r = hypotf(dx, dy);

  if(r > 1e-3f) self->hue = dt_color_wheel_point_to_hue(dx, dy);
  if(mode == DT_COLOR_WHEEL_DRAG_CHROMA)
    self->chroma = (g->r_disc > 0.f) ? CLAMPS(r / g->r_disc, 0.f, 1.f) : 0.f;

  gtk_widget_queue_draw(GTK_WIDGET(self));
  g_signal_emit(self, _signals[VALUE_CHANGED], 0);
}

static gboolean _button_press(GtkWidget *widget, GdkEventButton *event)
{
  if(event->button != 1) return FALSE;

  DtColorWheel *self = DT_COLOR_WHEEL(widget);
  GtkAllocation alloc;
  gtk_widget_get_allocation(widget, &alloc);
  const dt_color_wheel_geometry_t g = dt_color_wheel_geometry(alloc.width, alloc.height);

  self->drag = dt_color_wheel_hit_test(&g, (float)event->x, (float)event->y);
  if(self->drag == DT_COLOR_WHEEL_DRAG_NONE) return FALSE;

  _set_from_point(self, &g, (float)event->x, (float)event->y, self->drag);
  return TRUE;
}

static gboolean _motion_notify(GtkWidget *widget, GdkEventMotion *event)
{
  DtColorWheel *self = DT_COLOR_WHEEL(widget);
  if(self->drag == DT_COLOR_WHEEL_DRAG_NONE) return FALSE;

  GtkAllocation alloc;
  gtk_widget_get_allocation(widget, &alloc);
  const dt_color_wheel_geometry_t g = dt_color_wheel_geometry(alloc.width, alloc.height);

  _set_from_point(self, &g, (float)event->x, (float)event->y, self->drag);
  return TRUE;
}

static gboolean _button_release(GtkWidget *widget, GdkEventButton *event)
{
  (void)event;
  DtColorWheel *self = DT_COLOR_WHEEL(widget);
  const gboolean was = self->drag != DT_COLOR_WHEEL_DRAG_NONE;
  self->drag = DT_COLOR_WHEEL_DRAG_NONE;
  return was;
}

static void _destroy(GtkWidget *widget)
{
  DtColorWheel *self = DT_COLOR_WHEEL(widget);

  /* GTK may run destroy twice */
  if(!IS_NULL_PTR(self->raster))
  {
    cairo_surface_destroy(self->raster);
    self->raster = NULL;
  }

  GTK_WIDGET_CLASS(dt_color_wheel_parent_class)->destroy(widget);
}

static void dt_color_wheel_class_init(DtColorWheelClass *klass)
{
  GtkWidgetClass *widget_class = (GtkWidgetClass *)klass;

  widget_class->draw = _draw;
  widget_class->destroy = _destroy;
  widget_class->button_press_event = _button_press;
  widget_class->button_release_event = _button_release;
  widget_class->motion_notify_event = _motion_notify;

  _signals[VALUE_CHANGED] = g_signal_new("value-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0,
                                         NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static void dt_color_wheel_init(DtColorWheel *self)
{
  GtkWidget *widget = GTK_WIDGET(self);

  /* the base class locks the aspect ratio; no size vfunc of our own */
  self->parent.aspect = 1.0;

  self->hue = 0.f;
  self->chroma = 0.f;
  self->raster = NULL;
  self->raster_w = 0;
  self->raster_h = 0;
  self->raster_ppd = 0.0;
  self->drag = DT_COLOR_WHEEL_DRAG_NONE;

  gtk_widget_add_events(widget, GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
                                    | GDK_POINTER_MOTION_MASK);
  gtk_widget_set_size_request(widget, (int)DT_PIXEL_APPLY_DPI(64), -1);
  dt_gui_add_class(widget, "dt_colorwheel");
}

GtkWidget *dt_color_wheel_new(dt_color_wheel_color_fn fn, gpointer user_data)
{
  DtColorWheel *self = g_object_new(DT_TYPE_COLOR_WHEEL, NULL);
  self->color_fn = fn;
  self->color_user_data = user_data;
  return GTK_WIDGET(self);
}

void dt_color_wheel_set_hue_chroma(DtColorWheel *w, const float hue_deg, const float chroma_frac)
{
  g_return_if_fail(DT_IS_COLOR_WHEEL(w));

  float h = fmodf(hue_deg, 360.f);
  if(h < 0.f) h += 360.f;

  w->hue = h;
  w->chroma = CLAMPS(chroma_frac, 0.f, 1.f);
  gtk_widget_queue_draw(GTK_WIDGET(w));
}

float dt_color_wheel_get_hue(DtColorWheel *w)
{
  g_return_val_if_fail(DT_IS_COLOR_WHEEL(w), 0.f);
  return w->hue;
}

float dt_color_wheel_get_chroma(DtColorWheel *w)
{
  g_return_val_if_fail(DT_IS_COLOR_WHEEL(w), 0.f);
  return w->chroma;
}

void dt_color_wheel_invalidate_background(DtColorWheel *w)
{
  g_return_if_fail(DT_IS_COLOR_WHEEL(w));

  if(!IS_NULL_PTR(w->raster))
  {
    cairo_surface_destroy(w->raster);
    w->raster = NULL;
  }
  gtk_widget_queue_draw(GTK_WIDGET(w));
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
