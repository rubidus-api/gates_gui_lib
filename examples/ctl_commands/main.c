/* ctl_commands - one command model for buttons and shortcuts.
 *
 * Interaction: the application offers actions as commands - a label, an
 * optional shortcut, and enabled/checked state it keeps current. Buttons bound
 * to a command show its label, look inert while it is disabled, and run it;
 * the shortcut runs the same command. Invocations are delivered after the
 * input message and the command is re-checked, so a disabled command never
 * runs. Shortcuts need Ctrl (or are F-keys); typed letters stay text, and the
 * focused textbox keeps its own keys (Ctrl+Z is its undo, not the app's).
 *
 * Commands: New (Ctrl+N) clears the note; Save (Ctrl+S) is enabled only after
 * the note changes; Wrap (Ctrl+W) toggles a checked state shown in its label;
 * Refresh (F5) counts; Quit is the cancel command (Escape).
 *
 * A context menu offers the same commands (right-click, or Shift+F10 from the
 * keyboard): it shows their labels, the Wrap check mark and the shortcuts, greys
 * out Save while it is disabled, and runs the chosen command through the same
 * path. Up/Down/Enter choose, Escape or a click outside closes it.
 *
 * Field check (T026): Save is grey until the note is edited, then Ctrl+S or the
 * button saves and greys it again; Ctrl+N clears; Ctrl+W flips "Wrap: on/off";
 * F5 counts; typing "s" in the note types an s; right-click or Shift+F10 opens
 * the menu with the same state; choosing Refresh counts; Escape quits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/command.h>
#include <gates/overlay.h>
#include <gates/ui.h>
#include <gates/access.h>

#include <stdio.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

enum { CMD_NEW = 1, CMD_SAVE, CMD_WRAP, CMD_REFRESH, CMD_QUIT, CMD_MENU };

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t note, status;
    int saves, refreshes;
} demo_t;

static void say(demo_t *d, const char *fmt, int a) {
    char buf[96];
    int n = snprintf(buf, sizeof buf, fmt, a);
    (void)gates_widget_set_text(d->tree, d->status,
                                (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                               .size = n > 0 ? (gates_usize_t)n : 0 });
}

static const gates_command_id_t menu_ids[] = { CMD_NEW, CMD_SAVE, 0, CMD_WRAP, CMD_REFRESH };

static void open_menu(demo_t *d, gates_point_t at) {
    gates_node_t m = GATES_NODE_NULL;
    if (!gates_is_ok(gates_menu_open(d->tree, at, gates_tree_root(d->tree), menu_ids,
                                     sizeof menu_ids / sizeof menu_ids[0], &m))) {
        (void)gates_widget_set_text(d->tree, d->status, GATES_STR("could not open the menu"));
    }
}

/* Right button: the context menu at the pointer. */
static void on_pointer(gates_window_t *win, const gates_pointer_event_t *ev, void *user) {
    (void)win;
    if (ev->action == GATES_POINTER_DOWN && ev->button == GATES_BUTTON_RIGHT) {
        open_menu(user, ev->pos);
    }
}

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    demo_t *d = user;
    gates_node_t root = gates_tree_root(tree);
    switch (id) {
    case CMD_NEW:
        (void)gates_textbox_set_text(tree, d->note, GATES_STR(""));
        (void)gates_command_set_enabled(tree, root, CMD_SAVE, false);
        say(d, "new note (%d saves so far)", d->saves);
        break;
    case CMD_SAVE:
        (void)gates_command_set_enabled(tree, root, CMD_SAVE, false);
        say(d, "saved (%d)", ++d->saves);
        break;
    case CMD_WRAP: {
        bool on = !gates_command_checked(tree, root, CMD_WRAP);
        (void)gates_command_set_checked(tree, root, CMD_WRAP, on);
        (void)gates_command_set_label(tree, root, CMD_WRAP,
                                      on ? GATES_STR("Wrap: on") : GATES_STR("Wrap: off"));
        (void)gates_widget_set_text(tree, d->status, on ? GATES_STR("wrap on")
                                                        : GATES_STR("wrap off"));
        break;
    }
    case CMD_REFRESH:
        say(d, "refreshed %d times", ++d->refreshes);
        break;
    case CMD_QUIT:
        gates_app_quit(d->app);
        break;
    case CMD_MENU: { /* Shift+F10: the menu under the note */
        gates_rect_t r = gates_node_layout_rect(tree, d->note);
        open_menu(d, (gates_point_t){ r.x + 8, r.y + r.h });
        break;
    }
    default:
        break;
    }
}

static void on_note(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)user;
    if (ev->kind == GATES_EVENT_TEXT_CHANGED) {
        (void)gates_command_set_enabled(tree, gates_tree_root(tree), CMD_SAVE, true);
    }
}

static gates_err_t add(demo_t *d, gates_command_id_t id, const char *label, gates_u8 letter,
                       gates_key_t key, gates_command_role_t role, bool enabled) {
    gates_usize_t n = 0;
    while (label[n] != '\0') n++;
    gates_command_desc_t c = {
        .id = id,
        .label = { .ptr = (const gates_u8 *)label, .size = n },
        .shortcut = { .key = key, .letter = letter, .ctrl = letter != 0 },
        .role = role,
        .enabled = enabled,
        .invoke = on_command,
        .user = d,
    };
    return gates_command_register(d->tree, gates_tree_root(d->tree), &c);
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));

    TRY(add(d, CMD_NEW, "New (Ctrl+N)", 'N', GATES_KEY_NONE, GATES_COMMAND_NORMAL, true));
    TRY(add(d, CMD_SAVE, "Save (Ctrl+S)", 'S', GATES_KEY_NONE, GATES_COMMAND_NORMAL, false));
    TRY(add(d, CMD_WRAP, "Wrap: off", 'W', GATES_KEY_NONE, GATES_COMMAND_NORMAL, true));
    TRY(add(d, CMD_REFRESH, "Refresh (F5)", 0, GATES_KEY_F5, GATES_COMMAND_NORMAL, true));
    TRY(add(d, CMD_QUIT, "Quit (Esc)", 0, GATES_KEY_NONE, GATES_COMMAND_CANCEL, true));
    gates_command_desc_t menu = { .id = CMD_MENU, .label = GATES_STR("Menu"),
                                  .shortcut = { .key = GATES_KEY_F10, .shift = true },
                                  .enabled = true, .invoke = on_command, .user = d };
    TRY(gates_command_register(t, root, &menu));

    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Commands: buttons and shortcuts share them"), &n));
    TRY(gates_label_create(t, root, GATES_STR("Note"), &n));
    TRY(gates_textbox_create(t, root, GATES_STR(""), 36, &d->note));
    TRY(gates_node_set_labelled_by(t, d->note, n));
    TRY(gates_widget_set_handler(t, d->note, on_note, d));

    gates_node_t bar = GATES_NODE_NULL;
    TRY(gates_panel_create(t, root, &bar));
    TRY(gates_layout_set(t, bar, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, bar, 6));
    const gates_command_id_t ids[] = { CMD_NEW, CMD_SAVE, CMD_WRAP, CMD_REFRESH, CMD_QUIT };
    for (int i = 0; i < 5; i++) {
        gates_node_t b = GATES_NODE_NULL;
        TRY(gates_button_create(t, bar, GATES_STR(""), nullptr, nullptr, &b));
        TRY(gates_button_set_command(t, b, root, ids[i]));
    }
    TRY(gates_label_create(t, root, GATES_STR("type in the note, then try the shortcuts"),
                           &d->status));
    gates_tree_set_focus(t, d->note);
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t d = { .app = app };
    gates_window_desc_t desc = { .title = GATES_STR("gates: commands"), .size = { 620, 320 } };
    gates_window_callbacks_t cb = { .on_pointer = on_pointer, .user_data = &d };
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
