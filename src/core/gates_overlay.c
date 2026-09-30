/* gates_gui_lib - overlays: modal dialog and context menu with one set of
 * dismissal rules. Platform-free. */
#include <gates/overlay.h>
#include <gates/frame.h>
#include <gates/widget.h>
#include <gates/layout.h>
#include <gates/ui.h>
#include <gates/timer.h>
#include "gates_tree_internal.h"

#include <string.h>

#define MENU_PAD 4
#define MENU_SEP_H 9 /* a separator row: a line with room around it (0.8.0) */
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
    if (st != nullptr && gates_i_wants_events(tree, idx) && tree->event_len < tree->event_cap) {
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
    gates_i_tip_dismiss(tree);        /* nor a tooltip for a node the dialog now covers (0.10.0) */
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
    if (st != nullptr && gates_i_wants_events(tree, dialog.index)) {
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
    bool markup;                 /* command labels carry mnemonic markup; options do not */
    gates_u32 icon;              /* the command's icon (0.5.0), 0 = none */
    bool enabled;
    bool checked;
    gates_str_t label;
    const gates_shortcut_t *shortcut;
    bool sub;                    /* opens a submenu (0.10.0) */
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
        row = (menu_row_t){ .present = true, .markup = true, .icon = c->icon, .enabled = c->enabled, .checked = c->checked,
                            .label = { .ptr = c->label, .size = c->label_len },
                            .shortcut = &c->shortcut, .sub = c->sub_count > 0 };
    }
    return row;
}

static bool menu_selectable(const gates_tree_t *tree, const gates_widget_state_t *st,
                            gates_i32 row) {
    menu_row_t mr = menu_row(tree, st, row);
    return mr.present && mr.enabled;
}

/* -- submenus (0.10.0) ------------------------------------------------------------ */

/* The overlay record of a menu's parent, or -1 (a menu the program opened). */
static gates_i32 parent_overlay(const gates_tree_t *tree, gates_u32 i) {
    const gates_widget_state_t *st = state_at(tree, tree->overlays[i].index);
    if (st == nullptr || st->menu_parent_index == GATES_NONE) return -1;
    return find_overlay(tree, st->menu_parent_index, st->menu_parent_generation);
}

/* The menu the program opened at the start of this one's chain. */
static gates_u32 chain_root(const gates_tree_t *tree, gates_u32 i) {
    for (gates_i32 p = parent_overlay(tree, i); p >= 0; p = parent_overlay(tree, (gates_u32)p)) i = (gates_u32)p;
    return i;
}

static bool in_chain_of(const gates_tree_t *tree, gates_u32 i, gates_u32 ancestor) {
    for (gates_i32 p = (gates_i32)i; p >= 0; p = parent_overlay(tree, (gates_u32)p)) {
        if ((gates_u32)p == ancestor) return true;
    }
    return false;
}

static void menu_remove(gates_tree_t *tree, gates_u32 i, gates_u32 result);

/* Closes the submenus open above menu i (its children, theirs...). */
static void close_children(gates_tree_t *tree, gates_u32 i) {
    while (tree->overlay_count > i + 1) {
        gates_u32 top = tree->overlay_count - 1;
        if (tree->overlays[top].kind != GATES_NODE_MENU || !in_chain_of(tree, top, i)) break;
        menu_remove(tree, top, 0);
    }
    gates_widget_state_t *st = i < tree->overlay_count ? state_at(tree, tree->overlays[i].index) : nullptr;
    if (st != nullptr && st->menu_child_row >= 0) {
        st->menu_child_row = -1;
        gates_i_mark_dirty(tree, tree->overlays[i].index, GATES_DIRTY_PAINT);
    }
}

static gates_err_t menu_create(gates_tree_t *tree, gates_point_t at, gates_i32 above_y,
                               gates_node_t scope, const gates_command_id_t *ids, gates_u32 count,
                               gates_node_t *out_menu);
static void menu_step(gates_tree_t *tree, gates_u32 idx, gates_i32 from, gates_i32 dir);
gates_rect_t gates_i_menu_row_rect(const gates_tree_t *tree, gates_u32 menu_idx, gates_u32 row);

/* Opens the submenu of menu i's row (its command's list), beside the row. */
static gates_err_t open_sub(gates_tree_t *tree, gates_u32 i, gates_i32 row, bool keyboard) {
    gates_u32 idx = tree->overlays[i].index;
    gates_widget_state_t *st = state_at(tree, idx);
    if (st == nullptr || row < 0 || (gates_u32)row >= st->menu_count) return PROVEN_ERR_INVALID_ARG;
    if (st->menu_child_row == row && tree->overlay_count > i + 1) return GATES_OK; /* open already */
    close_children(tree, i);
    const gates_i_command_t *c = gates_i_command_find(tree, st->menu_scope_index, st->menu_scope_generation,
                                                      st->menu_ids[row]);
    if (c == nullptr || c->sub_count == 0 || !c->enabled) return PROVEN_ERR_INVALID_STATE;
    gates_rect_t rr = gates_i_menu_row_rect(tree, idx, (gates_u32)row);
    gates_rect_t mr = gates_i_slot(tree, idx)->layout_rect;
    gates_node_t child;
    gates_err_t err = menu_create(tree, (gates_point_t){ mr.x + mr.w - 2, rr.y - MENU_PAD }, rr.y + rr.h,
                                  (gates_node_t){ .index = st->menu_scope_index, .generation = st->menu_scope_generation },
                                  c->sub, c->sub_count, &child);
    if (!gates_is_ok(err)) return err;
    gates_widget_state_t *cs = state_at(tree, child.index);
    cs->menu_parent_index = idx;
    cs->menu_parent_generation = gates_i_slot(tree, idx)->generation;
    gates_i_overlay_t *co = &tree->overlays[tree->overlay_count - 1];
    co->flip_left = true;
    co->alt_right = mr.x + 2;
    st = state_at(tree, idx);
    st->menu_child_row = row;
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    gates_i_access_log(tree, GATES_ACCESS_CHANGED, idx, st->menu_ids[row]); /* expanded */
    if (keyboard) menu_step(tree, child.index, -1, 1);
    return GATES_OK;
}

static void menu_remove(gates_tree_t *tree, gates_u32 i, gates_u32 result) {
    close_children(tree, i);
    if (tree->sub_timer != 0 && tree->sub_menu_index == tree->overlays[i].index) {
        (void)gates_timer_cancel(tree, tree->sub_timer);
        tree->sub_timer = 0;
    }
    gates_u32 idx = tree->overlays[i].index;
    gates_i32 parent = parent_overlay(tree, i);
    drop_record(tree, i);
    if (parent >= 0) { /* a submenu: its parent's row is no longer open */
        gates_widget_state_t *ps = state_at(tree, tree->overlays[parent].index);
        if (ps != nullptr) {
            ps->menu_child_row = -1;
            gates_i_mark_dirty(tree, tree->overlays[parent].index, GATES_DIRTY_PAINT);
        }
    }
    gates_widget_state_t *st = state_at(tree, idx);
    bool from_bar = st != nullptr && st->menu_from_bar;
    if (st != nullptr && gates_i_wants_events(tree, idx)) {
        gates_err_t err = gates_i_event_reserve(tree, 1, 0);
        if (!gates_is_ok(err)) {
            tree->input_error = err; /* it still closes; only the report is lost */
            (void)gates_node_destroy(tree, gates_i_handle(tree, idx));
            if (from_bar) gates_i_menubar_menu_closed(tree);
            return;
        }
    }
    closed_event(tree, idx, GATES_EVENT_MENU_CLOSED, result);
    if (from_bar) {
        gates_i_menubar_menu_closed(tree); /* menu mode ends unless switching */
    }
}

gates_i32 gates_i_bar_menu_overlay(const gates_tree_t *tree) {
    for (gates_u32 i = 0; i < tree->overlay_count; i++) {
        if (tree->overlays[i].kind != GATES_NODE_MENU) continue;
        const gates_widget_state_t *ms = state_at(tree, tree->overlays[i].index);
        if (ms != nullptr && ms->menu_from_bar) {
            return (gates_i32)i;
        }
    }
    return -1;
}

void gates_i_menu_close_record(gates_tree_t *tree, gates_u32 i) {
    menu_remove(tree, i, 0);
}

/* Invokes the selected entry (queued, re-checked) and closes the menu. */
static void menu_choose(gates_tree_t *tree, gates_u32 i) {
    gates_u32 idx = tree->overlays[i].index;
    gates_widget_state_t *st = state_at(tree, idx);
    if (st == nullptr || !menu_selectable(tree, st, st->menu_sel)) {
        return;
    }
    if (menu_row(tree, st, st->menu_sel).sub) { /* a submenu entry opens, never invokes */
        gates_err_t err = open_sub(tree, i, st->menu_sel, true);
        if (!gates_is_ok(err)) tree->input_error = err;
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
    menu_remove(tree, chain_root(tree, i), id); /* the whole chain; the program's menu reports it */
}

/* Builds a menu overlay (commands of `scope`, or a choice's option list). */
static gates_err_t menu_create(gates_tree_t *tree, gates_point_t at, gates_i32 above_y,
                               gates_node_t scope, const gates_command_id_t *ids, gates_u32 count,
                               gates_node_t *out_menu) {
    if (count == 0 || tree->overlay_count >= GATES_I_OVERLAY_MAX) {
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
    st->menu_parent_index = GATES_NONE;
    st->menu_child_row = -1;
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

static void menu_step(gates_tree_t *tree, gates_u32 idx, gates_i32 from, gates_i32 dir);

gates_err_t gates_i_menu_open_for_bar(gates_tree_t *tree, gates_point_t at, gates_i32 above_y,
                                      gates_node_t scope, const gates_command_id_t *ids,
                                      gates_u32 count, gates_u32 title, bool keyboard,
                                      gates_node_t *out_menu) {
    gates_err_t err = menu_create(tree, at, above_y, scope, ids, count, out_menu);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_widget_state_t *st = state_at(tree, out_menu->index);
    st->menu_from_bar = true;
    st->menu_bar_title = title;
    if (keyboard) {
        menu_step(tree, out_menu->index, -1, 1); /* the first enabled entry */
    }
    return GATES_OK;
}

/* -- choice option list --------------------------------------------- */

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
    if (tree->mb_mode != GATES_I_MB_OFF) {
        gates_i_menubar_leave(tree); /* a highlighted title too (0.3.0) */
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
    if (x + w > viewport.w && o->flip_left && o->alt_right - w >= 0) x = o->alt_right - w; /* a submenu opens left */
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

/* A row's height and its top below the menu's padding: separators are short. */
static gates_i32 menu_row_h(const gates_widget_state_t *st, gates_u32 row, gates_i32 row_h) {
    return st->menu_ids[row] == 0 ? MENU_SEP_H : row_h;
}

static gates_i32 menu_row_top(const gates_widget_state_t *st, gates_u32 row, gates_i32 row_h) {
    gates_i32 y = 0;
    for (gates_u32 k = 0; k < row && k < st->menu_count; k++) y += menu_row_h(st, k, row_h);
    return y;
}

static gates_usize_t shortcut_text(const gates_shortcut_t *k, char *buf, gates_usize_t cap) {
    return gates_i_shortcut_text(k, buf, cap); /* one formatter (gates_command.c) */
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
        gates_i32 lw = mr.markup ? gates_i_mn_width(text, font_size, mr.label)
                                 : text->measure(text->ctx, font_size, mr.label).w;
        if (lw > label_w) label_w = lw;
        char buf[24];
        gates_usize_t n = mr.shortcut != nullptr ? shortcut_text(mr.shortcut, buf, sizeof buf) : 0;
        if (mr.sub) { /* room for the submenu arrow */
            buf[0] = '>';
            n = 1;
        }
        if (n > 0) {
            gates_size_t ks = text->measure(text->ctx, font_size,
                                            (gates_str_t){ .ptr = (const gates_u8 *)buf, .size = n });
            if (ks.w > key_w) key_w = ks.w;
        }
    }
    gates_i32 gut = 2 * m.advance > GATES_ICON_SIZE + 4 ? 2 * m.advance : GATES_ICON_SIZE + 4; /* check or icon */
    gates_i32 w = 2 * MENU_PAD + gut + label_w + (key_w > 0 ? 3 * m.advance + key_w : 0) +
                  m.advance;
    gates_i32 h = 2 * MENU_PAD + menu_row_top(st, st->menu_count, row_h);
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
    gates_i32 top = r.y + MENU_PAD;
    for (gates_u32 row = 0; st != nullptr && row < st->menu_count && gates_is_ok(err); row++) {
        gates_rect_t rr = { r.x + MENU_PAD, top, r.w - 2 * MENU_PAD, menu_row_h(st, row, row_h) };
        top += rr.h;
        if (st->menu_ids[row] == 0) {
            err = gates_draw_rect(dl, (gates_rect_t){ rr.x, rr.y + rr.h / 2, rr.w, 1 },
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
        gates_i32 gut = 2 * m.advance > GATES_ICON_SIZE + 4 ? 2 * m.advance : GATES_ICON_SIZE + 4;
        if (gates_is_ok(err) && c.checked) {
            gates_i32 box = m.line_height / 2;
            err = gates_draw_rect(dl, (gates_rect_t){ rr.x + (gut - box) / 2,
                                                      ty + (m.line_height - box) / 2, box, box },
                                  gates_theme_color(theme, fg));
        } else if (gates_is_ok(err) && c.icon != 0) {
            err = gates_i_draw_image_fit(tree, dl, c.icon,
                                         (gates_rect_t){ rr.x + (gut - GATES_ICON_SIZE) / 2,
                                                         rr.y + (row_h - GATES_ICON_SIZE) / 2, GATES_ICON_SIZE,
                                                         GATES_ICON_SIZE });
        }
        if (gates_is_ok(err) && c.label.size > 0) {
            gates_str_t lab = c.label;
            if (c.markup) {
                err = gates_i_mn_draw(dl, text, (gates_rect_t){ rr.x + gut, ty, 0, m.line_height },
                                      lab, font, gates_theme_color(theme, fg), gates_i_cues(tree));
            } else {
                gates_size_t ls = text->measure(text->ctx, font, lab);
                err = gates_draw_text(dl, (gates_rect_t){ rr.x + gut, ty, ls.w, ls.h }, lab, font,
                                      gates_theme_color(theme, fg));
            }
        }
        char buf[24];
        gates_usize_t n = c.shortcut != nullptr ? shortcut_text(c.shortcut, buf, sizeof buf) : 0;
        if (c.sub) { /* a submenu: an arrow where a shortcut would be */
            buf[0] = '>';
            n = 1;
        }
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
    const gates_widget_state_t *st = state_at(tree, idx);
    gates_i32 h = row_height(tree);
    for (gates_u32 row = 0; st != nullptr && row < st->menu_count; row++) {
        gates_i32 rh = menu_row_h(st, row, h);
        if (y < rh) return (gates_i32)row;
        y -= rh;
    }
    return -1;
}

static void menu_select(gates_tree_t *tree, gates_u32 idx, gates_i32 row) {
    gates_widget_state_t *st = state_at(tree, idx);
    if (st != nullptr && st->menu_sel != row) {
        st->menu_sel = row;
        gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    }
}

/* Resting on a submenu row: it opens after GATES_MENU_SUB_DELAY_MS (at once
 * without a clock); resting on another row of a parent closes its children. */
static void sub_fire(gates_tree_t *tree, gates_node_t node, gates_timer_id_t id, void *user) {
    (void)node;
    (void)user;
    if (id != tree->sub_timer) return;
    tree->sub_timer = 0;
    gates_i32 i = find_overlay(tree, tree->sub_menu_index, tree->sub_menu_generation);
    if (i < 0) return;
    const gates_widget_state_t *st = state_at(tree, tree->overlays[i].index);
    if (st == nullptr || st->menu_sel != tree->sub_row) return; /* moved on */
    gates_err_t err = open_sub(tree, (gates_u32)i, tree->sub_row, false);
    if (!gates_is_ok(err)) tree->input_error = err;
}

static void hover_row(gates_tree_t *tree, gates_u32 i, gates_i32 row) {
    gates_u32 idx = tree->overlays[i].index;
    gates_widget_state_t *st = state_at(tree, idx);
    if (st == nullptr || row == st->menu_child_row) return; /* on the row whose submenu is open */
    if (tree->sub_timer != 0) {
        (void)gates_timer_cancel(tree, tree->sub_timer);
        tree->sub_timer = 0;
    }
    if (st->menu_child_row >= 0) close_children(tree, i);
    if (row < 0 || !menu_row(tree, st, row).sub || !menu_selectable(tree, st, row)) return;
    tree->sub_menu_index = idx;
    tree->sub_menu_generation = gates_i_slot(tree, idx)->generation;
    tree->sub_row = row;
    gates_timer_id_t t = 0;
    if (gates_is_ok(gates_timer_start(tree, gates_i_handle(tree, tree->root), GATES_MENU_SUB_DELAY_MS, false, sub_fire,
                                      nullptr, &t))) {
        tree->sub_timer = t;
    } else {
        gates_err_t err = open_sub(tree, i, row, false); /* no clock: at once */
        if (!gates_is_ok(err)) tree->input_error = err;
    }
}

gates_u32 gates_i_overlay_pointer(gates_tree_t *tree, const gates_pointer_event_t *ev,
                                  bool *consumed) {
    *consumed = false;
    if (tree->overlay_count == 0) {
        return tree->root;
    }
    gates_u32 top = tree->overlay_count - 1;
    if (tree->overlays[top].kind == GATES_NODE_DIALOG) {
        return tree->overlays[top].index; /* modal: nothing below is hit */
    }
    *consumed = true;
    /* The menu of the top chain under the pointer (a submenu over its parents). */
    gates_u32 root_i = chain_root(tree, top);
    gates_i32 hit = -1, row = -2;
    for (gates_i32 k = (gates_i32)top; k >= (gates_i32)root_i; k--) {
        if (tree->overlays[k].kind != GATES_NODE_MENU || !in_chain_of(tree, top, (gates_u32)k)) continue;
        gates_i32 r = menu_row_at(tree, tree->overlays[k].index, ev->pos);
        if (r != -2) {
            hit = k;
            row = r;
            break;
        }
    }
    gates_widget_state_t *rst = state_at(tree, tree->overlays[root_i].index);
    /* over the menu bar while one of its menus is open. */
    if (hit < 0 && rst != nullptr && rst->menu_from_bar && tree->menubar != GATES_NONE &&
        gates_rect_contains(gates_i_slot(tree, tree->menubar)->layout_rect, ev->pos)) {
        gates_i32 t = gates_i_menubar_title_at(tree, tree->menubar, ev->pos);
        gates_err_t err = GATES_OK;
        if (ev->action == GATES_POINTER_MOVE && t >= 0 && (gates_u32)t != rst->menu_bar_title) {
            err = gates_i_menubar_open(tree, (gates_u32)t, false);
        } else if (ev->action == GATES_POINTER_DOWN) {
            if (t < 0 || (gates_u32)t == rst->menu_bar_title) {
                gates_i_menubar_leave(tree);
            } else {
                err = gates_i_menubar_open(tree, (gates_u32)t, false);
            }
        }
        if (!gates_is_ok(err)) {
            tree->input_error = err;
        }
        return GATES_NONE;
    }
    gates_u32 i = hit >= 0 ? (gates_u32)hit : top;
    gates_i_overlay_t *o = &tree->overlays[i];
    gates_u32 idx = o->index;
    gates_widget_state_t *st = state_at(tree, idx);
    switch (ev->action) {
    case GATES_POINTER_MOVE:
        if (hit >= 0 && row >= 0) {
            menu_select(tree, idx, menu_selectable(tree, st, row) ? row : -1);
            hover_row(tree, i, row);
        }
        break;
    case GATES_POINTER_DOWN:
        if (hit < 0) {
            menu_remove(tree, root_i, 0); /* outside every menu of the chain: only closes (owner decision) */
            return GATES_NONE;
        }
        o->pressed_inside = true;
        if (row >= 0) {
            menu_select(tree, idx, menu_selectable(tree, st, row) ? row : -1);
        }
        break;
    case GATES_POINTER_UP:
        if (hit >= 0 && o->pressed_inside && row >= 0 && menu_selectable(tree, st, row)) {
            o->pressed_inside = false;
            menu_select(tree, idx, row);
            menu_choose(tree, i); /* a submenu row opens it at once */
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
        if (st->menu_from_bar) {
            gates_i_menu_close_record(tree, top); /* back to the highlighted title */
            tree->mb_mode = GATES_I_MB_HIGHLIGHT;
        } else {
            menu_remove(tree, top, 0);
        }
        break;
    case GATES_KEY_LEFT:
    case GATES_KEY_RIGHT: {
        bool right = ev->key == GATES_KEY_RIGHT;
        if (right && menu_selectable(tree, st, st->menu_sel) && menu_row(tree, st, st->menu_sel).sub) {
            gates_err_t err = open_sub(tree, top, st->menu_sel, true); /* into the submenu */
            if (!gates_is_ok(err)) tree->input_error = err;
            break;
        }
        if (!right && parent_overlay(tree, top) >= 0) {
            menu_remove(tree, top, 0); /* out of the submenu */
            break;
        }
        const gates_widget_state_t *rs = state_at(tree, tree->overlays[chain_root(tree, top)].index);
        if (rs != nullptr && rs->menu_from_bar && tree->menubar != GATES_NONE) {
            gates_u32 bn = gates_menubar_count(tree, gates_i_handle(tree, tree->menubar));
            if (bn > 0) {
                gates_u32 t = (rs->menu_bar_title + (right ? 1 : bn - 1)) % bn;
                gates_err_t err = gates_i_menubar_open(tree, t, true);
                if (!gates_is_ok(err)) tree->input_error = err;
            }
        }
        break;
    }
    case GATES_KEY_F10: {
        const gates_widget_state_t *rs = state_at(tree, tree->overlays[chain_root(tree, top)].index);
        if (rs != nullptr && rs->menu_from_bar) {
            gates_i_menubar_leave(tree);
        }
        break;
    }
    default:
        /* A letter chooses the entry whose mnemonic it is (0.3.0). */
        if (ev->letter != 0 && !ev->ctrl && !st->menu_is_list) {
            gates_u8 l = ev->letter >= 'a' && ev->letter <= 'z' ? (gates_u8)(ev->letter - 32) : ev->letter;
            for (gates_i32 r = 0; r < n; r++) {
                menu_row_t mr = menu_row(tree, st, r);
                if (mr.present && mr.enabled && mr.markup && gates_mnemonic_of(mr.label) == l) {
                    menu_select(tree, idx, r);
                    menu_choose(tree, top);
                    break;
                }
            }
        }
        break; /* a menu takes every key while it is open */
    }
    return true;
}

/* -- accessibility ------------------------------------------------ */

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

/* Whether an entry opens a submenu (and whether it is open). */
bool gates_i_menu_sub_state(const gates_tree_t *tree, gates_u32 menu_idx, gates_command_id_t id, bool *open) {
    const gates_widget_state_t *st = state_at(tree, menu_idx);
    *open = false;
    for (gates_u32 r = 0; st != nullptr && r < st->menu_count; r++) {
        if (st->menu_ids[r] != id || id == 0) continue;
        if (!menu_row(tree, st, (gates_i32)r).sub) return false;
        *open = st->menu_child_row == (gates_i32)r;
        return true;
    }
    return false;
}

gates_err_t gates_i_menu_expand(gates_tree_t *tree, gates_u32 menu_idx, gates_command_id_t id, bool expand) {
    gates_i32 i = -1;
    for (gates_u32 k = 0; k < tree->overlay_count; k++) {
        if (tree->overlays[k].index == menu_idx && tree->overlays[k].kind == GATES_NODE_MENU) i = (gates_i32)k;
    }
    gates_widget_state_t *st = state_at(tree, menu_idx);
    bool open = false;
    if (i < 0 || st == nullptr || !gates_i_menu_sub_state(tree, menu_idx, id, &open)) return PROVEN_ERR_INVALID_ARG;
    for (gates_u32 r = 0; r < st->menu_count; r++) {
        if (st->menu_ids[r] != id) continue;
        if (!expand) {
            if (open) close_children(tree, (gates_u32)i);
            return GATES_OK;
        }
        if (!menu_selectable(tree, st, (gates_i32)r)) return PROVEN_ERR_INVALID_STATE;
        menu_select(tree, menu_idx, (gates_i32)r);
        return open_sub(tree, (gates_u32)i, (gates_i32)r, true);
    }
    return PROVEN_ERR_INVALID_ARG;
}

gates_rect_t gates_i_menu_row_rect(const gates_tree_t *tree, gates_u32 menu_idx, gates_u32 row) {
    gates_rect_t r = gates_i_slot(tree, menu_idx)->layout_rect;
    const gates_widget_state_t *st = state_at(tree, menu_idx);
    gates_i32 h = row_height(tree);
    if (st == nullptr || row >= st->menu_count) return (gates_rect_t){ r.x + MENU_PAD, r.y + MENU_PAD, r.w - 2 * MENU_PAD, 0 };
    return (gates_rect_t){ r.x + MENU_PAD, r.y + MENU_PAD + menu_row_top(st, row, h), r.w - 2 * MENU_PAD,
                           menu_row_h(st, row, h) };
}
