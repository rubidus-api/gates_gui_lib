/* T049: input controls and plumbing (docs/tests/cases/T049-inputs.md, plan-0019) -
 * event bubbling, deferred calls. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/command.h>
#include <gates/overlay.h>
#include <gates/frame.h>
#include <gates/access.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <string.h>

static const gates_text_backend_t *be;
static const gates_theme_t *theme;
#define VW 480
#define VH 360

static void layout(gates_tree_t *t) {
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VW, VH }, be));
}
static void pointer(gates_tree_t *t, gates_pointer_action_t a, gates_point_t p) {
    gates_pointer_event_t e = { .action = a, .button = GATES_BUTTON_LEFT, .pos = p };
    (void)gates_input_pointer(t, &e);
}
static gates_point_t center(const gates_tree_t *t, gates_node_t n) {
    gates_rect_t r = gates_node_layout_rect(t, n);
    return (gates_point_t){ r.x + r.w / 2, r.y + r.h / 2 };
}
static void click(gates_tree_t *t, gates_node_t n) {
    pointer(t, GATES_POINTER_DOWN, center(t, n));
    pointer(t, GATES_POINTER_UP, center(t, n));
}
static void dispatch(gates_tree_t *t) { (void)gates_tree_dispatch_events(t, 0); }

typedef struct rec_t {
    gates_event_kind_t kind[32];
    gates_node_t source[32];
    gates_u32 result[32];
    bool checked[32];
    char text[32][16];
    int n;
    int tag;           /* which handler recorded */
} rec_t;

static void record(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    rec_t *r = user;
    if (r->n < 32) {
        r->kind[r->n] = ev->kind;
        r->source[r->n] = ev->source;
        r->result[r->n] = ev->result;
        r->checked[r->n] = ev->checked;
        gates_usize_t k = ev->text.size < 15 ? ev->text.size : 15;
        memcpy(r->text[r->n], ev->text.ptr != nullptr ? (const char *)ev->text.ptr : "", k);
        r->text[r->n][k] = 0;
        r->n++;
    }
}

/* -- bubbling ---------------------------------------------------------------------- */

static void test_bubbling(void) {
    gates_tree_t *t;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), pad, keys[3], own, box, check, inner, deep;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_panel_create(t, root, &pad));
    GT_ASSERT_OK(gates_layout_set(t, pad, GATES_LAYOUT_KIND_ROW));
    for (int i = 0; i < 3; i++) {
        char label[2] = { (char)('1' + i), 0 };
        GT_ASSERT_OK(gates_button_create(t, pad, (gates_str_t){ .ptr = (const gates_u8 *)label, .size = 1 },
                                         nullptr, nullptr, &keys[i]));
    }
    GT_ASSERT_OK(gates_button_create(t, pad, GATES_STR("own"), nullptr, nullptr, &own));
    GT_ASSERT_OK(gates_textbox_create(t, pad, GATES_STR(""), 6, &box));
    GT_ASSERT_OK(gates_checkbox_create(t, pad, GATES_STR("c"), false, nullptr, nullptr, &check));
    GT_ASSERT_OK(gates_panel_create(t, root, &inner));
    GT_ASSERT_OK(gates_layout_set(t, inner, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_button_create(t, inner, GATES_STR("deep"), nullptr, nullptr, &deep));
    rec_t pad_rec = { .tag = 1 }, own_rec = { .tag = 2 }, root_rec = { .tag = 3 };
    /* Nothing listens: nothing is queued. */
    layout(t);
    click(t, keys[0]);
    GT_ASSERT(gates_tree_pending_events(t) == 0);
    /* One handler on the container hears all its buttons; source = the button. */
    GT_ASSERT_OK(gates_node_set_bubble_handler(t, pad, record, &pad_rec));
    GT_ASSERT_OK(gates_widget_set_handler(t, own, record, &own_rec));
    for (int i = 0; i < 3; i++) click(t, keys[i]);
    click(t, own);
    dispatch(t);
    GT_ASSERT(pad_rec.n == 3 && own_rec.n == 1);
    for (int i = 0; i < 3; i++) {
        GT_ASSERT(pad_rec.kind[i] == GATES_EVENT_ACTIVATED && gates_node_eq(pad_rec.source[i], keys[i]));
    }
    /* Payloads travel with it: text and checked state. */
    gates_tree_set_focus(t, box);
    GT_ASSERT(gates_input_char(t, 'h') == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_input_char(t, 'i') == GATES_INPUT_CONSUMED);
    click(t, check);
    pad_rec.n = 0;
    dispatch(t);
    GT_ASSERT(pad_rec.n == 2);
    GT_ASSERT(pad_rec.kind[0] == GATES_EVENT_TEXT_CHANGED && strcmp(pad_rec.text[0], "hi") == 0);
    GT_ASSERT(pad_rec.kind[1] == GATES_EVENT_VALUE_CHANGED && pad_rec.checked[1]);
    /* Only the nearest: a root handler does not hear what the pad hears; it hears the rest. */
    GT_ASSERT_OK(gates_node_set_bubble_handler(t, root, record, &root_rec));
    pad_rec.n = 0;
    click(t, keys[1]);
    click(t, deep);
    dispatch(t);
    GT_ASSERT(pad_rec.n == 1 && root_rec.n == 1 && gates_node_eq(root_rec.source[0], deep));
    /* A dialog is a root of its own: its button does not reach the window's handler. */
    gates_node_t dlg, content, yes;
    GT_ASSERT_OK(gates_dialog_open(t, &(gates_dialog_desc_t){ .title = GATES_STR("d") }, &dlg, &content));
    GT_ASSERT_OK(gates_button_create(t, content, GATES_STR("yes"), nullptr, nullptr, &yes));
    layout(t);
    click(t, yes);
    dispatch(t);
    GT_ASSERT(root_rec.n == 1);
    rec_t dlg_rec = {0};
    GT_ASSERT_OK(gates_node_set_bubble_handler(t, content, record, &dlg_rec));
    click(t, yes);
    dispatch(t);
    GT_ASSERT(dlg_rec.n == 1 && root_rec.n == 1);
    GT_ASSERT_OK(gates_dialog_close(t, dlg, GATES_DIALOG_CANCELED));
    dispatch(t);
    layout(t);
    /* Removing: events queued before are dropped when nobody hears them any more. */
    pad_rec.n = root_rec.n = 0;
    click(t, keys[2]);
    GT_ASSERT_OK(gates_node_set_bubble_handler(t, pad, nullptr, nullptr));
    dispatch(t);
    GT_ASSERT(pad_rec.n == 0 && root_rec.n == 1);             /* now the root hears it */
    GT_ASSERT_OK(gates_node_set_bubble_handler(t, root, nullptr, nullptr));
    click(t, keys[2]);
    GT_ASSERT(gates_tree_pending_events(t) == 0);
    /* A destroyed node's entry is not inherited by the node that reuses its slot. */
    GT_ASSERT_OK(gates_node_set_bubble_handler(t, inner, record, &pad_rec));
    GT_ASSERT_OK(gates_node_destroy(t, inner));
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    gates_node_t again, b2;
    GT_ASSERT_OK(gates_panel_create(t, root, &again));
    GT_ASSERT_OK(gates_layout_set(t, again, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_button_create(t, again, GATES_STR("b2"), nullptr, nullptr, &b2));
    layout(t);
    GT_ASSERT(!gates_rect_is_empty(gates_node_layout_rect(t, b2)));
    click(t, b2);
    GT_ASSERT(gates_tree_pending_events(t) == 0);
    GT_ASSERT(gates_node_set_bubble_handler(t, GATES_NODE_NULL, record, nullptr) == PROVEN_ERR_INVALID_ARG);
    /* A bubble handler hears descendants, not its own node. */
    rec_t self_rec = {0};
    GT_ASSERT_OK(gates_node_set_bubble_handler(t, b2, record, &self_rec));
    click(t, b2);
    GT_ASSERT(gates_tree_pending_events(t) == 0);
    gates_tree_destroy(t);
}

/* -- deferred calls ------------------------------------------------------------------- */

typedef struct defer_log_t {
    gates_u32 keys[16];
    int n;
    int again;          /* defer the same key once more from inside */
    gates_tree_t *tree;
} defer_log_t;

static void on_defer(gates_tree_t *tree, gates_u32 key, void *user) {
    defer_log_t *d = user;
    if (d->n < 16) d->keys[d->n++] = key;
    if (d->again > 0) {
        d->again--;
        GT_ASSERT_OK(gates_tree_defer(tree, key, on_defer, d));
    }
}

static void on_defer_other(gates_tree_t *tree, gates_u32 key, void *user) {
    (void)tree;
    defer_log_t *d = user;
    if (d->n < 16) d->keys[d->n++] = 1000 + key;
}

typedef struct total_t {
    gates_tree_t *tree;
    int recomputes;
    int changes;
} total_t;

static void recompute(gates_tree_t *tree, gates_u32 key, void *user) {
    (void)tree;
    (void)key;
    ((total_t *)user)->recomputes++;
}

static void on_field(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)ev;
    total_t *tt = user;
    tt->changes++;
    GT_ASSERT_OK(gates_tree_defer(tree, 7, recompute, tt));
}

static void test_defer(void) {
    gates_tree_t *t;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    defer_log_t d = { .tree = t };
    /* Many requests, one call per key, in the order first asked; the latest fn wins. */
    GT_ASSERT_OK(gates_tree_defer(t, 1, on_defer, &d));
    GT_ASSERT_OK(gates_tree_defer(t, 2, on_defer, &d));
    GT_ASSERT_OK(gates_tree_defer(t, 1, on_defer, &d));
    GT_ASSERT_OK(gates_tree_defer(t, 3, on_defer, &d));
    GT_ASSERT_OK(gates_tree_defer(t, 3, on_defer_other, &d));
    GT_ASSERT(gates_tree_dispatch_events(t, 0) == 0);
    GT_ASSERT(d.n == 3 && d.keys[0] == 1 && d.keys[1] == 2 && d.keys[2] == 1003);
    GT_ASSERT(gates_tree_dispatch_events(t, 0) == 0);
    GT_ASSERT(d.n == 3);                                      /* each ran once */
    /* Cancel. */
    GT_ASSERT_OK(gates_tree_defer(t, 4, on_defer, &d));
    GT_ASSERT(gates_tree_cancel_defer(t, 4));
    GT_ASSERT(!gates_tree_cancel_defer(t, 4));
    GT_ASSERT(gates_tree_dispatch_events(t, 0) == 0 && d.n == 3);
    /* Asked for again from inside: the next dispatch, reported as remaining work. */
    d.again = 1;
    GT_ASSERT_OK(gates_tree_defer(t, 5, on_defer, &d));
    GT_ASSERT(gates_tree_dispatch_events(t, 0) == 1);
    GT_ASSERT(d.n == 4);
    GT_ASSERT(gates_tree_dispatch_events(t, 0) == 0);
    GT_ASSERT(d.n == 5 && d.keys[4] == 5);
    GT_ASSERT(gates_tree_defer(t, 1, nullptr, nullptr) == PROVEN_ERR_INVALID_ARG);
    /* While a partial dispatch leaves events queued, deferred calls wait. */
    gates_node_t btn;
    rec_t br = {0};
    GT_ASSERT_OK(gates_button_create(t, gates_tree_root(t), GATES_STR("b"), nullptr, nullptr, &btn));
    GT_ASSERT_OK(gates_widget_set_handler(t, btn, record, &br));
    layout(t);
    click(t, btn);
    click(t, btn);
    d.n = 0;
    GT_ASSERT_OK(gates_tree_defer(t, 9, on_defer, &d));
    GT_ASSERT(gates_tree_dispatch_events(t, 1) == 2);          /* one event and the call left */
    GT_ASSERT(br.n == 1 && d.n == 0);
    GT_ASSERT(gates_tree_dispatch_events(t, 0) == 0);
    GT_ASSERT(br.n == 2 && d.n == 1);
    /* The use case: every field change asks for one recompute, after all the events. */
    total_t tt = { .tree = t };
    gates_node_t root = gates_tree_root(t), boxes[3];
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    for (int i = 0; i < 3; i++) {
        GT_ASSERT_OK(gates_textbox_create(t, root, GATES_STR(""), 5, &boxes[i]));
    }
    GT_ASSERT_OK(gates_node_set_bubble_handler(t, root, on_field, &tt));
    layout(t);
    for (int i = 0; i < 3; i++) {
        gates_tree_set_focus(t, boxes[i]);
        GT_ASSERT(gates_input_char(t, 'x') == GATES_INPUT_CONSUMED);
    }
    dispatch(t);
    GT_ASSERT(tt.changes == 3 && tt.recomputes == 1);
    /* Deferred work during a nested dispatch waits for the outer one. */
    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_bubbling();
    test_defer();
    return gt_report("test_inputs");
}
