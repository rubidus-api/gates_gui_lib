/* manual example (host): layout - a column with a growing row.
 * expect: the list takes the rest; buttons in a row */
#include <gates/gates.h>

#include <stdio.h>

#define TRY(x) do { if (!gates_is_ok(x)) return 1; } while (0)

int main(void) {
    gates_tree_t *t = nullptr;
    TRY(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), title, list, bar, ok, cancel;
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 8));   /* logical units: 1/96 inch */
    TRY(gates_layout_set_gap(t, root, 6));
    TRY(gates_label_create(t, root, GATES_STR("Files"), &title));
    TRY(gates_panel_create(t, root, &list));
    TRY(gates_layout_set_child_grow(t, list, 1)); /* takes the space left over */
    TRY(gates_panel_create(t, root, &bar));
    TRY(gates_layout_set(t, bar, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, bar, 6));
    TRY(gates_button_create(t, bar, GATES_STR("OK"), nullptr, nullptr, &ok));
    TRY(gates_button_create(t, bar, GATES_STR("Cancel"), nullptr, nullptr, &cancel));
    TRY(gates_layout_run(t, (gates_size_t){ 240, 320 }, gates_text_backend_builtin()));

    gates_rect_t l = gates_node_layout_rect(t, list), a = gates_node_layout_rect(t, ok),
                 b = gates_node_layout_rect(t, cancel), r = gates_node_layout_rect(t, bar);
    /* The list ends one gap above the button row, which ends at the padding. */
    bool rest = l.y + l.h + 6 == r.y && r.y + r.h == 320 - 8;
    printf("%s; buttons %s\n", rest ? "the list takes the rest" : "the list does not grow",
           a.y == b.y && b.x > a.x ? "in a row" : "not in a row");
    gates_tree_destroy(t);
    return 0;
}
