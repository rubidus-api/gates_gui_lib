/* gates_gui_lib - tabs: a strip of titles over a stack of pages (0.3.0).
 * The tabs node is a column holding the strip (one node owning its titles,
 * one Tab stop) and a stack panel with the pages. Platform-free. */
#include <gates/frame.h>
#include <gates/widget.h>
#include <gates/layout.h>
#include <gates/ui.h>
#include <gates/overlay.h>
#include <gates/command.h>
#include "gates_tree_internal.h"

#include <string.h>

#define TAB_PAD_X 10
#define TAB_ROW_EXTRA 8
#define PAGE_PAD 8
#define PAGE_GAP 6

static gates_widget_state_t *state_at(const gates_tree_t *tree, gates_u32 idx) {
    return gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
}

static gates_i_tabs *tabs_of(const gates_tree_t *tree, gates_node_t tabs) {
    if (tree == nullptr || !gates_i_valid(tree, tabs) || gates_i_slot(tree, tabs.index)->kind != GATES_NODE_TABS) {
        return nullptr;
    }
    const gates_widget_state_t *st = state_at(tree, tabs.index);
    return st != nullptr ? st->tabs : nullptr;
}

/* The tabs node of a strip (its parent). */
static gates_u32 owner_of(const gates_tree_t *tree, gates_u32 strip) {
    return gates_i_slot(tree, strip)->parent;
}

static gates_u32 stack_of(const gates_tree_t *tree, gates_u32 tabs) {
    gates_u32 strip = gates_i_slot(tree, tabs)->first_child;
    return strip != GATES_NONE ? gates_i_slot(tree, strip)->next_sibling : GATES_NONE;
}

static gates_err_t make_node(gates_tree_t *tree, gates_node_t parent, gates_node_kind_t kind, gates_node_t *out) {
    gates_node_desc_t nd = { .kind = kind };
    gates_err_t err = gates_node_create(tree, parent, &nd, out);
    if (!gates_is_ok(err)) return err;
    gates_u32 state = GATES_NONE;
    err = gates_i_state_acquire(tree, &state);
    if (!gates_is_ok(err)) {
        gates_i_node_undo(tree, *out);
        return err;
    }
    gates_i_slot(tree, out->index)->state_index = state;
    return GATES_OK;
}

/* -- building ------------------------------------------------------------------------------ */

gates_err_t gates_tabs_create(gates_tree_t *tree, gates_node_t parent, gates_node_t *out_tabs) {
    if (tree == nullptr || out_tabs == nullptr ||
        (!gates_node_eq(parent, GATES_NODE_NULL) && !gates_i_valid(tree, parent))) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_tabs = GATES_NODE_NULL;
    gates_allocator_t a = tree->alloc;
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, sizeof(gates_i_tabs), alignof(gates_i_tabs));
    if (!proven_is_ok(r.err)) return r.err;
    gates_i_tabs *tb = (gates_i_tabs *)r.value.ptr;
    *tb = (gates_i_tabs){0};
    /* Built detached, attached last: a failure leaves nothing behind. */
    gates_node_t tabs = GATES_NODE_NULL, strip, stack;
    gates_err_t err = make_node(tree, GATES_NODE_NULL, GATES_NODE_TABS, &tabs);
    if (gates_is_ok(err)) {
        state_at(tree, tabs.index)->tabs = tb;
        tb->self = tabs;
        tb = nullptr;
        err = gates_layout_set(tree, tabs, GATES_LAYOUT_KIND_COLUMN);
    }
    if (gates_is_ok(err)) err = make_node(tree, tabs, GATES_NODE_TABSTRIP, &strip);
    if (gates_is_ok(err)) err = gates_panel_create(tree, tabs, &stack);
    if (gates_is_ok(err)) err = gates_layout_set(tree, stack, GATES_LAYOUT_KIND_STACK);
    if (gates_is_ok(err)) err = gates_layout_set_child_grow(tree, stack, 1);
    if (gates_is_ok(err) && !gates_node_eq(parent, GATES_NODE_NULL)) err = gates_node_append(tree, parent, tabs);
    if (!gates_is_ok(err)) {
        if (tb != nullptr) a.free_fn(a.ctx, tb);
        if (gates_i_valid(tree, tabs)) gates_i_discard_detached(tree, tabs);
        return err;
    }
    gates_i_mark_dirty(tree, tabs.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    *out_tabs = tabs;
    return GATES_OK;
}

static gates_err_t copy_title(gates_tree_t *tree, gates_str_t title, gates_u8 **out) {
    *out = nullptr;
    if (title.size == 0) return GATES_OK;
    proven_result_mem_mut_t r = tree->alloc.alloc_fn(tree->alloc.ctx, title.size, 1);
    if (!proven_is_ok(r.err)) return r.err;
    *out = (gates_u8 *)r.value.ptr;
    memcpy(*out, title.ptr, title.size);
    return GATES_OK;
}

gates_err_t gates_tabs_add(gates_tree_t *tree, gates_node_t tabs, gates_str_t title, gates_node_t *out_page) {
    gates_i_tabs *tb = tabs_of(tree, tabs);
    if (tb == nullptr || (title.size > 0 && title.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_allocator_t a = tree->alloc;
    if (tb->count == tb->cap) {
        gates_u32 cap = tb->cap == 0 ? 4u : tb->cap * 2u;
        proven_result_mem_mut_t r =
            tb->titles == nullptr
                ? a.alloc_fn(a.ctx, cap * sizeof *tb->titles, alignof(gates_i_tab_title))
                : a.realloc_fn(a.ctx, tb->titles, tb->cap * sizeof *tb->titles, cap * sizeof *tb->titles,
                               alignof(gates_i_tab_title));
        if (!proven_is_ok(r.err)) return r.err;
        tb->titles = (gates_i_tab_title *)r.value.ptr;
        tb->cap = cap;
    }
    gates_u8 *copy = nullptr;
    gates_err_t err = copy_title(tree, title, &copy);
    gates_node_t page = GATES_NODE_NULL;
    if (gates_is_ok(err)) err = gates_panel_create(tree, gates_i_handle(tree, stack_of(tree, tabs.index)), &page);
    if (gates_is_ok(err)) err = gates_layout_set(tree, page, GATES_LAYOUT_KIND_COLUMN);
    if (gates_is_ok(err)) err = gates_layout_set_padding(tree, page, PAGE_PAD);
    if (gates_is_ok(err)) err = gates_layout_set_gap(tree, page, PAGE_GAP);
    if (!gates_is_ok(err)) {
        if (copy != nullptr) a.free_fn(a.ctx, copy);
        if (gates_i_valid(tree, page)) {
            gates_i_node_undo(tree, page);
        }
        return err;
    }
    tb->titles[tb->count++] = (gates_i_tab_title){ .text = copy, .len = (gates_u32)title.size };
    gates_i_mark_dirty(tree, tabs.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    gates_i_focus_check(tree);
    if (out_page != nullptr) *out_page = page;
    return GATES_OK;
}

gates_err_t gates_tabs_set_title(gates_tree_t *tree, gates_node_t tabs, gates_u32 index, gates_str_t title) {
    gates_i_tabs *tb = tabs_of(tree, tabs);
    if (tb == nullptr || (title.size > 0 && title.ptr == nullptr)) return PROVEN_ERR_INVALID_ARG;
    if (index >= tb->count) return PROVEN_ERR_OUT_OF_BOUNDS;
    gates_u8 *copy = nullptr;
    gates_err_t err = copy_title(tree, title, &copy);
    if (!gates_is_ok(err)) return err;
    if (tb->titles[index].text != nullptr) tree->alloc.free_fn(tree->alloc.ctx, tb->titles[index].text);
    tb->titles[index] = (gates_i_tab_title){ .text = copy, .len = (gates_u32)title.size };
    gates_i_mark_dirty(tree, tabs.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    gates_i_access_log(tree, GATES_ACCESS_CHANGED, gates_i_slot(tree, tabs.index)->first_child, 0);
    return GATES_OK;
}

void gates_i_tabs_free(gates_tree_t *tree, gates_widget_state_t *st) {
    gates_i_tabs *tb = st->tabs;
    if (tb == nullptr) return;
    for (gates_u32 i = 0; i < tb->count; i++) {
        if (tb->titles[i].text != nullptr) tree->alloc.free_fn(tree->alloc.ctx, tb->titles[i].text);
    }
    if (tb->titles != nullptr) tree->alloc.free_fn(tree->alloc.ctx, tb->titles);
    tree->alloc.free_fn(tree->alloc.ctx, tb);
    st->tabs = nullptr;
}

gates_u32 gates_tabs_count(const gates_tree_t *tree, gates_node_t tabs) {
    const gates_i_tabs *tb = tabs_of(tree, tabs);
    return tb != nullptr ? tb->count : 0;
}

gates_str_t gates_tabs_title(const gates_tree_t *tree, gates_node_t tabs, gates_u32 index) {
    const gates_i_tabs *tb = tabs_of(tree, tabs);
    if (tb == nullptr || index >= tb->count) return (gates_str_t){0};
    return (gates_str_t){ .ptr = tb->titles[index].text, .size = tb->titles[index].len };
}

gates_node_t gates_tabs_page(const gates_tree_t *tree, gates_node_t tabs, gates_u32 index) {
    if (tabs_of(tree, tabs) == nullptr) return GATES_NODE_NULL;
    gates_u32 i = 0;
    for (gates_u32 c = gates_i_slot(tree, stack_of(tree, tabs.index))->first_child; c != GATES_NONE;
         c = gates_i_slot(tree, c)->next_sibling, i++) {
        if (i == index) return gates_i_handle(tree, c);
    }
    return GATES_NODE_NULL;
}

gates_u32 gates_i_tabs_selected(const gates_tree_t *tree, gates_u32 tabs) {
    gates_u32 stack = stack_of(tree, tabs);
    return stack != GATES_NONE ? gates_i_slot(tree, stack)->active_child : 0;
}

gates_u32 gates_tabs_selected(const gates_tree_t *tree, gates_node_t tabs) {
    return tabs_of(tree, tabs) != nullptr ? gates_i_tabs_selected(tree, tabs.index) : 0;
}

gates_err_t gates_tabs_set_selected(gates_tree_t *tree, gates_node_t tabs, gates_u32 index) {
    const gates_i_tabs *tb = tabs_of(tree, tabs);
    if (tb == nullptr) return PROVEN_ERR_INVALID_ARG;
    if (index >= tb->count) return PROVEN_ERR_OUT_OF_BOUNDS;
    gates_err_t err = gates_layout_set_stack_active(tree, gates_i_handle(tree, stack_of(tree, tabs.index)), index);
    if (gates_is_ok(err)) {
        gates_i_mark_dirty(tree, tabs.index, GATES_DIRTY_PAINT);
        gates_i_focus_check(tree); /* a control on the old page loses the focus */
    }
    return err;
}

gates_str_t gates_i_tabs_title(const gates_tree_t *tree, gates_u32 tabs, gates_u32 index) {
    const gates_widget_state_t *st = state_at(tree, tabs);
    if (st == nullptr || st->tabs == nullptr || index >= st->tabs->count) return (gates_str_t){0};
    return (gates_str_t){ .ptr = st->tabs->titles[index].text, .size = st->tabs->titles[index].len };
}

gates_u32 gates_i_tabs_count(const gates_tree_t *tree, gates_u32 tabs) {
    const gates_widget_state_t *st = state_at(tree, tabs);
    return st != nullptr && st->tabs != nullptr ? st->tabs->count : 0;
}

/* The page index of a node in a tabs' stack, or -1 when `idx` is not a page. */
gates_i32 gates_i_tab_page_index(const gates_tree_t *tree, gates_u32 idx, gates_u32 *out_tabs) {
    gates_u32 stack = gates_i_slot(tree, idx)->parent;
    if (stack == GATES_NONE) return -1;
    gates_u32 tabs = gates_i_slot(tree, stack)->parent;
    if (tabs == GATES_NONE || gates_i_slot(tree, tabs)->kind != GATES_NODE_TABS || stack_of(tree, tabs) != stack) {
        return -1;
    }
    gates_i32 i = 0;
    for (gates_u32 c = gates_i_slot(tree, stack)->first_child; c != idx; c = gates_i_slot(tree, c)->next_sibling) i++;
    if (out_tabs != nullptr) *out_tabs = tabs;
    return i;
}

/* -- the strip: geometry and paint ------------------------------------------------------------ */

static gates_i32 title_w(const gates_text_backend_t *be, gates_i32 font, gates_str_t title) {
    return gates_i_mn_width(be, font, title) + 2 * TAB_PAD_X;
}

gates_size_t gates_i_tabstrip_measure(const gates_tree_t *tree, const gates_node_slot_t *s,
                                      const gates_text_backend_t *text) {
    gates_u32 tabs = s->parent;
    gates_i32 font = gates_i_slot_font(tree, s);
    gates_i32 w = 0;
    for (gates_u32 i = 0; tabs != GATES_NONE && i < gates_i_tabs_count(tree, tabs); i++) {
        w += title_w(text, font, gates_i_tabs_title(tree, tabs, i));
    }
    gates_i32 h = text->metrics(text->ctx, font).line_height + TAB_ROW_EXTRA;
    return (gates_size_t){ w, h < GATES_ACCESS_MIN_TARGET ? GATES_ACCESS_MIN_TARGET : h };
}

static const gates_str_t MORE = { .ptr = (const gates_u8 *)">>", .size = 2 };

static gates_i32 more_w(const gates_text_backend_t *be, gates_i32 font) {
    return be->measure(be->ctx, font, MORE).w + 2 * TAB_PAD_X;
}

/* The titles shown (0.10.0): whole titles [*first, *end), always the selected
 * one - from the first title while the selected one fits, else ending at it.
 * When they do not all fit, *room is the width left of the ">>" button. */
static bool shown_range(const gates_tree_t *tree, gates_u32 strip, gates_u32 *first, gates_u32 *end, gates_i32 *room) {
    const gates_text_backend_t *be = tree->text_backend;
    gates_u32 tabs = owner_of(tree, strip);
    gates_rect_t r = gates_i_slot(tree, strip)->layout_rect;
    *first = *end = 0;
    *room = r.w;
    if (be == nullptr || tabs == GATES_NONE) return false;
    gates_i32 font = gates_i_font(tree, strip);
    gates_u32 n = gates_i_tabs_count(tree, tabs), sel = gates_i_tabs_selected(tree, tabs);
    gates_i32 total = 0;
    for (gates_u32 i = 0; i < n; i++) total += title_w(be, font, gates_i_tabs_title(tree, tabs, i));
    if (total <= r.w || n == 0) {
        *end = n;
        return false;
    }
    *room = r.w - more_w(be, font);
    if (sel >= n) sel = n - 1;
    gates_i32 w = 0;
    for (gates_u32 i = 0; i <= sel; i++) w += title_w(be, font, gates_i_tabs_title(tree, tabs, i));
    gates_u32 f = 0;
    while (f < sel && w > *room) w -= title_w(be, font, gates_i_tabs_title(tree, tabs, f++));
    gates_u32 e = sel + 1;
    while (e < n) {
        gates_i32 tw = title_w(be, font, gates_i_tabs_title(tree, tabs, e));
        if (w + tw > *room) break;
        w += tw;
        e++;
    }
    *first = f;
    *end = e;
    return true;
}

gates_rect_t gates_i_tab_rect(const gates_tree_t *tree, gates_u32 strip, gates_u32 index) {
    const gates_text_backend_t *be = tree->text_backend;
    gates_u32 tabs = owner_of(tree, strip);
    if (be == nullptr || tabs == GATES_NONE || index >= gates_i_tabs_count(tree, tabs)) return (gates_rect_t){0};
    gates_u32 first, end;
    gates_i32 room;
    (void)shown_range(tree, strip, &first, &end, &room);
    if (index < first || index >= end) return (gates_rect_t){0}; /* not shown: reached through ">>" */
    gates_rect_t r = gates_i_slot(tree, strip)->layout_rect;
    gates_i32 font = gates_i_font(tree, strip);
    gates_i32 x = r.x;
    for (gates_u32 i = first; i < index; i++) x += title_w(be, font, gates_i_tabs_title(tree, tabs, i));
    gates_rect_t t = { x, r.y, title_w(be, font, gates_i_tabs_title(tree, tabs, index)), r.h };
    return gates_rect_intersect(t, (gates_rect_t){ r.x, r.y, room, r.h }); /* one title wider than all */
}

gates_rect_t gates_i_tabstrip_more_rect(const gates_tree_t *tree, gates_u32 strip) {
    gates_u32 first, end;
    gates_i32 room;
    if (!shown_range(tree, strip, &first, &end, &room)) return (gates_rect_t){0};
    gates_rect_t r = gates_i_slot(tree, strip)->layout_rect;
    return (gates_rect_t){ r.x + room, r.y, r.w - room, r.h };
}

gates_i32 gates_i_tab_at(const gates_tree_t *tree, gates_u32 strip, gates_point_t p) {
    gates_u32 tabs = owner_of(tree, strip);
    for (gates_u32 i = 0; tabs != GATES_NONE && i < gates_i_tabs_count(tree, tabs); i++) {
        if (gates_rect_contains(gates_i_tab_rect(tree, strip, i), p)) return (gates_i32)i;
    }
    return -1;
}

gates_err_t gates_i_tabstrip_paint(const gates_tree_t *tree, gates_u32 strip, gates_draw_list_t *dl,
                                   const gates_theme_t *theme, const gates_text_backend_t *text) {
    gates_u32 tabs = owner_of(tree, strip);
    gates_rect_t r = gates_i_slot(tree, strip)->layout_rect;
    gates_color_t line = gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER);
    gates_err_t err = gates_draw_rect(dl, (gates_rect_t){ r.x, r.y + r.h - 1, r.w, 1 }, line);
    gates_i32 font = gates_i_font(tree, strip);
    gates_text_metrics_t m = text->metrics(text->ctx, font);
    gates_u32 sel = gates_i_tabs_selected(tree, tabs);
    if (!gates_is_ok(err)) return err;
    err = gates_draw_clip_push(dl, r);
    for (gates_u32 i = 0; gates_is_ok(err) && i < gates_i_tabs_count(tree, tabs); i++) {
        gates_rect_t t = gates_i_tab_rect(tree, strip, i);
        if (gates_rect_is_empty(t)) continue;
        bool on = i == sel;
        gates_rect_t box = on ? t : (gates_rect_t){ t.x, t.y + 2, t.w, t.h - 3 };
        gates_color_token_t bg = on ? GATES_COLOR_PANEL_BG : GATES_COLOR_CONTROL_BG;
        err = gates_draw_rect(dl, box, gates_theme_color(theme, bg));
        if (gates_is_ok(err)) err = gates_draw_border(dl, box, 1, line);
        if (gates_is_ok(err) && on) {
            /* The selected tab opens into its page: no line below it. */
            err = gates_draw_rect(dl, (gates_rect_t){ box.x + 1, box.y + box.h - 1, box.w - 2, 1 },
                                  gates_theme_color(theme, GATES_COLOR_PANEL_BG));
        }
        if (gates_is_ok(err)) {
            err = gates_i_mn_draw(dl, text,
                                  (gates_rect_t){ t.x + TAB_PAD_X, box.y + (box.h - m.line_height) / 2, 0, m.line_height },
                                  gates_i_tabs_title(tree, tabs, i), font,
                                  gates_theme_color(theme, GATES_COLOR_PANEL_FG), gates_i_cues(tree));
        }
        if (gates_is_ok(err) && on && tree->focus == strip) {
            gates_rect_t ring = { box.x + 2, box.y + 2, box.w - 4, box.h - 4 };
            err = gates_draw_border(dl, ring, gates_theme_focus_width(theme), gates_theme_color(theme, GATES_COLOR_FOCUS_RING));
        }
    }
    gates_rect_t mr = gates_i_tabstrip_more_rect(tree, strip);
    if (gates_is_ok(err) && !gates_rect_is_empty(mr)) { /* ">>": the rest of the titles (0.10.0) */
        gates_rect_t box = { mr.x, mr.y + 2, mr.w, mr.h - 3 };
        err = gates_draw_rect(dl, box, gates_theme_color(theme, GATES_COLOR_CONTROL_BG));
        if (gates_is_ok(err)) err = gates_draw_border(dl, box, 1, line);
        if (gates_is_ok(err)) {
            gates_i32 tw = text->measure(text->ctx, font, MORE).w;
            err = gates_draw_text(dl, (gates_rect_t){ box.x + (box.w - tw) / 2, box.y + (box.h - m.line_height) / 2, tw,
                                                      m.line_height },
                                  MORE, font, gates_theme_color(theme, GATES_COLOR_PANEL_FG));
        }
    }
    gates_err_t pop = gates_draw_clip_pop(dl);
    return gates_is_ok(err) ? pop : err;
}

/* -- switching ---------------------------------------------------------------------------------- */

static bool inside(const gates_tree_t *tree, gates_u32 idx, gates_u32 top) {
    for (gates_u32 a = idx; a != GATES_NONE; a = gates_i_slot(tree, a)->parent) {
        if (a == top) return true;
    }
    return false;
}

/* The first control of a subtree that can take the focus, or GATES_NONE. */
static gates_u32 first_focusable(const gates_tree_t *tree, gates_u32 top) {
    if (gates_i_focus_eligible(tree, top)) return top;
    for (gates_u32 c = gates_i_slot(tree, top)->first_child; c != GATES_NONE; c = gates_i_slot(tree, c)->next_sibling) {
        gates_u32 f = first_focusable(tree, c);
        if (f != GATES_NONE) return f;
    }
    return GATES_NONE;
}

gates_err_t gates_i_tabs_pick(gates_tree_t *tree, gates_u32 tabs, gates_u32 index) {
    gates_u32 old = gates_i_tabs_selected(tree, tabs);
    if (index >= gates_i_tabs_count(tree, tabs)) return PROVEN_ERR_OUT_OF_BOUNDS;
    if (index == old) return GATES_OK;
    gates_widget_state_t *st = state_at(tree, tabs);
    if (gates_i_wants_events(tree, tabs)) {
        gates_err_t err = gates_i_event_reserve(tree, 1, 0); /* no switch without its report */
        if (!gates_is_ok(err)) return err;
    }
    gates_u32 strip = gates_i_slot(tree, tabs)->first_child;
    gates_u32 stack = stack_of(tree, tabs);
    gates_node_t old_page = gates_tabs_page(tree, gates_i_handle(tree, tabs), old);
    bool follow = tree->focus != GATES_NONE && gates_i_valid(tree, old_page) && inside(tree, tree->focus, old_page.index);
    gates_err_t err = gates_layout_set_stack_active(tree, gates_i_handle(tree, stack), index);
    if (!gates_is_ok(err)) return err;
    st->revision++;
    gates_i_mark_dirty(tree, tabs, GATES_DIRTY_PAINT);
    gates_i_access_log(tree, GATES_ACCESS_CHANGED, strip, 0);
    if (follow) {
        gates_node_t page = gates_tabs_page(tree, gates_i_handle(tree, tabs), index);
        gates_u32 to = first_focusable(tree, page.index);
        gates_tree_set_focus(tree, gates_i_handle(tree, to != GATES_NONE ? to : strip));
    }
    gates_i_focus_check(tree);
    if (gates_i_wants_events(tree, tabs)) {
        gates_i_event_push_ex(tree, tabs, GATES_EVENT_VALUE_CHANGED, GATES_ORIGIN_USER, index, 0);
    }
    return GATES_OK;
}

static void pick(gates_tree_t *tree, gates_u32 tabs, gates_u32 index) {
    gates_err_t err = gates_i_tabs_pick(tree, tabs, index);
    if (!gates_is_ok(err)) tree->input_error = err;
}

bool gates_i_tabstrip_key(gates_tree_t *tree, gates_u32 strip, const gates_key_event_t *ev) {
    if (ev->alt && !ev->ctrl && !ev->shift && ev->key == GATES_KEY_DOWN) { /* the list of every tab */
        gates_err_t err = gates_i_tabs_open_list(tree, strip);
        if (!gates_is_ok(err)) tree->input_error = err;
        return true;
    }
    if (ev->ctrl || ev->alt) return false;
    gates_u32 tabs = owner_of(tree, strip);
    gates_u32 n = gates_i_tabs_count(tree, tabs), sel = gates_i_tabs_selected(tree, tabs);
    if (n == 0) return false;
    switch (ev->key) {
    case GATES_KEY_LEFT: if (sel > 0) pick(tree, tabs, sel - 1); return true;
    case GATES_KEY_RIGHT: if (sel + 1 < n) pick(tree, tabs, sel + 1); return true;
    case GATES_KEY_HOME: pick(tree, tabs, 0); return true;
    case GATES_KEY_END: pick(tree, tabs, n - 1); return true;
    default: return false;
    }
}

bool gates_i_tabs_ctrl_key(gates_tree_t *tree, const gates_key_event_t *ev) {
    if (!ev->ctrl || ev->alt || tree->focus == GATES_NONE) return false;
    bool back;
    if (ev->key == GATES_KEY_TAB) back = ev->shift;
    else if (ev->key == GATES_KEY_PAGE_DOWN && !ev->shift) back = false;
    else if (ev->key == GATES_KEY_PAGE_UP && !ev->shift) back = true;
    else return false;
    /* The nearest tabs around the focus. */
    for (gates_u32 a = tree->focus; a != GATES_NONE; a = gates_i_slot(tree, a)->parent) {
        if (gates_i_slot(tree, a)->kind != GATES_NODE_TABS) continue;
        gates_u32 n = gates_i_tabs_count(tree, a);
        if (n < 2) return n == 1;
        gates_u32 sel = gates_i_tabs_selected(tree, a);
        pick(tree, a, back ? (sel + n - 1) % n : (sel + 1) % n);
        return true;
    }
    return false;
}

/* The list of every tab (0.10.0): a menu of commands in the strip's own scope,
 * one per tab (id = index + 1, the selected one checked); choosing one switches. */
static void on_tab_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    gates_i_tabs *tb = user;
    if (gates_i_valid(tree, tb->self)) pick(tree, tb->self.index, (gates_u32)id - 1);
}

gates_err_t gates_i_tabs_open_list(gates_tree_t *tree, gates_u32 strip) {
    gates_u32 tabs = owner_of(tree, strip);
    if (tabs == GATES_NONE) return PROVEN_ERR_INVALID_ARG;
    gates_i_tabs *tb = state_at(tree, tabs)->tabs;
    gates_u32 n = gates_i_tabs_count(tree, tabs), sel = gates_i_tabs_selected(tree, tabs);
    if (n == 0) return GATES_OK;
    gates_node_t scope = gates_i_handle(tree, strip);
    for (gates_u32 k = 0; k < n; k++) {
        gates_str_t title = gates_i_tabs_title(tree, tabs, k);
        gates_err_t err;
        if (gates_command_exists(tree, scope, k + 1)) {
            err = gates_command_set_label(tree, scope, k + 1, title);
            if (gates_is_ok(err)) err = gates_command_set_checked(tree, scope, k + 1, k == sel);
        } else {
            gates_command_desc_t d = { .id = k + 1, .label = title, .enabled = true, .checked = k == sel,
                                       .invoke = on_tab_command, .user = tb };
            err = gates_command_register(tree, scope, &d);
        }
        if (!gates_is_ok(err)) return err;
    }
    gates_allocator_t a = tree->alloc;
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, n * sizeof(gates_command_id_t), alignof(gates_command_id_t));
    if (!proven_is_ok(r.err)) return r.err;
    gates_command_id_t *ids = (gates_command_id_t *)r.value.ptr;
    for (gates_u32 k = 0; k < n; k++) ids[k] = k + 1;
    gates_rect_t at = gates_i_tabstrip_more_rect(tree, strip);
    if (gates_rect_is_empty(at)) at = gates_i_slot(tree, strip)->layout_rect;
    gates_node_t menu;
    gates_err_t err = gates_menu_open(tree, (gates_point_t){ at.x, at.y + at.h }, scope, ids, n, &menu);
    a.free_fn(a.ctx, ids);
    return err;
}

void gates_i_tabstrip_press(gates_tree_t *tree, gates_u32 strip, gates_point_t p) {
    if (gates_rect_contains(gates_i_tabstrip_more_rect(tree, strip), p)) {
        gates_tree_set_focus(tree, gates_i_handle(tree, strip));
        gates_err_t err = gates_i_tabs_open_list(tree, strip);
        if (!gates_is_ok(err)) tree->input_error = err;
        return;
    }
    gates_i32 t = gates_i_tab_at(tree, strip, p);
    if (t < 0) return;
    gates_tree_set_focus(tree, gates_i_handle(tree, strip));
    pick(tree, owner_of(tree, strip), (gates_u32)t);
}

bool gates_i_tabs_mnemonic(gates_tree_t *tree, gates_u8 letter) {
    gates_u32 scope = gates_i_scope_root(tree);
    for (gates_u32 i = 0; i < tree->capacity; i++) {
        const gates_node_slot_t *s = gates_i_slot(tree, i);
        if (!s->alive || s->destroy_pending || s->kind != GATES_NODE_TABSTRIP || !gates_i_focus_eligible(tree, i)) {
            continue;
        }
        (void)scope; /* eligibility already means reachable inside the scope */
        gates_u32 tabs = owner_of(tree, i);
        for (gates_u32 k = 0; k < gates_i_tabs_count(tree, tabs); k++) {
            if (gates_mnemonic_of(gates_i_tabs_title(tree, tabs, k)) == letter) {
                gates_tree_set_focus(tree, gates_i_handle(tree, i));
                pick(tree, tabs, k);
                return true;
            }
        }
    }
    return false;
}
