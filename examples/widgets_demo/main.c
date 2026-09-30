/* widgets_demo - widgets, layout, and interaction.
 *
 * Semantic-first: the app describes meaning - a column with a
 * title, a toolbar row, a two-page stack, checkboxes and a status label -
 * and the library lays out and paints it with theme tokens and the
 * system UI font (proportional, 0.2.0).
 *
 * Manual checklist:
 *   - window opens; title, toolbar buttons, page content, status line render;
 *   - buttons show hover and pressed feedback; clicking "Page 1"/"Page 2"
 *     switches the stack page;
 *   - checkboxes toggle their mark and update the status label;
 *   - "Add row" appends a new label to page 1 (relayout live);
 *   - page 2 is a split view: drag the handle to resize the panes; the right
 *     pane scrolls with the wheel and its scrollbar thumb can be dragged;
 *   - resize relayouts without artifacts; ESC exits cleanly;
 *   - F2 cycles the theme: system, light, dark, high contrast; with
 *     "system" the window follows Windows dark mode and high contrast live;
 *   - F3 / F4 zoom the whole window in and out, on top of the
 *     monitor scale and the Windows "Text size" setting.
 */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/ui.h>

#include <stdio.h>

typedef struct demo_t {
    gates_app_t *app;
    gates_window_t *win;
    gates_tree_t *tree;
    gates_node_t stack;
    gates_node_t page1;
    gates_node_t status;
    int rows;
} demo_t;

static void set_status(demo_t *d, const char *msg) {
    gates_str_t s = { .ptr = (const proven_byte_t *)msg, .size = 0 };
    while (msg[s.size] != '\0') s.size++;
    (void)gates_widget_set_text(d->tree, d->status, s);
}

static void on_page(gates_tree_t *tree, gates_node_t node, void *user) {
    (void)node;
    demo_t *d = user;
    gates_u32 page = gates_layout_stack_active(tree, d->stack) == 0 ? 1 : 0;
    (void)gates_layout_set_stack_active(tree, d->stack, page);
    set_status(d, page == 0 ? "status: page 1" : "status: page 2");
}

static void on_add_row(gates_tree_t *tree, gates_node_t node, void *user) {
    (void)node;
    demo_t *d = user;
    char buf[32];
    d->rows++;
    snprintf(buf, sizeof buf, "row %d added", d->rows);
    gates_node_t lbl = GATES_NODE_NULL;
    gates_str_t s = { .ptr = (const proven_byte_t *)buf, .size = 0 };
    while (buf[s.size] != '\0') s.size++;
    (void)gates_label_create(tree, d->page1, s, &lbl);
    set_status(d, buf);
}

static void on_opt(gates_tree_t *tree, gates_node_t node, bool checked, void *user) {
    (void)tree; (void)node;
    demo_t *d = user;
    set_status(d, checked ? "status: option ON" : "status: option OFF");
}

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    demo_t *d = user;
    if (ev->down && ev->vkey == 0x1B) {
        gates_app_quit(d->app);
    }
    /* F2 cycles the theme mode: system -> light -> dark -> high contrast. */
    if (ev->down && ev->key == GATES_KEY_F2) {
        static const char *const names[] = { "status: theme follows the system",
                                             "status: light theme", "status: dark theme",
                                             "status: high-contrast theme" };
        gates_theme_mode_t m = (gates_theme_mode_t)((gates_window_theme_mode(win) + 1) % 4);
        gates_window_set_theme_mode(win, m);
        set_status(d, names[m]);
    }
    /* F3 / F4: zoom in / out in steps of 25%. */
    if (ev->down && (ev->key == GATES_KEY_F3 || ev->key == GATES_KEY_F4)) {
        gates_u32 z = gates_window_zoom(win);
        gates_window_set_zoom(win, ev->key == GATES_KEY_F3 ? z + 25 : (z > 50 ? z - 25 : z));
        char buf[40];
        snprintf(buf, sizeof buf, "status: zoom %u%%", (unsigned)gates_window_zoom(win));
        set_status(d, buf);
    }
}

static void build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    (void)gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN);
    (void)gates_layout_set_padding(t, root, 12);
    (void)gates_layout_set_gap(t, root, 8);

    gates_node_t title = GATES_NODE_NULL;
    (void)gates_label_create(t, root, GATES_STR("gates widgets_demo"), &title);

    /* Toolbar: row of command buttons. */
    gates_node_t toolbar = GATES_NODE_NULL;
    (void)gates_panel_create(t, root, &toolbar);
    (void)gates_layout_set(t, toolbar, GATES_LAYOUT_KIND_ROW);
    (void)gates_layout_set_padding(t, toolbar, 4);
    (void)gates_layout_set_gap(t, toolbar, 6);
    gates_node_t b_page = GATES_NODE_NULL, b_add = GATES_NODE_NULL;
    (void)gates_button_create(t, toolbar, GATES_STR("Switch page"), on_page, d, &b_page);
    (void)gates_button_create(t, toolbar, GATES_STR("Add row"), on_add_row, d, &b_add);

    /* Two-page stack (grows to fill). */
    (void)gates_panel_create(t, root, &d->stack);
    (void)gates_layout_set(t, d->stack, GATES_LAYOUT_KIND_STACK);
    (void)gates_layout_set_child_grow(t, d->stack, 1);

    (void)gates_panel_create(t, d->stack, &d->page1);
    (void)gates_layout_set(t, d->page1, GATES_LAYOUT_KIND_COLUMN);
    (void)gates_layout_set_padding(t, d->page1, 8);
    (void)gates_layout_set_gap(t, d->page1, 4);
    gates_node_t c1 = GATES_NODE_NULL, c2 = GATES_NODE_NULL;
    (void)gates_checkbox_create(t, d->page1, GATES_STR("enable option"), false, on_opt, d, &c1);
    (void)gates_checkbox_create(t, d->page1, GATES_STR("another option"), true, on_opt, d, &c2);

    /* Page 2: a split view whose right pane scrolls. */
    gates_node_t page2 = GATES_NODE_NULL;
    (void)gates_panel_create(t, d->stack, &page2);
    (void)gates_layout_set_split(t, page2, GATES_SPLIT_HORIZONTAL, 400);

    gates_node_t left = GATES_NODE_NULL;
    (void)gates_panel_create(t, page2, &left);
    (void)gates_layout_set(t, left, GATES_LAYOUT_KIND_COLUMN);
    (void)gates_layout_set_padding(t, left, 8);
    (void)gates_layout_set_gap(t, left, 4);
    gates_node_t l1 = GATES_NODE_NULL, l2 = GATES_NODE_NULL;
    (void)gates_label_create(t, left, GATES_STR("left pane"), &l1);
    (void)gates_label_create(t, left, GATES_STR("drag the handle"), &l2);

    gates_node_t right = GATES_NODE_NULL;
    (void)gates_panel_create(t, page2, &right);
    (void)gates_layout_set(t, right, GATES_LAYOUT_KIND_SCROLL);
    (void)gates_layout_set_padding(t, right, 6);
    for (int i = 0; i < 30; i++) {
        char buf[32];
        snprintf(buf, sizeof buf, "scroll line %d", i + 1);
        gates_str_t s = { .ptr = (const proven_byte_t *)buf, .size = 0 };
        while (buf[s.size] != '\0') s.size++;
        gates_node_t row = GATES_NODE_NULL;
        (void)gates_label_create(t, right, s, &row);
    }

    (void)gates_label_create(t, root, GATES_STR("status: ready"), &d->status);
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t d = { .app = app };

    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &d };
    gates_window_desc_t desc = {
        .title = GATES_STR("gates widgets_demo"),
        .size = { 520, 400 },
    };
    if (!gates_is_ok(gates_window_create(app, &desc, &cb, &d.win))) {
        gates_app_destroy(app);
        return 1;
    }
    d.tree = gates_window_tree(d.win);
    build_ui(&d);
    gates_window_request_repaint(d.win);

    gates_err_t err = gates_app_run(app);
    gates_window_destroy(d.win);
    gates_app_destroy(app);
    return gates_is_ok(err) ? 0 : 1;
}
