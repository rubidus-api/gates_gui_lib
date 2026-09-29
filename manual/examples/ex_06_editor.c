/* manual example (host): a multi-line editor - typing, lines, undo and the modified mark.
 * expect: 3 lines, caret on line 2; after undo: 2 lines, modified: no */
#include <gates/gates.h>

#include <stdio.h>

static void press(gates_tree_t *t, gates_key_t key, bool ctrl) {
    gates_key_event_t ev = { .key = key, .ctrl = ctrl, .down = true };
    (void)gates_input_key(t, &ev);
    ev.down = false;
    (void)gates_input_key(t, &ev);
}

static void type(gates_tree_t *t, const char *s) {
    for (; *s != 0; s++) (void)gates_input_char(t, (gates_u32)(unsigned char)*s);
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t notes;
    gates_editor_desc_t desc = { .rows = 8, .cols = 40 };
    if (!gates_is_ok(gates_editor_create(t, gates_tree_root(t), &desc, &notes)) ||
        !gates_is_ok(gates_editor_set_text(t, notes, GATES_STR("Shopping\nMilk"))) ||
        !gates_is_ok(gates_node_set_access_name(t, notes, GATES_STR("Notes"))) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 360, 200 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* A person goes to the end, starts a new line and types. */
    gates_tree_set_focus(t, notes);
    press(t, GATES_KEY_END, true);
    press(t, GATES_KEY_ENTER, false);
    type(t, "Bread");
    const gates_text_buffer_t *text = gates_editor_buffer(t, notes);
    gates_u32 caret = 0;
    gates_editor_selection(t, notes, nullptr, &caret);
    printf("%u lines, caret on line %u; ", gates_text_buffer_line_count(text), gates_text_buffer_line_of(text, caret));

    /* Ctrl+Z takes the typing back, then the new line: the text is as it was set. */
    press(t, GATES_KEY_Z, true);
    press(t, GATES_KEY_Z, true);
    printf("after undo: %u lines, modified: %s\n", gates_text_buffer_line_count(text),
           gates_editor_modified(t, notes) ? "yes" : "no");
    gates_tree_destroy(t);
    return 0;
}
