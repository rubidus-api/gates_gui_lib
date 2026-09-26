/* ctl_split - the split layout (two panes and a draggable handle).
 *
 * Interaction: a split shares an area between two panes; the person drags the
 * handle to give one pane more room. Panes never shrink below a minimum, and
 * the ratio is kept when the window is resized. Splits nest.
 *
 * Shows: a left navigation pane and a right pane that is itself split
 * top/bottom, plus a button that resets both ratios from code.
 *
 * Field check (T021): dragging the vertical handle resizes left/right and the
 * panes land where the pointer is; dragging the horizontal handle resizes
 * top/bottom; neither pane collapses; "reset" restores both ratios; resizing
 * the window keeps the proportions; ESC exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/ui.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t outer;
    gates_node_t inner;
} demo_t;

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

static void on_reset(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)ev;
    demo_t *d = user;
    (void)gates_layout_set_split(tree, d->outer, GATES_SPLIT_HORIZONTAL, 300);
    (void)gates_layout_set_split(tree, d->inner, GATES_SPLIT_VERTICAL, 600);
}

static gates_err_t pane(gates_tree_t *t, gates_node_t parent, const char *line1,
                        const char *line2) {
    gates_node_t p = GATES_NODE_NULL, n = GATES_NODE_NULL;
    TRY(gates_panel_create(t, parent, &p));
    TRY(gates_layout_set(t, p, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, p, 8));
    TRY(gates_layout_set_gap(t, p, 4));
    gates_usize_t a = 0, b = 0;
    while (line1[a] != '\0') a++;
    while (line2[b] != '\0') b++;
    TRY(gates_label_create(t, p, (gates_str_t){ .ptr = (const gates_u8 *)line1, .size = a }, &n));
    TRY(gates_label_create(t, p, (gates_str_t){ .ptr = (const gates_u8 *)line2, .size = b }, &n));
    return GATES_OK;
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));

    gates_node_t n = GATES_NODE_NULL, reset = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Split: drag a handle to share the space"), &n));
    TRY(gates_button_create(t, root, GATES_STR("reset"), nullptr, nullptr, &reset));
    TRY(gates_widget_set_handler(t, reset, on_reset, d));

    TRY(gates_panel_create(t, root, &d->outer));
    TRY(gates_layout_set(t, d->outer, GATES_LAYOUT_KIND_SPLIT));
    TRY(gates_layout_set_split(t, d->outer, GATES_SPLIT_HORIZONTAL, 300));
    TRY(gates_layout_set_child_grow(t, d->outer, 1));
    TRY(pane(t, d->outer, "Navigation", "left pane"));

    TRY(gates_panel_create(t, d->outer, &d->inner));
    TRY(gates_layout_set(t, d->inner, GATES_LAYOUT_KIND_SPLIT));
    TRY(gates_layout_set_split(t, d->inner, GATES_SPLIT_VERTICAL, 600));
    TRY(pane(t, d->inner, "Content", "top right pane"));
    TRY(pane(t, d->inner, "Details", "bottom right pane"));

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
    gates_window_desc_t desc = { .title = GATES_STR("gates: split"), .size = { 600, 400 } };
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
