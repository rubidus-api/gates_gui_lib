/* gates_gui_lib - typed change notifications (RFC-0003 section 4.1, plan-0007).
 * Queue on the tree, payload read from the widget at delivery, storage reserved
 * before an edit commits so a successful edit never loses its event. */
#include <gates/event.h>
#include <gates/ui.h>
#include "gates_tree_internal.h"

#include <string.h>

static gates_widget_state_t *state_at(const gates_tree_t *tree, gates_u32 idx) {
    return gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
}

gates_err_t gates_i_event_reserve(gates_tree_t *tree, gates_u32 slots, gates_u32 text_bytes) {
    gates_allocator_t a = tree->alloc;
    gates_u32 need = tree->event_len + slots;
    if (need > tree->event_cap) {
        gates_u32 cap = tree->event_cap == 0 ? 8u : tree->event_cap;
        while (cap < need) {
            cap *= 2u;
        }
        proven_result_mem_mut_t res =
            tree->events == nullptr
                ? a.alloc_fn(a.ctx, (gates_usize_t)cap * sizeof(gates_i_event_t),
                             alignof(gates_i_event_t))
                : a.realloc_fn(a.ctx, tree->events,
                               (gates_usize_t)tree->event_cap * sizeof(gates_i_event_t),
                               (gates_usize_t)cap * sizeof(gates_i_event_t),
                               alignof(gates_i_event_t));
        if (!proven_is_ok(res.err)) {
            return res.err;
        }
        tree->events = (gates_i_event_t *)res.value.ptr;
        tree->event_cap = cap;
    }
    if (text_bytes > tree->event_text_cap) {
        gates_u32 cap = tree->event_text_cap == 0 ? 64u : tree->event_text_cap;
        while (cap < text_bytes) {
            if (cap > UINT32_MAX / 2u) {
                return PROVEN_ERR_OVERFLOW;
            }
            cap *= 2u;
        }
        /* The contents never need preserving: the buffer is refilled per event. */
        proven_result_mem_mut_t res = a.alloc_fn(a.ctx, cap, 1);
        if (!proven_is_ok(res.err)) {
            return res.err;
        }
        if (tree->event_text != nullptr) {
            if (tree->event_text == tree->event_text_busy) {
                /* A handler is reading the old block: keep it until it returns.
                 * Only the block being read is ever parked here. */
                tree->event_text_retired = tree->event_text;
            } else {
                a.free_fn(a.ctx, tree->event_text);
            }
        }
        tree->event_text = (gates_u8 *)res.value.ptr;
        tree->event_text_cap = cap;
    }
    return GATES_OK;
}

static bool coalesces(gates_event_kind_t kind) {
    return kind == GATES_EVENT_TEXT_CHANGED || kind == GATES_EVENT_PREEDIT_CHANGED ||
           kind == GATES_EVENT_VALUE_CHANGED || kind == GATES_EVENT_LIMIT_EXCEEDED ||
           kind == GATES_EVENT_SELECTION_CHANGED;
}

/* -- bubbling (plan-0019) -------------------------------------------------------- */

static gates_i_bubble *bubble_of(const gates_tree_t *tree, gates_u32 idx) {
    gates_u32 gen = gates_i_slot(tree, idx)->generation;
    for (gates_u32 i = 0; i < tree->bubble_count; i++) {
        gates_i_bubble *b = &tree->bubbles[i];
        if (b->fn != nullptr && b->index == idx && b->generation == gen) return b;
    }
    return nullptr;
}

bool gates_i_handler(const gates_tree_t *tree, gates_u32 idx, gates_event_fn *fn, void **user) {
    const gates_widget_state_t *st = state_at(tree, idx);
    if (st != nullptr && st->on_event != nullptr) {
        if (fn != nullptr) *fn = st->on_event;
        if (user != nullptr) *user = st->event_user;
        return true;
    }
    if (tree->bubble_count == 0) return false;
    if (gates_i_view_of_editor(tree, idx) != GATES_NONE) return false; /* the view's own part (plan-0021) */
    for (gates_u32 a = gates_i_slot(tree, idx)->parent; a != GATES_NONE; a = gates_i_slot(tree, a)->parent) {
        const gates_i_bubble *b = bubble_of(tree, a);
        if (b != nullptr) {
            if (fn != nullptr) *fn = b->fn;
            if (user != nullptr) *user = b->user;
            return true;
        }
    }
    return false;
}

gates_err_t gates_node_set_bubble_handler(gates_tree_t *tree, gates_node_t node, gates_event_fn fn, void *user) {
    if (tree == nullptr || !gates_i_valid(tree, node)) return PROVEN_ERR_INVALID_ARG;
    gates_i_bubble *b = bubble_of(tree, node.index);
    if (b == nullptr && fn == nullptr) return GATES_OK;
    if (b == nullptr) {
        for (gates_u32 i = 0; i < tree->bubble_count && b == nullptr; i++) {
            gates_i_bubble *q = &tree->bubbles[i];
            gates_node_t n = { .index = q->index, .generation = q->generation };
            if (q->fn == nullptr || !gates_i_valid(tree, n)) b = q; /* reuse a free or dead entry */
        }
    }
    if (b == nullptr) {
        if (tree->bubble_count == tree->bubble_cap) {
            gates_u32 cap = tree->bubble_cap == 0 ? 4u : tree->bubble_cap * 2u;
            gates_allocator_t a = tree->alloc;
            proven_result_mem_mut_t r =
                tree->bubbles == nullptr
                    ? a.alloc_fn(a.ctx, cap * sizeof *tree->bubbles, alignof(gates_i_bubble))
                    : a.realloc_fn(a.ctx, tree->bubbles, tree->bubble_cap * sizeof *tree->bubbles,
                                   cap * sizeof *tree->bubbles, alignof(gates_i_bubble));
            if (!proven_is_ok(r.err)) return r.err;
            tree->bubbles = (gates_i_bubble *)r.value.ptr;
            tree->bubble_cap = cap;
        }
        b = &tree->bubbles[tree->bubble_count++];
    }
    *b = (gates_i_bubble){ .index = node.index, .generation = gates_i_slot(tree, node.index)->generation,
                           .fn = fn, .user = user };
    return GATES_OK;
}

void gates_i_bubble_free(gates_tree_t *tree) {
    gates_allocator_t a = tree->alloc;
    if (tree->bubbles != nullptr) a.free_fn(a.ctx, tree->bubbles);
    if (tree->defers != nullptr) a.free_fn(a.ctx, tree->defers);
    tree->bubbles = nullptr;
    tree->defers = nullptr;
    tree->bubble_count = tree->bubble_cap = tree->defer_count = tree->defer_cap = 0;
}

/* -- deferred calls (plan-0019) ------------------------------------------------------- */

gates_err_t gates_tree_defer(gates_tree_t *tree, gates_u32 key, gates_defer_fn fn, void *user) {
    if (tree == nullptr || fn == nullptr) return PROVEN_ERR_INVALID_ARG;
    for (gates_u32 i = 0; i < tree->defer_count; i++) {
        if (tree->defers[i].key == key) {
            tree->defers[i].fn = fn; /* one call: the latest request's */
            tree->defers[i].user = user;
            return GATES_OK;
        }
    }
    if (tree->defer_count == tree->defer_cap) {
        gates_u32 cap = tree->defer_cap == 0 ? 4u : tree->defer_cap * 2u;
        gates_allocator_t a = tree->alloc;
        proven_result_mem_mut_t r =
            tree->defers == nullptr
                ? a.alloc_fn(a.ctx, cap * sizeof *tree->defers, alignof(gates_i_defer))
                : a.realloc_fn(a.ctx, tree->defers, tree->defer_cap * sizeof *tree->defers,
                               cap * sizeof *tree->defers, alignof(gates_i_defer));
        if (!proven_is_ok(r.err)) return r.err;
        tree->defers = (gates_i_defer *)r.value.ptr;
        tree->defer_cap = cap;
    }
    tree->defers[tree->defer_count++] = (gates_i_defer){ .key = key, .fn = fn, .user = user };
    return GATES_OK;
}

bool gates_tree_cancel_defer(gates_tree_t *tree, gates_u32 key) {
    if (tree == nullptr) return false;
    for (gates_u32 i = 0; i < tree->defer_count; i++) {
        if (tree->defers[i].key == key) {
            memmove(&tree->defers[i], &tree->defers[i + 1], (tree->defer_count - i - 1) * sizeof *tree->defers);
            tree->defer_count--;
            return true;
        }
    }
    return false;
}

/* Runs the calls waiting now, oldest first; calls asked for meanwhile wait. */
static void run_defers(gates_tree_t *tree) {
    gates_u32 n = tree->defer_count;
    for (gates_u32 k = 0; k < n && tree->defer_count > 0; k++) {
        gates_i_defer d = tree->defers[0];
        memmove(&tree->defers[0], &tree->defers[1], (tree->defer_count - 1) * sizeof *tree->defers);
        tree->defer_count--;
        d.fn(tree, d.key, d.user); /* may defer again: that one runs next time */
    }
}

void gates_i_event_push(gates_tree_t *tree, gates_u32 idx, gates_event_kind_t kind,
                        gates_event_origin_t origin) {
    if (!gates_i_wants_events(tree, idx)) {
        return;
    }
    gates_u32 gen = gates_i_slot(tree, idx)->generation;
    if (coalesces(kind)) {
        for (gates_u32 i = tree->event_head; i < tree->event_len; i++) {
            gates_i_event_t *e = &tree->events[i];
            if (e->node_index == idx && e->generation == gen && e->kind == kind &&
                e->origin == origin) {
                return; /* delivery reads the latest state */
            }
        }
    }
    if (tree->event_len >= tree->event_cap) {
        return; /* caller skipped reservation: never write past the reserve */
    }
    tree->events[tree->event_len++] = (gates_i_event_t){
        .node_index = idx, .generation = gen, .kind = (gates_u8)kind, .origin = (gates_u8)origin,
    };
}

void gates_i_event_push_ex(gates_tree_t *tree, gates_u32 idx, gates_event_kind_t kind,
                           gates_event_origin_t origin, gates_u32 aux, gates_u64 item) {
    gates_u32 before = tree->event_len;
    gates_i_event_push(tree, idx, kind, origin);
    if (tree->event_len > before) {
        tree->events[tree->event_len - 1].aux = aux;
        tree->events[tree->event_len - 1].item = item;
    }
}

void gates_i_event_try_push(gates_tree_t *tree, gates_u32 idx, gates_event_kind_t kind,
                            gates_u32 text_bytes) {
    if (!gates_i_wants_events(tree, idx)) {
        return;
    }
    gates_err_t err = gates_i_event_reserve(tree, 1, text_bytes);
    if (!gates_is_ok(err)) {
        tree->input_error = err;
        return;
    }
    gates_i_event_push(tree, idx, kind, GATES_ORIGIN_USER);
}

void gates_i_event_purge(gates_tree_t *tree, gates_u32 idx) {
    if (tree->dispatching) {
        return; /* delivery re-checks the handler and skips these */
    }
    gates_u32 out = 0;
    for (gates_u32 i = 0; i < tree->event_len; i++) {
        if (tree->events[i].node_index != idx) {
            tree->events[out++] = tree->events[i];
        }
    }
    tree->event_len = out;
}

void gates_i_event_free(gates_tree_t *tree) {
    gates_allocator_t a = tree->alloc;
    if (tree->events != nullptr) {
        a.free_fn(a.ctx, tree->events);
    }
    if (tree->event_text != nullptr) {
        a.free_fn(a.ctx, tree->event_text);
    }
    if (tree->event_text_retired != nullptr) {
        a.free_fn(a.ctx, tree->event_text_retired);
    }
    tree->events = nullptr;
    tree->event_text = nullptr;
    tree->event_text_retired = nullptr;
}

/* -- public ------------------------------------------------------------------- */

static bool has_events(gates_node_kind_t kind) {
    return kind == GATES_NODE_TEXTBOX || kind == GATES_NODE_CHECKBOX ||
           kind == GATES_NODE_BUTTON || kind == GATES_NODE_DIALOG || kind == GATES_NODE_MENU ||
           kind == GATES_NODE_RADIO || kind == GATES_NODE_CHOICE || kind == GATES_NODE_VIEW ||
           kind == GATES_NODE_TABS || kind == GATES_NODE_SPIN || kind == GATES_NODE_SLIDER ||
           kind == GATES_NODE_EDITOR ||
           kind == GATES_NODE_GROUP;
}

gates_err_t gates_widget_set_handler(gates_tree_t *tree, gates_node_t node, gates_event_fn fn,
                                     void *user) {
    if (!gates_i_valid(tree, node) || !has_events(gates_i_slot(tree, node.index)->kind)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_widget_state_t *st = state_at(tree, node.index);
    if (st == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (fn == nullptr) {
        gates_i_event_purge(tree, node.index);
    }
    st->on_event = fn;
    st->event_user = fn != nullptr ? user : nullptr;
    return GATES_OK;
}

gates_err_t gates_widget_notify(gates_tree_t *tree, gates_node_t node) {
    if (!gates_i_valid(tree, node)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_node_kind_t kind = gates_i_slot(tree, node.index)->kind;
    gates_widget_state_t *st = state_at(tree, node.index);
    if (!has_events(kind) || st == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (!gates_i_wants_events(tree, node.index)) {
        return GATES_OK;
    }
    gates_event_kind_t ek = kind == GATES_NODE_VIEW      ? GATES_EVENT_SELECTION_CHANGED
                            : kind == GATES_NODE_TEXTBOX ? GATES_EVENT_TEXT_CHANGED
                            : (kind == GATES_NODE_CHECKBOX || kind == GATES_NODE_RADIO ||
                               kind == GATES_NODE_CHOICE)
                                ? GATES_EVENT_VALUE_CHANGED
                                : GATES_EVENT_ACTIVATED;
    gates_u32 text = st->edit != nullptr ? (gates_u32)gates_text_edit_text(st->edit).size : 0;
    gates_err_t err = gates_i_event_reserve(tree, 1, text);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_i_event_push(tree, node.index, ek, GATES_ORIGIN_PROGRAM);
    return GATES_OK;
}

gates_u32 gates_widget_revision(const gates_tree_t *tree, gates_node_t node) {
    if (!gates_i_valid(tree, node)) {
        return 0;
    }
    const gates_widget_state_t *st = state_at(tree, node.index);
    return st != nullptr ? st->revision : 0;
}

gates_u32 gates_tree_pending_events(const gates_tree_t *tree) {
    return tree != nullptr ? tree->event_len - tree->event_head : 0;
}

/* Copies `s` into the payload buffer; false when it cannot (text grown by a
 * path that did not reserve, e.g. the deprecated raw edit pointer). */
static bool payload(gates_tree_t *tree, gates_str_t s, gates_str_t *out) {
    if (s.size > tree->event_text_cap &&
        !gates_is_ok(gates_i_event_reserve(tree, 0, (gates_u32)s.size))) {
        return false;
    }
    if (s.size > 0) {
        memcpy(tree->event_text, s.ptr, s.size);
    }
    *out = (gates_str_t){ .ptr = tree->event_text, .size = s.size };
    return true;
}

gates_u32 gates_tree_dispatch_events(gates_tree_t *tree, gates_u32 max_events) {
    if (tree == nullptr) {
        return 0;
    }
    if (tree->dispatching) {
        return tree->event_len - tree->event_head; /* no recursive delivery */
    }
    tree->dispatching = true;
    gates_u32 n = tree->event_len;
    if (max_events != 0 && max_events < n) {
        n = max_events;
    }
    for (gates_u32 i = 0; i < n; i++) {
        gates_i_event_t e = tree->events[i];
        tree->event_head = i + 1; /* from here on, nested pushes append */
        gates_node_t node = { .index = e.node_index, .generation = e.generation };
        if (!gates_i_valid(tree, node)) {
            continue; /* destroyed (or pending destroy) since it was queued */
        }
        if (e.kind == GATES_I_EVENT_COMMAND) {
            /* Looked up again: it may be gone or disabled by now (RFC-0003 5.2). */
            gates_i_command_t *c = gates_i_command_find(tree, e.node_index, e.generation, e.aux);
            if (c != nullptr && c->enabled && c->invoke != nullptr) {
                gates_command_fn fn = c->invoke;
                void *user = c->user;
                fn(tree, e.aux, user); /* may unregister it or destroy nodes */
            }
            continue;
        }
        gates_widget_state_t *st = state_at(tree, e.node_index);
        if (e.kind == GATES_EVENT_DIALOG_CLOSED || e.kind == GATES_EVENT_MENU_CLOSED) {
            /* Reported once, then the overlay node goes (it was kept for this). */
            if (st != nullptr && st->on_event != nullptr) {
                gates_event_t ev = { .kind = (gates_event_kind_t)e.kind,
                                     .origin = (gates_event_origin_t)e.origin,
                                     .source = node, .revision = st->revision,
                                     .result = e.aux };
                gates_event_fn fn = st->on_event;
                fn(tree, &ev, st->event_user);
            }
            if (gates_i_valid(tree, node)) {
                (void)gates_node_destroy(tree, node);
            }
            continue;
        }
        gates_event_fn fn = nullptr;
        void *user = nullptr;
        if (st == nullptr || !gates_i_handler(tree, e.node_index, &fn, &user)) {
            continue; /* handler removed */
        }
        gates_event_t ev = {
            .kind = (gates_event_kind_t)e.kind,
            .origin = (gates_event_origin_t)e.origin,
            .source = node,
            .revision = st->revision,
            .checked = st->checked,
            .result = e.kind == GATES_EVENT_VALUE_CHANGED
                          ? (st->tabs != nullptr ? gates_i_tabs_selected(tree, node.index) : st->opt_sel)
                      : (e.kind == GATES_EVENT_SORT_REQUESTED || e.kind == GATES_EVENT_EXPAND_REQUESTED ||
                         e.kind == GATES_EVENT_CELL_EDITED) ? e.aux
                                                               : 0,
            /* A selection is read at delivery (latest); an activation keeps its row. */
            .item = e.kind == GATES_EVENT_SELECTION_CHANGED
                        ? (st->editor != nullptr ? gates_i_editor_caret(st) : gates_i_view_selected(st))
                        : e.item,
            .value = st->rng != nullptr ? gates_i_range_value(tree, node.index) : 0,
        };
        if (ev.kind == GATES_EVENT_LIMIT_EXCEEDED) {
            if (st->offer_len == 0) {
                continue; /* the offer was used or discarded before delivery */
            }
            ev.limit = st->max_bytes;
            ev.fit_bytes = st->offer_fit;
            gates_str_t offer = { .ptr = st->offer, .size = st->password ? 0 : st->offer_len };
            if (!payload(tree, offer, &ev.text)) {
                tree->input_error = PROVEN_ERR_NOMEM;
                continue;
            }
        } else if (ev.kind == GATES_EVENT_TEXT_CHANGED && st->password) {
            ev.text = (gates_str_t){0}; /* no plaintext notifications */
        } else if (ev.kind == GATES_EVENT_TEXT_CHANGED && st->edit != nullptr) {
            if (!payload(tree, gates_text_edit_text(st->edit), &ev.text)) {
                tree->input_error = PROVEN_ERR_NOMEM;
                continue;
            }
        } else if (ev.kind == GATES_EVENT_PREEDIT_CHANGED && st->edit != nullptr) {
            if (!payload(tree, gates_text_edit_preedit(st->edit), &ev.text)) {
                tree->input_error = PROVEN_ERR_NOMEM;
                continue;
            }
        }
        tree->event_text_busy = tree->event_text;
        fn(tree, &ev, user); /* may mutate anything, including `st`'s storage */
        tree->event_text_busy = nullptr;
        if (tree->event_text_retired != nullptr) {
            tree->alloc.free_fn(tree->alloc.ctx, tree->event_text_retired);
            tree->event_text_retired = nullptr;
        }
    }
    /* Drop the delivered prefix; nested events move to the front. */
    gates_u32 rest = tree->event_len - n;
    if (rest > 0) {
        memmove(tree->events, tree->events + n, (gates_usize_t)rest * sizeof(gates_i_event_t));
    }
    tree->event_len = rest;
    tree->event_head = 0;
    if (rest == 0) {
        run_defers(tree); /* after the events: the safe point (plan-0019) */
    }
    tree->dispatching = false;
    return rest + tree->defer_count;
}
