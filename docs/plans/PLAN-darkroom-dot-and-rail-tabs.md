# PLAN: Darkroom enable dot and rail tabs

**Status:** Complete
**Created:** 2026-09-12
**Type:** Single plan

## Context
The darkroom right panel today shows folder-style module-group tabs (bordered, stretched to
fill the header) and a 3x18 px orange bar as each module's enable switch. The developer
mocked a replacement in Open Design: compact rail tabs (borderless, an orange underline
marks the active group) and a ~10 px dot as the enable indicator inside a 26 px hit area.
The reference material lives outside the repo at
`C:\Users\sting\AppData\Roaming\Open Design\namespaces\release-stable-win\data\projects\eaa304a1-874b-4435-8f18-bee8f1c19168\gtk3-sample\`
(`ansel-dot-and-rail-tabs.css`, `paint-module-switch.c`, `modulegroups-tab-labels.c`) and its
`image-3.png` is the target look. This plan lands that look, with three choices the developer
made over the reference (see Design Decisions).

## Background
- The enable switch is a `dtgtk` togglebutton (`src/widgets/togglebutton.c`). Its `draw()`
  reads the CSS colour with `gtk_style_context_get_color()` and sets it as the cairo source
  before calling the paint function, so on/off/hover colour is entirely CSS (`:checked`,
  `:hover`). It subtracts CSS margin, border and padding from the allocation before handing
  `w,h` to the paint function, and reinterprets the child canvas's CSS `margin` as a paint
  offset. That is why the button sits in a `dt_iop_enable_button_box` wrapper: layout nudges
  go on the box, never on the button (`imageop_gui.c:1951-1961`).
- Paint functions draw in a unit square: `PREAMBLE(scaling, line_scaling, x_off, y_off)`
  (`paint.c:62-71`) scales by `min(w,h) * scaling` and centres. Any CSS padding on the button
  shrinks the square silently. A paint function may delegate to another with a shrunk box
  (`paint.c:1255` `eye_toggle` -> `eye` passes flags; `paint.c:1814` `styles` uses
  `PREAMBLE(0.5*1.1, ...)` to draw at half size).
- `togglebutton.c:73-91` sets `CPF_ACTIVE` from `gtk_toggle_button_get_active()` and
  `CPF_PRELIGHT` from the prelight state flag.
- The module-group tabs are a `GtkNotebook` inside the lib widget named `modules-tabs`
  (`modulegroups.c:1023`); CSS reaches it as `#modules-tabs notebook ...`. The colour
  equaliser builds its own notebook named `colorequal-ring-tabs` (`colorequal.c:2365`) and
  reuses the `dt_modulegroups_tab_label` class (`colorequal.c:2377,2418`). Every tab rule in
  `ansel.css:1061-1181` is a comma-joined pair covering both ids.
- GTK3 has no `text-transform`; `letter-spacing` is honoured from GTK 3.22 and the floor is
  3.24 (`src/CMakeLists.txt:352`). The theme uses no `letter-spacing` today.
- `ansel.css` hot-reloads when the app runs with `-d gtk` (`application.c:1980-2035`,
  `GFileMonitor` on the resolved theme path). The resolved path is the user config dir's
  `themes/ansel.css` if present, else the datadir copy (`application.c:2062-2080`).
- Branch state at planning time: the main checkout is on `preferences/sentence-case` with
  unrelated uncommitted preferences edits. Implementation branches from `master`.

## Codebase Map
- Entry points:
  - `src/widgets/paint.c:424-444` `dtgtk_cairo_paint_module_switch()` — pill, widens on
    `CPF_PRELIGHT`; `:446-456` `dtgtk_cairo_paint_module_switch_on()` — dot r=0.09, no
    flags; `:2563-2578` `dtgtk_cairo_paint_lock()` — filled body rect (0.25,0.5,0.5x0.45)
    plus stroked shackle arc, fills the unit square, ignores flags. Declared
    `src/widgets/paint.h:102,104,207`.
  - `src/develop/imageop_gui.c:967-982` `dt_iop_gui_set_enable_button_icon()` routes
    `hide_enable_button` modules (forced on or off) to `_switch_on`, all others to
    `_switch`; `:984-1005` `dt_iop_gui_set_enable_button()` toggles
    `dt_iop_enable_forced_on/off` classes and sensitivity; `:1939-1961` creates the button
    (class `dt_iop_enable_button`) and its `dt_iop_enable_button_box` wrapper; `:1847`
    names the header `module-header`.
  - `src/libs/ioporder.c:676-687` `_ioporder_set_enable_button_icon()` (same routing),
    `:769-801` builds a second enable button with the same class and `module-header` name,
    NO box wrapper.
  - `src/libs/modulegroups.c:1012-1064` `gui_init()`; loop `:1038-1053` builds seven
    labels (`labels[]` :1028, `tooltips[]` :1029-1036, `MOD_TAB_LAST` :82), class
    `dt_modulegroups_tab_label` :1041, `hexpand TRUE` :1044, `"tab-expand" TRUE, "tab-fill"
    TRUE` :1049; `gtk_notebook_set_scrollable(TRUE)` :1056. Nothing reads label text back.
- `data/themes/ansel.css` (the only GTK theme file):
  - `:431-434` `*:disabled, #module-header .dt_iop_enable_button.dt_iop_enable_forced_off
    { opacity: 0.5 }`
  - `:442-458` generic `button, .dt_module_btn, togglebutton, spinbutton { min-height:1em;
    min-width:1em; padding:0.125em }` — the rule the enable button's `padding:0` must beat
    (it does: `#module-header .dt_module_btn.dt_iop_enable_button` is more specific)
  - `:739-774` enable-button block: `_box` margin `0 3px 0 -6px` :744-746, base :748-752,
    `:checked` :761-763, hover/active/selected :765-769, `.dt_iop_enable_forced_on,
    #picker-grey { color: @orange_light }` :771-773
  - `:1061-1181` shared tab block; `#modules-tabs notebook tabs arrow` :1085-1094;
    comma-joined pairs at 1066-1067, 1085-1086, 1096-1097, 1102-1103, 1109-1110,
    1115-1118, 1131-1132, 1136-1137, 1142-1145, 1150-1151, 1160-1163, 1169-1170, 1175-1178
  - `:1727-1730` `.dt_iop_enable_button:disabled, .dt_iop_enable_button.dt_iop_enable_forced_off
    { color: @disabled_fg_color }`
  - colours confirmed present: `@slate_36` :109, `@slate_44` :110, `@grey_55` :85,
    `@grey_60` :86, `@grey_65` :87, `@grey_75` :89, `@grey_90` :92, `@grey_95` :93,
    `@grey_100` :94, `@bg_color` :118, `@recessed_color_bg` :126, `@recessed_color_border`
    :127, `@orange_dark` :146, `@orange_light` :147
- Tests: cmocka, registered in `tests/unittests/CMakeLists.txt` (`LIB_ANSEL_UNIT_TESTS`
  list) via `cmake/modules/AddCMockaTest.cmake`. No existing test touches GTK, paint.c or
  the theme; no CSS validator exists. Offscreen-GTK measurement is a documented technique
  (CLAUDE.md, GTK/UI section) but has no runnable test today.
- Commands: build dir `build/`, Ninja. `ninja ansel` builds only `src/ansel.exe`;
  `src/libs/*.c` (including `modulegroups.c`) are plugin DLLs and need a full `ninja`.
  Launch for verification: `build/src/ansel.exe -d gtk --configdir <throwaway>`.

## Non-Goals
- No change to the colour equaliser's ring tabs: every `#colorequal-ring-tabs` declaration
  is reproduced verbatim when the shared block is split.
- No factoring of the duplicated enable-button code in `imageop_gui.c` and `ioporder.c`
  into a shared helper. Both keep their current structure; `ioporder.c` inherits the new
  look through CSS alone.
- No other theme work: the generic `.dt_module_btn` sizing rule (`ansel.css:442-458`), the
  module frame card, fonts, bauhaus and `user.css` are untouched.
- No new GTK offscreen test harness. The one unit test added probes cairo pixels only.
- No change to which modules hide their enable button or to `dt_iop_gui_set_enable_button()`
  semantics (sensitivity, forced classes).

## Design Decisions
### D1: Tab label case and size
- **Chosen:** Mixed case, current label font size, no `letter-spacing` (matches
  `image-3.png`). The reference's uppercase + 0.78em + 0.09em tracking is Phase 3, run only
  if the developer asks for it after seeing Phase 2 live.
- **Rejected:** Uppercase first — diverges from the mock the developer named as the target,
  and introduces the theme's first `letter-spacing` without a need.
- **Consequences:** Phase 2 does not add `g_utf8_strup()`; the notebook page loop changes
  only `hexpand`/`tab-expand`. Phase 3 is conditional on developer feedback.
### D2: Off-state glyph
- **Chosen:** T2 hollow ring for off, filled disc for on, branched on `CPF_ACTIVE` inside
  `dtgtk_cairo_paint_module_switch()` (the `#if 0` variant in the reference).
- **Rejected:** T1 grey filled dot (what `image-3.png` shows) — the developer chose the ring
  so an inactive module gives back its ink.
- **Consequences:** Colour stays CSS-driven (`@slate_44` off, `@orange_light` on); only the
  shape branches. The pixel test must cover both flag states.
### D3: Locked modules (hidden enable button)
- **Chosen:** `dtgtk_cairo_paint_module_switch_on()` delegates to `dtgtk_cairo_paint_lock()`
  drawn in a shrunk box so the padlock reads at the dot's visual weight. Forced-on keeps
  `@orange_light`; forced-off keeps the existing 0.5 opacity and `@disabled_fg_color` rules.
- **Rejected:** Smaller dot (reference, r=0.13) — the developer wants the state to read as
  "locked", not as a weaker version of the same control.
- **Consequences:** `_switch_on` keeps its signature; every call site is untouched. Scale and
  shackle stroke weight are tuned visually in Phase 1 (see Risk #3).

## Progress
- [x] Phase 1: Enable switch — ring/disc, padlock, 26 px hit area
- [x] Phase 2: Rail tabs — split the shared block, restyle #modules-tabs, left-pack
- [x] Phase 3: Conditional — reference uppercase tracked labels (skipped per D1)
- [x] Final verification

## Phases

### Phase 1: Enable switch — ring/disc, padlock, 26 px hit area
**Risk:** flagged (!#1, !#2, !#3)
**Test-first:** required
**Goal:** Every module header shows a hollow ring (off) or filled disc (on) inside a 26 px
transparent circular hit target, and locked modules show a padlock, with colour still coming
from CSS.
**Assumes:**
- `paint.c` can be compiled into a cmocka test target against cairo + GTK headers (it is a
  plain C file drawing on a `cairo_t`). If linking pulls in more of the tree, compile
  `paint.c` directly into the test executable rather than linking a library. If that is
  not viable either, STOP and ask the developer — do not downgrade this phase to manual
  verification on your own (developer decision, 2026-09-12). ~~Harness assumed viable as-is.~~
  See Reconciliations R1.
**Files:**
- `src/widgets/paint.c` — rewrite `dtgtk_cairo_paint_module_switch()` (T2 branch on
  `CPF_ACTIVE`, prelight branch removed) and `dtgtk_cairo_paint_module_switch_on()`
  (delegate to `dtgtk_cairo_paint_lock()` with a shrunk `x,y,w,h` box).
- `data/themes/ansel.css` — replace the enable-button block `:744-774` with the reference's
  EDIT 1 (`_box` margin `0 0.25em 0 0`; button `min-width/min-height 1.625em`, `padding:0`,
  `border:none`, `border-radius:50%`, `background:transparent`, `color:@slate_44`; hover
  `@grey_55`; `:checked` `@orange_light`; `:checked:hover` `shade(@orange_light,1.12)`;
  forced-on `@orange_light`, keeping `#picker-grey` in that selector group).
- `tests/unittests/test_paint_module_switch.c` (new) + `tests/unittests/CMakeLists.txt` —
  register in `LIB_ANSEL_UNIT_TESTS`.

**Reuse:**
- Delegate to `dtgtk_cairo_paint_lock()` in `src/widgets/paint.c:2563` — do NOT draw a
  second padlock.
- Pattern to mirror: `paint.c:1255` (`eye_toggle` calling `eye` with adjusted flags) for
  delegation; `paint.c:1814` (`PREAMBLE(0.5*1.1, ...)`) for reduced-scale drawing.
- Test shape: mirror an existing file in `tests/unittests/` for cmocka boilerplate and
  registration.

**Contracts:**
- `void dtgtk_cairo_paint_module_switch(cairo_t*, gint x, gint y, gint w, gint h, gint flags, void*)`
  and `..._module_switch_on(...)` — signatures unchanged; `paint.h` untouched; all seven
  call sites (`imageop_gui.c:972,976,980,1939`, `ioporder.c:682,686,769`) untouched.
- CSS names frozen: `#module-header`, `.dt_iop_enable_button`, `.dt_iop_enable_button_box`,
  `.dt_iop_enable_forced_on`, `.dt_iop_enable_forced_off`, `.dt_module_btn`.
- `.dt_module_btn.dt_iop_enable_button` keeps `padding:0; border:none` — the dot's 0.19
  radius is computed against the full 26 px content box.

**Out of scope:**
- `imageop_gui.c`, `ioporder.c`, `paint.h`, `togglebutton.c` — no edits.
- The generic sizing rule `ansel.css:442-458`, the `:431-434` opacity rule and the
  `:1727-1730` disabled-colour rule — leave as they are.
- Any other paint function, including `dtgtk_cairo_paint_lock()` itself.
- Tab CSS (Phase 2).

**Tests (write first, confirm red):**
- [ ] `module_switch` with `CPF_ACTIVE` on a 26x26 image surface paints the centre pixel
  (filled disc) and paints a pixel at radius ~0.2 of the box.
- [ ] `module_switch` without `CPF_ACTIVE` leaves the centre pixel unpainted and paints a
  pixel on the ring at radius ~0.18 (hollow ring).
- [ ] `module_switch` with `CPF_PRELIGHT` and without `CPF_ACTIVE` paints the same pixels as
  without `CPF_PRELIGHT` (the widen-on-hover branch is gone).
- [ ] `module_switch_on` paints a pixel inside the padlock body (lower-centre of the shrunk
  box) and leaves the centre of the shackle opening (upper-centre) unpainted; the painted
  bounding box is strictly smaller than the 26 px box on every side.

**Steps:**
1. Write the tests above; build with `ninja`; run them; confirm they FAIL (red).
2. Rewrite `dtgtk_cairo_paint_module_switch()`: `PREAMBLE(1,1,0,0)`; if `CPF_ACTIVE` fill
   `arc(0.5,0.5,0.21)`, else `line_width 0.058`, stroke `arc(0.5,0.5,0.182)`; `FINISH`.
3. Rewrite `dtgtk_cairo_paint_module_switch_on()` to call `dtgtk_cairo_paint_lock()` with a
   centred box of ~0.55 x `w,h` (start there; tune per Risk #3).
4. Replace `ansel.css:744-774` with the reference EDIT 1 block; keep `#picker-grey` in the
   forced-on selector group.
5. Run the tests; confirm they PASS (green).
6. Full `ninja`; launch `-d gtk` with a throwaway `--configdir`; check a darkroom header, a
   locked module (e.g. one with `hide_enable_button`), and the module-order window from
   `ioporder.c`.

**Acceptance criteria:**
- [ ] In the running darkroom, hovering the dot area up to ~13 px from the glyph centre
  toggles the hover colour and a click there toggles the module (26 px target, not 10 px).
- [ ] Locked modules show a padlock; forced-off ones at 0.5 opacity in `@disabled_fg_color`;
  forced-on ones in `@orange_light`.
- [ ] The module-order window's enable buttons show the same ring/disc/padlock without any
  `ioporder.c` change.
- [x] `git diff data/themes/ansel.css` touches only lines inside the former `:739-774`
  block.

### Phase 2: Rail tabs — split the shared block, restyle #modules-tabs, left-pack
**Risk:** flagged (!#4, !#5)
**Test-first:** N/A — pure CSS plus two GTK child properties; no runnable GTK test harness
exists and asserting a child property equals what was just set is tautological.
**Goal:** The module-group tabs render as borderless mixed-case labels, left-packed, with an
inset orange underline under the active one and a grey underline on hover, while the colour
equaliser's ring tabs render exactly as before.
**Assumes:**
- Phase 1 merged (shares `ansel.css`; avoids a second conflicting edit).
**Files:**
- `data/themes/ansel.css` — replace `:1061-1181` with (a) the `#colorequal-ring-tabs`
  half of every comma-joined pair, declarations verbatim, then (b) the reference EDIT 2
  `#modules-tabs` rules with these deltas per D1: keep the file's current tab-label
  `font-size` (do not adopt 0.78em), omit `letter-spacing`, keep label text case as
  delivered by `_()`; then (c) reference EDIT 3 for `#modules-tabs notebook tabs arrow`.
- `src/libs/modulegroups.c:1038-1053` — `gtk_widget_set_hexpand(label, FALSE)` and
  `"tab-expand", FALSE` (keep `"tab-fill", TRUE`). No `g_utf8_strup()`.

**Reuse:**
- Extend the existing rules under `#modules-tabs notebook ...` — do NOT introduce a new
  widget name or class; `dt_modulegroups_tab_label` stays as the label class on both
  notebooks.
- Pattern to mirror: the reference file's EDIT 2 layout (colorequal half first, then the
  new half) so a reviewer can diff the colorequal half against the old file.

**Contracts:**
- Selector scoping: every new rule is prefixed `#modules-tabs`; no rule targets
  `.dt_modulegroups_tab_label` without that prefix (the class is shared with colorequal).
- Active indicator is `box-shadow: inset 0 -2px 0 <colour>` on the `tab` node with
  `margin-bottom: 0` — Phase 3 must not reintroduce borders or the -1px overlap.
- Widget name `modules-tabs` and label class `dt_modulegroups_tab_label` unchanged.

**Out of scope:**
- `src/iop/colorequal.c` and any `#colorequal-ring-tabs` declaration value.
- Other notebooks in the theme (blend channel tabs, preferences tabs).
- Label text content, tooltips, `labels[]`/`tooltips[]`, `MOD_TAB_LAST`, presets.
- Uppercase/tracking (Phase 3).

**Manual verification:**
- [x] Extract colorequal declarations before and after and diff:
  `git show master:data/themes/ansel.css | grep -A6 'colorequal-ring-tabs'` vs the same
  on the working tree — expected: identical declaration values for every
  `#colorequal-ring-tabs` selector.
- [ ] `ninja` (full), launch with `-d gtk` and a throwaway `--configdir`, open the
  darkroom: tabs are left-packed, no borders, orange 2 px underline under the current
  group, grey underline on hover, chevrons visible when the panel is narrower than the
  seven labels. Compare against `image-3.png`.
- [ ] Open the colour equaliser: its ring tabs look exactly as on `master`.
- [x] GTK prints no CSS parser warnings on stderr at startup or on hot-reload.

**Steps:**
1. Split `ansel.css:1061-1181` into the colorequal half (verbatim) and the `#modules-tabs`
   half; apply EDIT 2 with the D1 deltas and EDIT 3.
2. Flip `hexpand` and `tab-expand` to FALSE in `modulegroups.c`.
3. Full `ninja`; run the manual verification above.

**Acceptance criteria:**
- [x] The colorequal diff check above shows no changed declaration.
- [x] `git diff src/libs/modulegroups.c` is exactly two changed lines.

### Phase 3: Conditional — reference uppercase tracked labels
**Risk:** flagged (!#6)
**Test-first:** N/A — CSS and one string transform; visual outcome only.
**Goal:** If, after seeing Phase 2 live, the developer asks for the reference look, the tab
labels become uppercase at 0.78em with 0.09em tracking; otherwise this phase is skipped.
**Assumes:**
- The developer has reviewed Phase 2 in the running app and explicitly asked for the
  reference look. If not, mark this phase skipped and proceed to Final verification.
**Files:**
- `src/libs/modulegroups.c:1038-1053` — `gchar *caps = g_utf8_strup(labels[i], -1)` for the
  label text, `g_free(caps)` after `gtk_label_new()`; tooltip keeps the untransformed string.
- `data/themes/ansel.css` — on the `#modules-tabs notebook tab label` rule add
  `font-size: 0.78em; letter-spacing: 0.09em; padding: 0.125em 0.75em 0.625em;` and
  `font-weight: 500` (600 on `:checked`).

**Reuse:**
- Extend the Phase 2 `#modules-tabs` rules in place — no new selectors.
- Pattern to mirror: the reference `modulegroups-tab-labels.c` loop body.

**Out of scope:**
- The Pango `pango_attr_letter_spacing_new()` fallback from the reference — only if
  `letter-spacing` visibly fails to render, and then as a separate decision with the
  developer.
- Any colorequal rule.

**Manual verification:**
- [ ] Launch with `-d gtk`; tabs read PIPELINE / BASIC / ... with visible tracking; tooltips
  still show the mixed-case translated name.
- [ ] The colorequal diff check from Phase 2 still shows no change.

**Steps:**
1. Apply the two edits above.
2. Full `ninja`; run the manual verification.

**Acceptance criteria:**
- [ ] Developer confirms the look in the running app, or the phase is recorded as skipped in
  the Phase Handoff Log.

## Verification
- [x] Branch from `master` (the main checkout is on `preferences/sentence-case`; do not build
  on its uncommitted edits).
- [x] `ninja` (full, from `build/`) — every target, since `modulegroups.c` is a plugin DLL.
- [x] `ctest` from `build/` (confirm the exact test target name against
  `tests/unittests/CMakeLists.txt`) — the new `test_paint_module_switch` passes.
- [ ] `build/src/ansel.exe -d gtk --configdir <throwaway>`: no CSS parser warnings on stderr;
  darkroom headers, locked modules, module-order window and colour equaliser as described in
  each phase's criteria.
- [x] `python3 tools/pragma_once_to_guards.py --verify` and `tools/check_unused_includes.sh`
  if any include line was added (the test file includes `paint.h`).
- [ ] Quality gates the project defines beyond these: none for CSS. A CSS typo surfaces only
  as a GTK runtime warning, which is why the `-d gtk` stderr check is mandatory.

## Notes
- The label starts ~14 px further right than today (button 12->26 px, the -6 px pull gone).
  If a module name the developer cares about ellipsizes, pull it back with
  `margin-left: -0.25em` on `.dt_iop_enable_button_box` — never on the button.
- `hexpand FALSE` + `tab-expand FALSE` makes the seven labels overflow a narrow panel; that
  is intended, the notebook is already scrollable and EDIT 3 resizes its chevrons.
- With `-d gtk` the monitor watches the resolved theme path: if a copy exists under the user
  config dir's `themes/`, edits to `data/themes/ansel.css` will not reload. Use a throwaway
  `--configdir` so the datadir copy resolves.

## Risks
#1. **paint.c may not compile standalone into a cmocka target** — it is the first test to
    touch `src/widgets/`. If its includes drag in `darktable.h` or the widget libs, compile
    `paint.c` directly into the test executable with only cairo/GTK link flags. If that is
    still not viable within the phase, stop and ask; do not weaken the test to a no-op.
#2. **The ring's 1.5 px stroke is thin at 26 px and thinner at low DPI** — line width 0.058
    in unit space is 1.5 px at the 16 px reference font. Confirm in the running app at 100%
    scale; if it reads as noise, raise it to 0.077 (2 px) and move the ring radius in to keep
    the outer edge at 0.211.
#3. **Padlock scale and shackle weight need visual tuning** — `dtgtk_cairo_paint_lock()` fills
    its unit square and strokes the shackle at PREAMBLE's default line width, which at a
    ~14 px box may be under 1 px. Start at 0.55 of the button box; if the shackle vanishes,
    pass a larger box or accept a slightly bolder padlock. The pixel test asserts topology
    (body painted, opening clear, bounds inside the box), not exact size, so tuning does not
    churn the test.
#4. **The colorequal split is where a wholesale paste breaks something invisible** — every
    tab selector is a comma-joined pair, and the `dt_modulegroups_tab_label` class is shared.
    The manual diff of `#colorequal-ring-tabs` declarations is the gate; a new rule that
    forgets the `#modules-tabs` prefix restyles the equaliser silently.
#5. **`box-shadow` on a GtkNotebook `tab` node** — supported by GTK 3.24's CSS engine, but
    this theme has never used it on tabs. If the underline does not render, the fallback is
    `border-bottom: 2px solid` with `margin-bottom: 0` and no container border-bottom (keep
    the geometry constant across states, which is the whole point of the inset shadow).
#6. **Uppercasing is Unicode-aware but not universally pretty** — `g_utf8_strup()` is
    locale-independent, so no dotless-i surprise, but some translations read badly in caps.
    The tooltip keeps the original string. Phase 3 only runs on explicit developer request.

## Reconciliations
<!-- Drift amendments written by /implement during execution. Append-only. Outdated phase
text above is struck through (~~...~~) but preserved; entries here are the authoritative
correction. Empty at plan creation. -->

### R1 — 2026-09-12 — Phase 1: the cmocka harness does not link on Windows
- **Broke:** the Assumes precondition that `paint.c` can be tested through the existing
  harness. `ansel_deps` links every executable with `-municode`, so a test binary whose only
  entry is `main()` fails with `undefined reference to wWinMain` — `test_stroke_raster`
  (pre-existing, never built on Windows) fails identically. `CMakeLists.txt:751-753` records
  this as deferred work ("each test binary needs the wWinMain wrapper wired up first").
- **Amendment:** wire the wrapper the tree already uses for every app: a new
  `tests/unittests/win_main.c` that includes `src/win/main_wrapper.h` (defines `wmain()`
  forwarding UTF-8 argv to `main()`), added to the SOURCES of every cmocka test target on
  WIN32 (`tests/unittests/CMakeLists.txt` foreach loop, `test_sample`, and
  `tests/unittests/iop/test_filmicrgb`). Test files themselves are untouched. The build tree is
  reconfigured with `-DBUILD_TESTING=ON`.
- **Also corrected:** T1/T2 probe pixel (18,13) has ~0.43 disc coverage (alpha ~108), so the
  planned `> 128` assertion could never go green; the probe moves to (17,13), which lies wholly
  inside both the disc and the ring band, asserted `> 200`.
- **Approval:** developer pre-authorised ("do what you must to finish the implementation
  session", 2026-09-12) under session-wide auto-approval.

## Discoveries
<!-- Non-contradictory findings logged by /implement during execution (act / defer / drop).
Append-only, empty at plan creation. -->

### 2026-09-12 — Phase 1
- **act:** `ansel.css:754-759` (`.dt-collapse-arrow` sizing rule) sat inside the replace range
  but is not an enable-button rule — kept verbatim in place.
- **act:** the reviewer found the new `#module-header` base colour (`@slate_44`) outranks the
  generic `.dt_iop_enable_button:disabled/.dt_iop_enable_forced_off` rule at `:1755` (it used to
  be invisible because the old base was `@grey_65 == @disabled_fg_color`). A `#module-header`-
  scoped forced-off rule was added so AC #2 holds; `:1755` untouched. It sits ABOVE the
  forced-on rule: forced-on buttons are insensitive too (`:disabled` matches), and equal
  specificity means the later rule wins.
- **act:** `dtgtk_cairo_paint_module_switch_on()` now rounds the shrunk-box origin (`+ 0.5`) so
  the padlock is centred; truncation put it one pixel up-left.
- **defer:** `_pixel()`/`_alpha()` in `test_paint_module_switch.c` duplicate
  `test_stroke_raster.c:36-47` — move to a shared test header on the third copy.
- **defer:** `tools/check_unused_includes.sh` cannot run here (`clang-tidy not found`); the
  added includes are the test's cairo/cmocka set and `win/main_wrapper.h`, all used.
- **defer (Phase 2):** `dt_modulegroups_tab_label` has a third consumer,
  `src/iop/colorprimaries.c:1276,1312`, on an unnamed notebook — untouched here because every
  rule is id-prefixed, but a future bare-class rule would hit three modules.
- **act (Phase 2):** `#modules-tabs notebook tab:hover { background-color: transparent }` is
  load-bearing — it overrides the global `notebook tab:hover { background-color:
  @button_hover_bg }` further down the file. Do not remove it as boilerplate.
- **act (Phase 2):** one reviewer simplification applied — a duplicated comment sentence above
  the `tab label` rule was trimmed.
- **watch:** measured padlock shackle alpha 62 at the apex vs 255 body — the pen is scaled by
  `_lock`'s internal `cairo_scale(.2,.4)` regardless of box size. Visual call for the developer;
  a bolder shackle means editing `dtgtk_cairo_paint_lock()` (out of scope).

## Phase Handoff Log
<!-- Written by /implement at each 3G phase gate (Done / Learned / Drift / Watch-next per
phase). Append-only, empty at plan creation. MUST remain the LAST section of this file:
/implement's Step 2 reads the plan up to this heading plus only the log's final entry, so
never add a section below it. -->

### 2026-09-12 — Phase 1: Enable switch — ring/disc, padlock, 26 px hit area
- Done: ring/disc `module_switch`, padlock delegation in `module_switch_on`, EDIT-1 CSS block
  plus a scoped forced-off rule; cmocka test `test_paint_module_switch` (4 cases, red then
  green); Windows unit-test harness wired (`tests/unittests/win_main.c`, R1). Full `ninja`,
  `ctest`, `ninja install` and a `-d gtk` launch are clean.
- Learned: the build tree now has `BUILD_TESTING=ON`; tests run with
  `PATH=/c/msys64/mingw64/bin:$PATH ctest -R <name>` from `build/`. Tests that already
  include `win/main_wrapper.h` themselves (`test_sample`, `test_filmicrgb`, `variables`,
  `styles`) must NOT get `win_main.c` too.
- Drift: R1 (harness) — see `## Reconciliations`.
- Watch-next: AC #1-#3 (26 px hover target, padlock look incl. shackle weight, module-order
  window) are visual and unverified — check them in the running app before or with Phase 2's
  own visual pass. Phase 2 edits `ansel.css:1061-1181` — the block now starts ~16 lines later
  because Phase 1's block grew; locate by selector, not by line.

### 2026-09-12 — Phase 2: Rail tabs — split the shared block, restyle #modules-tabs, left-pack
- Done: shared tab block split; 19 `#colorequal-ring-tabs` rules reproduced with identical
  declaration bodies (checked by script against `HEAD`); `#modules-tabs` rail rules per
  reference EDIT 2 with D1 deltas (1.05em/500, 700 checked, no letter-spacing) and EDIT 3
  chevrons; `modulegroups.c` hexpand/tab-expand FALSE (2 lines). Full `ninja`, `ninja install`
  and a `-d gtk` launch with an image are clean (no CSS warnings). Reviewer: clean.
- Learned: `ninja install` regenerates man pages after every commit and needs msys2's perl
  (`PATH=/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH`); Git Bash's perl lacks Pod::Man. The
  desktop session cannot screenshot the app (capture is black), so visual acceptance is manual.
- Drift: none.
- Watch-next: Risk #5 (does GTK 3.24 render the inset `box-shadow` rail on a notebook `tab`?)
  and the left-packed look vs `image-3.png` are unverified visually. Phase 3 is skipped per D1
  unless the developer asks for the uppercase tracked look after seeing this live.

### 2026-09-12 — Phase 3: Conditional — reference uppercase tracked labels
- Done: SKIPPED. D1 makes it conditional on the developer asking for the reference look after
  seeing Phase 2 live; the session ran under a standing "go straight to the end" instruction,
  so no such request exists. Re-open with `/implement` if wanted: the phase text stands.
- Learned: nothing new.
- Drift: none.
- Watch-next: —

### 2026-09-12 — Final verification
- Done: full `ninja` clean; `ctest -R 'test_paint_module_switch|test_stroke_raster'` pass;
  `pragma_once_to_guards.py --verify` and `include_graph.py --summary` (cycles 0) pass;
  `-d gtk` launches (lighttable and with an image) print no CSS warnings.
- Learned: the full `ctest` run shows 9 pre-existing Windows failures unrelated to this branch
  — `masks_geometry` (0xc0000135: libansel.dll not copied next to `tests/` binaries),
  `test_image_repository` / `test_removed_image_repository` (fail on Windows paths/DB), and the
  LensSerious `knots/vendor/parity/db_*` cases (Not Run). These never ran on Windows before R1
  made the harness link; they are outside this plan's scope.
- Drift: none.
- Watch-next: developer visual pass on AC #1-#3 (Phase 1) and Risk #5 (Phase 2 rail) in the
  running app; `check_unused_includes.sh` needs clang-tidy, which this machine lacks.

