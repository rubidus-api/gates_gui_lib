/* gates_gui_lib - keyboard focus: eligibility, tree-order traversal inside the
 * focus scope, repair when the focused control goes away, and scrolling the
 * focused control into view (plan-0009, RFC-0003 5.1). Platform-free. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/layout.h>
#include "gates_tree_internal.h"

gates_u32 gates_i_scope_root(const gates_tree_t *tree) {
    return tree->focus_scope != GATES_NONE ? tree->focus_scope : tree->root;
}

static bool focusable_kind(gates_node_kind_t k) {
    return k == GATES_NODE_BUTTON || k == GATES_NODE_CHECKBOX || k == GATES_NODE_TEXTBOX ||
           k == GATES_NODE_RADIO || k == GATES_NODE_CHOICE || k == GATES_NODE_VIEW ||
           k == GATES_NODE_TOOLBAR;
}

/* Reachable: nothing on the way up is hidden, every stack ancestor shows the
 * branch, and the scope root is an ancestor (or the node itself). */
bool gates_i_reachable(const gates_tree_t *tree, gates_u32 idx) {
    gates_u32 scope = gates_i_scope_root(tree);
    gates_u32 child = idx;
    for (gates_u32 p = gates_i_slot(tree, idx)->parent; child != scope; ) {
        if (gates_i_slot(tree, child)->hidden) {
            return false;
        }
        if (p == GATES_NONE) {
            return false; /* detached, or outside the scope */
        }
        const gates_node_slot_t *ps = gates_i_slot(tree, p);
        if (ps->layout_kind == GATES_LAYOUT_STACK) {
            gates_u32 i = 0;
            gates_u32 c = ps->first_child;
            while (c != GATES_NONE && c != child) {
                c = gates_i_slot(tree, c)->next_sibling;
                i++;
            }
            if (i != ps->active_child) {
                return false; /* on an inactive page */
            }
        }
        child = p;
        p = ps->parent;
    }
    return true;
}

bool gates_i_focus_eligible(const gates_tree_t *tree, gates_u32 idx) {
    if (idx == GATES_NONE || idx >= tree->capacity) {
        return false;
    }
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (!s->alive || s->destroy_pending || !focusable_kind(s->kind)) {
        return false;
    }
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    if (st == nullptr || st->not_focusable || gates_i_widget_inert(tree, st)) {
        return false;
    }
    return gates_i_reachable(tree, idx);
}

/* Pre-order successor inside the scope; wraps to the scope's first node. */
static gates_u32 next_node(const gates_tree_t *tree, gates_u32 idx, bool skip_children) {
    gates_u32 scope = gates_i_scope_root(tree);
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (!skip_children && s->first_child != GATES_NONE) {
        return s->first_child;
    }
    for (gates_u32 n = idx; n != scope && n != GATES_NONE; n = gates_i_slot(tree, n)->parent) {
        gates_u32 sib = gates_i_slot(tree, n)->next_sibling;
        if (sib != GATES_NONE) {
            return sib;
        }
    }
    return scope; /* wrapped */
}

static gates_u32 last_descendant(const gates_tree_t *tree, gates_u32 idx) {
    while (gates_i_slot(tree, idx)->last_child != GATES_NONE) {
        idx = gates_i_slot(tree, idx)->last_child;
    }
    return idx;
}

/* Pre-order predecessor inside the scope; wraps to the scope's last node. */
static gates_u32 prev_node(const gates_tree_t *tree, gates_u32 idx) {
    gates_u32 scope = gates_i_scope_root(tree);
    if (idx == scope) {
        return last_descendant(tree, scope);
    }
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->prev_sibling != GATES_NONE) {
        return last_descendant(tree, s->prev_sibling);
    }
    return s->parent != GATES_NONE ? s->parent : scope;
}

/* The next eligible node after `from` (not `from` itself), or GATES_NONE.
 * skip_subtree: do not enter `from`'s children (it is going away). */
static gates_u32 find_next(const gates_tree_t *tree, gates_u32 from, bool backward,
                           bool skip_subtree) {
    gates_u32 limit = tree->capacity + 2;
    gates_u32 n = from;
    for (gates_u32 i = 0; i < limit; i++) {
        n = backward ? prev_node(tree, n) : next_node(tree, n, skip_subtree && i == 0);
        if (n == from) {
            return GATES_NONE;
        }
        if (skip_subtree) {
            /* Never land inside the leaving subtree. */
            bool inside = false;
            for (gates_u32 a = n; a != GATES_NONE; a = gates_i_slot(tree, a)->parent) {
                if (a == from) {
                    inside = true;
                    break;
                }
            }
            if (inside) {
                continue;
            }
        }
        if (gates_i_focus_eligible(tree, n)) {
            return n;
        }
    }
    return GATES_NONE;
}

void gates_i_focus_check(gates_tree_t *tree) {
    gates_i_choice_lists_check(tree, GATES_NONE); /* same triggers: disabled, hidden, page */
    gates_i_menubar_check(tree);
    gates_i_tip_check(tree);
    if (tree->focus == GATES_NONE || gates_i_focus_eligible(tree, tree->focus)) {
        return;
    }
    gates_u32 next = find_next(tree, tree->focus, false, false);
    gates_tree_set_focus(tree, gates_i_handle(tree, next));
}

void gates_i_focus_leave_subtree(gates_tree_t *tree, gates_u32 idx) {
    if (tree->focus == GATES_NONE) {
        return;
    }
    bool inside = false;
    for (gates_u32 a = tree->focus; a != GATES_NONE; a = gates_i_slot(tree, a)->parent) {
        if (a == idx) {
            inside = true;
            break;
        }
    }
    if (!inside) {
        return;
    }
    gates_u32 next = find_next(tree, idx, false, true);
    gates_tree_set_focus(tree, gates_i_handle(tree, next));
}

/* How far scroll offsets above `idx` moved since the last arrange: layout
 * rects are stale by that much until the next layout run. */
static gates_i32 pending_shift(const gates_tree_t *tree, gates_u32 idx) {
    gates_i32 shift = 0;
    for (gates_u32 a = gates_i_slot(tree, idx)->parent; a != GATES_NONE;
         a = gates_i_slot(tree, a)->parent) {
        const gates_node_slot_t *as = gates_i_slot(tree, a);
        if (as->layout_kind == GATES_LAYOUT_SCROLL) {
            shift += as->scroll_offset - as->scroll_arranged;
        }
    }
    return shift;
}

void gates_i_scroll_into_view(gates_tree_t *tree, gates_u32 idx) {
    for (gates_u32 a = gates_i_slot(tree, idx)->parent; a != GATES_NONE;
         a = gates_i_slot(tree, a)->parent) {
        gates_node_slot_t *as = gates_i_slot(tree, a);
        if (as->layout_kind != GATES_LAYOUT_SCROLL || !gates_i_scrollable(tree, a)) {
            continue;
        }
        /* Where the node and this viewport are now, not at the last layout. */
        gates_rect_t r = gates_i_slot(tree, idx)->layout_rect;
        r.y -= pending_shift(tree, idx);
        gates_rect_t vp = gates_i_scroll_viewport(tree, a);
        vp.y -= pending_shift(tree, a);
        gates_i32 delta = 0;
        if (r.y < vp.y) {
            delta = r.y - vp.y;
        } else if (r.y + r.h > vp.y + vp.h) {
            delta = (r.y + r.h) - (vp.y + vp.h);
        }
        if (delta != 0) {
            gates_i32 off = gates_i_scroll_clamp(tree, a, as->scroll_offset + delta);
            gates_i32 moved = off - as->scroll_offset;
            if (moved != 0) {
                as->scroll_offset = off;
                gates_i_mark_dirty(tree, a, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
            }
        }
    }
}

bool gates_tree_focus_next(gates_tree_t *tree, bool backward) {
    if (tree == nullptr) {
        return false;
    }
    gates_u32 from = tree->focus;
    gates_u32 next;
    if (from == GATES_NONE || !gates_i_focus_eligible(tree, from)) {
        /* Start at the scope edge: the first (or last) eligible control. */
        gates_u32 scope = gates_i_scope_root(tree);
        next = gates_i_focus_eligible(tree, scope) && !backward
                   ? scope
                   : find_next(tree, scope, backward, false);
        if (backward && next == GATES_NONE && gates_i_focus_eligible(tree, scope)) {
            next = scope;
        }
    } else {
        next = find_next(tree, from, backward, false);
    }
    if (next == GATES_NONE) {
        return false;
    }
    gates_tree_set_focus(tree, gates_i_handle(tree, next));
    gates_i_scroll_into_view(tree, next);
    return true;
}

gates_err_t gates_widget_set_focusable(gates_tree_t *tree, gates_node_t node, bool focusable) {
    if (!gates_i_valid(tree, node)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, node.index)->state_index);
    if (st == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    st->not_focusable = !focusable;
    gates_i_focus_check(tree);
    return GATES_OK;
}

bool gates_widget_focusable(const gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node) && gates_i_focus_eligible(tree, node.index);
}
