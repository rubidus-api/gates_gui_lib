/* gates_gui_lib - overlays: modal dialog and context menu with one set of
 * dismissal rules (plan-0009 stage 2, RFC-0003 7, RFC-0001 10). Platform-free. */
#include <gates/overlay.h>
#include <gates/widget.h>
#include <gates/layout.h>
#include <gates/ui.h>
#include "gates_tree_internal.h"

#include <string.h>

#define MENU_PAD 4
#define MENU_ROW_EXTRA 6

/* -- records -------------------------------------------------------------- */

static gates_i32 find_overlay(const gates_tree_t *tree, gates_u32 idx, gates_u32 gen) {
    for (gates_u32 i = 0; i < tree->overlay_count; i++) {
        if (tree->overlays[i].index == idx && tree->overlays[i].generation == gen) {
            return (gates_i32)i;
        }
    }
    return -1;
}

static gates_widget_state_t *state_at(const gates_tree_t *tree, gates_u32 idx) {
    return gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
}

static void drop_record(gates_tree_t *tree, gates_u32 i) {
    memmove(&tree->overlays[i], &tree->overlays[i + 1],
            (tree->overlay_count - i - 1) * sizeof tree->overlays[0]);
    tree->overlay_count--;
    gates_i_mark_dirty(tree, GATES_NONE, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
}

/* Queues a closing event for an overlay node (its handler decides whether one
 * is queued). The node is destroyed after delivery; without a handler it is
 * destroyed now. The caller reserved the slot. */
static void closed_event(gates_tree_t *tree, gates_u32 idx, gates_event_kind_t kind,
                         gates_u32 result) {
    gates_widget_state_t *st = state_at(tree, idx);
    if (st != nullptr && st->on_event != nullptr && tree->event_len < tree->event_cap) {
        tree->events[tree->event_len++] = (gates_i_event_t){
            .node_index = idx,
            .generation = gates_i_slot(tree, idx)->generation,
            .kind = (gates_u8)kind,
            .origin = (gates_u8)GATES_ORIGIN_USER,
            .aux = result,
        };
        return;
    }
    (void)gates_node_destroy(tree, gates_i_handle(tree, idx));
}

static void menu_remove(gates_tree_t *tree, gates_u32 i, gates_u32 result);

/* Restores what a dialog took over when it opened. */
static void dialog_restore(gates_tree_t *tree, const gates_i_overlay_t *o) {
    tree->focus_scope = o->prev_scope;
    if (tree->focus_scope != GATES_NONE &&
        !gates_i_valid(tree, gates_i_handle(tree, tree->focus_scope))) {
        tree->focus_scope = GATES_NONE;
    }
    gates_node_t prev = { .index = o->prev_focus_index, .generation = o->prev_focus_generation };
    if (o->prev_focus_index != GATES_NONE && gates_i_valid(tree, prev) &&
        gates_i_focus_eligible(tree, prev.index)) {
        gates_tree_set_focus(tree, prev);
    } else {
        gates_tree_set_focus(tree, GATES_NODE_NULL);
        (void)gates_tree_focus_next(tree, false);
    }
}

/* Removes dialog record i; menus using its commands close with it. */
static void dialog_remove(gates_tree_t *tree, gates_u32 i) {
    gates_i_overlay_t o = tree->overlays[i];
    drop_record(tree, i);
    for (gates_u32 k = tree->overlay_count; k-- > 0;) {
        if (tree->overlays[k].kind != GATES_NODE_MENU) continue;
        const gates_widget_state_t *ms = state_at(tree, tree->overlays[k].index);
        if (ms != nullptr && ms->menu_scope_index == o.index &&
            ms->menu_scope_generation == o.generation) {
            menu_remove(tree, k, 0);
        }
    }
    dialog_restore(tree, &o);
}

/* -- dialog ------------------------------------------------------------------- */

gates_err_t gates_dialog_open(gates_tree_t *tree, const gates_dialog_desc_t *desc,
                              gates_node_t *out_dialog, gates_node_t *out_content) {
    if (tree == nullptr || desc == nullptr || out_dialog == nullptr || out_content == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_dialog = GATES_NODE_NULL;
    *out_content = GATES_NODE_NULL;
    if (tree->overlay_count >= GATES_I_OVERLAY_MAX) {
        return PROVEN_ERR_OUT_OF_BOUNDS;
    }
    gates_node_desc_t nd = { .kind = GATES_NODE_DIALOG };
    gates_node_t dlg = GATES_NODE_NULL;
    gates_err_t err = gates_node_create(tree, GATES_NODE_NULL, &nd, &dlg);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_u32 state = GATES_NONE;
    err = gates_i_state_acquire(tree, &state);
    gates_node_t title = GATES_NODE_NULL, content = GATES_NODE_NULL;
    if (gates_is_ok(err)) {
        gates_i_slot(tree, dlg.index)->state_index = state;
        err = gates_layout_set(tree, dlg, GATES_LAYOUT_KIND_COLUMN);
    }
    if (gates_is_ok(err)) err = gates_layout_set_padding(tree, dlg, 12);
    if (gates_is_ok(err)) err = gates_layout_set_gap(tree, dlg, 10);
    if (gates_is_ok(err)) err = gates_label_create(tree, dlg, desc->title, &title);
    if (gates_is_ok(err)) err = gates_panel_create(tree, dlg, &content);
    if (gates_is_ok(err)) err = gates_layout_set(tree, content, GATES_LAYOUT_KIND_COLUMN);
    if (gates_is_ok(err)) err = gates_layout_set_gap(tree, content, 6);
    if (!gates_is_ok(err)) {
        gates_i_discard_detached(tree, dlg); /* nothing half-built stays; no allocation */
        return err;
    }
    gates_i_overlay_t *o = &tree->overlays[tree->overlay_count++];
    *o = (gates_i_overlay_t){
        .index = dlg.index,
        .generation = dlg.generation,
        .kind = GATES_NODE_DIALOG,
        .prev_focus_index = tree->focus,
        .prev_focus_generation = tree->focus != GATES_NONE
                                     ? gates_i_slot(tree, tree->focus)->generation
                                     : 0,
        .prev_scope = tree->focus_scope,
        .needs_focus = true,
    };
    gates_input_cancel_pointer(tree); /* a drag begun below must not go on behind it */
    gates_tree_set_focus(tree, GATES_NODE_NULL);
    tree->focus_scope = dlg.index;
    gates_i_mark_dirty(tree, dlg.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    *out_dialog = dlg;
    *out_content = content;
    return GATES_OK;
}

gates_err_t gates_dialog_close(gates_tree_t *tree, gates_node_t dialog,
                               gates_dialog_result_t result) {
    if (tree == nullptr || !gates_i_valid(tree, dialog)) {
        return PROVEN_ERR_INVALID_STATE;
    }
    gates_i32 i = find_overlay(tree, dialog.index, dialog.generation);
    if (i < 0 || tree->overlays[i].kind != GATES_NODE_DIALOG) {
        return PROVEN_ERR_INVALID_STATE;
    }
    gates_widget_state_t *st = state_at(tree, dialog.index);
    if (st != nullptr && st->on_event != nullptr) {
        gates_err_t err = gates_i_event_reserve(tree, 1, 0);
        if (!gates_is_ok(err)) {
            return err; /* stays open: the result is never lost */
        }
    }
    dialog_remove(tree, (gates_u32)i);
    closed_event(tree, dialog.index, GATES_EVENT_DIALOG_CLOSED, (gates_u32)result);
    return GATES_OK;
}

bool gates_i_overlay_escape(gates_tree_t *tree) {
    if (tree->overlay_count == 0) {
        return false;
    }
    const gates_i_overlay_t *top = &tree->overlays[tree->overlay_count - 1];
    if (top->kind != GATES_NODE_DIALOG) {
        return false;
    }
    gates_err_t err = gates_dialog_close(tree, gates_i_handle(tree, top->index),
                                         GATES_DIALOG_CANCELED);
    if (!gates_is_ok(err)) {
        tree->input_error = err;
    }
    return true;
}

bool gates_i_overlay_is_dialog_scope(const gates_tree_t *tree, gates_u32 scope) {
    for (gates_u32 i = 0; i < tree->overlay_count; i++) {
        if (tree->overlays[i].index == scope && tree->overlays[i].kind == GATES_NODE_DIALOG) {
            return true;
        }
    }
    return false;
}

void gates_i_overlay_node_destroyed(gates_tree_t *tree, gates_u32 idx) {
    /* Menus whose commands live in this node's scope close with it. */
    gates_u32 gen = gates_i_slot(tree, idx)->generation;
    for (gates_u32 k = tree->overlay_count; k-- > 0;) {
        if (tree->overlays[k].kind != GATES_NODE_MENU) continue;
        const gates_widget_state_t *ms = state_at(tree, tree->overlays[k].index);
        if (ms != nullptr && ms->menu_scope_index == idx && ms->menu_scope_generation == gen &&
            tree->overlays[k].index != idx) {
            menu_remove(tree, k, 0);
        }
    }
    gates_i32 i = find_overlay(tree, idx, gates_i_slot(tree, idx)->generation);
    if (i < 0) {
        return;
    }
    if (tree->overlays[i].kind == GATES_NODE_DIALOG) {
        dialog_remove(tree, (gates_u32)i); /* the application ended it: no event */
    } else {
        drop_record(tree, (gates_u32)i);
    }
}

/* -- menu --------------------------------------------------------------------- */

/* One row as the menu shows it: from a command, or from a choice's option. */
typedef struct menu_row_t {
    bool present;                /* false: separator, or command/option gone */
    bool enabled;
    bool checked;
    gates_str_t label;
    const gates_shortcut_t *shortcut;
} menu_row_t;

/* The choice a list belongs to (its state), or null for a command menu. */
static const gates_widget_state_t *list_owner(const gates_tree_t *tree,
                                              const gates_widget_state_t *st) {
    if (!st->menu_is_list) {
        return nullptr;
    }
    gates_node_t owner = { .index = st->menu_scope_index,
                           .generation = st->menu_scope_generation };
    return gates_i_valid(tree, owner) ? state_at(tree, owner.index) : nullptr;
}

static menu_row_t menu_row(const gates_tree_t *tree, const gates_widget_state_t *st,
                           gates_i32 r) {
    menu_row_t row = {0};
    if (r < 0 || (gates_u32)r >= st->menu_count || st->menu_ids[r] == 0) {
        return row;
    }
    if (st->menu_is_list) {
        const gates_widget_state_t *owner = list_owner(tree, st);
        const gates_i_option_t *o = owner != nullptr ? gates_i_option_find(owner, st->menu_ids[r])
                                                     : nullptr;
        if (o != nullptr) {
            row = (menu_row_t){ .present = true, .enabled = !o->disabled,
                                .checked = o->id == owner->opt_sel,
                                .label = { .ptr = o->label, .size = o->label_len } };
        }
        return row;
    }
    const gates_i_command_t *c = gates_i_command_find(tree, st->menu_scope_index,
                                                      st->menu_scope_generation, st->menu_ids[r]);
    if (c != nullptr) {
        row = (menu_row_t){ .present = true, .enabled = c->enabled, .checked = c->checked,
                            .label = { .ptr = c->label, .size = c->label_len },
                            .shortcut = &c->shortcut };
    }
    return row;
}

static bool menu_selectable(const gates_tree_t *tree, const gates_widget_state_t *st,
                            gates_i32 row) {
    menu_row_t mr = menu_row(tree, st, row);
    return mr.present && mr.enabled;
}

static void menu_remove(gates_tree_t *tree, gates_u32 i, gates_u32 result) {
    gates_u32 idx = tree->overlays[i].index;
    drop_record(tree, i);
    gates_widget_state_t *st = state_at(tree, idx);
    if (st != nullptr && st->on_event != nullptr) {
        gates_err_t err = gates_i_event_reserve(tree, 1, 0);
        if (!gates_is_ok(err)) {
            tree->input_error = err; /* it still closes; only the report is lost */
            (void)gates_node_destroy(tree, gates_i_handle(tree, idx));
            return;
        }
    }
    closed_event(tree, idx, GATES_EVENT_MENU_CLOSED, result);
}

/* Invokes the selected entry (queued, re-checked) and closes the menu. */
static void menu_choose(gates_tree_t *tree, gates_u32 i) {
    gates_u32 idx = tree->overlays[i].index;
    gates_widget_state_t *st = state_at(tree, idx);
    if (st == nullptr || !menu_selectable(tree, st, st->menu_sel)) {
        return;
    }
    gates_command_id_t id = st->menu_ids[st->menu_sel];
    if (st->menu_is_list) {
        /* A choice's list: the choice takes the option (its own VALUE_CHANGED). */
        gates_u32 owner = st->menu_scope_index;
        const gates_widget_state_t *os = list_owner(tree, st);
        gates_err_t err = os != nullptr && !gates_i_widget_inert(tree, os)
                              ? gates_i_option_pick(tree, owner, id)
                              : PROVEN_ERR_INVALID_STATE;
        if (!gates_is_ok(err)) {
            tree->input_error = err;
            id = 0;
        }
        menu_remove(tree, i, id);
        return;
    }
    const gates_i_command_t *c = gates_i_command_find(tree, st->menu_scope_index,
                                                      st->menu_scope_generation, id);
    gates_err_t err = gates_i_event_reserve(tree, 1, 0);
    if (gates_is_ok(err)) {
        gates_i_command_push(tree, c, GATES_ORIGIN_USER);
    } else {
        tree->input_error = err;
        id = 0; /* nothing was invoked */
    }
    menu_remove(tree, i, id);
}

/* Builds a menu overlay (commands of `scope`, or a choice's option list). */
static gates_err_t menu_create(gates_tree_t *tree, gates_point_t at, gates_i32 above_y,
                               gates_node_t scope, const gates_command_id_t *ids, gates_u32 count,
                               gates_node_t *out_menu) {
    if (tree->overlay_count >= GATES_I_OVERLAY_MAX) {
        return PROVEN_ERR_OUT_OF_BOUNDS;
    }
    gates_allocator_t a = tree->alloc;
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, count * sizeof(gates_command_id_t),
                                           alignof(gates_command_id_t));
    if (!proven_is_ok(r.err)) {
        return r.err;
    }
    gates_command_id_t *copy = (gates_command_id_t *)r.value.ptr;
    memcpy(copy, ids, count * sizeof *copy);
    gates_node_desc_t nd = { .kind = GATES_NODE_MENU };
    gates_node_t m = GATES_NODE_NULL;
    gates_err_t err = gates_node_create(tree, GATES_NODE_NULL, &nd, &m);
    gates_u32 state = GATES_NONE;
    if (gates_is_ok(err)) {
        err = gates_i_state_acquire(tree, &state);
        if (!gates_is_ok(err)) {
            gates_i_discard_detached(tree, m);
        }
    }
    if (!gates_is_ok(err)) {
        a.free_fn(a.ctx, copy);
        return err;
    }
    gates_i_slot(tree, m.index)->state_index = state;
    gates_widget_state_t *st = state_at(tree, m.index);
    st->menu_ids = copy;
    st->menu_count = count;
    st->menu_sel = -1;
    st->menu_scope_index = scope.index;
    st->menu_scope_generation = scope.generation;
    tree->overlays[tree->overlay_count++] = (gates_i_overlay_t){
        .index = m.index,
        .generation = m.generation,
        .kind = GATES_NODE_MENU,
        .at = at,
        .above_y = above_y,
        .prev_focus_index = GATES_NONE,
        .prev_scope = GATES_NONE,
    };
    gates_i_mark_dirty(tree, m.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    *out_menu = m;
    return GATES_OK;
}

gates_err_t gates_menu_open(gates_tree_t *tree, gates_point_t at, gates_node_t scope,
                            const gates_command_id_t *ids, gates_u32 count,
                            gates_node_t *out_menu) {
    if (tree == nullptr || out_menu == nullptr || ids == nullptr || count == 0 ||
        !gates_i_valid(tree, scope)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_menu = GATES_NODE_NULL;
    return menu_create(tree, at, at.y, scope, ids, count, out_menu);
}

/* -- choice option list (plan-0010) --------------------------------------------- */

gates_i32 gates_i_choice_list_find(const gates_tree_t *tree, gates_u32 idx) {
    if (idx == GATES_NONE) {
        return -1;
    }
    gates_u32 gen = gates_i_slot(tree, idx)->generation;
    for (gates_u32 i = 0; i < tree->overlay_count; i++) {
        if (tree->overlays[i].kind != GATES_NODE_MENU) continue;
        const gates_widget_state_t *ms = state_at(tree, tree->overlays[i].index);
        if (ms != nullptr && ms->menu_is_list && ms->menu_scope_index == idx &&
            ms->menu_scope_generation == gen) {
            return (gates_i32)i;
        }
    }
    return -1;
}

void gates_i_choice_open(gates_tree_t *tree, gates_u32 idx) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    const gates_widget_state_t *cs = gates_i_state(tree, s->state_index);
    if (cs == nullptr || cs->opt_count == 0 || gates_i_widget_inert(tree, cs) ||
        gates_i_choice_list_find(tree, idx) >= 0) {
        return;
    }
    /* The rows are the option ids at opening time; labels and states are read live. */
    gates_command_id_t ids[64];
    gates_command_id_t *buf = ids;
    gates_allocator_t a = tree->alloc;
    if (cs->opt_count > 64) {
        proven_result_mem_mut_t r = a.alloc_fn(a.ctx, cs->opt_count * sizeof *buf, alignof(gates_command_id_t));
        if (!proven_is_ok(r.err)) {
            tree->input_error = r.err;
            return;
        }
        buf = (gates_command_id_t *)r.value.ptr;
    }
    for (gates_u32 k = 0; k < cs->opt_count; k++) {
        buf[k] = cs->opts[k].id;
    }
    gates_rect_t r = s->layout_rect;
    gates_node_t m = GATES_NODE_NULL;
    gates_err_t err = menu_create(tree, (gates_point_t){ r.x, r.y + r.h }, r.y,
                                  gates_i_handle(tree, idx), buf, cs->opt_count, &m);
    if (buf != ids) {
        a.free_fn(a.ctx, buf);
    }
    if (!gates_is_ok(err)) {
        tree->input_error = err;
        return;
    }
    gates_widget_state_t *ms = state_at(tree, m.index);
    ms->menu_is_list = true;
    ms->menu_min_w = r.w;
    ms->menu_sel = -1;
    for (gates_u32 k = 0; k < cs->opt_count; k++) {
        if (cs->opts[k].id == cs->opt_sel && !cs->opts[k].disabled) {
            ms->menu_sel = (gates_i32)k; /* opens on the current option */
        }
    }
}

void gates_i_choice_lists_check(gates_tree_t *tree, gates_u32 only_idx) {
    for (gates_u32 k = tree->overlay_count; k-- > 0;) {
        if (k >= tree->overlay_count || tree->overlays[k].kind != GATES_NODE_MENU) continue;
        const gates_widget_state_t *ms = state_at(tree, tree->overlays[k].index);
        if (ms == nullptr || !ms->menu_is_list) continue;
        gates_u32 owner = ms->menu_scope_index;
        bool close;
        if (only_idx != GATES_NONE) {
            close = owner == only_idx;
        } else {
            const gates_widget_state_t *os = list_owner(tree, ms);
            close = os == nullptr || gates_i_widget_inert(tree, os) ||
                    !gates_i_reachable(tree, owner);
        }
        if (close) {
            menu_remove(tree, k, 0);
        }
    }
}

gates_err_t gates_menu_close(gates_tree_t *tree, gates_node_t menu) {
    if (tree == nullptr || !gates_i_valid(tree, menu)) {
        return PROVEN_ERR_INVALID_STATE;
    }
    gates_i32 i = find_overlay(tree, menu.index, menu.generation);
    if (i < 0 || tree->overlays[i].kind != GATES_NODE_MENU) {
        return PROVEN_ERR_INVALID_STATE;
    }
    menu_remove(tree, (gates_u32)i, 0);
    return GATES_OK;
}

void gates_tree_dismiss_menus(gates_tree_t *tree) {
    if (tree == nullptr) {
        return;
    }
    for (gates_u32 k = tree->overlay_count; k-- > 0;) {
        if (tree->overlays[k].kind == GATES_NODE_MENU) {
            menu_remove(tree, k, 0);
        }
    }
}

gates_u32 gates_tree_overlay_count(const gates_tree_t *tree) {
    return tree != nullptr ? tree->overlay_count : 0;
}

bool gates_overlay_is_open(const gates_tree_t *tree, gates_node_t overlay) {
    return tree != nullptr && gates_i_valid(tree, overlay) &&
           find_overlay(tree, overlay.index, overlay.generation) >= 0;
}

void gates_i_menu_free(gates_tree_t *tree, gates_widget_state_t *st) {
    if (st->menu_ids != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, st->menu_ids);
    }
    st->menu_ids = nullptr;
    st->menu_count = 0;
}

/* -- layout ------------------------------------------------------------------- */

gates_rect_t gates_i_overlay_place(const gates_tree_t *tree, gates_u32 i, gates_size_t pref,
                                   gates_size_t viewport) {
    const gates_i_overlay_t *o = &tree->overlays[i];
    gates_i32 w = pref.w < viewport.w ? pref.w : viewport.w;
    gates_i32 h = pref.h < viewport.h ? pref.h : viewport.h;
    if (o->kind == GATES_NODE_DIALOG) {
        return (gates_rect_t){ (viewport.w - w) / 2, (viewport.h - h) / 2, w, h };
    }
    gates_i32 x = o->at.x, y = o->at.y;
    if (x + w > viewport.w) x = viewport.w - w;
    if (y + h > viewport.h) y = o->above_y - h >= 0 ? o->above_y - h : viewport.h - h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    return (gates_rect_t){ x, y, w, h };
}

void gates_i_overlays_after_layout(gates_tree_t *tree) {
    for (gates_u32 i = 0; i < tree->overlay_count; i++) {
        gates_i_overlay_t *o = &tree->overlays[i];
        if (o->kind == GATES_NODE_DIALOG && o->needs_focus) {
            o->needs_focus = false;
            if (i == tree->overlay_count - 1 || tree->focus == GATES_NONE) {
                (void)gates_tree_focus_next(tree, false);
            }
        }
    }
}

/* -- menu geometry, paint, input ------------------------------------------------ */

static gates_i32 row_height(const gates_tree_t *tree) {
    gates_i32 lh = tree->line_height > 0 ? tree->line_height : 16;
    gates_i32 h = lh + MENU_ROW_EXTRA;
    return h < GATES_ACCESS_MIN_TARGET ? GATES_ACCESS_MIN_TARGET : h; /* WCAG 2.5.8 */
}

/* "Ctrl+Shift+S", "F5": written into buf, returns the length. */
static gates_usize_t shortcut_text(const gates_shortcut_t *k, char *buf, gates_usize_t cap) {
    gates_usize_t n = 0;
    const char *parts[3] = { k->ctrl ? "Ctrl+" : "", k->shift ? "Shift+" : "", "" };
    for (int p = 0; p < 2; p++) {
        for (const char *c = parts[p]; *c && n + 1 < cap; c++) buf[n++] = *c;
    }
    char name[8] = {0};
    if (k->letter != 0) {
        name[0] = (char)k->letter;
    } else if (k->key >= GATES_KEY_F1 && k->key <= GATES_KEY_F12) {
        int f = (int)(k->key - GATES_KEY_F1) + 1;
        name[0] = 'F';
        name[1] = (char)('0' + (f >= 10 ? 1 : f));
        if (f >= 10) name[2] = (char)('0' + f - 10);
    } else {
        static const struct { gates_key_t key; const char *text; } names[] = {
            { GATES_KEY_A, "A" }, { GATES_KEY_C, "C" }, { GATES_KEY_X, "X" },
            { GATES_KEY_V, "V" }, { GATES_KEY_Z, "Z" }, { GATES_KEY_Y, "Y" },
            { GATES_KEY_DELETE, "Del" }, { GATES_KEY_ENTER, "Enter" },
            { GATES_KEY_SPACE, "Space" }, { GATES_KEY_HOME, "Home" }, { GATES_KEY_END, "End" },
        };
        for (gates_usize_t q = 0; q < sizeof names / sizeof names[0]; q++) {
            if (names[q].key == k->key) {
                strncpy(name, names[q].text, sizeof name - 1);
            }
        }
    }
    if (name[0] == 0) {
        return 0; /* no shortcut */
    }
    for (const char *c = name; *c && n + 1 < cap; c++) buf[n++] = *c;
    return n;
}

gates_size_t gates_i_menu_measure(const gates_tree_t *tree, const gates_widget_state_t *st,
                                  const gates_text_backend_t *text, gates_i32 font_size) {
    gates_text_metrics_t m = text->metrics(text->ctx, font_size);
    gates_i32 row_h = m.line_height + MENU_ROW_EXTRA;
    if (row_h < GATES_ACCESS_MIN_TARGET) row_h = GATES_ACCESS_MIN_TARGET;
    gates_i32 label_w = 0, key_w = 0;
    for (gates_u32 r = 0; r < st->menu_count; r++) {
        menu_row_t mr = menu_row(tree, st, (gates_i32)r);
        if (!mr.present) continue;
        gates_size_t ls = text->measure(text->ctx, font_size, mr.label);
        if (ls.w > label_w) label_w = ls.w;
        char buf[24];
        gates_usize_t n = mr.shortcut != nullptr ? shortcut_text(mr.shortcut, buf, sizeof buf) : 0;
        if (n > 0) {
            gates_size_t ks = text->measure(text->ctx, font_size,
                                            (gates_str_t){ .ptr = (const gates_u8 *)buf, .size = n });
            if (ks.w > key_w) key_w = ks.w;
        }
    }
    gates_i32 w = 2 * MENU_PAD + 2 * m.advance + label_w + (key_w > 0 ? 3 * m.advance + key_w : 0) +
                  m.advance;
    gates_i32 h = 2 * MENU_PAD + (gates_i32)st->menu_count * row_h;
    if (w < st->menu_min_w) {
        w = st->menu_min_w;
    }
    return (gates_size_t){ w, h };
}

gates_err_t gates_i_menu_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                               const gates_theme_t *theme, const gates_text_backend_t *text) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    gates_rect_t r = s->layout_rect;
    gates_i32 font = gates_i_font(tree, idx);
    gates_text_metrics_t m = text->metrics(text->ctx, font);
    gates_i32 row_h = m.line_height + MENU_ROW_EXTRA;
    if (row_h < GATES_ACCESS_MIN_TARGET) row_h = GATES_ACCESS_MIN_TARGET;
    gates_err_t err = gates_draw_rect(dl, r, gates_theme_color(theme, GATES_COLOR_CONTROL_BG));
    if (gates_is_ok(err)) {
        err = gates_draw_border(dl, r, 1, gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER));
    }
    for (gates_u32 row = 0; st != nullptr && row < st->menu_count && gates_is_ok(err); row++) {
        gates_rect_t rr = { r.x + MENU_PAD, r.y + MENU_PAD + (gates_i32)row * row_h,
                            r.w - 2 * MENU_PAD, row_h };
        if (st->menu_ids[row] == 0) {
            err = gates_draw_rect(dl, (gates_rect_t){ rr.x, rr.y + row_h / 2, rr.w, 1 },
                                  gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER));
            continue;
        }
        menu_row_t c = menu_row(tree, st, (gates_i32)row);
        if (!c.present) continue;
        bool sel = (gates_i32)row == st->menu_sel && c.enabled;
        gates_color_token_t fg = !c.enabled ? GATES_COLOR_CONTROL_DISABLED_FG
                                 : sel       ? GATES_COLOR_SELECTION_FG
                                             : GATES_COLOR_CONTROL_FG;
        if (sel) {
            err = gates_draw_rect(dl, rr, gates_theme_color(theme, GATES_COLOR_SELECTION_BG));
        }
        gates_i32 ty = rr.y + (row_h - m.line_height) / 2;
        if (gates_is_ok(err) && c.checked) {
            gates_i32 box = m.line_height / 2;
            err = gates_draw_rect(dl, (gates_rect_t){ rr.x + (m.advance * 2 - box) / 2,
                                                      ty + (m.line_height - box) / 2, box, box },
                                  gates_theme_color(theme, fg));
        }
        if (gates_is_ok(err) && c.label.size > 0) {
            gates_str_t lab = c.label;
            gates_size_t ls = text->measure(text->ctx, font, lab);
            err = gates_draw_text(dl, (gates_rect_t){ rr.x + 2 * m.advance, ty, ls.w, ls.h }, lab, font,
                                  gates_theme_color(theme, fg));
        }
        char buf[24];
        gates_usize_t n = c.shortcut != nullptr ? shortcut_text(c.shortcut, buf, sizeof buf) : 0;
        if (gates_is_ok(err) && n > 0) {
            gates_str_t ks = { .ptr = (const gates_u8 *)buf, .size = n };
            gates_size_t sz = text->measure(text->ctx, font, ks);
            err = gates_draw_text(dl, (gates_rect_t){ rr.x + rr.w - m.advance - sz.w, ty, sz.w, sz.h },
                                  ks, font, gates_theme_color(theme, fg));
        }
    }
    return err;
}

static gates_i32 menu_row_at(const gates_tree_t *tree, gates_u32 idx, gates_point_t p) {
    gates_rect_t r = gates_i_slot(tree, idx)->layout_rect;
    if (!gates_rect_contains(r, p)) {
        return -2; /* outside the menu */
    }
    gates_i32 y = p.y - r.y - MENU_PAD;
    if (y < 0) {
        return -1;
    }
    gates_i32 row = y / row_height(tree);
    const gates_widget_state_t *st = state_at(tree, idx);
    return (st != nullptr && (gates_u32)row < st->menu_count) ? row : -1;
}

static void menu_select(gates_tree_t *tree, gates_u32 idx, gates_i32 row) {
    gates_widget_state_t *st = state_at(tree, idx);
    if (st != nullptr && st->menu_sel != row) {
        st->menu_sel = row;
        gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    }
}

gates_u32 gates_i_overlay_pointer(gates_tree_t *tree, const gates_pointer_event_t *ev,
                                  bool *consumed) {
    *consumed = false;
    if (tree->overlay_count == 0) {
        return tree->root;
    }
    gates_u32 top = tree->overlay_count - 1;
    gates_i_overlay_t *o = &tree->overlays[top];
    if (o->kind == GATES_NODE_DIALOG) {
        return o->index; /* modal: nothing below is hit */
    }
    *consumed = true;
    gates_u32 idx = o->index;
    gates_widget_state_t *st = state_at(tree, idx);
    gates_i32 row = menu_row_at(tree, idx, ev->pos);
    switch (ev->action) {
    case GATES_POINTER_MOVE:
        if (row >= 0) {
            menu_select(tree, idx, menu_selectable(tree, st, row) ? row : -1);
        }
        break;
    case GATES_POINTER_DOWN:
        if (row == -2) {
            menu_remove(tree, top, 0); /* outside: only closes (owner decision) */
            return GATES_NONE;
        }
        o->pressed_inside = true;
        if (row >= 0) {
            menu_select(tree, idx, menu_selectable(tree, st, row) ? row : -1);
        }
        break;
    case GATES_POINTER_UP:
        if (o->pressed_inside && row >= 0 && menu_selectable(tree, st, row)) {
            menu_select(tree, idx, row);
            menu_choose(tree, top);
            return GATES_NONE;
        }
        o->pressed_inside = false;
        break;
    default:
        break;
    }
    return GATES_NONE;
}

static void menu_step(gates_tree_t *tree, gates_u32 idx, gates_i32 from, gates_i32 dir) {
    gates_widget_state_t *st = state_at(tree, idx);
    gates_i32 n = (gates_i32)st->menu_count;
    for (gates_i32 k = 1; k <= n; k++) {
        gates_i32 row = ((from + dir * k) % n + n) % n;
        if (menu_selectable(tree, st, row)) {
            menu_select(tree, idx, row);
            return;
        }
    }
}

bool gates_i_overlay_key(gates_tree_t *tree, const gates_key_event_t *ev) {
    if (tree->overlay_count == 0 ||
        tree->overlays[tree->overlay_count - 1].kind != GATES_NODE_MENU) {
        return false;
    }
    gates_u32 top = tree->overlay_count - 1;
    gates_u32 idx = tree->overlays[top].index;
    gates_widget_state_t *st = state_at(tree, idx);
    gates_i32 n = (gates_i32)st->menu_count;
    switch (ev->key) {
    case GATES_KEY_DOWN:
        menu_step(tree, idx, st->menu_sel < 0 ? -1 : st->menu_sel, 1);
        break;
    case GATES_KEY_UP:
        menu_step(tree, idx, st->menu_sel < 0 ? n : st->menu_sel, -1);
        break;
    case GATES_KEY_HOME:
        menu_step(tree, idx, -1, 1);
        break;
    case GATES_KEY_END:
        menu_step(tree, idx, n, -1);
        break;
    case GATES_KEY_ENTER:
    case GATES_KEY_SPACE:
        menu_choose(tree, top);
        break;
    case GATES_KEY_ESCAPE:
        menu_remove(tree, top, 0);
        break;
    default:
        break; /* a menu takes every key while it is open */
    }
    return true;
}

/* -- accessibility (plan-0014) ------------------------------------------------ */

gates_err_t gates_i_menu_invoke(gates_tree_t *tree, gates_u32 menu_idx, gates_command_id_t id) {
    gates_i32 found = -1;
    for (gates_u32 i = 0; i < tree->overlay_count; i++) {
        if (tree->overlays[i].index == menu_idx && tree->overlays[i].kind == GATES_NODE_MENU) {
            found = (gates_i32)i;
        }
    }
    gates_widget_state_t *st = state_at(tree, menu_idx);
    if (found < 0 || st == nullptr) {
        return PROVEN_ERR_INVALID_STATE;
    }
    for (gates_u32 r = 0; r < st->menu_count; r++) {
        if (st->menu_ids[r] == id && id != 0) {
            if (!menu_selectable(tree, st, (gates_i32)r)) {
                return PROVEN_ERR_INVALID_STATE; /* disabled entry: refused like a click */
            }
            st->menu_sel = (gates_i32)r;
            gates_err_t before = tree->input_error;
            tree->input_error = GATES_OK;
            menu_choose(tree, (gates_u32)found);
            gates_err_t err = tree->input_error;
            tree->input_error = before;
            return err;
        }
    }
    return PROVEN_ERR_INVALID_ARG;
}

gates_rect_t gates_i_menu_row_rect(const gates_tree_t *tree, gates_u32 menu_idx, gates_u32 row) {
    gates_rect_t r = gates_i_slot(tree, menu_idx)->layout_rect;
    gates_i32 h = row_height(tree);
    return (gates_rect_t){ r.x + MENU_PAD, r.y + MENU_PAD + (gates_i32)row * h, r.w - 2 * MENU_PAD, h };
}
