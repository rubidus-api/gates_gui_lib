/* text_demo — Phase 3A: real glyphs plus a working textbox.
 *
 * Shows the point of RFC-0002: the same layout, measured in cells, rendered
 * by the platform's own font. Hangul and Latin sit on the same cell grid
 * (wide characters take two cells) — the library never asks the font where
 * the next character goes.
 *
 * Manual checklist (T017):
 *   - Hangul, Latin and CJK render as real glyphs (not boxes);
 *   - the ruler line and the sample lines stay column-aligned;
 *   - clicking the textbox focuses it (border turns to the focus colour) and
 *     places the caret at the clicked cell;
 *   - typing, Backspace/Delete, Home/End, Shift+arrows and Ctrl+A work;
 *   - dragging inside the textbox selects a range;
 *   - the mirror label under the box follows what you type;
 *   - typing past the right edge scrolls the view and the caret stays visible;
 *   - ESC exits.
 *
 * Korean IME composition (plan-0006, T019): the syllable being composed is
 * drawn inline and underlined; the mirror shows committed text only and the
 * line under it shows the open preedit, so commit and composition can be
 * told apart on screen.
 */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/ui.h>
#include <gates/event.h>
#include <gates/access.h>

#include <string.h>

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t box;
    gates_node_t mirror;
    gates_node_t compose;
} demo_t;

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

/* The mirror and the composing line follow the box through its events, not by
 * polling it during paint (RFC-0003 section 4.1). */
static void box_changed(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    if (ev->kind == GATES_EVENT_TEXT_CHANGED) {
        (void)gates_widget_set_text(tree, d->mirror, ev->text);
    } else if (ev->kind == GATES_EVENT_PREEDIT_CHANGED) {
        gates_u8 buf[256];
        gates_str_t line = GATES_STR("composing: (none)");
        if (ev->text.size > 0 && ev->text.size + 11 <= sizeof buf) {
            memcpy(buf, "composing: ", 11);
            memcpy(buf + 11, ev->text.ptr, ev->text.size);
            line = (gates_str_t){ .ptr = buf, .size = ev->text.size + 11 };
        }
        (void)gates_widget_set_text(tree, d->compose, line);
    }
}

static void build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    (void)gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN);
    (void)gates_layout_set_padding(t, root, 12);
    (void)gates_layout_set_gap(t, root, 6);

    gates_node_t title = GATES_NODE_NULL;
    (void)gates_label_create(t, root, GATES_STR("gates text_demo — RFC-0002"), &title);

    /* A ruler plus samples: every line must stay column-aligned because the
     * library places cells itself. */
    gates_node_t ruler = GATES_NODE_NULL, l1 = GATES_NODE_NULL, l2 = GATES_NODE_NULL,
                 l3 = GATES_NODE_NULL;
    (void)gates_label_create(t, root, GATES_STR("0123456789012345678901234567"), &ruler);
    (void)gates_label_create(t, root, GATES_STR("ABCD한글EFGH가나다ABCD"), &l1);
    (void)gates_label_create(t, root, GATES_STR("....한국어 텍스트 렌더링...."), &l2);
    (void)gates_label_create(t, root, GATES_STR("漢字 かな ABC 123 !@#"), &l3);

    gates_node_t hint = GATES_NODE_NULL;
    (void)gates_label_create(t, root, GATES_STR("edit below (click to focus):"), &hint);
    (void)gates_textbox_create(t, root, GATES_STR("type here"), 28, &d->box);
    (void)gates_node_set_labelled_by(t, d->box, hint);
    (void)gates_label_create(t, root, GATES_STR("type here"), &d->mirror);
    (void)gates_label_create(t, root, GATES_STR("composing: (none)"), &d->compose);
    (void)gates_widget_set_handler(t, d->box, box_changed, d);
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t d = { .app = app };

    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &d };
    gates_window_desc_t desc = {
        .title = GATES_STR("gates text_demo"),
        .size = { 560, 340 },
    };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(app, &desc, &cb, &win))) {
        gates_app_destroy(app);
        return 1;
    }
    d.tree = gates_window_tree(win);
    build_ui(&d);
    gates_window_request_repaint(win);

    gates_err_t err = gates_app_run(app);
    gates_window_destroy(win);
    gates_app_destroy(app);
    return gates_is_ok(err) ? 0 : 1;
}
