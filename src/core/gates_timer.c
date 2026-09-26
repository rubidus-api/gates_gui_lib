/* gates_gui_lib - UI timers: a per-tree registry of due times; the platform
 * supplies the clock and re-arms one timer for the next due time (plan-0012,
 * RFC-0003 9). Platform-free. */
#include <gates/timer.h>
#include "gates_tree_internal.h"

#include <string.h>

void gates_i_timers_free(gates_tree_t *tree) {
    if (tree->timers != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, tree->timers);
    }
    tree->timers = nullptr;
    tree->timer_count = 0;
    tree->timer_cap = 0;
}

void gates_tree_set_clock(gates_tree_t *tree, gates_clock_fn now, void (*changed)(void *ctx),
                          void *ctx) {
    if (tree == nullptr) return;
    tree->clock = now;
    tree->clock_changed = changed;
    tree->clock_ctx = ctx;
}

static void notify(gates_tree_t *tree) {
    if (tree->clock_changed != nullptr) {
        tree->clock_changed(tree->clock_ctx);
    }
}

static gates_i_timer_t *find(const gates_tree_t *tree, gates_timer_id_t id) {
    for (gates_u32 i = 0; id != 0 && i < tree->timer_count; i++) {
        if (tree->timers[i].alive && tree->timers[i].id == id) return &tree->timers[i];
    }
    return nullptr;
}

gates_err_t gates_timer_start(gates_tree_t *tree, gates_node_t node, gates_u32 interval_ms,
                              bool repeat, gates_timer_fn fn, void *user, gates_timer_id_t *out_id) {
    if (tree == nullptr || fn == nullptr || out_id == nullptr || interval_ms == 0 ||
        !gates_i_valid(tree, node)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_id = 0;
    if (tree->clock == nullptr) {
        return PROVEN_ERR_INVALID_STATE;
    }
    if (tree->next_timer_id == UINT32_MAX - 1) {
        return PROVEN_ERR_OVERFLOW; /* ids are never reused */
    }
    if (tree->timer_count == tree->timer_cap) {
        gates_u32 cap = tree->timer_cap != 0 ? tree->timer_cap * 2 : 8;
        gates_allocator_t a = tree->alloc;
        proven_result_mem_mut_t r =
            tree->timers == nullptr
                ? a.alloc_fn(a.ctx, cap * sizeof(gates_i_timer_t), alignof(gates_i_timer_t))
                : a.realloc_fn(a.ctx, tree->timers, tree->timer_cap * sizeof(gates_i_timer_t),
                               cap * sizeof(gates_i_timer_t), alignof(gates_i_timer_t));
        if (!proven_is_ok(r.err)) {
            return r.err;
        }
        tree->timers = (gates_i_timer_t *)r.value.ptr;
        tree->timer_cap = cap;
    }
    gates_timer_id_t id = ++tree->next_timer_id;
    tree->timers[tree->timer_count++] = (gates_i_timer_t){
        .id = id,
        .index = node.index,
        .generation = node.generation,
        .interval = interval_ms,
        .repeat = repeat,
        .alive = true,
        .due = tree->clock(tree->clock_ctx) + interval_ms,
        .fn = fn,
        .user = user,
    };
    *out_id = id;
    notify(tree);
    return GATES_OK;
}

gates_err_t gates_timer_cancel(gates_tree_t *tree, gates_timer_id_t id) {
    if (tree == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_i_timer_t *t = find(tree, id);
    if (t == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    t->alive = false; /* reclaimed after the current run, if any */
    notify(tree);
    return GATES_OK;
}

bool gates_timer_active(const gates_tree_t *tree, gates_timer_id_t id) {
    return tree != nullptr && find(tree, id) != nullptr;
}

static gates_u32 until(gates_u64 due, gates_u64 now) {
    if (due <= now) return 0;
    gates_u64 d = due - now;
    return d >= GATES_TIMER_NONE ? GATES_TIMER_NONE - 1 : (gates_u32)d;
}

gates_u32 gates_tree_next_timer(const gates_tree_t *tree) {
    if (tree == nullptr || tree->clock == nullptr) return GATES_TIMER_NONE;
    gates_u64 now = tree->clock(tree->clock_ctx);
    gates_u32 best = GATES_TIMER_NONE;
    for (gates_u32 i = 0; i < tree->timer_count; i++) {
        const gates_i_timer_t *t = &tree->timers[i];
        if (!t->alive) continue;
        gates_u32 u = until(t->due, now);
        if (u < best) best = u;
    }
    return best;
}

gates_u32 gates_tree_timer_count(const gates_tree_t *tree) {
    gates_u32 n = 0;
    for (gates_u32 i = 0; tree != nullptr && i < tree->timer_count; i++) {
        n += tree->timers[i].alive ? 1u : 0u;
    }
    return n;
}

gates_u32 gates_tree_run_timers(gates_tree_t *tree) {
    if (tree == nullptr || tree->clock == nullptr) return GATES_TIMER_NONE;
    /* `now` is read once: a timer started by a callback is due at clock + interval
     * (interval >= 1), later than `now`, so it can never fire in this run. */
    gates_u64 now = tree->clock(tree->clock_ctx);
    /* By index, in start order; callbacks may grow the array or cancel entries. */
    for (gates_u32 i = 0; i < tree->timer_count; i++) {
        gates_i_timer_t *t = &tree->timers[i];
        if (!t->alive || t->due > now) continue;
        gates_node_t node = { .index = t->index, .generation = t->generation };
        if (!gates_i_valid(tree, node)) {
            t->alive = false; /* its node is gone: cancelled */
            continue;
        }
        gates_timer_id_t id = t->id;
        gates_timer_fn fn = t->fn;
        void *user = t->user;
        if (t->repeat) {
            t->due = now + t->interval; /* late: once, then from now */
        } else {
            t->alive = false;
        }
        fn(tree, node, id, user); /* `t` may be stale after this */
    }
    /* Reclaim cancelled and finished entries, keeping start order. */
    gates_u32 out = 0;
    for (gates_u32 i = 0; i < tree->timer_count; i++) {
        if (tree->timers[i].alive) tree->timers[out++] = tree->timers[i];
    }
    tree->timer_count = out;
    return gates_tree_next_timer(tree);
}
