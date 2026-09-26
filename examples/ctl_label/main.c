/* ctl_label - the label control.
 *
 * Interaction: a label tells the person something; it takes no input. The
 * application says what the text is; the backend decides how it looks
 * (RFC-0001 section 1.0). Text is UTF-8 and every character takes its own
 * advance from the font, Hangul and other wide characters included (RFC-0004).
 *
 * Shows: plain, wide-character and disabled labels, and a label whose text
 * the program replaces when a button is activated (programmatic setters are
 * silent; the change is visible on the next frame).
 *
 * Field check (T021): all four labels render with real glyphs; "next message"
 * cycles the last label through three texts; ESC exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/ui.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t message;
    int step;
} demo_t;

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

static void next_message(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)ev;
    demo_t *d = user;
    static const char *const texts[] = { "first message", "second: 두 번째", "third message" };
    d->step = (d->step + 1) % 3;
    const char *t = texts[d->step];
    gates_usize_t n = 0;
    while (t[n] != '\0') n++;
    (void)gates_widget_set_text(tree, d->message, (gates_str_t){ .ptr = (const gates_u8 *)t,
                                                                 .size = n });
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));

    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Label: text the person reads"), &n));
    TRY(gates_label_create(t, root, GATES_STR("Wide characters: 한글 漢字 かな (2 cells each)"), &n));
    TRY(gates_label_create(t, root, GATES_STR("A disabled label"), &n));
    TRY(gates_widget_set_disabled(t, n, true));
    TRY(gates_label_create(t, root, GATES_STR("first message"), &d->message));

    gates_node_t btn = GATES_NODE_NULL;
    TRY(gates_button_create(t, root, GATES_STR("next message"), nullptr, nullptr, &btn));
    TRY(gates_widget_set_handler(t, btn, next_message, d));
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
    gates_window_desc_t desc = { .title = GATES_STR("gates: label"), .size = { 460, 260 } };
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
