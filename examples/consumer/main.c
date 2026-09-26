/* consumer - a Windows program built from the gates package alone (plan-0015).
 * A text box and a button: the button greets the name typed. Escape exits.
 * Check (T042): the window opens; typing a name and pressing Greet (or Enter
 * on the button) shows "Hello, <name>!"; Narrator reads the field as "Your name". */
#include <gates/gates.h>

#include <stdio.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

typedef struct app_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t name, greeting;
} app_t;

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    app_t *a = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) gates_app_quit(a->app);
}

static void on_greet(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    app_t *a = user;
    if (ev->kind != GATES_EVENT_ACTIVATED) return;
    gates_str_t name = gates_textbox_text(tree, a->name);
    char buf[160];
    int n = name.size > 0 ? snprintf(buf, sizeof buf, "Hello, %.*s!", (int)(name.size < 100 ? name.size : 100),
                                     (const char *)name.ptr)
                          : snprintf(buf, sizeof buf, "Type a name first.");
    if (n > 0) (void)gates_widget_set_text(tree, a->greeting, (gates_str_t){ .ptr = (const gates_u8 *)buf, .size = (gates_usize_t)n });
}

static gates_err_t build(app_t *a) {
    gates_tree_t *t = a->tree;
    gates_node_t root = gates_tree_root(t), label, button;
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));
    TRY(gates_label_create(t, root, GATES_STR("Your name"), &label));
    TRY(gates_textbox_create(t, root, GATES_STR(""), 30, &a->name));
    TRY(gates_node_set_labelled_by(t, a->name, label));
    TRY(gates_button_create(t, root, GATES_STR("Greet"), nullptr, nullptr, &button));
    TRY(gates_widget_set_handler(t, button, on_greet, a));
    TRY(gates_label_create(t, root, GATES_STR(""), &a->greeting));
    TRY(gates_node_set_live(t, a->greeting, GATES_LIVE_POLITE));
    gates_tree_set_focus(t, a->name);
    return GATES_OK;
}

int main(void) {
    if (gates_version() != GATES_VERSION_NUMBER) return 2; /* library and headers differ */
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) return 1;
    app_t a = { .app = app };
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &a };
    gates_window_desc_t desc = { .title = GATES_STR("gates " GATES_VERSION_STRING ": consumer"), .size = { 360, 200 } };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(app, &desc, &cb, &win))) {
        gates_app_destroy(app);
        return 1;
    }
    a.tree = gates_window_tree(win);
    gates_err_t err = build(&a);
    if (gates_is_ok(err)) {
        gates_window_request_repaint(win);
        err = gates_app_run(app);
    }
    gates_window_destroy(win);
    gates_app_destroy(app);
    return gates_is_ok(err) ? 0 : 1;
}
