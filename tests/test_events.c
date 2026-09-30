/* typed change notifications and pointer lifecycle. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/event.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <string.h>

static const gates_text_backend_t *be;

/* -- recorder ----------------------------------------------------------------- */

typedef struct rec_event_t {
    gates_event_kind_t kind;
    gates_event_origin_t origin;
    gates_node_t source;
    gates_u32 revision;
    char text[96];
    gates_usize_t text_len;
    bool checked;
} rec_event_t;

typedef struct recorder_t {
    rec_event_t ev[32];
    int n;
} recorder_t;

static void record(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    recorder_t *r = user;
    if (r->n >= 32) {
        return;
    }
    rec_event_t *e = &r->ev[r->n++];
    e->kind = ev->kind;
    e->origin = ev->origin;
    e->source = ev->source;
    e->revision = ev->revision;
    e->text_len = ev->text.size < sizeof e->text ? ev->text.size : sizeof e->text;
    if (e->text_len > 0) {
        memcpy(e->text, ev->text.ptr, e->text_len);
    }
    e->checked = ev->checked;
}

static bool rec_text_is(const rec_event_t *e, const char *s) {
    gates_usize_t n = strlen(s);
    return e->text_len == n && (n == 0 || memcmp(e->text, s, n) == 0);
}

static bool same(gates_node_t a, gates_node_t b) {
    return a.index == b.index && a.generation == b.generation;
}

/* -- fixtures ------------------------------------------------------------------ */

typedef struct fail_alloc_t {
    gates_allocator_t inner;
    bool fail;
} fail_alloc_t;

static proven_result_mem_mut_t fa_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->fail) {
        return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    }
    return f->inner.alloc_fn(f->inner.ctx, size, align);
}

static proven_result_mem_mut_t fa_realloc(void *ctx, void *old_ptr, proven_size_t old_size,
                                          proven_size_t new_size, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->fail) {
        return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    }
    return f->inner.realloc_fn(f->inner.ctx, old_ptr, old_size, new_size, align);
}

static void fa_free(void *ctx, void *ptr) {
    fail_alloc_t *f = ctx;
    f->inner.free_fn(f->inner.ctx, ptr);
}

typedef struct ui_t {
    gates_tree_t *t;
    gates_node_t tb, tb2, cb, btn;
} ui_t;

static ui_t make_ui(gates_allocator_t alloc) {
    ui_t u = {0};
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = alloc }, &u.t));
    gates_node_t root = gates_tree_root(u.t);
    GT_ASSERT_OK(gates_layout_set(u.t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_textbox_create(u.t, root, GATES_STR("abc"), 20, &u.tb));
    GT_ASSERT_OK(gates_textbox_create(u.t, root, GATES_STR("xyz"), 20, &u.tb2));
    GT_ASSERT_OK(gates_checkbox_create(u.t, root, GATES_STR("check"), false, nullptr, nullptr,
                                       &u.cb));
    GT_ASSERT_OK(gates_button_create(u.t, root, GATES_STR("press"), nullptr, nullptr, &u.btn));
    GT_ASSERT_OK(gates_layout_run(u.t, (gates_size_t){ 400, 300 }, be));
    return u;
}

static gates_point_t center(const gates_tree_t *t, gates_node_t n) {
    gates_rect_t r = gates_node_layout_rect(t, n);
    return (gates_point_t){ r.x + r.w / 2, r.y + r.h / 2 };
}

static void pointer(gates_tree_t *t, gates_pointer_action_t a, gates_point_t p) {
    gates_pointer_event_t e = { .action = a, .button = GATES_BUTTON_LEFT, .pos = p };
    (void)gates_input_pointer(t, &e);
}

static void click(gates_tree_t *t, gates_node_t n) {
    pointer(t, GATES_POINTER_DOWN, center(t, n));
    pointer(t, GATES_POINTER_UP, center(t, n));
}

static bool key(gates_tree_t *t, gates_key_t k, bool ctrl) {
    gates_key_event_t e = { .key = k, .down = true, .ctrl = ctrl };
    return gates_input_key(t, &e);
}

/* -- tests -------------------------------------------------------------------- */

static void test_typing_notifies_once_per_batch(void) {
    ui_t u = make_ui((gates_allocator_t){0});
    recorder_t r = {0};
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.tb, record, &r));
    gates_tree_set_focus(u.t, u.tb);
    gates_text_edit_set_caret(gates_textbox_edit(u.t, u.tb), 3, false);

    GT_ASSERT(gates_widget_revision(u.t, u.tb) == 0);
    GT_ASSERT(gates_input_char(u.t, 'x') == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_input_char(u.t, 'y') == GATES_INPUT_CONSUMED);
    GT_ASSERT(r.n == 0);                           /* queued, not delivered inline */
    GT_ASSERT(gates_tree_pending_events(u.t) == 1); /* coalesced to the latest state */
    GT_ASSERT(gates_tree_dispatch_events(u.t, 0) == 0);
    GT_ASSERT(r.n == 1);
    GT_ASSERT(r.ev[0].kind == GATES_EVENT_TEXT_CHANGED);
    GT_ASSERT(r.ev[0].origin == GATES_ORIGIN_USER);
    GT_ASSERT(same(r.ev[0].source, u.tb));
    GT_ASSERT(r.ev[0].revision == 2);
    GT_ASSERT(rec_text_is(&r.ev[0], "abcxy"));

    /* Caret moves and select-all change no text: no event, no revision. */
    GT_ASSERT(key(u.t, GATES_KEY_LEFT, false));
    GT_ASSERT(key(u.t, GATES_KEY_A, true));
    GT_ASSERT(gates_tree_pending_events(u.t) == 0);
    GT_ASSERT(gates_widget_revision(u.t, u.tb) == 2);

    /* Backspace on the selection is a text change. */
    GT_ASSERT(key(u.t, GATES_KEY_BACKSPACE, false));
    GT_ASSERT(gates_tree_dispatch_events(u.t, 0) == 0);
    GT_ASSERT(r.n == 2 && rec_text_is(&r.ev[1], "") && r.ev[1].revision == 3);

    /* Delete at the end of an empty box changes nothing. */
    GT_ASSERT(key(u.t, GATES_KEY_DELETE, false));
    GT_ASSERT(gates_tree_pending_events(u.t) == 0);
    gates_tree_destroy(u.t);
}

static void test_composition_events(void) {
    ui_t u = make_ui((gates_allocator_t){0});
    recorder_t r = {0};
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.tb, record, &r));
    gates_tree_set_focus(u.t, u.tb);
    gates_text_edit_set_caret(gates_textbox_edit(u.t, u.tb), 3, false);

    GT_ASSERT(gates_input_preedit(u.t, GATES_STR("g"), 1) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_input_preedit(u.t, GATES_STR("ge"), 2) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_tree_pending_events(u.t) == 1);
    (void)gates_tree_dispatch_events(u.t, 0);
    GT_ASSERT(r.n == 1 && r.ev[0].kind == GATES_EVENT_PREEDIT_CHANGED);
    GT_ASSERT(rec_text_is(&r.ev[0], "ge"));
    GT_ASSERT(gates_widget_revision(u.t, u.tb) == 0); /* committed text untouched */

    /* Commit: the preedit empties and the committed text changes, one event each. */
    GT_ASSERT(gates_input_commit(u.t, GATES_STR("G")) == GATES_INPUT_CONSUMED);
    (void)gates_tree_dispatch_events(u.t, 0);
    GT_ASSERT(r.n == 3);
    bool saw_empty_preedit = false, saw_text = false;
    for (int i = 1; i < r.n; i++) {
        if (r.ev[i].kind == GATES_EVENT_PREEDIT_CHANGED && rec_text_is(&r.ev[i], "")) {
            saw_empty_preedit = true;
        }
        if (r.ev[i].kind == GATES_EVENT_TEXT_CHANGED && rec_text_is(&r.ev[i], "abcG")) {
            saw_text = true;
        }
    }
    GT_ASSERT(saw_empty_preedit && saw_text);

    /* Cancel reports the empty preedit and no text change. */
    GT_ASSERT(gates_input_preedit(u.t, GATES_STR("h"), 1) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_input_preedit_cancel(u.t) == GATES_INPUT_CONSUMED);
    (void)gates_tree_dispatch_events(u.t, 0);
    GT_ASSERT(r.n == 4 && r.ev[3].kind == GATES_EVENT_PREEDIT_CHANGED);
    GT_ASSERT(rec_text_is(&r.ev[3], ""));
    gates_tree_destroy(u.t);
}

static void test_program_changes_are_silent_unless_notified(void) {
    ui_t u = make_ui((gates_allocator_t){0});
    recorder_t r = {0};
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.tb, record, &r));
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.cb, record, &r));

    GT_ASSERT_OK(gates_textbox_set_text(u.t, u.tb, GATES_STR("set")));
    GT_ASSERT_OK(gates_checkbox_set_checked(u.t, u.cb, true));
    GT_ASSERT(gates_tree_pending_events(u.t) == 0);
    GT_ASSERT(gates_widget_revision(u.t, u.tb) == 1);
    GT_ASSERT(gates_widget_revision(u.t, u.cb) == 1);
    /* Identical value: a no-op, no revision. */
    GT_ASSERT_OK(gates_checkbox_set_checked(u.t, u.cb, true));
    GT_ASSERT(gates_widget_revision(u.t, u.cb) == 1);

    GT_ASSERT_OK(gates_widget_notify(u.t, u.tb));
    GT_ASSERT_OK(gates_widget_notify(u.t, u.cb));
    (void)gates_tree_dispatch_events(u.t, 0);
    GT_ASSERT(r.n == 2);
    GT_ASSERT(r.ev[0].kind == GATES_EVENT_TEXT_CHANGED && r.ev[0].origin == GATES_ORIGIN_PROGRAM);
    GT_ASSERT(rec_text_is(&r.ev[0], "set"));
    GT_ASSERT(r.ev[1].kind == GATES_EVENT_VALUE_CHANGED && r.ev[1].checked);
    GT_ASSERT(r.ev[1].origin == GATES_ORIGIN_PROGRAM);

    /* The generic getter reads the textbox's real text. */
    gates_str_t s = gates_widget_text(u.t, u.tb);
    GT_ASSERT(s.size == 3 && memcmp(s.ptr, "set", 3) == 0);

    GT_ASSERT(gates_widget_notify(u.t, gates_tree_root(u.t)) == PROVEN_ERR_INVALID_ARG);
    gates_tree_destroy(u.t);
}

static int legacy_clicks;
static void legacy_click(gates_tree_t *t, gates_node_t n, void *user) {
    (void)t; (void)n; (void)user;
    legacy_clicks++;
}

static void test_pointer_events(void) {
    ui_t u = make_ui((gates_allocator_t){0});
    recorder_t r = {0};
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.cb, record, &r));
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.btn, record, &r));

    click(u.t, u.cb);
    click(u.t, u.btn);
    click(u.t, u.btn);
    GT_ASSERT(gates_checkbox_checked(u.t, u.cb));
    GT_ASSERT(gates_tree_pending_events(u.t) == 3); /* activations never coalesce */
    (void)gates_tree_dispatch_events(u.t, 0);
    GT_ASSERT(r.n == 3);
    GT_ASSERT(r.ev[0].kind == GATES_EVENT_VALUE_CHANGED && r.ev[0].checked);
    GT_ASSERT(r.ev[0].origin == GATES_ORIGIN_USER);
    GT_ASSERT(r.ev[1].kind == GATES_EVENT_ACTIVATED && same(r.ev[1].source, u.btn));
    GT_ASSERT(r.ev[2].kind == GATES_EVENT_ACTIVATED);

    /* Toggle twice before dispatch: one event with the latest state. */
    click(u.t, u.cb);
    click(u.t, u.cb);
    GT_ASSERT(gates_tree_pending_events(u.t) == 1);
    (void)gates_tree_dispatch_events(u.t, 0);
    GT_ASSERT(r.n == 4 && r.ev[3].checked);

    /* No handler, no queued work. */
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.btn, nullptr, nullptr));
    click(u.t, u.btn);
    GT_ASSERT(gates_tree_pending_events(u.t) == 0);
    gates_tree_destroy(u.t);
}

/* Handler that destroys the node it hears from. */
static void destroy_source(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    recorder_t *r = user;
    record(tree, ev, r);
    GT_ASSERT_OK(gates_node_destroy(tree, ev->source));
}

static void test_handler_may_destroy_source(void) {
    ui_t u = make_ui((gates_allocator_t){0});
    recorder_t r = {0};
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.btn, destroy_source, &r));
    click(u.t, u.btn);
    click(u.t, u.btn);
    GT_ASSERT(gates_tree_pending_events(u.t) == 2);
    GT_ASSERT(gates_tree_dispatch_events(u.t, 0) == 0);
    GT_ASSERT(r.n == 1);            /* the second event's source is gone */
    GT_ASSERT(!gates_node_is_valid(u.t, u.btn));
    GT_ASSERT_OK(gates_tree_flush_destroys(u.t));
    gates_tree_destroy(u.t);
}

/* Handler that mutates the source (growing buffers) while reading ev->text. */
typedef struct mutate_ctx_t {
    gates_node_t other;
    bool text_intact;
    int calls;
} mutate_ctx_t;

static void mutate_while_reading(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    mutate_ctx_t *m = user;
    m->calls++;
    char before[64];
    gates_usize_t n = ev->text.size < sizeof before ? ev->text.size : sizeof before;
    memcpy(before, ev->text.ptr, n);
    /* Long enough to force the tree's event text buffer to grow. */
    const char *big = "0123456789012345678901234567890123456789012345678901234567890123"
                      "0123456789012345678901234567890123456789012345678901234567890123";
    GT_ASSERT_OK(gates_textbox_set_text(tree, ev->source, (gates_str_t){
        .ptr = (const gates_u8 *)big, .size = strlen(big) }));
    GT_ASSERT_OK(gates_widget_set_text(tree, m->other, ev->text));
    m->text_intact = ev->text.size == n && memcmp(before, ev->text.ptr, n) == 0;
}

static void test_payload_survives_nested_setters(void) {
    ui_t u = make_ui((gates_allocator_t){0});
    mutate_ctx_t m = { .other = u.tb2 };
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.tb, mutate_while_reading, &m));
    gates_tree_set_focus(u.t, u.tb);
    GT_ASSERT(gates_input_char(u.t, 'q') == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_tree_dispatch_events(u.t, 0) == 0);
    GT_ASSERT(m.calls == 1 && m.text_intact);
    gates_str_t s = gates_textbox_text(u.t, u.tb2);
    GT_ASSERT(s.size == 4 && memcmp(s.ptr, "abcq", 4) == 0);
    GT_ASSERT(gates_tree_pending_events(u.t) == 0); /* nested program setters are silent */
    gates_tree_destroy(u.t);
}

/* Nested notify inside a handler waits for the next dispatch call; re-entry is a no-op. */
typedef struct nest_ctx_t {
    recorder_t rec;
    gates_node_t target;
    gates_u32 reentry_result;
} nest_ctx_t;

static void notify_other(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    nest_ctx_t *c = user;
    record(tree, ev, &c->rec);
    if (ev->kind == GATES_EVENT_ACTIVATED) {
        GT_ASSERT_OK(gates_widget_notify(tree, c->target));
        c->reentry_result = gates_tree_dispatch_events(tree, 0);
    }
}

static void test_nested_and_bounded_dispatch(void) {
    ui_t u = make_ui((gates_allocator_t){0});
    nest_ctx_t c = { .target = u.cb };
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.btn, notify_other, &c));
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.cb, notify_other, &c));
    click(u.t, u.btn);
    GT_ASSERT(gates_tree_dispatch_events(u.t, 0) == 1); /* the nested one is left */
    GT_ASSERT(c.rec.n == 1);
    GT_ASSERT(c.reentry_result == 1);                   /* re-entry delivered nothing */
    GT_ASSERT(gates_tree_dispatch_events(u.t, 0) == 0);
    GT_ASSERT(c.rec.n == 2 && c.rec.ev[1].kind == GATES_EVENT_VALUE_CHANGED);

    /* A per-call limit leaves the rest queued. */
    recorder_t r = {0};
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.btn, record, &r));
    click(u.t, u.btn);
    click(u.t, u.btn);
    click(u.t, u.btn);
    GT_ASSERT(gates_tree_dispatch_events(u.t, 2) == 1);
    GT_ASSERT(r.n == 2);
    GT_ASSERT(gates_tree_dispatch_events(u.t, 2) == 0);
    GT_ASSERT(r.n == 3);

    /* Removing the handler drops what it had not received yet. */
    click(u.t, u.btn);
    GT_ASSERT(gates_tree_pending_events(u.t) == 1);
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.btn, nullptr, nullptr));
    GT_ASSERT(gates_tree_dispatch_events(u.t, 0) == 0);
    GT_ASSERT(r.n == 3);
    gates_tree_destroy(u.t);
}

static void test_reservation_failure_leaves_state(void) {
    fail_alloc_t fa = { .inner = proven_heap_allocator() };
    gates_allocator_t alloc = { .ctx = &fa, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc,
                                .free_fn = fa_free };
    ui_t u = make_ui(alloc);
    recorder_t r = {0};
    legacy_clicks = 0;
    gates_node_t b2 = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_button_create(u.t, gates_tree_root(u.t), GATES_STR("legacy"),
                                     legacy_click, nullptr, &b2));
    GT_ASSERT_OK(gates_layout_run(u.t, (gates_size_t){ 400, 300 }, be));
    GT_ASSERT_OK(gates_widget_set_handler(u.t, b2, record, &r));
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.tb, record, &r));
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.cb, record, &r));
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.btn, record, &r));
    gates_tree_set_focus(u.t, u.tb);
    gates_text_edit_set_caret(gates_textbox_edit(u.t, u.tb), 3, false);
    GT_ASSERT(gates_input_take_error(u.t) == GATES_OK);

    /* The queue has never been allocated: every first event needs memory. */
    fa.fail = true;
    GT_ASSERT(gates_input_char(u.t, 'x') == GATES_INPUT_FAILED);
    GT_ASSERT(gates_textbox_text(u.t, u.tb).size == 3);
    GT_ASSERT(gates_widget_revision(u.t, u.tb) == 0);
    GT_ASSERT(gates_input_take_error(u.t) == PROVEN_ERR_NOMEM);
    GT_ASSERT(gates_input_take_error(u.t) == GATES_OK); /* taken */

    GT_ASSERT(key(u.t, GATES_KEY_BACKSPACE, false));      /* consumed ... */
    GT_ASSERT(gates_textbox_text(u.t, u.tb).size == 3);   /* ... but not applied */
    GT_ASSERT(gates_input_take_error(u.t) == PROVEN_ERR_NOMEM);

    click(u.t, u.cb);
    GT_ASSERT(!gates_checkbox_checked(u.t, u.cb));
    click(u.t, u.btn);
    GT_ASSERT(gates_input_take_error(u.t) == PROVEN_ERR_NOMEM);
    GT_ASSERT(gates_tree_pending_events(u.t) == 0);
    GT_ASSERT(gates_widget_notify(u.t, u.tb) == PROVEN_ERR_NOMEM);
    /* A legacy on_click is not run when its activation could not be recorded. */
    click(u.t, b2);
    GT_ASSERT(legacy_clicks == 0);
    GT_ASSERT(gates_input_take_error(u.t) == PROVEN_ERR_NOMEM);

    /* With memory back, everything works and nothing stale is delivered. */
    fa.fail = false;
    gates_tree_set_focus(u.t, u.tb); /* the clicks above moved focus away */
    GT_ASSERT(gates_input_char(u.t, 'x') == GATES_INPUT_CONSUMED);
    click(u.t, u.cb);
    (void)gates_tree_dispatch_events(u.t, 0);
    GT_ASSERT(r.n == 2);
    GT_ASSERT(rec_text_is(&r.ev[0], "abcx"));
    GT_ASSERT(r.ev[1].kind == GATES_EVENT_VALUE_CHANGED && r.ev[1].checked);

    click(u.t, b2);
    GT_ASSERT(legacy_clicks == 1);
    (void)gates_tree_dispatch_events(u.t, 0);
    gates_tree_destroy(u.t);
}

static void test_cancel_pointer(void) {
    ui_t u = make_ui((gates_allocator_t){0});
    recorder_t r = {0};
    legacy_clicks = 0;
    gates_node_t b2 = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_button_create(u.t, gates_tree_root(u.t), GATES_STR("legacy"),
                                     legacy_click, nullptr, &b2));
    GT_ASSERT_OK(gates_layout_run(u.t, (gates_size_t){ 400, 300 }, be));
    GT_ASSERT_OK(gates_widget_set_handler(u.t, u.btn, record, &r));

    pointer(u.t, GATES_POINTER_DOWN, center(u.t, u.btn));
    GT_ASSERT(gates_widget_pressed(u.t, u.btn));
    gates_input_cancel_pointer(u.t);
    GT_ASSERT(!gates_widget_pressed(u.t, u.btn));
    GT_ASSERT((gates_tree_dirty(u.t) & GATES_TREE_DIRTY_PAINT) != 0);
    pointer(u.t, GATES_POINTER_UP, center(u.t, u.btn)); /* stale release */
    GT_ASSERT(gates_tree_pending_events(u.t) == 0);

    pointer(u.t, GATES_POINTER_DOWN, center(u.t, b2));
    gates_input_cancel_pointer(u.t);
    pointer(u.t, GATES_POINTER_UP, center(u.t, b2));
    GT_ASSERT(legacy_clicks == 0);
    gates_tree_destroy(u.t);

    /* A split handle drag is cancelled too: later moves change nothing. */
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_SPLIT));
    GT_ASSERT_OK(gates_layout_set_split(t, root, GATES_SPLIT_HORIZONTAL, 500));
    gates_node_t a = GATES_NODE_NULL, b = GATES_NODE_NULL;
    GT_ASSERT_OK(gates_panel_create(t, root, &a));
    GT_ASSERT_OK(gates_panel_create(t, root, &b));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 406, 100 }, be));
    gates_rect_t pa = gates_node_layout_rect(t, a);
    gates_point_t handle = { pa.x + pa.w + GATES_SPLIT_HANDLE_PX / 2, 50 };
    pointer(t, GATES_POINTER_DOWN, handle);
    gates_input_cancel_pointer(t);
    pointer(t, GATES_POINTER_MOVE, (gates_point_t){ handle.x + 100, 50 });
    pointer(t, GATES_POINTER_UP, (gates_point_t){ handle.x + 100, 50 });
    GT_ASSERT(gates_layout_split_ratio(t, root) == 500);
    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    test_typing_notifies_once_per_batch();
    test_composition_events();
    test_program_changes_are_silent_unless_notified();
    test_pointer_events();
    test_handler_may_destroy_source();
    test_payload_survives_nested_setters();
    test_nested_and_bounded_dispatch();
    test_reservation_failure_leaves_state();
    test_cancel_pointer();
    return gt_report("test_events");
}
