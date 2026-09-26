/* ctl_dialog - modal dialogs as overlays inside the window.
 *
 * Interaction: a dialog asks the person something and returns the answer once.
 * Opening it returns at once (no nested event loop); the window behind is
 * dimmed and ignores input; Tab stays inside the dialog; Enter runs the
 * dialog's default command, Escape its cancel command; the window's own
 * shortcuts wait until it closes. The answer arrives as
 * GATES_EVENT_DIALOG_CLOSED with ACCEPTED or CANCELED, and focus returns to
 * where it was.
 *
 * Shows: "Delete all..." asks for confirmation - its OK stays disabled until
 * the person types DELETE; "Rename..." asks for a new name. The status line
 * reports each answer. Escape in the window quits.
 *
 * Field check (T026): each button opens its dialog centred over a dimmed
 * window; clicks on the window behind do nothing; OK in the delete dialog is
 * grey until DELETE is typed; Enter accepts, Escape cancels; the status line
 * shows the result; focus returns to the button that opened the dialog. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/command.h>
#include <gates/overlay.h>
#include <gates/ui.h>
#include <gates/access.h>

#include <stdio.h>
#include <string.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

enum { CMD_QUIT = 1, CMD_OK = 10, CMD_CANCEL = 11 };
enum { ASK_DELETE = 1, ASK_RENAME = 2 };

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t del_btn, ren_btn, status, name_label;
    gates_node_t dialog, field;       /* the open dialog, if any */
    int asking;
    int items;
    char name[64];
} demo_t;

static void set_status(demo_t *d, const char *text) {
    (void)gates_widget_set_text(d->tree, d->status,
                                (gates_str_t){ .ptr = (const gates_u8 *)text, .size = strlen(text) });
}

static void show_state(demo_t *d) {
    char buf[128];
    snprintf(buf, sizeof buf, "document \"%s\" with %d items", d->name, d->items);
    (void)gates_widget_set_text(d->tree, d->name_label,
                                (gates_str_t){ .ptr = (const gates_u8 *)buf, .size = strlen(buf) });
}

/* Dialog commands close the dialog with their answer. */
static void on_dialog_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    demo_t *d = user;
    (void)gates_dialog_close(tree, d->dialog,
                             id == CMD_OK ? GATES_DIALOG_ACCEPTED : GATES_DIALOG_CANCELED);
}

/* The answer, delivered once. */
static void on_closed(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    if (ev->kind != GATES_EVENT_DIALOG_CLOSED) {
        return;
    }
    bool ok = ev->result == GATES_DIALOG_ACCEPTED;
    if (d->asking == ASK_DELETE) {
        if (ok) d->items = 0;
        set_status(d, ok ? "deleted everything" : "delete canceled");
    } else if (ok) {
        gates_str_t t = gates_textbox_text(tree, d->field);
        gates_usize_t n = t.size < sizeof d->name - 1 ? t.size : sizeof d->name - 1;
        memcpy(d->name, t.ptr, n);
        d->name[n] = '\0';
        set_status(d, "renamed");
    } else {
        set_status(d, "rename canceled");
    }
    show_state(d);
    d->asking = 0;
    d->dialog = GATES_NODE_NULL;
}

/* In the delete dialog, OK is enabled only for the exact word. */
static void on_confirm_text(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    if (ev->kind == GATES_EVENT_TEXT_CHANGED) {
        bool match = ev->text.size == 6 && memcmp(ev->text.ptr, "DELETE", 6) == 0;
        (void)gates_command_set_enabled(tree, d->dialog, CMD_OK, match);
    }
}

static gates_err_t ask(demo_t *d, int what) {
    gates_tree_t *t = d->tree;
    gates_node_t content = GATES_NODE_NULL, n = GATES_NODE_NULL;
    gates_dialog_desc_t desc = { .title = what == ASK_DELETE ? GATES_STR("Delete all items?")
                                                             : GATES_STR("Rename document") };
    TRY(gates_dialog_open(t, &desc, &d->dialog, &content));
    d->asking = what;
    TRY(gates_widget_set_handler(t, d->dialog, on_closed, d));
    gates_command_desc_t ok = { .id = CMD_OK, .label = GATES_STR("OK (Enter)"),
                                .role = GATES_COMMAND_DEFAULT, .enabled = what != ASK_DELETE,
                                .invoke = on_dialog_command, .user = d };
    gates_command_desc_t no = { .id = CMD_CANCEL, .label = GATES_STR("Cancel (Esc)"),
                                .role = GATES_COMMAND_CANCEL, .enabled = true,
                                .invoke = on_dialog_command, .user = d };
    TRY(gates_command_register(t, d->dialog, &ok));
    TRY(gates_command_register(t, d->dialog, &no));
    if (what == ASK_DELETE) {
        TRY(gates_label_create(t, content, GATES_STR("This cannot be undone."), &n));
        TRY(gates_label_create(t, content, GATES_STR("Type DELETE to confirm:"), &n));
        TRY(gates_textbox_create(t, content, GATES_STR(""), 20, &d->field));
        TRY(gates_node_set_labelled_by(t, d->field, n));
        TRY(gates_widget_set_handler(t, d->field, on_confirm_text, d));
    } else {
        TRY(gates_label_create(t, content, GATES_STR("New name:"), &n));
        TRY(gates_textbox_create(t, content, (gates_str_t){ .ptr = (const gates_u8 *)d->name,
                                                            .size = strlen(d->name) },
                                 24, &d->field));
        TRY(gates_node_set_labelled_by(t, d->field, n));
    }
    gates_node_t row = GATES_NODE_NULL, b = GATES_NODE_NULL;
    TRY(gates_panel_create(t, content, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    TRY(gates_button_create(t, row, GATES_STR(""), nullptr, nullptr, &b));
    TRY(gates_button_set_command(t, b, d->dialog, CMD_OK));
    TRY(gates_button_create(t, row, GATES_STR(""), nullptr, nullptr, &b));
    TRY(gates_button_set_command(t, b, d->dialog, CMD_CANCEL));
    return GATES_OK;
}

static void on_button(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    demo_t *d = user;
    if (!gates_is_ok(ask(d, gates_node_eq(ev->source, d->del_btn) ? ASK_DELETE : ASK_RENAME))) {
        set_status(d, "could not open the dialog");
    }
}

static void on_quit(gates_tree_t *tree, gates_command_id_t id, void *user) {
    (void)tree;
    (void)id;
    demo_t *d = user;
    gates_app_quit(d->app);
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));
    gates_command_desc_t quit = { .id = CMD_QUIT, .label = GATES_STR("Quit"),
                                  .role = GATES_COMMAND_CANCEL, .enabled = true,
                                  .invoke = on_quit, .user = d };
    TRY(gates_command_register(t, root, &quit));

    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Dialogs: ask, get one answer, carry on"), &n));
    TRY(gates_label_create(t, root, GATES_STR(""), &d->name_label));
    gates_node_t row = GATES_NODE_NULL;
    TRY(gates_panel_create(t, root, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    TRY(gates_button_create(t, row, GATES_STR("Delete all..."), nullptr, nullptr, &d->del_btn));
    TRY(gates_button_create(t, row, GATES_STR("Rename..."), nullptr, nullptr, &d->ren_btn));
    TRY(gates_widget_set_handler(t, d->del_btn, on_button, d));
    TRY(gates_widget_set_handler(t, d->ren_btn, on_button, d));
    TRY(gates_label_create(t, root, GATES_STR("Esc quits (when no dialog is open)"), &d->status));
    show_state(d);
    gates_tree_set_focus(t, d->del_btn);
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t d = { .app = app, .items = 42 };
    strcpy(d.name, "report");
    gates_window_desc_t desc = { .title = GATES_STR("gates: dialog"), .size = { 520, 360 } };
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
