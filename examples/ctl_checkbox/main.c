/* ctl_checkbox - the checkbox control.
 *
 * Interaction: a checkbox lets the person turn one option on or off. The
 * application hears GATES_EVENT_VALUE_CHANGED with the new state. Changes made
 * by the program are silent unless it announces them with gates_widget_notify,
 * in which case the event arrives with origin PROGRAM.
 *
 * Shows: three options, a summary line kept current by the events, and "all
 * on" / "all off" buttons that set the options from code and announce it.
 *
 * Field check: clicking an option toggles its box and updates the
 * summary ("by you"); "all on"/"all off" set every box and the summary says
 * "by the program"; the disabled option cannot be toggled; ESC exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/ui.h>

#include <stdio.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)
#define OPTIONS 3

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t opt[OPTIONS];
    gates_node_t all_on;
    gates_node_t all_off;
    gates_node_t summary;
} demo_t;

static const char *const names[OPTIONS] = { "wrap lines", "show hidden files", "auto save" };

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

static void update_summary(demo_t *d, gates_event_origin_t origin) {
    char buf[128];
    int on = 0;
    for (int i = 0; i < OPTIONS; i++) {
        on += gates_checkbox_checked(d->tree, d->opt[i]) ? 1 : 0;
    }
    int n = snprintf(buf, sizeof buf, "%d of %d on (last change by %s)", on, OPTIONS,
                     origin == GATES_ORIGIN_USER ? "you" : "the program");
    if (n > 0) {
        (void)gates_widget_set_text(d->tree, d->summary,
                                    (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                   .size = (gates_usize_t)n });
    }
}

static void on_option(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    if (ev->kind == GATES_EVENT_VALUE_CHANGED) {
        update_summary(user, ev->origin);
    }
}

static void on_all(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    bool value = gates_node_eq(ev->source, d->all_on);
    for (int i = 0; i < OPTIONS; i++) {
        if (gates_widget_disabled(tree, d->opt[i])) {
            continue;
        }
        /* Silent setter, then an explicit announcement (origin PROGRAM). */
        if (gates_is_ok(gates_checkbox_set_checked(tree, d->opt[i], value))) {
            (void)gates_widget_notify(tree, d->opt[i]);
        }
    }
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));

    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Checkbox: turn an option on or off"), &n));
    for (int i = 0; i < OPTIONS; i++) {
        gates_usize_t len = 0;
        while (names[i][len] != '\0') len++;
        TRY(gates_checkbox_create(t, root,
                                  (gates_str_t){ .ptr = (const gates_u8 *)names[i], .size = len },
                                  i == 0, nullptr, nullptr, &d->opt[i]));
        TRY(gates_widget_set_handler(t, d->opt[i], on_option, d));
    }
    gates_node_t locked = GATES_NODE_NULL;
    TRY(gates_checkbox_create(t, root, GATES_STR("locked option (disabled)"), true, nullptr,
                              nullptr, &locked));
    TRY(gates_widget_set_disabled(t, locked, true));

    gates_node_t row = GATES_NODE_NULL;
    TRY(gates_panel_create(t, root, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    TRY(gates_button_create(t, row, GATES_STR("all on"), nullptr, nullptr, &d->all_on));
    TRY(gates_button_create(t, row, GATES_STR("all off"), nullptr, nullptr, &d->all_off));
    TRY(gates_widget_set_handler(t, d->all_on, on_all, d));
    TRY(gates_widget_set_handler(t, d->all_off, on_all, d));

    TRY(gates_label_create(t, root, GATES_STR(""), &d->summary));
    TRY(gates_label_create(t, root, GATES_STR("ESC exits"), &n));
    update_summary(d, GATES_ORIGIN_PROGRAM);
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t d = { .app = app };
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &d };
    gates_window_desc_t desc = { .title = GATES_STR("gates: checkbox"), .size = { 460, 300 } };
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
