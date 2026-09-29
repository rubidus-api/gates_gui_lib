/* gates_gui_lib — node pool + retained tree core (RFC-0001 §6-§8).
 *
 * Invariants maintained here (RFC-0001 §30):
 *   - live node links are mutually consistent; no cycles; root has no parent;
 *   - a dead slot is never linked in the tree;
 *   - a stale handle cannot access a new live node (generation check);
 *   - a destroy_pending node is never a valid target (is_valid false);
 *   - all memory flows through the injected allocator (no hidden malloc);
 *   - growth and destroy are failure-atomic: on allocation failure the tree
 *     is left unchanged.
 */
#include "gates_tree_internal.h"
#include <proven/heap.h>

#include <string.h>
#include <stdatomic.h>

#define GATES_DEFAULT_CAPACITY 32u
#define GATES_STATE_DEFAULT_CAPACITY 16u

/* -- shared helpers -------------------------------------------------------- */

gates_node_slot_t *gates_i_slot(const gates_tree_t *tree, gates_u32 idx) {
    return &tree->slots[idx];
}

bool gates_i_valid(const gates_tree_t *tree, gates_node_t node) {
    if (tree == nullptr || node.index >= tree->capacity) {
        return false;
    }
    const gates_node_slot_t *s = gates_i_slot(tree, node.index);
    return s->alive && !s->destroy_pending && s->generation == node.generation;
}

gates_node_t gates_i_handle(const gates_tree_t *tree, gates_u32 idx) {
    if (idx == GATES_NONE) {
        return GATES_NODE_NULL;
    }
    return (gates_node_t){ .index = idx, .generation = gates_i_slot(tree, idx)->generation };
}

void gates_i_mark_dirty(gates_tree_t *tree, gates_u32 idx, gates_u32 bits) {
    if (idx != GATES_NONE) {
        gates_i_slot(tree, idx)->dirty |= bits;
        gates_i_access_log(tree, GATES_ACCESS_CHANGED, idx, 0); /* no-op unless enabled */
    }
    tree->dirty_bits |= bits;
}

static void slot_reset_links(gates_node_slot_t *s) {
    s->parent = GATES_NONE;
    s->first_child = GATES_NONE;
    s->last_child = GATES_NONE;
    s->prev_sibling = GATES_NONE;
    s->next_sibling = GATES_NONE;
    s->child_count = 0;
}

static void slot_reset_content(gates_node_slot_t *s) {
    s->kind = GATES_NODE_CUSTOM;
    s->state_index = GATES_NONE;
    s->font = -1; /* GATES_FONT_INHERIT */
    s->layout_kind = GATES_LAYOUT_NONE;
    s->grow = 0;
    s->align = GATES_ALIGN_STRETCH;
    s->padding = 0;
    s->gap = 0;
    s->active_child = 0;
    s->abs_rect = (gates_rect_t){ 0, 0, 0, 0 };
    s->pref = (gates_size_t){ 0, 0 };
    s->layout_rect = (gates_rect_t){ 0, 0, 0, 0 };
    s->split_ratio = 500;          /* centered until set */
    s->split_vertical = 0;
    s->scroll_offset = 0;
    s->content_size = (gates_size_t){ 0, 0 };
    s->dirty = 0;
    s->user_data = nullptr;
}

/* -- widget state pool ------------------------------------------------------ */

static void state_link_free_range(gates_tree_t *tree, gates_u32 from, gates_u32 to) {
    for (gates_u32 i = to; i > from; i--) {
        gates_u32 idx = i - 1;
        memset(&tree->states[idx], 0, sizeof tree->states[idx]);
        tree->states[idx].next_free = tree->state_first_free;
        tree->state_first_free = idx;
    }
}

gates_err_t gates_i_state_acquire(gates_tree_t *tree, gates_u32 *out_index) {
    if (tree->state_first_free == GATES_NONE) {
        gates_u32 old_cap = tree->state_cap;
        gates_u32 new_cap = old_cap == 0 ? GATES_STATE_DEFAULT_CAPACITY : old_cap * 2u;
        if (new_cap <= old_cap && old_cap != 0) {
            return PROVEN_ERR_OVERFLOW;
        }
        proven_result_mem_mut_t res;
        if (tree->states == nullptr) {
            res = tree->alloc.alloc_fn(tree->alloc.ctx,
                                       (proven_size_t)new_cap * sizeof(gates_widget_state_t),
                                       alignof(gates_widget_state_t));
        } else {
            res = tree->alloc.realloc_fn(tree->alloc.ctx, tree->states,
                                         (proven_size_t)old_cap * sizeof(gates_widget_state_t),
                                         (proven_size_t)new_cap * sizeof(gates_widget_state_t),
                                         alignof(gates_widget_state_t));
        }
        if (!proven_is_ok(res.err)) {
            return res.err;
        }
        tree->states = (gates_widget_state_t *)res.value.ptr;
        tree->state_cap = new_cap;
        state_link_free_range(tree, old_cap, new_cap);
    }
    gates_u32 idx = tree->state_first_free;
    gates_widget_state_t *st = &tree->states[idx];
    tree->state_first_free = st->next_free;
    memset(st, 0, sizeof *st);
    st->in_use = true;
    *out_index = idx;
    return GATES_OK;
}

void gates_i_state_release(gates_tree_t *tree, gates_u32 state_index) {
    if (state_index == GATES_NONE || state_index >= tree->state_cap) {
        return;
    }
    gates_widget_state_t *st = &tree->states[state_index];
    if (!st->in_use) {
        return;
    }
    if (st->text != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, st->text);
    }
    if (st->edit != nullptr) {
        gates_text_edit_deinit(st->edit);
        tree->alloc.free_fn(tree->alloc.ctx, st->edit);
    }
    gates_i_box_free(tree, st);
    gates_i_menu_free(tree, st);
    gates_i_options_free(tree, st);
    gates_i_form_free(tree, st);
    gates_i_view_free(tree, st);
    gates_i_menubar_free(tree, st);
    gates_i_toolbar_free(tree, st);
    gates_i_tabs_free(tree, st);
    gates_i_range_free(tree, st);
    memset(st, 0, sizeof *st);
    st->next_free = tree->state_first_free;
    tree->state_first_free = state_index;
}

gates_widget_state_t *gates_i_state(const gates_tree_t *tree, gates_u32 state_index) {
    if (state_index == GATES_NONE || state_index >= tree->state_cap) {
        return nullptr;
    }
    gates_widget_state_t *st = &tree->states[state_index];
    return st->in_use ? st : nullptr;
}

/* -- slot pool -------------------------------------------------------------- */

static void free_list_add_range(gates_tree_t *tree, gates_u32 from, gates_u32 to) {
    for (gates_u32 i = to; i > from; i--) {
        gates_u32 idx = i - 1;
        gates_node_slot_t *s = gates_i_slot(tree, idx);
        memset(s, 0, sizeof *s);
        slot_reset_links(s);
        slot_reset_content(s);
        s->next_free = tree->first_free;
        tree->first_free = idx;
    }
}

static gates_err_t pool_grow(gates_tree_t *tree) {
    gates_u32 old_cap = tree->capacity;
    gates_u32 new_cap = old_cap * 2u;
    if (new_cap <= old_cap) {
        return PROVEN_ERR_OVERFLOW;
    }
    proven_result_mem_mut_t res = tree->alloc.realloc_fn(
        tree->alloc.ctx, tree->slots,
        (proven_size_t)old_cap * sizeof(gates_node_slot_t),
        (proven_size_t)new_cap * sizeof(gates_node_slot_t),
        alignof(gates_node_slot_t));
    if (!proven_is_ok(res.err)) {
        return res.err;
    }
    tree->slots = (gates_node_slot_t *)res.value.ptr;
    tree->capacity = new_cap;
    free_list_add_range(tree, old_cap, new_cap);
    return GATES_OK;
}

static gates_err_t pool_alloc(gates_tree_t *tree, gates_u32 *out_idx) {
    if (tree->first_free == GATES_NONE) {
        gates_err_t err = pool_grow(tree);
        if (!gates_is_ok(err)) {
            return err;
        }
    }
    gates_u32 idx = tree->first_free;
    gates_node_slot_t *s = gates_i_slot(tree, idx);
    tree->first_free = s->next_free;
    /* generation survives across free/alloc cycles (bumped on free). */
    s->next_free = GATES_NONE;
    s->alive = true;
    s->destroy_pending = false;
    slot_reset_links(s);
    slot_reset_content(s);
    tree->live_count++;
    *out_idx = idx;
    return GATES_OK;
}

static void pool_free_slot(gates_tree_t *tree, gates_u32 idx) {
    gates_node_slot_t *s = gates_i_slot(tree, idx);
    gates_i_state_release(tree, s->state_index);
    if (tree->hover == idx) {
        tree->hover = GATES_NONE;
    }
    if (tree->pressed == idx) {
        tree->pressed = GATES_NONE;
    }
    if (tree->drag_node == idx) {
        tree->drag_node = GATES_NONE;
        tree->drag_kind = GATES_DRAG_NONE;
    }
    if (tree->focus == idx) {
        tree->focus = GATES_NONE;
    }
    if (tree->key_press == idx) {
        tree->key_press = GATES_NONE;
    }
    if (tree->focus_scope == idx) {
        tree->focus_scope = GATES_NONE;
    }
    if (tree->menubar == idx) {
        tree->menubar = GATES_NONE;
        tree->mb_mode = GATES_I_MB_OFF;
    }
    if (tree->tb_hover == idx) {
        tree->tb_hover = GATES_NONE;
    }
    s->alive = false;
    s->destroy_pending = false;
    s->generation++;
    slot_reset_links(s);
    slot_reset_content(s);
    s->next_free = tree->first_free;
    tree->first_free = idx;
}

/* Frees a detached subtree that was never handed out, without allocating:
 * the rollback path of constructors that fail half-way (plan-0009). */
static void discard_subtree(gates_tree_t *tree, gates_u32 idx) {
    gates_u32 c = gates_i_slot(tree, idx)->first_child;
    while (c != GATES_NONE) {
        gates_u32 next = gates_i_slot(tree, c)->next_sibling;
        discard_subtree(tree, c);
        c = next;
    }
    if (tree->live_count > 0) {
        tree->live_count--;
    }
    pool_free_slot(tree, idx);
}

void gates_i_discard_detached(gates_tree_t *tree, gates_node_t node) {
    if (!gates_i_valid(tree, node) || node.index == tree->root ||
        gates_i_slot(tree, node.index)->parent != GATES_NONE) {
        return;
    }
    discard_subtree(tree, node.index);
}

/* -- link helpers (O(1), RFC-0001 §6.3) ------------------------------------- */

static void link_append(gates_tree_t *tree, gates_u32 parent, gates_u32 child) {
    gates_i_access_log(tree, GATES_ACCESS_STRUCTURE, parent, 0);
    gates_node_slot_t *p = gates_i_slot(tree, parent);
    gates_node_slot_t *c = gates_i_slot(tree, child);
    c->parent = parent;
    c->prev_sibling = p->last_child;
    c->next_sibling = GATES_NONE;
    if (p->last_child != GATES_NONE) {
        gates_i_slot(tree, p->last_child)->next_sibling = child;
    } else {
        p->first_child = child;
    }
    p->last_child = child;
    p->child_count++;
    gates_i_mark_dirty(tree, parent, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
}

static void link_insert_before(gates_tree_t *tree, gates_u32 parent,
                               gates_u32 child, gates_u32 before) {
    gates_i_access_log(tree, GATES_ACCESS_STRUCTURE, parent, 0);
    gates_node_slot_t *p = gates_i_slot(tree, parent);
    gates_node_slot_t *c = gates_i_slot(tree, child);
    gates_node_slot_t *b = gates_i_slot(tree, before);
    c->parent = parent;
    c->next_sibling = before;
    c->prev_sibling = b->prev_sibling;
    if (b->prev_sibling != GATES_NONE) {
        gates_i_slot(tree, b->prev_sibling)->next_sibling = child;
    } else {
        p->first_child = child;
    }
    b->prev_sibling = child;
    p->child_count++;
    gates_i_mark_dirty(tree, parent, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
}

static void link_unlink(gates_tree_t *tree, gates_u32 child) {
    gates_i_access_log(tree, GATES_ACCESS_STRUCTURE, gates_i_slot(tree, child)->parent, 0);
    gates_node_slot_t *c = gates_i_slot(tree, child);
    gates_u32 parent = c->parent;
    gates_node_slot_t *p = gates_i_slot(tree, parent);
    if (c->prev_sibling != GATES_NONE) {
        gates_i_slot(tree, c->prev_sibling)->next_sibling = c->next_sibling;
    } else {
        p->first_child = c->next_sibling;
    }
    if (c->next_sibling != GATES_NONE) {
        gates_i_slot(tree, c->next_sibling)->prev_sibling = c->prev_sibling;
    } else {
        p->last_child = c->prev_sibling;
    }
    p->child_count--;
    c->parent = GATES_NONE;
    c->prev_sibling = GATES_NONE;
    c->next_sibling = GATES_NONE;
    gates_i_mark_dirty(tree, parent, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
}

static bool is_same_or_descendant(const gates_tree_t *tree, gates_u32 idx, gates_u32 ancestor) {
    for (gates_u32 cur = idx; cur != GATES_NONE; cur = gates_i_slot(tree, cur)->parent) {
        if (cur == ancestor) {
            return true;
        }
    }
    return false;
}

typedef void (*subtree_visit_fn)(gates_tree_t *tree, gates_u32 idx, void *ctx);

static void for_each_in_subtree(gates_tree_t *tree, gates_u32 start,
                                subtree_visit_fn visit, void *ctx) {
    gates_u32 cur = start;
    for (;;) {
        visit(tree, cur, ctx);
        if (gates_i_slot(tree, cur)->first_child != GATES_NONE) {
            cur = gates_i_slot(tree, cur)->first_child;
            continue;
        }
        while (cur != start && gates_i_slot(tree, cur)->next_sibling == GATES_NONE) {
            cur = gates_i_slot(tree, cur)->parent;
        }
        if (cur == start) {
            break;
        }
        cur = gates_i_slot(tree, cur)->next_sibling;
    }
}

static void visit_count(gates_tree_t *tree, gates_u32 idx, void *ctx) {
    (void)tree; (void)idx;
    (*(gates_u32 *)ctx)++;
}

static void visit_mark_pending(gates_tree_t *tree, gates_u32 idx, void *ctx) {
    (void)ctx;
    gates_node_slot_t *s = gates_i_slot(tree, idx);
    s->destroy_pending = true;
    tree->live_count--;
    tree->pending[tree->pending_len++] = idx;
}

static gates_err_t pending_reserve(gates_tree_t *tree, gates_u32 extra) {
    gates_u32 need = tree->pending_len + extra;
    if (need < tree->pending_len) {
        return PROVEN_ERR_OVERFLOW;
    }
    if (need <= tree->pending_cap) {
        return GATES_OK;
    }
    gates_u32 new_cap = tree->pending_cap == 0 ? 16u : tree->pending_cap;
    while (new_cap < need) {
        if (new_cap > (UINT32_MAX / 2u)) {
            return PROVEN_ERR_OVERFLOW;
        }
        new_cap *= 2u;
    }
    proven_result_mem_mut_t res;
    if (tree->pending == nullptr) {
        res = tree->alloc.alloc_fn(tree->alloc.ctx,
                                   (proven_size_t)new_cap * sizeof(gates_u32),
                                   alignof(gates_u32));
    } else {
        res = tree->alloc.realloc_fn(tree->alloc.ctx, tree->pending,
                                     (proven_size_t)tree->pending_cap * sizeof(gates_u32),
                                     (proven_size_t)new_cap * sizeof(gates_u32),
                                     alignof(gates_u32));
    }
    if (!proven_is_ok(res.err)) {
        return res.err;
    }
    tree->pending = (gates_u32 *)res.value.ptr;
    tree->pending_cap = new_cap;
    return GATES_OK;
}

/* -- lifecycle -------------------------------------------------------------- */

/* Tree serial numbers (plan-0012): unique for the process, so a posted target
 * never matches a later tree that reuses the memory. */
static _Atomic gates_u64 g_tree_serial;

gates_err_t gates_tree_create(const gates_tree_desc_t *desc, gates_tree_t **out_tree) {
    if (out_tree == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_tree = nullptr;

    gates_allocator_t alloc = (desc != nullptr && proven_alloc_is_valid(desc->allocator))
                                  ? desc->allocator
                                  : proven_heap_allocator();
    gates_u32 capacity = (desc != nullptr && desc->initial_capacity != 0)
                             ? desc->initial_capacity
                             : GATES_DEFAULT_CAPACITY;

    proven_result_mem_mut_t tres = alloc.alloc_fn(alloc.ctx, sizeof(gates_tree_t),
                                                  alignof(gates_tree_t));
    if (!proven_is_ok(tres.err)) {
        return tres.err;
    }
    gates_tree_t *tree = (gates_tree_t *)tres.value.ptr;
    memset(tree, 0, sizeof *tree);
    tree->alloc = alloc;
    tree->first_free = GATES_NONE;
    tree->root = GATES_NONE;
    tree->state_first_free = GATES_NONE;
    tree->hover = GATES_NONE;
    tree->pressed = GATES_NONE;
    tree->drag_node = GATES_NONE;
    tree->drag_kind = GATES_DRAG_NONE;
    tree->focus = GATES_NONE;
    tree->key_press = GATES_NONE;
    tree->focus_scope = GATES_NONE;
    tree->menubar = GATES_NONE;
    tree->mb_hover = GATES_NONE;
    tree->tip_index = GATES_NONE;
    tree->tb_hover = GATES_NONE;
    tree->serial = atomic_fetch_add(&g_tree_serial, 1) + 1;

    proven_result_mem_mut_t sres = alloc.alloc_fn(
        alloc.ctx, (proven_size_t)capacity * sizeof(gates_node_slot_t),
        alignof(gates_node_slot_t));
    if (!proven_is_ok(sres.err)) {
        alloc.free_fn(alloc.ctx, tree);
        return sres.err;
    }
    tree->slots = (gates_node_slot_t *)sres.value.ptr;
    tree->capacity = capacity;
    free_list_add_range(tree, 0, capacity);

    gates_u32 root_idx = GATES_NONE;
    gates_err_t err = pool_alloc(tree, &root_idx);
    if (!gates_is_ok(err)) {
        alloc.free_fn(alloc.ctx, tree->slots);
        alloc.free_fn(alloc.ctx, tree);
        return err;
    }
    tree->root = root_idx;

    *out_tree = tree;
    return GATES_OK;
}

void gates_tree_destroy(gates_tree_t *tree) {
    if (tree == nullptr) {
        return;
    }
    gates_allocator_t alloc = tree->alloc;
    /* Before any slot goes: messages still queued for this tree become stale. */
    if (tree->sender != nullptr) {
        gates_sender_detach(tree->sender, tree);
    }
    gates_i_post_tree_free(tree);
    gates_i_timers_free(tree);
    gates_i_access_free(tree);
    gates_i_tip_free(tree);
    gates_i_bubble_free(tree);
    gates_i_grid_free(tree);
    gates_i_images_free(tree);
    /* Release owned widget text before dropping the pools. */
    if (tree->states != nullptr) {
        for (gates_u32 i = 0; i < tree->state_cap; i++) {
            if (!tree->states[i].in_use) {
                continue;
            }
            if (tree->states[i].text != nullptr) {
                alloc.free_fn(alloc.ctx, tree->states[i].text);
            }
            if (tree->states[i].edit != nullptr) {
                gates_text_edit_deinit(tree->states[i].edit);
                alloc.free_fn(alloc.ctx, tree->states[i].edit);
            }
            gates_i_box_free(tree, &tree->states[i]);
            gates_i_menu_free(tree, &tree->states[i]);
            gates_i_options_free(tree, &tree->states[i]);
            gates_i_form_free(tree, &tree->states[i]);
            gates_i_view_free(tree, &tree->states[i]);
            gates_i_menubar_free(tree, &tree->states[i]);
            gates_i_toolbar_free(tree, &tree->states[i]);
            gates_i_tabs_free(tree, &tree->states[i]);
            gates_i_range_free(tree, &tree->states[i]);
        }
        alloc.free_fn(alloc.ctx, tree->states);
    }
    if (tree->pending != nullptr) {
        alloc.free_fn(alloc.ctx, tree->pending);
    }
    gates_i_event_free(tree);
    gates_i_commands_free(tree);
    alloc.free_fn(alloc.ctx, tree->slots);
    alloc.free_fn(alloc.ctx, tree);
}

gates_node_t gates_tree_root(const gates_tree_t *tree) {
    if (tree == nullptr || tree->root == GATES_NONE) {
        return GATES_NODE_NULL;
    }
    return gates_i_handle(tree, tree->root);
}

bool gates_node_is_valid(const gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node);
}

/* -- node ops ---------------------------------------------------------------- */

gates_err_t gates_node_create(gates_tree_t *tree, gates_node_t parent,
                              const gates_node_desc_t *desc, gates_node_t *out_node) {
    if (tree == nullptr || out_node == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_node = GATES_NODE_NULL;
    bool attach = !gates_node_is_null(parent);
    if (attach && !gates_i_valid(tree, parent)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_u32 idx = GATES_NONE;
    gates_err_t err = pool_alloc(tree, &idx);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (desc != nullptr) {
        s->kind = desc->kind;
        s->user_data = desc->user_data;
    }
    if (attach) {
        link_append(tree, parent.index, idx);
    }
    *out_node = gates_i_handle(tree, idx);
    return GATES_OK;
}

gates_err_t gates_node_append(gates_tree_t *tree, gates_node_t parent, gates_node_t child) {
    return gates_node_insert_before(tree, parent, child, GATES_NODE_NULL);
}

gates_err_t gates_node_insert_before(gates_tree_t *tree, gates_node_t parent,
                                     gates_node_t child, gates_node_t before) {
    if (!gates_i_valid(tree, parent) || !gates_i_valid(tree, child)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (child.index == tree->root) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (gates_i_slot(tree, child.index)->parent != GATES_NONE) {
        return PROVEN_ERR_INVALID_STATE;
    }
    if (is_same_or_descendant(tree, parent.index, child.index)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (gates_node_is_null(before)) {
        link_append(tree, parent.index, child.index);
        return GATES_OK;
    }
    if (!gates_i_valid(tree, before) || gates_i_slot(tree, before.index)->parent != parent.index) {
        return PROVEN_ERR_INVALID_ARG;
    }
    link_insert_before(tree, parent.index, child.index, before.index);
    return GATES_OK;
}

gates_err_t gates_node_remove(gates_tree_t *tree, gates_node_t node) {
    if (!gates_i_valid(tree, node)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (node.index == tree->root) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (gates_i_slot(tree, node.index)->parent == GATES_NONE) {
        return PROVEN_ERR_INVALID_STATE;
    }
    link_unlink(tree, node.index);
    return GATES_OK;
}

gates_err_t gates_node_reparent(gates_tree_t *tree, gates_node_t node,
                                gates_node_t new_parent) {
    if (!gates_i_valid(tree, node) || !gates_i_valid(tree, new_parent)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (node.index == tree->root) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (is_same_or_descendant(tree, new_parent.index, node.index)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (gates_i_slot(tree, node.index)->parent != GATES_NONE) {
        link_unlink(tree, node.index);
    }
    link_append(tree, new_parent.index, node.index);
    return GATES_OK;
}

gates_err_t gates_node_destroy(gates_tree_t *tree, gates_node_t node) {
    if (!gates_i_valid(tree, node)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (node.index == tree->root) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_u32 subtree_size = 0;
    for_each_in_subtree(tree, node.index, visit_count, &subtree_size);
    gates_err_t err = pending_reserve(tree, subtree_size);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_i_access_log(tree, GATES_ACCESS_REMOVED, node.index, 0); /* before the slot can be reused */
    gates_i_overlay_node_destroyed(tree, node.index); /* an open overlay ends quietly */
    gates_i_menubar_destroying(tree, node.index);   /* its open menu closes, menu mode ends */
    gates_i_tip_destroying(tree, node.index);       /* a tooltip over it goes */
    gates_i_focus_leave_subtree(tree, node.index); /* before the links go */
    if (gates_i_slot(tree, node.index)->parent != GATES_NONE) {
        link_unlink(tree, node.index);
    }
    for_each_in_subtree(tree, node.index, visit_mark_pending, nullptr);
    gates_i_mark_dirty(tree, GATES_NONE, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_tree_flush_destroys(gates_tree_t *tree) {
    if (tree == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    for (gates_u32 i = 0; i < tree->pending_len; i++) {
        pool_free_slot(tree, tree->pending[i]);
    }
    tree->pending_len = 0;
    return GATES_OK;
}

/* -- introspection ------------------------------------------------------------ */

static gates_node_t link_handle(const gates_tree_t *tree, gates_node_t node,
                                gates_u32 (*pick)(const gates_node_slot_t *)) {
    if (!gates_i_valid(tree, node)) {
        return GATES_NODE_NULL;
    }
    gates_u32 idx = pick(gates_i_slot(tree, node.index));
    return idx == GATES_NONE ? GATES_NODE_NULL : gates_i_handle(tree, idx);
}

static gates_u32 pick_parent(const gates_node_slot_t *s) { return s->parent; }
static gates_u32 pick_first_child(const gates_node_slot_t *s) { return s->first_child; }
static gates_u32 pick_last_child(const gates_node_slot_t *s) { return s->last_child; }
static gates_u32 pick_prev_sibling(const gates_node_slot_t *s) { return s->prev_sibling; }
static gates_u32 pick_next_sibling(const gates_node_slot_t *s) { return s->next_sibling; }

gates_node_t gates_node_parent(const gates_tree_t *tree, gates_node_t node) {
    return link_handle(tree, node, pick_parent);
}

gates_node_t gates_node_first_child(const gates_tree_t *tree, gates_node_t node) {
    return link_handle(tree, node, pick_first_child);
}

gates_node_t gates_node_last_child(const gates_tree_t *tree, gates_node_t node) {
    return link_handle(tree, node, pick_last_child);
}

gates_node_t gates_node_prev_sibling(const gates_tree_t *tree, gates_node_t node) {
    return link_handle(tree, node, pick_prev_sibling);
}

gates_node_t gates_node_next_sibling(const gates_tree_t *tree, gates_node_t node) {
    return link_handle(tree, node, pick_next_sibling);
}

gates_u32 gates_node_child_count(const gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node) ? gates_i_slot(tree, node.index)->child_count : 0;
}

gates_node_kind_t gates_node_kind(const gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node) ? gates_i_slot(tree, node.index)->kind : GATES_NODE_CUSTOM;
}

void *gates_node_user_data(const gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node) ? gates_i_slot(tree, node.index)->user_data : nullptr;
}

gates_u32 gates_tree_live_count(const gates_tree_t *tree) {
    return tree == nullptr ? 0 : tree->live_count;
}

gates_u32 gates_tree_pending_count(const gates_tree_t *tree) {
    return tree == nullptr ? 0 : tree->pending_len;
}

gates_u32 gates_tree_capacity(const gates_tree_t *tree) {
    return tree == nullptr ? 0 : tree->capacity;
}

gates_u32 gates_tree_dirty(const gates_tree_t *tree) {
    return tree == nullptr ? 0 : tree->dirty_bits;
}

void gates_tree_clear_dirty(gates_tree_t *tree, gates_u32 bits) {
    if (tree != nullptr) {
        tree->dirty_bits &= ~bits;
    }
}

gates_node_t gates_tree_focus(const gates_tree_t *tree) {
    if (tree == nullptr || tree->focus == GATES_NONE) {
        return GATES_NODE_NULL;
    }
    return gates_i_handle(tree, tree->focus);
}

void gates_tree_set_focus(gates_tree_t *tree, gates_node_t node) {
    if (tree == nullptr) {
        return;
    }
    gates_u32 idx = gates_i_valid(tree, node) ? node.index : GATES_NONE;
    if (tree->focus == idx) {
        return;
    }
    /* A composition belongs to the box that had focus: leaving drops it
     * uncommitted (the platform adapter completes it first if it wants the
     * text kept, plan-0006). */
    if (tree->focus != GATES_NONE) {
        gates_u32 spin = gates_i_spin_of_box(tree, tree->focus);
        if (spin != GATES_NONE) {
            gates_u32 leaving = tree->focus;
            gates_i_spin_commit(tree, spin); /* plan-0019: leaving commits or reverts */
            if (tree->focus != leaving) return;
        }
        gates_widget_state_t *st =
            gates_i_state(tree, gates_i_slot(tree, tree->focus)->state_index);
        if (st != nullptr && st->edit != nullptr) {
            bool had = gates_text_edit_preedit(st->edit).size > 0;
            gates_i_box_seal(st);
            gates_text_edit_clear_preedit(st->edit);
            st->caret_valid = false;
            if (had) {
                gates_i_event_try_push(tree, tree->focus, GATES_EVENT_PREEDIT_CHANGED, 0);
            }
        }
    }
    /* A Space press belongs to the control that had focus: moving on cancels it. */
    if (tree->key_press != GATES_NONE) {
        if (tree->pressed == tree->key_press) {
            gates_i_mark_dirty(tree, tree->pressed, GATES_DIRTY_PAINT);
            tree->pressed = GATES_NONE;
        }
        tree->key_press = GATES_NONE;
    }
    gates_i_mark_dirty(tree, tree->focus, GATES_DIRTY_PAINT);
    if (tree->focus != GATES_NONE) {
        gates_i_access_log(tree, GATES_ACCESS_FOCUS_LOST, tree->focus, 0);
    }
    tree->focus = idx;
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    if (idx != GATES_NONE) {
        gates_i_access_log(tree, GATES_ACCESS_FOCUS_GAINED, idx, 0);
    }
}

/* -- hidden (plan-0010) ---------------------------------------------------------- */

static bool inside(const gates_tree_t *tree, gates_u32 idx, gates_u32 top) {
    for (gates_u32 a = idx; a != GATES_NONE; a = gates_i_slot(tree, a)->parent) {
        if (a == top) {
            return true;
        }
    }
    return false;
}

gates_err_t gates_node_set_hidden(gates_tree_t *tree, gates_node_t node, bool hidden) {
    if (!gates_i_valid(tree, node) || node.index == tree->root) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_node_slot_t *s = gates_i_slot(tree, node.index);
    if (s->kind == GATES_NODE_DIALOG || s->kind == GATES_NODE_MENU) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (s->hidden == hidden) {
        return GATES_OK;
    }
    s->hidden = hidden;
    if (hidden) {
        /* Let go of whatever the pointer or a key held inside it. */
        if (tree->hover != GATES_NONE && inside(tree, tree->hover, node.index)) {
            tree->hover = GATES_NONE;
        }
        if (tree->pressed != GATES_NONE && inside(tree, tree->pressed, node.index)) {
            tree->pressed = GATES_NONE;
        }
        if (tree->key_press != GATES_NONE && inside(tree, tree->key_press, node.index)) {
            tree->key_press = GATES_NONE;
        }
        if (tree->drag_kind != GATES_DRAG_NONE && inside(tree, tree->drag_node, node.index)) {
            tree->drag_kind = GATES_DRAG_NONE;
            tree->drag_node = GATES_NONE;
        }
    }
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    gates_i_focus_check(tree); /* also closes a choice list inside it */
    return GATES_OK;
}

gates_err_t gates_node_set_font(gates_tree_t *tree, gates_node_t node, gates_i32 font) {
    if (tree == nullptr || !gates_i_valid(tree, node) || font < -1 || font > 1) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_node_slot_t *s = gates_i_slot(tree, node.index);
    if (s->font != (gates_i8)font) {
        s->font = (gates_i8)font;
        gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    }
    return GATES_OK;
}

gates_i32 gates_i_font(const gates_tree_t *tree, gates_u32 idx) {
    for (gates_u32 p = idx; p != GATES_NONE; p = gates_i_slot(tree, p)->parent) {
        gates_i8 f = gates_i_slot(tree, p)->font;
        if (f >= 0) return f;
    }
    return 0; /* GATES_FONT_UI */
}

gates_i32 gates_node_font(const gates_tree_t *tree, gates_node_t node) {
    return tree != nullptr && gates_i_valid(tree, node) ? gates_i_font(tree, node.index) : 0;
}

bool gates_node_hidden(const gates_tree_t *tree, gates_node_t node) {
    return gates_i_valid(tree, node) && gates_i_slot(tree, node.index)->hidden;
}
