/* T003: foundation layer - proven wrappers, allocator injection (docs/tests/cases/T003-foundation.md). */
#include <gates/types.h>
#include <gates/tree.h>
#include <proven/heap.h>
#include <proven/arena.h>
#include "gates_test.h"

#include <stdlib.h>
#include <string.h>

/* Fixed-width aliases match proven counterparts exactly. */
static_assert(sizeof(gates_u8) == 1, "gates_u8");
static_assert(sizeof(gates_u32) == 4, "gates_u32");
static_assert(sizeof(gates_u64) == 8, "gates_u64");
static_assert(sizeof(gates_i32) == 4, "gates_i32");
static_assert(sizeof(gates_i64) == 8, "gates_i64");
static_assert((gates_i32)-1 < 0, "gates_i32 signed");
static_assert((gates_u32)-1 > 0, "gates_u32 unsigned");
static_assert(sizeof(gates_usize_t) == sizeof(proven_size_t), "gates_usize_t");
static_assert(sizeof(gates_str_t) == sizeof(proven_u8str_view_t), "gates_str_t layout");

/* Counting allocator: wraps the proven heap allocator and counts every call -
 * proves the tree performs no hidden allocation outside the injected trait. */
typedef struct {
    gates_allocator_t inner;
    gates_usize_t allocs;
    gates_usize_t reallocs;
    gates_usize_t frees;
} count_alloc_t;

static proven_result_mem_mut_t count_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    count_alloc_t *c = ctx;
    c->allocs++;
    return c->inner.alloc_fn(c->inner.ctx, size, align);
}

static proven_result_mem_mut_t count_realloc(void *ctx, void *old_ptr, proven_size_t old_size,
                                             proven_size_t new_size, proven_size_t align) {
    count_alloc_t *c = ctx;
    c->reallocs++;
    return c->inner.realloc_fn(c->inner.ctx, old_ptr, old_size, new_size, align);
}

static void count_free(void *ctx, void *ptr) {
    count_alloc_t *c = ctx;
    c->frees++;
    c->inner.free_fn(c->inner.ctx, ptr);
}

static void test_err_style(void) {
    gates_err_t ok = GATES_OK;
    gates_err_t bad = PROVEN_ERR_INVALID_ARG;
    GT_ASSERT(gates_is_ok(ok));
    GT_ASSERT(!gates_is_ok(bad));
    GT_ASSERT(ok != bad);
}

static void test_str_view_no_copy(void) {
    gates_str_t s = GATES_STR("hello gates");
    GT_ASSERT(s.size == 11);
    /* Round trip is the identity: same pointer, no copy. */
    proven_u8str_view_t p = gates_str_to_proven(s);
    gates_str_t back = gates_str_from_proven(p);
    GT_ASSERT(p.ptr == s.ptr && p.size == s.size);
    GT_ASSERT(back.ptr == s.ptr && back.size == s.size);
    GT_ASSERT(memcmp(s.ptr, "hello gates", s.size) == 0);
}

static void test_node_handle_basics(void) {
    gates_node_t null_node = GATES_NODE_NULL;
    GT_ASSERT(gates_node_is_null(null_node));
    gates_node_t a = { .index = 3, .generation = 7 };
    gates_node_t b = { .index = 3, .generation = 8 };
    GT_ASSERT(!gates_node_eq(a, b));   /* same slot, different generation */
    GT_ASSERT(gates_node_eq(a, a));
    GT_ASSERT(!gates_node_is_null(a));
}

static void test_counting_allocator(void) {
    count_alloc_t counter = { .inner = proven_heap_allocator() };
    gates_tree_desc_t desc = {
        .allocator = { .ctx = &counter, .alloc_fn = count_alloc,
                       .realloc_fn = count_realloc, .free_fn = count_free },
    };
    gates_tree_t *tree = nullptr;
    GT_ASSERT_OK(gates_tree_create(&desc, &tree));
    GT_ASSERT(tree != nullptr);
    GT_ASSERT(counter.allocs > 0);

    gates_node_t n = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_node_create(tree, gates_tree_root(tree), &(gates_node_desc_t){0}, &n));
    GT_ASSERT(gates_node_is_valid(tree, n));

    gates_tree_destroy(tree);
    /* Every allocation freed through the injected trait. */
    GT_ASSERT(counter.frees == counter.allocs);
}

static void test_arena_backing(void) {
    /* The tree runs unchanged on an arena (allocator-agnostic core). */
    static unsigned char backing[64 * 1024];
    proven_arena_t arena = proven_arena_create(
        (proven_mem_mut_t){ .ptr = backing, .size = sizeof backing });
    gates_tree_desc_t desc = { .allocator = proven_arena_as_allocator(&arena) };

    gates_tree_t *tree = nullptr;
    GT_ASSERT_OK(gates_tree_create(&desc, &tree));
    gates_node_t n = GATES_NODE_NULL;
    for (int i = 0; i < 100; i++) {
        GT_ASSERT_OK(gates_node_create(tree, gates_tree_root(tree),
                                       &(gates_node_desc_t){0}, &n));
    }
    GT_ASSERT(gates_tree_live_count(tree) == 101); /* root + 100 */
    gates_tree_destroy(tree);
    proven_arena_reset(&arena);
}

int main(void) {
    test_err_style();
    test_str_view_no_copy();
    test_node_handle_basics();
    test_counting_allocator();
    test_arena_backing();
    return gt_report("test_foundation");
}
