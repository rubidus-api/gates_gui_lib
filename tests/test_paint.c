/* tree -> draw list paint walk (+ soft-render smoke, allocator balance). */
#include <gates/ui.h>
#include <gates/widget.h>
#include <proven/heap.h>
#include "gates_test.h"

#include <string.h>

static const gates_text_backend_t *be;
static const gates_theme_t *theme;

/* Counting allocator: everything (tree, widgets, text, draw list) balances. */
typedef struct {
    gates_allocator_t inner;
    gates_usize_t allocs, frees;
} count_alloc_t;

static proven_result_mem_mut_t count_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    count_alloc_t *c = ctx;
    c->allocs++;
    return c->inner.alloc_fn(c->inner.ctx, size, align);
}

static proven_result_mem_mut_t count_realloc(void *ctx, void *old_ptr, proven_size_t old_size,
                                             proven_size_t new_size, proven_size_t align) {
    count_alloc_t *c = ctx;
    return c->inner.realloc_fn(c->inner.ctx, old_ptr, old_size, new_size, align);
}

static void count_free(void *ctx, void *ptr) {
    count_alloc_t *c = ctx;
    c->frees++;
    c->inner.free_fn(c->inner.ctx, ptr);
}

static int count_cmds(const gates_draw_list_t *dl, gates_draw_kind_t kind) {
    int n = 0;
    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        if (gates_draw_list_at(dl, i)->kind == kind) n++;
    }
    return n;
}

static const gates_draw_cmd_t *find_text_cmd(const gates_draw_list_t *dl, const char *needle) {
    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        const gates_draw_cmd_t *cmd = gates_draw_list_at(dl, i);
        if (cmd->kind != GATES_DRAW_TEXT) continue;
        gates_str_t s = gates_draw_cmd_text(dl, cmd);
        if (s.size == strlen(needle) && memcmp(s.ptr, needle, s.size) == 0) return cmd;
    }
    return nullptr;
}

static bool color_eq(gates_color_t a, gates_color_t b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void test_paint_commands(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_layout_set_padding(t, root, 8));

    gates_node_t lbl = GATES_NODE_NULL, btn = GATES_NODE_NULL, chk = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("title"), &lbl));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("Run"), nullptr, nullptr, &btn));
    GT_ASSERT_OK(gates_checkbox_create(t, root, GATES_STR("opt"), true, nullptr, nullptr, &chk));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 240, 120 }, be));

    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));

    /* Window background + button bg + checkbox box + checkbox mark = RECTs. */
    GT_ASSERT(count_cmds(&dl, GATES_DRAW_RECT) == 4);
    /* Button border + checkbox border. */
    GT_ASSERT(count_cmds(&dl, GATES_DRAW_BORDER) == 2);
    /* Three texts. */
    GT_ASSERT(count_cmds(&dl, GATES_DRAW_TEXT) == 3);

    /* First command is the window background with the WINDOW_BG token. */
    const gates_draw_cmd_t *bg = gates_draw_list_at(&dl, 0);
    GT_ASSERT(bg->kind == GATES_DRAW_RECT);
    GT_ASSERT(color_eq(bg->color, gates_theme_color(theme, GATES_COLOR_WINDOW_BG)));
    GT_ASSERT(bg->rect.w == 240 && bg->rect.h == 120);

    /* Label text sits at its layout rect with the PANEL_FG token. */
    const gates_draw_cmd_t *lt = find_text_cmd(&dl, "title");
    GT_ASSERT(lt != nullptr);
    gates_rect_t lr = gates_node_layout_rect(t, lbl);
    GT_ASSERT(lt->rect.x == lr.x && lt->rect.y == lr.y);
    GT_ASSERT(color_eq(lt->color, gates_theme_color(theme, GATES_COLOR_PANEL_FG)));

    /* Button text is centered inside the button rect. */
    const gates_draw_cmd_t *bt = find_text_cmd(&dl, "Run");
    GT_ASSERT(bt != nullptr);
    gates_rect_t br = gates_node_layout_rect(t, btn);
    GT_ASSERT(bt->rect.x > br.x && bt->rect.x + bt->rect.w < br.x + br.w);

    /* Checkbox mark uses the SELECTION_BG token (checked). */
    bool found_mark = false;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *cmd = gates_draw_list_at(&dl, i);
        if (cmd->kind == GATES_DRAW_RECT &&
            color_eq(cmd->color, gates_theme_color(theme, GATES_COLOR_SELECTION_BG))) {
            found_mark = true;
        }
    }
    GT_ASSERT(found_mark);

    /* Paint cleared the paint-dirty bit; the list renders cleanly. */
    GT_ASSERT((gates_tree_dirty(t) & GATES_TREE_DIRTY_PAINT) == 0);
    static gates_u32 pix[120][240];
    memset(pix, 0, sizeof pix);
    GT_ASSERT_OK(gates_render_soft(&dl, (gates_pixels_t){ .ptr = pix, .w = 240, .h = 120,
                                                          .stride_bytes = 240 * 4 }, be));

    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

static void test_hover_pressed_tokens(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    gates_node_t btn = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("B"), nullptr, nullptr, &btn));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 100, 60 }, be));
    gates_rect_t br = gates_node_layout_rect(t, btn);

    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));

    /* Idle -> CONTROL_BG. */
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    GT_ASSERT(color_eq(gates_draw_list_at(&dl, 1)->color,
                       gates_theme_color(theme, GATES_COLOR_CONTROL_BG)));

    /* Hover -> HOVER_BG. */
    gates_pointer_event_t mv = { .action = GATES_POINTER_MOVE,
                                 .pos = { br.x + 2, br.y + 2 } };
    (void)gates_input_pointer(t, &mv);
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    GT_ASSERT(color_eq(gates_draw_list_at(&dl, 1)->color,
                       gates_theme_color(theme, GATES_COLOR_CONTROL_HOVER_BG)));

    /* Pressed -> PRESSED_BG. */
    gates_pointer_event_t dn = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT,
                                 .pos = { br.x + 2, br.y + 2 } };
    (void)gates_input_pointer(t, &dn);
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    GT_ASSERT(color_eq(gates_draw_list_at(&dl, 1)->color,
                       gates_theme_color(theme, GATES_COLOR_CONTROL_PRESSED_BG)));

    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

static void test_stack_paints_active_only(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_STACK));
    gates_node_t p1 = GATES_NODE_NULL, p2 = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_panel_create(t, root, &p1));
    GT_ASSERT_OK(gates_panel_create(t, root, &p2));
    GT_ASSERT_OK(gates_layout_set(t, p1, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_layout_set(t, p2, GATES_LAYOUT_KIND_COLUMN));
    gates_node_t l1 = GATES_NODE_NULL, l2 = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_label_create(t, p1, GATES_STR("one"), &l1));
    GT_ASSERT_OK(gates_label_create(t, p2, GATES_STR("two"), &l2));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 100, 60 }, be));

    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    GT_ASSERT(find_text_cmd(&dl, "one") != nullptr);
    GT_ASSERT(find_text_cmd(&dl, "two") == nullptr);

    GT_ASSERT_OK(gates_layout_set_stack_active(t, root, 1));
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    GT_ASSERT(find_text_cmd(&dl, "one") == nullptr);
    GT_ASSERT(find_text_cmd(&dl, "two") != nullptr);

    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

static void test_full_lifecycle_allocator_balance(void) {
    count_alloc_t counter = { .inner = proven_heap_allocator() };
    gates_allocator_t alloc = { .ctx = &counter, .alloc_fn = count_alloc,
                                .realloc_fn = count_realloc, .free_fn = count_free };

    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = alloc }, &t));
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    for (int i = 0; i < 8; i++) {
        gates_node_t b = GATES_NODE_NULL;
        GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("button text"), nullptr,
                                         nullptr, &b));
        GT_ASSERT_OK(gates_widget_set_text(t, b, GATES_STR("longer replacement text")));
    }
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 300, 400 }, be));

    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, alloc, 4));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    GT_ASSERT(gates_draw_list_len(&dl) > 8);

    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
    GT_ASSERT(counter.frees == counter.allocs); /* nothing leaked anywhere */
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_paint_commands();
    test_hover_pressed_tokens();
    test_stack_paints_active_only();
    test_full_lifecycle_allocator_balance();
    return gt_report("test_paint");
}
