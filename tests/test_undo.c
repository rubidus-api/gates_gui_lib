/* the undo stack (0.6.0) - push, undo, redo, labels,
 * merge runs, the bound, the clean mark, refusals and data dropping, and
 * two commands kept in step. */
#include <gates/undo.h>
#include <gates/command.h>
#include <gates/event.h>
#include <gates/widget.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <string.h>

/* The program's data: one number; each entry holds the values before and after. */
static int value;
static int drops;
static int fail_next;

typedef struct change_t {
    int before, after;
} change_t;

static change_t pool[256];
static int pool_used;

static change_t *change(int before, int after) {
    change_t *c = &pool[pool_used++ % 256];
    *c = (change_t){ before, after };
    return c;
}

static gates_err_t undo_fn(void *d) {
    if (fail_next) { fail_next = 0; return PROVEN_ERR_IO; }
    value = ((change_t *)d)->before;
    return GATES_OK;
}

static gates_err_t redo_fn(void *d) {
    if (fail_next) { fail_next = 0; return PROVEN_ERR_IO; }
    value = ((change_t *)d)->after;
    return GATES_OK;
}

static void drop_fn(void *d) {
    (void)d;
    drops++;
}

/* Sets the value as a person would, and records it. */
static gates_err_t set(gates_undo_t *u, int v, const char *label, gates_u32 merge) {
    change_t *c = change(value, v);
    value = v;
    gates_undo_entry_t e = { .label = { (const gates_u8 *)label, strlen(label) }, .undo = undo_fn, .undo_data = c,
                             .redo = redo_fn, .redo_data = c, .drop = drop_fn, .merge_key = merge };
    return gates_undo_push(u, &e);
}

static bool str_is(gates_str_t s, const char *z) {
    return s.size == strlen(z) && (s.size == 0 || memcmp(s.ptr, z, s.size) == 0);
}

static void test_basics(void) {
    gates_undo_t *u = nullptr;
    GT_ASSERT(gates_undo_create((gates_allocator_t){0}, 0, nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_undo_create((gates_allocator_t){0}, 0, &u));
    value = 0;
    drops = 0;
    GT_ASSERT(!gates_undo_can_undo(u) && !gates_undo_can_redo(u) && gates_undo_is_clean(u));
    GT_ASSERT(gates_undo_undo(u) == PROVEN_ERR_INVALID_STATE && gates_undo_redo(u) == PROVEN_ERR_INVALID_STATE);
    GT_ASSERT(gates_undo_undo_label(u).size == 0 && gates_undo_redo_label(u).size == 0);
    GT_ASSERT_OK(set(u, 1, "One", 0));
    GT_ASSERT_OK(set(u, 2, "Two", 0));
    GT_ASSERT(gates_undo_can_undo(u) && !gates_undo_can_redo(u) && !gates_undo_is_clean(u));
    GT_ASSERT(str_is(gates_undo_undo_label(u), "Two"));
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(value == 1 && str_is(gates_undo_undo_label(u), "One") && str_is(gates_undo_redo_label(u), "Two"));
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(value == 0 && gates_undo_is_clean(u) && !gates_undo_can_undo(u));
    GT_ASSERT_OK(gates_undo_redo(u));
    GT_ASSERT(value == 1 && !gates_undo_is_clean(u));
    /* A new change after an undo drops what could have been redone (its data once). */
    GT_ASSERT(drops == 0);
    GT_ASSERT_OK(set(u, 5, "Five", 0));
    GT_ASSERT(drops == 1 && !gates_undo_can_redo(u));
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(value == 1);
    /* An undo or redo that fails leaves the stack where it was. */
    fail_next = 1;
    GT_ASSERT(gates_undo_redo(u) == PROVEN_ERR_IO);
    GT_ASSERT(value == 1 && gates_undo_can_redo(u) && str_is(gates_undo_redo_label(u), "Five"));
    fail_next = 1;
    GT_ASSERT(gates_undo_undo(u) == PROVEN_ERR_IO);
    GT_ASSERT(value == 1 && str_is(gates_undo_undo_label(u), "One"));
    /* Clear: everything dropped, the present is clean. */
    gates_undo_clear(u);
    GT_ASSERT(drops == 3 && !gates_undo_can_undo(u) && !gates_undo_can_redo(u) && gates_undo_is_clean(u));
    /* Destroy drops what is left. */
    GT_ASSERT_OK(set(u, 7, "Seven", 0));
    gates_undo_destroy(u);
    GT_ASSERT(drops == 4);
    gates_undo_destroy(nullptr);
    /* Null stacks. */
    GT_ASSERT(!gates_undo_can_undo(nullptr) && !gates_undo_is_clean(nullptr));
    GT_ASSERT(gates_undo_undo(nullptr) == PROVEN_ERR_INVALID_ARG && gates_undo_redo(nullptr) == PROVEN_ERR_INVALID_ARG);
    gates_undo_break_merge(nullptr);
    gates_undo_mark_clean(nullptr);
    gates_undo_clear(nullptr);
}

static void test_refusals(void) {
    gates_undo_t *u = nullptr;
    GT_ASSERT_OK(gates_undo_create((gates_allocator_t){0}, 0, &u));
    drops = 0;
    change_t *c = change(0, 1);
    gates_undo_entry_t e = { .label = GATES_STR_INIT("x"), .undo = undo_fn, .undo_data = c, .redo = nullptr,
                             .redo_data = c, .drop = drop_fn };
    GT_ASSERT(gates_undo_push(u, &e) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(drops == 1); /* the data is dropped once (one pointer) */
    e.redo = redo_fn;
    e.undo = nullptr;
    e.redo_data = change(0, 2); /* two pointers: both dropped */
    GT_ASSERT(gates_undo_push(u, &e) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(drops == 3);
    e.undo = undo_fn;
    e.label = (gates_str_t){ .ptr = nullptr, .size = 3 };
    GT_ASSERT(gates_undo_push(u, &e) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_undo_push(nullptr, &e) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_undo_push(u, nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(!gates_undo_can_undo(u));
    gates_undo_destroy(u);
}

static void test_merge(void) {
    gates_undo_t *u = nullptr;
    GT_ASSERT_OK(gates_undo_create((gates_allocator_t){0}, 0, &u));
    value = 0;
    drops = 0;
    /* Typing a, b, c into one field: one entry, from 0 to 3; the data between dropped. */
    GT_ASSERT_OK(set(u, 1, "Typing", 7));
    GT_ASSERT_OK(set(u, 2, "Typing more", 7));
    GT_ASSERT(drops == 0);
    GT_ASSERT_OK(set(u, 3, "Typing", 7));
    GT_ASSERT(drops == 1); /* the middle entry's data: neither the first undo nor the last redo */
    GT_ASSERT(str_is(gates_undo_undo_label(u), "Typing"));
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(value == 0 && !gates_undo_can_undo(u));
    GT_ASSERT_OK(gates_undo_redo(u));
    GT_ASSERT(value == 3);
    /* After an undo or redo a push starts a new entry. */
    GT_ASSERT_OK(set(u, 4, "Typing", 7));
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(value == 3);
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(value == 0);
    GT_ASSERT_OK(gates_undo_redo(u));
    GT_ASSERT_OK(gates_undo_redo(u));
    /* Two keys in a row: two entries. */
    GT_ASSERT_OK(set(u, 40, "K1", 1));
    GT_ASSERT_OK(set(u, 41, "K2", 2));
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(value == 40);
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(value == 4);
    GT_ASSERT_OK(gates_undo_redo(u));
    GT_ASSERT_OK(gates_undo_redo(u));
    GT_ASSERT_OK(set(u, 4, "Back", 0));
    /* Another key, or no key, starts a new entry; break_merge ends a run. */
    GT_ASSERT_OK(set(u, 5, "Other", 8));
    GT_ASSERT_OK(set(u, 6, "Other", 0));
    GT_ASSERT_OK(set(u, 7, "Other", 0));
    GT_ASSERT_OK(set(u, 8, "Run", 9));
    gates_undo_break_merge(u);
    GT_ASSERT_OK(set(u, 9, "Run", 9));
    const int back[] = { 8, 7, 6, 5, 4, 41 }; /* one entry per step: nothing merged */
    for (int i = 0; i < 6; i++) {
        GT_ASSERT_OK(gates_undo_undo(u));
        GT_ASSERT(value == back[i]);
    }
    /* Separate undo and redo data: a merge drops the replaced redo data and the
     * new entry's undo data, and the entry keeps the first undo, the last redo. */
    gates_undo_clear(u);
    value = 0;
    drops = 0;
    change_t *u1 = change(0, 0), *r1 = change(0, 1), *u2 = change(1, 1), *r2 = change(1, 2);
    gates_undo_entry_t e1 = { .undo = undo_fn, .undo_data = u1, .redo = redo_fn, .redo_data = r1, .drop = drop_fn, .merge_key = 5 };
    gates_undo_entry_t e2 = { .undo = undo_fn, .undo_data = u2, .redo = redo_fn, .redo_data = r2, .drop = drop_fn, .merge_key = 5 };
    value = 1;
    GT_ASSERT_OK(gates_undo_push(u, &e1));
    value = 2;
    GT_ASSERT_OK(gates_undo_push(u, &e2));
    GT_ASSERT(drops == 2); /* r1 and u2 */
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(value == 0);
    GT_ASSERT_OK(gates_undo_redo(u));
    GT_ASSERT(value == 2);
    gates_undo_clear(u);
    GT_ASSERT(drops == 4); /* u1 and r2 */
    /* The clean mark ends a run: the saved state stays reachable. */
    gates_undo_clear(u);
    value = 0;
    GT_ASSERT_OK(set(u, 1, "Typing", 7));
    gates_undo_mark_clean(u);
    GT_ASSERT(gates_undo_is_clean(u));
    GT_ASSERT_OK(set(u, 2, "Typing", 7));
    GT_ASSERT(!gates_undo_is_clean(u));
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(value == 1 && gates_undo_is_clean(u));
    gates_undo_destroy(u);
}

static void test_bound_and_clean(void) {
    gates_undo_t *u = nullptr;
    GT_ASSERT_OK(gates_undo_create((gates_allocator_t){0}, 3, &u));
    value = 0;
    drops = 0;
    GT_ASSERT_OK(set(u, 1, "a", 0));
    gates_undo_mark_clean(u); /* saved at 1 */
    GT_ASSERT_OK(set(u, 2, "b", 0));
    GT_ASSERT_OK(set(u, 3, "c", 0));
    GT_ASSERT(drops == 0);
    /* A fourth entry drops the oldest (and the state before it). */
    GT_ASSERT_OK(set(u, 4, "d", 0));
    GT_ASSERT(drops == 1);
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(value == 2);
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(value == 1 && gates_undo_is_clean(u)); /* the saved state is the oldest reachable */
    GT_ASSERT(gates_undo_undo(u) == PROVEN_ERR_INVALID_STATE);
    GT_ASSERT_OK(gates_undo_redo(u));
    GT_ASSERT_OK(set(u, 5, "e", 0)); /* drops the redo tail (c, d) */
    GT_ASSERT(drops == 3);
    GT_ASSERT_OK(set(u, 6, "f", 0)); /* b, e, f: full */
    GT_ASSERT(drops == 3);
    GT_ASSERT_OK(set(u, 7, "g", 0)); /* b goes, and the saved state at 1 with it */
    GT_ASSERT(drops == 4);
    while (gates_undo_can_undo(u)) GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(value == 2 && !gates_undo_is_clean(u)); /* the saved state is gone for good */
    /* A clean mark in a dropped redo tail is gone too. */
    gates_undo_clear(u);
    value = 0;
    GT_ASSERT_OK(set(u, 1, "a", 0));
    GT_ASSERT_OK(set(u, 2, "b", 0));
    gates_undo_mark_clean(u);
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT_OK(set(u, 9, "z", 0));
    GT_ASSERT(!gates_undo_is_clean(u));
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(!gates_undo_is_clean(u) && value == 1);
    /* Clearing makes the present clean again. */
    gates_undo_clear(u);
    GT_ASSERT(gates_undo_is_clean(u));
    gates_undo_destroy(u);
}

/* Undo and redo functions must not call into the stack: BUSY. */
static gates_undo_t *reentry;
static gates_err_t reentry_result;
static gates_err_t undo_reenter(void *d) {
    (void)d;
    reentry_result = gates_undo_undo(reentry);
    gates_undo_entry_t e = { .undo = undo_fn, .redo = redo_fn, .undo_data = change(0, 0), .drop = drop_fn };
    e.redo_data = e.undo_data;
    GT_ASSERT(gates_undo_push(reentry, &e) == PROVEN_ERR_BUSY);
    gates_undo_clear(reentry); /* ignored while busy */
    return GATES_OK;
}

static void test_reentry(void) {
    GT_ASSERT_OK(gates_undo_create((gates_allocator_t){0}, 0, &reentry));
    drops = 0;
    gates_undo_entry_t e = { .label = GATES_STR_INIT("r"), .undo = undo_reenter, .redo = redo_fn,
                             .undo_data = change(0, 1), .drop = drop_fn };
    e.redo_data = e.undo_data;
    GT_ASSERT_OK(gates_undo_push(reentry, &e));
    GT_ASSERT_OK(gates_undo_undo(reentry));
    GT_ASSERT(reentry_result == PROVEN_ERR_BUSY);
    GT_ASSERT(drops == 1); /* the refused push's data */
    GT_ASSERT(gates_undo_can_redo(reentry)); /* not cleared */
    gates_undo_destroy(reentry);
}

/* -- commands kept in step ------------------------------------------------------------ */

static void on_cmd(gates_tree_t *tree, gates_command_id_t id, void *user) {
    (void)tree;
    gates_undo_t *u = user;
    (void)(id == 1 ? gates_undo_undo(u) : gates_undo_redo(u));
}

static void test_bind(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t);
    gates_undo_t *u = nullptr;
    GT_ASSERT_OK(gates_undo_create((gates_allocator_t){0}, 0, &u));
    value = 0;
    gates_command_desc_t cu = { .id = 1, .label = GATES_STR_INIT("Undo"), .enabled = true, .invoke = on_cmd, .user = u };
    gates_command_desc_t cr = { .id = 2, .label = GATES_STR_INIT("Redo"), .enabled = true, .invoke = on_cmd, .user = u };
    GT_ASSERT_OK(gates_command_register(t, root, &cu));
    GT_ASSERT(gates_undo_bind(u, t, root, 1, 2, GATES_STR("Undo"), GATES_STR("Redo")) == PROVEN_ERR_NOT_FOUND);
    GT_ASSERT_OK(gates_command_register(t, root, &cr));
    GT_ASSERT(gates_undo_bind(u, t, root, 1, 2, (gates_str_t){ .size = 2 }, GATES_STR("Redo")) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_undo_bind(nullptr, t, root, 1, 2, GATES_STR("Undo"), GATES_STR("Redo")) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_undo_bind(u, t, root, 1, 2, GATES_STR("Undo"), GATES_STR("Redo")));
    GT_ASSERT(!gates_command_enabled(t, root, 1) && !gates_command_enabled(t, root, 2));
    GT_ASSERT(str_is(gates_command_label(t, root, 1), "Undo") && str_is(gates_command_label(t, root, 2), "Redo"));
    GT_ASSERT_OK(set(u, 1, "Rename", 0));
    GT_ASSERT(gates_command_enabled(t, root, 1) && !gates_command_enabled(t, root, 2));
    GT_ASSERT(str_is(gates_command_label(t, root, 1), "Undo Rename"));
    GT_ASSERT_OK(set(u, 2, "Move", 0));
    GT_ASSERT(str_is(gates_command_label(t, root, 1), "Undo Move"));
    /* The commands run the stack; the labels follow. */
    GT_ASSERT_OK(gates_command_invoke(t, root, 1));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(value == 1);
    GT_ASSERT(str_is(gates_command_label(t, root, 1), "Undo Rename") && str_is(gates_command_label(t, root, 2), "Redo Move"));
    GT_ASSERT(gates_command_enabled(t, root, 2));
    GT_ASSERT_OK(gates_undo_undo(u));
    GT_ASSERT(!gates_command_enabled(t, root, 1) && str_is(gates_command_label(t, root, 1), "Undo"));
    /* A label-less entry shows the word alone; words in another language. */
    GT_ASSERT_OK(gates_undo_bind(u, t, root, 1, 2, GATES_STR("실행 취소"), GATES_STR("다시 실행")));
    GT_ASSERT(str_is(gates_command_label(t, root, 2), "다시 실행 Rename"));
    GT_ASSERT_OK(set(u, 3, "", 0));
    GT_ASSERT(str_is(gates_command_label(t, root, 1), "실행 취소"));
    /* Unbound: the commands are left as they are. */
    GT_ASSERT_OK(gates_undo_bind(u, nullptr, root, 0, 0, (gates_str_t){0}, (gates_str_t){0}));
    GT_ASSERT_OK(set(u, 4, "Later", 0));
    GT_ASSERT(str_is(gates_command_label(t, root, 1), "실행 취소"));
    /* A scope that goes away just stops the updates. */
    gates_node_t panel;
    GT_ASSERT_OK(gates_panel_create(t, root, &panel));
    GT_ASSERT_OK(gates_command_register(t, panel, &cu));
    GT_ASSERT_OK(gates_command_register(t, panel, &cr));
    GT_ASSERT_OK(gates_undo_bind(u, t, panel, 1, 2, GATES_STR("U"), GATES_STR("R")));
    GT_ASSERT(str_is(gates_command_label(t, panel, 1), "U Later"));
    GT_ASSERT_OK(gates_node_destroy(t, panel));
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    GT_ASSERT_OK(set(u, 5, "Gone", 0));
    gates_undo_destroy(u);
    gates_tree_destroy(t);
}

/* -- allocation failures -------------------------------------------------------------- */

typedef struct fail_alloc_t {
    gates_allocator_t inner;
    int left;
} fail_alloc_t;

static proven_result_mem_mut_t fa_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->left == 0) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    if (f->left > 0) f->left--;
    return f->inner.alloc_fn(f->inner.ctx, size, align);
}
static proven_result_mem_mut_t fa_realloc(void *ctx, void *p, proven_size_t os, proven_size_t ns, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->left == 0) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    if (f->left > 0) f->left--;
    return f->inner.realloc_fn(f->inner.ctx, p, os, ns, align);
}
static void fa_free(void *ctx, void *p) {
    fail_alloc_t *f = ctx;
    f->inner.free_fn(f->inner.ctx, p);
}

static void test_failures(void) {
    for (int k = 0; k < 40; k++) {
        fail_alloc_t f = { .inner = proven_heap_allocator(), .left = k };
        gates_allocator_t al = { .ctx = &f, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc, .free_fn = fa_free };
        gates_undo_t *u = nullptr;
        gates_err_t err = gates_undo_create(al, 4, &u);
        if (!gates_is_ok(err)) {
            GT_ASSERT(err == PROVEN_ERR_NOMEM && u == nullptr);
            continue;
        }
        value = 0;
        drops = 0;
        int kept = 0, pushes = 0;
        for (int i = 1; i <= 10; i++) {
            pushes++;
            gates_err_t e = set(u, i, "step", 0);
            GT_ASSERT(gates_is_ok(e) || e == PROVEN_ERR_NOMEM);
            if (gates_is_ok(e)) kept++;
        }
        /* Every refused push dropped its data; every kept one is dropped by now or at destroy. */
        int undos = 0;
        while (gates_undo_can_undo(u)) {
            GT_ASSERT_OK(gates_undo_undo(u));
            undos++;
        }
        GT_ASSERT(undos <= 4 && undos <= kept);
        gates_undo_destroy(u);
        GT_ASSERT(drops == pushes); /* each pushed change dropped exactly once */
    }
}

int main(void) {
    test_basics();
    test_refusals();
    test_merge();
    test_bound_and_clean();
    test_reentry();
    test_bind();
    test_failures();
    return gt_report("test_undo");
}
