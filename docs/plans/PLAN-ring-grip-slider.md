# PLAN: Ring-grip slider restyle + inline value editing

**Status:** Complete
**Created:** 2026-09-20
**Type:** Single plan

## Context

The bauhaus slider looks like a thin 5 px bar with a filled circular knob, so the colour
ramp underneath the knob is hidden exactly where the user is looking, and a signed slider
gives no visual cue where its origin sits. Separately, some sliders paint a ramp that
misrepresents the parameter: the Color primaries **brightness** sliders are painted in the
node's saturated hue (red/yellow) even though brightness is a neutral luminance control.

This plan replaces the slider appearance with the "ring grip" design (recessed 8 px rounded
rail, hollow ring indicator that lets the ramp read through it, origin notch on signed
sliders) and adds click-to-type value entry so a value can be entered instead of dragged. It
also fixes the Color primaries brightness ramp to a neutral black → grey → white luminance
ramp.

The design references (`ansel-slider-spec.md`, `ansel-slider-demo.c`, `demo-shot.png`) live
OUTSIDE this repository, under the Open Design project directory. This plan therefore embeds
the geometry and draw rules it needs — a cold implementer must not depend on those external
files. The full-size demo used baseline 9 / marker 16 / gap 5; this plan uses the compact
constant set (see D3).

## Background

Every **bauhaus** slider is drawn once, in `src/widgets/bauhaus.c`, so the visual restyle
applies to all of them without touching any module. (`src/widgets/gradientslider.c` is a
separate widget used by the blend GUI and is out of scope.) Gradient stops are set per module
through the public `dt_bauhaus_slider_set_stop(widget, stop, r, g, b)` API, which stores raw
RGB into the slider's private `grad_col`/`grad_pos` arrays; the ramp replaces the fill when
present. Colour tokens are GTK named colours in `data/themes/ansel.css`, read at theme-load
time and cached as `GdkRGBA` in `dt_bauhaus_t`.

`DtBauhausWidget` is a `GTK_TYPE_DRAWING_AREA` and cannot host a child widget, so the inline
editor is hosted in a **`GtkPopover` anchored to the slider** and pointed at the value rect.
`GtkPopover` owns its own window, input grab, focus and click-out dismissal, so it does not
touch the existing calculator/combobox popup (which grabs input on `popup_area` and draws a
magnifier). The tree already uses this pattern (`src/widgets/popup.c:112`,
`src/views/dev_toolbox.c:341`, `src/libs/textnotes.c:536`).

### Embedded geometry (compact set)

| Constant | Value | Note |
|---|---|---|
| `BH_BASELINE` | 8.0 | rail thickness (was 5) |
| `BH_RADIUS` | 4.0 | rail corner radius = baseline / 2 |
| `BH_MARKER` | 14.0 | ring outer diameter (was `line_height * 0.6`) |
| `BH_RING_WIDTH` | 2.0 | ring stroke, always hollow |
| `BH_HALO` | 4.0 | hover halo, outside the ring |
| `BH_NOTCH_OVER` | 2.0 | origin notch overshoot, top and bottom |
| `BH_GAP` | 4.0 | label baseline → track top |
| `BH_PAD` | 3.0 | widget padding, top and bottom |

Row height = `ceil(pad + line_h + gap + baseline + (marker - baseline)/2 + pad)`.

Draw rules:
- **Track:** rounded rect across the full width at `y = pad + line_h + gap`, radius
  `BH_RADIUS`, filled `rgba(0,0,0,0.30)`, then clipped. Inside the clip: if `grad_cnt > 0`,
  the colour ramp (per-stop alpha **0.55**); otherwise the bipolar fill in the fill colour,
  growing from `origin` to the current fraction (`fill_x`). After unclipping, draw a 1 px
  white-at-30% notch at `origin` with ±`BH_NOTCH_OVER` overshoot when `origin` is strictly
  interior.
- **Indicator:** hollow ring of outer diameter `BH_MARKER`, stroke `BH_RING_WIDTH`, centred at
  `pos_to_x(fraction)`, `track_cy`. Enabled: dark drop shadow ring (+0.5 y, alpha 0.45); hover
  halo (`orange_light` at 16%) when hot; ring stroke = `bauhaus_indicator_border` (`grey_95`),
  white when hot. Disabled: white at 35%, **no shadow, no halo**.
- **Mapping:** `pos_to_x(p) = inset + p * (width - 2*inset)`, `inset = BH_MARKER/2`;
  `fill_x(p)` is identical except it snaps to `0.0`/`width` at the extremes so a unipolar
  slider at max leaves no un-fillable stub. `x_to_pos` is the clamped inverse.

## Codebase Map

- Entry points:
  - `src/widgets/bauhaus.c` — the whole slider: theme load `dt_bauhaus_load_theme` (:1133),
    constants `baseline_size` (:1226) / `marker_size` (:1228), baseline draw
    `dt_bauhaus_draw_baseline` (:2266), indicator draw `dt_bauhaus_draw_indicator` (:2194),
    value text in `_widget_draw` (:2882-2890), hit test `_bh_get_active_region` (:334) over
    `_bh_active_region_t` (:315), main width `_widget_get_main_width` (:205), height
    `_get_slider_height` (:255), bar height `_get_slider_bar_height` (:267), track centre
    `_get_indicator_y_position` (:262).
  - `src/widgets/bauhaus.h` — public API; `dt_bauhaus_t` (:241-278) holds the shared popup
    (`current` :245, `popup_window` :246, `popup_area` :247), the cached scalar metrics
    (`line_height`, `marker_size`, `baseline_size`, `border_width`, `quad_width`,
    `pango_font_desc` :269-274), the `keys`/`keys_cnt` buffer (:261-262), and theme colours as
    `GdkRGBA` (:277-278). `dt_bauhaus_slider_set_stop` (:368);
    `dt_bauhaus_slider_get_text` (:344) is the value formatter.
  - `src/gui/application.c:2038` — `dt_gui_load_theme()`, loads `data/themes/ansel.css` via
    `gtk_css_provider_load_from_data` (GTK named colours, no text preprocessor).
- Module boundaries: `src/widgets/` is the widget layer; modules (`src/iop/`, `src/libs/`)
  only call its public API. `src/widgets/CMakeLists.txt:13,40` builds `ansel_widgets`, which
  `src/CMakeLists.txt:864` whole-archives into `lib_ansel`.
- Reuse targets (do NOT duplicate):
  - `dt_draw_rounded_rectangle_path()` — `src/widgets/draw.h:541`;
    `dt_gui_draw_rounded_rectangle()` — `src/widgets/draw.h:554`; `set_color()` —
    `src/widgets/draw.h:125`. The demo's own `rounded_rect` is a duplicate of these.
  - Colour source: `dt_bauhaus_t`'s cached `GdkRGBA` fields (:277-278), filled at
    `bauhaus.c:1143-1156`; generic helper `dt_widget_set_source_rgba()` /
    `dt_widget_colors()` (`src/widgets/widget_settings.c:450,461`). There is no
    `dt_gui_get_color`/`dt_gui_lookup_color`.
  - `dt_bauhaus_slider_get_text()` (`bauhaus.h:344`) — seed text for the editor.
- Existing popup vs popover: the calculator/combobox popup is `GTK_WINDOW_POPUP` created at
  `bauhaus.c:1244`, single child `popup_area` (`:1266`), input grabbed on `popup_area`
  (`gtk_grab_add`, :1040-1043), force-focused (`:3068,3077`), keys consumed by
  `dt_bauhaus_popup_key_press` (:3493), magnifier + square sizing in the slider branch
  (:2511-2583, :3003-3008), and handlers deref `bh->current` as the sole widget
  (:396, :889, :3496, :3605). **Do not host the editor there.** `GtkPopover` precedent:
  `src/widgets/popup.c:112` (`gtk_popover_set_pointing_to` at :88),
  `src/views/dev_toolbox.c:341`, `src/libs/textnotes.c:536`.
- Slider census: 474 constructor call sites (346 `from_params`, 112 `with_range`,
  13 `with_range_and_feedback`, 3 `new`); 119 `set_stop` call sites (121 occurrences incl.
  definition). Highest files: `colorbalancergb.c` 32, `channelmixerrgb.c` 31,
  `splittoningrgb.c` 20, `filmicrgb.c` 19, `colorprimaries.c` 9 call sites (24 widgets:
  6 nodes x hue/saturation/brightness + 6 options). This census bounds the gradient question
  only; the restyle is global in `bauhaus.c` and needs NO call-site edits.
- Color primaries site to change: `_refresh_slider_gradients` (`colorprimaries.c:1031-1108`);
  brightness stops at (:1089-1098); hue loop (:1057-1063) and saturation (:1078-1085) are
  already accurate and stay untouched; node names (:201-219); brightness range/soft-range
  `-0.25…0.25` (:99, :1305).
- Gradient draw and alpha: ramp block `bauhaus.c:2291-2324`, alpha literal `0.4f` at
  **:2300**.
- Value/label width split (all four sites must agree): widget value `:2882-2890` and label
  `:2882-2902`; popup value `:2558-2568` and label `:2571-2579`; both compute
  `label_width = text_width - value_width - INNER_PADDING` (`:2572`, `:2895`).
- Disabled indicator: `dt_bauhaus_draw_indicator` is currently called only inside
  `if(gtk_widget_is_sensitive(widget))` (`bauhaus.c:2875-2880`), and the ring uses `color_fg`
  unconditionally (:2219). The insensitive colour available is
  `color_fg_insensitive = alpha(@bauhaus_fg,0.5)` (`ansel.css:161`).
- `_get_indicator_y_position` (`bauhaus.c:262-265`) returns
  `line_height + INNER_PADDING + baseline_size/2` — it does NOT account for the new gap and
  must be updated; `_get_slider_bar_height` (:267) feeds the popup magnifier normalization
  (:925, :2522) and must follow.
- Tests: cmocka. `tests/unittests/CMakeLists.txt:37-82` lists targets in
  `LIB_ANSEL_UNIT_TESTS` and links each with `${CMOCKA_LINK_TARGET}` + `lib_ansel`
  (:59-63); `add_cmocka_test` is `cmake/modules/AddCMockaTest.cmake:72-119`; Windows injects
  `win_main.c` (`tests/unittests/CMakeLists.txt:4-7`, `:61`). Display-free cairo precedent:
  `tests/unittests/test_paint_module_switch.c:56` and `test_stroke_raster.c:60` create a
  `cairo_image_surface_create(CAIRO_FORMAT_ARGB32, …)` with no `gtk_init`. The idiomatic
  widgets-layer test links `lib_ansel` and includes the unit's public header
  (`test_stroke_raster.c:27`, `test_paint_module_switch.c:27`).
- Commands: configure `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
  -DBUILD_TESTING=ON`; build+stage `./rebuild.sh` (MSYS2 MINGW64); run a test from `build/`
  with `PATH=/c/msys64/mingw64/bin:$PATH ctest -R <name>`; run staged app
  `build/stage/bin/ansel.exe`.

## Non-Goals

- No audit or recolouring of gradient ramps in any module other than Color primaries
  brightness (colorbalance, temperature, colorbalancergb, etc. are untouched).
- No new abstract gradient API in `bauhaus.h`; no per-module call-site sweep.
- No change to slider interaction semantics other than adding the inline editor (drag, shift
  fine-drag, wheel, arrow keys, double-click reset are preserved).
- The right-click popup and its calculator/expression entry are kept; inline editing is
  plain numeric and does not replace them.
- No combobox, quad-button, `gradientslider.c`, or other bauhaus widget restyle; no
  display-backed GUI test harness.
- No restyle of the popup magnifier itself beyond keeping its geometry consistent with the
  new rail.

## Design Decisions

### D1: Inline value-entry hosting
- **Chosen:** a `GtkPopover` anchored to the slider widget
  (`gtk_popover_new(widget)` + `gtk_popover_set_pointing_to(value_rect)`) holding one shared
  `GtkEntry`. GTK owns the popup window, input grab, focus and click-out dismissal.
- **Rejected:** the existing shared `GTK_WINDOW_POPUP` — it grabs input on `popup_area`
  (`bauhaus.c:1040-1043`), force-focuses it (`:3068,3077`), consumes keys into the calculator
  buffer (`:3493`), draws a magnifier and sizes the window square (`:3003-3008`), and every
  handler assumes `bh->current` is the sole active widget — a `GtkEntry` there is unreachable
  by keyboard and the combobox/calculator path is at risk. A panel `GtkOverlay` over
  `"plugins_box"` (`window_manager.c:399`) — no `get-child-position` precedent exists in the
  tree, the box sits inside a `GtkViewport` so overlay children clip on scroll, and every
  other host would need the same wrap. A dedicated `GTK_WINDOW_POPUP` — hand-rolls the grab,
  focus and dismiss semantics `GtkPopover` already provides.
- **Consequences:** Phase 3 stores the popover + entry on `dt_bauhaus_t`; the popover's
  lifecycle (shown on value click, popped down on Enter/Escape/click-out) is independent of
  the calculator popup. No change to `popup_area`, its allocation, or `bh->current`.

### D2: Placement and inputs of the drawing/geometry helpers
- **Chosen:** a new cairo-only unit `src/widgets/bauhaus_draw.{c,h}` holding the
  `BhMetrics`-based geometry and track/ring raster, called from `bauhaus.c`. It reuses
  `draw.h`'s rounded-rect and colour primitives and receives colours as `const GdkRGBA *`
  sourced from `dt_bauhaus_t`/`dt_widget_colors()` — no parallel colour-resolution path, no
  raw `double[3]` arrays.
- **Rejected:** keeping the helpers `static` in `bauhaus.c` (matches the spec's in-place
  wording but no display-free test can reach them, so every phase degrades to manual-only);
  promoting them into `bauhaus.h` (pollutes public widget API for no caller).
- **Consequences:** Phase 1 is unit-testable on a cairo image surface. `BhMetrics` is a
  by-value view populated in `bauhaus.c` from `dt_bauhaus_t`'s already-cached `line_height`,
  `baseline_size`, `marker_size` (`bauhaus.h:269-274`) plus the measured value-field width;
  it is not a new source of truth.

### D3: Geometry constants
- **Chosen:** the compact set embedded in the Background table (baseline 8, radius 4,
  marker 14, ring stroke 2, halo 4, notch overshoot 2, gap 4, pad 3). Row height derived by
  the formula above.
- **Rejected:** the demo's full set (baseline 9 / marker 16 / gap 5) — rows grow ~7 px each,
  which on a twelve-slider module adds ~84 px of panel height for a small legibility gain.
- **Consequences:** the ring must not shrink below 14 px (below that the hollow centre closes
  up); `INNER_PADDING` stays 2 (used widely elsewhere, including IOP custom draws) and a
  slider-local gap constant is added.

### D4: Gradient accuracy scope
- **Chosen:** fix only the Color primaries brightness ramp, inline in `colorprimaries.c`, to a
  neutral black → mid-grey → white ramp. Hue and saturation are already correct.
- **Rejected:** a semantic-gradient API (`luma`/`grey→colour`/`hue`) in bauhaus — the census
  found no second luminance-parameter slider painted in a channel colour, so the abstraction
  has no second real call site; a full 119-site audit — subjective and out of scope.
- **Consequences:** no public API change; if a second offender appears later, the API can be
  added then.

### D5: Value field width is measured, not hardcoded
- **Chosen:** each slider reserves a value-field width measured once from the widest of its
  formatted min/max through its own `format`/`factor`/`offset`/`digits`
  (`dt_bauhaus_slider_get_text`, `bauhaus.h:344`), recomputed when any of those change.
- **Rejected:** a fixed character count (the design demo's 6.5 mono chars) — values carry
  suffixes (`%`, ` EV`, `°`, ` px`) and a factor/offset (`set_format("%")` sets factor 100 at
  `bauhaus.c:3403-3416`), and the value font is the widget default, not mono; a fixed count
  clips or overlaps the label.
- **Consequences:** the reserved width feeds `_widget_get_main_width` and all four
  value/label split sites; the field stays a constant width per slider regardless of the
  current value.

## Progress
- [x] Phase 1: Raster core — `bauhaus_draw` unit
- [x] Phase 2: Wire the ring-grip restyle into `bauhaus.c` and the theme
- [x] Phase 3: Inline value editor via a `GtkPopover`
- [x] Phase 4: Accurate Color primaries brightness ramp
- [ ] Final verification

## Phases

### Phase 1: Raster core — `bauhaus_draw` unit
**Risk:** none
**Test-first:** required
**Goal:** a display-free cairo unit that owns the slider geometry and track/ring raster,
pinned by image-surface tests.
**Assumes:**
- `src/widgets/CMakeLists.txt` builds the `ansel_widgets` target and unit tests link
  `lib_ansel` (confirmed: `src/CMakeLists.txt:864`, `tests/unittests/CMakeLists.txt:59-63`).
- No GTK or display is available to tests; only cairo image surfaces are (confirmed:
  `test_paint_module_switch.c:56`).

**Files:**
- `src/widgets/bauhaus_draw.h` — new: `BhMetrics`, `BhGradStop`, `BhTrackState`, constants,
  and the function contracts below.
- `src/widgets/bauhaus_draw.c` — new: the geometry + track/ring raster implementing the
  embedded draw rules.
- `src/widgets/CMakeLists.txt` — add `bauhaus_draw.c` to the widgets source list.
- `tests/unittests/test_bauhaus_draw.c` — new cmocka test.
- `tests/unittests/CMakeLists.txt` — add `test_bauhaus_draw` to `LIB_ANSEL_UNIT_TESTS`.

**Reuse:**
- `dt_draw_rounded_rectangle_path` / `dt_gui_draw_rounded_rectangle` (`draw.h:541,554`) for
  the rounded rail — do NOT port the demo's `rounded_rect`.
- `set_color` (`draw.h:125`) for stroke/fill sources where a `GdkRGBA` is available.
- Pattern to mirror: `tests/unittests/test_paint_module_switch.c` — cairo image-surface setup
  and per-pixel assertion style (setup :56-57), linked `lib_ansel` + public header.

**Contracts:**
- `void dt_bauhaus_draw_track(cairo_t *cr, const BhMetrics *m, const BhTrackState *s)` —
  recessed rounded rail (clipped), then colour ramp (when `grad_cnt > 0`) or bipolar fill from
  `origin`, then the origin notch.
- ~~`void dt_bauhaus_draw_indicator(cairo_t *cr, const BhMetrics *m, const BhTrackState *s)`~~ →
  `void dt_bauhaus_draw_ring(...)` (same signature; renamed — see Reconciliations 2026-09-20) —
  hollow ring; shadow only when `!disabled`; halo only when `hot && !disabled`; disabled ring
  white at 35%, no shadow/halo.
- `void dt_bauhaus_value_rect(const BhMetrics *m, int *x, int *y, int *w, int *h)` — right-
  aligned value field, width = `m->value_w`.
- `double dt_bauhaus_pos_to_x(const BhMetrics *m, double p)` / `dt_bauhaus_fill_x` /
  `dt_bauhaus_x_to_pos` — as defined in the Background mapping rules.
- `BhMetrics` fields: `line_h`, `track_top`, `track_cy`, `inset`, `width`, `value_w`.
- ~~`BhTrackState` fields: `frac`, `origin`, `disabled`, `hot`, `grad_cnt`,
  `const BhGradStop *grad`, and `const GdkRGBA *fill`, `*ring`, `*ring_hover`.~~ → the same plus
  `const GdkRGBA *halo` (see Reconciliations 2026-09-20).
- Constant `BH_RAMP_ALPHA 0.55` (replaces the `0.4f` literal at `bauhaus.c:2300`).

**Out of scope:**
- No edits to `bauhaus.c` in this phase; the unit is not yet called.
- No GTK/Pango includes in the unit: text, theme lookup and metrics measurement stay in
  `bauhaus.c`.
- No value parsing, hit-region enum, or popover (Phase 3).
- No changes to any `src/iop/` module.

**Tests (write first, confirm red):**
- [x] A signed slider whose value equals its origin draws no fill pixels (zero-width fill).
- [x] A unipolar slider at max fills the rail to both rail ends — no dead stub at either end
  (`dt_bauhaus_fill_x` extreme snapping).
- [x] A gradient slider's ramp is drawn inside the rail clip, and the ring's hollow centre
  shows the ramp, not the ring stroke colour (sample the centre pixel).
- [x] A disabled slider draws the ring at ~35% white, with no hover halo and no drop shadow.
- [x] `dt_bauhaus_value_rect` is right-aligned and its width equals `m->value_w` for any
  current value (the value does not enter the computation).
- [x] `dt_bauhaus_x_to_pos`/`dt_bauhaus_pos_to_x` round-trip within tolerance and clamp to
  `[0, 1]` outside the rail.

**Steps:**
1. Write `tests/unittests/test_bauhaus_draw.c` with the assertions above; register it; build
   and confirm it FAILS to compile/link (red) because the unit does not exist.
2. Add `src/widgets/bauhaus_draw.h` with the structs, constants and contracts.
3. Add `src/widgets/bauhaus_draw.c` implementing the geometry and raster via `draw.h`
   primitives.
4. Add the source to `src/widgets/CMakeLists.txt`; build; confirm the tests PASS (green).

**Acceptance criteria:**
- [x] `ctest -R test_bauhaus_draw` passes from `build/`.
- [x] `bauhaus_draw.c` includes no GTK/Pango header (grep the include list).

### Phase 2: Wire the ring-grip restyle into `bauhaus.c` and the theme
**Risk:** flagged (!#1, !#4, !#5, !#7)
**Test-first:** N/A — the raster is covered by Phase 1 on a cairo surface; embedding it in a
live `GtkWidget` requires a display the test harness does not have (`test_paint_module_switch.c`
is the only display-free precedent, and it does not build a widget).
**Goal:** the new rail/ring/notch geometry and the measured value field are what every
on-screen slider draws, in both the main widget and the popup.
**Assumes:**
- Phase 1 contracts exist and are green.
- `dt_bauhaus_load_theme` runs before any draw and caches theme colours (confirmed:
  `bauhaus.c:1133`; re-run on theme hot-reload, `application.c:2003`).

**Files:**
- `src/widgets/bauhaus.c`:
  - `dt_bauhaus_load_theme` constants (:1226-1228) → the compact values.
  - `dt_bauhaus_draw_baseline` (:2266) and `dt_bauhaus_draw_indicator` (:2194) →
    call the Phase 1 unit; ramp alpha `0.4f` (:2300) → `BH_RAMP_ALPHA`.
  - `_get_slider_height` (:255) → the derived formula; `_get_indicator_y_position` (:262) →
    account for `BH_GAP`/pad so the ring and notch sit on the rail centre;
    `_get_slider_bar_height` (:267) → keep the popup magnifier consistent.
  - `_widget_get_main_width` (:205) → reserve the measured value width and the larger marker
    inset.
  - Add `_slider_value_width()` (measured once from the widest formatted min/max via
    `dt_bauhaus_slider_get_text`); apply it at all four value/label split sites
    (:2558-2579 popup, :2882-2902 widget).
  - Call `dt_bauhaus_draw_indicator` outside the `gtk_widget_is_sensitive` gate
    (:2875-2880) so disabled sliders draw the ring with the disabled style; wire
    `indicator_border` (:1148, currently unread) as the enabled ring colour.
  - `_translate_cursor` (:306) and the cursor→value sites (:3666 press, :3702 release,
    :3724 motion, and the popup's magnifier ratio at :924) → map through
    `dt_bauhaus_x_to_pos` with the same metrics, so the ring stays under the cursor
    (Discovery 2026-09-20).
- `src/widgets/bauhaus.h` — one field on `dt_bauhaus_slider_data_t` (:100-122) caching the
  reserved value width (Discovery 2026-09-20).
- `data/themes/ansel.css` — `@bauhaus_indicator_border` from `@grey_50` to `@grey_95`
  (:153).

**Reuse:**
- Call the Phase 1 unit rather than re-implementing raster in `bauhaus.c`.
- Reuse `dt_bauhaus_t`'s cached metrics (`bauhaus.h:269-274`) to populate `BhMetrics`;
  reuse `dt_bauhaus_slider_get_text` for the width measurement.
- Reuse existing theme lookup and `bh->grad_col`/`grad_pos` for ramp input.

**Contracts:**
- `_get_slider_height` returns `ceil(pad + line_h + gap + baseline + (marker - baseline)/2 + pad)`.
- `_get_indicator_y_position` returns `pad + line_h + gap + baseline/2` (the rail centre).
- Ring colour is the cached `indicator_border`; hover ring is white; disabled ring is white at
  0.35 with no shadow/halo.
- Gradient ramp alpha is 0.55 for every gradient slider, in both widget and popup.
- The reserved value width is constant per slider between redraws and is recomputed when
  `format`/`factor`/`offset`/`digits` change.
- `BH_REGION_VALUE` is NOT introduced here.

**Out of scope:**
- No inline value editor / hit-region change (Phase 3).
- No per-module gradient changes (Phase 4).
- No combobox, quad-button, `gradientslider.c`, or `INNER_PADDING` changes.
- No change to the popup magnifier's appearance beyond geometry consistency.

**Manual verification:**
- [ ] `./rebuild.sh` then `build/stage/bin/ansel.exe`; open darkroom. Every bauhaus slider
      shows a rounded rail and a hollow ring; a signed slider at zero shows the notch with no
      fill.
- [ ] Open Color primaries and a dense module (colorbalancergb, channelmixerrgb): the ramp is
      visible through the ring at every position; measure one slider row height and confirm it
      stays well under the demo's ~34 px.
- [ ] Disabled sliders (e.g. Vibrance at default) draw a 35%-white ring with no shadow/halo.
- [ ] A slider with a unit suffix (`%`, ` EV`, `°`) does not clip its value or overlap its
      label; dragging it never changes the value-field width.
- [ ] Right-click a slider: the calculator popup still opens, shows the magnifier, and
      commits (no regression from the draw changes).

**Steps:**
1. Switch the geometry constants and the height/centre/bar/width formulas in `bauhaus.c`.
2. Replace the two draw-function bodies with calls into `bauhaus_draw`; change the ramp alpha.
3. Add the measured value width and apply it at all four split sites.
4. Un-gate the disabled indicator and wire `indicator_border`; change the CSS token.
5. Map the cursor through `dt_bauhaus_x_to_pos` at the press/release/motion and popup-ratio
   sites (Discovery 2026-09-20).
6. Build, stage, and run the manual checks above.

**Acceptance criteria:**
- [x] `test_bauhaus_draw` still passes after the wiring.
- [x] No `src/iop/` source file is modified by this phase (only `bauhaus.c` + the CSS).
- [x] Every ~~`baseline_size`~~/`marker_size` consumer still compiles and behaves (quad buttons,
  comboboxes unaffected — `_widget_get_main_width` applies `marker_size` only to sliders).
  `baseline_size`/`border_width` were removed from `dt_bauhaus_t` (write-only after the restyle);
  the quad hit boundary regression the first 3F pass found is fixed (see the handoff log).

### Phase 3: Inline value editor via a `GtkPopover`
**Risk:** flagged (!#2, !#3, !#6)
**Test-first:** required
**Goal:** clicking a slider's value opens a typable field over it; typing a number and pressing
Enter commits it, Escape reverts, and popping down commits.
**Assumes:**
- Phase 1's `dt_bauhaus_value_rect` is available and `BhMetrics.value_w` is populated.
- A slider is a `GtkWidget` a popover can anchor to (`bauhaus.c:86`).

**Files:**
- `src/widgets/bauhaus_draw.h` / `bauhaus_draw.c` — add
  `int dt_bauhaus_value_parse(const char *text, double factor, double offset, double min,
  double max, double *out)` returning 1 on commit / 0 on reject, and
  `int dt_bauhaus_value_hit(const BhMetrics *m, double x, double y)` returning 1 inside the
  value rect.
- `tests/unittests/test_bauhaus_draw.c` — tests for both.
- `src/widgets/bauhaus.h` — add the shared popover + entry handles to `dt_bauhaus_t` beside
  the existing popup fields (:241-247).
- `src/widgets/bauhaus.c` — add `BH_REGION_VALUE` to `_bh_active_region_t` (:315) and test it
  before `BH_REGION_MAIN` in `_bh_get_active_region` (:334); create the popover + entry at
  init; open it over the value rect on a value click; commit/revert/popdown; skip drawing the
  value text while it is open.
- `data/themes/ansel.css` — styling for the popover entry.

**Reuse:**
- `GtkPopover` pattern from `src/widgets/popup.c:112` (+ `gtk_popover_set_pointing_to` :88)
  and `dev_toolbox.c:341`.
- `dt_bauhaus_slider_get_text` (`bauhaus.h:344`) for the entry's initial text.
- `dt_bauhaus_slider_set_val` for the commit path (it applies the existing `factor`).

**Contracts:**
- Interaction: click a value opens the popover and selects all; Enter commits via
  `dt_bauhaus_value_parse`; Escape reverts; popdown (click-out) commits; the track keeps drag,
  shift fine-drag (0.2x), double-click reset, wheel, and arrow keys.
- `dt_bauhaus_value_parse` accepts optional surrounding whitespace and a leading `+`/`-`,
  interprets `text` in **display** units (`val * factor + offset`), converts back to domain
  units, clamps to `[min, max]`, and returns 0 for empty or non-numeric input (no commit).
- `dt_bauhaus_value_hit` returns 1 only inside `dt_bauhaus_value_rect`, so a click elsewhere on
  the row starts a drag.
- The popover is independent of the calculator popup: it does not touch `popup_area`,
  `bh->current`, or the input grab at `bauhaus.c:1040-1043`.

**Out of scope:**
- No IME, no clipboard-format handling beyond the `GtkEntry` default, no expression/calculator
  support in the inline field (the right-click popup keeps that).
- No changes to the calculator popup's key handling.
- No per-module changes.

**Tests (write first, confirm red):**
- [ ] `dt_bauhaus_value_parse("45", 1.0, 0.0, 0, 100, &v)` commits and yields 45.
- [ ] On a percent slider (`factor = 100`), `dt_bauhaus_value_parse("45", 100.0, 0.0, 0, 1, &v)`
  commits and yields 0.45 (display units converted to domain units).
- [ ] Parse clamps a below-min and an above-max value to the bound.
- [ ] Parse rejects `""` and `"abc"` without committing.
- [ ] Parse accepts leading/trailing whitespace and a leading sign (`" +45 "`, `"-3.5"`).
- [ ] `dt_bauhaus_value_hit` reports a hit at a point inside the value rect and no hit just
  outside it (value vs main precedence).

**Steps:**
1. Add the tests above; run; confirm they FAIL (red) — the helpers do not exist.
2. Implement `dt_bauhaus_value_parse` / `dt_bauhaus_value_hit`; confirm green.
3. Add `BH_REGION_VALUE` and its precedence in the hit test.
4. Create the popover + entry, wire open/commit/revert/popdown, and skip the value text draw
   while it is open; build and stage.

**Acceptance criteria:**
- [ ] Manual: typing `45`, Enter, sets the slider to 45.00 and returns focus to the track.
- [ ] Manual: clicking the number never moves the value; clicking the rail still scrubs.
- [ ] Manual: the slider popup (right-click) and the combobox popup still open and commit
  correctly (unaffected — separate window).
- [ ] Manual: on a `%` slider, typing `45` yields 45% (value 0.45), not 45.00.

### Phase 4: Accurate Color primaries brightness ramp
**Risk:** none
**Test-first:** N/A — the stops are computed inside the static `_refresh_slider_gradients`
(`colorprimaries.c:1031-1108`); there is no display-free seam, and adding a public helper for a
single call site would be API pollution.
**Goal:** the Color primaries brightness sliders show a neutral luminance ramp, not the node's
saturated hue.
**Assumes:**
- `dt_bauhaus_slider_set_stop` is the only colour mechanism (confirmed: `bauhaus.h:368`).
- Hue and saturation ramps are already accurate and must not change.

**Files:**
- `src/iop/colorprimaries.c` — the brightness branch of `_refresh_slider_gradients`
  (:1089-1098): replace the hue/saturation-tinted stops with a neutral black → mid-grey → white
  ramp (stops at pos 0, 0.5, 1 with `{0,0,0}`, `{0.5,0.5,0.5}`, `{1,1,1}`).

**Reuse:**
- `dt_bauhaus_slider_set_stop` (`bauhaus.h:368`); do not add a new API.
- Leave the hue (:1057-1063) and saturation (:1078-1085) branches untouched.

**Out of scope:**
- No changes to any other module's ramps.
- No new gradient API, no `bauhaus.c`/theme changes.

**Manual verification:**
- [ ] Open Color primaries; all six nodes' brightness sliders show a black → grey → white ramp
      with the origin notch at centre; hue and saturation ramps are unchanged.
- [ ] Drag a brightness slider and confirm the fill still grows from the centre in both
      directions.

**Steps:**
1. Replace the brightness stop computation with the neutral ramp.
2. Build and stage; open Color primaries and confirm the ramp.

**Acceptance criteria:**
- [x] Only the brightness branch of `_refresh_slider_gradients` changed in `colorprimaries.c`.
- [ ] Color primaries renders and adjusts without error in the staged app.

## Verification
- [ ] Configure with `-DBUILD_TESTING=ON` and run `./rebuild.sh` (MSYS2 MINGW64).
- [ ] `PATH=/c/msys64/mingw64/bin:$PATH ctest -R test_bauhaus_draw` passes from `build/`.
- [ ] Run the full test suite (`ctest`) and confirm no regression in the existing targets.
- [ ] Run `build/stage/bin/ansel.exe` and walk the Phase 2 and Phase 4 manual checks, plus the
  Phase 3 editor interactions.
- [ ] Confirm the design's acceptance checks on real sliders: ramp through ring, zero notch, no
  dead stub at max, click-the-number does not move the value, `45`+Enter sets 45.00, constant
  value-field width, disabled ring at 35% white.
- [ ] Lint/format: `clang-format` per `.clang-format` on the changed files; `git diff --check`
  clean.

## Notes

- The design source files (`ansel-slider-spec.md`, `ansel-slider-demo.c`, `demo-shot.png`) are
  external to this repo; the geometry and draw rules they specify are embedded in this plan's
  Background so an implementer needs no access to them.
- The right-click slider popup (with `dt_calculator_solve` expression entry) is deliberately
  kept alongside the new inline editor; only a left click on the value opens the popover.
- `bauhaus_indicator_border` is currently loaded but never read (`bauhaus.c:1148`); Phase 2 is
  where it starts being used. `color_fill`/`color_value_insensitive` remain unused.
- `src/widgets/gradientslider.c` (used by `src/develop/blend_gui.c:3593`) is a separate widget
  and does not inherit this restyle.
- The census (474 constructor call sites / 119 `set_stop` sites; top files `colorbalancergb.c`
  32, `channelmixerrgb.c` 31, `splittoningrgb.c` 20, `filmicrgb.c` 19) exists only to bound the
  gradient question; the restyle itself is global in `bauhaus.c` and needs no call-site edits.
- If the compact geometry still costs too much panel height on dense modules, the tunables are
  `baseline`/`marker`/`gap`; do not take the ring below 14 px.

## Risks
#1. **Global panel density grows with every slider row** — every module panel gets taller.
    Mitigated by the compact constants (D3); Phase 2 measures a real row. Confirm on the
    densest modules before accepting.
#2. **`GtkPopover` lifecycle and dismissal** — the editor must open only on a value click,
    pop down on Enter/Escape/click-out without losing the commit, and not interfere with an
    in-progress drag. If dismissal races the drag handlers, add a mode guard rather than
    reusing the calculator popup's `bh->current` assumption.
#3. **The value hit-rect competes with drag-start** — if the rect is larger than the visible
    number, a click meant to scrub opens the editor. The rect is pinned to the value field
    bounds and the hit test gives it precedence only inside those bounds.
#4. **Gradient ramp alpha 0.4 → 0.55 repaints every existing gradient slider** — intended (a
    thicker rail reads poorly at 0.4), but it is a visible change app-wide. If any ramp now
    reads too strong, lower the constant rather than special-casing modules.
#5. **The measured value width depends on `format`/`factor`/`offset`/`digits`** — if any of
    those change after construction and the width is not recomputed, the field clips or
    overflows. Recompute on change; keep the width constant between changes.
#6. **Value parsing must convert display units before clamping** — `set_format("%")` raises
    `factor` to 100 (`bauhaus.c:3403-3416`) and `set_factor` is used elsewhere
    (`masks_gui.c:540`); parsing in domain units would clamp percent/offset sliders wrongly.
    The Phase 3 tests pin the percent case.
#7. **Disabled sliders gain a visible ring** — today they draw no indicator
    (`bauhaus.c:2875-2880`); un-gating is required for the design but is a visible behavior
    change. Confirm the insensitive ring reads as disabled, not active.

## Reconciliations
<!-- Drift amendments written by /implement during execution. Append-only. Outdated phase
text above is struck through (~~...~~) but preserved; entries here are the authoritative
correction. Empty at plan creation. -->

### 2026-09-20 — Phase 1: frozen contract collided with existing code → renamed + extended
- `dt_bauhaus_draw_indicator` is already taken by `static void dt_bauhaus_draw_indicator(struct
  dt_bauhaus_widget_t *w, float pos, cairo_t *cr, float wd)` at `bauhaus.c:2194`. Phase 2 must
  include `bauhaus_draw.h` from `bauhaus.c` while keeping that static as the wrapper whose body
  calls the unit, so an extern of the same name is a compile error ("static declaration follows
  non-static declaration"). **Authoritative name: `dt_bauhaus_draw_ring`.** `dt_bauhaus_draw_track`
  is unchanged — no collision (the existing static is `dt_bauhaus_draw_baseline`).
- `BhTrackState` had no accent colour for the `orange_light` hover halo the draw rules require.
  **Authoritative field list adds `const GdkRGBA *halo`.** Phase 2 wires it to
  `bauhaus->color_value_text` (`@bauhaus_value_text` = `@orange_light`; loaded at
  `bauhaus.c:1153`).
- Phase 2 colour wiring for the remaining fields (recorded here, not previously stated):
  `fill` = `bauhaus->color_value` (`@orange_dark`), `ring` = `bauhaus->indicator_border`,
  `ring_hover` = white. Draw alphas beyond the pinned `BH_RAMP_ALPHA` 0.55 and disabled-ring 0.35
  (rail 0.30, notch 0.30, ring 0.92, halo 0.16, shadow 0.45, disabled dim 0.42) come from the
  design demo and are settled in `docs/plans/.impl/PLAN-ring-grip-slider-phase-1.md`.
- No requirements-level change; the design intent is unchanged.

## Discoveries
<!-- Non-contradictory findings logged by /implement during execution (act / defer / drop).
Append-only, empty at plan creation. -->

2026-09-20 — Phase 2: the new symmetric rail inset changes the cursor→value mapping, which the
phase text never mentions. `pos_to_x(p) = inset + p*(width - 2*inset)` means a click lands the
ring under the cursor only if the input path uses `dt_bauhaus_x_to_pos` with the same `BhMetrics`.
The current code divides a cursor x that was pre-shifted by `0.5*marker_size` by a width that was
reduced by the same radius (`bauhaus.c:308, 212, 3666, 3702, 3724`). Left as-is, the ring lags the
cursor by up to `BH_MARKER/2` = 7 px at the right end (where the old geometry was flush and the new
one stops 7 px short), and ~2-3 px at the left. → Suggested action: act now, convert
`_translate_cursor` to subtract only margin/padding and use `dt_bauhaus_x_to_pos` at the three
main-widget sites; keep the popup's own fine-tune ratio math otherwise.

2026-09-20 — Phase 2: the reserved value width (D5) needs a per-slider cache, but Phase 2's file
list names only `bauhaus.c` and the CSS. There is no spare field in `dt_bauhaus_slider_data_t`
(`bauhaus.h:100-122`) and no draw-free text-measure helper exists in `bauhaus.c`
(`show_pango_text` at :673 always draws). → Suggested action: act now, add one field to that
struct in `src/widgets/bauhaus.h`, set it from the widest formatted min/max, and invalidate it in
`dt_bauhaus_slider_set_format|set_factor|set_offset|set_digits` (:3339-3431, none of which
currently queues a redraw); the alternative is recomputing per call with no cache, which pays a
Pango measure on every draw and every motion-event hit test.

2026-09-20 — Phase 2: `_widget_get_main_width`'s post-restyle contract is ambiguous ("reserve the
measured value width and the larger marker inset"). → Settled in the phase impl plan (no
developer decision needed): the rail width is `total - quad - 2*INNER_PADDING` and the unit applies
the 7 px inset itself (the old `- slider_cursor_radius` term goes away), `text_width` becomes that
same value so the value is right-aligned flush with the rail's right edge, and the reserved value
width only sizes the label split (`label_width = text_width - value_w - INNER_PADDING`).

2026-09-20 — Phase 2 (3F SIMPLIFY report, **deferred**): `_bh_get_active_region` ends with an
unreachable `return BH_REGION_OUT;` after an `if/else` that returns from both branches. It is
pre-existing (present before this phase) and harmless. → Later cleanup, or drop; logging so it is
not re-raised.

2026-09-20 — Phase 2 (3F SIMPLIFY report, **deferred**): the eight draw alphas in
`bauhaus_draw.c:26-33` restate the values settled in the Phase 1 impl plan instead of deriving
from a named token. They exist only inside that unit and nothing else reads them. → No action
needed; noting so it is not re-raised.

2026-09-20 — Phase 2 (review note, **open verification**): `_measure_text` measures under the
default cairo CTM while `show_pango_text` draws under `dt_cairo_surface_create_at_scale(..., dt_widget_ppd())`.
`pango_layout_get_size` is CTM-independent so the widths should agree, but this link in the D5
chain cannot be settled read-only. → The Phase 2 manual check on a non-1.0 display scale
(value not clipped or overlapping) is the test; if it fails, scale the measurement by `dt_widget_ppd()`.

2026-09-21 — Phase 3 (**deferred**): `dt_bauhaus_value_rect` returns `y = BH_PAD` (3) while the
value text is drawn at `y = 0` inside a `line_height`-tall box (`bauhaus.c:2872-2878`, since
`show_pango_text` uses `bounding_box->y` directly), so the value hit rect sits 3 px below the
visible number. The top 3 px of the number lands in the no-op header branch, and the 3 px of rect
below the text falls in the dead band between the label row and the rail (rail top = `BH_PAD +
line_h + BH_GAP` = 23), so no drag area is stolen — it is a misalignment, not a hijack.
→ Deferred: fixing it means either moving the label/value draw to `y = BH_PAD` (a Phase 2 visual
change to a layout whose manual checks are still unrun) or changing the Phase 1 rect and its
pinned `y = 3` test. Decide after the Phase 2 manual checks.

## Phase Handoff Log
<!-- Written by /implement at each 3G phase gate (Done / Learned / Drift / Watch-next per
phase). Append-only, empty at plan creation. MUST remain the LAST section of this file:
/implement's Step 2 reads the plan up to this heading plus only the log's final entry, so
never add a section below it. -->

### 2026-09-20 — Phase 1: Raster core — bauhaus_draw unit
- Done: new `src/widgets/bauhaus_draw.{c,h}` (BhMetrics/BhGradStop/BhTrackState, the BH_*
  geometry constants, `dt_bauhaus_pos_to_x|fill_x|x_to_pos`, `dt_bauhaus_value_rect`,
  `dt_bauhaus_draw_track`, `dt_bauhaus_draw_ring`) wired into `ANSEL_WIDGETS_SOURCES`; new
  `tests/unittests/test_bauhaus_draw.c` (7 tests, display-free cairo surface) wired into
  `LIB_ANSEL_UNIT_TESTS`. Red confirmed first (missing header), then green.
- Learned: (1) `dt_draw_rounded_rectangle_path` is `static inline` and takes an explicit radius
  — `dt_gui_draw_rounded_rectangle` is unusable for the rail because it hardcodes radius
  height/5 (= 1.6 at baseline 8). (2) `widgets/draw.h` itself `#include <gtk/gtk.h>` (line 63),
  so the "no GTK include" criterion can only hold on `bauhaus_draw.c`'s own include list; the
  test remains display-free (no `gtk_init`) regardless. (3) `rebuild.sh`/the configure step need
  `MSYSTEM=MINGW64` set in the *parent* shell before launching `bash -l`, or `/etc/profile`
  builds a PATH with no cmake. (4) clang-format 22.1.8 flags untouched files
  (`stroke_raster.c`, `paint.c`) too — the tree is not format-clean, so that check is not a
  usable gate; the new files were deliberately left matching their neighbours.
- Drift: two frozen-contract items amended before coding, both recorded above in
  `## Reconciliations` (2026-09-20): `dt_bauhaus_draw_indicator` → `dt_bauhaus_draw_ring`
  (name collision with the existing static at `bauhaus.c:2194`), and `BhTrackState` gained
  `const GdkRGBA *halo`.
- Watch-next: Phase 2 must keep the two existing statics (`dt_bauhaus_draw_baseline` at
  `bauhaus.c:2266`, `dt_bauhaus_draw_indicator` at `bauhaus.c:2194`) as wrappers whose bodies
  call the unit — so the *unit* is `dt_bauhaus_draw_ring`/`dt_bauhaus_draw_track` while the
  widget-local names stay as they are. Wire `halo` = `color_value_text` (`@orange_light`),
  `fill` = `color_value` (`@orange_dark`), `ring` = `indicator_border` (CSS token changes
  `@grey_50` → `@grey_95`). `dt_bauhaus_draw_ring` must be called outside the
  `gtk_widget_is_sensitive` gate (`bauhaus.c:2875-2880`) for disabled sliders to draw a ring.

### 2026-09-20 — Phase 2: Wire the ring-grip restyle into bauhaus.c and the theme
- Done: `dt_bauhaus_load_theme` now sets `baseline_size`/`marker_size` from the unit's
  `BH_BASELINE`/`BH_MARKER`; the three layout getters derive from the same constants;
  `_widget_get_main_width` returns the full rail width; both draw wrappers forward to
  `dt_bauhaus_draw_track`/`_ring`; the ring is drawn outside the sensitivity gate with a
  35%-white disabled style; `indicator_border` is wired and the CSS token moved `@grey_50` →
  `@grey_95`; a measured per-slider value width (`value_width` on `dt_bauhaus_slider_data_t`)
  now sizes the label split in both the widget and the popup; the cursor→value mapping goes
  through `dt_bauhaus_x_to_pos`.
- Learned: (1) dropping `-slider_cursor_radius` from `_widget_get_main_width` silently moved the
  MAIN/QUAD hit boundary half a marker into every quad — `_translate_cursor` still shifts slider
  cursors left by half a marker, so `_bh_get_active_region` must add that shift back
  (`cursor_shift`) before comparing. Any future change to `_widget_get_main_width` must re-check
  that boundary. (2) `baseline_size` and `border_width` are gone from `dt_bauhaus_t`; `BH_BASELINE`
  and `BH_MARKER` are the single source of truth, and `marker_size` must stay `BH_MARKER` so the
  cursor shift and the unit's `inset` agree. (3) `show_pango_text` and `_measure_text` share
  `_resolve_font`, so a measured string and a drawn one cannot drift. (4) `_get_slider_height` =
  margins/paddings + `line_h + 21` (at `line_h` 16 that is 37 px) — risk !#1 is real: every row
  grew ~8 px, and the visual checks below are unrun. (5) The popup magnifier keeps its own
  translate and ratio; its rail now extends 7 px further right (accepted, see the impl plan §2).
- Drift: none. Two plan gaps were approved through `## Discoveries` instead (cursor mapping; the
  `value_width` field on `dt_bauhaus_slider_data_t`), and the phase's `Files`/`Steps` text was
  amended in place to record them.
- Watch-next: **Phase 2's five manual visual checks have NOT been run** (no display here) — run
  them on `build/stage/bin/ansel.exe` before Phase 3's results are trusted: rail+ring on every
  slider, ramp through the ring on dense modules, row height, disabled ring, unit-suffixed value
  width, and the right-click calculator popup. Phase 3 must add `BH_REGION_VALUE` to
  `_bh_active_region_t` and test it **before** `BH_REGION_MAIN` in `_bh_get_active_region`, where
  the new `cursor_shift` expression now sits — and must not touch the popup or its input grab.

### 2026-09-21 — Phase 3: Inline value editor via a `GtkPopover`
- Done: `dt_bauhaus_value_parse` / `dt_bauhaus_value_hit` added to the `bauhaus_draw` unit with
  six red-first tests (13/13 green); `BH_REGION_VALUE` added to `_bh_active_region_t` and tested
  before `BH_REGION_MAIN`, with the slider's half-marker cursor shift added back before the hit
  test; one `GtkPopover` + `GtkEntry` on `dt_bauhaus_t` (`value_popover`, `value_entry`,
  `value_editing`, `value_revert`), opened from a value click, committed on the popover's
  `"closed"` signal (Enter and click-out share that single commit site), reverted on Escape
  (caught on the entry so the flag is set before the popover's own binding bubbles);
  `#bauhaus-value-entry` CSS block.
- Learned: (1) the popover must stay **modal** — GTK's input grab is what dismisses it on an
  outside click, and the non-modal idiom it was first written with (copied from `popup.c:115`, a
  menu-button popover that is dismissed by its toggle instead) silently broke "popdown commits"
  *and* let the outside click through to the rail, where the eventual close committed stale entry
  text over the value the user had just dragged. D1 was right and the impl plan's step 5 was the
  wrong part, so no durable-plan text needed amending. (2) Over the value field, non-left buttons
  must fall through to the `BH_REGION_MAIN` branch, or right-click (calculator popup) and
  middle-click (zoom reset) die there. (3) `gtk_entry_select_region` is unavailable under
  `GTK_DISABLE_DEPRECATED`; the tree's idiom is `gtk_editable_select_region`
  (`src/libs/tagging.c:3625`). (4) `ctest -R <name>` alone can report a **false pass**: when ninja
  stops on a compile error it never relinks, so ctest runs the previous executable — always check
  the build's exit code first. (5) Ninja's per-target POST_BUILD copies of the same
  `libansel.dll` into `build/tests/unittests` race under `-j`: a rebuild can fail with
  `Permission denied` on two unrelated test targets and then succeed on an identical re-run.
- Drift: none. One deferred Discovery logged above (the 3 px value-rect / drawn-row offset).
- Watch-next: Phase 3's four acceptance criteria are all MANUAL and remain unchecked (no display
  here) — run them together with Phase 2's five. Phase 4 is self-contained in
  `_refresh_slider_gradients` (`src/iop/colorprimaries.c`) and needs no Phase 3 context.

### 2026-09-21 — Phase 4: Accurate Color primaries brightness ramp
- Done: the brightness branch of `_refresh_slider_gradients` (`src/iop/colorprimaries.c`) now sets
  three neutral stops — saturation 0, brightness 0 / 0.5 / 1 — through the existing
  `_set_slider_stop_from_hsb` + `dt_bauhaus_slider_set_stop` path, so the ramp reads black ->
  grey -> white instead of the node's saturated hue. Ten lines replaced; the hue and saturation
  branches are untouched, verified by `git diff --stat`.
- Learned: with S = 0 that helper is already achromatic (`dt_UCS_HSB_to_XYZ` -> display profile),
  so a neutral ramp needed no new API and no second conversion path. The defect was purely in the
  arguments: `target_hsb[0]/[1]` fed the node's hue and saturation, and the ±0.05 brightness span
  made the ramp nearly flat as well as tinted. Full suite re-run: 9 failures of 28, byte-identical
  to the pre-plan baseline of 9 of 27 (the extra test is Phase 1's `test_bauhaus_draw`).
- Drift: none.
- Watch-next: the middle stop is 0.5 in profile space, which the display conversion does not place
  at perceptual mid-grey; if the ramp reads top-heavy in the staged app, the tunable is the middle
  stop, not the endpoints. All manual checks for Phases 2, 3 and 4 are still outstanding.
