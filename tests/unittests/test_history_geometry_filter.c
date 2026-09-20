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

/* The contract of dt_history_filter_geometry(), the "before" history reducer.
 *
 * The function takes ownership of a duplicated live history and returns a NEW list holding only
 * the items the before keeps: geometry modules (those carrying the OPTIONAL `geometry_record`
 * hook) at an index below the live history end. This file pins the three edges that matter --
 * order/count of the kept set, the live-end bound that drops the undone/redo tail, and the
 * empty-list fallback where the full duplicate is returned at end 0 so the engine still renders
 * it (a NULL history would make it render the on-disk edit instead).
 *
 * Items are hand-built with dt_dev_history_item_create(); dt_dev_free_history_item() does not
 * touch item->module, so the static module stubs below need no teardown. */

#include "develop/dev_history.h"
#include "develop/imageop.h"

#include <glib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>  // NOLINT(misc-include-cleaner)
#include <stdint.h>
#include <stdlib.h>
#include <cmocka.h>

/* Stub for the OPTIONAL `geometry_record` vtable hook. Its body is never run here -- only the
 * pointer's non-NULL-ness selects the item as geometry -- so the parameters are unused. */
static gboolean _geometry_record_stub(struct dt_iop_module_t *self, const void *params,
                                      struct dt_geometry_record_t *record)
{
  return TRUE;
}

static struct dt_iop_module_t _geometry_module = { .geometry_record = _geometry_record_stub };
static struct dt_iop_module_t _non_geometry_module = { 0 };

static dt_dev_history_item_t *_item(struct dt_iop_module_t *module)
{
  dt_dev_history_item_t *item = dt_dev_history_item_create();
  assert_non_null(item);
  item->module = module;
  return item;
}

static void keeps_geometry_items_below_the_end(void **state)
{
  (void)state;
  dt_dev_history_item_t *g1 = _item(&_geometry_module);
  dt_dev_history_item_t *n1 = _item(&_non_geometry_module);
  dt_dev_history_item_t *g2 = _item(&_geometry_module);
  dt_dev_history_item_t *n2 = _item(&_non_geometry_module);

  GList *input = NULL;
  input = g_list_append(input, g1);
  input = g_list_append(input, n1);
  input = g_list_append(input, g2);
  input = g_list_append(input, n2);

  int32_t out_end = -1;
  GList *result = dt_history_filter_geometry(input, 4, &out_end);

  // Both geometry items survive, in the input's order; the two plain modules are discarded.
  assert_int_equal(g_list_length(result), 2);
  assert_ptr_equal(g_list_nth_data(result, 0), g1);
  assert_ptr_equal(g_list_nth_data(result, 1), g2);
  assert_int_equal(out_end, 2);

  g_list_free_full(result, dt_dev_free_history_item);
}

static void drops_geometry_items_past_the_live_end(void **state)
{
  (void)state;
  dt_dev_history_item_t *g1 = _item(&_geometry_module);
  dt_dev_history_item_t *n1 = _item(&_non_geometry_module);
  dt_dev_history_item_t *g2 = _item(&_geometry_module);

  GList *input = NULL;
  input = g_list_append(input, g1);
  input = g_list_append(input, n1);
  input = g_list_append(input, g2);

  // Live end is 1: the geometry item at index 2 is in the undone/redo tail and must not frame
  // the before.
  int32_t out_end = -1;
  GList *result = dt_history_filter_geometry(input, 1, &out_end);

  assert_int_equal(g_list_length(result), 1);
  assert_ptr_equal(g_list_nth_data(result, 0), g1);
  assert_int_equal(out_end, 1);

  g_list_free_full(result, dt_dev_free_history_item);
}

static void returns_the_full_list_at_end_zero_when_nothing_is_geometry(void **state)
{
  (void)state;
  dt_dev_history_item_t *n1 = _item(&_non_geometry_module);
  dt_dev_history_item_t *n2 = _item(&_non_geometry_module);

  GList *input = NULL;
  input = g_list_append(input, n1);
  input = g_list_append(input, n2);

  int32_t out_end = -1;
  GList *result = dt_history_filter_geometry(input, 2, &out_end);

  // The fallback returns the untouched duplicate so the engine renders it at end 0 rather than
  // falling back to the on-disk edit.
  assert_ptr_equal(result, input);
  assert_int_equal(g_list_length(result), 2);
  assert_int_equal(out_end, 0);

  g_list_free_full(result, dt_dev_free_history_item);
}

static void treats_a_item_with_no_module_as_non_geometry(void **state)
{
  (void)state;
  dt_dev_history_item_t *none = _item(NULL);
  dt_dev_history_item_t *g = _item(&_geometry_module);

  GList *input = NULL;
  input = g_list_append(input, none);
  input = g_list_append(input, g);

  int32_t out_end = -1;
  GList *result = dt_history_filter_geometry(input, 2, &out_end);

  assert_int_equal(g_list_length(result), 1);
  assert_ptr_equal(g_list_nth_data(result, 0), g);
  assert_int_equal(out_end, 1);

  g_list_free_full(result, dt_dev_free_history_item);
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  const struct CMUnitTest tests[] = {
    cmocka_unit_test(keeps_geometry_items_below_the_end),
    cmocka_unit_test(drops_geometry_items_past_the_live_end),
    cmocka_unit_test(returns_the_full_list_at_end_zero_when_nothing_is_geometry),
    cmocka_unit_test(treats_a_item_with_no_module_as_non_geometry),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
