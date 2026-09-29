/* gates_gui_lib - tooltips: one at a time, shown after the pointer rests or
 * keyboard focus arrives, hidden by presses, keys, leaving and time (plan-0018).
 * One timer on the tree root, armed only while there is a target, so an idle
 * window stays without timers. Platform-free. */
#include <gates/frame.h>
#include <gates/timer.h>
#include <gates/widget.h>
#include "gates_tree_internal.h"

#include <string.h>

#define TIP_PAD_X 6
#define TIP_PAD_Y 3
#define TIP_GAP 4

static void cancel_timer(gates_tree_t *tree) {
    if (tree->tip_timer != 0) {
        (void)gates_timer_cancel(tree, tree->tip_timer);
        tree->tip_timer = 0;
    }
}

static void fire(gates_tree_t *tree, gates_node_t node, gates_timer_id_t id, void *user);

static bool arm(gates_tree_t *tree, gates_u32 ms) {
    cancel_timer(tree);
    gates_timer_id_t id = 0;
    if (!gates_is_ok(gates_timer_start(tree, gates_i_handle(tree, tree->root), ms, false, fire, nullptr, &id))) {
        return false; /* no clock: tooltips are never timed */
    }
    tree->tip_timer = id;
    return true;
}

static void hide(gates_tree_t *tree) {
    cancel_timer(tree);
    if (tree->tip_state == GATES_I_TIP_SHOWN) {
        gates_i_mark_dirty(tree, GATES_NONE, GATES_DIRTY_PAINT);
    }
    tree->tip_state = GATES_I_TIP_OFF;
}

static void forget(gates_tree_t *tree) {
    hide(tree);
    tree->tip_index = GATES_NONE;
    tree->tip_item = 0;
    tree->tip_by_focus = false;
}

static bool target_alive(const gates_tree_t *tree) {
    gates_node_t n = { .index = tree->tip_index, .generation = tree->tip_generation };
    return tree->tip_index != GATES_NONE && gates_i_valid(tree, n);
}

/* The target's text into tree->tip_text; false when it has none (or no memory). */
static bool compose(gates_tree_t *tree) {
    gates_u8 small[256];
    gates_u32 n;
    gates_str_t own = {0};
    if (tree->tip_item != 0) {
        n = gates_i_toolbar_tip(tree, tree->tip_index, (gates_u32)tree->tip_item - 1, small, sizeof small);
    } else {
        own = gates_i_tooltip_of(tree, tree->tip_index);
        n = (gates_u32)own.size;
    }
    if (n == 0) {
        return false;
    }
    if (n > tree->tip_cap) {
        gates_allocator_t a = tree->alloc;
        proven_result_mem_mut_t r = tree->tip_text == nullptr ? a.alloc_fn(a.ctx, n, 1)
                                                              : a.realloc_fn(a.ctx, tree->tip_text, tree->tip_cap, n, 1);
        if (!proven_is_ok(r.err)) {
            tree->input_error = r.err;
            return false;
        }
        tree->tip_text = (gates_u8 *)r.value.ptr;
        tree->tip_cap = n;
    }
    if (tree->tip_item != 0) {
        if (n <= sizeof small) {
            memcpy(tree->tip_text, small, n);
        } else {
            (void)gates_i_toolbar_tip(tree, tree->tip_index, (gates_u32)tree->tip_item - 1, tree->tip_text, n);
        }
    } else {
        memcpy(tree->tip_text, own.ptr, n);
    }
    tree->tip_len = n;
    return true;
}

/* Below the target (above when it does not fit), inside the window. */
static void place(gates_tree_t *tree) {
    const gates_text_backend_t *be = tree->text_backend;
    gates_rect_t view = gates_i_slot(tree, tree->root)->layout_rect;
    gates_rect_t t = tree->tip_item != 0
                         ? gates_i_toolbar_entry_rect(tree, tree->tip_index, (gates_u32)tree->tip_item - 1)
                         : gates_i_slot(tree, tree->tip_index)->layout_rect;
    gates_i32 lh = be != nullptr ? be->metrics(be->ctx, GATES_FONT_UI).line_height : 16;
    gates_i32 tw = be != nullptr ? be->measure(be->ctx, GATES_FONT_UI,
                                               (gates_str_t){ .ptr = tree->tip_text, .size = tree->tip_len }).w
                                 : 0;
    gates_i32 w = tw + 2 * TIP_PAD_X, h = lh + 2 * TIP_PAD_Y;
    if (w > view.w) w = view.w;
    gates_i32 x = t.x, y = t.y + t.h + TIP_GAP;
    if (y + h > view.y + view.h) y = t.y - TIP_GAP - h;
    if (y < view.y) y = view.y;
    if (x + w > view.x + view.w) x = view.x + view.w - w;
    if (x < view.x) x = view.x;
    tree->tip_box = (gates_rect_t){ x, y, w, h };
}

static void show(gates_tree_t *tree) {
    if (!target_alive(tree) || !compose(tree)) {
        forget(tree);
        return;
    }
    place(tree);
    tree->tip_state = GATES_I_TIP_SHOWN;
    gates_i_mark_dirty(tree, GATES_NONE, GATES_DIRTY_PAINT);
    (void)arm(tree, GATES_TOOLTIP_SHOW_MS);
}

static void fire(gates_tree_t *tree, gates_node_t node, gates_timer_id_t id, void *user) {
    (void)node;
    (void)user;
    if (id != tree->tip_timer) {
        return;
    }
    tree->tip_timer = 0;
    if (tree->tip_state == GATES_I_TIP_PENDING) {
        show(tree);
    } else {
        hide(tree); /* shown long enough; not again until the pointer moves on (the target stays) */
    }
}

/* Usable: alive, shown, reachable, not disabled; a toolbar button enabled. */
static bool usable(const gates_tree_t *tree, gates_u32 idx, gates_u64 item) {
    const gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    if (!gates_i_reachable(tree, idx) || (st != nullptr && gates_i_widget_inert(tree, st))) {
        return false;
    }
    if (item != 0) {
        const gates_i_command_t *c = gates_i_toolbar_command(tree, idx, (gates_u32)item - 1);
        return c != nullptr && c->enabled && (gates_u32)item - 1 < gates_i_toolbar_shown(tree, idx);
    }
    return gates_i_tooltip_of(tree, idx).size > 0;
}

/* Starts on a new target: pending, or at once when one is shown (switching). */
static void target(gates_tree_t *tree, gates_u32 idx, gates_u64 item, bool by_focus) {
    bool switching = tree->tip_state == GATES_I_TIP_SHOWN;
    hide(tree);
    tree->tip_index = idx;
    tree->tip_generation = gates_i_slot(tree, idx)->generation;
    tree->tip_item = item;
    tree->tip_by_focus = by_focus;
    if (switching) {
        show(tree);
        return;
    }
    tree->tip_state = GATES_I_TIP_PENDING;
    if (!arm(tree, GATES_TOOLTIP_DELAY_MS)) {
        tree->tip_state = GATES_I_TIP_OFF;
    }
}

void gates_i_tip_hover(gates_tree_t *tree, gates_point_t p, gates_u32 hit) {
    /* The nearest node with a tooltip, or a toolbar button under the pointer. */
    gates_u32 idx = GATES_NONE;
    gates_u64 item = 0;
    for (gates_u32 n = hit; n != GATES_NONE; n = gates_i_slot(tree, n)->parent) {
        if (gates_i_slot(tree, n)->kind == GATES_NODE_TOOLBAR) {
            gates_i32 k = gates_i_toolbar_entry_at(tree, n, p);
            const gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, n)->state_index);
            if (k >= 0 && st != nullptr && (gates_u32)k < st->tbar->count) {
                idx = n;
                item = (gates_u64)k + 1;
                break;
            }
        }
        if (gates_i_tooltip_of(tree, n).size > 0) {
            idx = n;
            break;
        }
    }
    if (idx != GATES_NONE && !usable(tree, idx, item)) {
        idx = GATES_NONE;
    }
    if (idx == GATES_NONE) {
        if (!tree->tip_by_focus || tree->tip_state != GATES_I_TIP_SHOWN) {
            forget(tree);
        }
        return;
    }
    if (target_alive(tree) && tree->tip_index == idx && tree->tip_item == item && !tree->tip_by_focus) {
        return; /* still resting on it (shown, pending or blocked) */
    }
    target(tree, idx, item, false);
}

/* Hiding keeps the target, so resting on it does not bring the tooltip back. */
void gates_i_tip_dismiss(gates_tree_t *tree) {
    hide(tree);
}

/* After a key that moved the focus (the key already hid any tooltip). */
void gates_i_tip_focus(gates_tree_t *tree, gates_u32 idx) {
    if (idx != GATES_NONE && usable(tree, idx, 0)) {
        target(tree, idx, 0, true);
    }
}

void gates_i_tip_check(gates_tree_t *tree) {
    if (tree->tip_index == GATES_NONE) {
        return;
    }
    if (!target_alive(tree) || !usable(tree, tree->tip_index, tree->tip_item)) {
        forget(tree);
    } else if (tree->tip_state == GATES_I_TIP_SHOWN && compose(tree)) {
        place(tree);
        gates_i_mark_dirty(tree, GATES_NONE, GATES_DIRTY_PAINT);
    }
}

void gates_i_tip_destroying(gates_tree_t *tree, gates_u32 top) {
    for (gates_u32 a = tree->tip_index; a != GATES_NONE && target_alive(tree); a = gates_i_slot(tree, a)->parent) {
        if (a == top) {
            forget(tree);
            return;
        }
    }
}

gates_err_t gates_i_tip_paint(const gates_tree_t *tree, gates_draw_list_t *dl, const gates_theme_t *theme,
                              const gates_text_backend_t *text) {
    if (tree->tip_state != GATES_I_TIP_SHOWN) {
        return GATES_OK;
    }
    gates_rect_t b = tree->tip_box;
    gates_i32 lh = text->metrics(text->ctx, GATES_FONT_UI).line_height;
    gates_err_t err = gates_draw_rect(dl, b, gates_theme_color(theme, GATES_COLOR_CONTROL_BG));
    if (gates_is_ok(err)) err = gates_draw_border(dl, b, 1, gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER));
    if (gates_is_ok(err)) err = gates_draw_clip_push(dl, b);
    if (gates_is_ok(err)) {
        err = gates_draw_text(dl, (gates_rect_t){ b.x + TIP_PAD_X, b.y + TIP_PAD_Y, b.w - 2 * TIP_PAD_X, lh },
                              (gates_str_t){ .ptr = tree->tip_text, .size = tree->tip_len }, GATES_FONT_UI,
                              gates_theme_color(theme, GATES_COLOR_CONTROL_FG));
        gates_err_t pop = gates_draw_clip_pop(dl);
        if (gates_is_ok(err)) err = pop;
    }
    return err;
}

void gates_i_tip_free(gates_tree_t *tree) {
    if (tree->tip_text != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, tree->tip_text);
    }
    tree->tip_text = nullptr;
    tree->tip_cap = tree->tip_len = 0;
}

bool gates_tooltip_shown(const gates_tree_t *tree, gates_node_t *node, gates_u64 *item, gates_str_t *text,
                         gates_rect_t *box) {
    if (tree == nullptr || tree->tip_state != GATES_I_TIP_SHOWN || !target_alive(tree)) {
        return false;
    }
    if (node != nullptr) *node = (gates_node_t){ .index = tree->tip_index, .generation = tree->tip_generation };
    if (item != nullptr) *item = tree->tip_item;
    if (text != nullptr) *text = (gates_str_t){ .ptr = tree->tip_text, .size = tree->tip_len };
    if (box != nullptr) *box = tree->tip_box;
    return true;
}
