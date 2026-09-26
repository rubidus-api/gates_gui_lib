/* manual example (host): handles, generations and copies.
 * expect: stale handle refused; label kept its copy */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

int main(void) {
    gates_tree_t *tree = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &tree))) return 1;
    gates_node_t root = gates_tree_root(tree), old, fresh;

    /* Text is copied: the caller's buffer may change or go away at once. */
    char buf[16] = "first";
    if (!gates_is_ok(gates_label_create(tree, root, (gates_str_t){ .ptr = (const gates_u8 *)buf, .size = strlen(buf) }, &old))) return 1;
    strcpy(buf, "XXXXX");

    /* Destroying marks the node; its slot is freed at a safe point. */
    if (!gates_is_ok(gates_node_destroy(tree, old))) return 1;
    (void)gates_tree_flush_destroys(tree);

    /* A new node may reuse the slot, with a new generation: the old handle
     * never reaches it. */
    if (!gates_is_ok(gates_label_create(tree, root, GATES_STR("second"), &fresh))) return 1;
    bool stale_refused = !gates_node_is_valid(tree, old) &&
                         gates_widget_set_text(tree, old, GATES_STR("oops")) == PROVEN_ERR_INVALID_ARG;
    gates_str_t now = gates_widget_text(tree, fresh);
    bool kept = now.size == 6 && memcmp(now.ptr, "second", 6) == 0;
    printf("%s; %s\n", stale_refused ? "stale handle refused" : "STALE HANDLE WORKED",
           kept ? "label kept its copy" : "label changed");
    gates_tree_destroy(tree);
    return stale_refused && kept ? 0 : 1;
}
