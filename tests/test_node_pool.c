/* T001: node pool and generation handles (docs/tests/cases/T001-node-pool.md). */
#include <gates/tree.h>
#include "gates_test.h"

#define N_NODES 64

static gates_tree_t *make_tree(gates_u32 initial_capacity) {
    gates_tree_desc_t desc = { .initial_capacity = initial_capacity };
    gates_tree_t *tree = nullptr;
    GT_ASSERT_OK(gates_tree_create(&desc, &tree));
    return tree;
}

static void test_null_handle_invalid(void) {
    gates_tree_t *tree = make_tree(0);
    GT_ASSERT(!gates_node_is_valid(tree, GATES_NODE_NULL));
    GT_ASSERT(gates_node_is_valid(tree, gates_tree_root(tree)));
    gates_tree_destroy(tree);
}

static void test_alloc_distinct_and_roundtrip(void) {
    gates_tree_t *tree = make_tree(0);
    gates_node_t nodes[N_NODES];
    int payload[N_NODES];

    for (int i = 0; i < N_NODES; i++) {
        payload[i] = i;
        gates_node_desc_t d = { .user_data = &payload[i] };
        GT_ASSERT_OK(gates_node_create(tree, GATES_NODE_NULL, &d, &nodes[i]));
    }
    for (int i = 0; i < N_NODES; i++) {
        GT_ASSERT(gates_node_is_valid(tree, nodes[i]));
        GT_ASSERT(gates_node_user_data(tree, nodes[i]) == &payload[i]);
        for (int j = i + 1; j < N_NODES; j++) {
            GT_ASSERT(!gates_node_eq(nodes[i], nodes[j]));
        }
    }
    GT_ASSERT(gates_tree_live_count(tree) == N_NODES + 1);
    gates_tree_destroy(tree);
}

static void test_free_invalidates_immediately(void) {
    gates_tree_t *tree = make_tree(0);
    gates_node_t n = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_node_create(tree, GATES_NODE_NULL, &(gates_node_desc_t){0}, &n));
    GT_ASSERT(gates_node_is_valid(tree, n));

    GT_ASSERT_OK(gates_node_destroy(tree, n));
    /* destroy_pending: invalid immediately, before the flush (§30). */
    GT_ASSERT(!gates_node_is_valid(tree, n));
    GT_ASSERT(gates_tree_pending_count(tree) == 1);

    GT_ASSERT_OK(gates_tree_flush_destroys(tree));
    GT_ASSERT(gates_tree_pending_count(tree) == 0);
    GT_ASSERT(!gates_node_is_valid(tree, n));
    gates_tree_destroy(tree);
}

static void test_slot_reuse_lifo_and_stale_handle(void) {
    gates_tree_t *tree = make_tree(0);
    gates_node_t a = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_node_create(tree, GATES_NODE_NULL, &(gates_node_desc_t){0}, &a));
    GT_ASSERT_OK(gates_node_destroy(tree, a));
    GT_ASSERT_OK(gates_tree_flush_destroys(tree));

    gates_node_t b = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_node_create(tree, GATES_NODE_NULL, &(gates_node_desc_t){0}, &b));
    /* Freed slot reused (free-list LIFO), with a bumped generation. */
    GT_ASSERT(b.index == a.index);
    GT_ASSERT(b.generation > a.generation);
    /* The stale handle must not reach the new occupant. */
    GT_ASSERT(!gates_node_is_valid(tree, a));
    GT_ASSERT(gates_node_is_valid(tree, b));
    gates_tree_destroy(tree);
}

static void test_double_destroy_rejected(void) {
    gates_tree_t *tree = make_tree(0);
    gates_node_t n = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_node_create(tree, GATES_NODE_NULL, &(gates_node_desc_t){0}, &n));
    GT_ASSERT_OK(gates_node_destroy(tree, n));
    gates_u32 pending = gates_tree_pending_count(tree);

    GT_ASSERT_ERR(gates_node_destroy(tree, n));            /* already pending */
    GT_ASSERT(gates_tree_pending_count(tree) == pending);  /* state unchanged */

    GT_ASSERT_OK(gates_tree_flush_destroys(tree));
    GT_ASSERT_ERR(gates_node_destroy(tree, n));            /* stale */
    gates_tree_destroy(tree);
}

static void test_grow_preserves_live_handles(void) {
    /* Tiny initial capacity forces several grows. */
    gates_tree_t *tree = make_tree(2);
    gates_u32 cap0 = gates_tree_capacity(tree);
    gates_node_t nodes[N_NODES];
    int payload[N_NODES];

    for (int i = 0; i < N_NODES; i++) {
        payload[i] = 1000 + i;
        gates_node_desc_t d = { .user_data = &payload[i] };
        GT_ASSERT_OK(gates_node_create(tree, GATES_NODE_NULL, &d, &nodes[i]));
    }
    GT_ASSERT(gates_tree_capacity(tree) > cap0);
    for (int i = 0; i < N_NODES; i++) {
        GT_ASSERT(gates_node_is_valid(tree, nodes[i]));
        GT_ASSERT(gates_node_user_data(tree, nodes[i]) == &payload[i]);
    }
    gates_tree_destroy(tree);
}

static void test_generation_monotonic_per_slot(void) {
    gates_tree_t *tree = make_tree(0);
    gates_node_t prev = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_node_create(tree, GATES_NODE_NULL, &(gates_node_desc_t){0}, &prev));
    gates_u32 slot = prev.index;

    for (int round = 0; round < 8; round++) {
        GT_ASSERT_OK(gates_node_destroy(tree, prev));
        GT_ASSERT_OK(gates_tree_flush_destroys(tree));
        gates_node_t next = GATES_NODE_NULL;
        GT_ASSERT_OK(gates_node_create(tree, GATES_NODE_NULL, &(gates_node_desc_t){0}, &next));
        GT_ASSERT(next.index == slot);
        GT_ASSERT(next.generation > prev.generation);
        prev = next;
    }
    gates_tree_destroy(tree);
}

static void test_error_paths_return_err(void) {
    gates_tree_t *tree = make_tree(0);
    gates_node_t out = GATES_NODE_NULL;
    /* Bad arguments are errors, not aborts. */
    GT_ASSERT_ERR(gates_node_create(tree, (gates_node_t){ .index = 9999, .generation = 0 },
                                    &(gates_node_desc_t){0}, &out));
    GT_ASSERT_ERR(gates_node_destroy(tree, GATES_NODE_NULL));
    GT_ASSERT_ERR(gates_node_destroy(tree, gates_tree_root(tree))); /* root protected */
    GT_ASSERT(gates_node_is_valid(tree, gates_tree_root(tree)));
    gates_tree_destroy(tree);
}

int main(void) {
    test_null_handle_invalid();
    test_alloc_distinct_and_roundtrip();
    test_free_invalidates_immediately();
    test_slot_reuse_lifo_and_stale_handle();
    test_double_destroy_rejected();
    test_grow_preserves_live_handles();
    test_generation_monotonic_per_slot();
    test_error_paths_return_err();
    return gt_report("test_node_pool");
}
