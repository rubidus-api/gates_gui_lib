/* manual example (host): events - what the person did, delivered at a safe point.
 * expect: user checked=1, program checked=0 */
#include <gates/gates.h>

#include <stdio.h>

static char log_text[128];
static int log_len;

static void on_value(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    (void)user;
    if (ev->kind != GATES_EVENT_VALUE_CHANGED) return;
    log_len += snprintf(log_text + log_len, sizeof log_text - (size_t)log_len, "%s%s checked=%d",
                        log_len > 0 ? ", " : "", ev->origin == GATES_ORIGIN_USER ? "user" : "program",
                        ev->checked ? 1 : 0);
}

int main(void) {
    gates_tree_t *tree = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &tree))) return 1;
    gates_node_t box;
    if (!gates_is_ok(gates_checkbox_create(tree, gates_tree_root(tree), GATES_STR("Send news"), false, nullptr, nullptr, &box)) ||
        !gates_is_ok(gates_widget_set_handler(tree, box, on_value, nullptr))) {
        return 1;
    }
    /* A person ticks the box (here through the accessibility action, which
     * takes the same path as a click): an event is queued, not delivered. */
    if (!gates_is_ok(gates_access_toggle(tree, box))) return 1;
    /* The window delivers queued events after each input message; without a
     * window, the program does it. */
    (void)gates_tree_dispatch_events(tree, 0);

    /* Setters are silent: the program knows what it did. Announce it
     * explicitly when the rest of the program should hear it. */
    if (!gates_is_ok(gates_checkbox_set_checked(tree, box, false))) return 1;
    if (!gates_is_ok(gates_widget_notify(tree, box))) return 1;
    (void)gates_tree_dispatch_events(tree, 0);

    printf("%s\n", log_text);
    gates_tree_destroy(tree);
    return 0;
}
