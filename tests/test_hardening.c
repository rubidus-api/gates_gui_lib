/* RFC-0009 hardening (0.15.0): extreme numbers and floats handed to the API, an empty text's null
 * pointer, deferred calls that change the list, and the slider's tick loop. Each case failed before
 * its fix; run the suite under UBSan (make test CC=clang CFLAGS="... -fsanitize=undefined") to see
 * the cases that are only undefined behaviour on this machine. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/inputs.h>
#include <gates/editor.h>
#include <gates/overlay.h>
#include <gates/command.h>
#include <gates/frame.h>
#include <gates/access.h>
#include <gates/event.h>
#include <gates/view.h>
#include <gates/encoding.h>
#include <gates/text.h>
#include <gates/text_edit.h>
#include <gates/text_buffer.h>
#include <gates/undo.h>
#include <gates/render.h>
#include <proven/heap.h>
#include "gates_test.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const gates_text_backend_t *be;
static const gates_theme_t *theme;

static gates_tree_t *new_tree(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    return t;
}

static gates_pointer_event_t ptr(gates_pointer_action_t a, gates_i32 x, gates_i32 y) {
    return (gates_pointer_event_t){ .action = a, .type = GATES_POINTER_MOUSE, .pos = { x, y },
                                    .buttons = a == GATES_POINTER_UP ? 0u : GATES_BUTTON_LEFT, .button = GATES_BUTTON_LEFT,
                                    .clicks = 1, .pressure = 1.0f, .primary = true };
}

static gates_pointer_event_t wheel_ev(gates_i32 x, gates_i32 y, float dx, float dy) {
    return (gates_pointer_event_t){ .action = GATES_POINTER_WHEEL, .pos = { x, y }, .wheel = { dx, dy } };
}

static gates_size_t paint_count(gates_tree_t *t, gates_size_t vp, gates_u32 *cmds) {
    GT_ASSERT_OK(gates_layout_run(t, vp, be));
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, proven_heap_allocator(), 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    *cmds = gates_draw_list_len(&dl);
    gates_draw_list_deinit(&dl);
    return vp;
}

/* -- R9-01: a range may span the whole of gates_i64 ------------------------------------------ */

static gates_node_t make_slider(gates_tree_t *t, gates_i64 min, gates_i64 max, gates_i64 step, gates_i64 value) {
    gates_range_t g = { .min = min, .max = max, .step = step, .value = value };
    gates_node_t s = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_layout_set(t, gates_tree_root(t), GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_slider_create(t, gates_tree_root(t), &g, false, &s));
    return s;
}

static void key(gates_tree_t *t, gates_key_t k) {
    gates_key_event_t e = { .key = k, .down = true };
    (void)gates_input_key(t, &e);
}

static void test_range_extremes(void) {
    gates_tree_t *t = new_tree();
    const gates_i64 lo = -9000000000000000000ll, hi = 9000000000000000000ll; /* span past gates_i64 */
    gates_node_t s = make_slider(t, lo, hi, 0, 0);
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 400, 100 }, be));
    gates_range_t g = gates_range_get(t, s);
    GT_ASSERT(g.step == 1 && g.page == 10);
    /* The thumb sits in the middle for 0 and at the ends for the extremes (the span overflowed). */
    gates_tree_set_focus(t, s);
    gates_rect_t r = gates_node_layout_rect(t, s);
    key(t, GATES_KEY_END);
    GT_ASSERT(gates_range_value(t, s) == hi);
    key(t, GATES_KEY_RIGHT); /* past the end: stays */
    GT_ASSERT(gates_range_value(t, s) == hi);
    key(t, GATES_KEY_PAGE_UP);
    GT_ASSERT(gates_range_value(t, s) == hi);
    key(t, GATES_KEY_HOME);
    GT_ASSERT(gates_range_value(t, s) == lo);
    key(t, GATES_KEY_LEFT);
    key(t, GATES_KEY_PAGE_DOWN);
    GT_ASSERT(gates_range_value(t, s) == lo);
    /* A drag to the far end of the track reaches the maximum; to the near end, the minimum. */
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 400, 100 }, be));
    gates_pointer_event_t d = ptr(GATES_POINTER_DOWN, r.x + 8, r.y + r.h / 2);
    (void)gates_input_pointer(t, &d);
    gates_pointer_event_t m = ptr(GATES_POINTER_MOVE, r.x + r.w + 50, r.y + r.h / 2);
    (void)gates_input_pointer(t, &m);
    gates_pointer_event_t u = ptr(GATES_POINTER_UP, r.x + r.w + 50, r.y + r.h / 2);
    (void)gates_input_pointer(t, &u);
    GT_ASSERT(gates_range_value(t, s) == hi);
    gates_tree_destroy(t);

    /* A step as large as the range: the default page (ten steps) saturates instead of wrapping. */
    t = new_tree();
    s = make_slider(t, INT64_MIN, INT64_MAX, INT64_MAX, 0);
    g = gates_range_get(t, s);
    GT_ASSERT(g.step == INT64_MAX && g.page == INT64_MAX);
    gates_tree_set_focus(t, s);
    key(t, GATES_KEY_RIGHT);
    GT_ASSERT(gates_range_value(t, s) == INT64_MAX);
    key(t, GATES_KEY_LEFT);
    GT_ASSERT(gates_range_value(t, s) == 0);
    GT_ASSERT_OK(gates_range_set_value(t, s, INT64_MIN));
    key(t, GATES_KEY_LEFT);
    GT_ASSERT(gates_range_value(t, s) == INT64_MIN);
    gates_tree_destroy(t);

    /* A spin box over the whole range steps and holds at both ends. */
    t = new_tree();
    gates_range_t sg = { .min = INT64_MIN, .max = INT64_MAX, .step = INT64_MAX, .value = INT64_MAX - 5 };
    gates_node_t sp = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_spin_create(t, gates_tree_root(t), &sg, &sp));
    gates_tree_set_focus(t, gates_spin_box(t, sp));
    key(t, GATES_KEY_UP);
    GT_ASSERT(gates_range_value(t, sp) == INT64_MAX);
    key(t, GATES_KEY_PAGE_DOWN);
    key(t, GATES_KEY_PAGE_DOWN);
    GT_ASSERT(gates_range_value(t, sp) == INT64_MIN + 1); /* INT64_MAX - 2 * INT64_MAX */
    key(t, GATES_KEY_PAGE_DOWN);
    GT_ASSERT(gates_range_value(t, sp) == INT64_MIN); /* held at the minimum */
    gates_tree_destroy(t);
}

/* R9-09: a tick on every step of a huge range drew a command per step: now none closer than a pixel. */
static void test_slider_ticks_bounded(void) {
    gates_tree_t *t = new_tree();
    gates_node_t s = make_slider(t, 0, 300000, 1, 0);
    gates_u32 plain = 0, ticked = 0;
    (void)paint_count(t, (gates_size_t){ 400, 100 }, &plain);
    GT_ASSERT_OK(gates_slider_set_ticks(t, s, 1));
    (void)paint_count(t, (gates_size_t){ 400, 100 }, &ticked);
    GT_ASSERT(ticked <= plain + 1); /* too close to be ticks: none */
    gates_tree_destroy(t);

    t = new_tree();
    s = make_slider(t, 0, INT64_MAX, 1, 0);
    GT_ASSERT_OK(gates_slider_set_ticks(t, s, 1));
    (void)paint_count(t, (gates_size_t){ 400, 100 }, &ticked); /* used to run until memory ended */
    GT_ASSERT(ticked < 100);
    gates_tree_destroy(t);

    /* Ticks that are far enough apart are still drawn. */
    t = new_tree();
    s = make_slider(t, 0, 100, 10, 0);
    (void)paint_count(t, (gates_size_t){ 400, 100 }, &plain);
    GT_ASSERT_OK(gates_slider_set_ticks(t, s, 1));
    (void)paint_count(t, (gates_size_t){ 400, 100 }, &ticked);
    GT_ASSERT(ticked == plain + 11);
    gates_tree_destroy(t);
}

/* -- R9-02: wheel amounts from outside ------------------------------------------------------- */

static void test_wheel_not_finite(void) {
    gates_tree_t *t = new_tree();
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_SCROLL));
    for (int i = 0; i < 100; i++) {
        gates_node_t l;
        GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("row"), &l));
    }
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 100, 50 }, be));
    gates_i32 max_off = gates_layout_scroll_content(t, root).h - 50;
    GT_ASSERT(max_off > 0);
    GT_ASSERT_OK(gates_layout_set_scroll_offset(t, root, 100));
    float nan = nanf("");
    gates_pointer_event_t e = wheel_ev(10, 10, nan, nan);
    (void)gates_input_pointer(t, &e);
    GT_ASSERT(gates_layout_scroll_offset(t, root) == 100); /* NaN moves nothing */
    e = wheel_ev(10, 10, 0.0f, -INFINITY);
    (void)gates_input_pointer(t, &e);
    GT_ASSERT(gates_layout_scroll_offset(t, root) == max_off); /* held to the most one event moves, then to the end */
    e = wheel_ev(10, 10, 0.0f, 1e30f);
    (void)gates_input_pointer(t, &e);
    GT_ASSERT(gates_layout_scroll_offset(t, root) == 0);
    e = wheel_ev(10, 10, nan, 0.0f);
    (void)gates_input_pointer(t, &e);
    GT_ASSERT(gates_layout_scroll_offset(t, root) == 0);
    gates_tree_destroy(t);

    /* An editor: the same floats, then the lines move by a bounded amount. */
    t = new_tree();
    gates_editor_desc_t ed = { .rows = 5, .cols = 20 };
    gates_node_t eh;
    GT_ASSERT_OK(gates_editor_create(t, gates_tree_root(t), &ed, &eh));
    char text[2000];
    for (int i = 0; i < 300; i++) text[i * 6] = 'l', text[i * 6 + 1] = 'i', text[i * 6 + 2] = 'n', text[i * 6 + 3] = 'e', text[i * 6 + 4] = 'x',
                                  text[i * 6 + 5] = '\n';
    GT_ASSERT_OK(gates_editor_set_text(t, eh, (gates_str_t){ (const gates_u8 *)text, 1800 }));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 300, 120 }, be));
    gates_rect_t r = gates_node_layout_rect(t, eh);
    gates_pointer_event_t w = wheel_ev(r.x + 5, r.y + 5, nan, nan);
    (void)gates_input_pointer(t, &w);
    GT_ASSERT(gates_editor_first_line(t, eh) == 0);
    w = wheel_ev(r.x + 5, r.y + 5, -INFINITY, -INFINITY);
    (void)gates_input_pointer(t, &w);
    GT_ASSERT(gates_editor_first_line(t, eh) > 0);
    w = wheel_ev(r.x + 5, r.y + 5, 0.0f, INFINITY);
    (void)gates_input_pointer(t, &w);
    GT_ASSERT(gates_editor_first_line(t, eh) == 0);
    gates_tree_destroy(t);
}

/* -- R9-03: positions and sizes handed in ----------------------------------------------------- */

static void cmd_noop(gates_tree_t *t, gates_command_id_t id, void *u) { (void)t; (void)id; (void)u; }

static void test_extreme_geometry(void) {
    gates_tree_t *t = new_tree();
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_ROW));
    gates_node_t a, b;
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("ok"), nullptr, nullptr, &a));
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("label"), &b));
    GT_ASSERT(gates_layout_set_padding(t, root, INT32_MAX) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_layout_set_gap(t, root, GATES_EXTENT_MAX + 1) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_layout_set_padding(t, root, GATES_EXTENT_MAX));
    GT_ASSERT_OK(gates_layout_set_gap(t, root, GATES_EXTENT_MAX));
    GT_ASSERT_OK(gates_layout_set_padding(t, root, 4));
    GT_ASSERT_OK(gates_layout_set_gap(t, root, 4));
    /* A viewport as large as an int: held to GATES_COORD_MAX. */
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ INT32_MAX, INT32_MAX }, be));
    gates_rect_t r = gates_node_layout_rect(t, root);
    GT_ASSERT(r.w == GATES_COORD_MAX && r.h == GATES_COORD_MAX);
    gates_rect_t ra = gates_node_layout_rect(t, a), rb = gates_node_layout_rect(t, b);
    GT_ASSERT(ra.x >= 0 && ra.x + ra.w <= GATES_COORD_MAX && rb.x + rb.w <= GATES_COORD_MAX);
    gates_u32 cmds;
    (void)paint_count(t, (gates_size_t){ INT32_MAX, INT32_MAX }, &cmds); /* paint adds to these: no overflow */
    /* An absolute rectangle is held the same way. */
    gates_node_t panel;
    GT_ASSERT_OK(gates_panel_create(t, root, &panel));
    GT_ASSERT_OK(gates_layout_set_abs_rect(t, panel, (gates_rect_t){ INT32_MIN, INT32_MAX, INT32_MAX, INT32_MIN }));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 100 }, be));
    /* Pointer positions at the edge of the integers, a menu opened there, a hit test there. */
    gates_pointer_event_t e = ptr(GATES_POINTER_MOVE, INT32_MIN, INT32_MAX);
    (void)gates_input_pointer(t, &e);
    e = ptr(GATES_POINTER_DOWN, INT32_MAX, INT32_MIN);
    (void)gates_input_pointer(t, &e);
    e = ptr(GATES_POINTER_UP, INT32_MAX, INT32_MIN);
    (void)gates_input_pointer(t, &e);
    (void)gates_hit_test(t, (gates_point_t){ INT32_MIN, INT32_MIN });
    (void)gates_cursor_at(t, (gates_point_t){ INT32_MAX, INT32_MAX });
    gates_command_desc_t cd = { .id = 1, .label = GATES_STR("Cut"), .enabled = true, .invoke = cmd_noop };
    GT_ASSERT_OK(gates_command_register(t, root, &cd));
    gates_command_id_t ids[1] = { 1 };
    gates_node_t menu;
    GT_ASSERT_OK(gates_menu_open(t, (gates_point_t){ INT32_MIN + 5, INT32_MAX - 5 }, root, ids, 1, &menu));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 100 }, be));
    gates_rect_t mr = gates_node_layout_rect(t, menu);
    GT_ASSERT(mr.x >= -GATES_COORD_MAX && mr.y >= -GATES_COORD_MAX && mr.x + mr.w <= 2 * GATES_COORD_MAX);
    gates_tree_destroy(t);
}

static gates_u64 m_count(void *u) { (void)u; return 5; }
static gates_u64 m_id(void *u, gates_u64 row) { (void)u; return row + 1; }
static bool m_index(void *u, gates_u64 id, gates_u64 *row) { (void)u; if (id < 1 || id > 5) return false; *row = id - 1; return true; }
static gates_err_t m_cell(void *u, gates_u64 id, gates_u32 col, gates_cell_t *o) {
    (void)u; (void)id; (void)col;
    *o = (gates_cell_t){ .text = GATES_STR("cell") };
    return GATES_OK;
}

static void test_column_widths_held(void) {
    gates_tree_t *t = new_tree();
    gates_column_desc_t cols[2] = {
        { .id = 1, .label = GATES_STR("a"), .width = INT32_MAX, .min_width = INT32_MAX },
        { .id = 2, .label = GATES_STR("b"), .width = 40 },
    };
    gates_view_desc_t vd = { .columns = cols, .column_count = 2, .header = true };
    gates_node_t v;
    GT_ASSERT_OK(gates_view_create(t, gates_tree_root(t), &vd, &v));
    gates_rows_model_t m = { .count = m_count, .id_at = m_id, .index_of = m_index, .cell = m_cell };
    GT_ASSERT_OK(gates_view_set_model(t, v, &m));
    GT_ASSERT(gates_view_column_width(t, v, 1) == GATES_EXTENT_MAX);
    GT_ASSERT_OK(gates_view_set_column_width(t, v, 2, INT32_MAX));
    GT_ASSERT(gates_view_column_width(t, v, 2) == GATES_EXTENT_MAX);
    GT_ASSERT_OK(gates_view_set_column_width(t, v, 2, INT32_MIN));
    GT_ASSERT(gates_view_column_width(t, v, 2) > 0);
    gates_u32 cmds;
    (void)paint_count(t, (gates_size_t){ 300, 150 }, &cmds);
    gates_pointer_event_t w = wheel_ev(20, 40, INFINITY, -INFINITY);
    (void)gates_input_pointer(t, &w);
    w = wheel_ev(20, 40, nanf(""), 1e30f);
    (void)gates_input_pointer(t, &w);
    gates_tree_destroy(t);
}

/* -- R9-04: the empty text's null pointer plus zero -------------------------------------------- */

static void test_empty_text_paths(void) {
    gates_tree_t *t = new_tree();
    gates_node_t root = gates_tree_root(t);
    gates_node_t tb, ed;
    GT_ASSERT_OK(gates_textbox_create(t, root, GATES_STR(""), 10, &tb));
    gates_editor_desc_t d = { .rows = 4, .cols = 20 };
    GT_ASSERT_OK(gates_editor_create(t, root, &d, &ed));
    gates_tree_set_focus(t, tb);
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 300, 200 }, be));
    GT_ASSERT(gates_input_char(t, 'a') == GATES_INPUT_CONSUMED); /* an edit of the empty box */
    GT_ASSERT_OK(gates_textbox_set_text(t, tb, GATES_STR("")));
    (void)gates_input_preedit(t, GATES_STR("\xE3\x81\x82"), 3); /* a composition in an empty box: painted */
    gates_u32 cmds;
    (void)paint_count(t, (gates_size_t){ 300, 200 }, &cmds);
    GT_ASSERT(gates_input_preedit_cancel(t) == GATES_INPUT_CONSUMED);
    gates_key_event_t k = { .key = GATES_KEY_A, .down = true, .ctrl = true };
    (void)gates_input_key(t, &k); /* select all of nothing */
    k = (gates_key_event_t){ .key = GATES_KEY_C, .down = true, .ctrl = true };
    (void)gates_input_key(t, &k); /* copy of nothing */
    gates_tree_set_focus(t, ed);
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 300, 200 }, be));
    (void)paint_count(t, (gates_size_t){ 300, 200 }, &cmds); /* an empty editor, caret and all */
    GT_ASSERT_OK(gates_editor_set_wrap(t, ed, true));
    (void)paint_count(t, (gates_size_t){ 300, 200 }, &cmds);
    (void)gates_editor_set_selection(t, ed, 0, 0);
    GT_ASSERT(gates_input_char(t, 'z') == GATES_INPUT_CONSUMED);
    (void)gates_editor_undo(t, ed);
    gates_tree_destroy(t);
}

/* -- R9-06: deferred calls asked for during a run wait for the next ------------------------------ */

static int ran_a, ran_b, ran_c;
static void def_c(gates_tree_t *t, gates_u32 key, void *u) { (void)t; (void)key; (void)u; ran_c++; }
static void def_b(gates_tree_t *t, gates_u32 key, void *u) { (void)t; (void)key; (void)u; ran_b++; }
static void def_a(gates_tree_t *t, gates_u32 key, void *u) {
    (void)key; (void)u;
    ran_a++;
    GT_ASSERT(gates_tree_cancel_defer(t, 2)); /* B never runs ... */
    GT_ASSERT_OK(gates_tree_defer(t, 3, def_c, nullptr)); /* ... and C, asked for now, waits */
}

static void test_defer_turns(void) {
    gates_tree_t *t = new_tree();
    ran_a = ran_b = ran_c = 0;
    GT_ASSERT_OK(gates_tree_defer(t, 1, def_a, nullptr));
    GT_ASSERT_OK(gates_tree_defer(t, 2, def_b, nullptr));
    (void)gates_tree_dispatch_events(t, 10);
    GT_ASSERT(ran_a == 1 && ran_b == 0 && ran_c == 0);
    (void)gates_tree_dispatch_events(t, 10);
    GT_ASSERT(ran_a == 1 && ran_b == 0 && ran_c == 1);
    (void)gates_tree_dispatch_events(t, 10);
    GT_ASSERT(ran_c == 1);
    /* Asking again for a key that has not run yet still means one call, the latest. */
    ran_a = ran_b = ran_c = 0;
    GT_ASSERT_OK(gates_tree_defer(t, 5, def_b, nullptr));
    GT_ASSERT_OK(gates_tree_defer(t, 5, def_c, nullptr));
    (void)gates_tree_dispatch_events(t, 10);
    GT_ASSERT(ran_b == 0 && ran_c == 1);
    gates_tree_destroy(t);
}


/* -- R9-10: a log's line limit is a limit, not a reservation ---------------------------------------- */

static bool counting_fail;
static gates_usize_t biggest_request;
static proven_result_mem_mut_t cnt_alloc(void *c, proven_size_t size, proven_size_t align) {
    if (size > biggest_request) biggest_request = size;
    return proven_heap_allocator().alloc_fn(c, size, align);
}
static proven_result_mem_mut_t cnt_realloc(void *c, void *old, proven_size_t os, proven_size_t ns, proven_size_t align) {
    if (ns > biggest_request) biggest_request = ns;
    return proven_heap_allocator().realloc_fn(c, old, os, ns, align);
}
static void cnt_free(void *c, void *p) { proven_heap_allocator().free_fn(c, p); }

static void test_log_limit_not_reserved(void) {
    (void)counting_fail;
    gates_tree_t *t = nullptr;
    gates_tree_desc_t td = { .allocator = { nullptr, cnt_alloc, cnt_realloc, cnt_free } };
    GT_ASSERT_OK(gates_tree_create(&td, &t));
    biggest_request = 0;
    gates_log_desc_t d = { .max_lines = UINT32_MAX, .max_bytes = 0 };
    gates_node_t lg;
    GT_ASSERT_OK(gates_log_create(t, gates_tree_root(t), &d, &lg));
    GT_ASSERT(biggest_request < (1u << 20)); /* it used to ask for 4G lines of memory at once */
    /* Many lines: it grows as they come and keeps them in order past each doubling. */
    char text[16];
    for (int i = 0; i < 5000; i++) {
        int n = snprintf(text, sizeof text, "line %d", i);
        GT_ASSERT_OK(gates_log_append(t, lg, (gates_str_t){ (const gates_u8 *)text, (gates_usize_t)n }));
    }
    GT_ASSERT(gates_log_count(t, lg) == 5000 && gates_log_dropped(t, lg) == 0);
    gates_str_t first = gates_log_line(t, lg, 1), last = gates_log_line(t, lg, 5000);
    GT_ASSERT(first.size == 6 && memcmp(first.ptr, "line 0", 6) == 0);
    GT_ASSERT(last.size == 9 && memcmp(last.ptr, "line 4999", 9) == 0);
    gates_tree_destroy(t);

    /* A small limit still drops the oldest, across the wrap. */
    t = new_tree();
    gates_log_desc_t small = { .max_lines = 100 };
    GT_ASSERT_OK(gates_log_create(t, gates_tree_root(t), &small, &lg));
    for (int i = 0; i < 1000; i++) {
        int n = snprintf(text, sizeof text, "n%d", i);
        GT_ASSERT_OK(gates_log_append(t, lg, (gates_str_t){ (const gates_u8 *)text, (gates_usize_t)n }));
    }
    GT_ASSERT(gates_log_count(t, lg) == 100 && gates_log_dropped(t, lg) == 900);
    gates_str_t l1 = gates_log_line(t, lg, 901);
    GT_ASSERT(l1.size == 4 && memcmp(l1.ptr, "n900", 4) == 0);
    GT_ASSERT(gates_log_line(t, lg, 900).size == 0);
    gates_tree_destroy(t);
}

/* -- R9-11: destroying a subtree that holds the owner of an open menu --------------------------------- */

/* The destroy hooks close the open menu - a destroy of its own - between the count of the subtree and
 * the marking of its nodes. The pending list was sized from the count alone: the inner destroy used
 * the room, and the marking wrote past the array (found by the fuzzer; run under `make ubsan`). */
static void test_destroy_with_open_menu_inside(void) {
    for (int fillers = 8; fillers <= 70; fillers++) {
        gates_tree_t *t = new_tree();
        gates_node_t root = gates_tree_root(t), box, bar;
        GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
        GT_ASSERT_OK(gates_panel_create(t, root, &box));
        GT_ASSERT_OK(gates_layout_set(t, box, GATES_LAYOUT_KIND_COLUMN));
        for (int i = 0; i < fillers; i++) {
            gates_node_t l;
            GT_ASSERT_OK(gates_label_create(t, box, GATES_STR("filler"), &l));
        }
        gates_command_id_t ids[12];
        for (int i = 0; i < 12; i++) {
            gates_command_desc_t c = { .id = (gates_command_id_t)(i + 1), .label = GATES_STR("item"), .enabled = true,
                                       .invoke = cmd_noop };
            GT_ASSERT_OK(gates_command_register(t, box, &c)); /* the menu's scope is the node destroyed */
            ids[i] = (gates_command_id_t)(i + 1);
        }
        GT_ASSERT_OK(gates_menubar_create(t, box, box, &bar));
        GT_ASSERT_OK(gates_menubar_add(t, bar, GATES_STR("&File"), ids, 12, nullptr));
        GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 400, 600 }, be));
        gates_key_event_t alt = { .key = GATES_KEY_F10, .down = true };
        (void)gates_input_key(t, &alt);
        gates_key_event_t down = { .key = GATES_KEY_DOWN, .down = true };
        (void)gates_input_key(t, &down);
        GT_ASSERT(gates_menubar_open_index(t, bar) == 0 && gates_tree_overlay_count(t) == 1);
        gates_u32 live0 = gates_tree_live_count(t);
        GT_ASSERT_OK(gates_node_destroy(t, box)); /* the menu closes inside this call */
        if (getenv("TRACE")) printf("live before %u after %u\n", live0, gates_tree_live_count(t));
        GT_ASSERT(gates_tree_overlay_count(t) == 0);
        if (getenv("TRACE")) printf("fillers %d pending %u\n", fillers, gates_tree_pending_count(t));
        GT_ASSERT_OK(gates_tree_flush_destroys(t));
        GT_ASSERT(gates_tree_pending_count(t) == 0);
        gates_tree_destroy(t);
    }
}

/* -- R9-12: size hints are held, and the access calls hold their points ---------------------------- */

static void test_size_hints_and_access_points(void) {
    gates_tree_t *t = new_tree();
    gates_node_t root = gates_tree_root(t), tb, ed;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_textbox_create(t, root, GATES_STR("x"), UINT32_MAX, &tb));
    gates_editor_desc_t d = { .rows = UINT32_MAX, .cols = UINT32_MAX, .tab_width = UINT32_MAX };
    GT_ASSERT_OK(gates_editor_create(t, root, &d, &ed));
    gates_u32 cmds;
    (void)paint_count(t, (gates_size_t){ 400, 300 }, &cmds);
    gates_size_t pt = gates_node_preferred_size(t, tb), pe = gates_node_preferred_size(t, ed);
    GT_ASSERT(pt.w > 0 && pt.w < (1 << 24) && pe.w > 0 && pe.h > 0 && pe.w < (1 << 24) && pe.h < (1 << 24));
    gates_point_t far[] = { { INT32_MIN, INT32_MIN }, { INT32_MAX, INT32_MAX }, { INT32_MIN, INT32_MAX } };
    for (gates_usize_t i = 0; i < sizeof far / sizeof far[0]; i++) {
        (void)gates_access_at_point(t, far[i]);
        (void)gates_access_cell_at(t, tb, far[i]);
        (void)gates_access_text_offset_at(t, tb, far[i]);
        (void)gates_access_text_offset_at(t, ed, far[i]);
    }
    gates_tree_destroy(t);
}

/* -- second review (0.15.1) ------------------------------------------------------------------- */

/* The public gates_node_create makes plain nodes only: another kind would be found by code that
 * trusts the widget state its constructor attaches (it was a null dereference at layout). */
static void test_node_create_plain_kinds_only(void) {
    gates_tree_t *t = new_tree();
    gates_node_kind_t bad[] = { GATES_NODE_VIEW, GATES_NODE_SLIDER, GATES_NODE_SPIN, GATES_NODE_GROUP,
                                GATES_NODE_EDITOR, GATES_NODE_TEXTBOX, GATES_NODE_MENU, GATES_NODE_TABS };
    for (gates_usize_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        gates_node_t n = { .index = 77, .generation = 9 };
        GT_ASSERT(gates_node_create(t, gates_tree_root(t), &(gates_node_desc_t){ .kind = bad[i] }, &n) ==
                  PROVEN_ERR_INVALID_ARG);
        GT_ASSERT(gates_node_eq(n, GATES_NODE_NULL));
    }
    gates_node_t ok;
    GT_ASSERT_OK(gates_node_create(t, gates_tree_root(t), &(gates_node_desc_t){ .kind = GATES_NODE_PANEL }, &ok));
    GT_ASSERT_OK(gates_node_create(t, gates_tree_root(t), &(gates_node_desc_t){ .kind = GATES_NODE_SEPARATOR }, &ok));
    GT_ASSERT_OK(gates_node_create(t, gates_tree_root(t), &(gates_node_desc_t){ .kind = GATES_NODE_CUSTOM }, &ok));
    gates_u32 cmds;
    (void)paint_count(t, (gates_size_t){ 200, 100 }, &cmds);
    gates_tree_destroy(t);
}

/* One UTF-8 rule: overlong forms, surrogates and values past U+10FFFF are one byte of U+FFFD in
 * the decoder, and every validity check (text, edit, encoding) agrees with it. */
static void test_one_utf8_rule(void) {
    static const char *bad[] = { "\xC0\x80", "\xC1\xBF", "\xE0\x80\x80", "\xED\xA0\x80", "\xED\xBF\xBF",
                                 "\xF0\x80\x80\x80", "\xF4\x90\x80\x80", "\xF7\xBF\xBF\xBF", "\xFF", "\x80",
                                 "\xE2\x82" };
    for (gates_usize_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        gates_str_t b = { .ptr = (const gates_u8 *)bad[i], .size = strlen(bad[i]) };
        gates_u32 cp = 0;
        GT_ASSERT(gates_text_decode(b, 0, &cp) == 1 && cp == 0xFFFD);
        gates_usize_t at = 99;
        GT_ASSERT(!gates_text_utf8_valid(b, &at) && at == 0);
        GT_ASSERT(!gates_utf8_valid(b, nullptr));
    }
    gates_str_t good = GATES_STR("h\xC3\xA9llo \xE2\x9C\x93 \xF0\x9F\x98\x80 \xEF\xBF\xBD");
    GT_ASSERT(gates_text_utf8_valid(good, nullptr) && gates_utf8_valid(good, nullptr));
    gates_u32 cp = 0;
    GT_ASSERT(gates_text_decode(GATES_STR("\xEF\xBF\xBD"), 0, &cp) == 3 && cp == 0xFFFD); /* a real U+FFFD */
    GT_ASSERT(gates_text_decode(GATES_STR("\xF4\x8F\xBF\xBF"), 0, &cp) == 4 && cp == 0x10FFFF);
}

/* The single-line text paths refuse malformed text and keep what they had. */
static void test_textbox_refuses_bad_utf8(void) {
    gates_tree_t *t = new_tree();
    gates_node_t tb;
    GT_ASSERT_OK(gates_layout_set(t, gates_tree_root(t), GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_textbox_create(t, gates_tree_root(t), GATES_STR("ab"), 10, &tb));
    GT_ASSERT(gates_textbox_set_text(t, tb, GATES_STR("\xFF\xFE")) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_textbox_set_text(t, tb, GATES_STR("\xC0\x80")) == PROVEN_ERR_INVALID_ARG);
    gates_str_t cur = gates_textbox_text(t, tb);
    GT_ASSERT(cur.size == 2 && memcmp(cur.ptr, "ab", 2) == 0);
    GT_ASSERT(gates_textbox_replace_selection(t, tb, GATES_STR("\xED\xA0\x80")) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_textbox_text(t, tb).size == 2);
    GT_ASSERT_OK(gates_textbox_set_text(t, tb, GATES_STR("\xEA\xB0\x80"))); /* U+AC00 is fine */
    gates_text_edit_t ed;
    GT_ASSERT_OK(gates_text_edit_init(&ed, (gates_allocator_t){0}, GATES_STR("x")));
    GT_ASSERT(gates_text_edit_insert(&ed, GATES_STR("\xF7\xBF\xBF\xBF")) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_text_edit_set_text(&ed, GATES_STR("\x80")) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_text_edit_text(&ed).size == 1);
    gates_text_edit_deinit(&ed);
    gates_tree_destroy(t);
}

/* A composition the editor cannot take (the text limit) is reported, not dropped silently. */
static void test_ime_commit_failure_reported(void) {
    gates_tree_t *t = new_tree();
    GT_ASSERT_OK(gates_layout_set(t, gates_tree_root(t), GATES_LAYOUT_KIND_COLUMN));
    gates_node_t ed;
    GT_ASSERT_OK(gates_editor_create(t, gates_tree_root(t), &(gates_editor_desc_t){ .max_bytes = 4 }, &ed));
    gates_tree_set_focus(t, ed);
    (void)gates_input_commit(t, GATES_STR("abcdefgh")); /* past the limit: the composition ends, the error is kept */
    GT_ASSERT(gates_input_take_error(t) == PROVEN_ERR_OUT_OF_BOUNDS);
    GT_ASSERT(gates_input_commit(t, GATES_STR("\xFF\xFE")) == GATES_INPUT_FAILED); /* not text: it used to vanish */
    GT_ASSERT(gates_input_take_error(t) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_input_commit(t, GATES_STR("ab")) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_input_take_error(t) == GATES_OK);
    gates_tree_destroy(t);
}

/* A very long typing run is split into entries: one entry could outgrow the history budget. */
static void test_editor_history_long_run(void) {
    gates_tree_t *t = new_tree();
    GT_ASSERT_OK(gates_layout_set(t, gates_tree_root(t), GATES_LAYOUT_KIND_COLUMN));
    gates_node_t ed;
    GT_ASSERT_OK(gates_editor_create(t, gates_tree_root(t), &(gates_editor_desc_t){0}, &ed));
    gates_tree_set_focus(t, ed);
    for (int i = 0; i < 600000; i++) {
        GT_ASSERT(gates_input_char(t, 'a') == GATES_INPUT_CONSUMED);
    }
    GT_ASSERT(gates_editor_length(t, ed) == 600000);
    int undos = 0;
    while (gates_editor_length(t, ed) > 0 && undos < 20 && gates_is_ok(gates_editor_undo(t, ed))) undos++;
    GT_ASSERT(gates_editor_length(t, ed) == 0 && undos >= 2 && undos <= 5); /* split, but still a few steps */
    gates_tree_destroy(t);
}

/* An allocator with a hole in it (alloc without free) is not used: the default heap is, as
 * every other creator does. */
static void test_half_allocator_falls_back(void) {
    gates_allocator_t h = proven_heap_allocator();
    gates_allocator_t half = { .ctx = h.ctx, .alloc_fn = h.alloc_fn };
    gates_undo_t *u = nullptr;
    GT_ASSERT_OK(gates_undo_create(half, 0, &u));
    gates_undo_destroy(u);
    gates_text_buffer_t *b = nullptr;
    GT_ASSERT_OK(gates_text_buffer_create(half, &b));
    GT_ASSERT_OK(gates_text_buffer_replace(b, 0, 0, GATES_STR("hello")));
    gates_text_buffer_destroy(b);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_range_extremes();
    test_slider_ticks_bounded();
    test_wheel_not_finite();
    test_extreme_geometry();
    test_column_widths_held();
    test_empty_text_paths();
    test_defer_turns();
    test_log_limit_not_reserved();
    test_destroy_with_open_menu_inside();
    test_size_hints_and_access_points();
    test_node_create_plain_kinds_only();
    test_one_utf8_rule();
    test_textbox_refuses_bad_utf8();
    test_ime_commit_failure_reported();
    test_editor_history_long_run();
    test_half_allocator_falls_back();
    return gt_report("test_hardening");
}
