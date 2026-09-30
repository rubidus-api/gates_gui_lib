/* app_settings - reference application: a settings screen built from a form.
 *
 * Interaction: the person edits a draft of their settings and either saves it
 * (Enter, or the Save button) or throws it away (Escape, or Cancel). The
 * library shows the fields; this program owns the values: it keeps the saved
 * settings, checks the draft on Save, puts messages on the fields that are
 * wrong and moves the focus to the first of them, and only then stores the
 * draft. Nothing is validated or stored by gates itself.
 *
 * Shows: a form with text fields (name, e-mail with a rule, proxy address),
 * a language choice, a theme radio group and a checkbox that reveals the
 * proxy row; Save / Cancel as commands shared by buttons and keys; an
 * "applying" step after a valid Save - a progress bar filled by a timer, or at
 * once when the person asked Windows for less motion; a status line.
 *
 * Field check (T029 stage 2): with an empty name and "abc" as e-mail, Enter
 * shows two messages under the fields (the text boxes turn red) and puts the
 * focus in the name box; fixing them and pressing Enter saves ("saved: ...");
 * ticking "use a proxy server" shows the proxy row (Tab reaches it), unticking
 * hides it; Save with the proxy on and no address complains about it only;
 * editing a field clears its message; Escape restores the last saved values;
 * a valid Save fills the progress bar in about half a second ("saving..."),
 * then says "saved: ..." (with animations turned off in Windows: at once);
 * Save and Cancel wait while it runs; the window's close
 * button exits. A window narrower than the label column plus 12 average characters puts
 * each label above its field. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/command.h>
#include <gates/form.h>
#include <gates/ui.h>
#include <gates/access.h>
#include <gates/timer.h>

#include <stdio.h>
#include <string.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)
#define TEXT_MAX 64

enum { F_NAME = 1, F_MAIL, F_LANG, F_THEME, F_PROXY, F_PROXY_ADDR };
enum { CMD_SAVE = 1, CMD_CANCEL };
enum { LANG_EN = 1, LANG_KO, LANG_JA, LANG_DE };
enum { THEME_LIGHT = 1, THEME_DARK };
enum { APPLY_STEPS = 20, APPLY_TICK_MS = 30 }; /* the applying step: about 0.6 s */

/* The application's own model: what is saved. The draft lives in the editors. */
typedef struct settings_t {
    char name[TEXT_MAX];
    char mail[TEXT_MAX];
    gates_u32 lang;
    gates_u32 theme;
    bool proxy;
    char proxy_addr[TEXT_MAX];
} settings_t;

typedef struct app_t {
    gates_app_t *app;
    gates_window_t *win;
    gates_tree_t *tree;
    gates_node_t form, name, mail, lang, theme, proxy, proxy_addr;
    gates_node_t progress, status;
    settings_t saved;
    settings_t draft;            /* checked, being applied */
    gates_timer_id_t apply;      /* the applying step's timer, 0 = none */
    int apply_step;
    bool loading;                /* set while the program fills the editors */
} app_t;

static const gates_option_t languages[] = {
    { .id = LANG_EN, .label = GATES_STR_INIT("English") },
    { .id = LANG_KO, .label = GATES_STR_INIT("Korean") },
    { .id = LANG_JA, .label = GATES_STR_INIT("Japanese") },
    { .id = LANG_DE, .label = GATES_STR_INIT("German") },
};

static const gates_option_t themes[] = {
    { .id = THEME_LIGHT, .label = GATES_STR_INIT("Light") },
    { .id = THEME_DARK, .label = GATES_STR_INIT("Dark") },
};

static gates_str_t cstr(const char *s) {
    return (gates_str_t){ .ptr = (const gates_u8 *)s, .size = strlen(s) };
}

static void set_status(app_t *a, const char *text) {
    (void)gates_widget_set_text(a->tree, a->status, cstr(text));
}

/* Input the tree could not complete (usually out of memory): say so (RFC-0003 section 13). */
static void on_input_error(gates_window_t *win, gates_err_t err, void *user) {
    (void)win;
    set_status(user, err == PROVEN_ERR_NOMEM ? "Out of memory: the last change was not made" : "The last change failed");
}

/* Copies a text editor's value into buf (cut to fit, always terminated). */
static void read_text(app_t *a, gates_node_t box, char *buf, size_t cap) {
    gates_str_t s = gates_textbox_text(a->tree, box);
    size_t n = s.size < cap - 1 ? s.size : cap - 1;
    if (n > 0) memcpy(buf, s.ptr, n);
    buf[n] = '\0';
}

/* -- the rules (the application's, not the library's) --------------------------- */

static bool looks_like_mail(const char *s) {
    const char *at = strchr(s, '@');
    return at != nullptr && at != s && strchr(at + 1, '.') != nullptr &&
           at[1] != '.' && s[strlen(s) - 1] != '.';
}

static bool looks_like_proxy(const char *s) {
    const char *colon = strrchr(s, ':');
    return colon != nullptr && colon != s && colon[1] >= '0' && colon[1] <= '9';
}

/* Puts the saved settings into the editors (silently) and clears messages. */
static void load(app_t *a) {
    a->loading = true;
    const settings_t *s = &a->saved;
    (void)gates_textbox_set_text(a->tree, a->name, cstr(s->name));
    (void)gates_textbox_set_text(a->tree, a->mail, cstr(s->mail));
    (void)gates_options_set_selected(a->tree, a->lang, s->lang);
    (void)gates_options_set_selected(a->tree, a->theme, s->theme);
    (void)gates_checkbox_set_checked(a->tree, a->proxy, s->proxy);
    (void)gates_textbox_set_text(a->tree, a->proxy_addr, cstr(s->proxy_addr));
    (void)gates_form_set_row_hidden(a->tree, a->form, F_PROXY_ADDR, !s->proxy);
    for (gates_u32 id = F_NAME; id <= F_PROXY_ADDR; id++) {
        (void)gates_form_set_error(a->tree, a->form, id, GATES_STR(""));
    }
    a->loading = false;
}

/* -- commands ----------------------------------------------------------------------- */

/* The applying step's end: the draft becomes the saved settings. */
static void finish_save(app_t *a) {
    gates_node_t root = gates_tree_root(a->tree);
    a->saved = a->draft;
    (void)gates_progress_set_value(a->tree, a->progress, 1000);
    (void)gates_command_set_enabled(a->tree, root, CMD_SAVE, true);
    (void)gates_command_set_enabled(a->tree, root, CMD_CANCEL, true);
    const settings_t *d = &a->saved;
    char buf[160];
    snprintf(buf, sizeof buf, "saved: %s, %s, %s theme%s", d->name, d->mail,
             d->theme == THEME_DARK ? "dark" : "light", d->proxy ? ", proxy on" : "");
    set_status(a, buf);
}

static void on_apply_tick(gates_tree_t *tree, gates_node_t node, gates_timer_id_t id, void *user) {
    (void)node;
    app_t *a = user;
    a->apply_step++;
    (void)gates_progress_set_value(tree, a->progress, (gates_i32)(a->apply_step * 1000 / APPLY_STEPS));
    if (a->apply_step >= APPLY_STEPS) {
        (void)gates_timer_cancel(tree, id);
        a->apply = 0;
        finish_save(a);
    }
}

/* A checked draft: shown filling up, unless the person asked for less motion. */
static void apply(app_t *a) {
    gates_node_t root = gates_tree_root(a->tree);
    a->apply_step = 0;
    (void)gates_progress_set_value(a->tree, a->progress, 0);
    if (gates_window_reduced_motion(a->win) ||
        !gates_is_ok(gates_timer_start(a->tree, a->progress, APPLY_TICK_MS, true, on_apply_tick, a, &a->apply))) {
        finish_save(a); /* at once (a timer that cannot start: at once too) */
        return;
    }
    (void)gates_command_set_enabled(a->tree, root, CMD_SAVE, false); /* no second save meanwhile */
    (void)gates_command_set_enabled(a->tree, root, CMD_CANCEL, false);
    set_status(a, "saving...");
}

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    app_t *a = user;
    if (id == CMD_CANCEL) {
        load(a);
        set_status(a, "changes discarded");
        return;
    }
    /* Save: check the whole draft, report every problem, focus the first. */
    settings_t d = {0};
    read_text(a, a->name, d.name, sizeof d.name);
    read_text(a, a->mail, d.mail, sizeof d.mail);
    read_text(a, a->proxy_addr, d.proxy_addr, sizeof d.proxy_addr);
    d.lang = gates_options_selected(tree, a->lang);
    d.theme = gates_options_selected(tree, a->theme);
    d.proxy = gates_checkbox_checked(tree, a->proxy);

    gates_node_t first_bad = GATES_NODE_NULL;
    struct { gates_u32 field; gates_node_t editor; const char *message; } checks[] = {
        { F_NAME, a->name, d.name[0] == '\0' ? "enter your name" : "" },
        { F_MAIL, a->mail, d.mail[0] == '\0'          ? "enter an e-mail address"
                           : !looks_like_mail(d.mail) ? "this is not an e-mail address"
                                                      : "" },
        { F_PROXY_ADDR, a->proxy_addr,
          !d.proxy                        ? ""
          : d.proxy_addr[0] == '\0'       ? "enter host:port"
          : !looks_like_proxy(d.proxy_addr) ? "use the form host:port"
                                          : "" },
    };
    for (size_t i = 0; i < sizeof checks / sizeof checks[0]; i++) {
        (void)gates_form_set_error(tree, a->form, checks[i].field, cstr(checks[i].message));
        if (checks[i].message[0] != '\0' && gates_node_eq(first_bad, GATES_NODE_NULL)) {
            first_bad = checks[i].editor;
        }
    }
    if (!gates_node_eq(first_bad, GATES_NODE_NULL)) {
        gates_tree_set_focus(tree, first_bad);
        set_status(a, "not saved: please correct the marked fields");
        return;
    }
    a->draft = d;
    apply(a);
}

/* -- editors -------------------------------------------------------------------------- */

static void on_editor(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    app_t *a = user;
    if (a->loading || (ev->kind != GATES_EVENT_TEXT_CHANGED && ev->kind != GATES_EVENT_VALUE_CHANGED)) {
        return;
    }
    gates_u32 field = gates_form_field_of(tree, a->form, ev->source);
    if (field == F_PROXY) {
        (void)gates_form_set_row_hidden(tree, a->form, F_PROXY_ADDR, !ev->checked);
        if (!ev->checked) {
            (void)gates_form_set_error(tree, a->form, F_PROXY_ADDR, GATES_STR(""));
        }
    }
    (void)gates_form_set_error(tree, a->form, field, GATES_STR("")); /* edited: message goes */
    set_status(a, "unsaved changes");
}

static gates_err_t build_ui(app_t *a) {
    gates_tree_t *t = a->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 14));
    TRY(gates_layout_set_gap(t, root, 10));

    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Settings"), &n));
    TRY(gates_form_create(t, root, &a->form));

    gates_field_desc_t d = { .label = GATES_STR("Name"), .required = true, .max_bytes = 40 };
    TRY(gates_form_add_text(t, a->form, F_NAME, &d, &a->name));
    d = (gates_field_desc_t){ .label = GATES_STR("E-mail"), .required = true,
                              .help = GATES_STR("for example name@example.org"),
                              .max_bytes = TEXT_MAX - 1 };
    TRY(gates_form_add_text(t, a->form, F_MAIL, &d, &a->mail));
    d = (gates_field_desc_t){ .label = GATES_STR("Language"), .options = languages,
                              .option_count = 4, .selected_id = LANG_EN };
    TRY(gates_form_add_choice(t, a->form, F_LANG, &d, &a->lang));
    d = (gates_field_desc_t){ .label = GATES_STR("Theme"), .options = themes, .option_count = 2,
                              .selected_id = THEME_LIGHT };
    TRY(gates_form_add_radio(t, a->form, F_THEME, &d, &a->theme));
    d = (gates_field_desc_t){ .label = GATES_STR("Network"), .text = GATES_STR("use a proxy server") };
    TRY(gates_form_add_checkbox(t, a->form, F_PROXY, &d, &a->proxy));
    d = (gates_field_desc_t){ .label = GATES_STR("Proxy"), .required = true,
                              .help = GATES_STR("host:port"), .max_bytes = TEXT_MAX - 1 };
    TRY(gates_form_add_text(t, a->form, F_PROXY_ADDR, &d, &a->proxy_addr));
    gates_node_t editors[] = { a->name, a->mail, a->lang, a->theme, a->proxy, a->proxy_addr };
    for (size_t i = 0; i < sizeof editors / sizeof editors[0]; i++) {
        TRY(gates_widget_set_handler(t, editors[i], on_editor, a));
    }

    TRY(gates_separator_create(t, root, &n));
    gates_node_t row = GATES_NODE_NULL;
    TRY(gates_panel_create(t, root, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    TRY(gates_label_create(t, row, GATES_STR("applying"), &n));
    TRY(gates_progress_create(t, row, 0, &a->progress));
    TRY(gates_node_set_labelled_by(t, a->progress, n));
    TRY(gates_layout_set_child_align(t, a->progress, GATES_ALIGN_CENTER_V));

    /* Save and Cancel are commands: the buttons, Enter and Escape share them. */
    gates_command_desc_t save = { .id = CMD_SAVE, .label = GATES_STR("Save"),
                                  .role = GATES_COMMAND_DEFAULT, .enabled = true,
                                  .invoke = on_command, .user = a };
    gates_command_desc_t cancel = { .id = CMD_CANCEL, .label = GATES_STR("Cancel"),
                                    .role = GATES_COMMAND_CANCEL, .enabled = true,
                                    .invoke = on_command, .user = a };
    TRY(gates_command_register(t, root, &save));
    TRY(gates_command_register(t, root, &cancel));
    gates_node_t buttons = GATES_NODE_NULL, b = GATES_NODE_NULL;
    TRY(gates_panel_create(t, root, &buttons));
    TRY(gates_layout_set(t, buttons, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, buttons, 8));
    TRY(gates_button_create(t, buttons, GATES_STR(""), nullptr, nullptr, &b));
    TRY(gates_button_set_command(t, b, root, CMD_SAVE));
    TRY(gates_button_create(t, buttons, GATES_STR(""), nullptr, nullptr, &b));
    TRY(gates_button_set_command(t, b, root, CMD_CANCEL));

    TRY(gates_label_create(t, root, GATES_STR(""), &a->status));
    TRY(gates_node_set_live(t, a->status, GATES_LIVE_POLITE)); /* read out when it changes */
    TRY(gates_label_create(t, root, GATES_STR("Enter saves, Escape discards changes"), &n));

    a->saved = (settings_t){ .lang = LANG_EN, .theme = THEME_LIGHT };
    load(a);
    set_status(a, "nothing saved yet");
    gates_tree_set_focus(t, a->name);
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    app_t a = { .app = app };
    gates_window_desc_t desc = { .title = GATES_STR("gates: settings"), .size = { 560, 520 } };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(app, &desc, &(gates_window_callbacks_t){ .user_data = &a, .on_input_error = on_input_error },
                                         &win))) {
        gates_app_destroy(app);
        return 1;
    }
    a.win = win;
    a.tree = gates_window_tree(win);
    gates_err_t err = build_ui(&a);
    if (!gates_is_ok(err)) {
        fprintf(stderr, "app_settings: building the window failed (%d)\n", (int)err);
    } else {
        gates_window_request_repaint(win);
        err = gates_app_run(app);
    }
    gates_window_destroy(win);
    gates_app_destroy(app);
    return gates_is_ok(err) ? 0 : 1;
}
