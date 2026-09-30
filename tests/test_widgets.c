/* widget behavior - creation, props, hit test, synthetic pointer
 * interaction. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <proven/heap.h>
#include "gates_test.h"

#include <string.h>

static const gates_text_backend_t *be;

typedef struct cb_log_t {
    int clicks;
    int toggles;
    bool last_checked;
    gates_node_t last_node;
} cb_log_t;

static void on_click(gates_tree_t *tree, gates_node_t node, void *user) {
    (void)tree;
    cb_log_t *log = user;
    log->clicks++;
    log->last_node = node;
}

static void on_toggle(gates_tree_t *tree, gates_node_t node, bool checked, void *user) {
    (void)tree;
    cb_log_t *log = user;
    log->toggles++;
    log->last_checked = checked;
    log->last_node = node;
}

static gates_tree_t *make_tree(void) {
    gates_tree_desc_t desc = {0};
    gates_tree_t *tree = nullptr;
    GT_ASSERT_OK(gates_tree_create(&desc, &tree));
    return tree;
}

static gates_pointer_event_t ev_move(gates_i32 x, gates_i32 y) {
    return (gates_pointer_event_t){ .action = GATES_POINTER_MOVE,
                                    .pos = { x, y }, .type = GATES_POINTER_MOUSE };
}

static gates_pointer_event_t ev_down(gates_i32 x, gates_i32 y) {
    return (gates_pointer_event_t){ .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT,
                                    .buttons = GATES_BUTTON_LEFT,
                                    .pos = { x, y }, .type = GATES_POINTER_MOUSE };
}

static gates_pointer_event_t ev_up(gates_i32 x, gates_i32 y) {
    return (gates_pointer_event_t){ .action = GATES_POINTER_UP, .button = GATES_BUTTON_LEFT,
                                    .pos = { x, y }, .type = GATES_POINTER_MOUSE };
}

static void route(gates_tree_t *t, gates_pointer_event_t e) {
    (void)gates_input_pointer(t, &e);
}

static void test_creation_and_props(void) {
    gates_tree_t *t = make_tree();
    gates_node_t root = gates_tree_root(t);

    gates_node_t lbl = GATES_NODE_NULL, btn = GATES_NODE_NULL, chk = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("hello"), &lbl));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("Go"), nullptr, nullptr, &btn));
    GT_ASSERT_OK(gates_checkbox_create(t, root, GATES_STR("opt"), true, nullptr, nullptr, &chk));

    GT_ASSERT(gates_node_kind(t, lbl) == GATES_NODE_LABEL);
    GT_ASSERT(gates_node_kind(t, btn) == GATES_NODE_BUTTON);
    GT_ASSERT(gates_node_kind(t, chk) == GATES_NODE_CHECKBOX);

    gates_str_t s = gates_widget_text(t, lbl);
    GT_ASSERT(s.size == 5 && memcmp(s.ptr, "hello", 5) == 0);
    GT_ASSERT(gates_checkbox_checked(t, chk));
    GT_ASSERT(!gates_widget_disabled(t, btn));

    /* Text setter replaces the owned copy and marks layout dirty. */
    gates_tree_clear_dirty(t, GATES_TREE_DIRTY_LAYOUT | GATES_TREE_DIRTY_PAINT);
    GT_ASSERT_OK(gates_widget_set_text(t, lbl, GATES_STR("re")));
    s = gates_widget_text(t, lbl);
    GT_ASSERT(s.size == 2 && memcmp(s.ptr, "re", 2) == 0);
    GT_ASSERT((gates_tree_dirty(t) & GATES_TREE_DIRTY_LAYOUT) != 0);

    /* Wrong-kind and stale-handle prop calls fail cleanly. */
    GT_ASSERT_ERR(gates_checkbox_set_checked(t, btn, true));
    GT_ASSERT_ERR(gates_widget_set_text(t, root, GATES_STR("x"))); /* panel-like root */

    gates_tree_destroy(t);
}

/* Full alloc/free balance (including widget text) is proven by the counting
 * allocator in test_paint.c's lifecycle test; here we verify handle safety. */
static void test_widget_destroy_releases_state(void) {
    gates_tree_t *t = make_tree();
    gates_node_t root = gates_tree_root(t);
    gates_node_t btn = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("temp"), nullptr, nullptr, &btn));
    gates_u32 live = gates_tree_live_count(t);

    GT_ASSERT_OK(gates_node_destroy(t, btn));
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    GT_ASSERT(gates_tree_live_count(t) == live - 1);
    /* Stale widget queries return safe defaults. */
    GT_ASSERT(gates_widget_text(t, btn).size == 0);
    GT_ASSERT(!gates_widget_hovered(t, btn));
    gates_tree_destroy(t);
}

/* Layout for interaction tests:
 * root COLUMN, padding 0: button (pref h=26) then checkbox. Viewport 200x100. */
static void build_ui(gates_tree_t *t, cb_log_t *log,
                     gates_node_t *out_btn, gates_node_t *out_chk) {
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("OK"), on_click, log, out_btn));
    GT_ASSERT_OK(gates_checkbox_create(t, root, GATES_STR("check"), false, on_toggle, log,
                                       out_chk));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 100 }, be));
}

static void test_hit_test(void) {
    gates_tree_t *t = make_tree();
    cb_log_t log = {0};
    gates_node_t btn = GATES_NODE_NULL, chk = GATES_NODE_NULL;
    build_ui(t, &log, &btn, &chk);

    gates_rect_t br = gates_node_layout_rect(t, btn);
    gates_rect_t cr = gates_node_layout_rect(t, chk);
    GT_ASSERT(br.h > 0 && cr.y >= br.y + br.h); /* stacked vertically */

    gates_node_t hit = gates_hit_test(t, (gates_point_t){ br.x + 2, br.y + 2 });
    GT_ASSERT(gates_node_eq(hit, btn));
    hit = gates_hit_test(t, (gates_point_t){ cr.x + 2, cr.y + 2 });
    GT_ASSERT(gates_node_eq(hit, chk));
    /* Below both widgets: the root itself. */
    hit = gates_hit_test(t, (gates_point_t){ 150, 95 });
    GT_ASSERT(gates_node_eq(hit, gates_tree_root(t)));

    gates_tree_destroy(t);
}

static void test_click_sequence(void) {
    gates_tree_t *t = make_tree();
    cb_log_t log = {0};
    gates_node_t btn = GATES_NODE_NULL, chk = GATES_NODE_NULL;
    build_ui(t, &log, &btn, &chk);
    gates_rect_t br = gates_node_layout_rect(t, btn);
    gates_i32 bx = br.x + br.w / 2, by = br.y + br.h / 2;

    /* Hover. */
    route(t, ev_move(bx, by));
    GT_ASSERT(gates_widget_hovered(t, btn));
    GT_ASSERT((gates_tree_dirty(t) & GATES_TREE_DIRTY_PAINT) != 0);

    /* Press + release inside -> exactly one click. */
    route(t, ev_down(bx, by));
    GT_ASSERT(gates_widget_pressed(t, btn));
    GT_ASSERT(log.clicks == 0); /* not yet: activate on release */
    route(t, ev_up(bx, by));
    GT_ASSERT(log.clicks == 1);
    GT_ASSERT(gates_node_eq(log.last_node, btn));
    GT_ASSERT(!gates_widget_pressed(t, btn));

    /* Press inside, release OUTSIDE -> no click. */
    route(t, ev_down(bx, by));
    route(t, ev_up(190, 95));
    GT_ASSERT(log.clicks == 1);
    GT_ASSERT(!gates_widget_pressed(t, btn));

    gates_tree_destroy(t);
}

static void test_checkbox_toggle(void) {
    gates_tree_t *t = make_tree();
    cb_log_t log = {0};
    gates_node_t btn = GATES_NODE_NULL, chk = GATES_NODE_NULL;
    build_ui(t, &log, &btn, &chk);
    gates_rect_t cr = gates_node_layout_rect(t, chk);
    gates_i32 cx = cr.x + 6, cy = cr.y + cr.h / 2;

    route(t, ev_down(cx, cy));
    route(t, ev_up(cx, cy));
    GT_ASSERT(log.toggles == 1 && log.last_checked == true);
    GT_ASSERT(gates_checkbox_checked(t, chk));

    route(t, ev_down(cx, cy));
    route(t, ev_up(cx, cy));
    GT_ASSERT(log.toggles == 2 && log.last_checked == false);
    GT_ASSERT(!gates_checkbox_checked(t, chk));

    gates_tree_destroy(t);
}

static void test_disabled_widget_inert(void) {
    gates_tree_t *t = make_tree();
    cb_log_t log = {0};
    gates_node_t btn = GATES_NODE_NULL, chk = GATES_NODE_NULL;
    build_ui(t, &log, &btn, &chk);
    GT_ASSERT_OK(gates_widget_set_disabled(t, btn, true));
    gates_rect_t br = gates_node_layout_rect(t, btn);
    gates_i32 bx = br.x + br.w / 2, by = br.y + br.h / 2;

    route(t, ev_move(bx, by));
    GT_ASSERT(!gates_widget_hovered(t, btn)); /* disabled: not even hover */
    route(t, ev_down(bx, by));
    route(t, ev_up(bx, by));
    GT_ASSERT(log.clicks == 0);

    gates_tree_destroy(t);
}

static void test_stack_hits_active_page_only(void) {
    gates_tree_t *t = make_tree();
    cb_log_t log = {0};
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_STACK));

    gates_node_t page1 = GATES_NODE_NULL, page2 = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_panel_create(t, root, &page1));
    GT_ASSERT_OK(gates_panel_create(t, root, &page2));
    GT_ASSERT_OK(gates_layout_set(t, page1, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_layout_set(t, page2, GATES_LAYOUT_KIND_COLUMN));
    gates_node_t b1 = GATES_NODE_NULL, b2 = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_button_create(t, page1, GATES_STR("P1"), on_click, &log, &b1));
    GT_ASSERT_OK(gates_button_create(t, page2, GATES_STR("P2"), on_click, &log, &b2));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 100 }, be));

    /* Both pages share rects; only the active page's button is hittable. */
    gates_rect_t r1 = gates_node_layout_rect(t, b1);
    gates_point_t p = { r1.x + 2, r1.y + 2 };
    GT_ASSERT(gates_node_eq(gates_hit_test(t, p), b1));

    GT_ASSERT_OK(gates_layout_set_stack_active(t, root, 1));
    GT_ASSERT(gates_node_eq(gates_hit_test(t, p), b2));

    route(t, ev_down(p.x, p.y));
    route(t, ev_up(p.x, p.y));
    GT_ASSERT(log.clicks == 1 && gates_node_eq(log.last_node, b2));

    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    test_creation_and_props();
    test_widget_destroy_releases_state();
    test_hit_test();
    test_click_sequence();
    test_checkbox_toggle();
    test_disabled_widget_inert();
    test_stack_hits_active_page_only();
    return gt_report("test_widgets");
}
