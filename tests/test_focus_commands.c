/* keyboard focus, activation keys and the command model. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/command.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <string.h>

static const gates_text_backend_t *be;
static const gates_theme_t *theme;

/* -- helpers ---------------------------------------------------------------- */

static bool key_ex(gates_tree_t *t, gates_key_t k, bool down, bool ctrl, bool shift, bool alt,
                   gates_u8 letter) {
    gates_key_event_t e = { .key = k, .down = down, .ctrl = ctrl, .shift = shift, .alt = alt,
                            .letter = letter };
    return gates_input_key(t, &e);
}

static bool press_key(gates_tree_t *t, gates_key_t k) {
    return key_ex(t, k, true, false, false, false, 0);
}

static bool tab(gates_tree_t *t, bool back) {
    return key_ex(t, GATES_KEY_TAB, true, false, back, false, 0);
}

static bool focused(const gates_tree_t *t, gates_node_t n) {
    return gates_node_eq(gates_tree_focus(t), n);
}

static gates_point_t center(const gates_tree_t *t, gates_node_t n) {
    gates_rect_t r = gates_node_layout_rect(t, n);
    return (gates_point_t){ r.x + r.w / 2, r.y + r.h / 2 };
}

static void click(gates_tree_t *t, gates_node_t n) {
    gates_pointer_event_t d = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT,
                                .pos = center(t, n) };
    gates_pointer_event_t u = d;
    u.action = GATES_POINTER_UP;
    (void)gates_input_pointer(t, &d);
    (void)gates_input_pointer(t, &u);
}

static void layout(gates_tree_t *t) {
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 400, 300 }, be));
}

typedef struct rec_t {
    gates_event_kind_t kind[16];
    gates_node_t src[16];
    int n;
} rec_t;

static void record(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    rec_t *r = user;
    if (r->n < 16) {
        r->kind[r->n] = ev->kind;
        r->src[r->n] = ev->source;
        r->n++;
    }
}

typedef struct calls_t {
    int count[8];
} calls_t;

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    (void)tree;
    calls_t *c = user;
    if (id < 8) c->count[id]++;
}

/* A form: textbox, checkbox, button, disabled button, a stack with one
 * button per page, then a last button. */
typedef struct form_t {
    gates_tree_t *t;
    gates_node_t tb, cb, b1, b2, stack, p0btn, p1btn, last;
} form_t;

static form_t make_form(gates_allocator_t alloc) {
    form_t f = {0};
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = alloc }, &f.t));
    gates_node_t root = gates_tree_root(f.t);
    GT_ASSERT_OK(gates_layout_set(f.t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_textbox_create(f.t, root, GATES_STR("abc"), 10, &f.tb));
    GT_ASSERT_OK(gates_checkbox_create(f.t, root, GATES_STR("opt"), false, nullptr, nullptr,
                                       &f.cb));
    GT_ASSERT_OK(gates_button_create(f.t, root, GATES_STR("one"), nullptr, nullptr, &f.b1));
    GT_ASSERT_OK(gates_button_create(f.t, root, GATES_STR("two"), nullptr, nullptr, &f.b2));
    GT_ASSERT_OK(gates_widget_set_disabled(f.t, f.b2, true));
    GT_ASSERT_OK(gates_panel_create(f.t, root, &f.stack));
    GT_ASSERT_OK(gates_layout_set(f.t, f.stack, GATES_LAYOUT_KIND_STACK));
    GT_ASSERT_OK(gates_button_create(f.t, f.stack, GATES_STR("page0"), nullptr, nullptr,
                                     &f.p0btn));
    GT_ASSERT_OK(gates_button_create(f.t, f.stack, GATES_STR("page1"), nullptr, nullptr,
                                     &f.p1btn));
    GT_ASSERT_OK(gates_button_create(f.t, root, GATES_STR("last"), nullptr, nullptr, &f.last));
    layout(f.t);
    return f;
}

/* -- focus -------------------------------------------------------------------- */

static void test_tab_order(void) {
    form_t f = make_form((gates_allocator_t){0});
    GT_ASSERT(gates_node_is_null(gates_tree_focus(f.t)));
    GT_ASSERT(tab(f.t, false) && focused(f.t, f.tb));
    GT_ASSERT(tab(f.t, false) && focused(f.t, f.cb));
    GT_ASSERT(tab(f.t, false) && focused(f.t, f.b1));
    GT_ASSERT(tab(f.t, false) && focused(f.t, f.p0btn)); /* disabled b2 skipped */
    GT_ASSERT(tab(f.t, false) && focused(f.t, f.last));  /* inactive page skipped */
    GT_ASSERT(tab(f.t, false) && focused(f.t, f.tb));    /* wraps */
    GT_ASSERT(tab(f.t, true) && focused(f.t, f.last));   /* Shift+Tab wraps back */
    GT_ASSERT(tab(f.t, true) && focused(f.t, f.p0btn));

    /* Taken out of the order. */
    GT_ASSERT_OK(gates_widget_set_focusable(f.t, f.b1, false));
    GT_ASSERT(!gates_widget_focusable(f.t, f.b1));
    gates_tree_set_focus(f.t, f.cb);
    GT_ASSERT(tab(f.t, false) && focused(f.t, f.p0btn));

    /* The other page becomes reachable when it is shown. */
    GT_ASSERT_OK(gates_layout_set_stack_active(f.t, f.stack, 1));
    gates_tree_set_focus(f.t, f.cb);
    GT_ASSERT(tab(f.t, false) && focused(f.t, f.p1btn));

    /* Ctrl+Tab is not traversal. */
    GT_ASSERT(!key_ex(f.t, GATES_KEY_TAB, true, true, false, false, 0));
    gates_tree_destroy(f.t);
}

static void test_focus_repair(void) {
    form_t f = make_form((gates_allocator_t){0});
    /* Disabling the focused control moves focus on. */
    gates_tree_set_focus(f.t, f.b1);
    GT_ASSERT_OK(gates_widget_set_disabled(f.t, f.b1, true));
    GT_ASSERT(focused(f.t, f.p0btn));
    /* Hiding its stack page moves focus on, in tree order: the button of the
     * page that is now shown comes next. */
    GT_ASSERT_OK(gates_layout_set_stack_active(f.t, f.stack, 1));
    GT_ASSERT(focused(f.t, f.p1btn));
    /* Destroying it moves focus to the next control (wrapping). */
    gates_tree_set_focus(f.t, f.last);
    GT_ASSERT_OK(gates_node_destroy(f.t, f.last));
    GT_ASSERT(focused(f.t, f.tb));
    /* Destroying a subtree that holds focus. */
    gates_tree_set_focus(f.t, f.p1btn);
    GT_ASSERT_OK(gates_node_destroy(f.t, f.stack));
    GT_ASSERT(focused(f.t, f.tb));
    GT_ASSERT_OK(gates_tree_flush_destroys(f.t));
    /* Nothing left to focus. */
    GT_ASSERT_OK(gates_widget_set_focusable(f.t, f.cb, false));
    GT_ASSERT_OK(gates_widget_set_focusable(f.t, f.tb, false));
    GT_ASSERT(gates_node_is_null(gates_tree_focus(f.t)));
    GT_ASSERT(!tab(f.t, false));
    gates_tree_destroy(f.t);
}

static void test_click_focus_and_ring(void) {
    form_t f = make_form((gates_allocator_t){0});
    click(f.t, f.b1);
    GT_ASSERT(focused(f.t, f.b1));
    click(f.t, f.b2);                 /* disabled: focus stays */
    GT_ASSERT(focused(f.t, f.b1));

    /* The focused button draws its border with the focus ring colour. */
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(f.t, &dl, theme, be));
    gates_color_t ring = gates_theme_color(theme, GATES_COLOR_FOCUS_RING);
    gates_rect_t br = gates_node_layout_rect(f.t, f.b1);
    bool found = false;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(&dl, i);
        if (c->kind == GATES_DRAW_BORDER && c->rect.x == br.x && c->rect.y == br.y &&
            c->color.r == ring.r && c->color.g == ring.g && c->color.b == ring.b) {
            found = true;
        }
    }
    GT_ASSERT(found);
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(f.t);
}

static void test_scroll_to_focus(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    gates_node_t sc = GATES_NODE_NULL, rows[30];
    GT_ASSERT_OK(gates_panel_create(t, root, &sc));
    GT_ASSERT_OK(gates_layout_set(t, sc, GATES_LAYOUT_KIND_SCROLL));
    GT_ASSERT_OK(gates_layout_set_child_grow(t, sc, 1));
    for (int i = 0; i < 30; i++) {
        GT_ASSERT_OK(gates_checkbox_create(t, sc, GATES_STR("row"), false, nullptr, nullptr,
                                           &rows[i]));
    }
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 120 }, be));
    gates_tree_set_focus(t, rows[0]);
    for (int i = 0; i < 20; i++) {
        GT_ASSERT(tab(t, false));
    }
    GT_ASSERT(focused(t, rows[20]));
    GT_ASSERT(gates_layout_scroll_offset(t, sc) > 0);
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 120 }, be));
    gates_rect_t vr = gates_node_layout_rect(t, sc);
    gates_rect_t rr = gates_node_layout_rect(t, rows[20]);
    GT_ASSERT(rr.y >= vr.y && rr.y + rr.h <= vr.y + vr.h);
    /* Back to the top with Shift+Tab. */
    for (int i = 0; i < 20; i++) {
        GT_ASSERT(tab(t, true));
    }
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 120 }, be));
    GT_ASSERT(gates_layout_scroll_offset(t, sc) == 0);
    gates_tree_destroy(t);
}

/* -- activation keys ------------------------------------------------------ */

static void test_space_enter_escape(void) {
    form_t f = make_form((gates_allocator_t){0});
    rec_t r = {0};
    GT_ASSERT_OK(gates_widget_set_handler(f.t, f.b1, record, &r));
    GT_ASSERT_OK(gates_widget_set_handler(f.t, f.cb, record, &r));

    /* Space: pressed on key-down, activated on key-up. */
    gates_tree_set_focus(f.t, f.b1);
    GT_ASSERT(press_key(f.t, GATES_KEY_SPACE));
    GT_ASSERT(gates_widget_pressed(f.t, f.b1));
    GT_ASSERT(gates_tree_pending_events(f.t) == 0);
    GT_ASSERT(key_ex(f.t, GATES_KEY_SPACE, false, false, false, false, 0));
    GT_ASSERT(!gates_widget_pressed(f.t, f.b1));
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(r.n == 1 && r.kind[0] == GATES_EVENT_ACTIVATED);

    /* Escape during the press cancels it. */
    GT_ASSERT(press_key(f.t, GATES_KEY_SPACE));
    GT_ASSERT(press_key(f.t, GATES_KEY_ESCAPE));
    GT_ASSERT(!gates_widget_pressed(f.t, f.b1));
    GT_ASSERT(!key_ex(f.t, GATES_KEY_SPACE, false, false, false, false, 0));
    GT_ASSERT(gates_tree_pending_events(f.t) == 0);

    /* Moving focus during the press cancels it too. */
    GT_ASSERT(press_key(f.t, GATES_KEY_SPACE));
    GT_ASSERT(tab(f.t, false));
    GT_ASSERT(!gates_widget_pressed(f.t, f.b1));     /* no pressed look left behind */
    GT_ASSERT(!key_ex(f.t, GATES_KEY_SPACE, false, false, false, false, 0));
    GT_ASSERT(gates_tree_pending_events(f.t) == 0);

    /* Space toggles a checkbox on release. */
    gates_tree_set_focus(f.t, f.cb);
    GT_ASSERT(press_key(f.t, GATES_KEY_SPACE));
    GT_ASSERT(!gates_checkbox_checked(f.t, f.cb));
    GT_ASSERT(key_ex(f.t, GATES_KEY_SPACE, false, false, false, false, 0));
    GT_ASSERT(gates_checkbox_checked(f.t, f.cb));

    /* Enter activates a focused button at once. */
    r.n = 0;
    gates_tree_set_focus(f.t, f.b1);
    GT_ASSERT(press_key(f.t, GATES_KEY_ENTER));
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(r.n == 2 && r.kind[1] == GATES_EVENT_ACTIVATED);

    /* Without default/cancel commands, Enter in a textbox and Escape are the app's. */
    gates_tree_set_focus(f.t, f.tb);
    GT_ASSERT(!press_key(f.t, GATES_KEY_ENTER));
    GT_ASSERT(!press_key(f.t, GATES_KEY_ESCAPE));

    /* A space typed in a textbox is text, not activation. */
    GT_ASSERT(!press_key(f.t, GATES_KEY_SPACE));
    GT_ASSERT(gates_input_char(f.t, ' ') == GATES_INPUT_CONSUMED);
    gates_tree_destroy(f.t);
}

/* -- commands ----------------------------------------------------------------- */

static gates_command_desc_t cmd(gates_command_id_t id, const char *label, gates_u8 letter,
                                bool ctrl, calls_t *c) {
    return (gates_command_desc_t){
        .id = id,
        .label = { .ptr = (const gates_u8 *)label, .size = strlen(label) },
        .shortcut = { .letter = letter, .ctrl = ctrl },
        .enabled = true,
        .invoke = on_command,
        .user = c,
    };
}

static void test_command_registration(void) {
    form_t f = make_form((gates_allocator_t){0});
    gates_node_t root = gates_tree_root(f.t);
    calls_t c = {0};
    gates_command_desc_t save = cmd(1, "Save", 'S', true, &c);
    GT_ASSERT_OK(gates_command_register(f.t, root, &save));
    GT_ASSERT(gates_command_exists(f.t, root, 1));
    GT_ASSERT(gates_command_register(f.t, root, &save) == PROVEN_ERR_INVALID_STATE);
    gates_command_desc_t dup_key = cmd(2, "Other", 'S', true, &c);
    GT_ASSERT(gates_command_register(f.t, root, &dup_key) == PROVEN_ERR_INVALID_STATE);
    gates_command_desc_t plain = cmd(3, "Plain", 'P', false, &c);  /* typed character */
    GT_ASSERT(gates_command_register(f.t, root, &plain) == PROVEN_ERR_INVALID_ARG);
    gates_command_desc_t altgr = cmd(3, "AltGr", 'Q', true, &c);
    altgr.shortcut.alt = true;
    GT_ASSERT(gates_command_register(f.t, root, &altgr) == PROVEN_ERR_INVALID_ARG);
    gates_command_desc_t zero = cmd(0, "Zero", 0, false, &c);
    GT_ASSERT(gates_command_register(f.t, root, &zero) == PROVEN_ERR_INVALID_ARG);
    gates_command_desc_t fkey = cmd(4, "Refresh", 0, false, &c);
    fkey.shortcut.key = GATES_KEY_F5;
    GT_ASSERT_OK(gates_command_register(f.t, root, &fkey));
    gates_command_desc_t def = cmd(5, "OK", 0, false, &c);
    def.role = GATES_COMMAND_DEFAULT;
    GT_ASSERT_OK(gates_command_register(f.t, root, &def));
    gates_command_desc_t def2 = cmd(6, "OK2", 0, false, &c);
    def2.role = GATES_COMMAND_DEFAULT;
    GT_ASSERT(gates_command_register(f.t, root, &def2) == PROVEN_ERR_INVALID_STATE);

    GT_ASSERT_OK(gates_command_set_label(f.t, root, 1, GATES_STR("Save all")));
    GT_ASSERT(gates_command_label(f.t, root, 1).size == 8);
    GT_ASSERT_OK(gates_command_set_checked(f.t, root, 1, true));
    GT_ASSERT(gates_command_checked(f.t, root, 1));
    GT_ASSERT_OK(gates_command_unregister(f.t, root, 1));
    GT_ASSERT(!gates_command_exists(f.t, root, 1));
    GT_ASSERT(gates_command_unregister(f.t, root, 1) == PROVEN_ERR_NOT_FOUND);
    GT_ASSERT(gates_command_invoke(f.t, root, 1) == PROVEN_ERR_NOT_FOUND);
    /* Its shortcut is free again. */
    GT_ASSERT_OK(gates_command_register(f.t, root, &dup_key));
    gates_tree_destroy(f.t);
}

static void test_shortcuts(void) {
    form_t f = make_form((gates_allocator_t){0});
    gates_node_t root = gates_tree_root(f.t);
    calls_t c = {0};
    gates_command_desc_t save = cmd(1, "Save", 'S', true, &c);
    gates_command_desc_t undo = cmd(2, "Undo app", 0, true, &c);
    undo.shortcut.key = GATES_KEY_Z;
    gates_command_desc_t refresh = cmd(3, "Refresh", 0, false, &c);
    refresh.shortcut.key = GATES_KEY_F5;
    GT_ASSERT_OK(gates_command_register(f.t, root, &save));
    GT_ASSERT_OK(gates_command_register(f.t, root, &undo));
    GT_ASSERT_OK(gates_command_register(f.t, root, &refresh));

    /* Queued, not run inline; delivered once per press, never coalesced. */
    gates_tree_set_focus(f.t, f.b1);
    GT_ASSERT(key_ex(f.t, GATES_KEY_NONE, true, true, false, false, 'S'));
    GT_ASSERT(key_ex(f.t, GATES_KEY_NONE, true, true, false, false, 'S'));
    GT_ASSERT(c.count[1] == 0);
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(c.count[1] == 2);
    GT_ASSERT(press_key(f.t, GATES_KEY_F5));
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(c.count[3] == 1);

    /* Ctrl+Alt+S (AltGr) and a plain S are not the shortcut. */
    GT_ASSERT(!key_ex(f.t, GATES_KEY_NONE, true, true, false, true, 'S'));
    GT_ASSERT(!key_ex(f.t, GATES_KEY_NONE, true, false, false, false, 'S'));
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(c.count[1] == 2);

    /* The focused textbox gets first refusal: Ctrl+Z is its undo. */
    gates_tree_set_focus(f.t, f.tb);
    GT_ASSERT(gates_input_char(f.t, 'x') == GATES_INPUT_CONSUMED);
    GT_ASSERT(key_ex(f.t, GATES_KEY_Z, true, true, false, false, 'Z'));
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(c.count[2] == 0);
    GT_ASSERT(gates_textbox_text(f.t, f.tb).size == 3);
    /* ... but Ctrl+S, which the textbox does not use, reaches the command. */
    GT_ASSERT(key_ex(f.t, GATES_KEY_NONE, true, true, false, false, 'S'));
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(c.count[1] == 3);

    /* Disabled: the shortcut is not consumed and nothing runs. */
    GT_ASSERT_OK(gates_command_set_enabled(f.t, root, 1, false));
    GT_ASSERT(!key_ex(f.t, GATES_KEY_NONE, true, true, false, false, 'S'));
    /* Disabled after queuing but before delivery: re-checked, not run. */
    GT_ASSERT_OK(gates_command_set_enabled(f.t, root, 1, true));
    GT_ASSERT(key_ex(f.t, GATES_KEY_NONE, true, true, false, false, 'S'));
    GT_ASSERT_OK(gates_command_set_enabled(f.t, root, 1, false));
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(c.count[1] == 3);
    gates_tree_destroy(f.t);
}

static void test_default_and_cancel(void) {
    form_t f = make_form((gates_allocator_t){0});
    gates_node_t root = gates_tree_root(f.t);
    calls_t c = {0};
    gates_command_desc_t ok = cmd(1, "OK", 0, false, &c);
    ok.role = GATES_COMMAND_DEFAULT;
    gates_command_desc_t no = cmd(2, "Cancel", 0, false, &c);
    no.role = GATES_COMMAND_CANCEL;
    GT_ASSERT_OK(gates_command_register(f.t, root, &ok));
    GT_ASSERT_OK(gates_command_register(f.t, root, &no));

    gates_tree_set_focus(f.t, f.tb);
    GT_ASSERT(press_key(f.t, GATES_KEY_ENTER));    /* textbox declines Enter */
    gates_tree_set_focus(f.t, f.cb);
    GT_ASSERT(press_key(f.t, GATES_KEY_ENTER));
    GT_ASSERT(press_key(f.t, GATES_KEY_ESCAPE));
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(c.count[1] == 2 && c.count[2] == 1);
    /* Enter on a focused button activates that button, not the default. */
    gates_tree_set_focus(f.t, f.b1);
    GT_ASSERT(press_key(f.t, GATES_KEY_ENTER));
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(c.count[1] == 2);
    gates_tree_destroy(f.t);
}

static void test_bound_button(void) {
    form_t f = make_form((gates_allocator_t){0});
    gates_node_t root = gates_tree_root(f.t);
    calls_t c = {0};
    rec_t r = {0};
    gates_command_desc_t save = cmd(1, "Save file", 'S', true, &c);
    GT_ASSERT_OK(gates_command_register(f.t, root, &save));
    GT_ASSERT_OK(gates_button_set_command(f.t, f.b1, root, 1));
    GT_ASSERT_OK(gates_widget_set_handler(f.t, f.b1, record, &r));
    layout(f.t);

    /* It shows the command's label. */
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(f.t, &dl, theme, be));
    bool label = false;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *d = gates_draw_list_at(&dl, i);
        if (d->kind == GATES_DRAW_TEXT && d->text_len == 9 &&
            memcmp(dl.text + d->text_offset, "Save file", 9) == 0) {
            label = true;
        }
    }
    GT_ASSERT(label);

    /* Clicking runs the command (queued) and still reports the activation. */
    click(f.t, f.b1);
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(c.count[1] == 1 && r.n == 1);

    /* A disabled command makes the button inert and unfocusable. */
    gates_tree_set_focus(f.t, f.b1);
    GT_ASSERT_OK(gates_command_set_enabled(f.t, root, 1, false));
    GT_ASSERT(!focused(f.t, f.b1));  /* focus moved on */
    click(f.t, f.b1);
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(c.count[1] == 1);
    /* A relabel reaches the button (layout dirty). */
    gates_tree_clear_dirty(f.t, GATES_TREE_DIRTY_LAYOUT | GATES_TREE_DIRTY_PAINT);
    GT_ASSERT_OK(gates_command_set_label(f.t, root, 1, GATES_STR("Save")));
    GT_ASSERT((gates_tree_dirty(f.t) & GATES_TREE_DIRTY_LAYOUT) != 0);
    /* The command going away leaves the button inert, not dangling. */
    GT_ASSERT_OK(gates_command_set_enabled(f.t, root, 1, true));
    GT_ASSERT_OK(gates_command_unregister(f.t, root, 1));
    layout(f.t);
    click(f.t, f.b1);
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(c.count[1] == 1);
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(f.t);
}

/* A command that removes itself: the queued second run finds nothing. */
typedef struct self_t {
    gates_node_t root;
    int runs;
} self_t;

static void unregister_self(gates_tree_t *tree, gates_command_id_t id, void *user) {
    self_t *s = user;
    s->runs++;
    GT_ASSERT_OK(gates_command_unregister(tree, s->root, id));
}

static void test_handler_unregisters_itself(void) {
    form_t f = make_form((gates_allocator_t){0});
    self_t s = { .root = gates_tree_root(f.t) };
    gates_command_desc_t d = { .id = 7, .label = GATES_STR("Once"),
                               .shortcut = { .letter = 'O', .ctrl = true }, .enabled = true,
                               .invoke = unregister_self, .user = &s };
    GT_ASSERT_OK(gates_command_register(f.t, s.root, &d));
    GT_ASSERT_OK(gates_command_invoke(f.t, s.root, 7));
    GT_ASSERT_OK(gates_command_invoke(f.t, s.root, 7));
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(s.runs == 1);
    gates_tree_destroy(f.t);
}

typedef struct fail_alloc_t {
    gates_allocator_t inner;
    bool fail;
} fail_alloc_t;

static proven_result_mem_mut_t fa_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->fail) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    return f->inner.alloc_fn(f->inner.ctx, size, align);
}

static proven_result_mem_mut_t fa_realloc(void *ctx, void *p, proven_size_t os, proven_size_t ns,
                                          proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->fail) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    return f->inner.realloc_fn(f->inner.ctx, p, os, ns, align);
}

static void fa_free(void *ctx, void *p) {
    fail_alloc_t *f = ctx;
    f->inner.free_fn(f->inner.ctx, p);
}

static void test_allocation_failure(void) {
    fail_alloc_t fa = { .inner = proven_heap_allocator() };
    gates_allocator_t alloc = { .ctx = &fa, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc,
                                .free_fn = fa_free };
    form_t f = make_form(alloc);
    gates_node_t root = gates_tree_root(f.t);
    calls_t c = {0};
    gates_command_desc_t save = cmd(1, "Save", 'S', true, &c);
    fa.fail = true;
    GT_ASSERT(gates_command_register(f.t, root, &save) == PROVEN_ERR_NOMEM);
    GT_ASSERT(!gates_command_exists(f.t, root, 1));
    fa.fail = false;
    GT_ASSERT_OK(gates_command_register(f.t, root, &save));
    /* The invocation queue has never been allocated. */
    fa.fail = true;
    GT_ASSERT(key_ex(f.t, GATES_KEY_NONE, true, true, false, false, 'S'));
    GT_ASSERT(gates_input_take_error(f.t) == PROVEN_ERR_NOMEM);
    GT_ASSERT(gates_command_invoke(f.t, root, 1) == PROVEN_ERR_NOMEM);
    fa.fail = false;
    (void)gates_tree_dispatch_events(f.t, 0);
    GT_ASSERT(c.count[1] == 0);
    gates_tree_destroy(f.t);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_tab_order();
    test_focus_repair();
    test_click_focus_and_ring();
    test_scroll_to_focus();
    test_space_enter_escape();
    test_command_registration();
    test_shortcuts();
    test_default_and_cancel();
    test_bound_button();
    test_handler_unregisters_itself();
    test_allocation_failure();
    return gt_report("test_focus_commands");
}
