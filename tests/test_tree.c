/* tree links and subtree destroy. */
#include <gates/tree.h>
#include "gates_test.h"

static gates_tree_t *make_tree(void) {
    gates_tree_desc_t desc = {0};
    gates_tree_t *tree = nullptr;
    GT_ASSERT_OK(gates_tree_create(&desc, &tree));
    return tree;
}

static gates_node_t mk(gates_tree_t *tree, gates_node_t parent) {
    gates_node_t n = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_node_create(tree, parent, &(gates_node_desc_t){0}, &n));
    return n;
}

/* Link-consistency checker, run after every mutation:
 * child's parent points back, sibling chain doubly consistent, child_count
 * matches, no cycles (bounded by live node count). */
static gates_u32 check_subtree(gates_tree_t *tree, gates_node_t node, gates_u32 budget) {
    GT_ASSERT(gates_node_is_valid(tree, node));
    gates_u32 seen = 1;
    gates_u32 count = 0;
    gates_node_t prev = GATES_NODE_NULL;
    for (gates_node_t c = gates_node_first_child(tree, node);
         !gates_node_is_null(c);
         c = gates_node_next_sibling(tree, c)) {
        GT_ASSERT(count < budget); /* cycle guard */
        GT_ASSERT(gates_node_eq(gates_node_parent(tree, c), node));
        GT_ASSERT(gates_node_eq(gates_node_prev_sibling(tree, c), prev));
        if (gates_node_is_null(gates_node_next_sibling(tree, c))) {
            GT_ASSERT(gates_node_eq(gates_node_last_child(tree, node), c));
        }
        prev = c;
        count++;
        seen += check_subtree(tree, c, budget);
    }
    if (count == 0) {
        GT_ASSERT(gates_node_is_null(gates_node_first_child(tree, node)));
        GT_ASSERT(gates_node_is_null(gates_node_last_child(tree, node)));
    }
    GT_ASSERT(gates_node_child_count(tree, node) == count);
    return seen;
}

static void assert_consistent(gates_tree_t *tree, gates_node_t from, gates_u32 expect_nodes) {
    gates_u32 seen = check_subtree(tree, from, gates_tree_live_count(tree) + 1);
    GT_ASSERT(seen == expect_nodes);
}

static void test_append_order(void) {
    gates_tree_t *tree = make_tree();
    gates_node_t root = gates_tree_root(tree);
    GT_ASSERT(gates_node_is_null(gates_node_parent(tree, root))); /* root has no parent */

    gates_node_t a = mk(tree, root);
    gates_node_t b = mk(tree, root);
    gates_node_t c = mk(tree, GATES_NODE_NULL);
    GT_ASSERT(gates_node_is_null(gates_node_parent(tree, c))); /* detached: no parent */
    GT_ASSERT_OK(gates_node_append(tree, root, c));

    GT_ASSERT(gates_node_eq(gates_node_first_child(tree, root), a));
    GT_ASSERT(gates_node_eq(gates_node_last_child(tree, root), c));
    GT_ASSERT(gates_node_eq(gates_node_next_sibling(tree, a), b));
    GT_ASSERT(gates_node_child_count(tree, root) == 3);
    assert_consistent(tree, root, 4);

    /* Appending an already-attached child is an error; tree unchanged. */
    GT_ASSERT_ERR(gates_node_append(tree, root, b));
    assert_consistent(tree, root, 4);
    gates_tree_destroy(tree);
}

static void test_insert_before(void) {
    gates_tree_t *tree = make_tree();
    gates_node_t root = gates_tree_root(tree);
    gates_node_t a = mk(tree, root);
    gates_node_t b = mk(tree, root);

    /* Insert before the first child. */
    gates_node_t x = mk(tree, GATES_NODE_NULL);
    GT_ASSERT_OK(gates_node_insert_before(tree, root, x, a));
    GT_ASSERT(gates_node_eq(gates_node_first_child(tree, root), x));

    /* Insert in the middle. */
    gates_node_t y = mk(tree, GATES_NODE_NULL);
    GT_ASSERT_OK(gates_node_insert_before(tree, root, y, b));
    GT_ASSERT(gates_node_eq(gates_node_next_sibling(tree, a), y));
    GT_ASSERT(gates_node_eq(gates_node_prev_sibling(tree, b), y));

    /* before == NULL appends. */
    gates_node_t z = mk(tree, GATES_NODE_NULL);
    GT_ASSERT_OK(gates_node_insert_before(tree, root, z, GATES_NODE_NULL));
    GT_ASSERT(gates_node_eq(gates_node_last_child(tree, root), z));
    assert_consistent(tree, root, 6);

    /* `before` must be a child of parent. */
    gates_node_t sub = mk(tree, a);
    gates_node_t w = mk(tree, GATES_NODE_NULL);
    GT_ASSERT_ERR(gates_node_insert_before(tree, root, w, sub));
    assert_consistent(tree, root, 7);
    gates_tree_destroy(tree);
}

static void test_remove_keeps_subtree(void) {
    gates_tree_t *tree = make_tree();
    gates_node_t root = gates_tree_root(tree);
    gates_node_t a = mk(tree, root);
    gates_node_t b = mk(tree, root);
    gates_node_t a1 = mk(tree, a);
    gates_node_t a2 = mk(tree, a);

    GT_ASSERT_OK(gates_node_remove(tree, a));
    GT_ASSERT(gates_node_is_null(gates_node_parent(tree, a)));
    GT_ASSERT(gates_node_eq(gates_node_first_child(tree, root), b));
    GT_ASSERT(gates_node_child_count(tree, root) == 1);
    /* Detached subtree intact and consistent. */
    GT_ASSERT(gates_node_eq(gates_node_parent(tree, a1), a));
    GT_ASSERT(gates_node_eq(gates_node_parent(tree, a2), a));
    assert_consistent(tree, root, 2);
    assert_consistent(tree, a, 3);

    /* Removing an already-detached node is an error. */
    GT_ASSERT_ERR(gates_node_remove(tree, a));
    /* Root cannot be removed. */
    GT_ASSERT_ERR(gates_node_remove(tree, root));
    gates_tree_destroy(tree);
}

static void test_reparent_and_cycle_prevention(void) {
    gates_tree_t *tree = make_tree();
    gates_node_t root = gates_tree_root(tree);
    gates_node_t a = mk(tree, root);
    gates_node_t b = mk(tree, root);
    gates_node_t a1 = mk(tree, a);
    gates_node_t a11 = mk(tree, a1);

    /* Move subtree a1 (with a11) under b. */
    GT_ASSERT_OK(gates_node_reparent(tree, a1, b));
    GT_ASSERT(gates_node_eq(gates_node_parent(tree, a1), b));
    GT_ASSERT(gates_node_eq(gates_node_parent(tree, a11), a1));
    GT_ASSERT(gates_node_child_count(tree, a) == 0);
    assert_consistent(tree, root, 5);

    /* Cycle prevention: a1 under its own descendant a11 -> error, unchanged. */
    GT_ASSERT_ERR(gates_node_reparent(tree, a1, a11));
    GT_ASSERT_ERR(gates_node_reparent(tree, a1, a1)); /* under itself */
    GT_ASSERT(gates_node_eq(gates_node_parent(tree, a1), b));
    assert_consistent(tree, root, 5);

    /* Root cannot be reparented. */
    GT_ASSERT_ERR(gates_node_reparent(tree, root, b));

    /* Reparenting a detached node attaches it. */
    gates_node_t d = mk(tree, GATES_NODE_NULL);
    GT_ASSERT_OK(gates_node_reparent(tree, d, a));
    GT_ASSERT(gates_node_eq(gates_node_parent(tree, d), a));
    assert_consistent(tree, root, 6);
    gates_tree_destroy(tree);
}

static void test_subtree_destroy_deferred(void) {
    gates_tree_t *tree = make_tree();
    gates_node_t root = gates_tree_root(tree);
    gates_node_t a = mk(tree, root);
    gates_node_t a1 = mk(tree, a);
    gates_node_t a2 = mk(tree, a);
    gates_node_t a21 = mk(tree, a2);
    gates_node_t b = mk(tree, root);
    gates_u32 live_before = gates_tree_live_count(tree); /* 6 */

    GT_ASSERT_OK(gates_node_destroy(tree, a));
    /* Whole subtree invalid immediately (no new event targets). */
    GT_ASSERT(!gates_node_is_valid(tree, a));
    GT_ASSERT(!gates_node_is_valid(tree, a1));
    GT_ASSERT(!gates_node_is_valid(tree, a2));
    GT_ASSERT(!gates_node_is_valid(tree, a21));
    GT_ASSERT(gates_node_is_valid(tree, b));
    GT_ASSERT(gates_tree_pending_count(tree) == 4);
    GT_ASSERT(gates_tree_live_count(tree) == live_before - 4);
    /* Unlinked from the parent already. */
    GT_ASSERT(gates_node_child_count(tree, root) == 1);
    assert_consistent(tree, root, 2);

    /* Ops on pending nodes fail, tree unchanged. */
    GT_ASSERT_ERR(gates_node_remove(tree, a1));
    GT_ASSERT_ERR(gates_node_append(tree, root, a1));
    assert_consistent(tree, root, 2);

    /* Safe point: slots return to the free list; handles stale. */
    GT_ASSERT_OK(gates_tree_flush_destroys(tree));
    GT_ASSERT(gates_tree_pending_count(tree) == 0);
    GT_ASSERT(!gates_node_is_valid(tree, a21));
    assert_consistent(tree, root, 2);
    gates_tree_destroy(tree);
}

static void test_destroy_detached_subtree(void) {
    gates_tree_t *tree = make_tree();
    gates_node_t root = gates_tree_root(tree);
    gates_node_t a = mk(tree, root);
    gates_node_t a1 = mk(tree, a);
    (void)a1;
    GT_ASSERT_OK(gates_node_remove(tree, a));
    GT_ASSERT_OK(gates_node_destroy(tree, a)); /* destroy detached root */
    GT_ASSERT(gates_tree_pending_count(tree) == 2);
    GT_ASSERT_OK(gates_tree_flush_destroys(tree));
    GT_ASSERT(gates_tree_live_count(tree) == 1); /* only root */
    assert_consistent(tree, root, 1);
    gates_tree_destroy(tree);
}

static void test_stale_handle_ops_fail(void) {
    gates_tree_t *tree = make_tree();
    gates_node_t root = gates_tree_root(tree);
    gates_node_t a = mk(tree, root);
    GT_ASSERT_OK(gates_node_destroy(tree, a));
    GT_ASSERT_OK(gates_tree_flush_destroys(tree));

    /* a's slot may be reused; the stale handle must be rejected everywhere. */
    gates_node_t fresh = mk(tree, root);
    (void)fresh;
    GT_ASSERT_ERR(gates_node_remove(tree, a));
    GT_ASSERT_ERR(gates_node_append(tree, root, a));
    GT_ASSERT_ERR(gates_node_reparent(tree, a, root));
    GT_ASSERT_ERR(gates_node_destroy(tree, a));
    gates_node_t out = GATES_NODE_NULL;
    GT_ASSERT_ERR(gates_node_create(tree, a, &(gates_node_desc_t){0}, &out));
    assert_consistent(tree, root, 2);
    gates_tree_destroy(tree);
}

int main(void) {
    test_append_order();
    test_insert_before();
    test_remove_keeps_subtree();
    test_reparent_and_cycle_prevention();
    test_subtree_destroy_deferred();
    test_destroy_detached_subtree();
    test_stale_handle_ops_fail();
    return gt_report("test_tree");
}
