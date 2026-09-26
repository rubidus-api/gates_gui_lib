/* ctl_stack - the stack layout (one page visible at a time).
 *
 * Interaction: a stack holds several pages in the same place and shows exactly
 * one; only the visible page is painted and can be clicked. The program picks
 * the page, here from three buttons, the way a tab strip or a wizard would.
 *
 * Shows: three pages with different content (text, an option, a field) and
 * page buttons whose events switch the active page.
 *
 * Field check (T021): only one page is visible; each button shows its page and
 * the "page N of 3" line follows; the checkbox on page 2 keeps its state after
 * switching away and back; nothing on a hidden page reacts to clicks; ESC exits. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/ui.h>
#include <gates/access.h>

#include <stdio.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)
#define PAGES 3

typedef struct demo_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t stack;
    gates_node_t page_btn[PAGES];
    gates_node_t where;
} demo_t;

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    demo_t *d = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) {
        gates_app_quit(d->app);
    }
}

static void show_where(demo_t *d) {
    char buf[48];
    int n = snprintf(buf, sizeof buf, "page %u of %d",
                     gates_layout_stack_active(d->tree, d->stack) + 1u, PAGES);
    if (n > 0) {
        (void)gates_widget_set_text(d->tree, d->where,
                                    (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                   .size = (gates_usize_t)n });
    }
}

static void on_page(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    demo_t *d = user;
    for (gates_u32 i = 0; i < PAGES; i++) {
        if (ev->source.index == d->page_btn[i].index) {
            (void)gates_layout_set_stack_active(tree, d->stack, i);
        }
    }
    show_where(d);
}

static gates_err_t page(gates_tree_t *t, gates_node_t stack, gates_node_t *out) {
    TRY(gates_panel_create(t, stack, out));
    TRY(gates_layout_set(t, *out, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, *out, 8));
    TRY(gates_layout_set_gap(t, *out, 6));
    return GATES_OK;
}

static gates_err_t build_ui(demo_t *d) {
    gates_tree_t *t = d->tree;
    gates_node_t root = gates_tree_root(t);
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));

    gates_node_t n = GATES_NODE_NULL;
    TRY(gates_label_create(t, root, GATES_STR("Stack: one page at a time"), &n));
    gates_node_t row = GATES_NODE_NULL;
    TRY(gates_panel_create(t, root, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    static const char *const titles[PAGES] = { "general", "options", "about you" };
    for (int i = 0; i < PAGES; i++) {
        gates_usize_t len = 0;
        while (titles[i][len] != '\0') len++;
        TRY(gates_button_create(t, row,
                                (gates_str_t){ .ptr = (const gates_u8 *)titles[i], .size = len },
                                nullptr, nullptr, &d->page_btn[i]));
        TRY(gates_widget_set_handler(t, d->page_btn[i], on_page, d));
    }

    TRY(gates_panel_create(t, root, &d->stack));
    TRY(gates_layout_set(t, d->stack, GATES_LAYOUT_KIND_STACK));
    TRY(gates_layout_set_child_grow(t, d->stack, 1));
    gates_node_t p = GATES_NODE_NULL;
    TRY(page(t, d->stack, &p));
    TRY(gates_label_create(t, p, GATES_STR("General settings live on this page."), &n));
    TRY(gates_label_create(t, p, GATES_STR("Pages share one area; only this one is shown."),
                           &n));
    TRY(page(t, d->stack, &p));
    TRY(gates_checkbox_create(t, p, GATES_STR("an option that keeps its state"), false, nullptr,
                              nullptr, &n));
    TRY(page(t, d->stack, &p));
    gates_node_t your = GATES_NODE_NULL;
    TRY(gates_label_create(t, p, GATES_STR("Your name:"), &your));
    TRY(gates_textbox_create(t, p, GATES_STR(""), 24, &n));
    TRY(gates_node_set_labelled_by(t, n, your));

    TRY(gates_label_create(t, root, GATES_STR(""), &d->where));
    TRY(gates_label_create(t, root, GATES_STR("ESC exits"), &n));
    show_where(d);
    return GATES_OK;
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }
    demo_t d = { .app = app };
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &d };
    gates_window_desc_t desc = { .title = GATES_STR("gates: stack"), .size = { 460, 300 } };
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
