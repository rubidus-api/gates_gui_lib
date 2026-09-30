/* gates_gui_lib - group box: a titled frame around a content panel, optionally
 * collapsible (0.4.0). The group is a column holding its title (GROUPHEAD,
 * the Tab stop of a collapsible group) and the content panel. Platform-free. */
#include <gates/widget.h>
#include <gates/layout.h>
#include <gates/ui.h>
#include "gates_tree_internal.h"

#include <string.h>

#define GROUP_PAD 8
#define GROUP_GAP 4
#define MARK_W 12
#define MARK_GAP 6
#define HEAD_PAD_X 4

static gates_widget_state_t *state_at(const gates_tree_t *tree, gates_u32 idx) {
    return gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
}

static gates_err_t make_node(gates_tree_t *tree, gates_node_t parent, gates_node_kind_t kind, gates_node_t *out) {
    gates_node_desc_t nd = { .kind = kind };
    gates_err_t err = gates_node_create(tree, parent, &nd, out);
    if (!gates_is_ok(err)) return err;
    gates_u32 state = GATES_NONE;
    err = gates_i_state_acquire(tree, &state);
    if (!gates_is_ok(err)) {
        gates_i_discard_detached(tree, *out);
        return err;
    }
    gates_i_slot(tree, out->index)->state_index = state;
    return GATES_OK;
}

static gates_u32 head_of(const gates_tree_t *tree, gates_u32 group) {
    return gates_i_slot(tree, group)->first_child;
}

static gates_u32 content_of(const gates_tree_t *tree, gates_u32 group) {
    gates_u32 h = head_of(tree, group);
    return h != GATES_NONE ? gates_i_slot(tree, h)->next_sibling : GATES_NONE;
}

bool gates_i_group_foldable(const gates_tree_t *tree, gates_u32 group) {
    const gates_widget_state_t *st = state_at(tree, group);
    return st != nullptr && st->group_fold;
}

gates_err_t gates_group_create(gates_tree_t *tree, gates_node_t parent, gates_str_t title, bool collapsible,
                               gates_node_t *out_group, gates_node_t *out_content) {
    if (tree == nullptr || out_group == nullptr || out_content == nullptr ||
        (title.size > 0 && title.ptr == nullptr) ||
        (!gates_node_eq(parent, GATES_NODE_NULL) && !gates_i_valid(tree, parent))) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_group = *out_content = GATES_NODE_NULL;
    gates_node_t group = GATES_NODE_NULL, head, content;
    gates_err_t err = make_node(tree, GATES_NODE_NULL, GATES_NODE_GROUP, &group);
    if (gates_is_ok(err)) {
        gates_widget_state_t *gs = state_at(tree, group.index);
        gs->group_fold = collapsible;
        gs->checked = true; /* expanded */
        err = gates_layout_set(tree, group, GATES_LAYOUT_KIND_COLUMN);
    }
    if (gates_is_ok(err)) err = gates_layout_set_padding(tree, group, GROUP_PAD);
    if (gates_is_ok(err)) err = gates_layout_set_gap(tree, group, GROUP_GAP);
    if (gates_is_ok(err)) err = make_node(tree, group, GATES_NODE_GROUPHEAD, &head);
    if (gates_is_ok(err)) {
        state_at(tree, head.index)->not_focusable = !collapsible;
        err = gates_widget_set_text(tree, head, title);
    }
    if (gates_is_ok(err)) err = gates_layout_set_child_align(tree, head, GATES_ALIGN_START_V);
    if (gates_is_ok(err)) err = gates_panel_create(tree, group, &content);
    if (gates_is_ok(err)) err = gates_layout_set(tree, content, GATES_LAYOUT_KIND_COLUMN);
    if (gates_is_ok(err)) err = gates_layout_set_gap(tree, content, 6);
    if (gates_is_ok(err) && !gates_node_eq(parent, GATES_NODE_NULL)) {
        err = gates_node_append(tree, parent, group);
    }
    if (!gates_is_ok(err)) {
        if (gates_i_valid(tree, group)) gates_i_discard_detached(tree, group);
        return err;
    }
    gates_i_mark_dirty(tree, group.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    *out_group = group;
    *out_content = content;
    return GATES_OK;
}

static void apply(gates_tree_t *tree, gates_u32 group, bool expanded) {
    gates_widget_state_t *st = state_at(tree, group);
    st->checked = expanded;
    st->revision++;
    (void)gates_node_set_hidden(tree, gates_i_handle(tree, content_of(tree, group)), !expanded);
    gates_i_mark_dirty(tree, group, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    gates_i_access_log(tree, GATES_ACCESS_CHANGED, group, 0);
}

gates_err_t gates_group_set_expanded(gates_tree_t *tree, gates_node_t group, bool expanded) {
    if (tree == nullptr || !gates_i_valid(tree, group) || gates_i_slot(tree, group.index)->kind != GATES_NODE_GROUP) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (!gates_i_group_foldable(tree, group.index)) return PROVEN_ERR_INVALID_STATE;
    if (state_at(tree, group.index)->checked != expanded) apply(tree, group.index, expanded);
    return GATES_OK;
}

bool gates_group_expanded(const gates_tree_t *tree, gates_node_t group) {
    if (tree == nullptr || !gates_i_valid(tree, group) || gates_i_slot(tree, group.index)->kind != GATES_NODE_GROUP) {
        return false;
    }
    return state_at(tree, group.index)->checked;
}

/* A person toggles (Space, Enter, a click, the mnemonic, accessibility). */
gates_err_t gates_i_group_toggle(gates_tree_t *tree, gates_u32 group, bool expanded) {
    if (!gates_i_group_foldable(tree, group)) return PROVEN_ERR_INVALID_STATE;
    if (state_at(tree, group)->checked == expanded) return GATES_OK;
    if (gates_i_wants_events(tree, group)) {
        gates_err_t err = gates_i_event_reserve(tree, 1, 0); /* no toggle without its report */
        if (!gates_is_ok(err)) return err;
    }
    apply(tree, group, expanded);
    gates_i_event_push(tree, group, GATES_EVENT_VALUE_CHANGED, GATES_ORIGIN_USER);
    return GATES_OK;
}

void gates_i_group_head_activate(gates_tree_t *tree, gates_u32 head) {
    gates_u32 group = gates_i_slot(tree, head)->parent;
    if (group == GATES_NONE) return;
    gates_err_t err = gates_i_group_toggle(tree, group, !state_at(tree, group)->checked);
    if (!gates_is_ok(err)) tree->input_error = err;
}

/* The first control of a plain group's content that takes focus (its mnemonic's target). */
gates_u32 gates_i_group_first(const gates_tree_t *tree, gates_u32 group) {
    gates_u32 top = content_of(tree, group);
    for (gates_u32 n = top; n != GATES_NONE;) {
        if (gates_i_focus_eligible(tree, n)) return n;
        const gates_node_slot_t *s = gates_i_slot(tree, n);
        if (s->first_child != GATES_NONE) {
            n = s->first_child;
            continue;
        }
        while (n != top && gates_i_slot(tree, n)->next_sibling == GATES_NONE) n = gates_i_slot(tree, n)->parent;
        if (n == top) break;
        n = gates_i_slot(tree, n)->next_sibling;
    }
    return GATES_NONE;
}

/* -- geometry and paint ----------------------------------------------------------------- */

gates_size_t gates_i_group_head_measure(const gates_tree_t *tree, const gates_node_slot_t *s,
                                        const gates_text_backend_t *text) {
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    gates_i32 font = gates_i_slot_font(tree, s);
    gates_text_metrics_t m = text->metrics(text->ctx, font);
    gates_str_t title = st != nullptr && st->text != nullptr ? (gates_str_t){ .ptr = st->text, .size = st->text_len }
                                                            : (gates_str_t){0};
    bool fold = s->parent != GATES_NONE && gates_i_group_foldable(tree, s->parent);
    gates_i32 w = gates_i_mn_width(text, font, title) + 2 * HEAD_PAD_X + (fold ? MARK_W + MARK_GAP : 0);
    gates_i32 h = m.line_height;
    if (fold && h + 4 < GATES_ACCESS_MIN_TARGET) h = GATES_ACCESS_MIN_TARGET;
    return (gates_size_t){ w, h };
}

gates_err_t gates_i_group_paint(const gates_tree_t *tree, gates_u32 group, gates_draw_list_t *dl,
                                const gates_theme_t *theme) {
    gates_rect_t r = gates_i_slot(tree, group)->layout_rect;
    gates_u32 head = head_of(tree, group);
    gates_i32 top = r.y;
    if (head != GATES_NONE) {
        gates_rect_t hr = gates_i_slot(tree, head)->layout_rect;
        top = hr.y + hr.h / 2;
    }
    return gates_draw_border(dl, (gates_rect_t){ r.x, top, r.w, r.y + r.h - top }, 1,
                             gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER));
}

gates_err_t gates_i_group_head_paint(const gates_tree_t *tree, gates_u32 head, gates_draw_list_t *dl,
                                     const gates_theme_t *theme, const gates_text_backend_t *text) {
    const gates_node_slot_t *s = gates_i_slot(tree, head);
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    gates_rect_t r = s->layout_rect;
    gates_i32 font = gates_i_font(tree, head);
    gates_text_metrics_t m = text->metrics(text->ctx, font);
    bool fold = s->parent != GATES_NONE && gates_i_group_foldable(tree, s->parent);
    bool open = s->parent != GATES_NONE && state_at(tree, s->parent)->checked;
    /* The title interrupts the frame's top line. */
    gates_err_t err = gates_draw_rect(dl, r, gates_theme_color(theme, GATES_COLOR_PANEL_BG));
    gates_color_t fg = gates_theme_color(theme, GATES_COLOR_PANEL_FG);
    gates_i32 x = r.x + HEAD_PAD_X, cy = r.y + r.h / 2;
    if (gates_is_ok(err) && fold) {
        /* An open mark points down, a closed one right: bars of growing length. */
        for (gates_i32 k = 0; k < 4 && gates_is_ok(err); k++) {
            err = open ? gates_draw_rect(dl, (gates_rect_t){ x + 2 + k, cy - 2 + k, 7 - 2 * k, 1 }, fg)
                       : gates_draw_rect(dl, (gates_rect_t){ x + 3 + k, cy - 3 + k, 1, 7 - 2 * k }, fg);
        }
        x += MARK_W + MARK_GAP;
    }
    if (gates_is_ok(err) && st != nullptr && st->text != nullptr) {
        err = gates_i_mn_draw(dl, text, (gates_rect_t){ x, r.y + (r.h - m.line_height) / 2, 0, m.line_height },
                              (gates_str_t){ .ptr = st->text, .size = st->text_len }, font, fg, gates_i_cues(tree));
    }
    if (gates_is_ok(err) && tree->focus == head) {
        err = gates_draw_border(dl, r, gates_theme_focus_width(theme), gates_theme_color(theme, GATES_COLOR_FOCUS_RING));
    }
    return err;
}
