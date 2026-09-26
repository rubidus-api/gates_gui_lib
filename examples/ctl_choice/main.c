/* ctl_choice - the choice (dropdown) control and a field's error state.
 *
 * Interaction: a choice shows one selected option and opens a list of all of
 * them on demand, for longer lists than a radio group suits. Its list follows
 * the menu's rules: arrows move, Enter or Space chooses, Escape or a click
 * outside only closes. The application hears GATES_EVENT_VALUE_CHANGED with
 * ev->result = the chosen option id. A textbox can show that its value was
 * refused (gates_textbox_set_invalid); checking the value is the
 * application's job.
 *
 * Shows: a language choice with one disabled option; "Other" reveals a
 * hidden row with a textbox that is marked invalid while it is empty; a
 * summary kept current by the events.
 *
 * Field check (T027 examples): Tab to the choice, Space/Enter/Alt+Down or a
 * click opens the list under it; Up/Down + Enter picks; "Latin" cannot be
 * picked; Escape and an outside click close the list without a change;
 * picking "Other" shows the name field with a red border until something is
 * typed; picking another language hides it again; ESC (list closed) exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/ui.h>
#include <gates/access.h>

#include <stdio.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

enum { LANG_EN = 1, LANG_KO, LANG_JA, LANG_DE, LANG_FR, LANG_LA, LANG_OTHER };

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t lang, other_row, other_name, summary;
} demo_t;

static const gates_option_t languages[] = {
    { .id = LANG_EN, .label = GATES_STR_INIT("English") },
    { .id = LANG_KO, .label = GATES_STR_INIT("Korean") },
    { .id = LANG_JA, .label = GATES_STR_INIT("Japanese") },
    { .id = LANG_DE, .label = GATES_STR_INIT("German") },
    { .id = LANG_FR, .label = GATES_STR_INIT("French") },
    { .id = LANG_LA, .label = GATES_STR_INIT("Latin"), .disabled = true },
    { .id = LANG_OTHER, .label = GATES_STR_INIT("Other...") },
};
#define LANG_COUNT (sizeof languages / sizeof languages[0])

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    /* Only keys the tree did not use arrive here: Escape that closed a list does not. */
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

static void update(demo_t *d) {
    gates_u32 id = gates_options_selected(d->tree, d->lang);
    gates_str_t name = GATES_STR("none");
    for (gates_u32 i = 0; i < LANG_COUNT; i++) {
        if (languages[i].id == id) {
            name = languages[i].label;
        }
    }
    gates_str_t other = gates_textbox_text(d->tree, d->other_name);
    char buf[160];
    if (id == LANG_OTHER) {
        name = other.size > 0 ? other : GATES_STR("(name it)");
    }
    int n = snprintf(buf, sizeof buf, "language: %.*s", (int)name.size, (const char *)name.ptr);
    if (n > 0) {
        (void)gates_widget_set_text(d->tree, d->summary,
                                    (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                   .size = (gates_usize_t)n });
    }
    (void)gates_node_set_hidden(d->tree, d->other_row, id != LANG_OTHER);
    (void)gates_textbox_set_invalid(d->tree, d->other_name, id == LANG_OTHER && other.size == 0);
}

static void on_lang(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    if (ev->kind == GATES_EVENT_VALUE_CHANGED) {
        update(user);
    }
}

static void on_name(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    if (ev->kind == GATES_EVENT_TEXT_CHANGED) {
        update(user);
    }
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));

    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Choice: pick one option from a list"), &n));
    TRY(gates_choice_create(t, root, languages, (gates_u32)LANG_COUNT, LANG_EN, &d->lang));
    TRY(gates_node_set_access_name(t, d->lang, GATES_STR("Language")));
    TRY(gates_layout_set_child_align(t, d->lang, GATES_ALIGN_START_V));
    TRY(gates_widget_set_handler(t, d->lang, on_lang, d));

    TRY(gates_panel_create(t, root, &d->other_row));
    TRY(gates_layout_set(t, d->other_row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, d->other_row, 8));
    TRY(gates_label_create(t, d->other_row, GATES_STR("name:"), &n));
    TRY(gates_textbox_create(t, d->other_row, GATES_STR(""), 20, &d->other_name));
    TRY(gates_node_set_labelled_by(t, d->other_name, n));
    TRY(gates_widget_set_handler(t, d->other_name, on_name, d));

    TRY(gates_separator_create(t, root, &n));
    TRY(gates_label_create(t, root, GATES_STR(""), &d->summary));
    TRY(gates_label_create(t, root, GATES_STR("ESC closes the list, then exits"), &n));
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
    gates_window_desc_t desc = { .title = GATES_STR("gates: choice"), .size = { 460, 360 } };
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
