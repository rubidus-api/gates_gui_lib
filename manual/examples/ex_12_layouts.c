/* manual example (host): a grid form inside a collapsible group, and wrapped chips.
 * expect: labels in column 1, fields in column 2 at x 40; 7 chips on 3 lines; folded: 0 fields reachable */
#include <gates/gates.h>

#include <stdio.h>

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t root = gates_tree_root(t), group, content, grid, chips, field[2], chip[7];
    const gates_text_backend_t *be = gates_text_backend_builtin(); /* 8 units a character */
    bool ok = gates_is_ok(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN)) &&
              gates_is_ok(gates_group_create(t, root, GATES_STR("&Connection"), true, &group, &content)) &&
              gates_is_ok(gates_panel_create(t, content, &grid)) &&
              gates_is_ok(gates_layout_set(t, grid, GATES_LAYOUT_KIND_GRID)) &&   /* two columns */
              gates_is_ok(gates_layout_set_gap(t, grid, 8)) &&
              gates_is_ok(gates_layout_set_grid_column_grow(t, grid, 1, 1));    /* fields take the rest */
    static const char *names[2] = { "Host", "Port" };
    for (int i = 0; ok && i < 2; i++) {
        gates_node_t l;
        ok = gates_is_ok(gates_label_create(t, grid, (gates_str_t){ .ptr = (const gates_u8 *)names[i], .size = 4 }, &l)) &&
             gates_is_ok(gates_textbox_create(t, grid, GATES_STR(""), 12, &field[i])) &&
             gates_is_ok(gates_node_set_labelled_by(t, field[i], l));
    }
    ok = ok && gates_is_ok(gates_panel_create(t, root, &chips)) &&
         gates_is_ok(gates_layout_set(t, chips, GATES_LAYOUT_KIND_WRAP)) &&
         gates_is_ok(gates_layout_set_gap(t, chips, 4));
    for (int i = 0; ok && i < 7; i++) {
        ok = gates_is_ok(gates_button_create(t, chips, GATES_STR("tag"), nullptr, nullptr, &chip[i]));
    }
    if (!ok || !gates_is_ok(gates_layout_run(t, (gates_size_t){ 160, 400 }, be))) return 1;

    gates_rect_t g = gates_node_layout_rect(t, grid), f = gates_node_layout_rect(t, field[0]);
    int lines = 1;
    for (int i = 1; i < 7; i++) {
        lines += gates_node_layout_rect(t, chip[i]).y > gates_node_layout_rect(t, chip[i - 1]).y;
    }
    /* Folding the group hides its content: nothing inside takes room or focus. */
    if (!gates_is_ok(gates_group_set_expanded(t, group, false)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 160, 400 }, be))) {
        return 1;
    }
    int shown = gates_widget_focusable(t, field[0]) + gates_widget_focusable(t, field[1]);
    printf("labels in column 1, fields in column 2 at x %d; 7 chips on %d lines; folded: %d fields reachable\n",
           f.x - g.x, lines, shown);
    gates_tree_destroy(t);
    return 0;
}
