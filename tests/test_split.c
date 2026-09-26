/* T013: split layout — ratio sizing, handle geometry, drag, clamping
 * (docs/tests/cases/T013-split.md). */
#include <gates/ui.h>
#include <gates/widget.h>
#include "gates_test.h"

static const gates_text_backend_t *be;
static const gates_theme_t *theme;

#define W 200
#define H 100
#define HANDLE GATES_SPLIT_HANDLE_PX

static gates_tree_t *make_split(gates_split_dir_t dir, gates_i32 ratio,
                                gates_node_t *a, gates_node_t *b) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set_split(t, root, dir, ratio));
    GT_ASSERT_OK(gates_panel_create(t, root, a));
    GT_ASSERT_OK(gates_panel_create(t, root, b));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ W, H }, be));
    return t;
}

static bool rect_eq(gates_rect_t r, gates_i32 x, gates_i32 y, gates_i32 w, gates_i32 h) {
    return r.x == x && r.y == y && r.w == w && r.h == h;
}

static void test_horizontal_exact(void) {
    gates_node_t a = GATES_NODE_NULL, b = GATES_NODE_NULL;
    gates_tree_t *t = make_split(GATES_SPLIT_HORIZONTAL, 500, &a, &b);
    gates_i32 total = W - HANDLE;      /* 194 */
    gates_i32 aw = total * 500 / 1000; /* 97 */

    GT_ASSERT(rect_eq(gates_node_layout_rect(t, a), 0, 0, aw, H));
    GT_ASSERT(rect_eq(gates_node_layout_rect(t, b), aw + HANDLE, 0, total - aw, H));
    /* Panes plus handle exactly fill the container, with no overlap. */
    GT_ASSERT(gates_node_layout_rect(t, a).w + HANDLE + gates_node_layout_rect(t, b).w == W);
    GT_ASSERT(gates_layout_validate(t, gates_tree_root(t)));

    gates_tree_destroy(t);
}

static void test_vertical_exact(void) {
    gates_node_t a = GATES_NODE_NULL, b = GATES_NODE_NULL;
    gates_tree_t *t = make_split(GATES_SPLIT_VERTICAL, 500, &a, &b);
    gates_i32 total = H - HANDLE;      /* 94 */
    gates_i32 ah = total * 500 / 1000; /* 47 */

    GT_ASSERT(rect_eq(gates_node_layout_rect(t, a), 0, 0, W, ah));
    GT_ASSERT(rect_eq(gates_node_layout_rect(t, b), 0, ah + HANDLE, W, total - ah));
    GT_ASSERT(gates_node_layout_rect(t, a).h + HANDLE + gates_node_layout_rect(t, b).h == H);
    GT_ASSERT(gates_layout_validate(t, gates_tree_root(t)));

    gates_tree_destroy(t);
}

static void test_ratio_and_min_clamp(void) {
    gates_node_t a = GATES_NODE_NULL, b = GATES_NODE_NULL;
    gates_tree_t *t = make_split(GATES_SPLIT_HORIZONTAL, 250, &a, &b);
    gates_i32 total = W - HANDLE;
    GT_ASSERT(gates_node_layout_rect(t, a).w == total * 250 / 1000); /* 48 */

    /* A tiny ratio still leaves the minimum pane width. */
    GT_ASSERT_OK(gates_layout_set_split(t, gates_tree_root(t), GATES_SPLIT_HORIZONTAL, 1));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ W, H }, be));
    GT_ASSERT(gates_node_layout_rect(t, a).w == GATES_SPLIT_MIN_PANE_PX);
    GT_ASSERT(gates_node_layout_rect(t, b).w == total - GATES_SPLIT_MIN_PANE_PX);

    /* And symmetrically at the other end. */
    GT_ASSERT_OK(gates_layout_set_split(t, gates_tree_root(t), GATES_SPLIT_HORIZONTAL, 999));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ W, H }, be));
    GT_ASSERT(gates_node_layout_rect(t, b).w == GATES_SPLIT_MIN_PANE_PX);

    /* Out-of-range ratios are rejected outright. */
    GT_ASSERT_ERR(gates_layout_set_split(t, gates_tree_root(t), GATES_SPLIT_HORIZONTAL, 0));
    GT_ASSERT_ERR(gates_layout_set_split(t, gates_tree_root(t), GATES_SPLIT_HORIZONTAL, 1000));

    gates_tree_destroy(t);
}

static void test_validator_requires_two_panes(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set_split(t, root, GATES_SPLIT_HORIZONTAL, 500));

    gates_node_t a = GATES_NODE_NULL, b = GATES_NODE_NULL, c = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_panel_create(t, root, &a));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ W, H }, be));
    GT_ASSERT(!gates_layout_validate(t, root)); /* one pane: invalid */

    GT_ASSERT_OK(gates_panel_create(t, root, &b));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ W, H }, be));
    GT_ASSERT(gates_layout_validate(t, root));  /* two panes: valid */

    GT_ASSERT_OK(gates_panel_create(t, root, &c));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ W, H }, be));
    GT_ASSERT(!gates_layout_validate(t, root)); /* three panes: invalid */
    /* The extra child is parked empty rather than left with a stale rect. */
    GT_ASSERT(gates_rect_is_empty(gates_node_layout_rect(t, c)));

    gates_tree_destroy(t);
}

static void test_handle_drag(void) {
    gates_node_t a = GATES_NODE_NULL, b = GATES_NODE_NULL;
    gates_tree_t *t = make_split(GATES_SPLIT_HORIZONTAL, 500, &a, &b);
    gates_node_t root = gates_tree_root(t);
    gates_i32 total = W - HANDLE;
    gates_i32 aw0 = gates_node_layout_rect(t, a).w; /* 97 */

    /* The handle sits between the panes and belongs to the container. */
    GT_ASSERT(gates_node_eq(gates_hit_test(t, (gates_point_t){ aw0 + 2, 50 }), root));

    gates_pointer_event_t down = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT,
                                   .pos = { aw0 + 2, 50 } };
    (void)gates_input_pointer(t, &down);
    gates_pointer_event_t move = { .action = GATES_POINTER_MOVE, .pos = { aw0 + 42, 50 } };
    (void)gates_input_pointer(t, &move);
    GT_ASSERT((gates_tree_dirty(t) & GATES_TREE_DIRTY_LAYOUT) != 0);
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ W, H }, be));
    /* Pane A grew by the drag delta (within per-mille rounding). */
    gates_i32 aw1 = gates_node_layout_rect(t, a).w;
    GT_ASSERT(aw1 >= aw0 + 39 && aw1 <= aw0 + 40);
    GT_ASSERT(aw1 + HANDLE + gates_node_layout_rect(t, b).w == W);

    /* Dragging far left clamps at the minimum pane. */
    gates_pointer_event_t far_left = { .action = GATES_POINTER_MOVE, .pos = { -900, 50 } };
    (void)gates_input_pointer(t, &far_left);
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ W, H }, be));
    GT_ASSERT(gates_node_layout_rect(t, a).w == GATES_SPLIT_MIN_PANE_PX);

    /* Far right clamps the other pane. */
    gates_pointer_event_t far_right = { .action = GATES_POINTER_MOVE, .pos = { 900, 50 } };
    (void)gates_input_pointer(t, &far_right);
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ W, H }, be));
    GT_ASSERT(gates_node_layout_rect(t, b).w == GATES_SPLIT_MIN_PANE_PX);
    GT_ASSERT(gates_node_layout_rect(t, a).w == total - GATES_SPLIT_MIN_PANE_PX);

    /* Release ends the drag: later moves leave the ratio alone. */
    gates_pointer_event_t up = { .action = GATES_POINTER_UP, .button = GATES_BUTTON_LEFT,
                                 .pos = { 900, 50 } };
    (void)gates_input_pointer(t, &up);
    gates_i32 ratio = gates_layout_split_ratio(t, root);
    gates_pointer_event_t after = { .action = GATES_POINTER_MOVE, .pos = { 20, 50 } };
    (void)gates_input_pointer(t, &after);
    GT_ASSERT(gates_layout_split_ratio(t, root) == ratio);

    gates_tree_destroy(t);
}

static void test_drag_owns_the_event_stream(void) {
    /* While the handle is being dragged, widgets must not take hover/press. */
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set_split(t, root, GATES_SPLIT_HORIZONTAL, 500));
    gates_node_t left = GATES_NODE_NULL, right = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_panel_create(t, root, &left));
    GT_ASSERT_OK(gates_panel_create(t, root, &right));
    GT_ASSERT_OK(gates_layout_set(t, right, GATES_LAYOUT_KIND_COLUMN));
    gates_node_t btn = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_button_create(t, right, GATES_STR("B"), nullptr, nullptr, &btn));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ W, H }, be));

    gates_i32 aw = gates_node_layout_rect(t, left).w;
    gates_rect_t br = gates_node_layout_rect(t, btn);

    gates_pointer_event_t down = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT,
                                   .pos = { aw + 2, 50 } };
    (void)gates_input_pointer(t, &down);
    gates_pointer_event_t over_btn = { .action = GATES_POINTER_MOVE,
                                       .pos = { br.x + 2, br.y + 2 } };
    (void)gates_input_pointer(t, &over_btn);
    GT_ASSERT(!gates_widget_hovered(t, btn));
    GT_ASSERT(!gates_widget_pressed(t, btn));

    /* After release the button behaves normally again. */
    gates_pointer_event_t up = { .action = GATES_POINTER_UP, .button = GATES_BUTTON_LEFT,
                                 .pos = { br.x + 2, br.y + 2 } };
    (void)gates_input_pointer(t, &up);
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ W, H }, be));
    br = gates_node_layout_rect(t, btn);
    gates_pointer_event_t hover = { .action = GATES_POINTER_MOVE,
                                    .pos = { br.x + 2, br.y + 2 } };
    (void)gates_input_pointer(t, &hover);
    GT_ASSERT(gates_widget_hovered(t, btn));

    gates_tree_destroy(t);
}

static void test_paint_emits_handle(void) {
    gates_node_t a = GATES_NODE_NULL, b = GATES_NODE_NULL;
    gates_tree_t *t = make_split(GATES_SPLIT_HORIZONTAL, 500, &a, &b);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));

    /* Last two commands are the handle fill + border, in the handle rect. */
    gates_u32 n = gates_draw_list_len(&dl);
    const gates_draw_cmd_t *border = gates_draw_list_at(&dl, n - 1);
    const gates_draw_cmd_t *fill = gates_draw_list_at(&dl, n - 2);
    GT_ASSERT(border->kind == GATES_DRAW_BORDER && fill->kind == GATES_DRAW_RECT);
    GT_ASSERT(fill->rect.w == HANDLE && fill->rect.h == H);
    GT_ASSERT(fill->rect.x == gates_node_layout_rect(t, a).w);
    GT_ASSERT(gates_draw_list_balanced(&dl));

    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_horizontal_exact();
    test_vertical_exact();
    test_ratio_and_min_clamp();
    test_validator_requires_two_panes();
    test_handle_drag();
    test_drag_owns_the_event_stream();
    test_paint_emits_handle();
    return gt_report("test_split");
}
