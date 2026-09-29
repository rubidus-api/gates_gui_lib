/* gates_gui_lib - toolbar (command buttons, one Tab stop, ">>" overflow menu)
 * and status bar (label segments with separators) (plan-0018). Platform-free. */
#include <gates/frame.h>
#include <gates/widget.h>
#include <gates/layout.h>
#include <gates/overlay.h>
#include <gates/ui.h>
#include "gates_tree_internal.h"

#include <string.h>

#define TB_PAD_X 8
#define TB_PAD_Y 4
#define TB_SEP_SPACE 4
#define SB_PAD 3
#define SB_SEP_SPACE 4

static const gates_str_t MORE = { .ptr = (const gates_u8 *)">>", .size = 2 };

static gates_widget_state_t *state_at(const gates_tree_t *tree, gates_u32 idx) {
    return gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
}

static gates_i_toolbar *bar_of(const gates_tree_t *tree, gates_node_t bar) {
    if (tree == nullptr || !gates_i_valid(tree, bar) ||
        gates_i_slot(tree, bar.index)->kind != GATES_NODE_TOOLBAR) {
        return nullptr;
    }
    const gates_widget_state_t *st = state_at(tree, bar.index);
    return st != nullptr ? st->tbar : nullptr;
}

/* Creates a node of `kind` with widget state (as the widgets do). */
static gates_err_t make_node(gates_tree_t *tree, gates_node_t parent, gates_node_kind_t kind,
                             gates_node_t *out) {
    gates_node_desc_t nd = { .kind = kind };
    gates_err_t err = gates_node_create(tree, parent, &nd, out);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_u32 state = GATES_NONE;
    err = gates_i_state_acquire(tree, &state);
    if (!gates_is_ok(err)) {
        (void)gates_node_destroy(tree, *out);
        (void)gates_tree_flush_destroys(tree);
        *out = GATES_NODE_NULL;
        return err;
    }
    gates_i_slot(tree, out->index)->state_index = state;
    return GATES_OK;
}

/* -- building ------------------------------------------------------------------------ */

gates_err_t gates_toolbar_create(gates_tree_t *tree, gates_node_t parent, gates_node_t scope,
                                 gates_node_t *out_bar) {
    if (tree == nullptr || out_bar == nullptr || !gates_i_valid(tree, scope) ||
        (!gates_node_eq(parent, GATES_NODE_NULL) && !gates_i_valid(tree, parent))) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_bar = GATES_NODE_NULL;
    gates_allocator_t a = tree->alloc;
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, sizeof(gates_i_toolbar), alignof(gates_i_toolbar));
    if (!proven_is_ok(r.err)) {
        return r.err;
    }
    gates_i_toolbar *tb = (gates_i_toolbar *)r.value.ptr;
    *tb = (gates_i_toolbar){ .scope_index = scope.index, .scope_generation = scope.generation,
                             .sel = -1, .press = -1, .hover = -1 };
    gates_node_t bar;
    gates_err_t err = make_node(tree, parent, GATES_NODE_TOOLBAR, &bar);
    if (!gates_is_ok(err)) {
        a.free_fn(a.ctx, tb);
        return err;
    }
    state_at(tree, bar.index)->tbar = tb;
    gates_i_mark_dirty(tree, bar.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    *out_bar = bar;
    return GATES_OK;
}

gates_err_t gates_toolbar_add(gates_tree_t *tree, gates_node_t bar, gates_command_id_t id) {
    gates_i_toolbar *tb = bar_of(tree, bar);
    if (tb == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (tb->count == tb->cap) {
        gates_allocator_t a = tree->alloc;
        gates_u32 cap = tb->cap == 0 ? 8u : tb->cap * 2u;
        proven_result_mem_mut_t r =
            tb->ids == nullptr ? a.alloc_fn(a.ctx, cap * sizeof *tb->ids, alignof(gates_command_id_t))
                               : a.realloc_fn(a.ctx, tb->ids, tb->cap * sizeof *tb->ids, cap * sizeof *tb->ids,
                                              alignof(gates_command_id_t));
        if (!proven_is_ok(r.err)) {
            return r.err;
        }
        tb->ids = (gates_command_id_t *)r.value.ptr;
        tb->cap = cap;
    }
    tb->ids[tb->count++] = id;
    gates_i_mark_dirty(tree, bar.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    gates_i_focus_check(tree);
    return GATES_OK;
}

void gates_i_toolbar_free(gates_tree_t *tree, gates_widget_state_t *st) {
    if (st->tbar == nullptr) {
        return;
    }
    if (st->tbar->ids != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, st->tbar->ids);
    }
    tree->alloc.free_fn(tree->alloc.ctx, st->tbar);
    st->tbar = nullptr;
}

gates_u32 gates_toolbar_count(const gates_tree_t *tree, gates_node_t bar) {
    const gates_i_toolbar *tb = bar_of(tree, bar);
    return tb != nullptr ? tb->count : 0;
}

gates_u32 gates_toolbar_shown(const gates_tree_t *tree, gates_node_t bar) {
    return bar_of(tree, bar) != nullptr ? gates_i_toolbar_shown(tree, bar.index) : 0;
}

const gates_i_command_t *gates_i_toolbar_command(const gates_tree_t *tree, gates_u32 idx, gates_u32 k) {
    const gates_widget_state_t *st = state_at(tree, idx);
    if (st == nullptr || st->tbar == nullptr || k >= st->tbar->count || st->tbar->ids[k] == 0) {
        return nullptr;
    }
    return gates_i_command_find(tree, st->tbar->scope_index, st->tbar->scope_generation, st->tbar->ids[k]);
}

static bool entry_enabled(const gates_tree_t *tree, gates_u32 idx, gates_u32 k) {
    const gates_i_command_t *c = gates_i_toolbar_command(tree, idx, k);
    return c != nullptr && c->enabled;
}

bool gates_i_toolbar_any_enabled(const gates_tree_t *tree, const gates_widget_state_t *st) {
    const gates_i_toolbar *tb = st->tbar;
    for (gates_u32 k = 0; tb != nullptr && k < tb->count; k++) {
        const gates_i_command_t *c =
            tb->ids[k] != 0 ? gates_i_command_find(tree, tb->scope_index, tb->scope_generation, tb->ids[k])
                            : nullptr;
        if (c != nullptr && c->enabled) return true;
    }
    return false;
}

/* -- geometry -------------------------------------------------------------------------- */

static gates_i32 entry_w(const gates_tree_t *tree, const gates_text_backend_t *be, gates_i32 font,
                         const gates_i_toolbar *tb, gates_u32 k) {
    if (tb->ids[k] == 0) {
        return 1 + 2 * TB_SEP_SPACE;
    }
    const gates_i_command_t *c = gates_i_command_find(tree, tb->scope_index, tb->scope_generation, tb->ids[k]);
    gates_str_t label = c != nullptr ? (gates_str_t){ .ptr = c->label, .size = c->label_len } : (gates_str_t){0};
    return gates_i_mn_width(be, font, label) + 2 * TB_PAD_X;
}

static gates_i32 more_w(const gates_text_backend_t *be, gates_i32 font) {
    return be->measure(be->ctx, font, MORE).w + 2 * TB_PAD_X;
}

gates_size_t gates_i_toolbar_measure(const gates_tree_t *tree, const gates_node_slot_t *s,
                                     const gates_text_backend_t *text) {
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    gates_i32 font = gates_i_slot_font(tree, s);
    gates_i32 w = 0;
    for (gates_u32 k = 0; st != nullptr && st->tbar != nullptr && k < st->tbar->count; k++) {
        w += entry_w(tree, text, font, st->tbar, k);
    }
    gates_i32 h = text->metrics(text->ctx, font).line_height + 2 * (TB_PAD_Y + 1);
    return (gates_size_t){ w, h < GATES_ACCESS_MIN_TARGET ? GATES_ACCESS_MIN_TARGET : h };
}

gates_u32 gates_i_toolbar_shown(const gates_tree_t *tree, gates_u32 idx) {
    const gates_widget_state_t *st = state_at(tree, idx);
    const gates_text_backend_t *be = tree->text_backend;
    if (st == nullptr || st->tbar == nullptr || be == nullptr) {
        return 0;
    }
    const gates_i_toolbar *tb = st->tbar;
    gates_i32 font = gates_i_font(tree, idx);
    gates_i32 avail = gates_i_slot(tree, idx)->layout_rect.w;
    gates_i32 total = 0;
    for (gates_u32 k = 0; k < tb->count; k++) total += entry_w(tree, be, font, tb, k);
    if (total <= avail) {
        return tb->count;
    }
    gates_i32 room = avail - more_w(be, font), x = 0;
    gates_u32 n = 0;
    while (n < tb->count && x + entry_w(tree, be, font, tb, n) <= room) {
        x += entry_w(tree, be, font, tb, n);
        n++;
    }
    while (n > 0 && tb->ids[n - 1] == 0) n--; /* never end on a separator */
    return n;
}

gates_rect_t gates_i_toolbar_entry_rect(const gates_tree_t *tree, gates_u32 idx, gates_u32 k) {
    const gates_widget_state_t *st = state_at(tree, idx);
    const gates_text_backend_t *be = tree->text_backend;
    if (st == nullptr || st->tbar == nullptr || be == nullptr || k >= gates_i_toolbar_shown(tree, idx)) {
        return (gates_rect_t){0};
    }
    gates_rect_t r = gates_i_slot(tree, idx)->layout_rect;
    gates_i32 font = gates_i_font(tree, idx);
    gates_i32 x = r.x;
    for (gates_u32 j = 0; j < k; j++) x += entry_w(tree, be, font, st->tbar, j);
    return (gates_rect_t){ x, r.y, entry_w(tree, be, font, st->tbar, k), r.h };
}

gates_rect_t gates_i_toolbar_more_rect(const gates_tree_t *tree, gates_u32 idx) {
    const gates_widget_state_t *st = state_at(tree, idx);
    const gates_text_backend_t *be = tree->text_backend;
    if (st == nullptr || st->tbar == nullptr || be == nullptr ||
        gates_i_toolbar_shown(tree, idx) >= st->tbar->count) {
        return (gates_rect_t){0};
    }
    gates_rect_t r = gates_i_slot(tree, idx)->layout_rect;
    gates_i32 w = more_w(be, gates_i_font(tree, idx));
    return (gates_rect_t){ r.x + r.w - w, r.y, w, r.h };
}

gates_i32 gates_i_toolbar_entry_at(const gates_tree_t *tree, gates_u32 idx, gates_point_t p) {
    const gates_widget_state_t *st = state_at(tree, idx);
    if (st == nullptr || st->tbar == nullptr) {
        return -1;
    }
    if (gates_rect_contains(gates_i_toolbar_more_rect(tree, idx), p)) {
        return (gates_i32)st->tbar->count;
    }
    gates_u32 shown = gates_i_toolbar_shown(tree, idx);
    for (gates_u32 k = 0; k < shown; k++) {
        if (st->tbar->ids[k] != 0 && gates_rect_contains(gates_i_toolbar_entry_rect(tree, idx, k), p)) {
            return (gates_i32)k;
        }
    }
    return -1;
}

/* -- paint --------------------------------------------------------------------------------- */

gates_err_t gates_i_toolbar_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                  const gates_theme_t *theme, const gates_text_backend_t *text) {
    const gates_widget_state_t *st = state_at(tree, idx);
    gates_rect_t r = gates_i_slot(tree, idx)->layout_rect;
    gates_err_t err = gates_draw_rect(dl, r, gates_theme_color(theme, GATES_COLOR_PANEL_BG));
    if (st == nullptr || st->tbar == nullptr || !gates_is_ok(err)) {
        return err;
    }
    const gates_i_toolbar *tb = st->tbar;
    gates_i32 font = gates_i_font(tree, idx);
    gates_text_metrics_t m = text->metrics(text->ctx, font);
    bool focused = tree->focus == idx;
    gates_i32 stop = focused ? gates_i_toolbar_stop(tree, idx) : -1;
    gates_u32 shown = gates_i_toolbar_shown(tree, idx);
    for (gates_u32 k = 0; k <= tb->count && gates_is_ok(err); k++) {
        bool more = k == tb->count;
        if (!more && k >= shown) continue;
        gates_rect_t er = more ? gates_i_toolbar_more_rect(tree, idx) : gates_i_toolbar_entry_rect(tree, idx, k);
        if (gates_rect_is_empty(er)) continue;
        if (!more && tb->ids[k] == 0) {
            err = gates_draw_rect(dl, (gates_rect_t){ er.x + TB_SEP_SPACE, er.y + TB_PAD_Y, 1, er.h - 2 * TB_PAD_Y },
                                  gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER));
            continue;
        }
        const gates_i_command_t *c = more ? nullptr : gates_i_toolbar_command(tree, idx, k);
        bool enabled = more || (c != nullptr && c->enabled);
        bool down = (tree->pressed == idx && tb->press == (gates_i32)k) || (c != nullptr && c->checked);
        bool hot = tb->hover == (gates_i32)k && enabled;
        if (down || hot) {
            err = gates_draw_rect(dl, er, gates_theme_color(theme, down ? GATES_COLOR_CONTROL_PRESSED_BG
                                                                         : GATES_COLOR_CONTROL_HOVER_BG));
        }
        if (gates_is_ok(err) && (down || hot)) {
            err = gates_draw_border(dl, er, 1, gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER));
        }
        gates_str_t label = more ? MORE : c != nullptr ? (gates_str_t){ .ptr = c->label, .size = c->label_len }
                                                       : (gates_str_t){0};
        gates_color_t fg = gates_theme_color(theme, enabled ? GATES_COLOR_CONTROL_FG : GATES_COLOR_CONTROL_DISABLED_FG);
        if (gates_is_ok(err)) {
            gates_rect_t tr = { er.x + TB_PAD_X, er.y + (er.h - m.line_height) / 2, 0, m.line_height };
            err = more ? gates_draw_text(dl, (gates_rect_t){ tr.x, tr.y, er.w - 2 * TB_PAD_X, m.line_height },
                                         label, font, fg)
                       : gates_i_mn_draw(dl, text, tr, label, font, fg, false);
        }
        if (gates_is_ok(err) && stop == (gates_i32)k) {
            err = gates_draw_border(dl, er, gates_theme_focus_width(theme),
                                    gates_theme_color(theme, GATES_COLOR_FOCUS_RING));
        }
    }
    return err;
}

/* -- keyboard stops and activation --------------------------------------------------------- */

/* Stops: shown, enabled buttons, then ">>" (index count) when there is overflow. */
static bool is_stop(const gates_tree_t *tree, gates_u32 idx, gates_i32 k) {
    const gates_i_toolbar *tb = state_at(tree, idx)->tbar;
    gates_u32 shown = gates_i_toolbar_shown(tree, idx);
    if (k == (gates_i32)tb->count) {
        return shown < tb->count;
    }
    return k >= 0 && (gates_u32)k < shown && entry_enabled(tree, idx, (gates_u32)k);
}

static gates_i32 step(const gates_tree_t *tree, gates_u32 idx, gates_i32 from, gates_i32 dir) {
    gates_i32 n = (gates_i32)state_at(tree, idx)->tbar->count + 1; /* entries and ">>" */
    for (gates_i32 i = 1; i <= n; i++) {
        gates_i32 k = ((from + dir * i) % n + n) % n;
        if (is_stop(tree, idx, k)) return k;
    }
    return -1;
}

gates_i32 gates_i_toolbar_stop(const gates_tree_t *tree, gates_u32 idx) {
    const gates_widget_state_t *st = state_at(tree, idx);
    if (st == nullptr || st->tbar == nullptr) {
        return -1;
    }
    gates_i32 sel = st->tbar->sel;
    if (sel >= 0 && is_stop(tree, idx, sel)) {
        return sel;
    }
    return step(tree, idx, sel < 0 ? -1 : sel, 1); /* the next one, as if moving right */
}

gates_err_t gates_i_toolbar_activate(gates_tree_t *tree, gates_u32 idx, gates_u32 k) {
    gates_i_toolbar *tb = state_at(tree, idx)->tbar;
    if (k == tb->count) {
        gates_u32 shown = gates_i_toolbar_shown(tree, idx);
        while (shown < tb->count && tb->ids[shown] == 0) shown++; /* no leading separator */
        if (shown >= tb->count) {
            return PROVEN_ERR_INVALID_STATE;
        }
        gates_rect_t mr = gates_i_toolbar_more_rect(tree, idx);
        gates_node_t scope = { .index = tb->scope_index, .generation = tb->scope_generation };
        gates_node_t menu;
        return gates_menu_open(tree, (gates_point_t){ mr.x, mr.y + mr.h }, scope, tb->ids + shown,
                               tb->count - shown, &menu);
    }
    const gates_i_command_t *c = gates_i_toolbar_command(tree, idx, k);
    if (c == nullptr || !c->enabled) {
        return PROVEN_ERR_INVALID_STATE;
    }
    gates_err_t err = gates_i_event_reserve(tree, 1, 0);
    if (gates_is_ok(err)) {
        gates_i_command_push(tree, c, GATES_ORIGIN_USER); /* re-checked at delivery */
    }
    return err;
}

bool gates_i_toolbar_key(gates_tree_t *tree, gates_u32 idx, const gates_key_event_t *ev) {
    gates_i_toolbar *tb = state_at(tree, idx)->tbar;
    if (tb == nullptr || ev->ctrl || ev->alt) {
        return false;
    }
    gates_i32 cur = gates_i_toolbar_stop(tree, idx);
    gates_i32 next = cur;
    switch (ev->key) {
    case GATES_KEY_LEFT: next = step(tree, idx, cur, -1); break;
    case GATES_KEY_RIGHT: next = step(tree, idx, cur, 1); break;
    case GATES_KEY_HOME: next = step(tree, idx, -1, 1); break;
    case GATES_KEY_END: next = step(tree, idx, (gates_i32)tb->count + 1, -1); break;
    case GATES_KEY_ENTER:
    case GATES_KEY_SPACE:
        if (cur >= 0) {
            gates_err_t err = gates_i_toolbar_activate(tree, idx, (gates_u32)cur);
            if (!gates_is_ok(err)) tree->input_error = err;
        }
        return true;
    default:
        return false;
    }
    if (next >= 0 && next != tb->sel) {
        tb->sel = next;
        gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
        gates_i_access_log(tree, GATES_ACCESS_FOCUS_GAINED, idx, (gates_u64)next + 1);
    }
    return true;
}

/* -- pointer: a press and a release on the same button invoke it; the focus stays -------- */

void gates_i_toolbar_down(gates_tree_t *tree, gates_u32 idx, gates_point_t p) {
    gates_i_toolbar *tb = state_at(tree, idx)->tbar;
    gates_i32 k = gates_i_toolbar_entry_at(tree, idx, p);
    if (k < 0 || (k < (gates_i32)tb->count && !entry_enabled(tree, idx, (gates_u32)k))) {
        return;
    }
    tb->press = k;
    tree->pressed = idx;
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
}

void gates_i_toolbar_up(gates_tree_t *tree, gates_u32 idx, gates_point_t p) {
    gates_i_toolbar *tb = state_at(tree, idx)->tbar;
    gates_i32 k = tb->press;
    tb->press = -1;
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    if (k >= 0 && gates_i_toolbar_entry_at(tree, idx, p) == k) {
        gates_err_t err = gates_i_toolbar_activate(tree, idx, (gates_u32)k);
        if (!gates_is_ok(err)) tree->input_error = err;
    }
}

gates_u32 gates_i_toolbar_tip(const gates_tree_t *tree, gates_u32 idx, gates_u32 k, gates_u8 *buf,
                              gates_u32 cap) {
    const gates_i_command_t *c = gates_i_toolbar_command(tree, idx, k);
    if (c == nullptr) {
        return 0;
    }
    gates_u32 n = gates_i_mn_strip((gates_str_t){ .ptr = c->label, .size = c->label_len }, buf, cap);
    char keys[32];
    gates_usize_t kn = gates_i_shortcut_text(&c->shortcut, keys, sizeof keys);
    if (kn > 0 && kn < sizeof keys) {
        const char *parts[3] = { " (", keys, ")" };
        for (int p = 0; p < 3; p++) {
            for (const char *ch = parts[p]; *ch; ch++, n++) {
                if (buf != nullptr && n < cap) buf[n] = (gates_u8)*ch;
            }
        }
    }
    return n;
}

/* -- status bar -------------------------------------------------------------------------------- */

gates_err_t gates_statusbar_create(gates_tree_t *tree, gates_node_t parent, gates_node_t *out_bar) {
    if (tree == nullptr || out_bar == nullptr ||
        (!gates_node_eq(parent, GATES_NODE_NULL) && !gates_i_valid(tree, parent))) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_node_t bar;
    gates_err_t err = make_node(tree, parent, GATES_NODE_STATUSBAR, &bar);
    if (gates_is_ok(err)) err = gates_layout_set(tree, bar, GATES_LAYOUT_KIND_ROW);
    if (gates_is_ok(err)) err = gates_layout_set_padding(tree, bar, SB_PAD);
    if (gates_is_ok(err)) err = gates_layout_set_gap(tree, bar, 1 + 2 * SB_SEP_SPACE);
    if (!gates_is_ok(err)) {
        if (gates_i_valid(tree, bar)) {
            (void)gates_node_destroy(tree, bar);
            (void)gates_tree_flush_destroys(tree);
        }
        return err;
    }
    *out_bar = bar;
    return GATES_OK;
}

gates_err_t gates_statusbar_add(gates_tree_t *tree, gates_node_t bar, gates_str_t text, gates_u8 grow,
                                gates_node_t *out_segment) {
    if (tree == nullptr || !gates_i_valid(tree, bar) || gates_i_slot(tree, bar.index)->kind != GATES_NODE_STATUSBAR) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_node_t seg;
    gates_err_t err = gates_label_create(tree, bar, text, &seg);
    if (gates_is_ok(err) && grow > 0) {
        err = gates_layout_set_child_grow(tree, seg, grow);
    }
    if (!gates_is_ok(err)) {
        return err;
    }
    if (out_segment != nullptr) *out_segment = seg;
    return GATES_OK;
}

gates_err_t gates_i_statusbar_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                    const gates_theme_t *theme) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    gates_rect_t r = s->layout_rect;
    gates_color_t line = gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER);
    gates_err_t err = gates_draw_rect(dl, r, gates_theme_color(theme, GATES_COLOR_PANEL_BG));
    if (gates_is_ok(err)) err = gates_draw_rect(dl, (gates_rect_t){ r.x, r.y, r.w, 1 }, line);
    bool first = true;
    for (gates_u32 c = s->first_child; c != GATES_NONE && gates_is_ok(err); c = gates_i_slot(tree, c)->next_sibling) {
        const gates_node_slot_t *cs = gates_i_slot(tree, c);
        if (cs->hidden) continue;
        if (!first) {
            gates_i32 x = cs->layout_rect.x - SB_SEP_SPACE - 1;
            err = gates_draw_rect(dl, (gates_rect_t){ x, r.y + SB_PAD, 1, r.h - 2 * SB_PAD }, line);
        }
        first = false;
    }
    return err;
}
