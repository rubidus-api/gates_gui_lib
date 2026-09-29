/* manual example (host): a property grid over a document's settings.
 * expect: 4 properties in 2 categories; property 2 (Pages) is now 13; property 3 (Draft) is now off */
#include <gates/gates.h>

#include <stdio.h>

enum { P_TITLE = 1, P_PAGES, P_DRAFT, P_PAPER };

/* One handler hears every change, by property id. */
static void on_property(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    (void)user;
    if (ev->result == P_PAGES) printf("property %u (Pages) is now %lld; ", ev->result, (long long)ev->value);
    if (ev->result == P_DRAFT) printf("property %u (Draft) is now %s\n", ev->result, ev->checked ? "on" : "off");
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t grid;
    const gates_option_t papers[] = { { .id = 1, .label = GATES_STR_INIT("A4") }, { .id = 2, .label = GATES_STR_INIT("Letter") } };
    gates_range_t pages = { .min = 1, .max = 999, .value = 12 };
    if (!gates_is_ok(gates_propgrid_create(t, gates_tree_root(t), &grid)) ||
        !gates_is_ok(gates_propgrid_add_text(t, grid, GATES_STR("Document"), P_TITLE, GATES_STR("Title"), GATES_STR("Notes"))) ||
        !gates_is_ok(gates_propgrid_add_number(t, grid, GATES_STR("Document"), P_PAGES, GATES_STR("Pages"), &pages)) ||
        !gates_is_ok(gates_propgrid_add_bool(t, grid, GATES_STR("Print"), P_DRAFT, GATES_STR("Draft"), true)) ||
        !gates_is_ok(gates_propgrid_add_choice(t, grid, GATES_STR("Print"), P_PAPER, GATES_STR("Paper"), papers, 2, 1)) ||
        !gates_is_ok(gates_propgrid_set_handler(t, grid, on_property, nullptr)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 320, 320 }, gates_text_backend_builtin()))) {
        return 1;
    }
    printf("%u properties in %d categories; ", gates_propgrid_count(t, grid),
           !gates_node_eq(gates_propgrid_category(t, grid, GATES_STR("Print")), GATES_NODE_NULL) ? 2 : 1);

    /* A person steps Pages up in its spin box, then turns Draft off with Space. */
    gates_tree_set_focus(t, gates_node_first_child(t, gates_propgrid_editor(t, grid, P_PAGES)));
    gates_key_event_t up = { .key = GATES_KEY_UP, .down = true };
    (void)gates_input_key(t, &up);
    gates_tree_set_focus(t, gates_propgrid_editor(t, grid, P_DRAFT));
    gates_key_event_t space = { .key = GATES_KEY_SPACE, .down = true };
    (void)gates_input_key(t, &space);
    space.down = false;
    (void)gates_input_key(t, &space);
    (void)gates_tree_dispatch_events(t, 0);
    gates_tree_destroy(t);
    return 0;
}
