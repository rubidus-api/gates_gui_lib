/* Minimal deterministic test runner for gates_gui_lib host tests. */
#ifndef GATES_TEST_H
#define GATES_TEST_H

#include <stdio.h>

static int gt_pass = 0;
static int gt_fail = 0;

/* Failures flush immediately: a later crash must not swallow the diagnosis. */
#define GT_ASSERT(cond) \
    do { \
        if (cond) { gt_pass++; } \
        else { \
            gt_fail++; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fflush(stdout); \
        } \
    } while (0)

#define GT_ASSERT_OK(expr)  GT_ASSERT(gates_is_ok(expr))
#define GT_ASSERT_ERR(expr) GT_ASSERT(!gates_is_ok(expr))

static inline int gt_report(const char *suite) {
    printf("%s: %d passed, %d failed\n", suite, gt_pass, gt_fail);
    return gt_fail ? 1 : 0;
}

#endif /* GATES_TEST_H */
