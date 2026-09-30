/* ctl_keyboard - a form used with the keyboard alone (focus and activation).
 *
 * Interaction: every control can be reached and used without a mouse. Tab and
 * Shift+Tab move through the controls in the order they appear; Space presses
 * the focused button or toggles the focused checkbox (on release); Enter runs
 * the form's default command from any field; Escape runs its cancel command.
 * The focused control is marked with the focus ring. Moving focus into the
 * scroll area scrolls the focused option into view.
 *
 * The "Submit" command is the default (Enter) and stays disabled until the
 * terms are accepted, so its button looks inert and Tab skips it. "Quit" is the
 * cancel command (Escape).
 *
 * Field check: Tab from the start reaches name, email, newsletter,
 * terms, the options in the scroll area (which scrolls to follow), then the
 * buttons; Shift+Tab goes back; Space toggles; Enter in a field submits once
 * the terms are ticked; the status line shows what was submitted; Escape quits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/command.h>
#include <gates/ui.h>
#include <gates/access.h>

#include <stdio.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

enum { CMD_SUBMIT = 1, CMD_QUIT = 2 };

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t name, email, news, terms, reset, status;
    int submitted;
} demo_t;

static void set_status(demo_t *d, const char *text) {
    int n = 0;
    while (text[n] != '\0') n++;
    (void)gates_widget_set_text(d->tree, d->status,
                                (gates_str_t){ .ptr = (const gates_u8 *)text,
                                               .size = (gates_usize_t)n });
}

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    demo_t *d = user;
    if (id == CMD_QUIT) {
        gates_app_quit(d->app);
        return;
    }
    char buf[200];
    gates_str_t name = gates_textbox_text(tree, d->name);
    gates_str_t email = gates_textbox_text(tree, d->email);
    snprintf(buf, sizeof buf, "submitted #%d: %.*s <%.*s>, newsletter %s", ++d->submitted,
             (int)name.size, (const char *)name.ptr, (int)email.size, (const char *)email.ptr,
             gates_checkbox_checked(tree, d->news) ? "yes" : "no");
    set_status(d, buf);
}

static void on_terms(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)user;
    if (ev->kind == GATES_EVENT_VALUE_CHANGED) {
        (void)gates_command_set_enabled(tree, gates_tree_root(tree), CMD_SUBMIT, ev->checked);
    }
}

static void on_reset(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)ev;
    demo_t *d = user;
    (void)gates_textbox_set_text(tree, d->name, GATES_STR(""));
    (void)gates_textbox_set_text(tree, d->email, GATES_STR(""));
    (void)gates_checkbox_set_checked(tree, d->news, false);
    (void)gates_checkbox_set_checked(tree, d->terms, false);
    (void)gates_command_set_enabled(tree, gates_tree_root(tree), CMD_SUBMIT, false);
    gates_tree_set_focus(tree, d->name);
    set_status(d, "cleared");
}

static gates_err_t row(gates_tree_t *t, gates_node_t parent, gates_node_t *out) {
    TRY(gates_panel_create(t, parent, out));
    TRY(gates_layout_set(t, *out, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, *out, 8));
    return GATES_OK;
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 6));

    gates_command_desc_t submit = { .id = CMD_SUBMIT, .label = GATES_STR("Submit (Enter)"),
                                    .role = GATES_COMMAND_DEFAULT, .enabled = false,
                                    .invoke = on_command, .user = d };
    gates_command_desc_t quit = { .id = CMD_QUIT, .label = GATES_STR("Quit (Esc)"),
                                  .role = GATES_COMMAND_CANCEL, .enabled = true,
                                  .invoke = on_command, .user = d };
    TRY(gates_command_register(t, root, &submit));
    TRY(gates_command_register(t, root, &quit));

    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Keyboard only: Tab, Shift+Tab, Space, Enter, Esc"),
                           &n));
    TRY(gates_label_create(t, root, GATES_STR("Name"), &n));
    TRY(gates_textbox_create(t, root, GATES_STR(""), 30, &d->name));
    TRY(gates_node_set_labelled_by(t, d->name, n));
    TRY(gates_label_create(t, root, GATES_STR("Email"), &n));
    TRY(gates_textbox_create(t, root, GATES_STR(""), 30, &d->email));
    TRY(gates_node_set_labelled_by(t, d->email, n));
    TRY(gates_checkbox_create(t, root, GATES_STR("Send me the newsletter"), false, nullptr,
                              nullptr, &d->news));
    TRY(gates_checkbox_create(t, root, GATES_STR("I accept the terms"), false, nullptr, nullptr,
                              &d->terms));
    TRY(gates_widget_set_handler(t, d->terms, on_terms, d));

    TRY(gates_label_create(t, root, GATES_STR("More options (scrolls to follow focus)"), &n));
    gates_node_t scroll = GATES_NODE_NULL;
    TRY(gates_panel_create(t, root, &scroll));
    TRY(gates_layout_set(t, scroll, GATES_LAYOUT_KIND_SCROLL));
    TRY(gates_layout_set_child_grow(t, scroll, 1));
    for (int i = 0; i < 30; i++) {
        char buf[32];
        int len = snprintf(buf, sizeof buf, "extra option %02d", i + 1);
        TRY(gates_checkbox_create(t, scroll,
                                  (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                 .size = len > 0 ? (gates_usize_t)len : 0 },
                                  false, nullptr, nullptr, &n));
    }

    gates_node_t buttons = GATES_NODE_NULL, b = GATES_NODE_NULL;
    TRY(row(t, root, &buttons));
    TRY(gates_button_create(t, buttons, GATES_STR(""), nullptr, nullptr, &b));
    TRY(gates_button_set_command(t, b, root, CMD_SUBMIT));
    TRY(gates_button_create(t, buttons, GATES_STR("Reset"), nullptr, nullptr, &d->reset));
    TRY(gates_widget_set_handler(t, d->reset, on_reset, d));
    TRY(gates_button_create(t, buttons, GATES_STR(""), nullptr, nullptr, &b));
    TRY(gates_button_set_command(t, b, root, CMD_QUIT));

    TRY(gates_label_create(t, root, GATES_STR("tick the terms, then press Enter"), &d->status));
    gates_tree_set_focus(t, d->name);
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t d = { .app = app };
    gates_window_desc_t desc = { .title = GATES_STR("gates: keyboard"), .size = { 480, 560 } };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(app, &desc, nullptr, &win))) {
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
