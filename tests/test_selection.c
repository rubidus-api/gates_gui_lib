/* the selection store (0.10.0): ranges, requests, rows coming and going,
 * allocation failure. */
#include <gates/view.h>
#include <proven/heap.h>
#include "gates_test.h"

#include <stdio.h>
#include <string.h>

/* "0-2,5,7-9" for the ranges. */
static const char *show(const gates_selection_t *s) {
    static char buf[512];
    buf[0] = '\0';
    for (gates_u32 i = 0; i < gates_selection_range_count(s); i++) {
        gates_u64 lo = 0, hi = 0;
        GT_ASSERT(gates_selection_range(s, i, &lo, &hi));
        char part[64];
        if (lo == hi) snprintf(part, sizeof part, "%s%llu", i > 0 ? "," : "", (unsigned long long)lo);
        else snprintf(part, sizeof part, "%s%llu-%llu", i > 0 ? "," : "", (unsigned long long)lo, (unsigned long long)hi);
        strncat(buf, part, sizeof buf - strlen(buf) - 1);
    }
    return buf;
}

static bool is(const gates_selection_t *s, const char *want) {
    bool ok = strcmp(show(s), want) == 0;
    if (!ok) printf("  have \"%s\", want \"%s\"\n", show(s), want);
    return ok;
}

static void test_ranges(void) {
    gates_selection_t *s = nullptr;
    GT_ASSERT(gates_selection_create((gates_allocator_t){0}, nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_selection_create((gates_allocator_t){0}, &s));
    GT_ASSERT(is(s, "") && gates_selection_count(s) == 0 && gates_selection_next(s, 0) == GATES_ROW_NONE);
    GT_ASSERT(!gates_selection_range(s, 0, nullptr, nullptr));
    /* Adding: kept in order, touching or overlapping ranges join. */
    GT_ASSERT_OK(gates_selection_add(s, 10, 12));
    GT_ASSERT_OK(gates_selection_add(s, 2, 3));
    GT_ASSERT_OK(gates_selection_add(s, 20, 20));
    GT_ASSERT(is(s, "2-3,10-12,20"));
    GT_ASSERT_OK(gates_selection_add(s, 4, 4));   /* touches 2-3 */
    GT_ASSERT(is(s, "2-4,10-12,20"));
    GT_ASSERT_OK(gates_selection_add(s, 9, 9));   /* touches 10-12 from below */
    GT_ASSERT_OK(gates_selection_add(s, 13, 13)); /* and from above */
    GT_ASSERT(is(s, "2-4,9-13,20"));
    GT_ASSERT_OK(gates_selection_add(s, 6, 6));   /* apart */
    GT_ASSERT(is(s, "2-4,6,9-13,20"));
    GT_ASSERT_OK(gates_selection_add(s, 5, 19));  /* swallows three, touches 20 */
    GT_ASSERT(is(s, "2-20"));
    GT_ASSERT_OK(gates_selection_add(s, 0, 0));
    GT_ASSERT(is(s, "0,2-20"));
    GT_ASSERT_OK(gates_selection_add(s, 3, 7));   /* inside: nothing new */
    GT_ASSERT(is(s, "0,2-20"));
    GT_ASSERT(gates_selection_add(s, 5, 4) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_selection_add(s, 0, GATES_ROW_NONE) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_selection_add(nullptr, 0, 1) == PROVEN_ERR_INVALID_ARG);
    /* Questions. */
    GT_ASSERT(gates_selection_contains(s, 0) && !gates_selection_contains(s, 1) && gates_selection_contains(s, 2));
    GT_ASSERT(gates_selection_contains(s, 20) && !gates_selection_contains(s, 21));
    GT_ASSERT(gates_selection_next(s, 0) == 0 && gates_selection_next(s, 1) == 2 && gates_selection_next(s, 7) == 7);
    GT_ASSERT(gates_selection_next(s, 21) == GATES_ROW_NONE);
    GT_ASSERT(gates_selection_count(s) == 20 && gates_selection_range_count(s) == 2);
    gates_u64 lo = 0, hi = 0;
    GT_ASSERT(gates_selection_range(s, 1, &lo, &hi) && lo == 2 && hi == 20);
    GT_ASSERT(gates_selection_range(s, 1, nullptr, nullptr) && !gates_selection_range(s, 2, &lo, &hi));
    GT_ASSERT(!gates_selection_contains(nullptr, 0) && gates_selection_next(nullptr, 0) == GATES_ROW_NONE);
    GT_ASSERT(gates_selection_count(nullptr) == 0 && gates_selection_range_count(nullptr) == 0);
    /* Removing: trims, splits, drops. */
    GT_ASSERT_OK(gates_selection_remove(s, 5, 7));
    GT_ASSERT(is(s, "0,2-4,8-20"));
    GT_ASSERT_OK(gates_selection_remove(s, 1, 1)); /* not selected: nothing */
    GT_ASSERT_OK(gates_selection_remove(s, 30, 40));
    GT_ASSERT(is(s, "0,2-4,8-20"));
    GT_ASSERT_OK(gates_selection_remove(s, 3, 9));  /* the end of one, the start of the next */
    GT_ASSERT(is(s, "0,2,10-20"));
    GT_ASSERT_OK(gates_selection_remove(s, 0, 2));  /* two whole ranges */
    GT_ASSERT(is(s, "10-20"));
    GT_ASSERT_OK(gates_selection_remove(s, 20, 20));
    GT_ASSERT_OK(gates_selection_remove(s, 10, 10));
    GT_ASSERT(is(s, "11-19"));
    GT_ASSERT(gates_selection_remove(s, 2, 1) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_selection_remove(s, 0, GATES_ROW_NONE) == PROVEN_ERR_INVALID_ARG);
    /* Toggling. */
    GT_ASSERT_OK(gates_selection_toggle(s, 15));
    GT_ASSERT(is(s, "11-14,16-19"));
    GT_ASSERT_OK(gates_selection_toggle(s, 15));
    GT_ASSERT(is(s, "11-19"));
    GT_ASSERT_OK(gates_selection_toggle(s, 0));
    GT_ASSERT(is(s, "0,11-19"));
    gates_selection_clear(s);
    GT_ASSERT(is(s, ""));
    gates_selection_clear(nullptr);
    /* Many ranges: the store grows (every other row of 1000). */
    for (gates_u64 r = 0; r < 1000; r += 2) GT_ASSERT_OK(gates_selection_add(s, r, r));
    GT_ASSERT(gates_selection_range_count(s) == 500 && gates_selection_count(s) == 500);
    GT_ASSERT(gates_selection_contains(s, 998) && !gates_selection_contains(s, 999) && gates_selection_next(s, 999) == GATES_ROW_NONE);
    GT_ASSERT(gates_selection_next(s, 501) == 502);
    for (gates_u64 r = 1; r < 1000; r += 2) GT_ASSERT_OK(gates_selection_add(s, r, r));
    GT_ASSERT(is(s, "0-999"));
    /* The largest rows. */
    gates_selection_clear(s);
    GT_ASSERT_OK(gates_selection_add(s, GATES_ROW_NONE - 2, GATES_ROW_NONE - 1));
    GT_ASSERT(gates_selection_contains(s, GATES_ROW_NONE - 1) && gates_selection_count(s) == 2);
    GT_ASSERT_OK(gates_selection_add(s, 0, 0));
    GT_ASSERT(gates_selection_range_count(s) == 2);
    gates_selection_destroy(s);
    gates_selection_destroy(nullptr);
}

static void test_requests(void) {
    gates_selection_t *s = nullptr;
    GT_ASSERT_OK(gates_selection_create((gates_allocator_t){0}, &s));
    GT_ASSERT_OK(gates_selection_apply(s, GATES_SELECT_ONE, 4, GATES_ROW_NONE, 10));
    GT_ASSERT(is(s, "4"));
    GT_ASSERT_OK(gates_selection_apply(s, GATES_SELECT_RANGE, 7, 4, 10));
    GT_ASSERT(is(s, "4-7"));
    GT_ASSERT_OK(gates_selection_apply(s, GATES_SELECT_RANGE, 2, 4, 10)); /* upwards; replaces */
    GT_ASSERT(is(s, "2-4"));
    GT_ASSERT_OK(gates_selection_apply(s, GATES_SELECT_TOGGLE, 3, 3, 10));
    GT_ASSERT(is(s, "2,4"));
    GT_ASSERT_OK(gates_selection_apply(s, GATES_SELECT_ADD_RANGE, 9, 7, 10));
    GT_ASSERT(is(s, "2,4,7-9"));
    GT_ASSERT_OK(gates_selection_apply(s, GATES_SELECT_ADD_RANGE, 5, 99, 10)); /* the anchor is gone: 5 alone */
    GT_ASSERT(is(s, "2,4-5,7-9"));
    GT_ASSERT_OK(gates_selection_apply(s, GATES_SELECT_RANGE, 6, GATES_ROW_NONE, 10));
    GT_ASSERT(is(s, "6"));
    GT_ASSERT_OK(gates_selection_apply(s, GATES_SELECT_ALL, 0, 0, 10));
    GT_ASSERT(is(s, "0-9"));
    GT_ASSERT_OK(gates_selection_apply(s, GATES_SELECT_ALL, 0, 0, 0)); /* no rows: nothing */
    GT_ASSERT(is(s, ""));
    GT_ASSERT_OK(gates_selection_apply(s, GATES_SELECT_ONE, 2, 0, 10));
    GT_ASSERT(gates_selection_apply(s, GATES_SELECT_ONE, 10, 0, 10) == PROVEN_ERR_OUT_OF_BOUNDS);
    GT_ASSERT(gates_selection_apply(s, GATES_SELECT_TOGGLE, 10, 0, 10) == PROVEN_ERR_OUT_OF_BOUNDS);
    GT_ASSERT(gates_selection_apply(s, (gates_select_request_t)0, 1, 0, 10) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_selection_apply(s, (gates_select_request_t)(GATES_SELECT_ALL + 1), 1, 0, 10) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_selection_apply(nullptr, GATES_SELECT_ONE, 1, 0, 10) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(is(s, "2"));
    /* A million rows, all: one range. */
    GT_ASSERT_OK(gates_selection_apply(s, GATES_SELECT_ALL, 0, 0, 1000000));
    GT_ASSERT(gates_selection_range_count(s) == 1 && gates_selection_count(s) == 1000000);
    gates_selection_destroy(s);
}

static void test_rows_moving(void) {
    gates_selection_t *s = nullptr;
    GT_ASSERT_OK(gates_selection_create((gates_allocator_t){0}, &s));
    GT_ASSERT_OK(gates_selection_add(s, 2, 4));
    GT_ASSERT_OK(gates_selection_add(s, 8, 9));
    /* Inserted rows move later ones down; inside a range they split it, unselected. */
    GT_ASSERT_OK(gates_selection_rows_inserted(s, 0, 1));
    GT_ASSERT(is(s, "3-5,9-10"));
    GT_ASSERT_OK(gates_selection_rows_inserted(s, 4, 2));
    GT_ASSERT(is(s, "3,6-7,11-12"));
    GT_ASSERT_OK(gates_selection_rows_inserted(s, 6, 1)); /* at a range's first row: all of it moves */
    GT_ASSERT(is(s, "3,7-8,12-13"));
    GT_ASSERT_OK(gates_selection_rows_inserted(s, 9, 3)); /* just after a range: not inside */
    GT_ASSERT(is(s, "3,7-8,15-16"));
    GT_ASSERT_OK(gates_selection_rows_inserted(s, 100, 5)); /* after everything */
    GT_ASSERT_OK(gates_selection_rows_inserted(s, 0, 0));
    GT_ASSERT(is(s, "3,7-8,15-16"));
    /* Removed rows: trimmed, moved up; ranges that meet join. */
    GT_ASSERT_OK(gates_selection_rows_removed(s, 4, 3)); /* the gap between 3 and 7-8 */
    GT_ASSERT(is(s, "3-5,12-13"));
    GT_ASSERT_OK(gates_selection_rows_removed(s, 5, 7));  /* the end of one, the gap: 12-13 meets it */
    GT_ASSERT(is(s, "3-6"));
    GT_ASSERT_OK(gates_selection_rows_removed(s, 4, 1));  /* the middle */
    GT_ASSERT(is(s, "3-5"));
    GT_ASSERT_OK(gates_selection_rows_removed(s, 0, 4));  /* the start */
    GT_ASSERT(is(s, "0-1"));
    GT_ASSERT_OK(gates_selection_rows_removed(s, 10, 4)); /* after: nothing */
    GT_ASSERT_OK(gates_selection_rows_removed(s, 0, 0));
    GT_ASSERT(is(s, "0-1"));
    GT_ASSERT_OK(gates_selection_rows_removed(s, 0, 2));  /* all of it */
    GT_ASSERT(is(s, ""));
    GT_ASSERT_OK(gates_selection_add(s, 5, 6));
    GT_ASSERT_OK(gates_selection_rows_removed(s, 3, GATES_ROW_NONE)); /* to the end */
    GT_ASSERT(is(s, ""));
    GT_ASSERT_OK(gates_selection_add(s, 5, 6));
    GT_ASSERT_OK(gates_selection_rows_removed(s, 6, 1));
    GT_ASSERT(is(s, "5"));
    /* Rows cannot move past the last row there can be. */
    GT_ASSERT_OK(gates_selection_add(s, GATES_ROW_NONE - 3, GATES_ROW_NONE - 2));
    GT_ASSERT(gates_selection_rows_inserted(s, 0, 2) == PROVEN_ERR_OVERFLOW);
    GT_ASSERT_OK(gates_selection_rows_inserted(s, 0, 1));
    GT_ASSERT(gates_selection_contains(s, 6) && gates_selection_contains(s, GATES_ROW_NONE - 1));
    GT_ASSERT(gates_selection_rows_inserted(s, 0, GATES_ROW_NONE) == PROVEN_ERR_OVERFLOW);
    GT_ASSERT(gates_selection_rows_inserted(nullptr, 0, 1) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_selection_rows_removed(nullptr, 0, 1) == PROVEN_ERR_INVALID_ARG);
    gates_selection_destroy(s);
}

/* -- allocation failure ------------------------------------------------------------------- */

typedef struct fail_alloc_t {
    gates_allocator_t inner;
    bool fail;
} fail_alloc_t;

static proven_result_mem_mut_t fa_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->fail) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    return f->inner.alloc_fn(f->inner.ctx, size, align);
}

static proven_result_mem_mut_t fa_realloc(void *ctx, void *p, proven_size_t os, proven_size_t ns, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->fail) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    return f->inner.realloc_fn(f->inner.ctx, p, os, ns, align);
}

static void fa_free(void *ctx, void *p) {
    fail_alloc_t *f = ctx;
    f->inner.free_fn(f->inner.ctx, p);
}

static void test_allocation_failure(void) {
    fail_alloc_t fa = { .inner = proven_heap_allocator(), .fail = true };
    gates_allocator_t alloc = { .ctx = &fa, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc, .free_fn = fa_free };
    gates_selection_t *s = (gates_selection_t *)&fa;
    GT_ASSERT(gates_selection_create(alloc, &s) == PROVEN_ERR_NOMEM && s == nullptr);
    fa.fail = false;
    GT_ASSERT_OK(gates_selection_create(alloc, &s));
    fa.fail = true;
    /* Nothing to grow into: every change that needs room is refused, nothing changes. */
    GT_ASSERT(gates_selection_add(s, 1, 2) == PROVEN_ERR_NOMEM && is(s, ""));
    GT_ASSERT(gates_selection_apply(s, GATES_SELECT_ONE, 1, 0, 10) == PROVEN_ERR_NOMEM);
    GT_ASSERT(gates_selection_apply(s, GATES_SELECT_ALL, 1, 0, 10) == PROVEN_ERR_NOMEM && is(s, ""));
    fa.fail = false;
    for (gates_u64 r = 0; r < 16; r += 2) GT_ASSERT_OK(gates_selection_add(s, r, r)); /* 8 ranges: full */
    fa.fail = true;
    GT_ASSERT(gates_selection_add(s, 20, 20) == PROVEN_ERR_NOMEM);           /* a ninth */
    GT_ASSERT(gates_selection_toggle(s, 21) == PROVEN_ERR_NOMEM);
    GT_ASSERT(is(s, "0,2,4,6,8,10,12,14"));
    GT_ASSERT_OK(gates_selection_add(s, 1, 1));                             /* joins: no room needed */
    GT_ASSERT(is(s, "0-2,4,6,8,10,12,14"));
    GT_ASSERT_OK(gates_selection_add(s, 16, 16));                           /* the slot it freed */
    GT_ASSERT(gates_selection_remove(s, 1, 1) == PROVEN_ERR_NOMEM);          /* splits into a ninth */
    GT_ASSERT(gates_selection_rows_inserted(s, 1, 1) == PROVEN_ERR_NOMEM);   /* so does this */
    GT_ASSERT(is(s, "0-2,4,6,8,10,12,14,16"));
    GT_ASSERT_OK(gates_selection_remove(s, 0, 0));                          /* trims */
    GT_ASSERT_OK(gates_selection_remove(s, 16, 16));                        /* drops */
    GT_ASSERT_OK(gates_selection_rows_inserted(s, 3, 1));                   /* between ranges */
    GT_ASSERT(is(s, "1-2,5,7,9,11,13,15"));
    GT_ASSERT_OK(gates_selection_rows_removed(s, 0, 3));
    GT_ASSERT(is(s, "2,4,6,8,10,12"));
    GT_ASSERT_OK(gates_selection_apply(s, GATES_SELECT_ALL, 0, 0, 5));       /* room for one is there */
    GT_ASSERT(is(s, "0-4"));
    fa.fail = false;
    gates_selection_destroy(s);
}

int main(void) {
    test_ranges();
    test_requests();
    test_rows_moving();
    test_allocation_failure();
    return gt_report("test_selection");
}
