/* ctl_panel - the panel control (a container).
 *
 * Interaction: a panel groups related things so the person sees them as one
 * unit; it takes no input itself. Its children are laid out by the panel's
 * layout (row or column here). Nodes can be added and removed while the program
 * runs; a removal requested from an event handler is carried out at the
 * window's next safe point, so no widget disappears under the pointer mid-event.
 *
 * Shows: a row of three grouped panels, and a list panel that the "add item" /
 * "remove last" buttons grow and shrink at run time.
 *
 * Field check (T021): three panels sit side by side with their contents; "add
 * item" appends a row to the list, "remove last" removes one until the list is
 * empty (then does nothing); the count line follows; ESC exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/ui.h>

#include <stdio.h>
#include <string.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t list;
    gates_node_t add_btn;
    gates_node_t remove_btn;
    gates_node_t count;
    int next_id;
} demo_t;

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

static gates_str_t fmt(char *buf, gates_usize_t cap, const char *f, int v) {
    int n = snprintf(buf, cap, f, v);
    return (gates_str_t){ .ptr = (const gates_u8 *)buf, .size = n > 0 ? (gates_usize_t)n : 0 };
}

static void show_count(demo_t *d) {
    char buf[64];
    (void)gates_widget_set_text(d->tree, d->count,
                                fmt(buf, sizeof buf, "items in the list: %d",
                                    (int)gates_node_child_count(d->tree, d->list)));
}

static gates_err_t add_item(demo_t *d) {
    char buf[64];
    gates_node_t item = GATES_NODE_NULL, label = GATES_NODE_NULL;
    TRY(gates_panel_create(d->tree, d->list, &item));
    gates_err_t err = gates_layout_set(d->tree, item, GATES_LAYOUT_KIND_ROW);
    if (gates_is_ok(err)) {
        err = gates_label_create(d->tree, item, fmt(buf, sizeof buf, "item %d", ++d->next_id),
                                 &label);
    }
    if (!gates_is_ok(err)) {
        (void)gates_node_destroy(d->tree, item); /* no half-built row stays visible */
    }
    return err;
}

static void on_button(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    if (gates_node_eq(ev->source, d->add_btn)) {
        (void)add_item(d);
    } else {
        gates_node_t last = gates_node_last_child(tree, d->list);
        if (gates_node_is_valid(tree, last)) {
            (void)gates_node_destroy(tree, last); /* freed at the window's safe point */
        }
    }
    show_count(d);
}

static gates_err_t group(gates_tree_t *t, gates_node_t parent, const char *title,
                         const char *a, const char *b) {
    gates_node_t p = GATES_NODE_NULL, n = GATES_NODE_NULL;
    TRY(gates_panel_create(t, parent, &p));
    TRY(gates_layout_set(t, p, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, p, 6));
    TRY(gates_layout_set_gap(t, p, 4));
    TRY(gates_layout_set_child_grow(t, p, 1));
    TRY(gates_label_create(t, p, (gates_str_t){ .ptr = (const gates_u8 *)title,
                                               .size = strlen(title) }, &n));
    TRY(gates_label_create(t, p, (gates_str_t){ .ptr = (const gates_u8 *)a,
                                               .size = strlen(a) }, &n));
    TRY(gates_label_create(t, p, (gates_str_t){ .ptr = (const gates_u8 *)b,
                                               .size = strlen(b) }, &n));
    return GATES_OK;
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));

    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Panel: group related things"), &n));
    gates_node_t row = GATES_NODE_NULL;
    TRY(gates_panel_create(t, root, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    TRY(group(t, row, "Account", "user: demo", "plan: free"));
    TRY(group(t, row, "Storage", "used: 2 GB", "free: 8 GB"));
    TRY(group(t, row, "Network", "online", "latency: 12 ms"));

    gates_node_t buttons = GATES_NODE_NULL;
    TRY(gates_panel_create(t, root, &buttons));
    TRY(gates_layout_set(t, buttons, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, buttons, 8));
    TRY(gates_button_create(t, buttons, GATES_STR("add item"), nullptr, nullptr, &d->add_btn));
    TRY(gates_button_create(t, buttons, GATES_STR("remove last"), nullptr, nullptr,
                            &d->remove_btn));
    TRY(gates_widget_set_handler(t, d->add_btn, on_button, d));
    TRY(gates_widget_set_handler(t, d->remove_btn, on_button, d));

    TRY(gates_label_create(t, root, GATES_STR(""), &d->count));
    TRY(gates_panel_create(t, root, &d->list));
    TRY(gates_layout_set(t, d->list, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, d->list, 4));
    TRY(gates_layout_set_gap(t, d->list, 2));
    TRY(add_item(d));
    TRY(add_item(d));
    show_count(d);
    TRY(gates_label_create(t, root, GATES_STR("ESC exits"), &n));
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t d = { .app = app };
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &d };
    gates_window_desc_t desc = { .title = GATES_STR("gates: panel"), .size = { 520, 420 } };
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
