/* gates_gui_lib - hit testing and pointer routing.
 * Hover and pressed live on the tree (slot indices); widgets are activated
 * on release-inside (button on_click, checkbox toggle + on_toggle). */
#include <gates/ui.h>
#include <gates/widget.h>
#include "gates_tree_internal.h"

static bool interactive(const gates_tree_t *tree, const gates_node_slot_t *s) {
    if (s->kind != GATES_NODE_BUTTON && s->kind != GATES_NODE_CHECKBOX &&
        s->kind != GATES_NODE_TEXTBOX && s->kind != GATES_NODE_RADIO &&
        s->kind != GATES_NODE_CHOICE && s->kind != GATES_NODE_VIEW && s->kind != GATES_NODE_SLIDER &&
        s->kind != GATES_NODE_EDITOR &&
        !(s->kind == GATES_NODE_GROUPHEAD && s->parent != GATES_NONE && gates_i_group_foldable(tree, s->parent))) {
        return false;
    }
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    return st != nullptr && !gates_i_widget_inert(tree, st);
}

/* Deepest visible node containing p; stack descends into the active page only. */
static gates_u32 hit_idx(const gates_tree_t *tree, gates_u32 idx, gates_point_t p) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->hidden) {
        return GATES_NONE;
    }
    if (!gates_rect_contains(s->layout_rect, p) && idx != tree->root) {
        return GATES_NONE;
    }
    if (s->layout_kind == GATES_LAYOUT_STACK) {
        gates_u32 i = 0;
        for (gates_u32 c = s->first_child; c != GATES_NONE;
             c = gates_i_slot(tree, c)->next_sibling, i++) {
            if (i == s->active_child) {
                gates_u32 deep = hit_idx(tree, c, p);
                return deep != GATES_NONE ? deep : idx;
            }
        }
        return idx;
    }
    /* Later siblings paint on top; walk from the last child backwards. */
    for (gates_u32 c = s->last_child; c != GATES_NONE;
         c = gates_i_slot(tree, c)->prev_sibling) {
        gates_u32 deep = hit_idx(tree, c, p);
        if (deep != GATES_NONE) {
            return deep;
        }
    }
    return gates_rect_contains(s->layout_rect, p) ? idx : GATES_NONE;
}

gates_node_t gates_hit_test(const gates_tree_t *tree, gates_point_t p) {
    if (tree == nullptr) {
        return GATES_NODE_NULL;
    }
    /* The topmost overlay under the point wins; a modal dialog hides the rest. */
    for (gates_u32 i = tree->overlay_count; i-- > 0;) {
        gates_u32 o = tree->overlays[i].index;
        if (gates_rect_contains(gates_i_slot(tree, o)->layout_rect, p)) {
            gates_u32 idx = hit_idx(tree, o, p);
            return idx == GATES_NONE ? GATES_NODE_NULL : gates_i_handle(tree, idx);
        }
        if (tree->overlays[i].kind == GATES_NODE_DIALOG) {
            return GATES_NODE_NULL;
        }
    }
    gates_u32 idx = hit_idx(tree, tree->root, p);
    return idx == GATES_NONE ? GATES_NODE_NULL : gates_i_handle(tree, idx);
}

static void set_hover(gates_tree_t *tree, gates_u32 idx) {
    if (tree->hover == idx) {
        return;
    }
    tree->hover = idx;
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
}

void gates_i_activate(gates_tree_t *tree, gates_u32 idx) {
    gates_node_slot_t *s = gates_i_slot(tree, idx);
    gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    gates_node_t node = gates_i_handle(tree, idx);
    if (st == nullptr || gates_i_widget_inert(tree, st)) {
        return;
    }
    if (s->kind == GATES_NODE_RADIO) {
        gates_i_radio_activate(tree, idx);
        return;
    }
    if (s->kind == GATES_NODE_CHOICE) {
        gates_i_choice_open(tree, idx);
        return;
    }
    if (s->kind == GATES_NODE_GROUPHEAD) {
        gates_i_group_head_activate(tree, idx);
        return;
    }
    if (s->kind != GATES_NODE_BUTTON && s->kind != GATES_NODE_CHECKBOX) {
        return;
    }
    const gates_i_command_t *cmd =
        st->cmd_id != 0 ? gates_i_command_find(tree, st->cmd_scope_index,
                                               st->cmd_scope_generation, st->cmd_id)
                        : nullptr;
    /* Reserve the notifications first: an activation or toggle that could not
     * be announced does not happen at all. */
    gates_u32 slots = (gates_i_wants_events(tree, idx) ? 1u : 0u) + (cmd != nullptr ? 1u : 0u);
    if (slots > 0) {
        gates_err_t err = gates_i_event_reserve(tree, slots, 0);
        if (!gates_is_ok(err)) {
            tree->input_error = err;
            return;
        }
    }
    if (s->kind == GATES_NODE_BUTTON) {
        gates_i_event_push(tree, idx, GATES_EVENT_ACTIVATED, GATES_ORIGIN_USER);
        if (cmd != nullptr) {
            gates_i_command_push(tree, cmd, GATES_ORIGIN_USER); /* re-checked at delivery */
        }
        if (st->on_click != nullptr) {
            st->on_click(tree, node, st->cb_user); /* legacy: synchronous */
        }
    } else {
        st->checked = !st->checked;
        st->revision++;
        gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
        gates_i_event_push(tree, idx, GATES_EVENT_VALUE_CHANGED, GATES_ORIGIN_USER);
        if (st->on_toggle != nullptr) {
            st->on_toggle(tree, node, st->checked, st->cb_user); /* legacy: synchronous */
        }
    }
}

void gates_input_cancel_pointer(gates_tree_t *tree) {
    if (tree == nullptr) {
        return;
    }
    if (tree->drag_kind == GATES_DRAG_SPIN) gates_i_spin_arrows_release(tree); /* no tick after it */
    if (tree->pressed != GATES_NONE) {
        gates_i_mark_dirty(tree, tree->pressed, GATES_DIRTY_PAINT);
        tree->pressed = GATES_NONE;
    }
    tree->drag_kind = GATES_DRAG_NONE;
    tree->drag_node = GATES_NONE;
}

/* -- split handle / scroll thumb / text selection dragging ----------------- */

/* Maps a pointer x to a caret offset: the nearest code-point boundary. */
static void textbox_caret_at(gates_tree_t *tree, gates_u32 idx, gates_point_t p, bool extend) {
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    if (st == nullptr || st->edit == nullptr) {
        return;
    }
    gates_rect_t inner = gates_i_textbox_inner(tree, idx);
    /* The nearest boundary by the font's advances (0.2.0). */
    gates_u32 off = gates_i_box_offset_at_x(tree->text_backend, gates_i_font(tree, idx), st,
                                            p.x - inner.x + st->view_x);
    gates_text_edit_set_caret(st->edit, off, extend);
    gates_i_box_seal(st); /* a caret placed by the pointer ends a typing run */
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
}


/* A word class for double-click selection in a text box: letters, digits, '_'
 * and anything past ASCII form words; blanks and punctuation form their own runs. */
static int word_class(gates_u8 c) {
    if (c == ' ' || c == '\t') return 0;
    if (c >= 0x80 || c == '_' || (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z')) return 1;
    return 2;
}

/* A double click selects the run under the caret; a triple click all the text (0.8.0). */
static void textbox_select_unit(gates_tree_t *tree, gates_u32 idx, gates_u32 clicks) {
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    if (st == nullptr || st->edit == nullptr) return;
    gates_str_t t = gates_text_edit_text(st->edit);
    if (clicks >= 3 || st->password) {
        gates_text_edit_select_all(st->edit);
    } else {
        gates_u32 at = gates_text_edit_caret(st->edit), b = at, e = at;
        if (at < t.size || at > 0) {
            gates_u32 probe = at < t.size ? at : at - 1;
            int c = word_class(t.ptr[probe]);
            b = probe;
            while (b > 0 && word_class(t.ptr[b - 1]) == c) b--;
            e = at;
            while (e < t.size && word_class(t.ptr[e]) == c) e++;
            while (b > 0 && (t.ptr[b] & 0xC0) == 0x80) b--; /* whole code points */
        }
        gates_text_edit_set_caret(st->edit, b, false);
        gates_text_edit_set_caret(st->edit, e, true);
    }
    gates_i_box_seal(st);
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
}

/* Starts a drag when the point lands on a split handle or a scroll thumb.
 * Those areas belong to the container itself (children never cover them), so
 * the ordinary hit result is enough to detect them. */
static bool drag_begin(gates_tree_t *tree, gates_u32 idx, const gates_pointer_event_t *ev) {
    gates_point_t p = ev->pos;
    gates_u32 clicks = ev->clicks != 0 ? ev->clicks : 1;
    gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->kind == GATES_NODE_SLIDER) {
        return gates_i_slider_press(tree, idx, p);
    }
    if (s->kind == GATES_NODE_SPINARROWS) {
        return gates_i_spin_arrows_press(tree, idx, p); /* repeats while held (0.8.0) */
    }
    if (s->kind == GATES_NODE_VIEW) {
        return gates_i_view_pointer_down(tree, idx, p, clicks); /* rows, header, bars */
    }
    if (s->kind == GATES_NODE_EDITOR) {
        return gates_i_editor_press(tree, idx, p, clicks, ev->shift);
    }
    if (s->layout_kind == GATES_LAYOUT_SPLIT &&
        gates_rect_contains(gates_i_split_handle(tree, idx), p)) {
        gates_rect_t a = gates_i_slot(tree, s->first_child)->layout_rect;
        tree->drag_kind = GATES_DRAG_SPLIT;
        tree->drag_node = idx;
        tree->drag_start = p;
        tree->drag_start_value = s->split_vertical ? a.h : a.w;
        return true;
    }
    if (s->layout_kind == GATES_LAYOUT_SCROLL &&
        gates_rect_contains(gates_i_scroll_thumb(tree, idx), p)) {
        tree->drag_kind = GATES_DRAG_SCROLL_THUMB;
        tree->drag_node = idx;
        tree->drag_start = p;
        tree->drag_start_value = s->scroll_offset;
        return true;
    }
    if (s->layout_kind == GATES_LAYOUT_SCROLL && gates_rect_contains(gates_i_scroll_track(tree, idx), p)) {
        /* A press on the track, off the thumb, pages toward the press (0.8.0). */
        gates_rect_t thumb = gates_i_scroll_thumb(tree, idx);
        gates_i32 page = s->layout_rect.h - 2 * s->padding;
        if (page < 1) page = 1;
        gates_i32 off = gates_i_scroll_clamp(tree, idx, s->scroll_offset + (p.y < thumb.y ? -page : page));
        if (off != s->scroll_offset) {
            s->scroll_offset = off;
            gates_i_mark_dirty(tree, idx, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
        }
        return true;
    }
    if (s->kind == GATES_NODE_TEXTBOX) {
        gates_widget_state_t *st = gates_i_state(tree, s->state_index);
        if (st != nullptr && !st->disabled) {
            gates_tree_set_focus(tree, gates_i_handle(tree, idx));
            textbox_caret_at(tree, idx, p, ev->shift); /* Shift extends (0.8.0) */
            if (clicks >= 2 && !ev->shift) {
                textbox_select_unit(tree, idx, clicks);
                return true;
            }
            tree->drag_kind = GATES_DRAG_TEXT_SELECT;
            tree->drag_node = idx;
            tree->drag_start = p;
            tree->drag_start_value = 0;
            return true;
        }
    }
    return false;
}

static void drag_update(gates_tree_t *tree, gates_point_t p) {
    gates_u32 idx = tree->drag_node;
    gates_node_slot_t *s = gates_i_slot(tree, idx);

    if (tree->drag_kind == GATES_DRAG_VIEW_VTHUMB || tree->drag_kind == GATES_DRAG_VIEW_HTHUMB ||
        tree->drag_kind == GATES_DRAG_VIEW_COLUMN) {
        gates_i_view_drag(tree, p);
        return;
    }
    if (tree->drag_kind == GATES_DRAG_SPLIT) {
        bool vert = s->split_vertical != 0;
        gates_i32 span = (vert ? s->layout_rect.h : s->layout_rect.w) - 2 * s->padding;
        gates_i32 total = span - GATES_SPLIT_HANDLE_PX;
        if (total < 2 * GATES_SPLIT_MIN_PANE_PX) {
            return; /* nothing meaningful to drag */
        }
        gates_i32 delta = vert ? (p.y - tree->drag_start.y) : (p.x - tree->drag_start.x);
        gates_i32 a = tree->drag_start_value + delta;
        if (a < GATES_SPLIT_MIN_PANE_PX) a = GATES_SPLIT_MIN_PANE_PX;
        if (a > total - GATES_SPLIT_MIN_PANE_PX) a = total - GATES_SPLIT_MIN_PANE_PX;
        /* Round the per-mille UP: arrange rounds down when converting back, so
         * ceiling here makes the pane land exactly where the pointer is. */
        gates_i32 ratio = (gates_i32)(((gates_i64)a * 1000 + (total - 1)) / total);
        if (ratio < 1) ratio = 1;
        if (ratio > 999) ratio = 999;
        if (ratio != s->split_ratio) {
            s->split_ratio = ratio;
            gates_i_mark_dirty(tree, idx, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
        }
        return;
    }

    if (tree->drag_kind == GATES_DRAG_SLIDER) {
        gates_i_slider_drag(tree, p, false);
        return;
    }
    if (tree->drag_kind == GATES_DRAG_SPIN) {
        tree->drag_start = p; /* where the pointer is: a tick steps only over the pressed arrow */
        return;
    }
    if (tree->drag_kind == GATES_DRAG_EDITOR_SELECT || tree->drag_kind == GATES_DRAG_EDITOR_VTHUMB ||
        tree->drag_kind == GATES_DRAG_EDITOR_HTHUMB) {
        gates_i_editor_drag(tree, p);
        return;
    }

    if (tree->drag_kind == GATES_DRAG_TEXT_SELECT) {
        textbox_caret_at(tree, idx, p, true); /* extend the selection */
        return;
    }

    if (tree->drag_kind == GATES_DRAG_SCROLL_THUMB) {
        gates_rect_t track = gates_i_scroll_track(tree, idx);
        gates_rect_t thumb = gates_i_scroll_thumb(tree, idx);
        gates_i32 travel = track.h - thumb.h;
        if (travel <= 0) {
            return;
        }
        gates_i32 max_off = gates_i_scroll_clamp(tree, idx, INT32_MAX);
        gates_i32 delta = p.y - tree->drag_start.y;
        gates_i32 off = tree->drag_start_value +
                        (gates_i32)(((gates_i64)delta * max_off) / travel);
        off = gates_i_scroll_clamp(tree, idx, off);
        if (off != s->scroll_offset) {
            s->scroll_offset = off;
            gates_i_mark_dirty(tree, idx, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
        }
    }
}

/* Nearest scrollable ancestor (or the node itself) for wheel events. */
static gates_u32 scroll_target(const gates_tree_t *tree, gates_u32 idx) {
    for (gates_u32 cur = idx; cur != GATES_NONE; cur = gates_i_slot(tree, cur)->parent) {
        if (gates_i_scrollable(tree, cur)) {
            return cur;
        }
    }
    return GATES_NONE;
}

static void wheel_scroll(gates_tree_t *tree, gates_u32 from, gates_vec2_t wheel) {
    gates_u32 idx = scroll_target(tree, from);
    if (idx == GATES_NONE || wheel.y == 0.0f) {
        return;
    }
    gates_i32 line = tree->line_height > 0 ? tree->line_height : 16;
    gates_i32 step = (gates_i32)(wheel.y * (float)(GATES_SCROLL_WHEEL_LINES * line));
    gates_node_slot_t *s = gates_i_slot(tree, idx);
    gates_i32 off = gates_i_scroll_clamp(tree, idx, s->scroll_offset - step);
    if (off != s->scroll_offset) {
        s->scroll_offset = off;
        gates_i_mark_dirty(tree, idx, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    }
}

gates_node_t gates_input_pointer(gates_tree_t *tree, const gates_pointer_event_t *ev) {
    if (tree == nullptr || ev == nullptr) {
        return GATES_NODE_NULL;
    }

    /* An active handle/thumb drag owns the event stream until release. */
    if (tree->drag_kind != GATES_DRAG_NONE) {
        if (ev->action == GATES_POINTER_MOVE) {
            drag_update(tree, ev->pos);
        } else if (ev->action == GATES_POINTER_UP) {
            drag_update(tree, ev->pos);
            if (tree->drag_kind == GATES_DRAG_SLIDER) gates_i_slider_drag(tree, ev->pos, true);
            if (tree->drag_kind == GATES_DRAG_SPIN) gates_i_spin_arrows_release(tree);
            tree->drag_kind = GATES_DRAG_NONE;
            tree->drag_node = GATES_NONE;
        }
        return gates_i_handle(tree, tree->drag_node);
    }

    /* A press hides a tooltip (0.3.0). */
    if (ev->action == GATES_POINTER_DOWN) {
        gates_i_tip_dismiss(tree);
    }
    /* A press hides the keyboard cues (0.3.0). */
    if (ev->action == GATES_POINTER_DOWN && tree->cues_shown) {
        tree->cues_shown = false;
        if (!tree->cues_always) gates_i_mark_dirty(tree, GATES_NONE, GATES_DIRTY_PAINT);
    }
    /* Overlays first: a menu takes the event; a modal dialog is the only hit root. */
    bool taken = false;
    gates_u32 base = gates_i_overlay_pointer(tree, ev, &taken);
    if (taken) {
        return GATES_NODE_NULL;
    }
    gates_u32 hit = hit_idx(tree, base, ev->pos);

    /* The menu bar (0.3.0): hover over titles, a press opens or closes. */
    gates_u32 hover_title = GATES_NONE;
    if (hit != GATES_NONE && gates_i_slot(tree, hit)->kind == GATES_NODE_MENUBAR && hit == tree->menubar) {
        gates_i32 t = gates_i_menubar_title_at(tree, hit, ev->pos);
        hover_title = t >= 0 ? (gates_u32)t : GATES_NONE;
    }
    if (hover_title != tree->mb_hover && ev->action != GATES_POINTER_WHEEL) {
        tree->mb_hover = hover_title;
        if (tree->menubar != GATES_NONE) gates_i_mark_dirty(tree, tree->menubar, GATES_DIRTY_PAINT);
    }
    if (ev->action == GATES_POINTER_DOWN && tree->mb_mode != GATES_I_MB_OFF &&
        (hit == GATES_NONE || hit != tree->menubar)) {
        gates_i_menubar_leave(tree); /* a press elsewhere ends menu mode */
    }
    if (ev->action == GATES_POINTER_DOWN && ev->button == GATES_BUTTON_LEFT && hit != GATES_NONE &&
        hit == tree->menubar) {
        gates_i_menubar_press(tree, hit, ev->pos);
        return gates_i_handle(tree, hit);
    }
    /* Toolbars (0.3.0): hover and press per button; the focus stays where it is. */
    gates_u32 tb = hit != GATES_NONE && gates_i_slot(tree, hit)->kind == GATES_NODE_TOOLBAR &&
                           !gates_i_widget_inert(tree, gates_i_state(tree, gates_i_slot(tree, hit)->state_index))
                       ? hit
                       : GATES_NONE;
    if (ev->action == GATES_POINTER_MOVE || ev->action == GATES_POINTER_DOWN) {
        if (tree->tb_hover != GATES_NONE && tree->tb_hover != tb) {
            gates_i_state(tree, gates_i_slot(tree, tree->tb_hover)->state_index)->tbar->hover = -1;
            gates_i_mark_dirty(tree, tree->tb_hover, GATES_DIRTY_PAINT);
        }
        tree->tb_hover = tb;
        if (tb != GATES_NONE) {
            gates_i_toolbar *t = gates_i_state(tree, gates_i_slot(tree, tb)->state_index)->tbar;
            gates_i32 k = gates_i_toolbar_entry_at(tree, tb, ev->pos);
            if (k != t->hover) {
                t->hover = k;
                gates_i_mark_dirty(tree, tb, GATES_DIRTY_PAINT);
            }
        }
    }
    if (ev->action == GATES_POINTER_MOVE) {
        gates_i_tip_hover(tree, ev->pos, hit);
    }
    if (ev->action == GATES_POINTER_DOWN && ev->button == GATES_BUTTON_LEFT && hit != GATES_NONE &&
        gates_i_slot(tree, hit)->kind == GATES_NODE_TABSTRIP) {
        gates_i_tabstrip_press(tree, hit, ev->pos);
        return gates_i_handle(tree, hit);
    }
    if (ev->action == GATES_POINTER_DOWN && ev->button == GATES_BUTTON_LEFT && tb != GATES_NONE) {
        set_hover(tree, GATES_NONE);
        gates_i_toolbar_down(tree, tb, ev->pos);
        return gates_i_handle(tree, tb);
    }
    if (ev->action == GATES_POINTER_UP && ev->button == GATES_BUTTON_LEFT && tree->pressed != GATES_NONE &&
        gates_i_slot(tree, tree->pressed)->kind == GATES_NODE_TOOLBAR) {
        gates_u32 was = tree->pressed;
        tree->pressed = GATES_NONE;
        gates_i_toolbar_up(tree, was, ev->pos);
        return gates_i_handle(tree, was);
    }

    if (ev->action == GATES_POINTER_DOWN && ev->button == GATES_BUTTON_RIGHT && hit != GATES_NONE &&
        gates_i_slot(tree, hit)->kind == GATES_NODE_VIEW && gates_i_view_context(tree, hit, ev->pos)) {
        return gates_i_handle(tree, hit); /* the header menu (0.6.0) */
    }
    if (ev->action == GATES_POINTER_WHEEL) {
        if (hit != GATES_NONE && gates_i_slot(tree, hit)->kind == GATES_NODE_VIEW &&
            gates_i_view_wheel(tree, hit, ev->wheel)) {
            return gates_i_handle(tree, hit); /* a view scrolls itself */
        }
        if (hit != GATES_NONE && gates_i_slot(tree, hit)->kind == GATES_NODE_EDITOR &&
            gates_i_editor_wheel(tree, hit, ev->wheel)) {
            return gates_i_handle(tree, hit); /* so does an editor (0.7.0) */
        }
        if (hit != GATES_NONE && gates_i_range_wheel(tree, hit, ev->wheel)) {
            return gates_i_handle(tree, hit); /* a focused spin box or slider steps (0.8.0) */
        }
        wheel_scroll(tree, hit, ev->wheel);
        return hit == GATES_NONE ? GATES_NODE_NULL : gates_i_handle(tree, hit);
    }
    if (ev->action == GATES_POINTER_DOWN && ev->button == GATES_BUTTON_LEFT &&
        hit != GATES_NONE && drag_begin(tree, hit, ev)) {
        return gates_i_handle(tree, hit);
    }
    gates_u32 target = GATES_NONE;
    if (hit != GATES_NONE && interactive(tree, gates_i_slot(tree, hit))) {
        target = hit;
    }

    switch (ev->action) {
    case GATES_POINTER_MOVE:
        set_hover(tree, target);
        /* Leaving/entering while a press is held repaints the pressed widget. */
        break;
    case GATES_POINTER_DOWN:
        set_hover(tree, target);
        if (ev->button == GATES_BUTTON_LEFT && target != GATES_NONE &&
            gates_i_focus_eligible(tree, target)) {
            /* Clicking a button or checkbox gives it keyboard focus;
             * clicks elsewhere leave focus where it is. */
            gates_tree_set_focus(tree, gates_i_handle(tree, target));
        }
        if (ev->button == GATES_BUTTON_LEFT && target != GATES_NONE) {
            tree->pressed = target;
            gates_i_mark_dirty(tree, target, GATES_DIRTY_PAINT);
            if (gates_i_slot(tree, target)->kind == GATES_NODE_RADIO) {
                /* The row counts only if the release lands on it too. */
                gates_widget_state_t *rs =
                    gates_i_state(tree, gates_i_slot(tree, target)->state_index);
                rs->opt_press = gates_i_radio_row_at(tree, target, ev->pos);
            }
        }
        break;
    case GATES_POINTER_UP:
        if (ev->button == GATES_BUTTON_LEFT && tree->pressed != GATES_NONE) {
            gates_u32 was = tree->pressed;
            tree->pressed = GATES_NONE;
            gates_i_mark_dirty(tree, was, GATES_DIRTY_PAINT);
            if (gates_i_slot(tree, was)->kind == GATES_NODE_VIEW) {
                gates_i_view_pointer_up(tree, was, ev->pos); /* header: sort on same column */
            } else if (was == hit && gates_i_slot(tree, was)->kind == GATES_NODE_RADIO) {
                gates_widget_state_t *rs =
                    gates_i_state(tree, gates_i_slot(tree, was)->state_index);
                gates_i32 row = gates_i_radio_row_at(tree, was, ev->pos);
                if (row >= 0 && row == rs->opt_press && !rs->opts[row].disabled &&
                    !gates_i_widget_inert(tree, rs)) {
                    gates_err_t err = gates_i_option_pick(tree, was, rs->opts[row].id);
                    if (!gates_is_ok(err)) {
                        tree->input_error = err;
                    }
                }
                rs->opt_press = -1;
            } else if (was == hit) {
                gates_i_activate(tree, was); /* release inside -> click/toggle */
            }
        }
        set_hover(tree, target);
        break;
    case GATES_POINTER_WHEEL:
    default:
        break; /* scroll arrives with the scroll layout (staged) */
    }

    return hit == GATES_NONE ? GATES_NODE_NULL : gates_i_handle(tree, hit);
}
