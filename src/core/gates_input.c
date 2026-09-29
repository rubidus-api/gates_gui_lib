/* gates_gui_lib — keyboard, character and IME routing to the focused textbox.
 * Every text change goes through gates_i_box_edit (gates_textbox.c), which
 * owns limits, undo and notifications (plan-0008). Enter/Tab/Escape are left
 * for RFC-0003 phase D (activation and focus traversal). Platform-free. */
#include <gates/ui.h>
#include <gates/widget.h>
#include "gates_tree_internal.h"

/* The focused, enabled textbox's state, or null. */
static gates_widget_state_t *focused_box(const gates_tree_t *tree) {
    if (tree == nullptr || tree->focus == GATES_NONE) {
        return nullptr;
    }
    const gates_node_slot_t *s = gates_i_slot(tree, tree->focus);
    if (s->kind != GATES_NODE_TEXTBOX) {
        return nullptr;
    }
    gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    if (st == nullptr || st->disabled || st->edit == nullptr) {
        return nullptr;
    }
    return st;
}

/* Encodes one codepoint as UTF-8; returns the byte count (0 when rejected). */
static gates_u32 encode_utf8(gates_u32 cp, gates_u8 out[4]) {
    if (cp < 0x80) {
        out[0] = (gates_u8)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (gates_u8)(0xC0 | (cp >> 6));
        out[1] = (gates_u8)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        if (cp >= 0xD800 && cp <= 0xDFFF) {
            return 0; /* lone surrogate: the platform layer must pair these */
        }
        out[0] = (gates_u8)(0xE0 | (cp >> 12));
        out[1] = (gates_u8)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (gates_u8)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        out[0] = (gates_u8)(0xF0 | (cp >> 18));
        out[1] = (gates_u8)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (gates_u8)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (gates_u8)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/* Runs one user edit on the focused box; failures go to input_error. */
static gates_err_t user_edit(gates_tree_t *tree, gates_u32 b, gates_u32 e, gates_str_t text,
                             gates_i_unit_t unit) {
    gates_err_t err = gates_i_box_edit(tree, tree->focus, b, e, text, unit, true);
    if (!gates_is_ok(err)) {
        tree->input_error = err;
    }
    return err;
}

static void copy_selection(gates_tree_t *tree, gates_widget_state_t *st) {
    gates_text_edit_t *ed = st->edit;
    if (!tree->has_clipboard || !gates_text_edit_has_selection(ed)) {
        return;
    }
    gates_u32 b = gates_text_edit_sel_begin(ed), e = gates_text_edit_sel_end(ed);
    gates_str_t t = gates_text_edit_text(ed);
    gates_err_t err = tree->clipboard.set_text(tree->clipboard.ctx,
                                               (gates_str_t){ .ptr = t.ptr + b, .size = e - b });
    if (!gates_is_ok(err)) {
        tree->input_error = err;
    }
}

static void cut_selection(gates_tree_t *tree, gates_widget_state_t *st) {
    gates_text_edit_t *ed = st->edit;
    if (!tree->has_clipboard || !gates_text_edit_has_selection(ed)) {
        return;
    }
    gates_err_t before = tree->input_error;
    tree->input_error = GATES_OK;
    copy_selection(tree, st);
    if (!gates_is_ok(tree->input_error)) {
        return; /* not on the clipboard: do not delete it */
    }
    tree->input_error = before;
    (void)user_edit(tree, gates_text_edit_sel_begin(ed), gates_text_edit_sel_end(ed),
                    (gates_str_t){0}, GATES_I_UNIT_OTHER);
}

static void paste(gates_tree_t *tree, gates_widget_state_t *st) {
    if (!tree->has_clipboard) {
        return;
    }
    gates_u8 *raw = nullptr;
    gates_usize_t raw_len = 0;
    gates_err_t err = tree->clipboard.get_text(tree->clipboard.ctx, tree->alloc, &raw, &raw_len);
    if (!gates_is_ok(err)) {
        tree->input_error = err;
        return;
    }
    gates_u8 *text = nullptr;
    gates_u32 len = 0;
    if (raw_len > 0) {
        err = gates_i_paste_normalize(tree->alloc, (gates_str_t){ .ptr = raw, .size = raw_len },
                                      &text, &len);
    }
    if (raw != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, raw);
    }
    if (!gates_is_ok(err)) {
        tree->input_error = err;
        return;
    }
    if (len > 0) {
        gates_text_edit_t *ed = st->edit;
        (void)user_edit(tree, gates_text_edit_sel_begin(ed), gates_text_edit_sel_end(ed),
                        (gates_str_t){ .ptr = text, .size = len }, GATES_I_UNIT_OTHER);
    }
    if (text != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, text);
    }
}

/* Editing keys of the focused textbox; false = not a key the textbox uses. */
static bool textbox_key(gates_tree_t *tree, gates_widget_state_t *st,
                        const gates_key_event_t *ev) {
    gates_text_edit_t *ed = st->edit;
    bool composing = gates_text_edit_preedit(ed).size > 0;

    bool consumed = true;
    switch (ev->key) {
    case GATES_KEY_LEFT:
        gates_text_edit_move(ed, GATES_CARET_LEFT, ev->shift);
        gates_i_box_seal(st);
        break;
    case GATES_KEY_RIGHT:
        gates_text_edit_move(ed, GATES_CARET_RIGHT, ev->shift);
        gates_i_box_seal(st);
        break;
    case GATES_KEY_HOME:
        gates_text_edit_move(ed, GATES_CARET_HOME, ev->shift);
        gates_i_box_seal(st);
        break;
    case GATES_KEY_END:
        gates_text_edit_move(ed, GATES_CARET_END, ev->shift);
        gates_i_box_seal(st);
        break;
    case GATES_KEY_BACKSPACE:
    case GATES_KEY_DELETE: {
        if (st->read_only) {
            consumed = false;
            break;
        }
        bool back = ev->key == GATES_KEY_BACKSPACE;
        gates_str_t t = gates_text_edit_text(ed);
        gates_u32 b, e;
        gates_i_unit_t unit;
        if (gates_text_edit_has_selection(ed)) {
            b = gates_text_edit_sel_begin(ed);
            e = gates_text_edit_sel_end(ed);
            unit = GATES_I_UNIT_OTHER;
        } else if (back) {
            e = gates_text_edit_caret(ed);
            b = e > 0 ? gates_text_prev_offset(t, e) : 0;
            unit = GATES_I_UNIT_DEL_BACK;
        } else {
            b = gates_text_edit_caret(ed);
            e = b < t.size ? gates_text_next_offset(t, b) : b;
            unit = GATES_I_UNIT_DEL_FWD;
        }
        if (b < e) {
            (void)user_edit(tree, b, e, (gates_str_t){0}, unit);
        }
        break;
    }
    case GATES_KEY_A:
        if (ev->ctrl) {
            gates_text_edit_select_all(ed);
            gates_i_box_seal(st);
        } else {
            consumed = false; /* plain 'a' arrives as a character */
        }
        break;
    case GATES_KEY_C:
        if (!ev->ctrl || st->password) {
            consumed = false;
        } else {
            copy_selection(tree, st);
        }
        break;
    case GATES_KEY_X:
        if (!ev->ctrl || st->password || st->read_only) {
            consumed = false;
        } else {
            cut_selection(tree, st);
        }
        break;
    case GATES_KEY_V:
        if (!ev->ctrl || st->read_only || composing) {
            consumed = false;
        } else {
            paste(tree, st);
        }
        break;
    case GATES_KEY_Z:
    case GATES_KEY_Y: {
        if (!ev->ctrl || st->read_only || composing) {
            consumed = false;
            break;
        }
        bool redo = ev->key == GATES_KEY_Y || ev->shift;
        gates_err_t err = redo ? gates_i_box_redo(tree, tree->focus)
                               : gates_i_box_undo(tree, tree->focus);
        if (!gates_is_ok(err)) {
            tree->input_error = err;
        }
        break;
    }
    default:
        consumed = false; /* Enter/Tab/Escape belong to RFC-0003 phase D */
        break;
    }
    if (consumed) {
        gates_i_mark_dirty(tree, tree->focus, GATES_DIRTY_PAINT);
    }
    return consumed;
}

/* Queues a command invocation; false when there is none or it is disabled
 * (the key then stays with the application). */
static bool run_command(gates_tree_t *tree, const gates_i_command_t *c) {
    if (c == nullptr || !c->enabled) {
        return false;
    }
    gates_err_t err = gates_i_event_reserve(tree, 1, 0);
    if (!gates_is_ok(err)) {
        tree->input_error = err; /* consumed, but nothing will run */
        return true;
    }
    gates_i_command_push(tree, c, GATES_ORIGIN_USER);
    return true;
}

static void cancel_key_press(gates_tree_t *tree) {
    if (tree->pressed == tree->key_press) {
        gates_i_mark_dirty(tree, tree->pressed, GATES_DIRTY_PAINT);
        tree->pressed = GATES_NONE;
    }
    tree->key_press = GATES_NONE;
}

static bool is_button(const gates_tree_t *tree, gates_u32 idx) {
    return idx != GATES_NONE && gates_i_slot(tree, idx)->kind == GATES_NODE_BUTTON;
}

/* Controls that Space presses and releases (activates on key-up). */
static bool is_toggle(const gates_tree_t *tree, gates_u32 idx) {
    gates_node_kind_t k = idx != GATES_NONE ? gates_i_slot(tree, idx)->kind : GATES_NODE_PANEL;
    return k == GATES_NODE_BUTTON || k == GATES_NODE_CHECKBOX || k == GATES_NODE_RADIO ||
           k == GATES_NODE_CHOICE;
}

static bool is_kind(const gates_tree_t *tree, gates_u32 idx, gates_node_kind_t kind) {
    return idx != GATES_NONE && gates_i_slot(tree, idx)->kind == kind;
}

static bool input_key(gates_tree_t *tree, const gates_key_event_t *ev);

bool gates_input_key(gates_tree_t *tree, const gates_key_event_t *ev) {
    if (tree == nullptr || ev == nullptr) {
        return false;
    }
    if (!ev->down) {
        return input_key(tree, ev);
    }
    /* A key hides a tooltip; one that moves the focus shows the new node's (plan-0018). */
    gates_u32 before = tree->focus;
    gates_i_tip_dismiss(tree);
    bool consumed = input_key(tree, ev);
    if (tree->focus != before) {
        gates_i_tip_focus(tree, tree->focus);
    }
    return consumed;
}

static bool input_key(gates_tree_t *tree, const gates_key_event_t *ev) {
    if (!ev->down) {
        /* Space activates on release, and only the control it pressed. */
        if (ev->key == GATES_KEY_SPACE && tree->key_press != GATES_NONE) {
            gates_u32 idx = tree->key_press;
            cancel_key_press(tree);
            if (idx == tree->focus && gates_i_focus_eligible(tree, idx)) {
                gates_i_activate(tree, idx);
            }
            return true;
        }
        return false;
    }
    tree->eat_char = false;
    /* An open menu takes every key (plan-0009 stage 2); the character a key
     * makes is not typed into the control below (plan-0018). */
    if (gates_i_overlay_key(tree, ev)) {
        tree->eat_char = true;
        return true;
    }
    /* Menu mode on the menu bar, and F10 as the menu key (plan-0018). */
    if (tree->mb_mode == GATES_I_MB_HIGHLIGHT) {
        if (gates_i_menubar_key(tree, ev)) {
            tree->eat_char = true;
            return true;
        }
    } else if (ev->key == GATES_KEY_F10 && !ev->ctrl && !ev->shift && !ev->alt &&
               gates_i_menubar_f10(tree)) {
        return true;
    }
    if (ev->ctrl && ev->alt) {
        return false; /* AltGr on some layouts: never a Ctrl shortcut */
    }
    /* The focused control first. */
    gates_widget_state_t *box = focused_box(tree);
    if (box != nullptr && textbox_key(tree, box, ev)) {
        return true;
    }
    gates_u32 f = tree->focus;
    bool focus_ok = f != GATES_NONE && gates_i_focus_eligible(tree, f);
    if (focus_ok && is_kind(tree, f, GATES_NODE_RADIO) && gates_i_radio_key(tree, f, ev)) {
        return true;
    }
    if (focus_ok && is_kind(tree, f, GATES_NODE_TOOLBAR) && gates_i_toolbar_key(tree, f, ev)) {
        return true; /* buttons, Enter/Space invoke (plan-0018) */
    }
    if (focus_ok && is_kind(tree, f, GATES_NODE_VIEW) && gates_i_view_key(tree, f, ev)) {
        return true; /* rows, paging, Enter activates the row */
    }
    if (focus_ok && is_kind(tree, f, GATES_NODE_CHOICE) && ev->alt && !ev->ctrl &&
        ev->key == GATES_KEY_DOWN) {
        gates_i_choice_open(tree, f); /* Alt+Down */
        return true;
    }
    switch (ev->key) {
    case GATES_KEY_TAB:
        if (ev->ctrl) {
            return false;
        }
        return gates_tree_focus_next(tree, ev->shift);
    case GATES_KEY_SPACE:
        if (!ev->ctrl && focus_ok && is_toggle(tree, f)) {
            if (tree->key_press == GATES_NONE) { /* auto-repeat keeps the press */
                tree->key_press = f;
                tree->pressed = f;
                gates_i_mark_dirty(tree, f, GATES_DIRTY_PAINT);
            }
            return true;
        }
        break;
    case GATES_KEY_ENTER:
        if (ev->ctrl) {
            break;
        }
        if (focus_ok && (is_button(tree, f) || is_kind(tree, f, GATES_NODE_CHOICE))) {
            gates_i_activate(tree, f); /* a choice opens its list */
            return true;
        }
        return run_command(tree, gates_i_command_with_role(tree, GATES_COMMAND_DEFAULT));
    case GATES_KEY_ESCAPE:
        if (tree->key_press != GATES_NONE) {
            cancel_key_press(tree);
            return true;
        }
        if (run_command(tree, gates_i_command_with_role(tree, GATES_COMMAND_CANCEL))) {
            return true;
        }
        return gates_i_overlay_escape(tree); /* a dialog without a cancel command */
    default:
        break;
    }
    return run_command(tree, gates_i_command_for_key(tree, ev));
}

gates_input_result_t gates_input_char(gates_tree_t *tree, gates_u32 codepoint) {
    if (tree != nullptr && tree->eat_char) {
        tree->eat_char = false;
        return GATES_INPUT_CONSUMED; /* made by a key a menu took (plan-0018) */
    }
    if (codepoint < 0x20 || codepoint == 0x7F) {
        return GATES_INPUT_IGNORED; /* control characters are not text */
    }
    gates_widget_state_t *st = focused_box(tree);
    if (st == nullptr || st->read_only) {
        return GATES_INPUT_IGNORED;
    }
    gates_u8 buf[4];
    gates_u32 n = encode_utf8(codepoint, buf);
    if (n == 0) {
        return GATES_INPUT_IGNORED;
    }
    gates_text_edit_t *ed = st->edit;
    gates_err_t err = user_edit(tree, gates_text_edit_sel_begin(ed), gates_text_edit_sel_end(ed),
                                (gates_str_t){ .ptr = buf, .size = n }, GATES_I_UNIT_TYPE);
    return gates_is_ok(err) ? GATES_INPUT_CONSUMED : GATES_INPUT_FAILED;
}

/* -- IME composition (plan-0006) ------------------------------------------- */

/* Composition is refused for read-only and password boxes (the Win32 window
 * also detaches the IME from them). */
static gates_widget_state_t *composing_box(const gates_tree_t *tree) {
    gates_widget_state_t *st = focused_box(tree);
    return (st != nullptr && !st->read_only && !st->password) ? st : nullptr;
}

gates_input_result_t gates_input_preedit(gates_tree_t *tree, gates_str_t text,
                                         gates_u32 cursor) {
    gates_widget_state_t *st = composing_box(tree);
    if (st == nullptr || (text.size > 0 && text.ptr == nullptr)) {
        return GATES_INPUT_IGNORED;
    }
    gates_err_t err = st->on_event != nullptr
                          ? gates_i_event_reserve(tree, 1, (gates_u32)text.size)
                          : GATES_OK;
    if (!gates_is_ok(err)) {
        tree->input_error = err;
        return GATES_INPUT_FAILED;
    }
    if (text.size == 0) {
        gates_text_edit_clear_preedit(st->edit);
        st->ime_cursor = 0;
    } else {
        err = gates_text_edit_set_preedit(st->edit, text);
        if (!gates_is_ok(err)) {
            tree->input_error = err;
            return GATES_INPUT_FAILED; /* previous preedit kept */
        }
        if (cursor > text.size) {
            cursor = (gates_u32)text.size;
        }
        while (cursor > 0 && cursor < text.size && (text.ptr[cursor] & 0xC0) == 0x80) {
            cursor--; /* snap to the start of the codepoint */
        }
        st->ime_cursor = cursor;
    }
    gates_i_event_push(tree, tree->focus, GATES_EVENT_PREEDIT_CHANGED, GATES_ORIGIN_USER);
    gates_i_mark_dirty(tree, tree->focus, GATES_DIRTY_PAINT);
    return GATES_INPUT_CONSUMED;
}

static void end_composition(gates_tree_t *tree, gates_widget_state_t *st, bool had_preedit) {
    gates_text_edit_clear_preedit(st->edit);
    st->ime_cursor = 0;
    if (had_preedit) {
        gates_i_event_push(tree, tree->focus, GATES_EVENT_PREEDIT_CHANGED, GATES_ORIGIN_USER);
    }
    gates_i_mark_dirty(tree, tree->focus, GATES_DIRTY_PAINT);
}

gates_input_result_t gates_input_commit(gates_tree_t *tree, gates_str_t text) {
    gates_widget_state_t *st = composing_box(tree);
    if (st == nullptr || (text.size > 0 && text.ptr == nullptr)) {
        return GATES_INPUT_IGNORED;
    }
    bool had_preedit = gates_text_edit_preedit(st->edit).size > 0;
    gates_text_edit_t *ed = st->edit;
    /* Room for the composition-ended event as well as the text change. */
    if (st->on_event != nullptr) {
        gates_u32 sel = gates_text_edit_sel_end(ed) - gates_text_edit_sel_begin(ed);
        gates_err_t err = gates_i_event_reserve(
            tree, 2u, (gates_u32)gates_text_edit_text(ed).size - sel + (gates_u32)text.size);
        if (!gates_is_ok(err)) {
            tree->input_error = err;
            return GATES_INPUT_FAILED;
        }
    }
    /* An empty result only ends the composition: text and selection stay. */
    if (text.size > 0) {
        gates_err_t err = user_edit(tree, gates_text_edit_sel_begin(ed),
                                    gates_text_edit_sel_end(ed), text, GATES_I_UNIT_OTHER);
        if (err == PROVEN_ERR_OUT_OF_BOUNDS) {
            end_composition(tree, st, had_preedit); /* the IME is done with it */
            return GATES_INPUT_FAILED;
        }
        if (!gates_is_ok(err)) {
            return GATES_INPUT_FAILED; /* text, selection and preedit unchanged */
        }
    }
    end_composition(tree, st, had_preedit);
    return GATES_INPUT_CONSUMED;
}

gates_input_result_t gates_input_preedit_cancel(gates_tree_t *tree) {
    gates_widget_state_t *st = focused_box(tree);
    if (st == nullptr || gates_text_edit_preedit(st->edit).size == 0) {
        return GATES_INPUT_IGNORED;
    }
    gates_text_edit_clear_preedit(st->edit);
    st->ime_cursor = 0;
    /* Cancel always happens; only its announcement can fail. */
    gates_i_event_try_push(tree, tree->focus, GATES_EVENT_PREEDIT_CHANGED, 0);
    gates_i_mark_dirty(tree, tree->focus, GATES_DIRTY_PAINT);
    return GATES_INPUT_CONSUMED;
}

gates_err_t gates_input_take_error(gates_tree_t *tree) {
    if (tree == nullptr) {
        return GATES_OK;
    }
    gates_err_t err = tree->input_error;
    tree->input_error = GATES_OK;
    return err;
}

bool gates_input_composing(const gates_tree_t *tree) {
    const gates_widget_state_t *st = focused_box(tree);
    return st != nullptr && gates_text_edit_preedit(st->edit).size > 0;
}

bool gates_input_caret_rect(const gates_tree_t *tree, gates_rect_t *out) {
    const gates_widget_state_t *st = focused_box(tree);
    if (st == nullptr || !st->caret_valid || out == nullptr) {
        return false;
    }
    *out = st->caret_rect;
    return true;
}
