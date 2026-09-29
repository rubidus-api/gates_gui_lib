/* gates_gui_lib - mnemonics: "&x" markup, underlines while keyboard cues are
 * visible, Alt+letter activation (plan-0018). Platform-free. */
#include <gates/frame.h>
#include <gates/widget.h>
#include <gates/ui.h>
#include "gates_tree_internal.h"

#include <string.h>

static bool alnum(gates_u8 c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

static gates_u8 upper(gates_u8 c) {
    return (c >= 'a' && c <= 'z') ? (gates_u8)(c - 'a' + 'A') : c;
}

gates_u8 gates_mnemonic_of(gates_str_t text) {
    for (gates_usize_t i = 0; i + 1 < text.size; i++) {
        if (text.ptr[i] != '&') continue;
        if (text.ptr[i + 1] == '&') {
            i++; /* "&&" is one '&' */
            continue;
        }
        if (alnum(text.ptr[i + 1])) {
            return upper(text.ptr[i + 1]);
        }
    }
    return 0;
}

/* Walks the markup: calls `piece` for every run of shown bytes, flagging the
 * mnemonic character (the first "&x"). Returns false when a callback fails. */
typedef bool (*piece_fn)(void *ctx, gates_str_t run, bool mnemonic);

static bool walk(gates_str_t text, piece_fn piece, void *ctx) {
    gates_usize_t start = 0;
    bool found = false;
    for (gates_usize_t i = 0; i < text.size; i++) {
        if (text.ptr[i] != '&' || i + 1 >= text.size) continue;
        gates_u8 next = text.ptr[i + 1];
        if (next != '&' && !alnum(next)) continue; /* shown as it is */
        if (i > start && !piece(ctx, (gates_str_t){ .ptr = text.ptr + start, .size = i - start }, false)) {
            return false;
        }
        bool mn = next != '&' && !found;
        found = found || mn;
        if (!piece(ctx, (gates_str_t){ .ptr = text.ptr + i + 1, .size = 1 }, mn)) {
            return false;
        }
        i++;
        start = i + 1;
    }
    if (start < text.size) {
        return piece(ctx, (gates_str_t){ .ptr = text.ptr + start, .size = text.size - start }, false);
    }
    return true;
}

typedef struct width_ctx_t {
    const gates_text_backend_t *be;
    gates_i32 font;
    gates_i32 w;
} width_ctx_t;

static bool width_piece(void *c, gates_str_t run, bool mnemonic) {
    (void)mnemonic;
    width_ctx_t *w = c;
    w->w += w->be->measure(w->be->ctx, w->font, run).w;
    return true;
}

gates_i32 gates_i_mn_width(const gates_text_backend_t *be, gates_i32 font, gates_str_t text) {
    width_ctx_t w = { .be = be, .font = font };
    (void)walk(text, width_piece, &w);
    return w.w;
}

typedef struct draw_ctx_t {
    gates_draw_list_t *dl;
    const gates_text_backend_t *be;
    gates_rect_t rect;
    gates_i32 font;
    gates_color_t color;
    bool cues;
    gates_i32 x;
    gates_i32 underline_y;
    gates_err_t err;
} draw_ctx_t;

static bool draw_piece(void *c, gates_str_t run, bool mnemonic) {
    draw_ctx_t *d = c;
    gates_size_t sz = d->be->measure(d->be->ctx, d->font, run);
    d->err = gates_draw_text(d->dl, (gates_rect_t){ d->x, d->rect.y, sz.w, sz.h }, run, d->font, d->color);
    if (gates_is_ok(d->err) && mnemonic && d->cues && sz.w > 0) {
        d->err = gates_draw_rect(d->dl, (gates_rect_t){ d->x, d->underline_y, sz.w, 1 }, d->color);
    }
    d->x += sz.w;
    return gates_is_ok(d->err);
}

gates_err_t gates_i_mn_draw(gates_draw_list_t *dl, const gates_text_backend_t *be, gates_rect_t rect,
                            gates_str_t text, gates_i32 font, gates_color_t color, bool cues) {
    gates_text_metrics_t m = be->metrics(be->ctx, font);
    /* One unit below the baseline, inside the line. */
    gates_i32 uy = rect.y + m.ascent + 1;
    if (uy > rect.y + m.line_height - 1) uy = rect.y + m.line_height - 1;
    draw_ctx_t d = { .dl = dl, .be = be, .rect = rect, .font = font, .color = color, .cues = cues,
                     .x = rect.x, .underline_y = uy, .err = GATES_OK };
    (void)walk(text, draw_piece, &d);
    return d.err;
}

typedef struct strip_ctx_t {
    gates_u8 *buf;
    gates_u32 cap;
    gates_u32 len;
} strip_ctx_t;

static bool strip_piece(void *c, gates_str_t run, bool mnemonic) {
    (void)mnemonic;
    strip_ctx_t *s = c;
    for (gates_usize_t i = 0; i < run.size; i++, s->len++) {
        if (s->len < s->cap) s->buf[s->len] = run.ptr[i];
    }
    return true;
}

gates_u32 gates_i_mn_strip(gates_str_t text, gates_u8 *buf, gates_u32 cap) {
    strip_ctx_t s = { .buf = buf, .cap = buf != nullptr ? cap : 0 };
    (void)walk(text, strip_piece, &s);
    return s.len;
}

bool gates_i_mn_markup(const gates_tree_t *tree, gates_u32 idx) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    switch (s->kind) {
    case GATES_NODE_BUTTON:
    case GATES_NODE_CHECKBOX:
    case GATES_NODE_MENUBAR:
    case GATES_NODE_GROUPHEAD:
        return true;
    case GATES_NODE_LABEL: {
        const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
        return st != nullptr && st->has_mn_target;
    }
    default:
        return false;
    }
}

bool gates_i_cues(const gates_tree_t *tree) {
    return tree->cues_always || tree->cues_shown;
}

/* -- label targets ------------------------------------------------------------------ */

gates_err_t gates_label_set_target(gates_tree_t *tree, gates_node_t label, gates_node_t target) {
    if (tree == nullptr || !gates_i_valid(tree, label) ||
        gates_i_slot(tree, label.index)->kind != GATES_NODE_LABEL) {
        return PROVEN_ERR_INVALID_ARG;
    }
    bool clear = gates_node_eq(target, GATES_NODE_NULL);
    if (!clear && (!gates_i_valid(tree, target) || target.index == label.index)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, label.index)->state_index);
    if (st == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    st->has_mn_target = !clear;
    st->mn_target_index = clear ? 0 : target.index;
    st->mn_target_generation = clear ? 0 : target.generation;
    gates_i_mark_dirty(tree, label.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_node_t gates_label_target(const gates_tree_t *tree, gates_node_t label) {
    if (tree == nullptr || !gates_i_valid(tree, label) ||
        gates_i_slot(tree, label.index)->kind != GATES_NODE_LABEL) {
        return GATES_NODE_NULL;
    }
    const gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, label.index)->state_index);
    if (st == nullptr || !st->has_mn_target) {
        return GATES_NODE_NULL;
    }
    gates_node_t t = { .index = st->mn_target_index, .generation = st->mn_target_generation };
    return gates_i_valid(tree, t) ? t : GATES_NODE_NULL;
}

/* -- cues ------------------------------------------------------------------------------ */

static void cues_set(gates_tree_t *tree, bool shown) {
    if (tree->cues_shown != shown) {
        tree->cues_shown = shown;
        if (!tree->cues_always) {
            gates_i_mark_dirty(tree, GATES_NONE, GATES_DIRTY_PAINT);
        }
    }
}

void gates_input_show_cues(gates_tree_t *tree) {
    if (tree != nullptr) {
        cues_set(tree, true);
    }
}

void gates_tree_set_cues_always(gates_tree_t *tree, bool always) {
    if (tree != nullptr && tree->cues_always != always) {
        tree->cues_always = always;
        gates_i_mark_dirty(tree, GATES_NONE, GATES_DIRTY_PAINT);
    }
}

bool gates_tree_cues_visible(const gates_tree_t *tree) {
    return tree != nullptr && gates_i_cues(tree);
}

/* -- Alt+letter -------------------------------------------------------------------------- */

/* The node a mnemonic candidate hands the focus to (a label: its target), or
 * GATES_NONE when `idx` is not a candidate for `letter`. */
static gates_u32 candidate(const gates_tree_t *tree, gates_u32 idx, gates_u8 letter) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (!s->alive || s->destroy_pending || !gates_i_mn_markup(tree, idx) ||
        s->kind == GATES_NODE_MENUBAR) {
        return GATES_NONE;
    }
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    if (st == nullptr || gates_mnemonic_of(gates_i_widget_label(tree, st)) != letter) {
        return GATES_NONE;
    }
    if (s->kind == GATES_NODE_GROUPHEAD && s->parent != GATES_NONE && !gates_i_group_foldable(tree, s->parent)) {
        /* A plain group's title: its first control (plan-0019). */
        return gates_i_reachable(tree, idx) ? gates_i_group_first(tree, s->parent) : GATES_NONE;
    }
    if (s->kind == GATES_NODE_LABEL) {
        gates_node_t t = { .index = st->mn_target_index, .generation = st->mn_target_generation };
        if (gates_i_valid(tree, t) && gates_i_slot(tree, t.index)->kind == GATES_NODE_SPIN) {
            t = gates_i_handle(tree, gates_i_slot(tree, t.index)->first_child); /* its text box */
        }
        if (!gates_i_valid(tree, t) || !gates_i_reachable(tree, idx) ||
            !gates_i_focus_eligible(tree, t.index)) {
            return GATES_NONE;
        }
        return t.index;
    }
    return gates_i_focus_eligible(tree, idx) ? idx : GATES_NONE;
}

/* Pre-order walk of the focus scope. */
static gates_u32 next_pre(const gates_tree_t *tree, gates_u32 idx, gates_u32 scope) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->first_child != GATES_NONE) {
        return s->first_child;
    }
    for (gates_u32 n = idx; n != scope && n != GATES_NONE; n = gates_i_slot(tree, n)->parent) {
        gates_u32 sib = gates_i_slot(tree, n)->next_sibling;
        if (sib != GATES_NONE) {
            return sib;
        }
    }
    return GATES_NONE;
}

bool gates_input_mnemonic(gates_tree_t *tree, gates_u32 codepoint) {
    if (tree == nullptr || codepoint > 0x7F || !alnum((gates_u8)codepoint)) {
        return false;
    }
    gates_u8 letter = upper((gates_u8)codepoint);
    cues_set(tree, true);
    /* A menu bar title first. */
    gates_u32 bar = gates_i_menubar_live(tree);
    if (bar != GATES_NONE) {
        const gates_widget_state_t *bs = gates_i_state(tree, gates_i_slot(tree, bar)->state_index);
        for (gates_u32 t = 0; bs->mbar != nullptr && t < bs->mbar->count; t++) {
            gates_str_t title = { .ptr = bs->mbar->items[t].title, .size = bs->mbar->items[t].title_len };
            if (gates_mnemonic_of(title) == letter) {
                gates_err_t err = gates_i_menubar_open(tree, t, true);
                if (!gates_is_ok(err)) {
                    tree->input_error = err;
                }
                return true;
            }
        }
    }
    /* Tab titles (plan-0018). */
    if (gates_i_tabs_mnemonic(tree, letter)) {
        return true;
    }
    /* Controls of the focus scope, in tree order. */
    gates_u32 scope = gates_i_scope_root(tree);
    gates_u32 found[2] = { GATES_NONE, GATES_NONE }; /* first match, first after focus */
    gates_u32 first_src = GATES_NONE;
    gates_u32 count = 0;
    bool after_focus = tree->focus == GATES_NONE;
    for (gates_u32 n = scope; n != GATES_NONE; n = next_pre(tree, n, scope)) {
        if (n == tree->focus) {
            after_focus = true;
            continue;
        }
        gates_u32 to = candidate(tree, n, letter);
        if (to == GATES_NONE) continue;
        count++;
        if (found[0] == GATES_NONE) {
            found[0] = to;
            first_src = n;
        }
        if (after_focus && found[1] == GATES_NONE && to != tree->focus) found[1] = to;
    }
    /* The focused control itself counts once more when it matches. */
    if (tree->focus != GATES_NONE && candidate(tree, tree->focus, letter) != GATES_NONE) {
        count++;
    }
    if (count == 0) {
        return false;
    }
    if (count > 1) {
        gates_u32 to = found[1] != GATES_NONE ? found[1] : found[0];
        gates_tree_set_focus(tree, gates_i_handle(tree, to));
        gates_i_scroll_into_view(tree, to);
        return true;
    }
    gates_u32 src = first_src != GATES_NONE ? first_src : tree->focus;
    gates_u32 to = found[0] != GATES_NONE ? found[0] : tree->focus;
    gates_node_kind_t kind = gates_i_slot(tree, src)->kind;
    gates_tree_set_focus(tree, gates_i_handle(tree, to));
    gates_i_scroll_into_view(tree, to);
    if (kind == GATES_NODE_BUTTON || kind == GATES_NODE_CHECKBOX ||
        (kind == GATES_NODE_GROUPHEAD && to == src)) {
        gates_i_activate(tree, src); /* a collapsible group's title toggles */
    }
    return true;
}
