/* manual example (host): tabs, and state kept between runs.
 * expect: tab 1 (Advanced) after Ctrl+Tab; saved 2 lines; next run: tab 1, split 700 */
#include <gates/gates.h>

#include <stdio.h>

/* Builds the same window each run: tabs and a split, both with automation ids. */
static bool build(gates_tree_t **out, gates_node_t *tabs, gates_node_t *split, gates_node_t *apply) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return false;
    gates_node_t root = gates_tree_root(t), page, left, right, reset;
    bool ok = gates_is_ok(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN)) &&
              gates_is_ok(gates_tabs_create(t, root, tabs)) &&
              gates_is_ok(gates_tabs_add(t, *tabs, GATES_STR("&General"), &page)) &&
              gates_is_ok(gates_button_create(t, page, GATES_STR("Apply"), nullptr, nullptr, apply)) &&
              gates_is_ok(gates_tabs_add(t, *tabs, GATES_STR("&Advanced"), &page)) &&
              gates_is_ok(gates_button_create(t, page, GATES_STR("Reset"), nullptr, nullptr, &reset)) &&
              gates_is_ok(gates_node_set_automation_id(t, *tabs, GATES_STR("settings.tabs"))) &&
              gates_is_ok(gates_panel_create(t, root, split)) &&
              gates_is_ok(gates_layout_set(t, *split, GATES_LAYOUT_KIND_SPLIT)) &&
              gates_is_ok(gates_panel_create(t, *split, &left)) &&
              gates_is_ok(gates_panel_create(t, *split, &right)) &&
              gates_is_ok(gates_node_set_automation_id(t, *split, GATES_STR("main.split"))) &&
              gates_is_ok(gates_layout_run(t, (gates_size_t){ 400, 300 }, gates_text_backend_builtin()));
    *out = t;
    return ok;
}

int main(void) {
    gates_tree_t *t;
    gates_node_t tabs, split, apply;
    if (!build(&t, &tabs, &split, &apply)) return 1;
    /* The person works in General, then Ctrl+Tab switches to Advanced. */
    gates_tree_set_focus(t, apply);
    gates_key_event_t ctrl_tab = { .key = GATES_KEY_TAB, .ctrl = true, .down = true };
    (void)gates_input_key(t, &ctrl_tab);
    gates_u32 now = gates_tabs_selected(t, tabs);
    gates_str_t title = gates_tabs_title(t, tabs, now);
    /* ...and drags the split. Save the state (a real program writes it to a file). */
    if (!gates_is_ok(gates_layout_set_split(t, split, GATES_SPLIT_HORIZONTAL, 700))) return 1;
    gates_u8 text[256];
    gates_usize_t len = 0;
    if (!gates_is_ok(gates_state_save(t, text, sizeof text, &len))) return 1;
    int lines = 0;
    for (gates_usize_t i = 0; i < len; i++) lines += text[i] == '\n' && i > 16;
    gates_tree_destroy(t);

    /* The next run builds the window again and loads what was saved. */
    if (!build(&t, &tabs, &split, &apply)) return 1;
    if (!gates_is_ok(gates_state_load(t, (gates_str_t){ .ptr = text, .size = len }, nullptr))) return 1;
    printf("tab %u (%.*s) after Ctrl+Tab; saved %d lines; next run: tab %u, split %d\n", now,
           (int)title.size - 1, (const char *)title.ptr + 1, lines, gates_tabs_selected(t, tabs),
           gates_layout_split_ratio(t, split));
    gates_tree_destroy(t);
    return 0;
}
