/* ctl_button - the button control.
 *
 * Interaction: a button lets the person ask for one action. The application
 * hears GATES_EVENT_ACTIVATED after a press and release inside the button;
 * releasing outside, or losing the mouse capture mid-press, activates nothing.
 * A disabled button looks inert and ignores the pointer.
 *
 * Shows: a counting button, a button that enables/disables it, and a status
 * line updated from the events (never from paint).
 *
 * Field check: "count" increments the status once per click; pressing
 * "count" and releasing outside it does not count; "disable count" greys it out
 * and clicks on it stop counting, pressing again re-enables it; ESC exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/ui.h>

#include <stdio.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t count_btn;
    gates_node_t toggle_btn;
    gates_node_t status;
    int count;
} demo_t;

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

static void show_status(demo_t *d) {
    char buf[96];
    int n = snprintf(buf, sizeof buf, "count = %d, count button %s", d->count,
                     gates_widget_disabled(d->tree, d->count_btn) ? "disabled" : "enabled");
    if (n > 0) {
        (void)gates_widget_set_text(d->tree, d->status,
                                    (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                   .size = (gates_usize_t)n });
    }
}

static void on_button(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    if (ev->kind != GATES_EVENT_ACTIVATED) {
        return;
    }
    if (gates_node_eq(ev->source, d->count_btn)) {
        d->count++;
    } else {
        bool off = !gates_widget_disabled(tree, d->count_btn);
        (void)gates_widget_set_disabled(tree, d->count_btn, off);
        (void)gates_widget_set_text(tree, d->toggle_btn,
                                    off ? GATES_STR("enable count") : GATES_STR("disable count"));
    }
    show_status(d);
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));

    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Button: ask for one action"), &n));
    gates_node_t row = GATES_NODE_NULL;
    TRY(gates_panel_create(t, root, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    TRY(gates_button_create(t, row, GATES_STR("count"), nullptr, nullptr, &d->count_btn));
    TRY(gates_button_create(t, row, GATES_STR("disable count"), nullptr, nullptr,
                            &d->toggle_btn));
    TRY(gates_widget_set_handler(t, d->count_btn, on_button, d));
    TRY(gates_widget_set_handler(t, d->toggle_btn, on_button, d));
    TRY(gates_label_create(t, root, GATES_STR(""), &d->status));
    TRY(gates_label_create(t, root, GATES_STR("ESC exits"), &n));
    show_status(d);
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t d = { .app = app };
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &d };
    gates_window_desc_t desc = { .title = GATES_STR("gates: button"), .size = { 460, 220 } };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(app, &desc, &cb, &win))) {
        gates_app_destroy(app);
        return 1;
    }
    d.tree = gates_window_tree(win);
    gates_err_t err = build_ui(&d);
    if (gates_is_ok(err)) {
        gates_window_request_repaint(win);
        err = gates_app_run(app);
    }
    gates_window_destroy(win);
    gates_app_destroy(app);
    return gates_is_ok(err) ? 0 : 1;
}
