# PLAN: Colour wheel for colorbalancergb 4-ways

**Status:** In Progress
**Created:** 2026-09-13
**Type:** Single plan

## Context
Lightroom's Color Grading exposes shadows/midtones/highlights/global as hue/chroma wheels with a
luminance slider. Ansel already has the exact parameters in `colorbalancergb`'s "4 ways" page,
but only as twelve bauhaus sliders. A hue/chroma wheel per zone makes the same params legible
and draggable. A GTK3 draft widget ("Halo", uncompiled) was handed over; this plan lands it as
a self-contained `src/widgets/` widget in the flat house style and binds one wheel per zone in
`colorbalancergb.c`, with no params, pipeline or version change. `colorequal` is untouched: it
is the HSL-panel equivalent (per-hue splines), not the colour-grading one.

## Background
- `colorbalancergb` params (v5) carry `{global,shadows,highlights,midtones}_{Y,C,H}`:
  `_H` degrees 0..360 with a GUI-only shift `ANGLE_SHIFT = -30` applied by `DEG_TO_RAD()`
  (`src/iop/colorbalancergb.c:74-78`), `_C` 0..1 with per-zone soft maxima (global 0.0075,
  shadows 0.375, highlights 0.15, midtones 0.075, set in `gui_init` ~1894-1970), `_Y` -1..1.
  `commit_params` converts `_H` once, `H -> Ych -> gradingRGB` (`:1166-1195`); the hue
  convention is `Ych_to_Yrg`'s `r = c*cos(h), g = c*sin(h)`
  (`src/common/colorspaces_inline_conversions.h:1108-1117`), so `H = 0` is red (the hue
  slider's stop ramp sweeps `H = stop*360` left to right and starts at red, `:2056-2070`).
- The draft widget measures hue clockwise from 12 o'clock. Because the disc is painted through
  the SAME wheel->param mapping the drag handler uses, any bijection is internally consistent;
  the only real constraints are that red sits where the design wants it and that hue runs round
  the disc in the same order as the module's own hue slider (R, Y, G, C, B, M). An additive map
  does that; a reflection would run the disc backwards relative to the slider.
- `src/widgets/` is GTK/cairo/glib only (`src/widgets/README.md`; CI gate
  `tools/check_module_boundaries.sh` rule 2 forbids only `gui/`). The draft included
  `colorprofiles/iop_profile.h` and `pixel/colorequal_shared.h`; the Design Gate replaced that
  with a caller-supplied colour callback so colour management stays in the IOP.
- The draft's other blockers: not in `ANSEL_WIDGETS_SOURCES`, `#pragma once` (forbidden),
  header over-including, unguarded divide at zero size. Its ARGB32 packing, GObject lifecycle
  and `dtgtk_gradient_slider_*` calls were verified correct.
- Two-slider-set commits: `color_picker_apply()` (`colorbalancergb.c:1400-1454`) writes the
  params by hand FIRST (`p->shadows_H = ...; p->shadows_C = ...;`, `:1414-1422`), then wraps its
  `dt_bauhaus_slider_set()` calls in `dt_gui_freeze_begin()/dt_gui_freeze_end()`, then calls the
  module's own `gui_changed()` + one `dt_dev_add_history_item()`. Under freeze a slider set does
  NOT write params: `_commit_slider_value()` returns early on `dt_gui_widgets_suppressed()`
  (`src/widgets/bauhaus.c:3441`), and that early-out skips the only param writer
  (`src/develop/imageop_gui.c:2094-2110`). The wheel binding mirrors the full sequence.
- `dt_iop_gui_update()` already wraps every module `gui_update` in freeze
  (`src/develop/imageop_gui.c:1178-1196`), and colorbalancergb's `gui_changed` adds its own
  (`:1704,1720`). House rule for any value handler: open with
  `if(dt_gui_widgets_suppressed()) return;` (`src/widgets/widget_settings.h:62-63`, already
  used at `colorbalancergb.c:1509`). `dt_iop_gui_changed()` (framework) commits history itself
  (`imageop_gui.c:2023-2037`); the module's static `gui_changed()` does not.
- `dt_dev_add_history_item()` coalesces consecutive edits of one module into the top item
  (`dev_history.c:892-931`, `force_new_item = FALSE` at `:1042`); a drag does not flood history.
- `src/pixel/colorequal_shared.c` holds the display chain as three file-static functions with
  no callers outside the file (`:42,63,90`): D50 XYZ -> profile linear RGB -> display RGB, where
  the last step max-NORMALISES any channel above 1 (`:63-88`, same intent as the slider stops'
  `RGB[c]/max_RGB` at `colorbalancergb.c:2064-2065`) and the public wrappers then clamp to
  [0,1] (`:218-222`). Negative channels are never lifted. Two public entry points exist:
  `dt_colorrings_hsb_to_display_rgb()` (`:217`, dt-UCS HSB in) and
  `dt_colorrings_profile_rgb_to_display_rgb()` (`:225-241`, profile RGB in). `colorbalancergb`
  works in Ych; `Ych_to_XYZ` outputs **D65** (`colorspaces_inline_conversions.h:1185-1193`).
  One public XYZ_D65 entry point is added beside the existing two. No unit test covers this
  file today.
- `colorequal.c` fetches its display profile with
  `dt_ioppr_get_pipe_output_profile_info(self->dev->preview_pipe)` guarded on `dev` and
  `preview_pipe`, AT DRAW TIME (`:1172-1174`), and keys its background cache on the profile
  pointer (`:1191-1245`). The read is a bare pointer read (`src/develop/iop_profile.c:233-236`);
  it is safe because the pointer is only compared, never dereferenced after a mismatch.
- `src/widgets/drawingarea.{c,h}` already provides a `GtkDrawingArea` subclass with a locked
  aspect ratio (`dtgtk_drawing_area_new_with_aspect_ratio()`, `drawingarea.c:27-45`).
- Layering: `tools/include_graph.py:40-51` places `widgets/` at layer 2.5, above `pixel/` (2) and
  `colorprofiles/` (1), so the draft's includes were NOT layering violations and every CI gate
  would have passed. `src/widgets/README.md:6` states the stricter GTK/cairo/glib-only rule.
  The concrete cost that decides D2 is what `colorprofiles/iop_profile.h:72-88` drags in:
  `common/colorspaces_inline_conversions.h` and `<CL/cl.h>`.

## Codebase Map
- Entry points: `src/iop/colorbalancergb.c` — `gui_init` (~1796, notebook via `dt_ui_notebook_new()`,
  page `dt_ui_notebook_page(g->notebook, N_("4 ways"), ...)` at ~1894), `gui_update` (~1725,
  sets all 12 sliders; frozen by its caller `dt_iop_gui_update()`), `gui_changed`,
  `color_picker_apply` (~1400).
  Zone order and section labels on the page: "Global offset", "Shadows lift", "Highlights gain",
  "Power" (midtones); per zone Y, H, C sliders; labels set at ~2030-2039 to "Luminance",
  "Hue", "Chroma". gui_data fields `g->{global,shadows,highlights,midtones}_{Y,H,C}`.
- Widget layer: `src/widgets/CMakeLists.txt` explicit `set(ANSEL_WIDGETS_SOURCES ...)` (lines
  11-38, no glob); `src/widgets/README.md` dependency rule; exemplar drawing-area widget
  `src/widgets/bauhaus.c` (`G_DEFINE_TYPE(... GTK_TYPE_DRAWING_AREA)` :86, class_init :1112,
  theme colours via `gtk_style_context_lookup_color(ctx, "bauhaus_fill", ...)` :1143-1179,
  style class via `dt_gui_add_class(w, "dt_bauhaus")` :1397). No `gtk_widget_class_set_css_name`
  anywhere in `src/`: theme targets `.dt_bauhaus` classes and `#id` names, never node names.
- Reuse targets: `dt_gui_freeze_begin/end()` (`src/widgets/widget_settings.c:198,211`),
  `dt_gui_widgets_suppressed()` (`widget_settings.h:62`); `dt_widget_ppd()`,
  `DT_PIXEL_APPLY_DPI` (`widget_settings.h`); `dt_gui_add_class()` (`src/widgets/widget_style.h:36`);
  `DTGTK_TYPE_DRAWING_AREA` / `dtgtk_drawing_area_new_with_aspect_ratio()`
  (`src/widgets/drawingarea.{c,h}`); `M_PI_F`, `CLAMPS` (`src/math/math.h`); `IS_NULL_PTR`
  (`src/system/macros.h`); `Ych_to_XYZ`, `gamut_check_Yrg()`
  (`src/common/colorspaces_inline_conversions.h:1185,1202`), `DEG_TO_RAD` (colorbalancergb.c);
  `dt_ioppr_get_pipe_output_profile_info()`; `dt_iop_color_picker_reset()` as called at
  `imageop_gui.c:2030`; `src/pixel/colorequal_shared.{h,c}` display chain.
- Scroll precedent: colorbalancergb's own drawing area returns FALSE from its scroll callback so
  the side panel keeps scrolling (`area_scroll_callback`, `colorbalancergb.c:1789,1989`).
- Theme: `data/themes/ansel.css` — `@bauhaus_*` block lines 148-163; `@grey_100` :94,
  `@grey_00` :68 defined; `.dt_bauhaus` rules ~970, 1875-1882; universal `box-shadow: none`
  reset :254-263. Only one theme file exists.
- Tests: cmocka, `tests/unittests/`, registered in `tests/unittests/CMakeLists.txt` list
  `LIB_ANSEL_UNIT_TESTS` (~lines 34-53), linked against `lib_ansel`, include root `src/`.
  Exemplar for pure-cairo pixel readback: `tests/unittests/test_stroke_raster.c`
  (`ansel_widgets` is whole-archive linked into `lib_ansel`, `src/CMakeLists.txt:862,864`).
  A test that needs a live widget calls `gtk_init_check()` and returns 77 when there is no
  display, registered with `SKIP_RETURN_CODE 77` (`test_tagging_completion.c:281`,
  `test_accel_map_defaults.c:146`, `tests/unittests/CMakeLists.txt:89`).
- Commands: `./build.sh` (Ninja, `build/`); full `ninja -C build` (IOPs and views are plugins,
  `ninja ansel` does not compile them); `ctest --test-dir build -R <name>`;
  gates listed under `## Verification`.

## Non-Goals
- No change to `colorequal` (spline equalizer stays as is).
- No params struct change, no version bump, no pipeline change: exports must stay pixel-identical.
- No Halo shadows/halo gradient (flat "Hairline" style chosen); no separate brightness
  gradient slider or `dt_color_wheel_pair_new()` helper — the existing "Luminance" slider is
  the per-zone luminance control.
- No colour-managing of the existing bauhaus slider stops (fixed Rec709/gamma 2.2 today);
  noted as adjacent work.
- No removal of the H/C/Y sliders; the wheel is an additional control kept in sync.

## Design Decisions
### D1: Which module owns the wheel
- **Chosen:** `colorbalancergb` 4-ways page, one wheel per zone, bound 1:1 to existing `_H`/`_C`.
- **Rejected:** `colorequal` — a wheel is strictly less expressive than its per-hue splines and
  would need a new params model. Both — doubles binding/calibration work.
- **Consequences:** no data-model work anywhere; all risk is GUI binding and hue convention.

### D2: How the widget gets its colours
- **Chosen:** caller-supplied callback `dt_color_wheel_color_fn(hue_deg, chroma_frac, rgb_out,
  user_data)`; the widget knows nothing about profiles. `src/widgets/colorwheel.c` depends on
  GTK/cairo/glib plus `math/math.h`, `system/macros.h`, `widgets/widget_settings.h` only.
- **Rejected:** include `pixel/`/`colorprofiles/` in the widget. Every CI gate passes with it
  (widgets sit at layer 2.5, above both), so an implementor who finds the gates green must NOT
  read that as permission: the reason is README's GTK/cairo/glib rule and the concrete drag of
  `common/colorspaces_inline_conversions.h` and `<CL/cl.h>` into the widget layer. Widget in
  `src/iop/` — not reusable, bloats a 2100-line file.
- **Consequences:** colour management lives in `colorbalancergb.c`; the display chain is
  exposed from `colorequal_shared` as one XYZ entry point (Phase 2) instead of duplicated. A
  callback is a departure from the tree's push-based colour pattern
  (`dt_bauhaus_slider_set_stop`, `dtgtk_gradient_slider_set_stop`); it is justified because a
  2-D hue x chroma field has no small stop list to push. To bound its cost the widget samples
  the callback on a coarse polar grid and interpolates (Phase 1 contract), never per pixel.

### D3: What the disc shows vs what the puck means
- **Chosen:** the disc paints a fixed, legible display ramp (Ych ~~`Y = 0.75`~~ ~~`Y = 0.6`~~ `Y = 0.5`,
  chroma 0 at centre to ~~`0.2`~~ ~~the searched in-gamut rim~~ that HUE'S OWN searched in-gamut rim at rim — ~~the same reference as the existing slider stops~~
  the slider stops' Y = 0.75 / C = 0.2 only look saturated because they max-normalise and never stay in
  gamut; struck 2026-09-17, see Reconciliations) through the display
  profile. The puck's radius maps linearly to param chroma over the zone's soft maximum;
  a param above the soft max draws the puck clamped at the rim.
- **Rejected:** paint the true param chroma — global's soft max 0.0075 renders as grey, useless
  as a hue picker.
- **Consequences:** hue under the puck is exact (same `DEG_TO_RAD` path as the pipeline);
  displayed saturation is symbolic. Document in the slider tooltip, not in code. The display
  chain max-normalises channels above 1 but never lifts negative ones, so saturated blues and
  greens at C = 0.2 would clip a channel to 0 and paint a flat, hue-shifted arc: ~~the callback
  runs `gamut_check_Yrg()` on the Ych BEFORE `Ych_to_XYZ` so the rim chroma is per-hue
  in-gamut, instead of clamping afterwards~~ (struck 2026-09-17, see Reconciliations: ~~the rim
  chroma is one binary-searched constant that fits every hue in the display cube, the
  construction `_compute_reference_saturation()` already uses for the colorequal rings~~ — the
  uniform constant is itself struck 2026-09-19, see Reconciliations: one rim per hue, each
  binary-searched against the display cube).

### D4: Programmatic setters do not emit
- **Chosen:** `dt_color_wheel_set_hue_chroma()` never emits `"value-changed"`; only pointer
  interaction does. The framework already freezes `gui_update` (`dt_iop_gui_update()`), so
  this is an API simplicity choice, not a loop guard: it lets the module sync the wheel from
  anywhere without reasoning about freeze depth.
- **Rejected:** bauhaus-style emit-on-set — adds a second place where a missed freeze creates
  a history item.
- **Consequences:** module code that wants a commit after a programmatic set must commit
  itself. ~~The one unfrozen path in the framework (`_iop_reset_label_reset`,
  `imageop_gui.c:2215`) is covered by the house rule, not by D4: the wheel's value handler
  opens with `if(dt_gui_widgets_suppressed()) return;` like every other value handler.~~
  (struck 2026-09-19, see Reconciliations: inverted — the guard cannot fire on an unfrozen
  path; D4 is what covers it.)

### D5: Hue convention seam
- **Chosen:** the widget's hue is degrees clockwise from 12 o'clock, convention-free. The
  module owns an ADDITIVE mapping pair,
  `_wheel_to_param_hue(theta) = fmodf(theta + WHEEL_HUE_ORIGIN, 360.f)` and
  `_param_to_wheel_hue(H) = fmodf(H - WHEEL_HUE_ORIGIN + 360.f, 360.f)`, with
  `WHEEL_HUE_ORIGIN` a `#define` of `0.f`: red (`H = 0`) at 12 o'clock, then Y, G, C, B, M
  clockwise — the same order as the module's own hue slider reads left to right. The disc
  callback and the drag handler both go through the pair, so the colour under the puck equals
  the hue the param applies **by construction**.
- **Rejected:** a reflection (`ORIGIN - theta`) — runs the disc backwards relative to the
  slider, and with 120 as origin put green at 12 and red at 4 o'clock; a per-caller direction
  flag in the widget (a second convention to keep in sync); an offset baked into the widget.
- **Consequences:** O1 is closed by derivation, not measurement; Phase 2's manual check only
  confirms it. Any second consumer (e.g. `colorequal`, +20 deg in radians) writes its own pair;
  the widget stays untouched.

## Progress
- [x] Phase 1: Widget core with pure, testable geometry and raster
- [x] Phase 2: Tracer bullet — shadows wheel bound in colorbalancergb
- [x] Phase 3: All four zones and page layout
- [x] Phase 4: Theme and docs
- [ ] Final verification

## Phases

### Phase 1: Widget core with pure, testable geometry and raster
**Risk:** flagged (!#1)
**Test-first:** required
**Goal:** `src/widgets/colorwheel.{c,h}` compiles into `lib_ansel` as a flat-style
`GtkDrawingArea` subclass whose geometry, hit-testing and background raster are plain C
functions covered by cmocka.
**Assumes:**
- ~~The draft at the handoff directory (`ansel-halo-wheel/colorwheel.{c,h}`) is the starting
  text; its `dtgtk_gradient_slider_*`, halo/shadow, and profile code is deleted, not ported.~~
  (struck 2026-09-13, see Reconciliations: the draft is not on this machine; the widget is
  written from the Contracts.)
**Files:**
- `src/widgets/colorwheel.h` — new; guard `DT_WIDGETS_COLORWHEEL_H`; includes `<gtk/gtk.h>` only.
- `src/widgets/colorwheel.c` — new; adapted from the draft per Contracts below.
- `src/widgets/CMakeLists.txt` — add `colorwheel.c` to `ANSEL_WIDGETS_SOURCES`.
- `tests/unittests/test_colorwheel.c` — new.
- `tests/unittests/CMakeLists.txt` — append `test_colorwheel` to `LIB_ANSEL_UNIT_TESTS`.

**Reuse:**
- Derive from `DTGTK_TYPE_DRAWING_AREA` (`src/widgets/drawingarea.{c,h}`) with aspect ratio
  1.0 — do NOT hand-roll `get_request_mode` / `get_preferred_*` for the square lock; delete
  those overrides from the draft.
- Pattern to mirror: `src/widgets/bauhaus.c` — `G_DEFINE_TYPE`, theme colours via
  `gtk_style_context_lookup_color()` with hardcoded fallback (`:1143-1179`), style class via
  `dt_gui_add_class(w, "dt_colorwheel")` (`widget_style.h`).
- Keep the draft's `widget_class->destroy` override freeing the cached surface (GTK tree
  teardown runs it; `IOP_GUI_FREE` does not destroy widgets).
- Pattern to mirror for tests: `tests/unittests/test_stroke_raster.c` — ARGB32 image surface,
  `cairo_image_surface_get_data()` pixel readback, no GTK init.
- Use `dt_widget_ppd()`, `DT_PIXEL_APPLY_DPI`, `dt_widget_scroll_mask()` from
  `widgets/widget_settings.h`; `M_PI_F`, `CLAMPS` from `math/math.h`; `IS_NULL_PTR`.

**Contracts:**
- `typedef void (*dt_color_wheel_color_fn)(float hue_deg, float chroma_frac, float rgb_out[3], gpointer user_data);`
  — `hue_deg` in [0,360) clockwise from 12 o'clock, `chroma_frac` in [0,1], `rgb_out` display
  RGB in [0,1] (already clamped by the caller).
- `GtkWidget *dt_color_wheel_new(dt_color_wheel_color_fn fn, gpointer user_data);`
- `void dt_color_wheel_set_hue_chroma(DtColorWheel *w, float hue_deg, float chroma_frac);`
  — wraps hue, clamps chroma, redraws, **emits nothing** (D4).
- `float dt_color_wheel_get_hue(DtColorWheel *w); float dt_color_wheel_get_chroma(DtColorWheel *w);`
- `void dt_color_wheel_invalidate_background(DtColorWheel *w);` — drops the cached raster; the
  next draw calls the colour callback again.
- Signal `"value-changed"` (no args) emitted only from button-press/motion/release handlers.
- Scroll: the widget does NOT handle scroll events (no scroll mask, or handler returns FALSE)
  so the side panel keeps scrolling — mirrors `area_scroll_callback` in colorbalancergb.c.
  Keyboard: the draft's arrow/Home/Delete handling is dropped (see Out of scope).
- Pure functions (declared in the header, no GTK objects). All geometry is in LOGICAL units
  (the widget's allocation / event coordinates):
  `typedef struct { float cx, cy, r_disc, r_track_in, r_track_out; } dt_color_wheel_geometry_t;`
  `dt_color_wheel_geometry_t dt_color_wheel_geometry(int width, int height);`
  `float dt_color_wheel_point_to_hue(float dx, float dy);` (degrees CW from 12)
  `void dt_color_wheel_hue_to_point(float hue_deg, float radius, float cx, float cy, float *x, float *y);`
  `dt_color_wheel_drag_t dt_color_wheel_hit_test(const dt_color_wheel_geometry_t *g, float x, float y);`
  `cairo_surface_t *dt_color_wheel_raster_build(int width, int height, double ppd, const dt_color_wheel_geometry_t *g, dt_color_wheel_color_fn fn, gpointer user_data);`
  — `width`/`height`/`g` logical; the builder scales `g` by `ppd` internally, allocates
  `width*ppd x height*ppd` device pixels, and returns the surface with
  `cairo_surface_set_device_scale(s, ppd, ppd)` set (the stroke_raster HiDPI trap in CLAUDE.md).
  ARGB32 premultiplied, transparent outside disc and track.
  The builder samples `fn` on a polar grid of `DT_COLOR_WHEEL_LUT_HUES` (360) x
  `DT_COLOR_WHEEL_LUT_CHROMAS` (33) points ONCE per build and bilinearly interpolates per pixel;
  it never calls `fn` per pixel.
- Theme colours read: `@colorwheel_handle_rim`, `@colorwheel_handle_edge` (Phase 4 defines them).

**Out of scope:**
- No `dt_color_wheel_pair_new()`, no gradient slider, no `_draw_soft_shadow()`/halo gradient.
- No keyboard handling and no scroll handling (the draft's key/scroll handlers are deleted, not
  ported); no `gtk_widget_class_set_css_name` (nothing in `src/` uses node names).
- No profile, `pixel/` or `colorprofiles/` include anywhere in the widget.
- No consumer wiring (Phase 2). No CSS edits (Phase 4). No changes to other widgets, including
  `drawingarea.{c,h}`.

**Tests (write first, confirm red):**
- [x] `dt_color_wheel_point_to_hue` / `dt_color_wheel_hue_to_point` round-trip for 0, 90, 180,
  270, 359.5 deg; hue increases clockwise (12 -> 3 o'clock is 0 -> 90).
- [x] `dt_color_wheel_hit_test` classifies centre and mid-disc as CHROMA, a point between
  `r_track_in` and `r_track_out` as HUE, the gap and outside as NONE.
- [x] `dt_color_wheel_geometry` on a non-square allocation centres a square; a 0x0 or 1x1
  allocation returns finite, non-negative radii (no NaN, no divide by zero).
- [x] `dt_color_wheel_raster_build` with a callback that encodes hue and chroma into R and G:
  pixel at the track radius, angle 90 deg, has R matching hue 90; the disc centre pixel has
  G == 0 (chroma 0); the rim has G == 255; a gap pixel and a corner pixel have alpha 0;
  the callback is never called with `chroma_frac` outside [0,1] or hue outside [0,360).
- [x] Rebuilding with `ppd = 2` doubles the pixel dimensions, sets a device scale of 2 on the
  returned surface, and keeps the same colours at the scaled coordinates.
- [x] The callback is called at most `360 * 33` times per build regardless of width (a counting
  callback on a 140 px and a 400 px build).
- [x] Widget-level, skipped (return 77) when `gtk_init_check()` fails: constructing a wheel and
  calling `dt_color_wheel_set_hue_chroma()` emits zero `"value-changed"` signals, and the
  getters return the wrapped/clamped values (hue 370 -> 10, chroma 1.5 -> 1).

**Steps:**
1. Write the tests above; run them; confirm they FAIL (red).
2. Create `colorwheel.h` (guard, forward types, contracts above; no `#pragma once`).
3. ~~Port the draft into~~ Write `colorwheel.c` from the Contracts: derive from `DTGTK_TYPE_DRAWING_AREA`,
   ~~strip slider/halo/profile/key/scroll code, replace the hue offset with~~ implement the pure geometry
   functions, route colours through the polar-LUT-sampled callback, guard zero-size, set the
   `dt_colorwheel` style class, cache the raster keyed on (width, height, ppd) plus explicit
   invalidation.
4. Add to `ANSEL_WIDGETS_SOURCES`; add the test to `LIB_ANSEL_UNIT_TESTS` with
   `SKIP_RETURN_CODE 77` as `test_tagging_completion` is registered.
5. Run the tests; confirm they PASS (green). Run `python3 tools/pragma_once_to_guards.py --verify`.

**Acceptance criteria:**
- [x] `ninja -C build` builds `lib_ansel` and `ansel` with the widget linked in.
- [x] `tools/check_module_boundaries.sh` and `python3 tools/include_graph.py --summary`
  (still `cycles 0`) pass; `colorwheel.c` includes nothing from `pixel/`, `colorprofiles/`,
  `common/`, `gui/`.

### Phase 2: Tracer bullet — shadows wheel bound in colorbalancergb
**Risk:** flagged (!#2, !#3)
**Test-first:** required
**Goal:** One wheel under the "Shadows lift" section drives `shadows_H`/`shadows_C`, follows the
sliders and the colour picker, paints through the display profile, and commits exactly one
history item per interaction.
**Assumes:**
- Phase 1 contracts are in place unchanged.
- `Ych_to_XYZ()` yields XYZ adapted to D65 (as the slider-stop code at ~2050 assumes when it
  passes it to `dt_XYZ_to_Rec709_D65`).
**Files:**
- `src/pixel/colorequal_shared.h` / `.c` — add ~~one~~ two public function(s) wrapping the existing
  static display chain (see Contracts; second one added 2026-09-17, see Reconciliations).
- `src/iop/colorbalancergb.c` — include `widgets/colorwheel.h` and `pixel/colorequal_shared.h`;
  the zone descriptor (Contracts) with one instance for shadows; gui_data gains
  `GtkWidget *shadows_wheel` and `const dt_iop_order_iccprofile_info_t *wheel_profile`;
  hue-mapping pair; colour callback; value-changed handler; draw hook for profile changes;
  sync in `gui_update` and `gui_changed`; hoist the four soft-max chroma constants so
  `dt_bauhaus_slider_set_soft_range()` and the descriptors share them.
- `tests/unittests/test_colorrings_display.c` — new (no test covers `colorequal_shared` today).
- `tests/unittests/CMakeLists.txt` — register it.

**Reuse:**
- Extend `src/pixel/colorequal_shared.c` beside `dt_colorrings_profile_rgb_to_display_rgb()`
  (`:225-241`, already public, already runs the last two statics) — the new entry point adds
  only the XYZ_D65 -> D50 step in front. Do NOT re-implement XYZ->display in the IOP.
- Pattern to mirror for commits: `color_picker_apply()` in `colorbalancergb.c:1400-1454` —
  params written by hand FIRST, then `dt_gui_freeze_begin()`, the `dt_bauhaus_slider_set()`
  calls, `dt_gui_freeze_end()`, the module's `gui_changed(self, ..., NULL)`, one
  `dt_dev_add_history_item(self->dev, self, TRUE, TRUE)`.
- Pattern to mirror for the profile: `colorequal.c:1172-1174` and `:1191-1245` — fetch
  `dt_ioppr_get_pipe_output_profile_info(self->dev->preview_pipe)` guarded on `self->dev &&
  self->dev->preview_pipe` at DRAW time and compare against the cached pointer; NULL is legal
  and yields the sRGB fallback.
- ~~`gamut_check_Yrg()` (`colorspaces_inline_conversions.h:1202`) for D3's in-gamut rim.~~ (struck
  2026-09-17: it clips to the Yrg cone, not the display gamut — see Reconciliations.)
- `_compute_reference_saturation()` (`colorequal_shared.c`, file-static) — the binary-search shape
  the new rim-chroma function mirrors (added 2026-09-17).

**Contracts:**
- `void dt_colorrings_xyz_d65_to_display_rgb(const dt_aligned_pixel_t XYZ_D65, const dt_iop_order_iccprofile_info_t *display_profile, dt_aligned_pixel_t RGB);`
  — D65->D50 adaptation, then exactly the existing chain: profile matrix (or
  `dt_XYZ_to_linearRGB` when NULL), max-normalisation of channels above 1, TRC, clamp to
  [0,1]. Byte-identical to what `dt_colorrings_hsb_to_display_rgb()` runs after its HSB->XYZ
  step; refactor that function to call the new one.
- Zone descriptor in `colorbalancergb.c`, one static instance per zone, ~~passed as `user_data`
  to both the colour callback and the `"value-changed"` handler~~ (struck 2026-09-13, see
  Reconciliations: the handler's `user_data` is a per-instance `{ self, zone }` binding kept
  in gui_data; the colour callback takes `self`):
  `typedef struct { const char *name; float soft_max_C; size_t off_H, off_C; /* offsetof into params */ size_t off_slider_H, off_slider_C, off_wheel; /* offsetof into gui_data */ } dt_cbrgb_wheel_zone_t;`
  (offsets, not pointers, so the table is `static const` and instance-safe.)
- Hue mapping pair (D5): `#define WHEEL_HUE_ORIGIN 0.f`;
  `static float _wheel_to_param_hue(float theta)` and `static float _param_to_wheel_hue(float H)`
  as written in D5. The disc callback uses the first; the handler uses the first; the
  `gui_update`/`gui_changed` sync uses the second. No other conversion anywhere.
- Disc callback: ~~`Ych = { 0.75f, 0.2f * chroma_frac, DEG_TO_RAD(_wheel_to_param_hue(hue_deg)), 0 }`
  -> `gamut_check_Yrg(Ych)` -> `Ych_to_XYZ` -> `dt_colorrings_xyz_d65_to_display_rgb` (D3).~~
  (struck 2026-09-17, see Reconciliations) `Ych = { 0.75f, g->wheel_rim_chroma * chroma_frac,
  DEG_TO_RAD(_wheel_to_param_hue(hue_deg)), 0 }` -> `Ych_to_XYZ` -> `dt_colorrings_xyz_d65_to_display_rgb`,
  with `g->wheel_rim_chroma = dt_colorrings_ych_display_rim_chroma(0.75f, 0.2f, profile)` computed
  in `gui_init` and recomputed by the draw hook when the profile pointer changes.
  Inside the callback, read the profile with the guarded fetch above.
- Value handler, in this order: `if(dt_gui_widgets_suppressed()) return;` -> write
  `p->*_H = _wheel_to_param_hue(hue)` and ~~`p->*_C = chroma_frac * soft_max_C`~~
  (struck 2026-09-19, see Reconciliations: the rim reading must not overwrite a chroma
  already above the soft maximum) -> the picker
  reset that `dt_iop_gui_changed()` performs (`imageop_gui.c:2030`) -> `dt_gui_freeze_begin()`
  -> `dt_bauhaus_slider_set()` on the zone's H and C sliders -> `dt_gui_freeze_end()` -> the
  module's static `gui_changed(self, wheel, NULL)` (NEVER `dt_iop_gui_changed()`, which commits
  by itself) -> one `dt_dev_add_history_item(self->dev, self, TRUE, TRUE)`.
- Sync rule in `gui_changed(self, w, previous)`: when `w` is a zone's H or C slider, push that
  zone's wheel; when `w == NULL` push every zone's wheel. `gui_update` pushes every wheel.
  Pushing = `dt_color_wheel_set_hue_chroma(wheel, _param_to_wheel_hue(p->*_H), CLAMPS(p->*_C / soft_max_C, 0.f, 1.f))`.
- Profile hook: a `"draw"` handler connected on each wheel (user handlers run before the class
  handler for this signal) compares the guarded fetch against `g->wheel_profile`; on mismatch
  it stores the new pointer and calls `dt_color_wheel_invalidate_background()`; it returns
  FALSE. Pointer is compared, never dereferenced.

**Out of scope:**
- Only the shadows zone; the other three and any layout reshuffle belong to Phase 3.
- Do not touch the slider-stop colouring at ~2050 or make it colour-managed.
- No CSS (Phase 4). No changes to `colorequal.c`.

**Tests (write first, confirm red):**
- [x] `dt_colorrings_xyz_d65_to_display_rgb` with a NULL profile maps D65 white XYZ to
  (1,1,1) within 1e-3 and black to (0,0,0); an out-of-gamut input returns values in [0,1].
- [x] `dt_colorrings_hsb_to_display_rgb()` output is ~~bit-identical~~ within 1e-6 (2026-09-17, see
  Reconciliations) before and after the
  refactor for a fixed set of HSB inputs at NULL profile (pin the pre-refactor values in the
  test as expected constants).

**Steps:**
1. Write the tests above; run them; confirm they FAIL (red).
2. Add the public XYZ entry point in `colorequal_shared.{h,c}`; make the HSB function call it.
3. Run the tests; confirm they PASS (green).
4. In `colorbalancergb.c`: hoist soft-max constants; add the zone descriptor type and the
   shadows instance, the hue mapping pair, the callback, the value handler and the draw hook
   exactly as the Contracts order them; create the wheel after the "Shadows lift" section label
   and before its sliders; add the sync rule to `gui_changed` and `gui_update`.
5. `ninja -C build`, open an image, verify the manual criteria below.

**Acceptance criteria:**
- [ ] Manual: red sits at 12 o'clock and yellow, green, cyan, blue, magenta follow clockwise —
  the same order as the "Hue" slider's stop ramp reads left to right. Use the shadows hue colour
  picker on a clearly red patch: the puck lands on the red region and the "Hue" slider reads
  near 0; drag the puck to the blue region: the slider's stop colour under its handle is blue.
- [ ] Manual: one drag of the puck produces exactly one new history item (history panel) and
  the "Hue"/"Chroma" sliders show the NEW values (proves the params write landed); moving the
  "Hue" or "Chroma" slider moves the puck without adding a second item.
- [ ] Manual: Ctrl+Z / Ctrl+Y and applying a preset move the puck and add no history item; an
  armed colour picker is disarmed by a puck drag.
- [ ] Manual: the disc shows no flat or clipped arc at the rim in the blue and green sectors
  (~~D3's gamut check~~ D3 as reconciled 2026-09-17: the searched rim chroma).
- [ ] Manual: switching the display profile in the darkroom colour-management popover repaints
  the disc within one frame; compare the rim pixel at 6 o'clock before and after.

### Phase 3: All four zones and page layout
**Risk:** flagged (!#3)
**Test-first:** N/A — pure GUI wiring of code Phase 2 already exercised; nothing new is
unit-testable without a live GTK display.
**Goal:** Every zone on the "4 ways" page reads: section label, wheel, then Luminance, Hue,
Chroma sliders, all four wheels bound and kept in sync.
**Assumes:**
- Phase 2 landed the `dt_cbrgb_wheel_zone_t` descriptor and its shadows instance unchanged, so
  a zone is one more `static const` table row plus one gui_data widget pointer.
**Files:**
- `src/iop/colorbalancergb.c` — gui_data gains `global_wheel`, `highlights_wheel`,
  `midtones_wheel`; three more descriptor rows; page order per Goal; ~~`color_picker_apply`
  pushes the picked zone's wheel through the Phase 2 sync helper.~~ (struck 2026-09-19, see
  Reconciliations: `gui_changed`'s per-zone dispatch already reaches every wheel from there.)

**Reuse:**
- The Phase 2 descriptor, callback, handler, draw hook and sync helper — do NOT duplicate any
  of them per zone; a fourth copy of the handler is Drift.
- Pattern to mirror: the existing per-zone slider creation order at `gui_init` ~1896-1970.

**Out of scope:**
- No rename of section labels or slider labels; no removal of any slider; no change to the
  "master" or "masks" pages; no tooltip rewrites beyond the one D3 sentence on the Chroma
  slider tooltip.
- No widget API changes: if Phase 3 needs one, run Drift Reconciliation.

**Manual verification:**
- [ ] Each of the four wheels moves only its own zone's Hue/Chroma sliders; each slider pair
  moves only its own wheel.
- [ ] The global wheel's puck reaches the rim at `global_C = 0.0075` (soft max), and a
  preset with `global_C` above that draws the puck clamped at the rim without error.
- [ ] Applying a built-in colorbalancergb preset updates all four wheels; Ctrl+Z / Ctrl+Y
  redraw them; module reset (which goes through params + `gui_update`, since this module's
  `gui_reset` only resets the picker, `colorbalancergb.c:1783-1787`) returns all pucks to
  centre.
- [ ] Each zone's colour picker moves its own wheel and disarms on a puck drag.
- [ ] The "4 ways" page still scrolls with the mouse wheel when the pointer is over a disc.

**Steps:**
1. Add the three descriptor rows and gui_data pointers.
2. Create the three remaining wheels in the order Goal states; ~~route `color_picker_apply`
   through the sync helper~~ (struck 2026-09-19, see Reconciliations).
3. `ninja -C build`; run the manual verification.

**Acceptance criteria:**
- [ ] `ninja -C build` clean; the manual verification list above is checked in full.

### Phase 4: Theme and docs
**Risk:** none
**Test-first:** N/A — CSS and documentation; the observable is visual.
**Goal:** The wheel follows the theme, and the rules that are not obvious from the code are
recorded in CLAUDE.md.
**Files:**
- `data/themes/ansel.css` — add `@define-color colorwheel_handle_rim @grey_100;` and
  `@define-color colorwheel_handle_edge alpha(@grey_00, 0.5);` next to the `@bauhaus_*` block
  (lines 148-163); add `.dt_colorwheel { background-color: transparent; border: none;
  margin: 0.25em 0 0.125em 0; }` and `.dt_colorwheel:focus { outline: none; }` near the
  `.dt_bauhaus` rules (~970). Do NOT add `colorwheel` node selectors or `#colorwheel-*` ids:
  nothing in the tree sets CSS node names, and the draft's node rules would match nothing.
- `CLAUDE.md` — one entry under "IOP modules" with three rules, 1-3 sentences each: the D5 hue
  seam (the wheel is CW-from-12 and convention-free; the IOP owns the additive mapping pair;
  colour under the puck equals the applied hue by construction); the D2 rule (widgets take
  colours through a callback, never a profile, and the CI gates being green is not permission
  to include `pixel/`/`colorprofiles/` there); the commit rule (a handler that sets sliders
  under freeze must write params by hand first, and must call the module's `gui_changed`, never
  `dt_iop_gui_changed`, before its own single history commit).

**Reuse:**
- Pattern to mirror: `bauhaus.c:1143-1179` theme colour lookup already used by Phase 1; the
  CSS block shape of `.dt_bauhaus` rules.

**Out of scope:**
- No other theme changes, no new theme file, no touching `box-shadow` resets.
- No CLAUDE.md prose beyond the three rules, no code in them.
- No `src/widgets/README.md` rewrite to reconcile it with `include_graph.py`'s layer table
  (mention the discrepancy in Discoveries; it is a separate decision).

**Manual verification:**
- [ ] Editing `@colorwheel_handle_rim` to a saturated colour in `ansel.css` and restarting
  changes the handle rim on all four wheels; deleting the define falls back to the hardcoded
  colour without a crash or a GTK warning on stderr.
- [x] `grep -n "colorwheel" data/themes/ansel.css` shows only `@define-color colorwheel_*`
  lines and `.dt_colorwheel` selectors — no bare `colorwheel` node selector, no `#colorwheel-*`.
  (Verified autonomously: exactly 4 matches, the two defines and the two selectors. The added
  CSS comments spell it "colour wheel" so they stay out of this grep.)

**Steps:**
1. Add the CSS defines and rules.
2. Write the CLAUDE.md entry.
3. `ninja -C build`; run the manual verification.

**Acceptance criteria:**
- [x] `tools/check_unused_includes.sh` clean on the diff; `tools/check_layering.sh` reports no
  increase against `tools/include_baseline.txt`. (No source file changed at all this phase, so
  the include check is trivially clean; clang-tidy is absent on this machine, so the standing
  substitute — zero added `#include` lines in the diff — was used. Layering 183 = baseline.)

## Verification
- [x] `ninja -C build` (full, not `ninja ansel`) with zero new warnings.
- [x] `ctest --test-dir build -R "test_colorwheel|test_colorrings_display"` green. (Run
  out-of-tree with plain gcc on this machine — cmocka cannot link in-tree on Windows, see
  Discoveries 2026-09-13. 6 pure + 1 widget, and 3/3.)
- [x] `python3 tools/pragma_once_to_guards.py --verify`; `python3 tools/include_graph.py
  --summary` still `cycles 0`; `tools/check_module_boundaries.sh`; `tools/check_layering.sh`;
  `tools/check_unused_includes.sh` — all pass. (The last one by its documented substitute.)
- [ ] Sanity only, not a gate that can fail from this work (nothing here is reachable from
  `commit_params`/`process`/export): `tools/check_export_pixels.sh <before-ref> <after-ref>`
  identical for an image whose XMP carries a non-default colorbalancergb 4-ways edit.
- [ ] `tools/check_it_runs.sh` (the binary starts and exits cleanly).
- [ ] Manual verification lists of Phases 2-4 all checked.

## Notes
- The draft's brightness gradient slider is dropped because `_Y` already has a "Luminance"
  slider under each zone; if a dedicated dark-to-light track is wanted later it is a Phase 3
  addition (`dtgtk_gradient_slider_new_with_color_and_name()`, `gradientslider.h:151`), not a
  widget change.
- `WHEEL_HUE_ORIGIN` decides only which hue sits at 12 o'clock; 0 puts red there and the
  additive map keeps the slider's R, Y, G, C, B, M order clockwise. If the developer prefers
  red elsewhere, change the define only; never the direction.
- Rebuild cost: with the 360 x 33 polar LUT a build costs ~12k callback calls per wheel, so a
  panel-width drag (one allocation per motion event, four wheels) stays in the low
  milliseconds. If profiling says otherwise, debounce the rebuild in the widget; do not shrink
  the LUT below 360 hues.
- The existing slider stops are Rec709 gamma 2.2 and the disc is profile-managed, so on a
  non-sRGB display the two may disagree slightly on the same hue. Known, out of scope.
- `dt_dev_add_history_item()` coalesces consecutive edits of the same module into the top
  item; a drag therefore should not flood history. If it does, that is a pre-existing bauhaus
  behaviour too — report, do not throttle in the wheel.

## Risks
#1. **A widget test that touches a live GtkWidget runs only where a display exists** — cmocka
    tests link `lib_ansel`, and the tree's precedent is `gtk_init_check()` with exit code 77
    registered as `SKIP_RETURN_CODE`. So Phase 1's geometry and raster tests are plain C
    functions (always run), and the one GObject-level test (D4: setters emit nothing) skips on
    headless CI and runs on a developer machine. Do not let the skip become the only run: check
    it locally once with a display before declaring Phase 1 green.
#2. **Both hue conversions must go through the one static pair, and the handler must write
    params before it sets sliders** — the disc colour, the drag-to-param path and the two syncs
    each convert between wheel and param degrees. If any of them inlines its own formula the
    puck sits over a colour that is not the hue it applies, and nothing fails loudly. Separately,
    a `dt_bauhaus_slider_set()` under freeze does NOT write params, so a handler that forgets
    the `p->*_H = ...; p->*_C = ...;` assignments commits the old values while the sliders show
    the new ones. Phase 2's first two manual criteria are the detectors for both.
#3. **One keystroke separates one commit from two: `gui_changed` vs `dt_iop_gui_changed`** —
    the framework wrapper commits history itself; the module's static one does not. A handler
    that calls the wrapper and then `dt_dev_add_history_item()` double-commits every drag; one
    that forgets `dt_gui_widgets_suppressed()` at the top re-enters from the one unfrozen
    framework path. Neither fails loudly; Phase 2/3 manual checks count history items explicitly.

## Reconciliations
<!-- Drift amendments written by /implement during execution. Append-only. Outdated phase
text above is struck through (~~...~~) but preserved; entries here are the authoritative
correction. Empty at plan creation. -->
- 2026-09-13 — Phase 1: the handoff draft `ansel-halo-wheel/colorwheel.{c,h}` named in
  **Assumes** does not exist anywhere on the build machine (searched the user profile) → the
  widget is written from Phase 1's Contracts directly; every property the plan credits to
  the draft (ARGB32 packing, GObject lifecycle, `destroy` override freeing the cached
  surface) is specified in the Contracts and the 3C plan, so no behaviour is lost. Approved
  autonomously under the developer's explicit "go with your recommended option" instruction.
- 2026-09-13 — Phase 2: the Contracts hand the `static const` zone descriptor to the
  `"value-changed"` handler as `user_data`, but the handler needs `self` (params, `dev`,
  gui_data) and a `static const` table cannot carry a per-instance pointer → the handler's
  `user_data` is `dt_cbrgb_wheel_binding_t { dt_iop_module_t *self; const dt_cbrgb_wheel_zone_t *zone; }`,
  one per zone in gui_data (`shadows_binding` in Phase 2); the descriptor stays `static const`
  and offset-based exactly as specified. The colour callback is zone-independent (the disc
  ramp is the same for every zone) and takes `self` as `user_data`. Approved autonomously.
- 2026-09-17 — Phase 2: D3 assumed `gamut_check_Yrg()` keeps the C = 0.2 rim inside the display
  gamut; it only clips to the Yrg/LMS cone (its own comment says so), and measured at Y = 0.75
  with a NULL profile three of six rim hues still clip a channel to 0 (wheel 60: B, 180: R,
  240: R) while wheel 0 exceeds 1 and is max-normalised — the plan's own test 3 caught it →
  the rim chroma is ONE constant, the largest chroma <= 0.2 for which every whole-degree hue
  at Y = 0.75 lands inside the display profile's linear [0,1] cube, found by an 18-step binary
  search (the construction `_compute_reference_saturation()` already uses for the colorequal
  rings: uniform rim, house precedent), exposed from `colorequal_shared` as
  `float dt_colorrings_ych_display_rim_chroma(float Y, float max_chroma, const dt_iop_order_iccprofile_info_t *display_profile)`
  (NULL = sRGB; runs the same D65->D50->profile linear step as the display chain, unclamped).
  gui_data caches it in `float wheel_rim_chroma`, set in `gui_init` and recomputed in the draw
  hook whenever the profile pointer changes (where the background is already invalidated). The
  disc callback drops `gamut_check_Yrg()` — a chroma inside the display cube is inside the cone.
  Test 3 pins it: the search bit (C < 0.2 and at least one of six hues clips at 0.2), and at C no
  channel is 0, the max channel is >= 0.5 and R/G/B dominate at wheel 0/120/240. Approved
  autonomously under the developer's standing instruction.
- 2026-09-17 — Phase 2 (follow-up): at D3's `Y = 0.75` the searched sRGB rim chroma is only
  0.0414 (measured; channel spread 0.13-0.25 — hue readable but washed out), below the 0.05
  legibility floor test 3 asserts. Y is the lever (measured: Y=0.5 → 0.088, 0.6 → 0.072,
  0.9 → 0.017) → `WHEEL_DISC_Y` is 0.6f, `WHEEL_DISC_MAX_C` stays 0.2f, the test floor stays
  0.05 as a real guard. Approved autonomously (worker's recommendation).
- 2026-09-17 — Phase 2 (3F Major): the refactor-identity test asserted bit-equality against
  constants measured under MSYS2, but the NULL-profile chain ends in `powf()` whose last bit
  differs between mingw's libm and glibc, and Linux CI runs the file → the assertion is
  `fabsf(diff) > 1e-6f`; same property (the refactor moved code, not arithmetic), no cross-libm
  red. Bit-identity still held on this machine. Approved autonomously.

- 2026-09-19 — Phase 3: **Files**/**Steps** call for `color_picker_apply` to push the picked
  zone's wheel through the sync helper, on the assumption that it needs its own push. It does
  not: the function ends with `gui_changed(self, picker, NULL)` (`colorbalancergb.c:1620`,
  after `dt_gui_freeze_end()`), and `picker` IS the zone's `_H` slider widget, so once
  `gui_changed` carries a dispatch arm per zone — which Phase 3 adds anyway for the sliders —
  every picker already re-syncs its own wheel through exactly the sync helper the plan names.
  An explicit push in `color_picker_apply` would be a second, redundant `set_hue_chroma` on the
  same widget in the same call. → `color_picker_apply` is left untouched; the zone dispatch in
  `gui_changed` is the single path, which is also what keeps the "do NOT duplicate the sync per
  zone" rule in **Reuse** honest. `_wheel_sync_from_params()` emits nothing (D4), so running it
  outside the freeze pair is safe. Phase 3's manual criterion "each zone's colour picker moves
  its own wheel and disarms on a puck drag" is unchanged and is the detector. Approved
  autonomously under the developer's standing instruction.

- 2026-09-19 — Phase 3 (3F Major, fixed in place): the Phase 2 **Contracts** have the value
  handler write `p->*_C = chroma_frac * soft_max_C` unconditionally. The puck pins at the rim
  for every chroma at or above the zone's soft maximum (`_wheel_sync_from_params` clamps
  `C / soft_max_C` to 1), so a rim reading cannot distinguish "the user asked for the maximum"
  from "the value was already past it" — and the params range to 1.0 while the soft maxima stop
  at 0.0075 for global, with `color_picker_apply` writing `Ych[1] * Ych[0]` unclamped. The
  widget emits `"value-changed"` on ANY left press (`colorwheel.c` `_button_press` →
  `_set_from_point`, which leaves chroma untouched for a hue-track press), so a click that moved
  nothing, or a press on the outer ring, collapsed a picked `global_C` of e.g. 0.3 to 0.0075 —
  silent data loss, and it falsified the wheel tooltip's "the outer ring changes the hue alone".
  Phase 1's Discoveries note accepted the emit-on-click as harmless; that reasoning holds only
  while the round trip is lossless, which it is not above the soft max. Phase 2 shipped the
  mechanism (shadows, 0.375 vs 1.0) but Phase 3's 50:1 global zone is what makes it the routine
  outcome. → the handler computes
  `pinned_at_rim = (chroma_frac >= 1.f) && (*param_C > zone->soft_max_C)` and keeps the existing
  param chroma in that case, taking `chroma_frac * soft_max_C` otherwise; dragging inward reports
  < 1 and is unaffected, so the puck stays fully able to REDUCE an out-of-range chroma. Hue is
  written unconditionally as before. No widget change (Phase 3 forbids one) and no change to the
  commit sequence that Risk #3 guards. Detector: pick a saturated patch with the global hue
  picker, note `global_C`, click the puck without moving it, re-read the slider — it must be
  unchanged. Approved autonomously under the developer's standing instruction.

- 2026-09-19 — Phase 4: D4's **Consequences** states the relation between the suppression
  guard and the one unfrozen framework path exactly backwards, and Phase 4's deliverable is
  CLAUDE.md prose derived from it. Measured: `_iop_reset_label_reset`
  (`src/develop/imageop_gui.c:2213-2221`) calls `d->module->gui_update(d->module)` with no
  `dt_gui_freeze_begin()` anywhere on the path, so `_widget_suppress_depth` is 0 and
  `dt_gui_widgets_suppressed()` returns FALSE there — a handler's opening
  `if(dt_gui_widgets_suppressed()) return;` is therefore the one check that CANNOT fire on the
  one path the plan credits it with covering. What actually covers the wheel is D4 itself:
  `dt_color_wheel_set_hue_chroma()` emits nothing, so `_wheel_value_changed` is never reached
  from a reset; `colorbalancergb`'s `gui_changed()` additionally opens its own freeze
  (`:1947`) before the per-zone sync, so the guard would hold even if the setter did emit. →
  the CLAUDE.md entry states it the right way round (guard for the frozen re-entries; D4 for
  the unfrozen reset path) and names the consequence a future reader needs: a widget whose
  programmatic setter DID emit would commit a reset as a user edit. No code change — both
  mechanisms are already present and correct in the shipped code; only the plan's explanation
  of which one covers which path was wrong. Found by the Phase 4 3F quality scan and confirmed
  against the source before acting. Approved autonomously under the developer's standing
  instruction.

- 2026-09-19 — Post-Phase-4, developer-reported: the discs read as washed out. Measured, not
  argued: `dt_colorrings_ych_display_rim_chroma()` searched ONE chroma that all 360 hues must
  satisfy, so every hue was painted at the most constrained hue's ceiling. In sRGB at the then
  `WHEEL_DISC_Y` of 0.6 that ceiling was 0.072, while the per-hue limits ran to 0.205 at
  yellow-green — the mean hue was at 1/1.5 of its own gamut, and yellow-green at under a third.
  The developer chose per-hue at `Y = 0.5` from four rendered candidates. → the function now
  FILLS a `rim_out[hues]` table, one bisection per hue (same signature name, new shape — the
  Phase 2 Contract's spelling changes); `WHEEL_DISC_Y` is 0.5; gui_data carries
  `wheel_rim_chroma[WHEEL_RIM_HUES]` (360, matching the widget's own polar LUT) instead of one
  float; `_wheel_rim_at()` interpolates between the two nearest entries so the disc has no step
  where the table samples. Measured after: the table spans 0.0879 at 163 deg to 0.2 at 89 deg,
  2.28x, and the max entry is the `WHEEL_DISC_MAX_C` cap rather than the display — the
  yellow-green arc is now bounded by the reference ramp, not by gamut. Cost is unchanged: the
  old search tested all 360 hues per iteration, so the table is the same order of work, and both
  are paid once per display-profile change. NOTE the arc here — D3's ORIGINAL instinct was
  per-hue rim and was right; what was wrong was its mechanism, `gamut_check_Yrg()`, which clips
  to the Yrg cone rather than to the display, and the 2026-09-17 entry over-corrected from that
  failure to a single constant. Radius is no longer iso-chroma across hues; it never carried
  that meaning, since the puck's radius is read against the zone's soft maximum (0.0075 at
  global) and the disc paints 10x-27x that. `test_colorrings_display` now guards the spread
  (>= 2.0x) so a return to a uniform rim fails loudly; its old "no channel clips at the rim"
  assertion is deliberately gone, because sitting ON the gamut boundary is the point.

## Discoveries
<!-- Non-contradictory findings logged by /implement during execution (act / defer / drop).
Append-only, empty at plan creation. -->
- 2026-09-13 — Phase 1 (deferred): `dt_color_wheel_raster_build()` hand-rolls the device-scaled
  surface (create at `w*ppd x h*ppd`, `cairo_surface_set_device_scale`); `system/surface_scaling.h:36`
  `dt_cairo_surface_create_at_scale()` does the same and `bauhaus.c:2478` already uses it. Swap is
  not bit-identical at fractional ppd (helper truncates, wheel rounds) — a judgment swap for a
  later cleanup, not Phase 2 work. (3F Minor.)
- 2026-09-13 — Environment: cmocka tests cannot link in-tree on Windows (`-municode`/`wWinMain`,
  root `CMakeLists.txt` ~:750, pre-existing); Phase 1 and 2 tests are run out-of-tree with plain
  gcc against `build/src/libansel.dll.a` (command in `docs/plans/.impl/*phase-2.md`). Linux CI runs
  them in-tree. `tools/check_unused_includes.sh` needs clang-tidy, absent in this MSYS2 — replaced by
  a manual symbol-per-include check. Both deferred as environment work.
- 2026-09-13 — Phase 1: `gui_changed()` in `colorbalancergb.c` is NOT static (plan text says
  "static"); it is still the module's own non-committing handler, so the Contracts hold unchanged.
- 2026-09-13 — Phase 1 note for Phase 2: a press landing on the puck's current position still emits
  `"value-changed"`, so the handler commits on a click that moved nothing; history coalescing makes
  it harmless (plan Notes already accept this).
- 2026-09-17 — Phase 2 (3F Major, ACT IN PHASE 3): `g->wheel_profile` / `g->wheel_rim_chroma` are one
  shared pair in gui_data while the draw hook invalidates only the wheel being drawn; with four wheels
  the first to draw after a profile change stores the new pointer and the other three keep the old
  raster until a resize. Harmless at one wheel; Phase 3's profile hook must invalidate every zone's
  wheel in the mismatch branch (it has the zone table to walk) — do not reuse the Phase 2 hook unchanged.
- 2026-09-17 — Phase 2 (3F Minor, deferred): `test_colorrings_display.c` re-declares
  `TEST_WHEEL_DISC_Y 0.6f` instead of reading the module's `WHEEL_DISC_Y` (the IOP is a plugin, the
  define is in its `.c`), so the 0.05 legibility floor guards the test's own constant. Hoisting the two
  defines to a shared header is the fix if it ever matters.

- 2026-09-19 — Phase 3 (3F simplification, deferred): `dt_cbrgb_wheel_zone_t.name` is read by no
  code path, so the table's four `N_()` markers export "global"/"shadows"/"highlights"/"mid-tones"
  to the `.po` catalogue with no consumer. Dropping the field (or just the `N_()`) changes a
  Phase 2 Contract, so it is a judgment call rather than a mechanical tidy — left for Phase 4 or
  a later cleanup.

- 2026-09-19 — Phase 4 (required by the phase's **Out of scope**): `src/widgets/README.md:6`
  says the directory is "Layer **4**, alongside `gui/`" while `tools/include_graph.py:51` has
  `('widgets', 2.5)` — above `pixel/` (2) and `colorprofiles/` (1), which is exactly why the
  gates stay green on a widget that includes either. The two documents contradict each other
  and the CLAUDE.md entry written this phase asserts the 2.5 figure. Reconciling them is a
  separate decision (either the README's layer number is wrong, or the layer table is too
  permissive for this directory); not touched here.

- 2026-09-19 — Phase 4 (3F simplification, kept): `.dt_colorwheel:focus { outline: none; }` is
  inert today — nothing sets `can-focus` on `DtColorWheel` or on its `DTGTK_TYPE_DRAWING_AREA`
  base, so the widget never takes focus. Kept because the phase's **Files** list pins the rule
  and it costs nothing as defence against a later focusable variant; dropping it is a judgment
  call, not a mechanical tidy.

- 2026-09-19 — Phase 4 (deferred, deliberately out of scope): the CLAUDE.md entry does NOT
  record Phase 3's hardest-won fact — the wheel emits `"value-changed"` on ANY left press, so a
  rim chroma reading is ambiguous whenever the param exceeds the zone's soft maximum, which
  `color_picker_apply`'s unclamped write makes routine at global's 0.0075. The phase's
  **Out of scope** allows no CLAUDE.md prose beyond the three named rules, so it stays in the
  2026-09-19 Reconciliations entry only. Worth promoting to CLAUDE.md in a later pass: it is
  the kind of silent-data-loss mechanism that file exists to carry.

## Phase Handoff Log
<!-- Written by /implement at each 3G phase gate (Done / Learned / Drift / Watch-next per
phase). Append-only, empty at plan creation. MUST remain the LAST section of this file:
/implement's Step 2 reads the plan up to this heading plus only the log's final entry, so
never add a section below it. -->

### 2026-09-13 — Phase 1: Widget core with pure, testable geometry and raster
- Done: `src/widgets/colorwheel.{c,h}` + `tests/unittests/test_colorwheel.c` committed as
  `9944b6319a` on branch `claude/color-balancer-rgb-wheel-ebe2a5` (worktree
  `.claude/worktrees/color-balancer-rgb-wheel-ebe2a5`, build dir `build/` configured there via
  `./build.sh --build-dir build --prefix C:/Users/sting/ansel-dev --skip-lensfun` under MSYS2 mingw64,
  submodules initialised). 7/7 tests green out-of-tree with a display (widget group ran, not skipped);
  full `ninja -C build` clean; pragma/include-graph (cycles 0)/module-boundaries/layering (183=baseline) pass.
- Learned: the handoff draft never existed (Reconciliations #1); the plan's line numbers for
  `colorbalancergb.c` are shifted (current: `color_picker_apply` :1401-1476, `gui_changed` :1699-1723,
  4-ways page :1894-1971, soft ranges :1910/:1929/:1948/:1967, stop colouring :1484-1508 and :2054-2068);
  `XYZ_D65_to_D50` lives in `pixel/chromatic_adaptation.h:285`, not `colorspaces_inline_conversions.h`.
- Drift: Reconciliations #1 (missing draft) and #2 (Phase 2 handler `user_data` is a `{self, zone}`
  binding, recorded ahead of Phase 2).
- Watch-next: Phase 2's 3C plan is already written at `docs/plans/.impl/PLAN-colorbalancergb-color-wheel-phase-2.md`
  (kept, gitignored) — review it and spawn the worker from it; 3A for Phase 2 is done (facts folded into
  that file). The GUI manual criteria of Phases 2-4 cannot be verified autonomously; the developer must
  run them.

### 2026-09-17 — Phase 2: Tracer bullet — shadows wheel bound in colorbalancergb
- Done: `dt_colorrings_xyz_d65_to_display_rgb()` + `dt_colorrings_ych_display_rim_chroma()` in
  `pixel/colorequal_shared.{h,c}` (HSB function refactored onto the former, its static helper deleted);
  `iop/colorbalancergb.c` gains the zone descriptor/binding, hue pair, disc callback, value handler,
  profile draw hook, `gui_changed` sync and the shadows wheel after the "shadows lift" label;
  `tests/unittests/test_colorrings_display.c` (3 tests, registered). Build 0 warnings; 3/3 + Phase 1's
  7/7 green out-of-tree; pragma/include-graph (cycles 0)/module-boundaries/layering (183) pass. Manual
  GUI criteria NOT run (autonomous session) — developer must run them.
- Learned: `gamut_check_Yrg()` clips to the Yrg cone, not the display gamut (3 of 6 rim hues clipped
  at C=0.2) → uniform binary-searched rim chroma per display profile (Reconciliations, 2026-09-17);
  at Y=0.75 the sRGB rim is 0.041 (too pale) so `WHEEL_DISC_Y` is 0.6 (rim 0.072). `dt_iop_module_t`
  has no `gui_data` member — use `dt_iop_gui_data(self)`. Bit-identity across libm is unsafe (powf).
- Drift: Reconciliations entries 3, 4, 5 (dated 2026-09-17: rim search; Y=0.6; 1e-6 identity test).
- Watch-next: Phase 3 MUST change the profile draw hook to invalidate ALL four wheels (Discoveries
  2026-09-17, 3F Major, act-in-Phase-3) — the shared `wheel_profile`/`wheel_rim_chroma` pair plus
  per-widget invalidation leaves three stale discs after a display-profile change. Reuse the zone
  descriptor table for the walk. Then run Phase 2's five manual criteria before Phase 3's own.

### 2026-09-19 — Phase 3: All four zones and page layout
- Done: `iop/colorbalancergb.c` only (+118/-36). `_wheel_zone_shadows` becomes a
  `dt_cbrgb_wheel_zone_id_t` enum plus a 4-row `_wheel_zones[]` table in page order; gui_data gains
  `global/highlights/midtones_wheel` and `wheel_bindings[CBRGB_WHEEL_ZONES]` (replacing
  `shadows_binding`); new `_zone_widget_slot()` writable accessor and `_wheel_add()` creation helper
  called once per zone; `gui_changed`'s single shadows block becomes a table walk; the "4 ways" page
  reads label -> wheel -> Y/H/C for all four zones and seeds the shared profile/rim pair once.
  `_wheel_draw_profile_hook` now takes `self` and invalidates ALL four wheels (Phase 2's deferred 3F
  Major, closed). Build 0 warnings after a forced recompile; 3/3 + 6+1/7 tests green out-of-tree;
  pragma / include-graph (cycles 0) / module-boundaries / layering (183) pass. No new `#include`
  (substitutes for `check_unused_includes.sh`, clang-tidy absent). Manual GUI criteria NOT run.
- Learned: the wheel emits `"value-changed"` on ANY left press (`colorwheel.c` `_button_press`), and
  a hue-track press leaves chroma untouched -- so the puck's rim reading is ambiguous whenever the
  param exceeds the zone's soft max, which `color_picker_apply`'s unclamped write makes routine at
  global's 0.0075. Phase 1's "emit on a click that moved nothing is harmless" note only holds while
  the round trip is lossless. `colorbalancergb.c` is compiled via `#include` inside the generated
  `introspection_colorbalancergb.c`, so a warning check needs a forced recompile, not a no-op build.
- Drift: Reconciliations 2026-09-19 x2 -- `color_picker_apply` left untouched (`gui_changed`'s new
  per-zone dispatch already reaches every wheel, so an explicit push would be a redundant second
  sync); and the 3F Major, the value handler no longer overwrites a chroma already above the soft
  maximum when the puck reads the rim. Discoveries 2026-09-19: the zone table's `name` field has no
  consumer (deferred).
- Watch-next: run Phase 2's five manual criteria AND Phase 3's five before Phase 4 -- Phase 3's
  single acceptance criterion couples `ninja -C build` to that manual list, so it stays unticked
  until the developer runs it. The new rim-preservation branch has no automated cover (this phase is
  `Test-first: N/A`); its detector is the picker round trip written into the Reconciliations entry.
  Phase 4 is CSS (`data/themes/ansel.css`) plus three CLAUDE.md rules -- no code.

### 2026-09-19 — Phase 4: Theme and docs
- Done: `data/themes/ansel.css` gains `@define-color colorwheel_handle_rim @grey_100;` /
  `colorwheel_handle_edge alpha(@grey_00, 0.5);` after the `@bauhaus_*` block, and
  `.dt_colorwheel` + `.dt_colorwheel:focus` at the END of section 5 "Panels, Modules &
  Toolboxes" (NOT the plan's "~970" anchor — that line has drifted into section 7
  "Dialogs, Popovers & Context Menus", whose only `.dt_bauhaus` hits are a commented-out debug
  block and popup rules; the new rules sit beside `.dt_section_label`/`.dt_section_expander`,
  which is where a module-panel control belongs). `CLAUDE.md` gains one `###` entry at the end
  of "IOP modules" with the three rules. No code. Build 0 warnings; 6+1 and 3/3 tests green
  out-of-tree; pragma / include-graph (cycles 0) / module-boundaries / layering (183) pass.
- Learned: measured through GTK rather than reasoned — the theme parses with 0 errors before
  and after, both new colours resolve (white, 50% black, i.e. exactly the widget's hardcoded
  fallbacks at `colorwheel.c:265-268`, so deleting a define is a visual no-op), and
  `.dt_colorwheel` really does apply to a `GtkDrawingArea` offscreen (transparent background,
  0 border, 3px/2px margin). `tools/check_it_runs.sh` cannot see this phase: it exercises
  `ansel-cli`, which never loads the GTK theme, and there is no staged build here anyway.
- Drift: Reconciliations 2026-09-19 (Phase 4) — D4's **Consequences** had the suppression guard
  and the unfrozen `_iop_reset_label_reset` path backwards; the guard cannot fire when nothing
  froze. Struck; CLAUDE.md states it the right way round. Found by the 3F scan, confirmed
  against `imageop_gui.c:2213-2221` before acting. No code change. Discoveries 2026-09-19 x3:
  the `README.md` "Layer 4" vs `include_graph.py` 2.5 contradiction (required by this phase's
  Out of scope); `.dt_colorwheel:focus` is inert but kept; and the emit-on-any-press fact is
  deliberately NOT in CLAUDE.md (out of scope) though it deserves to be.
- Watch-next: ALL FOUR phases are committed, but "Final verification" is still open and TEN
  manual GUI criteria remain the developer's — Phase 2's five, Phase 3's five, and Phase 4's
  first one (edit `@colorwheel_handle_rim` to a saturated colour, restart, confirm all four
  handle rims change; then delete the define and confirm the fallback with no GTK warning on
  stderr). Two Verification items also remain: `tools/check_it_runs.sh` needs a staged build
  (`cd build && DESTDIR=$PWD/stage cmake -P cmake_install.cmake`), and
  `tools/check_export_pixels.sh` needs two refs plus an image whose XMP carries a non-default
  4-ways edit. The highest-value manual check is still Phase 3's rim-preservation detector:
  pick a saturated patch with the global hue picker, note `global_C`, click the puck without
  moving it, re-read the slider — it must be unchanged.
