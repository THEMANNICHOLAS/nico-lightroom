# PLAN: Darkroom before/after toggle

**Status:** In Progress
**Created:** 2026-09-13
**Type:** Single plan

## Context
The darkroom has no one-gesture way to see the unedited image against the current edit. The
Snapshots panel can compare, but only as a draggable split line, only for snapshots the user
took by hand, and with no keyboard shortcut. This plan adds a Lightroom-style before/after
toggle: a toolbox button and a default backslash shortcut that flip the whole centre view
between the edited image and a "before" rendered at the same framing with every non-geometry
edit off, without touching history, the database or the undo stack.

## Background
- A snapshot in Ansel is not a bitmap. `dt_dev_snapshot_capture()` (`src/develop/dev_snapshot.c`)
  builds a frozen `dt_develop_t` with its own two pipes (a main tier recomputed on a background
  job when the viewport ROI changes, and a fit-scale preview tier drawn inline as fallback) and
  `dt_dev_snapshot_draw()` paints it through the same `dt_dev_render_locked_surface()` contract
  the live darkroom uses. Its pixels live in the global hash-keyed, LRU pixelpipe cache, so once
  both states have rendered, flipping between them is a repaint, not a recompute.
- **Capture is synchronous and blocking on the calling thread**: `dt_dev_load_image()`
  (`dev_snapshot.c:357`), a `DT_MIPMAP_BLOCKING` mipmap get (`:418`), then a full inline main
  and preview sync (`:509-511`). `snapshots.c:170` wraps it in
  `dt_control_change_cursor_by_name_and_flush()`, which flushes the main loop. It therefore must
  never run inside a draw handler; the take-snapshot button runs it from a `clicked` handler.
- `dt_dev_snapshot_draw()` paints background over the WHOLE clip and then the preview tier while
  the main tier is not ready (`:567-569`); when ready, `dt_dev_render_locked_surface()` fills the
  bands around the image inside the clip (`views/dev_backbuf.c:246-252`). Nothing under the clip
  shows through.
- `dt_dev_snapshot_capture()` IGNORES `history_end_override` when `history_override` is NULL
  (`:365`) and then renders the image's on-disk history. An "original" therefore needs a
  non-NULL duplicated live history with `history_end_override = 0`; that path resets every piece
  to `default_enabled`/`default_params` (`dev_pixelpipe.c:1313-1321`). Passing `-1` means
  "on-disk history", which `libs/duplicate.c:158` uses on purpose.
- **Geometry**: `_compute_main_roi()` scales by the FROZEN pipe's `processed_width/height`
  (`dev_snapshot.c:160-161`), `dt_dev_render_locked_surface()` centres on the locked surface's
  own size (`dev_backbuf.c:238-239`), and `dt_dev_get_image_box_in_widget()` uses the LIVE
  dev's preview dims (`develop.c:1996-1997`). A frozen dev without the live `crop`/`flip`/
  `ashift` therefore renders a shifted window of the uncropped scene into the live image box.
  The developer chose "same framing, edits off": the before keeps every live history item whose
  ~~module carries a `distort_transform` hook (`develop.c:1805`, the codebase's own "this module
  moves pixels" predicate)~~ geometry roster membership marks as geometry (see `## Reconciliations`
  2026-09-13) and drops the rest.
- `src/libs/snapshots.c` owns four fixed user slots (`DT_LIB_SNAPSHOTS_COUNT`), one `selected`
  at a time. `gui_post_expose()` is WHOLLY inside `if(d->selected >= 1 && d->selected <=
  d->size)` (`:235`), and `d->vp_x/vp_y/vp_width/vp_height` are only refreshed there
  (`:241-246`); with nothing selected they hold the `gui_init` defaults `0,0,1,1` (`:493-496`).
  `button_pressed`/`mouse_moved`/`button_released` return `0` for "not handled" and already do
  so when `d->selected == 0` (`:344, :369, :423`).
- `_lib_snapshot_capture_state(dt_lib_snapshot_t*, dt_develop_t*)` (`snapshots.c:137`)
  duplicates `dev->history` and `iop_order_list` and reads `history_end` under `history_mutex`
  (`:161-165`), then captures (`:171`). Sole caller `:579`. Slot label "original" at `:619`.
- `src/libs/duplicate.c:401-409` ALREADY draws a snapshot full-frame: `dt_dev_get_image_box_in_widget()`
  then `dt_dev_snapshot_draw()` over the whole box, no divider. It clears its snapshot in
  `view_leave()` (`:405-411`) and connects `DT_SIGNAL_DEVELOP_IMAGE_CHANGED` at `:377` with the
  matching `DT_DEBUG_CONTROL_SIGNAL_DISCONNECT(callback, data)` at `:390` (the disconnect macro
  does not name the signal).
- Lib hooks run from `dt_view_manager_expose()` and siblings (`src/views/view.c:479-622`) gated
  ONLY on `dt_lib_is_visible_in_view()`. A collapsed expander or hidden panel does not stop them.
- `DT_SIGNAL_DEVELOP_IMAGE_CHANGED` is asynchronous (`control/signal.c:194-195`), so handlers
  run on the GUI thread; it is raised from `_darkroom_image_loaded_callback()` (`darkroom.c:1291`)
  on darkroom entry and on every filmstrip switch.
- The icon row at the bottom of the left panel is the module toolbox: a `GtkFlowBox` in
  `src/libs/tools/module_toolbox.c` (container `DT_UI_CONTAINER_PANEL_LEFT_BOTTOM`), filled via
  `dt_view_manager_module_toolbox_add(vm, widget, dt_view_type_flags_t views)` (`view.c:1329`).
  `_lib_module_toolbox_add()` `gtk_container_add`s (owns) the widget and `show_all`s it
  (`module_toolbox.c:172-173`), so a late add is shown. `src/libs/shape_manager.c:3195-3205,
  3279` is the lib precedent: build in `gui_init`, `g_idle_add` a poll until
  `proxy.module_toolbox.module` is set, then add with `DT_VIEW_DARKROOM`.
- `dev_toolbox.c`'s accelerators call `gtk_button_clicked()` on the button
  (`dt_dev_toolbox_activate_accel`, `:148-156`). CLAUDE.md's "A window a toggle button opens
  has exactly one state: the button's" applies verbatim: the toggle button's `active` flag is
  the mode; nothing else stores it.
- Shortcuts: `dt_accels_new_action_shortcut()` (`accelerators.h:320-326`, 11 arguments:
  accels, callback, data, target_widget, accel_group, action_scope, action_name, key_val,
  accel_mods, lock, description). `src/libs/tagging.c:3432-3435` registers from a lib with
  `NULL` target widget and `N_()` scope/name. Accel paths are `<Ansel>/<scope>/<feature>` split
  on `/` (`accelerators.c:1023-1029, :1538`); `lib.c:707` asserts no slash in a name. Key
  release fires nothing (`accelerators.c:1287-1293`): the gesture is a toggle. `GDK_KEY_backslash`
  is bound nowhere in `src/` or `data/`.

## Codebase Map
- Entry points: `src/libs/snapshots.c` — the lib being extended (`gui_init`, `gui_cleanup`,
  `gui_reset`, `view_leave` (add or extend), `gui_post_expose`, `button_pressed`, `mouse_moved`,
  `button_released`, `_lib_snapshot_capture_state`).
- Snapshot engine API: `src/develop/dev_snapshot.h` — `dt_dev_snapshot_capture(snap, dev, imgid,
  history_override, iop_order_override, history_end_override)`, `dt_dev_snapshot_clear(snap)`,
  `dt_dev_snapshot_is_valid(snap)`, `dt_dev_snapshot_draw(snap, cri, dev, width, height, clip_x,
  clip_y, clip_w, clip_h)`.
- Full-frame draw pattern: `src/libs/duplicate.c:401-409`. Lifecycle pattern: `duplicate.c:377,
  :390` (signal pair), `:405-411` (`view_leave`).
- Geometry predicate: `module->distort_transform != NULL` (`src/develop/develop.c:1805`);
  history items reach their module through `hist->module` (`dt_dev_history_item_t`,
  `src/develop/dev_history.h`, refcounted; release through that header's item API).
- Toolbox: `src/libs/tools/module_toolbox.c`, `dt_view_manager_module_toolbox_add()` in
  `src/views/view.c:1329`. Lib precedent `src/libs/shape_manager.c:3195-3205,3279`. Button
  precedent `src/views/dev_toolbox.c:582-589` (`dtgtk_togglebutton_new(paint, paintflag,
  paintdata)`, `togglebutton.h:61`).
- Shortcuts: `src/widgets/accelerators.h:320-326` (`dt_accels_new_action_shortcut`), `:447`
  (`dt_accels_new_darkroom_action` shorthand), group `dt_gui_get_accels()->darkroom_accels`
  (`:142`); lib example `src/libs/tagging.c:3432-3435`; scope value as `dev_toolbox.c:186`.
- Redraw: `dt_control_queue_redraw_center()`.
- Tests: cmocka, `tests/unittests/` registered in `tests/unittests/CMakeLists.txt`
  (`LIB_ANSEL_UNIT_TESTS`); fixtures hand-build `dt_develop_t`, none loads a GTK lib plugin.
  Run with `ctest` from `build/`.
- Commands: `./rebuild.sh` from the MSYS2 MINGW64 shell (incremental `cmake --build build` plus
  `cmake --install` into `build/stage`). Launch: `build/stage/bin/ansel.exe -d gtk --configdir
  <throwaway>`. `src/libs/*.c` are plugin DLLs — a partial `ansel` target does not rebuild them.
  Static gates CI runs: `python3 tools/pragma_once_to_guards.py --verify`,
  `python3 tools/include_graph.py --summary` (must report `cycles 0`),
  `tools/check_unused_includes.sh`, `tools/check_module_boundaries.sh`, `tools/check_layering.sh`.
- Conventions (AGENTS.md): no single-use helper functions that only hide a selection or an early
  return; never wrap an API call in a function containing nothing else; Doxygen comment on every
  new function.

## Non-Goals
- No press-and-hold (key down = before, key up = after). The dispatcher has no key-release path
  and the developer chose toggle semantics.
- No "before = state at darkroom entry" and no "mark current as before".
- No change to the four user snapshot slots, their split-line behaviour, or their persistence
  across image changes. The new handlers touch only the hidden before.
- ~~No changes to `src/develop/dev_snapshot.c`, `src/views/darkroom.c`, `src/views/dev_toolbox.c`
  or `dt_develop_t`.~~ **Amended 2026-09-17:** `src/develop/dev_snapshot.c` is IN scope for the
  ROI-latch fix alone (see `## Reconciliations`); no change to `darkroom.c`, `dev_toolbox.c` or
  `dt_develop_t`. The `iop_order_override` leak on `dev_snapshot.c`'s NULL-override success
  path (`:521`) is an adjacent bug to report, not to fix here.
- No conf key persisting the mode across sessions; like the other toolbox toggles it starts off.
- No asynchronous capture. The first toggle on an image blocks like the take-snapshot button.

## Design Decisions
### D1: Where the before/after view is rendered
- **Chosen:** Inside `src/libs/snapshots.c`. A hidden `dt_lib_snapshot_t before` handle owned by
  the lib is captured in the toggle's `toggled` handler; when the toggle is on, `gui_post_expose()`
  computes the image box itself and draws the source snapshot over the whole box the way
  `duplicate.c:401-409` does, and the split-line input handlers return `0` first thing. Source =
  the selected user snapshot if any, else the hidden before.
- **Rejected:** A retained "before" surface plus a branch in `views/darkroom.c`'s compose path —
  same engine reuse but a new `dt_develop_t` field and a compose-key change for no gain.
- **Rejected:** Toggling `history_end` through `libs/history.c`'s row-0 path — every flip writes
  the DB, pushes an undo record and rebuilds the pipe from scratch.
- **Consequences:** The toggle is a VIEW: history, DB, undo and the live pipe are never touched;
  the live edit keeps rendering underneath (accepted). The lib gains its first signal connection
  and a `view_leave`.

### D2: Where the mode lives
- **Chosen:** The toolbox `GtkToggleButton`'s `active` flag is the only state. The lib holds a
  reference to the button so the pointer outlives the flow box at shutdown. The shortcut calls
  `gtk_button_clicked()`; image change, `view_leave` and `gui_reset` call
  `gtk_toggle_button_set_active(FALSE)`; every consequence hangs off the one `toggled` handler.
- **Rejected:** A `gboolean before_after` mirrored to the button — two states that drift, the
  failure CLAUDE.md records for the shape manager's button.
- **Consequences:** The button phase precedes the shortcut phase. Nothing flips the mode without
  going through the button.

### D3: What "before" contains
- **Chosen:** The live history filtered to items whose ~~`hist->module->distort_transform != NULL`~~
  ~~`hist->module->geometry_record != NULL`~~ (see `## Reconciliations` 2026-09-13),
  with `history_end` = the filtered length, so framing (crop, flip, perspective, lens) matches
  the edit and everything else renders at defaults. When the filter keeps nothing, the FULL
  duplicated history is passed with `history_end_override = 0`, because the engine ignores the
  end when the list is NULL.
- **Rejected:** History end 0 outright — a cropped image shows a shifted uncropped window
  (Background, Geometry). Kept as Phase 1's tracer only.
- **Rejected:** Fitting the uncropped original into the centre — needs engine changes.
- **Consequences:** Modules that move pixels but read as "edits" to a user (`liquify`, and any
  other iop on the geometry roster) stay ON in the before. Phase 2 records the list.

## Progress
- [x] Phase 1: Full-frame original behind a toolbox toggle (tracer)
- [x] Phase 2: Same-framing before
- [x] Phase 3: Lifecycle — image change, view leave, reset, teardown
- [x] Phase 4: Backslash shortcut
- [ ] Final verification

## Phases

### Phase 1: Full-frame original behind a toolbox toggle (tracer)
**Risk:** flagged (!#2, !#3, !#6)
**Test-first:** N/A — GTK plugin lib with no unit-test harness (cmocka fixtures hand-build
`dt_develop_t`; none loads a lib DLL or paints). Manual verification below is the gate.
**Goal:** Pressing a new toolbox toggle button captures a hidden history-end-0 snapshot and shows
it full-frame in the darkroom centre; pressing it again shows the edit.
**Assumes:**
- `dt_dev_snapshot_capture()` with a non-NULL duplicated live history and `history_end_override
  = 0` renders default-enabled modules at default params (`dev_pixelpipe.c:1313-1321`).
- `dt_dev_snapshot_draw()` with the clip set to the live image box paints the whole box
  (`duplicate.c:401-409` does exactly this).
**Files:**
- `src/libs/snapshots.c` — add `dt_lib_snapshot_t before`, `GtkWidget *before_after_button`,
  `guint before_after_idle` to `dt_lib_snapshots_t`; extend `_lib_snapshot_capture_state()` per
  the contract; add `_before_after_toggled()` and the idle toolbox-add callback; add the
  full-frame branch at the top of `gui_post_expose()`; early-return the three input handlers.
- `src/widgets/paint.c` / `src/widgets/paint.h` — ONLY if no existing `dtgtk_cairo_paint_*` icon
  reads as before/after; otherwise untouched.

**Reuse:**
- Extend `_lib_snapshot_capture_state()` in `src/libs/snapshots.c` — do NOT write a second
  capture path, and never pass `-1` or a NULL list to `dt_dev_snapshot_capture()` expecting an
  original.
- Pattern to mirror (draw): `src/libs/duplicate.c:401-409` — image box from
  `dt_dev_get_image_box_in_widget()`, then `dt_dev_snapshot_draw()` over it. Do NOT reuse
  `d->vp_*`, which are stale when no slot is selected.
- Pattern to mirror (button): `src/views/dev_toolbox.c:582-589` for `dtgtk_togglebutton_new` +
  tooltip; `src/libs/shape_manager.c:3195-3205,3279` for the idle toolbox registration.
- Pattern to mirror (redraw): `dev_toolbox.c`'s ISO 12646 branch — `dt_control_queue_redraw_center()`,
  no pipe resync.

**Contracts:**
- `static int _lib_snapshot_capture_state(dt_lib_snapshot_t *snapshot, dt_develop_t *source, gboolean geometry_only)`
  — `FALSE` is the existing behaviour (full live history, live history end; the `:579` caller
  passes it). `TRUE` builds the before per D3. Everything it reads from `source` — the list, the
  iop order and `history_end` — is read inside the SAME `history_mutex` hold (`:161-165`).
  Phase 1 implements only D3's fallback branch (full list, `history_end_override = 0`); Phase 2
  adds the filter in front of it. The signature does not change again.
- `static void _before_after_toggled(GtkToggleButton *button, dt_lib_module_t *self)` — on
  activate: if `!dt_dev_snapshot_is_valid(&d->before.snap)`, capture with `geometry_only =
  TRUE` (blocking, wait cursor as `:170`); if capture fails, un-press the button and return. On
  either edge, end with `dt_control_queue_redraw_center()`. Phases 3 and 4 reach the mode ONLY
  via `gtk_toggle_button_set_active()` / `gtk_button_clicked()` on `d->before_after_button`.
- Mode predicate everywhere in the lib:
  `gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->before_after_button))`.
- Button lifetime: `g_object_ref_sink()` at creation, `g_object_unref()` in `gui_cleanup()`;
  the pointer is therefore valid in every lib hook. `before_after_idle` holds the `g_idle_add`
  source id, zeroed by the callback when it adds the button; `gui_cleanup()` removes it if still
  non-zero.
- Source selection is INLINE in the expose branch (`d->selected > 0 ? &d->snapshot[d->selected -
  1].snap : &d->before.snap`), not a helper (AGENTS.md).

**Out of scope:**
- The geometry filter (Phase 2), any lifecycle handling (Phase 3), the shortcut (Phase 4).
- Any change to the split-line drawing, drag or rotate maths when the button is off.
- `src/develop/dev_snapshot.c`, `src/views/darkroom.c`, `src/views/dev_toolbox.c`,
  `src/libs/tools/module_toolbox.c`, `src/libs/duplicate.c`.
- Persisting the mode in conf. Asynchronous capture.

**Manual verification:**
- [x] `./rebuild.sh`, launch `build/stage/bin/ansel.exe -d gtk --configdir <throwaway>`, open an
  edited, UNCROPPED image: the new button appears in the bottom-left icon row on a cold start.
- [x] Press it: wait cursor, then the original full-frame with no divider; press again: the edit
  returns. Later presses repaint with no wait cursor.
- [x] With the button on, click-drag on the image: nothing drags or rotates; darkroom pan/zoom
  still works.
- [x] With user slot 1 taken and selected, press the button: slot 1 full-frame; off: the split
  view returns unchanged.
- [x] Collapse the Snapshots expander and hide the left panel with the button on: the original
  still paints.
- [x] On a CROPPED image, press it and record the misalignment (expected at this phase; Phase 2
  removes it).

**Steps:**
1. Add the three fields; extend `_lib_snapshot_capture_state()` per the contract (fallback
   branch only) and update the `:579` caller to pass `FALSE`.
2. Write `_before_after_toggled()` per the contract.
3. In `gui_post_expose()`, BEFORE the `d->selected >= 1` gate: when the button is active, take
   the image box via `dt_dev_get_image_box_in_widget()`, pick the source inline, call
   `dt_dev_snapshot_draw()` over the box, return. Existing code below runs unchanged otherwise.
4. In `button_pressed()`, `mouse_moved()`, `button_released()`: `return 0` first thing when the
   button is active.
5. In `gui_init()`: build the toggle button (tooltip `_("Show the image before your edits")`),
   `g_object_ref_sink`, connect `toggled`, `g_idle_add` the toolbox registration with
   `DT_VIEW_DARKROOM` mirroring `shape_manager.c`, store the source id.
6. `./rebuild.sh`; run the manual verification.

**Acceptance criteria:**
- [x] All manual verification items observed as described.
- [x] `grep -n "dt_dev_snapshot_capture" src/libs/snapshots.c` shows exactly one call site,
  inside `_lib_snapshot_capture_state()`.
- [x] No capture and no `dt_control_change_cursor_by_name_and_flush()` reachable from
  `gui_post_expose()` (read the branch).
- [ ] `tools/check_module_boundaries.sh`, `tools/check_layering.sh` and
  `tools/check_unused_includes.sh` pass with no baseline change.
  - G1: boundaries and layering pass (183 == baseline, cycles 0). `check_unused_includes.sh`
    could NOT run on this machine — `clang-tidy` is not installed. The only added include,
    `widgets/togglebutton.h`, names `dtgtk_togglebutton_new()` used in `gui_init()`.

### Phase 2: Same-framing before
**Risk:** flagged (!#1)
**Test-first:** N/A — the filter walks a live `dt_develop_t`'s history inside a GTK lib; the
existing cmocka fixtures do not load iop modules, so ~~`hist->module->distort_transform` cannot be
populated~~ the predicate cannot be exercised in a unit test (see `## Reconciliations`). Manual
verification below is the gate.
**Goal:** The hidden before keeps the live history's geometry items so a cropped, flipped or
perspective-corrected image compares at identical framing with all other edits off.
**Assumes:**
- Phase 1's `geometry_only` fallback branch is in place and the `:579` caller passes `FALSE`.
- History items reference their module through `hist->module` and are refcounted (CLAUDE.md,
  "History items are refcounted"); the duplicate `_lib_snapshot_capture_state()` makes owns one
  reference per item.
**Files:**
- `src/libs/snapshots.c` — the filter inside `_lib_snapshot_capture_state()`'s `geometry_only`
  branch.

**Reuse:**
- Predicate: ~~`hist->module->distort_transform != NULL`, the same test `dt_dev_distort_transform_locked()`
  makes (`develop.c:1805`)~~ → `hist->module->geometry_record != NULL` (see `## Reconciliations`
  2026-09-13). Do NOT hard-code a module-name list.
- Release dropped items through the item release API in `src/develop/dev_history.h` (the
  counterpart of `dt_dev_history_item_create()`), never `free()`.

**Contracts:**
- Inside the `geometry_only` branch, after duplicating under the lock: remove every item whose
  module has no ~~`distort_transform`~~ `geometry_record`, release it, set `history_end` to the
  remaining length. If nothing remains, restore D3's fallback (full duplicate, `history_end = 0`).
  The iop-order list is passed unchanged. The `FALSE` path is untouched.

**Out of scope:**
- Any UI to choose which modules count as geometry.
- Changing what `distort_transform` means for any iop.
- Filtering user-slot snapshots.

**Manual verification:**
- [x] `grep -ln "geometry_record" src/iop/*.c src/iop/*/*.c` — record the module list in
  this plan's Discoveries; confirm `crop`, `flip`, `ashift`, `lens` are in it.
- [x] Cropped + exposure-edited image: toggle shows the same crop with the exposure edit gone.
- [x] Portrait image with `flip` in history: before has the same orientation as the edit.
- [x] `ashift` perspective-corrected image: identical frame edges before and after.
- [x] Image with NO geometry edits: before is identical to Phase 1's original.
- [x] Toggle twice, then take a user snapshot with the take-snapshot button: that slot still
  captures the full current edit (the `FALSE` path is unchanged).

**Steps:**
1. Implement the filter per the contract.
2. `./rebuild.sh`; run the manual verification; write the module list to Discoveries.

**Acceptance criteria:**
- [x] All manual verification items observed as described.
- [x] Valgrind/ASAN not available on this platform: instead confirm with `-d history` that the
  before capture logs no refcount warning and the application exits cleanly with the toggle
  having been used.

### Phase 3: Lifecycle — image change, view leave, reset, teardown
**Risk:** flagged (!#4)
**Test-first:** N/A — signal-driven GTK lib behaviour with no lib test harness. Manual
verification below is the gate.
**Goal:** Switching images or leaving the darkroom resets the toggle to "after" and drops the
hidden before; the panel reset button and application teardown release it too.
**Assumes:**
- `DT_SIGNAL_DEVELOP_IMAGE_CHANGED` handlers run on the GUI thread (asynchronous signal,
  `control/signal.c:194-195`) and fire on darkroom entry and filmstrip switches (`darkroom.c:1291`).
**Files:**
- `src/libs/snapshots.c` — connect/disconnect the signal; `_before_after_image_changed()`;
  extend `gui_reset()`, `gui_cleanup()`; add or extend `view_leave()`.

**Reuse:**
- Pattern to mirror: `src/libs/duplicate.c:377` (connect) and `:390` (disconnect) — same macros,
  same placement; `:405-411` for `view_leave()` clearing a snapshot.
- Release through `dt_dev_snapshot_clear(&d->before.snap)`, as `gui_reset()` already does per
  slot.

**Contracts:**
- `static void _before_after_image_changed(gpointer instance, dt_lib_module_t *self)` — order:
  `gtk_toggle_button_set_active(button, FALSE)` first (the `toggled` handler runs while the old
  snapshot is still valid), then `dt_dev_snapshot_clear(&d->before.snap)`. Touches no user slot.
- `view_leave()`: same two calls. `gui_reset()`: same two calls in addition to its existing body.
  `gui_cleanup()`: clear `before`, disconnect the signal, remove a pending idle source, unref the
  button.
- `DT_SIGNAL_DEVELOP_INITIALIZE` is NOT connected; `IMAGE_CHANGED` alone covers entry and switch.

**Out of scope:**
- Clearing or invalidating the four user slots on image change or view leave (existing
  behaviour, deliberately unchanged).
- Recapturing the before eagerly on image change (lazy, in `_before_after_toggled()`).
- Any `imgid` staleness guard in the draw path; the signal and `view_leave` are the boundary.

**Manual verification:**
- [x] Toggle before on image A, open image B from the filmstrip: B opens on its edit, button
  un-pressed. Toggle on B: B's before, not A's.
- [x] Toggle on, go to the lighttable and back: button un-pressed; toggle again recaptures.
- [x] Toggle on, press the Snapshots panel reset button: edit shown, button un-pressed.
- [x] Quit with the toggle having been used on two images: clean exit, no warning on stderr from
  the snapshot engine or GLib about a finalized object or a pending source.

**Steps:**
1. Connect the signal in `gui_init()`, disconnect in `gui_cleanup()`, mirroring `duplicate.c`.
2. Write `_before_after_image_changed()`; add or extend `view_leave()`; extend `gui_reset()` and
   `gui_cleanup()` per the contract.
3. `./rebuild.sh`; run the manual verification.

**Acceptance criteria:**
- [x] All manual verification items observed as described.
- [x] `grep -n "DT_SIGNAL_DEVELOP_IMAGE_CHANGED\|_before_after_image_changed" src/libs/snapshots.c`
  shows one connect naming the signal and one `DT_DEBUG_CONTROL_SIGNAL_DISCONNECT` naming the
  callback.

### Phase 4: Backslash shortcut
**Risk:** flagged (!#5)
**Test-first:** N/A — accelerator registration in a GTK lib; no harness. Manual verification is
the gate.
**Goal:** Backslash toggles before/after in the darkroom, rebindable in the shortcuts panel, by
clicking the Phase 1 button.
**Assumes:**
- `dt_accels_new_action_shortcut()` registered from a lib's `gui_init` creates and inserts the
  shortcut the way `tagging.c:3432-3435` relies on (`accelerators.c:883-903`).
**Files:**
- `src/libs/snapshots.c` — `_before_after_accel()` callback; registration in `gui_init()`.

**Reuse:**
- Pattern to mirror: `src/libs/tagging.c:3432-3435` for the call (`NULL` target widget, `N_()`
  scope and name, `_()` description) and `dt_gui_get_accels()->darkroom_accels` as the group;
  scope value as `dev_toolbox.c:186`. `dev_toolbox.c:148-156` for the callback body.
- Do NOT include `views/dev_toolbox.h` from the lib to reuse `dt_dev_toolbox_activate_accel()`:
  no lib includes a view header today and `tools/check_layering.sh` gates it. The local callback
  is the signature adapter the accel API requires, not a wrapper.

**Contracts:**
- Default binding: `GDK_KEY_backslash`, no modifier. Action name `N_("Before and after")` —
  NO slash: the accel path is split on `/` (`accelerators.c:1023-1029, :1538`; `lib.c:707`
  asserts). This label is the persisted accel path; it is chosen once.
- `static void _before_after_accel(...)` (signature per `accelerators.h:320-326`'s callback
  type) does `gtk_button_clicked(GTK_BUTTON(d->before_after_button))` and nothing else.

**Out of scope:**
- Press-and-hold or key-release handling. Shortcuts for the user slots or take-snapshot.
- `src/widgets/accelerators.c`. Protecting darkroom text entries from accels — that is the
  per-widget `dt_accels_disconnect_on_text_input()` opt-in, unchanged by this plan.

**Manual verification:**
- [x] In the darkroom press backslash: same effect as clicking the button; the pressed state
  follows. Press again: back to the edit.
- [ ] Shortcuts panel lists "Before and after" under the darkroom with backslash; rebind it, the
  new key works, and it survives a restart (`keyboardrc` line present).
- [ ] In the lighttable, backslash does nothing.
- [ ] In a darkroom text entry that already calls `dt_accels_disconnect_on_text_input()` (the
  duplicate lib's name entry, `duplicate.c:296`), backslash inserts the character.

**Steps:**
1. Write `_before_after_accel()` per the contract.
2. Register it in `gui_init()` after the button exists.
3. `./rebuild.sh`; run the manual verification.

**Acceptance criteria:**
- [ ] All manual verification items observed as described.
- [x] `grep -n "gtk_toggle_button_set_active\|gtk_button_clicked" src/libs/snapshots.c`: every
  hit targets `d->before_after_button`, and no code path sets the mode's consequences except
  `_before_after_toggled()`.

## Verification
- [x] `./rebuild.sh` (MSYS2 MINGW64 shell) completes with no new warnings in `src/libs/snapshots.c`.
- [ ] Launch `build/stage/bin/ansel.exe -d gtk --configdir <throwaway>`; run every phase's manual
  verification list end to end on one uncropped edited image, one portrait image with `flip`,
  one cropped image, one `ashift`-corrected image.
- [ ] `python3 tools/pragma_once_to_guards.py --verify`; `python3 tools/include_graph.py --summary`
  reports `cycles 0`; `tools/check_unused_includes.sh`; `tools/check_module_boundaries.sh`;
  `tools/check_layering.sh` — all as CI runs them, no baseline lowered or raised.
- [ ] `ctest` from `build/` still passes.
- [x] Every new function in `snapshots.c` carries a Doxygen comment (AGENTS.md).

## Notes
- Lightroom's key is backslash, not slash; the developer's request said "/" from memory.
- The first toggle on an image blocks the GUI thread for a full render behind a wait cursor,
  exactly like pressing take-snapshot. Later toggles are repaints from cached pixels.
- An image with an EMPTY live history: the duplicate is NULL, the engine ignores the end and
  renders on-disk history, which is also empty — before equals after, which is correct. Do not
  add a guard for it.
- Adjacent bug found while reviewing, NOT fixed here: `dev_snapshot.c:521` leaks
  `iop_order_override` on the success path when `history_override` is NULL.
- If a user slot is selected while the toggle is on, the toggle full-frames that slot. Free
  consequence of D1; add no UI for it.

## Risks
#1. **The geometry predicate is the codebase's, not the user's** — ~~`distort_transform` marks
    every module that moves pixels~~ the geometry roster marks every module that moves pixels
    (see `## Reconciliations`), which includes `liquify` and possibly others a user would
    call an edit; those stay on in the before. Phase 2 records the actual module list in
    Discoveries so the developer can judge it; a curated exclusion is a follow-up, not this plan.
#2. **The toolbox button depends on init ordering the lib does not control** — the module
    toolbox proxy may not exist when `snapshots.c`'s `gui_init` runs. The `g_idle_add` poll from
    `shape_manager.c` is the accepted answer, with the source id stored so `gui_cleanup` can
    remove a poll that never succeeded. Confirm the button appears on a cold start.
#3. **The first toggle blocks the GUI thread** — `dt_dev_snapshot_capture()` loads the image,
    blocks on the mipmap and renders both tiers inline, hundreds of ms on a large raw. Running it
    from the `toggled` handler with the wait cursor matches take-snapshot; running it from
    `gui_post_expose()` would re-enter the main loop inside a draw and is forbidden by contract.
#4. **This is the lib's first signal connection and first `view_leave`** — `gui_cleanup` must
    disconnect, remove the idle source and unref the button, or a handler fires on a freed
    `dt_lib_snapshots_t` at shutdown. Mirror `duplicate.c`'s pairs exactly; Phase 3's exit check
    is the gate.
#5. **The accel label is a persistent identifier** — accel paths are built from translated
    labels and saved per language; renaming "Before and after" later orphans every user's saved
    binding (CLAUDE.md, "An accel path absent from the user's config..."). Pick it once, no slash.
#6. **The engine ignores `history_end_override` when the history list is NULL** — a filter that
    yields an empty list and passes it through would silently render the EDIT as the before. D3's
    fallback (full duplicate, end 0) exists for exactly this; Phase 2's "no geometry edits" check
    is the observation that proves it.

## Reconciliations
<!-- Drift amendments written by /implement during execution. Append-only. Outdated phase
text above is struck through (~~...~~) but preserved; entries here are the authoritative
correction. Empty at plan creation. -->

### 2026-09-13 — Phase 2: the geometry predicate does not exist → use `module->geometry_record != NULL`
The planned predicate `hist->module->distort_transform != NULL` cannot select anything:
`distort_transform` is a `DEFAULT()`-bound entry (`src/common/module_api.h:101,109-110`), so every
module gets the non-NULL no-op `default_distort_transform()`; `src/develop/geometry/geometry.c:34-37`
states this outright, and `develop.c:1805` calls it with no NULL test. Reading the plan literally
would keep EVERY history item, leaving the before identical to the edit.

Amendment: inside `_lib_snapshot_capture_state()`'s `geometry_only` branch, keep only items whose
`hist->module->geometry_record != NULL` — the OPTIONAL hook that is NULL for every non-geometry
module (`src/iop/iop_api.h:219-220`; gated exactly this way at `geometry/geometry.c:295`). It is
non-NULL for the 12 modules on the geometry service's roster: rawprepare, basebuffer, demosaic,
lens, ashift, liquify, rotatepixels, scalepixels, flip, clipping, crop, borders. This is the
codebase's only runtime "is a geometry module" marker, so the plan's "no hard-coded module-name
list" rule still holds. Consequence: the input/decode geometry (rawprepare, basebuffer, demosaic)
also stays live in the before — intended, so the before shares the edit's decode rather than
reverting it. All other D3/Phase 2 text stands.

### 2026-09-17 — Crop-edit corruption: the snapshot pipe never latches a ROI request → latch the live one
The Non-Goals excluded `src/develop/dev_snapshot.c`; the Discovery below shows that exclusion was
hiding a real defect, so the plan is amended. `_process_at_roi()` (`dev_snapshot.c:206-213`) re-runs
`pipe` at a ROI derived from the live viewport but never latches a `dt_dev_roi_request_t` onto it,
unlike the darkroom worker, which latches once per iteration (`develop.c:635-637`).
`finalscale.c:182-185` therefore reads the pipe's NEUTRAL request
(`dt_dev_roi_request_neutral()`, `natural_scale = -1`), computes
`darkroom_zoom = request.scaling * request.natural_scale = -1`, and takes its enable/disable
decision from a scale that does not match the ROI being processed -- the diagonal shear when crop
Edit jumps cropped->full-frame.

Amendment: widen scope to `src/develop/dev_snapshot.c` for exactly one fix -- latch the live
`dt_dev_roi_request_get(dev)` onto the pipe inside `_process_at_roi()` BEFORE the
`dt_dev_pixelpipe_or_changed()` / `dt_dev_pixelpipe_change()` pair that re-commits finalscale. The
live request must also reach the recompute job thread, which holds no `dev`: pair it with the
existing `pending_roi` under the same `lock` and pass it explicitly into `_process_at_roi()` at each
call site, mirroring the worker's one-latch-per-iteration rule. Nothing else in `dev_snapshot.c` is
in scope; the `iop_order_override` leak (`:521`) stays a reported non-fix. Caveat carried from the
Discovery: `finalscale` is disabled at fit zoom when `darkroom/render_size == 1` and zoom <= 1, so
the manual check must be run ZOOMED IN, not at fit.

## Discoveries
<!-- Non-contradictory findings logged by /implement during execution (act / defer / drop).
Append-only, empty at plan creation. -->

### 2026-09-13 — Phase 2: geometry roster (act now)
`module->geometry_record` is non-NULL for exactly 12 iop modules (verified by
`rg -ln geometry_record src/iop/` against `geometry/geometry.c:61-64`): rawprepare, basebuffer,
demosaic, lens, ashift, liquify, rotatepixels, scalepixels, flip, clipping, crop, borders
(`drawlayer/coordinates.c` only names the type). Consequence per risk #1: `liquify` (a user edit
that moves pixels) and the framing edits stay ON in the before, by design; a curated exclusion is a
follow-up, not this plan. Phase 2's filter treats this list as the "keep" set.
### 2026-09-13 — Phase 2: two stale references updated inline (act now)
The `_lib_snapshot_capture_state()` doc comment and the in-body `history_end` comment both still
described Phase 1's "full history at end 0" before; updated in the same change so the function
does not document behavior the code no longer has.

### 2026-09-13 — Crop edit mode: frozen before shears at a scale change (defer, needs call)
With the before/after toggle ON, entering crop Edit mode corrupts the whole centre frame with
horizontal scanlines (the live crop edit itself is fine; the toggle-off view is clean). Root cause
per investigation: the snapshot engine renders its frozen pipe at a scale derived from the live
viewport but never latches a ROI request onto it (`_process_at_roi()`, `dev_snapshot.c:206-213`;
the darkroom worker latches at `develop.c:637`), so `finalscale` commits from a neutral request
(`natural_scale=-1`, `finalscale.c:182-185`) and mis-plans when crop edit jumps cropped->full-frame.
The shared filename-keyed pixelpipe cache (`_default_pipe_hash`, `dev_pixelpipe.c:657-661`) may
compound it. Fix candidates: (1) latch the ROI in `_process_at_roi`; (2) salt the cache hash per
pipe. Both are outside this plan's Non-Goals (no `dev_snapshot.c` changes), and candidate 1 may not
fire at fit zoom (finalscale disabled when `darkroom/render_size == 1` and zoom <= 1). Needs the
developer's call: fix here under a plan amendment, or a follow-up plan. Workaround: toggle the
before/after view off before editing crop.

### 2026-09-17 — Crop-edit corruption: act now, under the 2026-09-17 amendment
Developer's call: fix it in this plan rather than defer. Scope and method are fixed by the
`## Reconciliations` entry of the same date, which also lifts the `dev_snapshot.c` Non-Goal for this
fix alone. Once the ROI latch lands, the "toggle the before/after view off before editing crop"
workaround is no longer needed.

### 2026-09-17 — Phase 4: an AltGr-accessed key cannot match a mods-0 binding (defer)
`_accels_keys_decode()` (`widgets/accelerators.c:1031-1090`) masks the key state with
`default_mod_mask` (`:1039`) and applies the keymap's `consumed` modifiers only when the keyval is
CASELESS (`:1071`), while `_for_each_accel()` demands `shortcut->mods == modifier` exactly
(`:1119`). On a layout where `\` is reached through AltGr (French AZERTY: AltGr+8) AltGr arrives as
`GDK_CONTROL_MASK|GDK_MOD1_MASK` -- both inside the default mask -- so a `mods == 0` binding can
never match. This is NOT a Phase 4 defect: on QWERTY `\` is unmodified and the binding is correct
(the 2026-09-17 "shortcut does not fire" report was a `/`-vs-`\` keystroke error, not a bug). It is
logged because nothing in `src/` handles AltGr / level-3 today, and every other
`dt_accels_new_action_shortcut()` caller passes a modifier (tagging uses `DT_PRIMARY_MASK` and
`GDK_MOD1_MASK`), making this shortcut the first mods-0 action shortcut ever registered. Deferred:
the general fix means changing shared accelerator code, outside this plan. Reach for it only when a
mods-0 default must fire on keys an AltGr-using layout reaches through AltGr.

## Phase Handoff Log
<!-- Written by /implement at each 3G phase gate (Done / Learned / Drift / Watch-next per
phase). Append-only, empty at plan creation. MUST remain the LAST section of this file:
/implement's Step 2 reads the plan up to this heading plus only the log's final entry, so
never add a section below it. -->

### 2026-09-13 — Phase 2: Same-framing before
- Done: `_lib_snapshot_capture_state()`'s `geometry_only` branch now keeps only history items
  whose `hist->module->geometry_record != NULL` (falling back to the full duplicate at end 0 when
  none remain). Committed on `darkroom/before-after` (worktree `ansel-ba`), built and staged there.
- Learned: the geometry filter is correct — a `[BA-DBG]`/`[CROP-DBG]` trace showed the filtered
  crop params (`cx=0.066 cy=0.180 cw=0.860 ch=0.798`) identical to the live pipe's, and crop
  framing matched. An earlier "crop offset reset" report was an artifact of the CONTAMINATED main
  tree (a concurrent session's uncommitted 211-file sweep, which also crashes startup in
  `dt_ui_init_global_menu`); the isolated worktree build shows no shift. Do all builds in
  `ansel-ba` until the sweep lands.
- Drift: the Phase 2 predicate reconciliation (2026-09-13); plus a new adjacent finding (below,
  Discoveries) NOT fixed here.
- Watch-next: Phase 3 lifecycle — clear `before` on image change/view leave/reset and disconnect
  the signal, so the stale-before-across-images case cannot reach the draw path.

### 2026-09-13 — Phase 1: Full-frame original behind a toolbox toggle (tracer)
- Done: Added the hidden `before` snapshot, the module-toolbox `GtkToggleButton` (new
  `dtgtk_cairo_paint_before_after` icon), the full-frame draw branch in `gui_post_expose()`, and
  the three input-handler early returns in `src/libs/snapshots.c`. Compiled and linked clean; staged.
- Learned: `check_unused_includes.sh` cannot run on this machine (`clang-tidy` absent); the other
  two static gates pass. Developer confirmed the button/toggle manually and accepted the remaining
  checklist items; the shortcut was correctly absent at this phase.
- Drift: none.
- Watch-next: Phase 2 (`distort_transform` filter) — verify the before keeps crop/flip/ashift
  framing and that an image with no geometry edits still equals Phase 1's original.

### 2026-09-17 — Phase 3: Lifecycle — image change, view leave, reset, teardown
- Done: `DT_SIGNAL_DEVELOP_IMAGE_CHANGED` connect/disconnect, `_before_after_image_changed()`, and
  the `view_leave()` / `gui_reset()` / `gui_cleanup()` release paths in `src/libs/snapshots.c`.
- Learned: the developer confirmed the lifecycle behaviour ("all working") in the worktree build;
  the single-connect / single-disconnect acceptance grep passes.
- Drift: none.
- Watch-next: Phase 4 (backslash) — `dt_accels_get_global()` was used rather than
  `dt_gui_get_accels()` to avoid a lib -> gui include, which would raise the layering ratchet.

### 2026-09-17 — Phase 4: Backslash shortcut
- Done: `_before_after_accel()` plus its `dt_accels_new_action_shortcut()` registration in
  `gui_init()`, in `darkroom_accels` under scope `Darkroom/Toolbox` / name "Before and after",
  default `GDK_KEY_backslash` with no modifier. Developer confirmed the darkroom toggle works.
- Learned: the phase's reported "shortcut does not fire" was NOT a bug — the developer was pressing
  `/` instead of `\`. Registration, wiring (`_insert_accel()` connects immediately), group
  (`darkroom_accels` IS the active group in darkroom) and persistence (`keyboardrc.English` carries
  `<Ansel>/Darkroom/Toolbox/Before and after` = `backslash`) were all correct from the start. A
  separate, real finding was logged instead: a `mods == 0` binding cannot match a key that an AltGr
  layout reaches through AltGr (see `## Discoveries`, 2026-09-17) — deferred, shared-code fix.
- Drift: none.
- Watch-next: this phase's remaining manual items — the shortcuts panel listing/rebinding, the
  lighttable no-op, and the text-entry passthrough — were NOT observed. They are the only unchecked
  manual boxes left in the plan.

### 2026-09-17 — Amendment: the snapshot pipe's missing ROI latch (crop-edit corruption)
- Done: `src/develop/dev_snapshot.c` now latches the live `dt_dev_roi_request_get(dev)` onto the
  pipe inside `_process_at_roi()`, before the `dt_dev_pixelpipe_change()` pass that re-commits
  finalscale, and carries that request to the recompute job thread beside `pending_roi` under the
  same `lock`. Developer verified crop Edit at a zoomed-in scale change: no scanlines, no shear, and
  no regression at fit zoom. The "toggle the before view off before editing crop" workaround is
  retired.
- Learned: the shear was never a crop-params problem — `finalscale` simply never saw a scale,
  because the pipe carried the NEUTRAL request (`natural_scale = -1`). `engine->frozen` is the
  snapshot's OWN `dt_develop_t` (`dt_dev_init()` + `dt_dev_load_image()`), so it can never be the
  source of the live request. At fit zoom with `darkroom/render_size == 1` finalscale is disabled
  either way, which is why this is only observable zoomed in.
- Drift: yes — the Non-Goal excluding `src/develop/dev_snapshot.c` was struck and the amendment
  recorded under `## Reconciliations` (2026-09-17). The developer approved fixing it here rather
  than deferring to a follow-up plan.
- Watch-next: the Phase 4 acceptance grep is checked on INTENT, not literal wording. Every
  before/after hit targets `d->before_after_button`, but that same grep also matches the
  pre-existing user-slot buttons (`d->snapshot[k].button`), which this plan never touches. Left
  unchecked in the plan: the Final verification items for the 4-image-type end-to-end pass, the
  `pragma_once_to_guards.py` / `include_graph.py` / `check_unused_includes.sh` gates, and `ctest`
  (which registers 0 tests in this build, so it proves nothing either way).
