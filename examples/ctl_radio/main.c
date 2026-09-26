/* ctl_radio - the radio group control.
 *
 * Interaction: a radio group lets the person pick exactly one of a few
 * options that are all visible at once. The application names options by
 * stable ids and hears GATES_EVENT_VALUE_CHANGED with ev->result = the chosen
 * id. The group is one Tab stop; the arrow keys move the choice.
 *
 * Shows: a theme group with one option that can be switched on and off, a
 * size group that is hidden until "more options" is ticked, a separator, a
 * button that selects from code and announces it, and a summary kept current
 * by the events.
 *
 * Field check (T027 examples): Tab reaches each group once; Up/Down change
 * the theme and skip "high contrast" while it is disabled; clicking a row
 * selects it; ticking "allow high contrast" makes it selectable; ticking
 * "more options" shows the size group (unticking hides it and Tab skips it);
 * "use dark" selects Dark and the summary says "by the program"; ESC exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/ui.h>
#include <gates/access.h>

#include <stdio.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

enum { THEME_LIGHT = 1, THEME_DARK = 2, THEME_CONTRAST = 3 };
enum { SIZE_SMALL = 10, SIZE_MEDIUM = 11, SIZE_LARGE = 12 };

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t theme, sizes, allow, more, use_dark, summary;
} demo_t;

static const gates_option_t themes[] = {
    { .id = THEME_LIGHT, .label = GATES_STR_INIT("Light") },
    { .id = THEME_DARK, .label = GATES_STR_INIT("Dark") },
    { .id = THEME_CONTRAST, .label = GATES_STR_INIT("High contrast"), .disabled = true },
};

static const gates_option_t size_options[] = {
    { .id = SIZE_SMALL, .label = GATES_STR_INIT("Small text") },
    { .id = SIZE_MEDIUM, .label = GATES_STR_INIT("Medium text") },
    { .id = SIZE_LARGE, .label = GATES_STR_INIT("Large text") },
};

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

static const char *theme_name(gates_u32 id) {
    return id == THEME_LIGHT ? "Light" : id == THEME_DARK ? "Dark"
         : id == THEME_CONTRAST ? "High contrast" : "none";
}

static const char *size_name(gates_u32 id) {
    return id == SIZE_SMALL ? "small" : id == SIZE_MEDIUM ? "medium"
         : id == SIZE_LARGE ? "large" : "none";
}

static void update_summary(demo_t *d, gates_event_origin_t origin) {
    char buf[160];
    int n = snprintf(buf, sizeof buf, "theme %s, %s text (last change by %s)",
                     theme_name(gates_options_selected(d->tree, d->theme)),
                     size_name(gates_options_selected(d->tree, d->sizes)),
                     origin == GATES_ORIGIN_USER ? "you" : "the program");
    if (n > 0) {
        (void)gates_widget_set_text(d->tree, d->summary,
                                    (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                   .size = (gates_usize_t)n });
    }
}

static void on_group(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    if (ev->kind == GATES_EVENT_VALUE_CHANGED) {
        update_summary(user, ev->origin); /* ev->result is the chosen id */
    }
}

static void on_allow(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    if (ev->kind != GATES_EVENT_VALUE_CHANGED) {
        return;
    }
    if (!gates_is_ok(gates_options_set_enabled(tree, d->theme, THEME_CONTRAST, ev->checked))) {
        return;
    }
    if (!ev->checked && gates_options_selected(tree, d->theme) == THEME_CONTRAST) {
        (void)gates_options_set_selected(tree, d->theme, THEME_LIGHT);
        (void)gates_widget_notify(tree, d->theme);
    }
}

static void on_more(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    if (ev->kind == GATES_EVENT_VALUE_CHANGED) {
        (void)gates_node_set_hidden(tree, d->sizes, !ev->checked);
    }
}

static void on_use_dark(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    (void)ev;
    /* Silent setter, then an explicit announcement (origin PROGRAM). */
    if (gates_is_ok(gates_options_set_selected(tree, d->theme, THEME_DARK))) {
        (void)gates_widget_notify(tree, d->theme);
    }
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));

    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Radio group: pick one of a few options"), &n));
    TRY(gates_radio_create(t, root, themes, 3, THEME_LIGHT, &d->theme));
    TRY(gates_node_set_access_name(t, d->theme, GATES_STR("Theme")));
    TRY(gates_widget_set_handler(t, d->theme, on_group, d));
    TRY(gates_checkbox_create(t, root, GATES_STR("allow high contrast"), false, nullptr, nullptr,
                              &d->allow));
    TRY(gates_widget_set_handler(t, d->allow, on_allow, d));
    TRY(gates_separator_create(t, root, &n));

    TRY(gates_checkbox_create(t, root, GATES_STR("more options"), false, nullptr, nullptr,
                              &d->more));
    TRY(gates_widget_set_handler(t, d->more, on_more, d));
    TRY(gates_radio_create(t, root, size_options, 3, SIZE_MEDIUM, &d->sizes));
    TRY(gates_node_set_access_name(t, d->sizes, GATES_STR("Size")));
    TRY(gates_widget_set_handler(t, d->sizes, on_group, d));
    TRY(gates_node_set_hidden(t, d->sizes, true));

    TRY(gates_button_create(t, root, GATES_STR("use dark"), nullptr, nullptr, &d->use_dark));
    TRY(gates_layout_set_child_align(t, d->use_dark, GATES_ALIGN_START_V));
    TRY(gates_widget_set_handler(t, d->use_dark, on_use_dark, d));
    TRY(gates_label_create(t, root, GATES_STR(""), &d->summary));
    TRY(gates_label_create(t, root, GATES_STR("Tab moves between groups, arrows choose; ESC exits"),
                           &n));
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
    gates_window_desc_t desc = { .title = GATES_STR("gates: radio group"), .size = { 480, 420 } };
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
