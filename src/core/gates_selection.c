/* gates_gui_lib - a selection kept as ranges of rows (0.10.0): the store a
 * multi-select view's program can use instead of writing its own. Sorted,
 * separate, non-touching inclusive ranges; lookups are binary searches.
 * Every change either happens whole or (NOMEM) not at all. Platform-free. */
#include <gates/view.h>
#include "gates_tree_internal.h"
#include <proven/heap.h>

struct gates_selection {
    gates_allocator_t alloc;
    gates_u64 *lo;
    gates_u64 *hi;
    gates_u32 n;
    gates_u32 cap;
};

gates_err_t gates_selection_create(gates_allocator_t alloc, gates_selection_t **out) {
    if (out == nullptr) return PROVEN_ERR_INVALID_ARG;
    *out = nullptr;
    gates_allocator_t a = proven_alloc_is_valid(alloc) ? alloc : proven_heap_allocator();
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, sizeof(gates_selection_t), alignof(gates_selection_t));
    if (!proven_is_ok(r.err)) return r.err;
    *out = (gates_selection_t *)r.value.ptr;
    **out = (gates_selection_t){ .alloc = a };
    return GATES_OK;
}

void gates_selection_destroy(gates_selection_t *sel) {
    if (sel == nullptr) return;
    gates_allocator_t a = sel->alloc;
    if (sel->lo != nullptr) a.free_fn(a.ctx, sel->lo);
    if (sel->hi != nullptr) a.free_fn(a.ctx, sel->hi);
    a.free_fn(a.ctx, sel);
}

/* Room for `want` ranges (doubling). */
static gates_err_t reserve(gates_selection_t *s, gates_u32 want) {
    if (want <= s->cap) return GATES_OK;
    gates_u32 cap = s->cap == 0 ? 8u : s->cap;
    while (cap < want) {
        if (cap > UINT32_MAX / 2u) return PROVEN_ERR_OVERFLOW;
        cap *= 2u;
    }
    gates_allocator_t a = s->alloc;
    proven_result_mem_mut_t rl = a.alloc_fn(a.ctx, (gates_usize_t)cap * sizeof(gates_u64), alignof(gates_u64));
    if (!proven_is_ok(rl.err)) return rl.err;
    proven_result_mem_mut_t rh = a.alloc_fn(a.ctx, (gates_usize_t)cap * sizeof(gates_u64), alignof(gates_u64));
    if (!proven_is_ok(rh.err)) {
        a.free_fn(a.ctx, rl.value.ptr);
        return rh.err;
    }
    gates_u64 *lo = (gates_u64 *)rl.value.ptr, *hi = (gates_u64 *)rh.value.ptr;
    for (gates_u32 i = 0; i < s->n; i++) {
        lo[i] = s->lo[i];
        hi[i] = s->hi[i];
    }
    if (s->lo != nullptr) a.free_fn(a.ctx, s->lo);
    if (s->hi != nullptr) a.free_fn(a.ctx, s->hi);
    s->lo = lo;
    s->hi = hi;
    s->cap = cap;
    return GATES_OK;
}

/* The first range whose end is at or after `row` (n when none). */
static gates_u32 first_ending_at_or_after(const gates_selection_t *s, gates_u64 row) {
    gates_u32 a = 0, b = s->n;
    while (a < b) {
        gates_u32 m = a + (b - a) / 2u;
        if (s->hi[m] < row) a = m + 1u;
        else b = m;
    }
    return a;
}

/* Replaces ranges [from, to) with `count` new ones; room is reserved. */
static void splice(gates_selection_t *s, gates_u32 from, gates_u32 to, const gates_u64 *lo, const gates_u64 *hi,
                   gates_u32 count) {
    gates_u32 tail = s->n - to;
    gates_u32 at = from + count;
    if (at < to) {
        for (gates_u32 i = 0; i < tail; i++) {
            s->lo[at + i] = s->lo[to + i];
            s->hi[at + i] = s->hi[to + i];
        }
    } else if (at > to) {
        for (gates_u32 i = tail; i-- > 0;) {
            s->lo[at + i] = s->lo[to + i];
            s->hi[at + i] = s->hi[to + i];
        }
    }
    for (gates_u32 i = 0; i < count; i++) {
        s->lo[from + i] = lo[i];
        s->hi[from + i] = hi[i];
    }
    s->n = s->n - (to - from) + count;
}

void gates_selection_clear(gates_selection_t *sel) {
    if (sel != nullptr) sel->n = 0;
}

gates_err_t gates_selection_add(gates_selection_t *sel, gates_u64 lo, gates_u64 hi) {
    if (sel == nullptr || lo > hi || hi == GATES_ROW_NONE) return PROVEN_ERR_INVALID_ARG;
    /* Ranges that overlap or touch [lo, hi] merge with it. */
    gates_u32 from = first_ending_at_or_after(sel, lo == 0 ? 0 : lo - 1);
    gates_u32 to = from;
    while (to < sel->n && sel->lo[to] <= hi + 1) to++;
    if (from == to) {
        gates_err_t err = reserve(sel, sel->n + 1u);
        if (!gates_is_ok(err)) return err;
    } else {
        if (sel->lo[from] < lo) lo = sel->lo[from];
        if (sel->hi[to - 1] > hi) hi = sel->hi[to - 1];
    }
    splice(sel, from, to, &lo, &hi, 1);
    return GATES_OK;
}

gates_err_t gates_selection_remove(gates_selection_t *sel, gates_u64 lo, gates_u64 hi) {
    if (sel == nullptr || lo > hi || hi == GATES_ROW_NONE) return PROVEN_ERR_INVALID_ARG;
    gates_u32 from = first_ending_at_or_after(sel, lo);
    gates_u32 to = from;
    while (to < sel->n && sel->lo[to] <= hi) to++;
    if (from == to) return GATES_OK;
    /* What is left of the first and the last range touched. */
    gates_u64 keep_lo[2], keep_hi[2];
    gates_u32 keep = 0;
    if (sel->lo[from] < lo) {
        keep_lo[keep] = sel->lo[from];
        keep_hi[keep++] = lo - 1;
    }
    if (sel->hi[to - 1] > hi) {
        keep_lo[keep] = hi + 1;
        keep_hi[keep++] = sel->hi[to - 1];
    }
    if (keep == 2 && to - from == 1) {
        gates_err_t err = reserve(sel, sel->n + 1u); /* one range split in two */
        if (!gates_is_ok(err)) return err;
    }
    splice(sel, from, to, keep_lo, keep_hi, keep);
    return GATES_OK;
}

bool gates_selection_contains(const gates_selection_t *sel, gates_u64 row) {
    if (sel == nullptr) return false;
    gates_u32 i = first_ending_at_or_after(sel, row);
    return i < sel->n && sel->lo[i] <= row;
}

gates_err_t gates_selection_toggle(gates_selection_t *sel, gates_u64 row) {
    return gates_selection_contains(sel, row) ? gates_selection_remove(sel, row, row)
                                              : gates_selection_add(sel, row, row);
}

gates_u64 gates_selection_next(const gates_selection_t *sel, gates_u64 row) {
    if (sel == nullptr) return GATES_ROW_NONE;
    gates_u32 i = first_ending_at_or_after(sel, row);
    if (i == sel->n) return GATES_ROW_NONE;
    return sel->lo[i] > row ? sel->lo[i] : row;
}

gates_u64 gates_selection_count(const gates_selection_t *sel) {
    gates_u64 n = 0;
    for (gates_u32 i = 0; sel != nullptr && i < sel->n; i++) n += sel->hi[i] - sel->lo[i] + 1u;
    return n;
}

gates_u32 gates_selection_range_count(const gates_selection_t *sel) {
    return sel != nullptr ? sel->n : 0;
}

bool gates_selection_range(const gates_selection_t *sel, gates_u32 index, gates_u64 *lo, gates_u64 *hi) {
    if (sel == nullptr || index >= sel->n) return false;
    if (lo != nullptr) *lo = sel->lo[index];
    if (hi != nullptr) *hi = sel->hi[index];
    return true;
}

gates_err_t gates_selection_apply(gates_selection_t *sel, gates_select_request_t what, gates_u64 target,
                                  gates_u64 anchor, gates_u64 row_count) {
    if (sel == nullptr || what < GATES_SELECT_ONE || what > GATES_SELECT_ALL) return PROVEN_ERR_INVALID_ARG;
    if (what == GATES_SELECT_ALL) {
        if (row_count == 0) {
            sel->n = 0;
            return GATES_OK;
        }
        gates_err_t err = reserve(sel, 1);
        if (!gates_is_ok(err)) return err;
        sel->n = 0;
        return gates_selection_add(sel, 0, row_count - 1u);
    }
    if (target >= row_count) return PROVEN_ERR_OUT_OF_BOUNDS;
    if (anchor >= row_count) anchor = target; /* an anchor that is gone: the target alone */
    gates_u64 lo = anchor < target ? anchor : target, hi = anchor < target ? target : anchor;
    switch (what) {
    case GATES_SELECT_TOGGLE:
        return gates_selection_toggle(sel, target);
    case GATES_SELECT_ADD_RANGE:
        return gates_selection_add(sel, lo, hi);
    default: { /* ONE and RANGE replace everything */
        gates_err_t err = reserve(sel, 1);
        if (!gates_is_ok(err)) return err;
        sel->n = 0;
        return what == GATES_SELECT_ONE ? gates_selection_add(sel, target, target) : gates_selection_add(sel, lo, hi);
    }
    }
}

gates_err_t gates_selection_rows_inserted(gates_selection_t *sel, gates_u64 at, gates_u64 count) {
    if (sel == nullptr) return PROVEN_ERR_INVALID_ARG;
    if (count == 0) return GATES_OK;
    gates_u32 i = first_ending_at_or_after(sel, at);
    if (i < sel->n && (count >= GATES_ROW_NONE || sel->hi[sel->n - 1] >= GATES_ROW_NONE - count)) {
        return PROVEN_ERR_OVERFLOW; /* rows past the last one there can be */
    }
    bool split = i < sel->n && sel->lo[i] < at; /* the new rows land inside a range: not selected */
    if (split) {
        gates_err_t err = reserve(sel, sel->n + 1u);
        if (!gates_is_ok(err)) return err;
        gates_u64 lo = at + count, hi = sel->hi[i] + count;
        sel->hi[i] = at - 1u;
        splice(sel, i + 1u, i + 1u, &lo, &hi, 1);
        i += 2u;
    }
    for (; i < sel->n; i++) {
        sel->lo[i] += count;
        sel->hi[i] += count;
    }
    return GATES_OK;
}

gates_err_t gates_selection_rows_removed(gates_selection_t *sel, gates_u64 at, gates_u64 count) {
    if (sel == nullptr) return PROVEN_ERR_INVALID_ARG;
    if (count == 0) return GATES_OK;
    gates_u64 end = at + count - 1u; /* the last removed row */
    if (end < at) end = GATES_ROW_NONE - 1u;
    /* Drop the removed rows (removal never needs room: a range cut in the
     * middle closes up into one), then move the later rows up. */
    gates_u32 w = 0;
    for (gates_u32 r = 0; r < sel->n; r++) {
        gates_u64 lo = sel->lo[r], hi = sel->hi[r];
        if (hi < at) {
            /* before: unchanged */
        } else if (lo > end) {
            lo -= count;
            hi -= count;
        } else {
            gates_u64 kept = (lo < at ? at - lo : 0) + (hi > end ? hi - end : 0);
            if (kept == 0) continue;
            lo = lo < at ? lo : at;
            hi = lo + kept - 1u;
        }
        if (w > 0 && sel->hi[w - 1] + 1u >= lo) { /* now touching the one before */
            if (hi > sel->hi[w - 1]) sel->hi[w - 1] = hi;
            continue;
        }
        sel->lo[w] = lo;
        sel->hi[w] = hi;
        w++;
    }
    sel->n = w;
    return GATES_OK;
}

/* -- a view's requests -------------------------------------------------------------------- */

gates_err_t gates_view_apply_selection(gates_tree_t *tree, const gates_event_t *ev, gates_selection_t *sel) {
    if (tree == nullptr || ev == nullptr || sel == nullptr || ev->kind != GATES_EVENT_SELECT_REQUESTED) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_u64 count = 0, target = 0, anchor = GATES_ROW_NONE;
    if (!gates_i_view_rows_of(tree, ev->source, ev->item, ev->anchor, &count, &target, &anchor)) {
        return PROVEN_ERR_NOT_FOUND; /* the view or its target row is gone: nothing changes */
    }
    gates_err_t err = gates_selection_apply(sel, (gates_select_request_t)ev->result, target, anchor, count);
    if (!gates_is_ok(err)) return err;
    return gates_view_model_changed(tree, ev->source);
}
