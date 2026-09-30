/* gates_gui_lib - primitive widget state and properties. */
#include <gates/widget.h>
#include "gates_tree_internal.h"

#include <string.h>

static gates_err_t widget_create(gates_tree_t *tree, gates_node_t parent,
                                 gates_node_kind_t kind, gates_str_t text,
                                 gates_node_t *out_node) {
    if (tree == nullptr || out_node == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_node_desc_t desc = { .kind = kind };
    gates_node_t node = GATES_NODE_NULL;
    gates_err_t err = gates_node_create(tree, parent, &desc, &node);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_u32 state_index = GATES_NONE;
    err = gates_i_state_acquire(tree, &state_index);
    if (!gates_is_ok(err)) {
        gates_i_node_undo(tree, node);
        return err;
    }
    gates_i_slot(tree, node.index)->state_index = state_index;
    if (text.size > 0) {
        err = gates_widget_set_text(tree, node, text);
        if (!gates_is_ok(err)) {
            gates_i_node_undo(tree, node);
            return err;
        }
    }
    *out_node = node;
    return GATES_OK;
}

gates_err_t gates_panel_create(gates_tree_t *tree, gates_node_t parent,
                               gates_node_t *out_node) {
    /* Panels carry no widget state; they are containers with a background. */
    if (tree == nullptr || out_node == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_node_desc_t desc = { .kind = GATES_NODE_PANEL };
    return gates_node_create(tree, parent, &desc, out_node);
}

gates_err_t gates_label_create(gates_tree_t *tree, gates_node_t parent,
                               gates_str_t text, gates_node_t *out_node) {
    return widget_create(tree, parent, GATES_NODE_LABEL, text, out_node);
}

gates_err_t gates_button_create(gates_tree_t *tree, gates_node_t parent,
                                gates_str_t text, gates_click_fn on_click,
                                void *user, gates_node_t *out_node) {
    gates_err_t err = widget_create(tree, parent, GATES_NODE_BUTTON, text, out_node);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, out_node->index)->state_index);
    st->on_click = on_click;
    st->cb_user = user;
    return GATES_OK;
}

gates_err_t gates_checkbox_create(gates_tree_t *tree, gates_node_t parent,
                                  gates_str_t text, bool checked,
                                  gates_toggle_fn on_toggle, void *user,
                                  gates_node_t *out_node) {
    gates_err_t err = widget_create(tree, parent, GATES_NODE_CHECKBOX, text, out_node);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, out_node->index)->state_index);
    st->checked = checked;
    st->on_toggle = on_toggle;
    st->cb_user = user;
    return GATES_OK;
}

gates_err_t gates_textbox_create(gates_tree_t *tree, gates_node_t parent, gates_str_t text,
                                 gates_u32 cols, gates_node_t *out_node) {
    gates_err_t err = widget_create(tree, parent, GATES_NODE_TEXTBOX, (gates_str_t){0},
                                    out_node);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, out_node->index)->state_index);
    st->cols = cols != 0 ? cols : 16u;

    proven_result_mem_mut_t res = tree->alloc.alloc_fn(tree->alloc.ctx,
                                                       sizeof(gates_text_edit_t),
                                                       alignof(gates_text_edit_t));
    if (!proven_is_ok(res.err)) {
        gates_i_node_undo(tree, *out_node);
        *out_node = GATES_NODE_NULL;
        return res.err;
    }
    st->edit = (gates_text_edit_t *)res.value.ptr;
    err = gates_text_edit_init(st->edit, tree->alloc, text);
    if (!gates_is_ok(err)) {
        tree->alloc.free_fn(tree->alloc.ctx, st->edit);
        st->edit = nullptr;
        gates_i_node_undo(tree, *out_node);
        *out_node = GATES_NODE_NULL;
        return err;
    }
    return GATES_OK;
}

gates_text_edit_t *gates_textbox_edit(gates_tree_t *tree, gates_node_t node) {
    if (!gates_i_valid(tree, node) ||
        gates_i_slot(tree, node.index)->kind != GATES_NODE_TEXTBOX) {
        return nullptr;
    }
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, node.index)->state_index);
    return st != nullptr ? st->edit : nullptr;
}

gates_str_t gates_textbox_text(const gates_tree_t *tree, gates_node_t node) {
    if (!gates_i_valid(tree, node) ||
        gates_i_slot(tree, node.index)->kind != GATES_NODE_TEXTBOX) {
        return (gates_str_t){0};
    }
    const gates_widget_state_t *st =
        gates_i_state(tree, gates_i_slot(tree, node.index)->state_index);
    return (st != nullptr && st->edit != nullptr) ? gates_text_edit_text(st->edit)
                                                  : (gates_str_t){0};
}

static gates_widget_state_t *state_of(const gates_tree_t *tree, gates_node_t node) {
    if (!gates_i_valid(tree, node)) {
        return nullptr;
    }
    return gates_i_state(tree, gates_i_slot(tree, node.index)->state_index);
}

gates_err_t gates_textbox_set_text(gates_tree_t *tree, gates_node_t node, gates_str_t text) {
    gates_widget_state_t *st = state_of(tree, node);
    if (st == nullptr || st->edit == nullptr ||
        gates_i_slot(tree, node.index)->kind != GATES_NODE_TEXTBOX ||
        (text.size > 0 && text.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (gates_text_edit_preedit(st->edit).size > 0) {
        return PROVEN_ERR_BUSY; /* never invent a result from a preedit */
    }
    gates_str_t cur = gates_text_edit_text(st->edit);
    if (cur.size == text.size && (text.size == 0 || memcmp(cur.ptr, text.ptr, text.size) == 0)) {
        return GATES_OK; /* identical value: no-op */
    }
    if (st->max_bytes != 0 && text.size > st->max_bytes) {
        return PROVEN_ERR_OUT_OF_BOUNDS;
    }
    /* Silent, but a queued user event for this box will copy the new text. */
    gates_err_t err = gates_i_wants_events(tree, node.index)
                          ? gates_i_event_reserve(tree, 0, (gates_u32)text.size)
                          : GATES_OK;
    if (!gates_is_ok(err)) {
        return err;
    }
    err = gates_text_edit_set_text(st->edit, text);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_i_box_forget(tree, st); /* a non-undoable set clears history and offers */
    st->revision++;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_widget_set_text(gates_tree_t *tree, gates_node_t node, gates_str_t text) {
    gates_widget_state_t *st = state_of(tree, node);
    if (st == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (text.size > 0 && text.ptr == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (st->edit != nullptr) {
        return gates_textbox_set_text(tree, node, text); /* the box's real text */
    }
    if (text.size == st->text_len && (text.size == 0 || memcmp(text.ptr, st->text, text.size) == 0)) {
        return GATES_OK; /* identical: nothing to lay out, repaint or announce */
    }
    gates_u8 *copy = nullptr;
    if (text.size > 0) {
        proven_result_mem_mut_t res = tree->alloc.alloc_fn(tree->alloc.ctx, text.size,
                                                           alignof(gates_u8));
        if (!proven_is_ok(res.err)) {
            return res.err; /* old text intact (failure-atomic) */
        }
        copy = (gates_u8 *)res.value.ptr;
        memcpy(copy, text.ptr, text.size);
    }
    if (st->text != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, st->text);
    }
    st->text = copy;
    st->text_len = (gates_u32)text.size;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    if (gates_i_access_live(tree, node.index) != GATES_LIVE_OFF) {
        gates_i_access_log(tree, GATES_ACCESS_LIVE, node.index, 0); /* announce the new text */
    }
    return GATES_OK;
}

gates_str_t gates_widget_text(const gates_tree_t *tree, gates_node_t node) {
    const gates_widget_state_t *st = state_of(tree, node);
    if (st != nullptr && st->edit != nullptr) {
        return gates_text_edit_text(st->edit); /* a textbox's real text */
    }
    if (st == nullptr || st->text == nullptr) {
        return (gates_str_t){0};
    }
    return (gates_str_t){ .ptr = st->text, .size = st->text_len };
}

gates_err_t gates_widget_set_disabled(gates_tree_t *tree, gates_node_t node, bool disabled) {
    gates_widget_state_t *st = state_of(tree, node);
    if (st == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (gates_i_slot(tree, node.index)->kind == GATES_NODE_SPIN) {
        gates_node_t box = gates_i_handle(tree, gates_i_slot(tree, node.index)->first_child);
        gates_widget_state_t *bs = state_of(tree, box);
        if (bs != nullptr && bs->disabled != disabled) {
            bs->disabled = disabled; /* the box is the spin box's input (0.4.0) */
            gates_i_mark_dirty(tree, box.index, GATES_DIRTY_PAINT);
        }
    }
    if (disabled && gates_i_slot(tree, node.index)->kind == GATES_NODE_VIEW) {
        gates_i_view_cancel_edit(tree, node.index);
    }
    if (st->disabled != disabled) {
        st->disabled = disabled;
        gates_i_mark_dirty(tree, node.index, GATES_DIRTY_PAINT);
        gates_i_focus_check(tree); /* a disabled control cannot keep focus */
    }
    return GATES_OK;
}

bool gates_widget_disabled(const gates_tree_t *tree, gates_node_t node) {
    const gates_widget_state_t *st = state_of(tree, node);
    return st != nullptr && st->disabled;
}

gates_err_t gates_checkbox_set_checked(gates_tree_t *tree, gates_node_t node, bool checked) {
    if (!gates_i_valid(tree, node) ||
        gates_i_slot(tree, node.index)->kind != GATES_NODE_CHECKBOX) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_widget_state_t *st = state_of(tree, node);
    if (st->checked != checked) {
        st->checked = checked;
        st->revision++;
        gates_i_mark_dirty(tree, node.index, GATES_DIRTY_PAINT);
    }
    return GATES_OK;
}

bool gates_checkbox_checked(const gates_tree_t *tree, gates_node_t node) {
    const gates_widget_state_t *st = state_of(tree, node);
    return st != nullptr && st->checked;
}

bool gates_widget_hovered(const gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node) && tree->hover == node.index;
}

bool gates_widget_pressed(const gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node) && tree->pressed == node.index;
}

/* -- textbox error state, separator, progress ------------------------ */

gates_err_t gates_textbox_set_invalid(gates_tree_t *tree, gates_node_t node, bool invalid) {
    if (!gates_i_valid(tree, node) ||
        gates_i_slot(tree, node.index)->kind != GATES_NODE_TEXTBOX) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_widget_state_t *st = state_of(tree, node);
    if (st->invalid != invalid) {
        st->invalid = invalid;
        gates_i_mark_dirty(tree, node.index, GATES_DIRTY_PAINT);
    }
    return GATES_OK;
}

bool gates_textbox_invalid(const gates_tree_t *tree, gates_node_t node) {
    const gates_widget_state_t *st = state_of(tree, node);
    return st != nullptr && gates_i_slot(tree, node.index)->kind == GATES_NODE_TEXTBOX &&
           st->invalid;
}

gates_err_t gates_separator_create(gates_tree_t *tree, gates_node_t parent,
                                   gates_node_t *out_node) {
    if (tree == nullptr || out_node == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_node_desc_t desc = { .kind = GATES_NODE_SEPARATOR };
    return gates_node_create(tree, parent, &desc, out_node); /* no state: nothing to say */
}

static gates_i32 clamp_permille(gates_i32 v) {
    return v < 0 ? 0 : v > 1000 ? 1000 : v;
}

gates_err_t gates_progress_create(gates_tree_t *tree, gates_node_t parent, gates_i32 permille,
                                  gates_node_t *out_node) {
    gates_err_t err = widget_create(tree, parent, GATES_NODE_PROGRESS, (gates_str_t){0},
                                    out_node);
    if (!gates_is_ok(err)) {
        return err;
    }
    state_of(tree, *out_node)->value = clamp_permille(permille);
    return GATES_OK;
}

gates_err_t gates_progress_set_value(gates_tree_t *tree, gates_node_t node, gates_i32 permille) {
    if (!gates_i_valid(tree, node) ||
        gates_i_slot(tree, node.index)->kind != GATES_NODE_PROGRESS) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_widget_state_t *st = state_of(tree, node);
    gates_i32 v = clamp_permille(permille);
    if (st->value != v) {
        st->value = v;
        gates_i_mark_dirty(tree, node.index, GATES_DIRTY_PAINT);
    }
    return GATES_OK;
}

gates_i32 gates_progress_value(const gates_tree_t *tree, gates_node_t node) {
    const gates_widget_state_t *st = state_of(tree, node);
    return st != nullptr && gates_i_slot(tree, node.index)->kind == GATES_NODE_PROGRESS
               ? st->value
               : 0;
}
