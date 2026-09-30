/* scroll layout - offset, clamping, wheel, clipping, scrollbar. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/access.h>
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

/* 0.8.0: a press on the track above or below the thumb pages. */
static void test_track_pages(void) {
    gates_tree_t *t = make_scroll_tree(10, nullptr);
    gates_node_t root = gates_tree_root(t);
    gates_pointer_event_t down = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT,
                                   .pos = { VIEW_W - 5, VIEW_H - 3 } };
    gates_pointer_event_t up = down;
    up.action = GATES_POINTER_UP;
    (void)gates_input_pointer(t, &down);
    (void)gates_input_pointer(t, &up);
    GT_ASSERT(gates_layout_scroll_offset(t, root) == VIEW_H);
    (void)gates_input_pointer(t, &down);
    (void)gates_input_pointer(t, &up);
    (void)gates_input_pointer(t, &down);
    (void)gates_input_pointer(t, &up);
    GT_ASSERT(gates_layout_scroll_offset(t, root) == 110); /* clamped */
    down.pos.y = 1;
    up.pos.y = 1;
    (void)gates_input_pointer(t, &down);
    (void)gates_input_pointer(t, &up);
    GT_ASSERT(gates_layout_scroll_offset(t, root) == 110 - VIEW_H);
    gates_tree_destroy(t);
}

/* 0.10.0: a scroll area that scrolls sideways. Row 0 is a panel (a row of two
 * buttons) wider than the view; one label under it (42 px in all: taller than
 * what a bottom bar leaves, not taller than the view). */
typedef struct side_t {
    gates_tree_t *t;
    gates_node_t root, row, near, far, lbl[1];
} side_t;

static void side_layout(side_t *x) { GT_ASSERT_OK(gates_layout_run(x->t, (gates_size_t){ VIEW_W, VIEW_H }, be)); }

static side_t make_side(bool on) {
    side_t x = {0};
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &x.t));
    x.root = gates_tree_root(x.t);
    GT_ASSERT_OK(gates_layout_set(x.t, x.root, GATES_LAYOUT_KIND_SCROLL));
    GT_ASSERT_OK(gates_panel_create(x.t, x.root, &x.row));
    GT_ASSERT_OK(gates_layout_set(x.t, x.row, GATES_LAYOUT_KIND_ROW));
    GT_ASSERT_OK(gates_button_create(x.t, x.row, GATES_STR("near button"), nullptr, nullptr, &x.near));
    GT_ASSERT_OK(gates_button_create(x.t, x.row, GATES_STR("far"), nullptr, nullptr, &x.far));
    GT_ASSERT_OK(gates_label_create(x.t, x.root, GATES_STR("row"), &x.lbl[0]));
    if (on) GT_ASSERT_OK(gates_layout_set_scroll_sideways(x.t, x.root, true));
    side_layout(&x);
    return x;
}

static gates_i32 side_max(const side_t *x) {
    return gates_layout_scroll_content(x->t, x->root).w - (VIEW_W - GATES_SCROLLBAR_PX);
}

static void test_sideways_off_by_default(void) {
    side_t x = make_side(false);
    GT_ASSERT(!gates_layout_scroll_sideways(x.t, x.root));
    GT_ASSERT(gates_layout_scroll_content(x.t, x.root).w > VIEW_W); /* measured all the same */
    /* Children as wide as the viewport, as before; no offset across. */
    gates_i32 w = gates_node_layout_rect(x.t, x.lbl[0]).w;
    GT_ASSERT(w == VIEW_W || w == VIEW_W - GATES_SCROLLBAR_PX);
    GT_ASSERT_OK(gates_layout_set_scroll_x(x.t, x.root, 30));
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == 0);
    gates_u32 pos = 0, page = 0;
    GT_ASSERT(!gates_access_hscroll_info(x.t, x.root, &pos, &page));
    GT_ASSERT(gates_access_hscroll_to(x.t, x.root, 5000) == PROVEN_ERR_INVALID_ARG);
    gates_access_info_t info = {0};
    GT_ASSERT_OK(gates_access_info(x.t, x.root, 0, &info));
    GT_ASSERT((info.actions & GATES_ACCESS_SCROLL) == 0); /* 42 px fit in 50 */
    gates_tree_destroy(x.t);
}

static void test_sideways_geometry(void) {
    side_t x = make_side(true);
    GT_ASSERT(gates_layout_scroll_sideways(x.t, x.root));
    gates_size_t content = gates_layout_scroll_content(x.t, x.root);
    /* The bottom bar leaves 40 px, too short for the rows: both bars show. */
    GT_ASSERT(content.h <= VIEW_H && content.h > VIEW_H - GATES_SCROLLBAR_PX);
    GT_ASSERT(gates_layout_scroll_offset(x.t, x.root) == 0);
    GT_ASSERT_OK(gates_layout_set_scroll_offset(x.t, x.root, 999));
    GT_ASSERT(gates_layout_scroll_offset(x.t, x.root) == content.h - (VIEW_H - GATES_SCROLLBAR_PX));
    GT_ASSERT_OK(gates_layout_set_scroll_offset(x.t, x.root, 0));
    side_layout(&x);
    /* Every child as wide as the widest. */
    GT_ASSERT(gates_node_layout_rect(x.t, x.lbl[0]).w == content.w);
    GT_ASSERT(gates_node_layout_rect(x.t, x.row).w == content.w);
    GT_ASSERT(gates_node_layout_rect(x.t, x.lbl[0]).x == 0);
    /* The offset clamps to what does not fit and moves every child. */
    GT_ASSERT_OK(gates_layout_set_scroll_x(x.t, x.root, 99999));
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == side_max(&x));
    GT_ASSERT_OK(gates_layout_set_scroll_x(x.t, x.root, -5));
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == 0);
    GT_ASSERT_OK(gates_layout_set_scroll_x(x.t, x.root, 7));
    side_layout(&x);
    GT_ASSERT(gates_node_layout_rect(x.t, x.lbl[0]).x == -7);
    GT_ASSERT(gates_node_layout_rect(x.t, x.row).x == -7);
    /* A point on the bottom bar belongs to the area, not the label over it. */
    gates_rect_t last = gates_node_layout_rect(x.t, x.lbl[0]);
    GT_ASSERT(last.y + last.h > VIEW_H - GATES_SCROLLBAR_PX);
    GT_ASSERT(gates_node_eq(gates_hit_test(x.t, (gates_point_t){ 5, VIEW_H - GATES_SCROLLBAR_PX + 1 }), x.root));
    GT_ASSERT(gates_node_eq(gates_hit_test(x.t, (gates_point_t){ 5, last.y + 1 }), x.lbl[0]));
    /* Off again: back to the viewport's width, offset gone. */
    GT_ASSERT_OK(gates_layout_set_scroll_sideways(x.t, x.root, false));
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == 0);
    side_layout(&x);
    GT_ASSERT(gates_node_layout_rect(x.t, x.lbl[0]).x == 0);
    GT_ASSERT(gates_node_layout_rect(x.t, x.lbl[0]).w < content.w);
    gates_tree_destroy(x.t);
}

static void test_sideways_narrow_content(void) {
    /* On, but everything fits: no bottom bar, no vertical one, nothing moves. */
    gates_tree_t *t = make_scroll_tree(2, nullptr);
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set_scroll_sideways(t, root, true));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VIEW_W, VIEW_H }, be));
    GT_ASSERT_OK(gates_layout_set_scroll_x(t, root, 5));
    GT_ASSERT(gates_layout_scroll_x(t, root) == 0);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    GT_ASSERT(count_kind(&dl, GATES_DRAW_RECT) == 1);
    gates_draw_list_deinit(&dl);
    /* Tall but narrow: the vertical bar only, rows the viewport's width. */
    gates_node_t kids[10];
    gates_tree_t *t2 = make_scroll_tree(10, kids);
    GT_ASSERT_OK(gates_layout_set_scroll_sideways(t2, gates_tree_root(t2), true));
    GT_ASSERT_OK(gates_layout_run(t2, (gates_size_t){ VIEW_W, VIEW_H }, be));
    GT_ASSERT(gates_node_layout_rect(t2, kids[0]).w == VIEW_W - GATES_SCROLLBAR_PX);
    GT_ASSERT_OK(gates_layout_set_scroll_offset(t2, gates_tree_root(t2), 999));
    GT_ASSERT(gates_layout_scroll_offset(t2, gates_tree_root(t2)) == 10 * ROW_H - VIEW_H);
    gates_tree_destroy(t2);
    gates_tree_destroy(t);
}

static void test_sideways_paint(void) {
    side_t x = make_side(true);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(x.t, &dl, theme, be));
    const gates_draw_cmd_t *clip = nullptr;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        if (gates_draw_list_at(&dl, i)->kind == GATES_DRAW_CLIP_PUSH) clip = gates_draw_list_at(&dl, i);
    }
    GT_ASSERT(clip != nullptr);
    GT_ASSERT(clip->rect.w == VIEW_W - GATES_SCROLLBAR_PX && clip->rect.h == VIEW_H - GATES_SCROLLBAR_PX);
    /* Vertical track and thumb, then the bottom track and thumb. */
    gates_u32 n = gates_draw_list_len(&dl);
    const gates_draw_cmd_t *vtrack = gates_draw_list_at(&dl, n - 4), *htrack = gates_draw_list_at(&dl, n - 2);
    const gates_draw_cmd_t *hthumb = gates_draw_list_at(&dl, n - 1);
    GT_ASSERT(vtrack->kind == GATES_DRAW_RECT && vtrack->rect.x == VIEW_W - GATES_SCROLLBAR_PX);
    GT_ASSERT(vtrack->rect.h == VIEW_H - GATES_SCROLLBAR_PX); /* stops above the corner */
    GT_ASSERT(htrack->rect.x == 0 && htrack->rect.y == VIEW_H - GATES_SCROLLBAR_PX);
    GT_ASSERT(htrack->rect.w == VIEW_W - GATES_SCROLLBAR_PX && htrack->rect.h == GATES_SCROLLBAR_PX);
    gates_i32 cw = gates_layout_scroll_content(x.t, x.root).w;
    gates_i32 vw = VIEW_W - GATES_SCROLLBAR_PX;
    gates_i32 tw = vw * vw / cw;
    if (tw < GATES_SCROLLBAR_PX) tw = GATES_SCROLLBAR_PX;
    GT_ASSERT(hthumb->rect.x == 0 && hthumb->rect.w == tw && hthumb->rect.y == htrack->rect.y);
    /* At the end the thumb touches the track's end. */
    GT_ASSERT_OK(gates_layout_set_scroll_x(x.t, x.root, side_max(&x)));
    side_layout(&x);
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(x.t, &dl, theme, be));
    hthumb = gates_draw_list_at(&dl, gates_draw_list_len(&dl) - 1);
    GT_ASSERT(hthumb->rect.x + hthumb->rect.w == vw);
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(x.t);
}

static void pointer(gates_tree_t *t, gates_pointer_action_t a, gates_i32 px, gates_i32 py) {
    gates_pointer_event_t ev = { .action = a, .button = GATES_BUTTON_LEFT, .pos = { px, py } };
    (void)gates_input_pointer(t, &ev);
}

static void test_sideways_pointer(void) {
    side_t x = make_side(true);
    GT_ASSERT_OK(gates_widget_set_text(x.t, x.far, GATES_STR("a far button, wider than the view")));
    side_layout(&x);
    /* On the bar, where the label reaches under it. */
    gates_i32 max = side_max(&x), bar_y = VIEW_H - GATES_SCROLLBAR_PX + 1;
    GT_ASSERT(max > 2 * VIEW_W);
    /* Drag the bottom thumb: part way, then far past the end, then released. */
    pointer(x.t, GATES_POINTER_DOWN, 2, bar_y);
    pointer(x.t, GATES_POINTER_MOVE, 20, bar_y);
    gates_i32 mid = gates_layout_scroll_x(x.t, x.root);
    GT_ASSERT(mid > 0 && mid < max);
    pointer(x.t, GATES_POINTER_MOVE, 20, 0); /* only x counts */
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == mid);
    pointer(x.t, GATES_POINTER_MOVE, 999, bar_y);
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == max);
    pointer(x.t, GATES_POINTER_UP, 999, bar_y);
    pointer(x.t, GATES_POINTER_MOVE, 0, bar_y);
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == max);
    GT_ASSERT(gates_layout_scroll_offset(x.t, x.root) == 0); /* up and down untouched */
    /* A press on the track pages across toward it. */
    side_layout(&x);
    pointer(x.t, GATES_POINTER_DOWN, 1, bar_y);
    pointer(x.t, GATES_POINTER_UP, 1, bar_y);
    gates_i32 page = VIEW_W - GATES_SCROLLBAR_PX;
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == (max > page ? max - page : 0));
    GT_ASSERT_OK(gates_layout_set_scroll_x(x.t, x.root, 0));
    side_layout(&x);
    pointer(x.t, GATES_POINTER_DOWN, VIEW_W - GATES_SCROLLBAR_PX - 2, bar_y);
    pointer(x.t, GATES_POINTER_UP, VIEW_W - GATES_SCROLLBAR_PX - 2, bar_y);
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == (max < page ? max : page));
    /* The wheel's sideways motion; positive goes right, clamped both ways. */
    GT_ASSERT_OK(gates_layout_set_scroll_x(x.t, x.root, 0));
    side_layout(&x);
    gates_pointer_event_t w = { .action = GATES_POINTER_WHEEL, .pos = { 5, 30 }, .wheel = { 0.25f, 0.0f } };
    (void)gates_input_pointer(x.t, &w);
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == GATES_SCROLL_WHEEL_LINES * ROW_H / 4);
    GT_ASSERT(gates_layout_scroll_offset(x.t, x.root) == 0);
    w.wheel.x = 100.0f;
    (void)gates_input_pointer(x.t, &w);
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == max);
    w.wheel.x = -100.0f;
    (void)gates_input_pointer(x.t, &w);
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == 0);
    /* Up and down still works on the same wheel event. */
    w.wheel = (gates_vec2_t){ 0.0f, -1.0f };
    (void)gates_input_pointer(x.t, &w);
    GT_ASSERT(gates_layout_scroll_offset(x.t, x.root) > 0);
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == 0);
    gates_tree_destroy(x.t);
}

static void test_sideways_focus_and_access(void) {
    side_t x = make_side(true);
    gates_i32 max = side_max(&x);
    /* Tab to the far button brings its right edge into view; back, the start. */
    gates_tree_set_focus(x.t, x.near);
    GT_ASSERT(gates_tree_focus_next(x.t, false));
    GT_ASSERT(gates_node_eq(gates_tree_focus(x.t), x.far));
    side_layout(&x);
    gates_rect_t far = gates_node_layout_rect(x.t, x.far);
    GT_ASSERT(far.w < VIEW_W - GATES_SCROLLBAR_PX);
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) > 0);
    GT_ASSERT(far.x + far.w == VIEW_W - GATES_SCROLLBAR_PX);
    GT_ASSERT(gates_tree_focus_next(x.t, true));
    side_layout(&x);
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == 0);
    GT_ASSERT(gates_node_layout_rect(x.t, x.near).x == 0);
    /* A control wider than the view shows its start. */
    GT_ASSERT_OK(gates_widget_set_text(x.t, x.far, GATES_STR("a far button, wider than the view")));
    side_layout(&x);
    GT_ASSERT(gates_tree_focus_next(x.t, false));
    side_layout(&x);
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) > 0);
    GT_ASSERT(gates_node_layout_rect(x.t, x.far).x == 0);
    GT_ASSERT(gates_tree_focus_next(x.t, true));
    side_layout(&x);
    max = side_max(&x);
    /* Assistive technology: position and page in 1/10000, and set. */
    gates_access_info_t info = {0};
    GT_ASSERT_OK(gates_access_info(x.t, x.root, 0, &info));
    GT_ASSERT((info.actions & GATES_ACCESS_SCROLL) != 0);
    gates_u32 pos = 1, page = 0;
    GT_ASSERT(gates_access_hscroll_info(x.t, x.root, &pos, &page));
    gates_i32 cw = gates_layout_scroll_content(x.t, x.root).w;
    GT_ASSERT(pos == 0 && page == (gates_u32)((VIEW_W - GATES_SCROLLBAR_PX) * 10000 / cw));
    GT_ASSERT(!gates_access_hscroll_info(x.t, x.root, nullptr, &page));
    GT_ASSERT_OK(gates_access_hscroll_to(x.t, x.root, 5000));
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == max / 2);
    GT_ASSERT(gates_access_hscroll_info(x.t, x.root, &pos, &page) && pos == (gates_u32)(max / 2 * 10000 / max));
    GT_ASSERT_OK(gates_access_hscroll_to(x.t, x.root, 20000)); /* clamped */
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == max);
    GT_ASSERT(gates_access_hscroll_info(x.t, x.root, &pos, &page) && pos == 10000);
    /* A plain panel has nothing to scroll. */
    GT_ASSERT(!gates_access_hscroll_info(x.t, x.row, &pos, &page));
    gates_tree_destroy(x.t);
}

static void test_sideways_bars(void) {
    /* A vertical bar can make the content too wide: 10 tall rows and one line
     * a little wider than what the vertical bar leaves. */
    gates_node_t kids[10];
    gates_tree_t *t = make_scroll_tree(10, kids);
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set_scroll_sideways(t, root, true));
    GT_ASSERT_OK(gates_widget_set_text(t, kids[0], GATES_STR("rowrowrowrow")));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VIEW_W, VIEW_H }, be));
    gates_i32 cw = gates_layout_scroll_content(t, root).w;
    GT_ASSERT(cw > VIEW_W - GATES_SCROLLBAR_PX && cw <= VIEW_W);
    GT_ASSERT_OK(gates_layout_set_scroll_x(t, root, 999));
    GT_ASSERT(gates_layout_scroll_x(t, root) == cw - (VIEW_W - GATES_SCROLLBAR_PX));
    GT_ASSERT_OK(gates_layout_set_scroll_offset(t, root, 999));
    GT_ASSERT(gates_layout_scroll_offset(t, root) == 10 * ROW_H - (VIEW_H - GATES_SCROLLBAR_PX));
    gates_tree_destroy(t);

    /* Only the bottom bar: short content. It alone lets assistive technology scroll. */
    side_t x = make_side(true);
    GT_ASSERT_OK(gates_node_set_hidden(x.t, x.lbl[0], true));
    side_layout(&x);
    GT_ASSERT_OK(gates_layout_set_scroll_offset(x.t, x.root, 5));
    GT_ASSERT(gates_layout_scroll_offset(x.t, x.root) == 0);
    GT_ASSERT_OK(gates_layout_set_scroll_x(x.t, x.root, 999));
    GT_ASSERT(gates_layout_scroll_x(x.t, x.root) == gates_layout_scroll_content(x.t, x.root).w - VIEW_W);
    gates_access_info_t info = {0};
    GT_ASSERT_OK(gates_access_info(x.t, x.root, 0, &info));
    GT_ASSERT((info.actions & GATES_ACCESS_SCROLL) != 0);
    gates_u32 pos = 0, page = 0;
    GT_ASSERT(!gates_access_scroll_info(x.t, x.root, &pos, &page));
    GT_ASSERT(gates_access_hscroll_info(x.t, x.root, &pos, &page) && pos == 10000);
    gates_tree_destroy(x.t);
}

static void test_sideways_stale_rects(void) {
    /* The offset changed since the last layout: focus moves use where things are now. */
    side_t x = make_side(true);
    gates_i32 max = side_max(&x);
    GT_ASSERT_OK(gates_layout_set_scroll_x(x.t, x.root, max));
    side_layout(&x);
    GT_ASSERT_OK(gates_layout_set_scroll_x(x.t, x.root, 0)); /* no layout after it */
    gates_tree_set_focus(x.t, x.near);
    GT_ASSERT(gates_tree_focus_next(x.t, false));
    side_layout(&x);
    gates_rect_t far = gates_node_layout_rect(x.t, x.far);
    GT_ASSERT(far.x + far.w == VIEW_W - GATES_SCROLLBAR_PX);
    gates_tree_destroy(x.t);
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
    test_track_pages();
    test_sideways_off_by_default();
    test_sideways_geometry();
    test_sideways_narrow_content();
    test_sideways_paint();
    test_sideways_pointer();
    test_sideways_focus_and_access();
    test_sideways_bars();
    test_sideways_stale_rects();
    return gt_report("test_scroll");
}
