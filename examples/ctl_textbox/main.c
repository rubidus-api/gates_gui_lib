/* ctl_textbox - the single-line textbox control.
 *
 * Interaction: a textbox lets the person enter one line of text, with the
 * keyboard or through the installed IME (Korean included). The application
 * hears GATES_EVENT_TEXT_CHANGED for committed text and, separately,
 * GATES_EVENT_PREEDIT_CHANGED while a syllable is being composed, so it never
 * mistakes an unfinished syllable for input. Replacing the text from code while
 * a composition is open returns BUSY instead of guessing a result.
 *
 * Everyday editing works in every field: Ctrl+C / Ctrl+X / Ctrl+V through the
 * Windows clipboard (pasted line breaks become spaces), Ctrl+Z / Ctrl+Y undo
 * and redo. A field can be read-only (select and copy only), limited to a
 * number of UTF-8 bytes, or a password field (stars, no copy, no IME).
 *
 * When input does not fit a limited field, the field refuses it and reports
 * GATES_EVENT_LIMIT_EXCEEDED; the application asks the person whether to
 * insert the part that fits (gates_textbox_accept_fit) or drop it.
 *
 * Shows: two fields with live echoes, the open composition, a "clear" button
 * that handles BUSY, a disabled field, a 10-byte code field with its question,
 * a password field whose echo shows only its length, and a read-only field.
 *
 * Field check (T021, T023): typing updates the echo under each field; Korean
 * shows an underlined syllable in the box and on the composing line, and
 * reaches the echo only when committed; "clear" empties the name field, or
 * reports that a composition is open; the disabled field cannot be focused;
 * copy/paste work with other applications; Ctrl+Z/Y undo and redo; pasting
 * too much into "code" asks, and "insert" keeps what fits; the password field
 * shows stars and refuses copy; the read-only field copies but cannot change;
 * ESC exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/ui.h>
#include <gates/access.h>

#include <stdio.h>
#include <string.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t name, name_echo;
    gates_node_t city, city_echo;
    gates_node_t composing;
    gates_node_t clear_btn;
    gates_node_t status;
    gates_node_t code, question, insert_btn, discard_btn;
    gates_node_t secret, secret_echo;
} demo_t;

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

/* "prefix" + text into a fixed buffer (truncated at a byte limit is fine for a
 * demo: the backend draws a replacement box for a cut sequence). */
static gates_str_t join(gates_u8 *buf, gates_usize_t cap, const char *prefix, gates_str_t text) {
    gates_usize_t p = strlen(prefix);
    gates_usize_t n = text.size;
    if (p + n > cap) {
        n = cap - p;
    }
    memcpy(buf, prefix, p);
    if (n > 0) {
        memcpy(buf + p, text.ptr, n);
    }
    return (gates_str_t){ .ptr = buf, .size = p + n };
}

static void on_field(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    gates_u8 buf[200];
    bool is_name = ev->source.index == d->name.index;
    if (ev->kind == GATES_EVENT_TEXT_CHANGED) {
        (void)gates_widget_set_text(tree, is_name ? d->name_echo : d->city_echo,
                                    join(buf, sizeof buf, is_name ? "name: " : "city: ",
                                         ev->text));
    } else if (ev->kind == GATES_EVENT_PREEDIT_CHANGED) {
        (void)gates_widget_set_text(tree, d->composing,
                                    ev->text.size > 0
                                        ? join(buf, sizeof buf, "composing: ", ev->text)
                                        : GATES_STR("composing: (none)"));
    }
}

static gates_str_t fmt(char *buf, gates_usize_t cap, const char *f, unsigned a, unsigned b) {
    int n = snprintf(buf, cap, f, a, b);
    return (gates_str_t){ .ptr = (const gates_u8 *)buf, .size = n > 0 ? (gates_usize_t)n : 0 };
}

static void ask(demo_t *d, bool open) {
    (void)gates_widget_set_disabled(d->tree, d->insert_btn, !open);
    (void)gates_widget_set_disabled(d->tree, d->discard_btn, !open);
    if (!open) {
        (void)gates_widget_set_text(d->tree, d->question, GATES_STR(""));
    }
}

/* The code field refused input that does not fit: ask the person. */
static void on_code(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    char buf[128];
    if (ev->kind == GATES_EVENT_LIMIT_EXCEEDED) {
        (void)gates_widget_set_text(tree, d->question,
                                    fmt(buf, sizeof buf,
                                        "Too long: %u bytes fit (limit %u). Insert what fits?",
                                        ev->fit_bytes, ev->limit));
        ask(d, true);
    } else if (ev->kind == GATES_EVENT_TEXT_CHANGED) {
        ask(d, false); /* the text moved on: the old question no longer applies */
    }
}

static void on_answer(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    if (ev->source.index == d->insert_btn.index) {
        if (!gates_is_ok(gates_textbox_accept_fit(tree, d->code))) {
            (void)gates_widget_set_text(tree, d->status, GATES_STR("that input is gone"));
        }
    } else {
        gates_textbox_discard_rejected(tree, d->code);
    }
    ask(d, false);
}

/* The password's value never goes to the screen: only its size. */
static void on_secret(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    if (ev->kind != GATES_EVENT_TEXT_CHANGED) {
        return;
    }
    gates_usize_t n = 0;
    char buf[64];
    if (gates_is_ok(gates_textbox_copy_text(tree, d->secret, nullptr, 0, &n))) {
        int len = snprintf(buf, sizeof buf, "password: %u bytes", (unsigned)n);
        (void)gates_widget_set_text(tree, d->secret_echo,
                                    (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                   .size = len > 0 ? (gates_usize_t)len : 0 });
    }
}

static void on_clear(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)ev;
    demo_t *d = user;
    gates_err_t err = gates_textbox_set_text(tree, d->name, GATES_STR(""));
    if (err == PROVEN_ERR_BUSY) {
        (void)gates_widget_set_text(tree, d->status,
                                    GATES_STR("finish or cancel the composition first"));
        return;
    }
    if (gates_is_ok(err)) {
        /* The setter is silent; announce it so the echo follows. */
        (void)gates_widget_notify(tree, d->name);
        (void)gates_widget_set_text(tree, d->status, GATES_STR("cleared"));
    }
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 6));

    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Textbox: enter one line (keyboard or IME)"), &n));
    TRY(gates_label_create(t, root, GATES_STR("Name"), &n));
    TRY(gates_textbox_create(t, root, GATES_STR(""), 30, &d->name));
    TRY(gates_node_set_labelled_by(t, d->name, n));
    TRY(gates_label_create(t, root, GATES_STR("name: "), &d->name_echo));
    TRY(gates_label_create(t, root, GATES_STR("City"), &n));
    TRY(gates_textbox_create(t, root, GATES_STR("Seoul"), 30, &d->city));
    TRY(gates_node_set_labelled_by(t, d->city, n));
    TRY(gates_label_create(t, root, GATES_STR("city: Seoul"), &d->city_echo));
    TRY(gates_label_create(t, root, GATES_STR("composing: (none)"), &d->composing));
    TRY(gates_widget_set_handler(t, d->name, on_field, d));
    TRY(gates_widget_set_handler(t, d->city, on_field, d));

    gates_node_t off = GATES_NODE_NULL;
    TRY(gates_textbox_create(t, root, GATES_STR("disabled field"), 30, &off));
    TRY(gates_node_set_access_name(t, off, GATES_STR("Disabled example")));
    TRY(gates_widget_set_disabled(t, off, true));

    TRY(gates_button_create(t, root, GATES_STR("clear name"), nullptr, nullptr, &d->clear_btn));
    TRY(gates_widget_set_handler(t, d->clear_btn, on_clear, d));
    TRY(gates_label_create(t, root, GATES_STR(""), &d->status));

    TRY(gates_label_create(t, root, GATES_STR("Code (at most 10 bytes)"), &n));
    TRY(gates_textbox_create(t, root, GATES_STR(""), 30, &d->code));
    TRY(gates_node_set_labelled_by(t, d->code, n));
    TRY(gates_textbox_set_max_bytes(t, d->code, 10));
    TRY(gates_widget_set_handler(t, d->code, on_code, d));
    TRY(gates_label_create(t, root, GATES_STR(""), &d->question));
    gates_node_t row = GATES_NODE_NULL;
    TRY(gates_panel_create(t, root, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    TRY(gates_button_create(t, row, GATES_STR("insert what fits"), nullptr, nullptr,
                            &d->insert_btn));
    TRY(gates_button_create(t, row, GATES_STR("discard"), nullptr, nullptr, &d->discard_btn));
    TRY(gates_widget_set_handler(t, d->insert_btn, on_answer, d));
    TRY(gates_widget_set_handler(t, d->discard_btn, on_answer, d));
    ask(d, false);

    TRY(gates_label_create(t, root, GATES_STR("Password"), &n));
    TRY(gates_textbox_create(t, root, GATES_STR(""), 30, &d->secret));
    TRY(gates_node_set_labelled_by(t, d->secret, n));
    TRY(gates_textbox_set_password(t, d->secret, true));
    TRY(gates_widget_set_handler(t, d->secret, on_secret, d));
    TRY(gates_label_create(t, root, GATES_STR("password: 0 bytes"), &d->secret_echo));

    gates_node_t ro = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Read-only (select and copy)"), &n));
    TRY(gates_textbox_create(t, root, GATES_STR("copy me: 복사해 보세요"), 30, &ro));
    TRY(gates_node_set_labelled_by(t, ro, n));
    TRY(gates_textbox_set_read_only(t, ro, true));
    TRY(gates_label_create(t, root, GATES_STR("ESC exits"), &n));
    gates_tree_set_focus(t, d->name);
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t d = { .app = app };
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &d };
    gates_window_desc_t desc = { .title = GATES_STR("gates: textbox"), .size = { 480, 720 } };
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
