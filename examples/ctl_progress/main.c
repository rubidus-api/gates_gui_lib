/* ctl_progress - the progress bar and the separator.
 *
 * Interaction: a progress bar reports how far a piece of work has come; the
 * person reads it, it takes no input and is never focused. Its value is in
 * per-mille (0..1000) and is clamped. The words next to it are an ordinary
 * label. A separator is a thin line between groups: horizontal in a column,
 * vertical in a row.
 *
 * Shows: a bar with a percentage label, buttons that step it, fill it and
 * reset it, a "run" button that fills it with a repeating timer (gates/timer.h)
 * and turns into "stop" while running, and separators in both directions.
 *
 * Field check: "+10%" and "-10%" move the fill and the label
 * in steps and stop at 0% and 100%; "fill" and "reset" jump to the ends; Tab
 * visits only the buttons, never the bar; the separators show as a
 * horizontal line under the bar and a vertical line between the button
 * groups; "run" fills the bar smoothly in about 2.5 s and turns back into
 * "run" at 100%; "stop" halts it at once; ESC exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/ui.h>
#include <gates/timer.h>
#include <gates/access.h>

#include <stdio.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t bar, percent, up, down, fill, reset, run;
    gates_timer_id_t timer;      /* 0 = not running */
} demo_t;

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

static void show(demo_t *d) {
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%d%% done", (int)(gates_progress_value(d->tree, d->bar) / 10));
    if (n > 0) {
        (void)gates_widget_set_text(d->tree, d->percent,
                                    (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                   .size = (gates_usize_t)n });
    }
}

/* A repeating timer moves the bar: the work "happens" in steps of 2%. */
static void on_tick(gates_tree_t *tree, gates_node_t node, gates_timer_id_t id, void *user) {
    (void)node;
    demo_t *d = user;
    gates_i32 v = gates_progress_value(tree, d->bar) + 20;
    (void)gates_progress_set_value(tree, d->bar, v);
    if (v >= 1000) {
        (void)gates_timer_cancel(tree, id);
        d->timer = 0;
        (void)gates_widget_set_text(tree, d->run, GATES_STR("run"));
        (void)gates_access_announce(tree, GATES_STR("Work finished"), false); /* for screen readers */
    }
    show(d);
}

static void on_run(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)ev;
    demo_t *d = user;
    if (d->timer != 0) {
        (void)gates_timer_cancel(tree, d->timer); /* stops at once: no tick after this */
        d->timer = 0;
        (void)gates_widget_set_text(tree, d->run, GATES_STR("run"));
        return;
    }
    if (gates_progress_value(tree, d->bar) >= 1000) {
        (void)gates_progress_set_value(tree, d->bar, 0);
    }
    if (gates_is_ok(gates_timer_start(tree, d->bar, 50, true, on_tick, d, &d->timer))) {
        (void)gates_widget_set_text(tree, d->run, GATES_STR("stop"));
    }
    show(d);
}

static void on_button(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    gates_i32 v = gates_progress_value(tree, d->bar);
    if (gates_node_eq(ev->source, d->up)) v += 100;
    if (gates_node_eq(ev->source, d->down)) v -= 100;
    if (gates_node_eq(ev->source, d->fill)) v = 1000;
    if (gates_node_eq(ev->source, d->reset)) v = 0;
    if (gates_is_ok(gates_progress_set_value(tree, d->bar, v))) { /* clamped to 0..1000 */
        show(d);
    }
}

static gates_err_t button(demo_t *d, gates_node_t parent, gates_str_t text, gates_node_t *out) {
    TRY(gates_button_create(d->tree, parent, text, nullptr, nullptr, out));
    return gates_widget_set_handler(d->tree, *out, on_button, d);
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));

    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Progress: how far the work has come"), &n));
    TRY(gates_progress_create(t, root, 300, &d->bar));
    TRY(gates_node_set_access_name(t, d->bar, GATES_STR("Work")));
    TRY(gates_label_create(t, root, GATES_STR(""), &d->percent));
    TRY(gates_separator_create(t, root, &n));

    gates_node_t row = GATES_NODE_NULL;
    TRY(gates_panel_create(t, root, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    TRY(button(d, row, GATES_STR("-10%"), &d->down));
    TRY(button(d, row, GATES_STR("+10%"), &d->up));
    TRY(gates_separator_create(t, row, &n));
    TRY(button(d, row, GATES_STR("fill"), &d->fill));
    TRY(button(d, row, GATES_STR("reset"), &d->reset));
    TRY(gates_separator_create(t, row, &n));
    TRY(gates_button_create(t, row, GATES_STR("run"), nullptr, nullptr, &d->run));
    TRY(gates_widget_set_handler(t, d->run, on_run, d));

    TRY(gates_label_create(t, root, GATES_STR("ESC exits"), &n));
    show(d);
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t d = { .app = app };
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &d };
    gates_window_desc_t desc = { .title = GATES_STR("gates: progress"), .size = { 460, 260 } };
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
