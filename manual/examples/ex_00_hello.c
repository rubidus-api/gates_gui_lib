/* manual example (windows): the first program - a window, a label, a button.
 * Build: see chapter 0 (links -lgates -lproven and the Windows libraries). */
#include <gates/gates.h>

#include <stdio.h>

typedef struct hello_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t count_label;
    int clicks;
} hello_t;

/* The button tells us it was pressed; we change the label. Nothing is read
 * back from widgets while painting. */
static void on_button(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    hello_t *h = user;
    if (ev->kind != GATES_EVENT_ACTIVATED) return;
    h->clicks++;
    char text[48];
    int n = snprintf(text, sizeof text, "Pressed %d time%s", h->clicks, h->clicks == 1 ? "" : "s");
    (void)gates_widget_set_text(tree, h->count_label, (gates_str_t){ .ptr = (const gates_u8 *)text, .size = (gates_usize_t)n });
}

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    hello_t *h = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) gates_app_quit(h->app);
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) return 1;
    hello_t h = { .app = app };
    gates_window_desc_t desc = { .title = GATES_STR("Hello, gates"), .size = { 320, 160 } };
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &h };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(app, &desc, &cb, &win))) {
        gates_app_destroy(app);
        return 1;
    }
    h.tree = gates_window_tree(win);
    gates_node_t root = gates_tree_root(h.tree), button;
    gates_err_t err = gates_layout_set(h.tree, root, GATES_LAYOUT_KIND_COLUMN);
    if (gates_is_ok(err)) err = gates_layout_set_padding(h.tree, root, 12);
    if (gates_is_ok(err)) err = gates_label_create(h.tree, root, GATES_STR("Not pressed yet"), &h.count_label);
    if (gates_is_ok(err)) err = gates_button_create(h.tree, root, GATES_STR("Press me"), nullptr, nullptr, &button);
    if (gates_is_ok(err)) err = gates_widget_set_handler(h.tree, button, on_button, &h);
    if (gates_is_ok(err)) err = gates_app_run(app); /* returns when the last window closes */
    gates_window_destroy(win);
    gates_app_destroy(app);
    return gates_is_ok(err) ? 0 : 1;
}
