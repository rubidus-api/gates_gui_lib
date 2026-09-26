/* ctl_scroll - the scroll layout (a clipped view over taller content).
 *
 * Interaction: a scroll area lets the person reach content that does not fit,
 * with the mouse wheel or by dragging the scrollbar thumb. Content outside the
 * view is clipped and cannot be clicked. The widgets inside behave as usual.
 *
 * Shows: forty options in a scroll area, a line counting the checked ones
 * (kept by events), and a "top" button that scrolls back from code.
 *
 * Field check (T021): the wheel scrolls by three lines per notch; dragging the
 * thumb scrolls proportionally; options toggle and the count follows; rows
 * scrolled out of view do not react; "top" returns to the first row; ESC exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/ui.h>

#include <stdio.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)
#define ROWS 40

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t scroll;
    gates_node_t count;
    int checked;
} demo_t;

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

static void show_count(demo_t *d) {
    char buf[48];
    int n = snprintf(buf, sizeof buf, "checked: %d of %d", d->checked, ROWS);
    if (n > 0) {
        (void)gates_widget_set_text(d->tree, d->count,
                                    (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                   .size = (gates_usize_t)n });
    }
}

static void on_row(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    demo_t *d = user;
    if (ev->kind == GATES_EVENT_VALUE_CHANGED) {
        d->checked += ev->checked ? 1 : -1;
        show_count(d);
    }
}

static void on_top(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)ev;
    demo_t *d = user;
    (void)gates_layout_set_scroll_offset(tree, d->scroll, 0);
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));

    gates_node_t n = GATES_NODE_NULL, top = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Scroll: reach content that does not fit"), &n));
    TRY(gates_button_create(t, root, GATES_STR("top"), nullptr, nullptr, &top));
    TRY(gates_widget_set_handler(t, top, on_top, d));
    TRY(gates_label_create(t, root, GATES_STR(""), &d->count));

    TRY(gates_panel_create(t, root, &d->scroll));
    TRY(gates_layout_set(t, d->scroll, GATES_LAYOUT_KIND_SCROLL));
    TRY(gates_layout_set_child_grow(t, d->scroll, 1));
    TRY(gates_layout_set_gap(t, d->scroll, 2));
    for (int i = 0; i < ROWS; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof buf, "option %02d", i + 1);
        gates_node_t row = GATES_NODE_NULL;
        TRY(gates_checkbox_create(t, d->scroll,
                                  (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                 .size = len > 0 ? (gates_usize_t)len : 0 },
                                  false, nullptr, nullptr, &row));
        TRY(gates_widget_set_handler(t, row, on_row, d));
    }

    TRY(gates_label_create(t, root, GATES_STR("ESC exits"), &n));
    show_count(d);
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t d = { .app = app };
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &d };
    gates_window_desc_t desc = { .title = GATES_STR("gates: scroll"), .size = { 420, 380 } };
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
