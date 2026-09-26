/* T008: intrinsic layout — measure/arrange (docs/tests/cases/T008-layout.md). */
#include <gates/layout.h>
#include <gates/widget.h>
#include "gates_test.h"

static const gates_text_backend_t *be;
static gates_text_metrics_t M;

static gates_tree_t *make_tree(void) {
    gates_tree_desc_t desc = {0};
    gates_tree_t *tree = nullptr;
    GT_ASSERT_OK(gates_tree_create(&desc, &tree));
    return tree;
}

static gates_node_t mk_panel(gates_tree_t *t, gates_node_t parent) {
    gates_node_t n = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_panel_create(t, parent, &n));
    return n;
}

static bool rect_eq(gates_rect_t r, gates_i32 x, gates_i32 y, gates_i32 w, gates_i32 h) {
    return r.x == x && r.y == y && r.w == w && r.h == h;
}

static void test_widget_intrinsic_measure(void) {
    gates_tree_t *t = make_tree();
    gates_node_t root = gates_tree_root(t);

    gates_node_t lbl = GATES_NODE_NULL, btn = GATES_NODE_NULL, chk = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("abcd"), &lbl));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("OK"), nullptr, nullptr, &btn));
    GT_ASSERT_OK(gates_checkbox_create(t, root, GATES_STR("on"), false, nullptr, nullptr, &chk));
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 300, 200 }, be));

    /* Label: exactly the measured text. */
    gates_size_t lp = gates_node_preferred_size(t, lbl);
    GT_ASSERT(lp.w == 4 * M.advance && lp.h == M.line_height);
    /* Button: text + 2*(pad 8 + border 1) x, 2*(pad 4 + border 1) y. */
    gates_size_t bp = gates_node_preferred_size(t, btn);
    GT_ASSERT(bp.w == 2 * M.advance + 18 && bp.h == M.line_height + 10);
    /* Checkbox: 12px box + 6 gap + text; height >= box. */
    gates_size_t cp = gates_node_preferred_size(t, chk);
    GT_ASSERT(cp.w == 12 + 6 + 2 * M.advance);
    GT_ASSERT(cp.h == (M.line_height > 24 ? M.line_height : 24) /* WCAG 2.5.8 minimum target (plan-0014) */);

    /* Wide text (Hangul) measures 2 cells per syllable in widget sizes too. */
    GT_ASSERT_OK(gates_widget_set_text(t, lbl, GATES_STR("한글")));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 300, 200 }, be));
    GT_ASSERT(gates_node_preferred_size(t, lbl).w == 4 * M.advance);

    gates_tree_destroy(t);
}

static void test_row_layout_exact(void) {
    gates_tree_t *t = make_tree();
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_ROW));
    GT_ASSERT_OK(gates_layout_set_padding(t, root, 10));
    GT_ASSERT_OK(gates_layout_set_gap(t, root, 5));

    /* Three fixed 20x30 children via absolute pref (labels give text sizes;
     * use panels with abs pref via children of size — simplest: labels with
     * known text lengths). */
    gates_node_t a = GATES_NODE_NULL, b = GATES_NODE_NULL, c = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("aa"), &a));   /* 16x16 */
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("bbb"), &b));  /* 24x16 */
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("c"), &c));    /* 8x16 */
    GT_ASSERT_OK(gates_layout_set_child_align(t, a, GATES_ALIGN_START_V));
    GT_ASSERT_OK(gates_layout_set_child_align(t, b, GATES_ALIGN_START_V));
    GT_ASSERT_OK(gates_layout_set_child_align(t, c, GATES_ALIGN_START_V));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 100 }, be));

    GT_ASSERT(rect_eq(gates_node_layout_rect(t, a), 10, 10, 16, 16));
    GT_ASSERT(rect_eq(gates_node_layout_rect(t, b), 10 + 16 + 5, 10, 24, 16));
    GT_ASSERT(rect_eq(gates_node_layout_rect(t, c), 10 + 16 + 5 + 24 + 5, 10, 8, 16));
    GT_ASSERT(gates_layout_validate(t, root));

    /* Root preferred size accounts for children + gaps + padding. */
    gates_size_t rp = gates_node_preferred_size(t, root);
    GT_ASSERT(rp.w == 20 + 16 + 24 + 8 + 10);  /* 2*pad + widths + 2 gaps */
    GT_ASSERT(rp.h == 20 + 16);
    gates_tree_destroy(t);
}

static void test_grow_distribution(void) {
    gates_tree_t *t = make_tree();
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_ROW));

    gates_node_t a = mk_panel(t, root);
    gates_node_t b = mk_panel(t, root);
    gates_node_t c = mk_panel(t, root);
    GT_ASSERT_OK(gates_layout_set_child_grow(t, a, 1));
    GT_ASSERT_OK(gates_layout_set_child_grow(t, b, 3));
    GT_ASSERT_OK(gates_layout_set_child_grow(t, c, 0)); /* stays at pref (0) */
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 400, 50 }, be));

    gates_rect_t ra = gates_node_layout_rect(t, a);
    gates_rect_t rb = gates_node_layout_rect(t, b);
    gates_rect_t rc = gates_node_layout_rect(t, c);
    GT_ASSERT(ra.w == 100 && rb.w == 300 && rc.w == 0);   /* 1:3 of 400 */
    GT_ASSERT(ra.x == 0 && rb.x == 100 && rc.x == 400);
    GT_ASSERT(ra.h == 50 && rb.h == 50);                  /* default stretch */
    GT_ASSERT(gates_layout_validate(t, root));

    /* Exactness with a non-dividing total: 1:2 of 100 -> 33 + 67 = 100. */
    gates_tree_t *t2 = make_tree();
    gates_node_t r2 = gates_tree_root(t2);
    GT_ASSERT_OK(gates_layout_set(t2, r2, GATES_LAYOUT_KIND_ROW));
    gates_node_t x = mk_panel(t2, r2);
    gates_node_t y = mk_panel(t2, r2);
    GT_ASSERT_OK(gates_layout_set_child_grow(t2, x, 1));
    GT_ASSERT_OK(gates_layout_set_child_grow(t2, y, 2));
    GT_ASSERT_OK(gates_layout_run(t2, (gates_size_t){ 100, 10 }, be));
    gates_i32 wx = gates_node_layout_rect(t2, x).w;
    gates_i32 wy = gates_node_layout_rect(t2, y).w;
    GT_ASSERT(wx + wy == 100);
    GT_ASSERT(wx == 33 && wy == 67);
    gates_tree_destroy(t2);
    gates_tree_destroy(t);
}

static void test_column_and_align(void) {
    gates_tree_t *t = make_tree();
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));

    gates_node_t a = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("mm"), &a)); /* 16x16 */
    gates_node_t b = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("q"), &b));  /* 8x16 */
    GT_ASSERT_OK(gates_layout_set_child_align(t, a, GATES_ALIGN_CENTER_V));
    GT_ASSERT_OK(gates_layout_set_child_align(t, b, GATES_ALIGN_END_V));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 100, 60 }, be));

    /* Column main axis = y; cross = x. */
    GT_ASSERT(rect_eq(gates_node_layout_rect(t, a), (100 - 16) / 2, 0, 16, 16));
    GT_ASSERT(rect_eq(gates_node_layout_rect(t, b), 100 - 8, 16, 8, 16));
    GT_ASSERT(gates_layout_validate(t, root));
    gates_tree_destroy(t);
}

static void test_stack_and_absolute(void) {
    gates_tree_t *t = make_tree();
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_STACK));
    GT_ASSERT_OK(gates_layout_set_padding(t, root, 4));

    gates_node_t p1 = mk_panel(t, root);
    gates_node_t p2 = mk_panel(t, root);
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 100, 80 }, be));

    /* Stack children share the content rect (overlap allowed by §10). */
    GT_ASSERT(rect_eq(gates_node_layout_rect(t, p1), 4, 4, 92, 72));
    GT_ASSERT(rect_eq(gates_node_layout_rect(t, p2), 4, 4, 92, 72));
    GT_ASSERT(gates_layout_stack_active(t, root) == 0);
    GT_ASSERT_OK(gates_layout_set_stack_active(t, root, 1));
    GT_ASSERT(gates_layout_stack_active(t, root) == 1);
    GT_ASSERT_ERR(gates_layout_set_stack_active(t, root, 2)); /* out of range */

    /* Absolute inside a stack page. */
    GT_ASSERT_OK(gates_layout_set(t, p1, GATES_LAYOUT_KIND_ABSOLUTE));
    gates_node_t abs_child = mk_panel(t, p1);
    GT_ASSERT_OK(gates_layout_set_abs_rect(t, abs_child, (gates_rect_t){ 10, 20, 30, 15 }));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 100, 80 }, be));
    GT_ASSERT(rect_eq(gates_node_layout_rect(t, abs_child), 14, 24, 30, 15));

    GT_ASSERT(gates_layout_validate(t, root));
    gates_tree_destroy(t);
}

static void test_nested_and_dirty(void) {
    gates_tree_t *t = make_tree();
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));

    gates_node_t toolbar = mk_panel(t, root);
    GT_ASSERT_OK(gates_layout_set(t, toolbar, GATES_LAYOUT_KIND_ROW));
    GT_ASSERT_OK(gates_layout_set_gap(t, toolbar, 2));
    gates_node_t b1 = GATES_NODE_NULL, b2 = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_button_create(t, toolbar, GATES_STR("A"), nullptr, nullptr, &b1));
    GT_ASSERT_OK(gates_button_create(t, toolbar, GATES_STR("B"), nullptr, nullptr, &b2));
    gates_node_t body = mk_panel(t, root);
    GT_ASSERT_OK(gates_layout_set_child_grow(t, body, 1));

    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 150 }, be));
    GT_ASSERT(gates_layout_validate(t, root));

    /* Toolbar height = button pref height; body takes the rest. */
    gates_i32 btn_h = M.line_height + 10;
    GT_ASSERT(gates_node_layout_rect(t, toolbar).h == btn_h);
    GT_ASSERT(gates_node_layout_rect(t, body).h == 150 - btn_h);
    GT_ASSERT(gates_node_layout_rect(t, b2).x ==
              gates_node_layout_rect(t, b1).x + gates_node_layout_rect(t, b1).w + 2);

    /* layout_run cleared the layout-dirty bit; a text change sets it again. */
    GT_ASSERT((gates_tree_dirty(t) & GATES_TREE_DIRTY_LAYOUT) == 0);
    GT_ASSERT_OK(gates_widget_set_text(t, b1, GATES_STR("AAAA")));
    GT_ASSERT((gates_tree_dirty(t) & GATES_TREE_DIRTY_LAYOUT) != 0);
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 150 }, be));
    GT_ASSERT((gates_tree_dirty(t) & GATES_TREE_DIRTY_LAYOUT) == 0);
    /* Wider button pushed its sibling. */
    GT_ASSERT(gates_node_layout_rect(t, b1).w == 4 * M.advance + 18);

    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    M = be->metrics(be->ctx, 0);
    test_widget_intrinsic_measure();
    test_row_layout_exact();
    test_grow_distribution();
    test_column_and_align();
    test_stack_and_absolute();
    test_nested_and_dirty();
    return gt_report("test_layout");
}
