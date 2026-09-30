/* ctl_log - the log view: a bounded list of lines that follows the end.
 *
 * Interaction: a log keeps at most a set number of lines (and bytes); new
 * lines are appended and the oldest are dropped and counted. While the view
 * shows the last line it follows new ones; scrolling up stops that so the
 * person can read, and reaching the end again (End, wheel, thumb) resumes it.
 * Lines have stable ids, so a selected line stays selected until it is dropped.
 *
 * Shows: a log limited to 200 lines; buttons that add one line or 1000 lines
 * and clear the log; a status line with the count, the dropped lines and
 * whether the view is following.
 *
 * Field check (T032 stage 2): "add 1000" leaves 200 lines, 800+ dropped, the
 * last line visible; wheel up: "following: no" and new lines do not move the
 * view; End: "following: yes"; "clear" empties it; ESC exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/view.h>
#include <gates/ui.h>
#include <gates/access.h>

#include <stdio.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t log, status, one, many, clear;
    unsigned next;
} demo_t;

static void update(demo_t *d) {
    char buf[160];
    int n = snprintf(buf, sizeof buf, "%llu lines   dropped: %llu   following: %s",
                     (unsigned long long)gates_log_count(d->tree, d->log),
                     (unsigned long long)gates_log_dropped(d->tree, d->log),
                     gates_log_following(d->tree, d->log) ? "yes" : "no");
    if (n > 0) {
        (void)gates_widget_set_text(d->tree, d->status,
                                    (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                   .size = (gates_usize_t)n });
    }
}

static void add_lines(demo_t *d, unsigned count) {
    char buf[64];
    for (unsigned i = 0; i < count; i++) {
        d->next++;
        int n = snprintf(buf, sizeof buf, "event %u: something happened", d->next);
        if (!gates_is_ok(gates_log_append(d->tree, d->log,
                                          (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                         .size = (gates_usize_t)n }))) {
            break; /* out of memory: nothing was added for this line */
        }
    }
}

static void on_button(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    if (gates_node_eq(ev->source, d->one)) add_lines(d, 1);
    if (gates_node_eq(ev->source, d->many)) add_lines(d, 1000);
    if (gates_node_eq(ev->source, d->clear)) gates_log_clear(tree, d->log);
    update(d);
}

/* Selection and follow changes (scrolling up stops following, the end resumes
 * it: GATES_EVENT_FOLLOW_CHANGED) both refresh the status line. */
static void on_log(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree; (void)ev;
    update(user);
}

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

static gates_err_t button(demo_t *d, gates_node_t row, gates_str_t text, gates_node_t *out) {
    TRY(gates_button_create(d->tree, row, text, nullptr, nullptr, out));
    return gates_widget_set_handler(d->tree, *out, on_button, d);
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));
    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Log: at most 200 lines, follows the end"), &n));
    TRY(gates_log_create(t, root, &(gates_log_desc_t){ .max_lines = 200 }, &d->log));
    TRY(gates_node_set_font(t, d->log, GATES_FONT_MONO)); /* log lines read best aligned */
    TRY(gates_node_set_access_name(t, d->log, GATES_STR("Log")));
    TRY(gates_layout_set_child_grow(t, d->log, 1));
    TRY(gates_widget_set_handler(t, d->log, on_log, d));
    gates_node_t row = GATES_NODE_NULL;
    TRY(gates_panel_create(t, root, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    TRY(button(d, row, GATES_STR("add 1"), &d->one));
    TRY(button(d, row, GATES_STR("add 1000"), &d->many));
    TRY(button(d, row, GATES_STR("clear"), &d->clear));
    TRY(gates_label_create(t, root, GATES_STR(""), &d->status));
    TRY(gates_label_create(t, root, GATES_STR("ESC exits"), &n));
    add_lines(d, 20);
    update(d);
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t d = { .app = app };
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &d };
    gates_window_desc_t desc = { .title = GATES_STR("gates: log"), .size = { 480, 400 } };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(app, &desc, &cb, &win))) {
        gates_app_destroy(app);
        return 1;
    }
    d.tree = gates_window_tree(win);
    gates_err_t err = build_ui(&d);
    if (!gates_is_ok(err)) {
        fprintf(stderr, "ctl_log: building the window failed (%d)\n", (int)err);
    } else {
        gates_window_request_repaint(win);
        err = gates_app_run(app);
    }
    gates_window_destroy(win);
    gates_app_destroy(app);
    return gates_is_ok(err) ? 0 : 1;
}
