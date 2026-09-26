/* T012: scroll layout — offset, clamping, wheel, clipping, scrollbar
 * (docs/tests/cases/T012-scroll.md). */
#include <gates/ui.h>
#include <gates/widget.h>
#include "gates_test.h"

#define VIEW_W 100
#define VIEW_H 50
#define ROW_H 16   /* builtin backend line_height */

static const gates_text_backend_t *be;
static const gates_theme_t *theme;

static gates_tree_t *make_scroll_tree(int rows, gates_node_t *out_children) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_SCROLL));
    for (int i = 0; i < rows; i++) {
        gates_node_t lbl = GATES_NODE_NULL;
        GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("row"), &lbl));
        if (out_children != nullptr) {
            out_children[i] = lbl;
        }
    }
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VIEW_W, VIEW_H }, be));
    return t;
}

static gates_pointer_event_t ev_wheel(gates_i32 x, gates_i32 y, float notches) {
    return (gates_pointer_event_t){ .action = GATES_POINTER_WHEEL, .pos = { x, y },
                                    .wheel = { 0.0f, notches } };
}

static int count_kind(const gates_draw_list_t *dl, gates_draw_kind_t kind) {
    int n = 0;
    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        if (gates_draw_list_at(dl, i)->kind == kind) n++;
    }
    return n;
}

static void test_content_measure_and_offset(void) {
    gates_node_t kids[10];
    gates_tree_t *t = make_scroll_tree(10, kids);
    gates_node_t root = gates_tree_root(t);

    /* Content is measured (10 rows) but the viewport does not grow with it. */
    GT_ASSERT(gates_layout_scroll_content(t, root).h == 10 * ROW_H);
    GT_ASSERT(gates_node_layout_rect(t, root).h == VIEW_H);

    /* Scrollbar narrows the content width. */
    GT_ASSERT(gates_node_layout_rect(t, kids[0]).w == VIEW_W - GATES_SCROLLBAR_PX);

    /* Offset 0: rows stack from the top. */
    GT_ASSERT(gates_node_layout_rect(t, kids[0]).y == 0);
    GT_ASSERT(gates_node_layout_rect(t, kids[3]).y == 3 * ROW_H);

    /* Offset shifts every row up by exactly the offset. */
    GT_ASSERT_OK(gates_layout_set_scroll_offset(t, root, 20));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VIEW_W, VIEW_H }, be));
    GT_ASSERT(gates_layout_scroll_offset(t, root) == 20);
    GT_ASSERT(gates_node_layout_rect(t, kids[0]).y == -20);
    GT_ASSERT(gates_node_layout_rect(t, kids[3]).y == 3 * ROW_H - 20);
    GT_ASSERT(gates_layout_validate(t, root));

    gates_tree_destroy(t);
}

static void test_offset_clamping(void) {
    gates_tree_t *t = make_scroll_tree(10, nullptr);
    gates_node_t root = gates_tree_root(t);
    gates_i32 max_off = 10 * ROW_H - VIEW_H; /* 110 */

    GT_ASSERT_OK(gates_layout_set_scroll_offset(t, root, 5000));
    GT_ASSERT(gates_layout_scroll_offset(t, root) == max_off);

    GT_ASSERT_OK(gates_layout_set_scroll_offset(t, root, -50));
    GT_ASSERT(gates_layout_scroll_offset(t, root) == 0);

    gates_tree_destroy(t);
}

static void test_shrinking_content_reclamps(void) {
    gates_node_t kids[10];
    gates_tree_t *t = make_scroll_tree(10, kids);
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set_scroll_offset(t, root, 110));
    GT_ASSERT(gates_layout_scroll_offset(t, root) == 110);

    /* Remove rows until the content fits: the stale offset must reset. */
    for (int i = 2; i < 10; i++) {
        GT_ASSERT_OK(gates_node_destroy(t, kids[i]));
    }
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VIEW_W, VIEW_H }, be));

    GT_ASSERT(gates_layout_scroll_content(t, root).h == 2 * ROW_H);
    GT_ASSERT(gates_layout_scroll_offset(t, root) == 0);
    /* Not scrollable any more: full width, rows back at the top. */
    GT_ASSERT(gates_node_layout_rect(t, kids[0]).w == VIEW_W);
    GT_ASSERT(gates_node_layout_rect(t, kids[0]).y == 0);

    gates_tree_destroy(t);
}

static void test_wheel_scrolls(void) {
    gates_tree_t *t = make_scroll_tree(10, nullptr);
    gates_node_t root = gates_tree_root(t);
    gates_i32 step = GATES_SCROLL_WHEEL_LINES * ROW_H; /* 48 */

    /* Wheel down (negative notches, as Win32 reports) scrolls content up. */
    gates_pointer_event_t down = ev_wheel(10, 10, -1.0f);
    (void)gates_input_pointer(t, &down);
    GT_ASSERT(gates_layout_scroll_offset(t, root) == step);
    GT_ASSERT((gates_tree_dirty(t) & GATES_TREE_DIRTY_LAYOUT) != 0);

    /* Wheel up returns to the top and clamps there. */
    gates_pointer_event_t up = ev_wheel(10, 10, 1.0f);
    (void)gates_input_pointer(t, &up);
    GT_ASSERT(gates_layout_scroll_offset(t, root) == 0);
    (void)gates_input_pointer(t, &up);
    GT_ASSERT(gates_layout_scroll_offset(t, root) == 0);

    /* Wheel over a non-scrollable tree does nothing (no crash, no change). */
    gates_tree_t *small = make_scroll_tree(2, nullptr);
    gates_pointer_event_t w = ev_wheel(10, 10, -1.0f);
    (void)gates_input_pointer(small, &w);
    GT_ASSERT(gates_layout_scroll_offset(small, gates_tree_root(small)) == 0);
    gates_tree_destroy(small);

    gates_tree_destroy(t);
}

static void test_scrolled_out_child_not_hittable(void) {
    gates_node_t kids[10];
    gates_tree_t *t = make_scroll_tree(10, kids);
    gates_node_t root = gates_tree_root(t);

    gates_point_t p = { 5, 5 };
    GT_ASSERT(gates_node_eq(gates_hit_test(t, p), kids[0]));

    GT_ASSERT_OK(gates_layout_set_scroll_offset(t, root, 112)); /* clamps to 110 */
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VIEW_W, VIEW_H }, be));
    /* Row 0 is now above the viewport; the row under the cursor is row 6/7. */
    GT_ASSERT(gates_node_layout_rect(t, kids[0]).y < 0);
    gates_node_t hit = gates_hit_test(t, p);
    GT_ASSERT(!gates_node_eq(hit, kids[0]));
    GT_ASSERT(gates_node_eq(hit, kids[6]) || gates_node_eq(hit, kids[7]));
    /* A point on the scrollbar belongs to the scroll container, not a row. */
    GT_ASSERT(gates_node_eq(gates_hit_test(t, (gates_point_t){ VIEW_W - 3, 10 }), root));

    gates_tree_destroy(t);
}

static void test_paint_clips_and_draws_scrollbar(void) {
    gates_tree_t *t = make_scroll_tree(10, nullptr);
    gates_node_t root = gates_tree_root(t);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));

    /* Content is clipped to the viewport, and the list stays balanced. */
    GT_ASSERT(count_kind(&dl, GATES_DRAW_CLIP_PUSH) == 1);
    GT_ASSERT(count_kind(&dl, GATES_DRAW_CLIP_POP) == 1);
    GT_ASSERT(gates_draw_list_balanced(&dl));

    /* The clip is the viewport (content minus the scrollbar strip). */
    const gates_draw_cmd_t *clip = nullptr;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        if (gates_draw_list_at(&dl, i)->kind == GATES_DRAW_CLIP_PUSH) {
            clip = gates_draw_list_at(&dl, i);
        }
    }
    GT_ASSERT(clip != nullptr);
    GT_ASSERT(clip->rect.w == VIEW_W - GATES_SCROLLBAR_PX && clip->rect.h == VIEW_H);

    /* Track + thumb are emitted after the content; thumb is proportional. */
    const gates_draw_cmd_t *thumb = gates_draw_list_at(&dl, gates_draw_list_len(&dl) - 1);
    GT_ASSERT(thumb->kind == GATES_DRAW_RECT);
    GT_ASSERT(thumb->rect.w == GATES_SCROLLBAR_PX);
    GT_ASSERT(thumb->rect.h == VIEW_H * VIEW_H / (10 * ROW_H)); /* 50*50/160 = 15 */
    GT_ASSERT(thumb->rect.y == 0); /* at the top with offset 0 */

    /* Scrolling to the end puts the thumb at the bottom of the track. */
    GT_ASSERT_OK(gates_layout_set_scroll_offset(t, root, 110));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VIEW_W, VIEW_H }, be));
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    thumb = gates_draw_list_at(&dl, gates_draw_list_len(&dl) - 1);
    GT_ASSERT(thumb->rect.y + thumb->rect.h == VIEW_H);

    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

static void test_no_scrollbar_when_content_fits(void) {
    gates_tree_t *t = make_scroll_tree(2, nullptr);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));

    /* Window background + 2 text rows only: no track, no thumb. */
    GT_ASSERT(count_kind(&dl, GATES_DRAW_RECT) == 1);
    GT_ASSERT(count_kind(&dl, GATES_DRAW_TEXT) == 2);
    GT_ASSERT(count_kind(&dl, GATES_DRAW_CLIP_PUSH) == 1); /* still clipped */

    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

static void test_thumb_drag(void) {
    gates_tree_t *t = make_scroll_tree(10, nullptr);
    gates_node_t root = gates_tree_root(t);

    /* Press on the thumb (top of the scrollbar), drag halfway down the track. */
    gates_pointer_event_t down = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT,
                                   .pos = { VIEW_W - 5, 3 } };
    (void)gates_input_pointer(t, &down);
    gates_pointer_event_t move = { .action = GATES_POINTER_MOVE, .pos = { VIEW_W - 5, 20 } };
    (void)gates_input_pointer(t, &move);
    gates_i32 mid = gates_layout_scroll_offset(t, root);
    GT_ASSERT(mid > 0 && mid < 110);

    /* Dragging far past the end clamps at max. */
    gates_pointer_event_t far = { .action = GATES_POINTER_MOVE, .pos = { VIEW_W - 5, 999 } };
    (void)gates_input_pointer(t, &far);
    GT_ASSERT(gates_layout_scroll_offset(t, root) == 110);

    /* Release ends the drag; later moves no longer scroll. */
    gates_pointer_event_t up = { .action = GATES_POINTER_UP, .button = GATES_BUTTON_LEFT,
                                 .pos = { VIEW_W - 5, 999 } };
    (void)gates_input_pointer(t, &up);
    gates_pointer_event_t after = { .action = GATES_POINTER_MOVE, .pos = { VIEW_W - 5, 0 } };
    (void)gates_input_pointer(t, &after);
    GT_ASSERT(gates_layout_scroll_offset(t, root) == 110);

    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_content_measure_and_offset();
    test_offset_clamping();
    test_shrinking_content_reclamps();
    test_wheel_scrolls();
    test_scrolled_out_child_not_hittable();
    test_paint_clips_and_draws_scrollbar();
    test_no_scrollbar_when_content_fits();
    test_thumb_drag();
    return gt_report("test_scroll");
}
