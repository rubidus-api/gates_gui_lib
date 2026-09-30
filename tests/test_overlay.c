/* overlays - modal dialog and context menu with one set of dismissal
 * rules. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/command.h>
#include <gates/overlay.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <string.h>

static const gates_text_backend_t *be;
static const gates_theme_t *theme;
#define VW 400
#define VH 300

static void layout(gates_tree_t *t) {
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VW, VH }, be));
}

static bool key(gates_tree_t *t, gates_key_t k, bool ctrl, bool shift, gates_u8 letter) {
    gates_key_event_t e = { .key = k, .down = true, .ctrl = ctrl, .shift = shift,
                            .letter = letter };
    return gates_input_key(t, &e);
}

static void pointer(gates_tree_t *t, gates_pointer_action_t a, gates_point_t p) {
    gates_pointer_event_t e = { .action = a, .button = GATES_BUTTON_LEFT, .pos = p };
    (void)gates_input_pointer(t, &e);
}

static gates_point_t center(const gates_tree_t *t, gates_node_t n) {
    gates_rect_t r = gates_node_layout_rect(t, n);
    return (gates_point_t){ r.x + r.w / 2, r.y + r.h / 2 };
}

static void click_at(gates_tree_t *t, gates_point_t p) {
    pointer(t, GATES_POINTER_DOWN, p);
    pointer(t, GATES_POINTER_UP, p);
}

static bool focused(const gates_tree_t *t, gates_node_t n) {
    return gates_node_eq(gates_tree_focus(t), n);
}

typedef struct rec_t {
    gates_event_kind_t kind[16];
    gates_u32 result[16];
    int n;
} rec_t;

static void record(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    rec_t *r = user;
    if (r->n < 16) {
        r->kind[r->n] = ev->kind;
        r->result[r->n] = ev->result;
        r->n++;
    }
}

typedef struct calls_t {
    int count[16];
    gates_node_t dialog;
    gates_tree_t *tree;
} calls_t;

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    calls_t *c = user;
    if (id < 16) c->count[id]++;
    /* Dialog commands close it with a result. */
    if (id == 11) (void)gates_dialog_close(tree, c->dialog, GATES_DIALOG_ACCEPTED);
    if (id == 12) (void)gates_dialog_close(tree, c->dialog, GATES_DIALOG_CANCELED);
}

static gates_command_desc_t cmd(gates_command_id_t id, const char *label, gates_u8 letter,
                                gates_command_role_t role, calls_t *c) {
    return (gates_command_desc_t){
        .id = id,
        .label = { .ptr = (const gates_u8 *)label, .size = strlen(label) },
        .shortcut = { .letter = letter, .ctrl = letter != 0 },
        .role = role,
        .enabled = true,
        .invoke = on_command,
        .user = c,
    };
}

typedef struct app_t {
    gates_tree_t *t;
    gates_node_t main_btn, main_box;
    gates_node_t dialog, content, field, ok, cancel;
    calls_t calls;
    rec_t rec;
} app_t;

static void make_app(app_t *a, gates_allocator_t alloc) {
    memset(a, 0, sizeof *a);
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = alloc }, &a->t));
    a->calls.tree = a->t;
    gates_node_t root = gates_tree_root(a->t);
    GT_ASSERT_OK(gates_layout_set(a->t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_textbox_create(a->t, root, GATES_STR("main"), 10, &a->main_box));
    GT_ASSERT_OK(gates_button_create(a->t, root, GATES_STR("main button"), nullptr, nullptr,
                                     &a->main_btn));
    gates_command_desc_t save = cmd(1, "Save", 'S', GATES_COMMAND_NORMAL, &a->calls);
    gates_command_desc_t root_ok = cmd(2, "Root OK", 0, GATES_COMMAND_DEFAULT, &a->calls);
    GT_ASSERT_OK(gates_command_register(a->t, root, &save));
    GT_ASSERT_OK(gates_command_register(a->t, root, &root_ok));
    layout(a->t);
}

/* A dialog with a field and OK (default) / Cancel (cancel) buttons. */
static void open_dialog(app_t *a, bool with_cancel_command) {
    gates_dialog_desc_t d = { .title = GATES_STR("Confirm") };
    GT_ASSERT_OK(gates_dialog_open(a->t, &d, &a->dialog, &a->content));
    a->calls.dialog = a->dialog;
    GT_ASSERT_OK(gates_widget_set_handler(a->t, a->dialog, record, &a->rec));
    gates_command_desc_t ok = cmd(11, "OK", 0, GATES_COMMAND_DEFAULT, &a->calls);
    gates_command_desc_t no = cmd(12, "Cancel", 0, GATES_COMMAND_CANCEL, &a->calls);
    GT_ASSERT_OK(gates_command_register(a->t, a->dialog, &ok));
    if (with_cancel_command) {
        GT_ASSERT_OK(gates_command_register(a->t, a->dialog, &no));
    }
    GT_ASSERT_OK(gates_textbox_create(a->t, a->content, GATES_STR(""), 12, &a->field));
    GT_ASSERT_OK(gates_button_create(a->t, a->content, GATES_STR(""), nullptr, nullptr, &a->ok));
    GT_ASSERT_OK(gates_button_set_command(a->t, a->ok, a->dialog, 11));
    GT_ASSERT_OK(gates_button_create(a->t, a->content, GATES_STR("Cancel"), nullptr, nullptr,
                                     &a->cancel));
    layout(a->t);
}

/* -- dialog ------------------------------------------------------------------- */

static void test_dialog_modal(void) {
    app_t a;
    make_app(&a, (gates_allocator_t){0});
    gates_tree_set_focus(a.t, a.main_btn);
    open_dialog(&a, true);
    GT_ASSERT(gates_tree_overlay_count(a.t) == 1);
    GT_ASSERT(gates_overlay_is_open(a.t, a.dialog));

    /* Centred inside the window. */
    gates_rect_t dr = gates_node_layout_rect(a.t, a.dialog);
    GT_ASSERT(dr.w > 0 && dr.h > 0);
    GT_ASSERT(dr.x >= 0 && dr.y >= 0 && dr.x + dr.w <= VW && dr.y + dr.h <= VH);
    GT_ASSERT(dr.x == (VW - dr.w) / 2 && dr.y == (VH - dr.h) / 2);

    /* Focus moved into the dialog; Tab cycles inside it only. */
    GT_ASSERT(focused(a.t, a.field));
    GT_ASSERT(key(a.t, GATES_KEY_TAB, false, false, 0) && focused(a.t, a.ok));
    GT_ASSERT(key(a.t, GATES_KEY_TAB, false, false, 0) && focused(a.t, a.cancel));
    GT_ASSERT(key(a.t, GATES_KEY_TAB, false, false, 0) && focused(a.t, a.field));

    /* Input below is blocked: a click on the main button does nothing. */
    rec_t mr = {0};
    GT_ASSERT_OK(gates_widget_set_handler(a.t, a.main_btn, record, &mr));
    click_at(a.t, center(a.t, a.main_btn));
    GT_ASSERT(focused(a.t, a.field));
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(mr.n == 0);

    /* Only the dialog's commands apply: Ctrl+S of the window scope is blocked. */
    GT_ASSERT(!key(a.t, GATES_KEY_NONE, true, false, 'S'));
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(a.calls.count[1] == 0);

    /* Paint: the dim layer and the dialog come after the window content. */
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(a.t, &dl, theme, be));
    gates_color_t dim = gates_theme_color(theme, GATES_COLOR_OVERLAY_DIM);
    gates_rect_t mb = gates_node_layout_rect(a.t, a.main_btn);
    int main_at = -1, dim_at = -1, dialog_at = -1;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(&dl, i);
        if (c->kind == GATES_DRAW_BORDER && c->rect.x == mb.x && c->rect.y == mb.y) main_at = (int)i;
        if (c->kind == GATES_DRAW_RECT && c->color.a == dim.a && c->color.r == dim.r &&
            c->rect.w == VW && c->rect.h == VH) dim_at = (int)i;
        if (c->rect.x == dr.x && c->rect.y == dr.y && c->rect.w == dr.w && dialog_at < 0 &&
            dim_at >= 0) dialog_at = (int)i;
    }
    GT_ASSERT(main_at >= 0 && dim_at > main_at && dialog_at > dim_at);
    gates_draw_list_deinit(&dl);

    /* Enter runs the dialog's default (not the window's); it closes it once. */
    GT_ASSERT(key(a.t, GATES_KEY_ENTER, false, false, 0));
    (void)gates_tree_dispatch_events(a.t, 0);   /* runs OK -> close -> queues CLOSED */
    GT_ASSERT(a.calls.count[11] == 1 && a.calls.count[2] == 0);
    GT_ASSERT(gates_tree_overlay_count(a.t) == 0);
    (void)gates_tree_dispatch_events(a.t, 0);   /* delivers CLOSED */
    GT_ASSERT(a.rec.n == 1 && a.rec.kind[0] == GATES_EVENT_DIALOG_CLOSED);
    GT_ASSERT(a.rec.result[0] == GATES_DIALOG_ACCEPTED);
    GT_ASSERT(!gates_node_is_valid(a.t, a.dialog));  /* destroyed after delivery */
    GT_ASSERT_OK(gates_tree_flush_destroys(a.t));
    /* Focus is back where it was; the window's shortcuts work again. */
    GT_ASSERT(focused(a.t, a.main_btn));
    GT_ASSERT(key(a.t, GATES_KEY_NONE, true, false, 'S'));
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(a.calls.count[1] == 1);
    gates_tree_destroy(a.t);
}

/* A drag begun below must not continue behind a dialog that opens mid-drag. */
static void test_dialog_cancels_drag(void) {
    app_t a;
    make_app(&a, (gates_allocator_t){0});
    gates_rect_t br = gates_node_layout_rect(a.t, a.main_box);
    gates_point_t start = { br.x + 6, br.y + br.h / 2 };
    pointer(a.t, GATES_POINTER_DOWN, start);           /* text selection drag begins */
    open_dialog(&a, true);
    pointer(a.t, GATES_POINTER_MOVE, (gates_point_t){ br.x + br.w - 2, start.y });
    pointer(a.t, GATES_POINTER_UP, (gates_point_t){ br.x + br.w - 2, start.y });
    gates_text_edit_t *ed = gates_textbox_edit(a.t, a.main_box);
    GT_ASSERT(!gates_text_edit_has_selection(ed));     /* the drag did not go on */
    gates_tree_destroy(a.t);
}

static void test_dialog_cancel_paths(void) {
    app_t a;
    make_app(&a, (gates_allocator_t){0});
    /* Escape runs the cancel command. */
    open_dialog(&a, true);
    GT_ASSERT(key(a.t, GATES_KEY_ESCAPE, false, false, 0));
    (void)gates_tree_dispatch_events(a.t, 0);
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(a.calls.count[12] == 1);
    GT_ASSERT(a.rec.n == 1 && a.rec.result[0] == GATES_DIALOG_CANCELED);
    GT_ASSERT_OK(gates_tree_flush_destroys(a.t));

    /* Without a cancel command Escape cancels the dialog itself. */
    a.rec.n = 0;
    open_dialog(&a, false);
    GT_ASSERT(key(a.t, GATES_KEY_ESCAPE, false, false, 0));
    GT_ASSERT(gates_tree_overlay_count(a.t) == 0);
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(a.rec.n == 1 && a.rec.result[0] == GATES_DIALOG_CANCELED);
    GT_ASSERT_OK(gates_tree_flush_destroys(a.t));

    /* Closing twice: the second is refused; the result arrives once. */
    a.rec.n = 0;
    open_dialog(&a, true);
    GT_ASSERT_OK(gates_dialog_close(a.t, a.dialog, GATES_DIALOG_ACCEPTED));
    GT_ASSERT(gates_dialog_close(a.t, a.dialog, GATES_DIALOG_CANCELED) ==
              PROVEN_ERR_INVALID_STATE);
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(a.rec.n == 1 && a.rec.result[0] == GATES_DIALOG_ACCEPTED);
    GT_ASSERT_OK(gates_tree_flush_destroys(a.t));

    /* Destroying the dialog directly: gone, no event, focus restored. */
    a.rec.n = 0;
    gates_tree_set_focus(a.t, a.main_box);
    open_dialog(&a, true);
    GT_ASSERT_OK(gates_node_destroy(a.t, a.dialog));
    GT_ASSERT(gates_tree_overlay_count(a.t) == 0);
    GT_ASSERT(focused(a.t, a.main_box));
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(a.rec.n == 0);
    GT_ASSERT_OK(gates_tree_flush_destroys(a.t));
    GT_ASSERT(gates_dialog_close(a.t, a.dialog, GATES_DIALOG_ACCEPTED) ==
              PROVEN_ERR_INVALID_STATE);
    gates_tree_destroy(a.t);
}

/* -- menu --------------------------------------------------------------------- */

static void menu_commands(app_t *a, gates_node_t scope) {
    gates_command_desc_t c3 = cmd(3, "Copy", 'C', GATES_COMMAND_NORMAL, &a->calls);
    gates_command_desc_t c4 = cmd(4, "Paste", 'V', GATES_COMMAND_NORMAL, &a->calls);
    gates_command_desc_t c5 = cmd(5, "Delete", 0, GATES_COMMAND_NORMAL, &a->calls);
    c4.enabled = false;
    c5.checked = true;
    GT_ASSERT_OK(gates_command_register(a->t, scope, &c3));
    GT_ASSERT_OK(gates_command_register(a->t, scope, &c4));
    GT_ASSERT_OK(gates_command_register(a->t, scope, &c5));
}

static void test_menu(void) {
    app_t a;
    make_app(&a, (gates_allocator_t){0});
    gates_node_t root = gates_tree_root(a.t);
    menu_commands(&a, root);
    const gates_command_id_t ids[] = { 3, 0, 4, 5 };   /* 0 = separator */
    gates_node_t m = GATES_NODE_NULL;
    rec_t mr = {0};

    /* Opened near the corner: kept inside the window. */
    GT_ASSERT_OK(gates_menu_open(a.t, (gates_point_t){ VW - 5, VH - 5 }, root, ids, 4, &m));
    GT_ASSERT_OK(gates_widget_set_handler(a.t, m, record, &mr));
    layout(a.t);
    gates_rect_t r = gates_node_layout_rect(a.t, m);
    GT_ASSERT(r.w > 0 && r.h > 0 && r.x + r.w <= VW && r.y + r.h <= VH && r.x >= 0 && r.y >= 0);

    /* Keys go to the menu: Down skips the separator and the disabled entry. */
    GT_ASSERT(key(a.t, GATES_KEY_DOWN, false, false, 0));   /* -> Copy */
    GT_ASSERT(key(a.t, GATES_KEY_DOWN, false, false, 0));   /* -> Delete */
    GT_ASSERT(key(a.t, GATES_KEY_ENTER, false, false, 0));
    GT_ASSERT(gates_tree_overlay_count(a.t) == 0);
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(a.calls.count[5] == 1 && a.calls.count[3] == 0 && a.calls.count[4] == 0);
    GT_ASSERT(mr.n == 1 && mr.kind[0] == GATES_EVENT_MENU_CLOSED && mr.result[0] == 5);
    GT_ASSERT_OK(gates_tree_flush_destroys(a.t));

    /* Escape closes it with nothing chosen. */
    mr.n = 0;
    GT_ASSERT_OK(gates_menu_open(a.t, (gates_point_t){ 10, 10 }, root, ids, 4, &m));
    GT_ASSERT_OK(gates_widget_set_handler(a.t, m, record, &mr));
    layout(a.t);
    GT_ASSERT(key(a.t, GATES_KEY_ESCAPE, false, false, 0));
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(mr.n == 1 && mr.result[0] == 0);
    GT_ASSERT_OK(gates_tree_flush_destroys(a.t));

    /* An outside click only closes it: the button underneath is not pressed. */
    mr.n = 0;
    rec_t br = {0};
    GT_ASSERT_OK(gates_widget_set_handler(a.t, a.main_btn, record, &br));
    GT_ASSERT_OK(gates_menu_open(a.t, (gates_point_t){ VW - 120, 10 }, root, ids, 4, &m));
    GT_ASSERT_OK(gates_widget_set_handler(a.t, m, record, &mr));
    layout(a.t);
    click_at(a.t, center(a.t, a.main_btn));
    GT_ASSERT(gates_tree_overlay_count(a.t) == 0);
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(mr.n == 1 && mr.result[0] == 0);
    GT_ASSERT(br.n == 0);
    GT_ASSERT_OK(gates_tree_flush_destroys(a.t));

    /* A click on an entry chooses it; a click on the disabled one does nothing. */
    mr.n = 0;
    GT_ASSERT_OK(gates_menu_open(a.t, (gates_point_t){ 10, 10 }, root, ids, 4, &m));
    GT_ASSERT_OK(gates_widget_set_handler(a.t, m, record, &mr));
    layout(a.t);
    r = gates_node_layout_rect(a.t, m);
    gates_i32 row_h = (r.h - 2 * 4) / 4;
    gates_point_t paste = { r.x + r.w / 2, r.y + 4 + 2 * row_h + row_h / 2 };
    gates_point_t copy = { r.x + r.w / 2, r.y + 4 + row_h / 2 };
    click_at(a.t, paste);
    GT_ASSERT(gates_tree_overlay_count(a.t) == 1);
    click_at(a.t, copy);
    GT_ASSERT(gates_tree_overlay_count(a.t) == 0);
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(a.calls.count[3] == 1 && a.calls.count[4] == 0);
    GT_ASSERT(mr.n == 1 && mr.result[0] == 3);
    GT_ASSERT_OK(gates_tree_flush_destroys(a.t));

    /* Losing window focus closes menus. */
    GT_ASSERT_OK(gates_menu_open(a.t, (gates_point_t){ 10, 10 }, root, ids, 4, &m));
    gates_tree_dismiss_menus(a.t);
    GT_ASSERT(gates_tree_overlay_count(a.t) == 0);
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT_OK(gates_tree_flush_destroys(a.t));
    gates_tree_destroy(a.t);
}

static void test_menu_over_dialog(void) {
    app_t a;
    make_app(&a, (gates_allocator_t){0});
    open_dialog(&a, true);
    menu_commands(&a, a.dialog);
    const gates_command_id_t ids[] = { 3, 5 };
    gates_node_t m = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_menu_open(a.t, (gates_point_t){ 20, 20 }, a.dialog, ids, 2, &m));
    layout(a.t);
    GT_ASSERT(gates_tree_overlay_count(a.t) == 2);
    /* Escape closes the menu only. */
    GT_ASSERT(key(a.t, GATES_KEY_ESCAPE, false, false, 0));
    GT_ASSERT(gates_tree_overlay_count(a.t) == 1 && gates_overlay_is_open(a.t, a.dialog));
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(a.calls.count[12] == 0);
    /* A menu whose command scope goes away closes with it. */
    GT_ASSERT_OK(gates_menu_open(a.t, (gates_point_t){ 20, 20 }, a.dialog, ids, 2, &m));
    GT_ASSERT_OK(gates_dialog_close(a.t, a.dialog, GATES_DIALOG_CANCELED));
    GT_ASSERT(gates_tree_overlay_count(a.t) == 0);
    (void)gates_tree_dispatch_events(a.t, 0);
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT_OK(gates_tree_flush_destroys(a.t));
    gates_tree_destroy(a.t);
}

static void test_limits_and_errors(void) {
    app_t a;
    make_app(&a, (gates_allocator_t){0});
    gates_node_t root = gates_tree_root(a.t);
    menu_commands(&a, root);
    const gates_command_id_t ids[] = { 3 };
    gates_node_t m = GATES_NODE_NULL;
    for (int i = 0; i < 8; i++) {
        GT_ASSERT_OK(gates_menu_open(a.t, (gates_point_t){ 10, 10 }, root, ids, 1, &m));
    }
    GT_ASSERT(gates_menu_open(a.t, (gates_point_t){ 10, 10 }, root, ids, 1, &m) ==
              PROVEN_ERR_OUT_OF_BOUNDS);
    gates_tree_dismiss_menus(a.t);
    GT_ASSERT(gates_tree_overlay_count(a.t) == 0);
    GT_ASSERT(gates_menu_open(a.t, (gates_point_t){ 10, 10 }, root, ids, 0, &m) ==
              PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_menu_close(a.t, root) == PROVEN_ERR_INVALID_STATE);
    gates_tree_destroy(a.t);
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
    app_t a;
    make_app(&a, alloc);
    gates_u32 live = gates_tree_live_count(a.t);
    fa.fail = true;
    gates_dialog_desc_t d = { .title = GATES_STR("x") };
    gates_node_t dlg = GATES_NODE_NULL, content = GATES_NODE_NULL;
    GT_ASSERT(gates_dialog_open(a.t, &d, &dlg, &content) == PROVEN_ERR_NOMEM);
    GT_ASSERT(gates_tree_overlay_count(a.t) == 0);
    fa.fail = false;
    GT_ASSERT_OK(gates_tree_flush_destroys(a.t));
    GT_ASSERT(gates_tree_live_count(a.t) == live);   /* nothing half-built left */

    /* The close event cannot be queued: the dialog stays open. */
    open_dialog(&a, true);
    fa.fail = true;
    GT_ASSERT(gates_dialog_close(a.t, a.dialog, GATES_DIALOG_ACCEPTED) == PROVEN_ERR_NOMEM);
    GT_ASSERT(gates_overlay_is_open(a.t, a.dialog));
    fa.fail = false;
    GT_ASSERT_OK(gates_dialog_close(a.t, a.dialog, GATES_DIALOG_ACCEPTED));
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(a.rec.n == 1);
    gates_tree_destroy(a.t);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_dialog_modal();
    test_dialog_cancels_drag();
    test_dialog_cancel_paths();
    test_menu();
    test_menu_over_dialog();
    test_limits_and_errors();
    test_allocation_failure();
    return gt_report("test_overlay");
}
