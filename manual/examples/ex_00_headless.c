/* manual example (host): gates without a window - build, lay out, inspect.
 * expect: Press me: button at 0,16 320x26 */
#include <gates/gates.h>

#include <stdio.h>

int main(void) {
    gates_tree_t *tree = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &tree))) return 1;
    gates_node_t root = gates_tree_root(tree), label, button;
    gates_err_t err = gates_layout_set(tree, root, GATES_LAYOUT_KIND_COLUMN);
    if (gates_is_ok(err)) err = gates_label_create(tree, root, GATES_STR("Not pressed yet"), &label);
    if (gates_is_ok(err)) err = gates_button_create(tree, root, GATES_STR("Press me"), nullptr, nullptr, &button);
    /* Layout needs text metrics: the builtin backend works everywhere. */
    if (gates_is_ok(err)) err = gates_layout_run(tree, (gates_size_t){ 320, 160 }, gates_text_backend_builtin());
    gates_access_info_t info;
    if (gates_is_ok(err)) err = gates_access_info(tree, button, 0, &info);
    if (gates_is_ok(err)) {
        printf("%.*s: %s at %d,%d %dx%d\n", (int)info.name.size, (const char *)info.name.ptr,
               info.role == GATES_ROLE_BUTTON ? "button" : "?", info.bounds.x, info.bounds.y, info.bounds.w,
               info.bounds.h);
    }
    gates_tree_destroy(tree);
    return gates_is_ok(err) ? 0 : 1;
}
