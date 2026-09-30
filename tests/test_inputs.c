/* T049: input controls and plumbing (docs/tests/cases/T049-inputs.md, plan-0019) -
 * event bubbling, deferred calls. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/command.h>
#include <gates/overlay.h>
#include <gates/frame.h>
#include <gates/access.h>
#include <gates/inputs.h>
#include <gates/form.h>
#include <gates/timer.h>
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
static bool keyx(gates_tree_t *t, gates_key_t k, bool ctrl) {
    gates_key_event_t e = { .key = k, .down = true, .ctrl = ctrl };
    return gates_input_key(t, &e);
}
static bool key(gates_tree_t *t, gates_key_t k) { return keyx(t, k, false); }
static void type(gates_tree_t *t, const char *s) {
    for (; *s; s++) GT_ASSERT(gates_input_char(t, (gates_u8)*s) == GATES_INPUT_CONSUMED);
}
static bool seq(gates_str_t a, const char *b) {
    return a.size == strlen(b) && (a.size == 0 || memcmp(a.ptr, b, a.size) == 0);
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
static gates_u64 fake_now(void *ctx) { return *(gates_u64 *)ctx; }
static void fake_changed(void *ctx) { (void)ctx; }
static void wheel(gates_tree_t *t, gates_point_t p, float notches) {
    gates_pointer_event_t e = { .action = GATES_POINTER_WHEEL, .pos = p, .wheel = { 0, notches } };
    (void)gates_input_pointer(t, &e);
}

typedef struct rec_t {
    gates_event_kind_t kind[32];
    gates_node_t source[32];
    gates_u32 result[32];
    gates_i64 value[32];
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
        r->value[r->n] = ev->value;
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


/* -- number formatting ---------------------------------------------------------------- */

static void test_range_text(void) {
    char b[32];
    GT_ASSERT(gates_range_format(125, 100, b, sizeof b) == 4 && strcmp(b, "1.25") == 0);
    gates_range_format(-5, 100, b, sizeof b);
    GT_ASSERT(strcmp(b, "-0.05") == 0);
    gates_range_format(7, 0, b, sizeof b);
    GT_ASSERT(strcmp(b, "7") == 0);
    gates_range_format(-1200, 10, b, sizeof b);
    GT_ASSERT(strcmp(b, "-120.0") == 0);
    gates_range_format(INT64_MIN, 1, b, sizeof b);
    GT_ASSERT(strcmp(b, "-9223372036854775808") == 0);
    GT_ASSERT(gates_range_format(125, 100, b, 3) == 4 && strcmp(b, "1.") == 0);
    gates_i64 v = 0;
    GT_ASSERT(gates_range_parse(GATES_STR("1.25"), 100, &v) && v == 125);
    GT_ASSERT(gates_range_parse(GATES_STR("1,5"), 100, &v) && v == 150);
    GT_ASSERT(gates_range_parse(GATES_STR("-3"), 1000, &v) && v == -3000);
    GT_ASSERT(gates_range_parse(GATES_STR("+42"), 0, &v) && v == 42);
    GT_ASSERT(gates_range_parse(GATES_STR(" 8 "), 0, &v) && v == 8);
    GT_ASSERT(gates_range_parse(GATES_STR(".5"), 10, &v) && v == 5);
    GT_ASSERT(!gates_range_parse(GATES_STR("1.234"), 100, &v));
    GT_ASSERT(!gates_range_parse(GATES_STR("1.5"), 1, &v));
    GT_ASSERT(!gates_range_parse(GATES_STR("12a"), 0, &v));
    GT_ASSERT(!gates_range_parse(GATES_STR(""), 0, &v));
    GT_ASSERT(!gates_range_parse(GATES_STR("-"), 0, &v));
    GT_ASSERT(!gates_range_parse(GATES_STR("."), 10, &v));
    GT_ASSERT(!gates_range_parse(GATES_STR("99999999999999999999"), 0, &v));
    GT_ASSERT(!gates_range_parse(GATES_STR("9223372036854775808"), 0, &v));
    GT_ASSERT(gates_range_parse(GATES_STR("-9223372036854775808"), 0, &v) && v == INT64_MIN);
    GT_ASSERT(!gates_range_parse(GATES_STR("1 2"), 0, &v));
}

/* -- spin box --------------------------------------------------------------------------- */

static void test_spin(void) {
    gates_tree_t *t;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), spin, other;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    gates_range_t r = { .min = 0, .max = 1000, .step = 25, .page = 100, .value = 150, .scale = 100 };
    GT_ASSERT_OK(gates_spin_create(t, root, &r, &spin));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("other"), nullptr, nullptr, &other));
    GT_ASSERT(gates_spin_create(t, root, &(gates_range_t){ .min = 5, .max = 1 }, &other) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_spin_create(t, root, &(gates_range_t){ .max = 1, .scale = 7 }, &other) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_spin_create(t, root, &(gates_range_t){ .max = 1, .step = -1 }, &other) == PROVEN_ERR_INVALID_ARG);
    rec_t rec = {0};
    GT_ASSERT_OK(gates_widget_set_handler(t, spin, record, &rec));
    layout(t);
    gates_node_t box = gates_spin_box(t, spin);
    GT_ASSERT(gates_node_kind(t, spin) == GATES_NODE_SPIN && gates_node_kind(t, box) == GATES_NODE_TEXTBOX);
    GT_ASSERT(seq(gates_textbox_text(t, box), "1.50"));
    GT_ASSERT(gates_range_value(t, spin) == 150);
    gates_rect_t sr = gates_node_layout_rect(t, spin);
    GT_ASSERT(sr.h >= 24);
    /* Keys step and page; the text follows; clamped at the ends. */
    gates_tree_set_focus(t, box);
    GT_ASSERT(key(t, GATES_KEY_UP));
    GT_ASSERT(gates_range_value(t, spin) == 175 && seq(gates_textbox_text(t, box), "1.75"));
    GT_ASSERT(key(t, GATES_KEY_PAGE_UP));
    GT_ASSERT(gates_range_value(t, spin) == 275);
    GT_ASSERT(key(t, GATES_KEY_DOWN));
    GT_ASSERT(key(t, GATES_KEY_PAGE_DOWN));
    GT_ASSERT(gates_range_value(t, spin) == 150);
    dispatch(t);
    GT_ASSERT(rec.n == 1 && rec.kind[0] == GATES_EVENT_VALUE_CHANGED && rec.value[0] == 150); /* coalesced */
    GT_ASSERT(gates_node_eq(rec.source[0], spin));
    for (int i = 0; i < 20; i++) (void)key(t, GATES_KEY_PAGE_UP);
    GT_ASSERT(gates_range_value(t, spin) == 1000);
    dispatch(t);
    rec.n = 0;
    GT_ASSERT(key(t, GATES_KEY_UP));
    GT_ASSERT(gates_range_value(t, spin) == 1000);
    dispatch(t);
    GT_ASSERT(rec.n == 0);                                   /* no change, no report */
    /* Typing: invalid while not a number in range; Enter commits. */
    rec.n = 0;
    dispatch(t);
    rec.n = 0;
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 0, (gates_u32)gates_textbox_text(t, box).size));
    type(t, "2.5");
    GT_ASSERT(!gates_textbox_invalid(t, box));
    GT_ASSERT(gates_range_value(t, spin) == 1000);           /* not yet */
    GT_ASSERT(key(t, GATES_KEY_ENTER));
    GT_ASSERT(gates_range_value(t, spin) == 250 && seq(gates_textbox_text(t, box), "2.50"));
    dispatch(t);
    GT_ASSERT(rec.n == 1 && rec.value[0] == 250);
    type(t, "x");
    GT_ASSERT(gates_textbox_invalid(t, box));
    /* Leaving reverts text that is not a number. */
    gates_tree_set_focus(t, other);
    GT_ASSERT(seq(gates_textbox_text(t, box), "2.50") && !gates_textbox_invalid(t, box));
    GT_ASSERT(gates_range_value(t, spin) == 250);
    /* Out of range is invalid too, and reverts. */
    gates_tree_set_focus(t, box);
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 0, (gates_u32)gates_textbox_text(t, box).size));
    type(t, "20");
    GT_ASSERT(gates_textbox_invalid(t, box));
    GT_ASSERT(key(t, GATES_KEY_ENTER));
    GT_ASSERT(seq(gates_textbox_text(t, box), "2.50"));
    /* A valid typed number is committed by leaving too. */
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 0, (gates_u32)gates_textbox_text(t, box).size));
    type(t, "3");
    gates_tree_set_focus(t, other);
    GT_ASSERT(gates_range_value(t, spin) == 300 && seq(gates_textbox_text(t, box), "3.00"));
    /* A step key commits typed text first, then steps. */
    gates_tree_set_focus(t, box);
    GT_ASSERT_OK(gates_textbox_set_selection(t, box, 0, (gates_u32)gates_textbox_text(t, box).size));
    type(t, "4");
    GT_ASSERT(key(t, GATES_KEY_UP));
    GT_ASSERT(gates_range_value(t, spin) == 425);
    /* The arrows: a click steps and focuses the box. */
    gates_tree_set_focus(t, other);
    gates_rect_t br = gates_node_layout_rect(t, box);
    gates_point_t up = { sr.x + sr.w - 4, sr.y + 3 }, down = { sr.x + sr.w - 4, sr.y + sr.h - 3 };
    GT_ASSERT(up.x > br.x + br.w);
    pointer(t, GATES_POINTER_DOWN, up);
    pointer(t, GATES_POINTER_UP, up);
    GT_ASSERT(gates_range_value(t, spin) == 450);
    GT_ASSERT(gates_node_eq(gates_tree_focus(t), box));
    pointer(t, GATES_POINTER_DOWN, down);
    pointer(t, GATES_POINTER_UP, down);
    GT_ASSERT(gates_range_value(t, spin) == 425);
    /* Held, an arrow repeats after a pause, while the pointer stays on it (0.8.0). */
    gates_u64 now = 1000;
    gates_tree_set_clock(t, fake_now, fake_changed, &now);
    pointer(t, GATES_POINTER_DOWN, up);
    GT_ASSERT(gates_range_value(t, spin) == 450);
    now += 399;
    (void)gates_tree_run_timers(t);
    GT_ASSERT(gates_range_value(t, spin) == 450);
    now += 1;
    (void)gates_tree_run_timers(t);
    GT_ASSERT(gates_range_value(t, spin) == 475);
    now += 50;
    (void)gates_tree_run_timers(t);
    GT_ASSERT(gates_range_value(t, spin) == 500);
    pointer(t, GATES_POINTER_MOVE, down); /* on the other arrow: paused */
    now += 50;
    (void)gates_tree_run_timers(t);
    GT_ASSERT(gates_range_value(t, spin) == 500 && gates_tree_timer_count(t) == 1);
    pointer(t, GATES_POINTER_MOVE, up);
    now += 50;
    (void)gates_tree_run_timers(t);
    GT_ASSERT(gates_range_value(t, spin) == 525);
    pointer(t, GATES_POINTER_UP, up); /* release: no more steps, no timer */
    GT_ASSERT(gates_tree_timer_count(t) == 0);
    now += 500;
    (void)gates_tree_run_timers(t);
    GT_ASSERT(gates_range_value(t, spin) == 525);
    pointer(t, GATES_POINTER_DOWN, up); /* cancelled from outside, a tick stops */
    gates_input_cancel_pointer(t);
    now += 400;
    (void)gates_tree_run_timers(t);
    GT_ASSERT(gates_range_value(t, spin) == 550 && gates_tree_timer_count(t) == 0); /* the press's step only */
    /* The wheel steps a focused spin box, over its text or its arrows; unfocused, nothing. */
    wheel(t, center(t, box), 1);
    GT_ASSERT(gates_range_value(t, spin) == 575);
    wheel(t, up, -2);
    GT_ASSERT(gates_range_value(t, spin) == 525);
    wheel(t, up, 0.2f); /* a fine wheel step still steps once */
    GT_ASSERT(gates_range_value(t, spin) == 550);
    gates_tree_set_focus(t, other);
    wheel(t, center(t, box), 1);
    GT_ASSERT(gates_range_value(t, spin) == 550);
    gates_tree_set_focus(t, box);
    pointer(t, GATES_POINTER_DOWN, down);
    pointer(t, GATES_POINTER_UP, down);
    GT_ASSERT(gates_range_value(t, spin) == 525);
    /* The program: silent, clamped; new limits clamp the value. */
    rec.n = 0;
    dispatch(t);
    rec.n = 0;
    GT_ASSERT_OK(gates_range_set_value(t, spin, 5000));
    GT_ASSERT(gates_range_value(t, spin) == 1000 && seq(gates_textbox_text(t, box), "10.00"));
    GT_ASSERT_OK(gates_range_set(t, spin, &(gates_range_t){ .min = 0, .max = 500, .step = 1, .scale = 100 }));
    GT_ASSERT(gates_range_value(t, spin) == 500 && gates_range_get(t, spin).page == 10);
    GT_ASSERT_OK(gates_range_set(t, spin, &(gates_range_t){ .min = 0, .max = 500, .step = 10, .page = 5, .scale = 100 }));
    GT_ASSERT(gates_range_get(t, spin).page == 10);            /* never below a step */
    dispatch(t);
    GT_ASSERT(rec.n == 0);
    GT_ASSERT(gates_range_set_value(t, other, 1) == PROVEN_ERR_INVALID_ARG);
    /* Disabled: no stepping. */
    GT_ASSERT_OK(gates_widget_set_disabled(t, spin, true));
    pointer(t, GATES_POINTER_DOWN, up);
    pointer(t, GATES_POINTER_UP, up);
    GT_ASSERT(gates_range_value(t, spin) == 500);
    GT_ASSERT(!gates_widget_focusable(t, box));
    GT_ASSERT_OK(gates_widget_set_disabled(t, spin, false));
    /* Accessibility: a spinner with a range, named by its label; the edit carries it too. */
    gates_node_t lab;
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("Width"), &lab));
    GT_ASSERT_OK(gates_node_set_labelled_by(t, spin, lab));
    layout(t);
    gates_access_info_t info;
    GT_ASSERT_OK(gates_access_info(t, spin, 0, &info));
    GT_ASSERT(info.role == GATES_ROLE_SPINNER && seq(info.name, "Width"));
    GT_ASSERT(info.has_range && info.range_min == 0 && info.range_max == 500 && info.range_value == 500);
    GT_ASSERT(info.range_scale == 100 && info.range_step == 10 && info.range_page == 10); /* 0.8.0 */
    GT_ASSERT(info.actions & GATES_ACCESS_SET_VALUE);
    GT_ASSERT_OK(gates_access_info(t, box, 0, &info));
    GT_ASSERT(info.role == GATES_ROLE_EDIT && seq(info.name, "Width"));
    GT_ASSERT_OK(gates_access_set_range_value(t, spin, 120));
    GT_ASSERT(gates_range_value(t, spin) == 120 && seq(gates_textbox_text(t, box), "1.20"));
    dispatch(t);
    GT_ASSERT(rec.n == 1 && rec.value[0] == 120);
    GT_ASSERT(gates_access_set_range_value(t, lab, 1) == PROVEN_ERR_INVALID_ARG);
    gates_access_issue_t issues[8];
    GT_ASSERT(gates_access_audit(t, theme, issues, 8) == 0);
    /* In a form: the row's editor. */
    gates_tree_destroy(t);
}

/* -- slider ------------------------------------------------------------------------------ */

static void test_slider(void) {
    gates_tree_t *t;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), s, v;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    gates_range_t r = { .min = -50, .max = 50, .step = 5, .page = 20, .value = 0 };
    GT_ASSERT_OK(gates_slider_create(t, root, &r, false, &s));
    GT_ASSERT_OK(gates_slider_create(t, root, &(gates_range_t){ .min = 0, .max = 10 }, true, &v));
    GT_ASSERT_OK(gates_layout_set_child_align(t, s, GATES_ALIGN_START_V));
    GT_ASSERT_OK(gates_layout_set_child_align(t, v, GATES_ALIGN_START_V));
    GT_ASSERT_OK(gates_node_set_access_name(t, s, GATES_STR("Balance")));
    GT_ASSERT_OK(gates_node_set_access_name(t, v, GATES_STR("Level")));
    rec_t rec = {0};
    GT_ASSERT_OK(gates_widget_set_handler(t, s, record, &rec));
    layout(t);
    gates_rect_t sr = gates_node_layout_rect(t, s), vr = gates_node_layout_rect(t, v);
    GT_ASSERT(sr.w > sr.h && sr.h >= 24);
    GT_ASSERT(vr.h > vr.w && vr.w >= 24);
    /* The wheel: only when focused (0.8.0). */
    wheel(t, (gates_point_t){ sr.x + sr.w / 2, sr.y + sr.h / 2 }, 1);
    GT_ASSERT(gates_range_value(t, s) == 0);
    gates_tree_set_focus(t, s);
    wheel(t, (gates_point_t){ sr.x + sr.w / 2, sr.y + sr.h / 2 }, 2);
    GT_ASSERT(gates_range_value(t, s) == 10);
    wheel(t, (gates_point_t){ sr.x + sr.w / 2, sr.y + sr.h / 2 }, -2);
    GT_ASSERT(gates_range_value(t, s) == 0);
    GT_ASSERT_OK(gates_widget_set_disabled(t, s, true));
    gates_tree_set_focus(t, s); /* focused by the program while disabled: still inert */
    wheel(t, (gates_point_t){ sr.x + sr.w / 2, sr.y + sr.h / 2 }, 1);
    GT_ASSERT(gates_range_value(t, s) == 0);
    GT_ASSERT_OK(gates_widget_set_disabled(t, s, false));
    /* Keys. */
    gates_tree_set_focus(t, s);
    GT_ASSERT(key(t, GATES_KEY_RIGHT));
    GT_ASSERT(gates_range_value(t, s) == 5);
    GT_ASSERT(key(t, GATES_KEY_UP));
    GT_ASSERT(key(t, GATES_KEY_PAGE_UP));
    GT_ASSERT(gates_range_value(t, s) == 30);
    GT_ASSERT(key(t, GATES_KEY_LEFT));
    GT_ASSERT(key(t, GATES_KEY_DOWN));
    GT_ASSERT(key(t, GATES_KEY_PAGE_DOWN));
    GT_ASSERT(gates_range_value(t, s) == 0);
    GT_ASSERT(key(t, GATES_KEY_END));
    GT_ASSERT(gates_range_value(t, s) == 50);
    GT_ASSERT(key(t, GATES_KEY_HOME));
    GT_ASSERT(gates_range_value(t, s) == -50);
    GT_ASSERT(key(t, GATES_KEY_LEFT));
    GT_ASSERT(gates_range_value(t, s) == -50);
    GT_ASSERT(!key(t, GATES_KEY_ENTER));                    /* not the slider's */
    dispatch(t);
    GT_ASSERT(rec.n == 1 && rec.value[0] == -50);
    /* Pointer: the track pages toward the press; the thumb drags in steps. */
    gates_point_t right_end = { sr.x + sr.w - 2, sr.y + sr.h / 2 };
    pointer(t, GATES_POINTER_DOWN, right_end);
    pointer(t, GATES_POINTER_UP, right_end);
    GT_ASSERT(gates_range_value(t, s) == -30);
    GT_ASSERT_OK(gates_range_set_value(t, s, 0));
    layout(t);
    gates_point_t mid = { sr.x + sr.w / 2, sr.y + sr.h / 2 };
    pointer(t, GATES_POINTER_DOWN, mid);                     /* on the thumb */
    GT_ASSERT(gates_range_value(t, s) == 0);
    pointer(t, GATES_POINTER_MOVE, (gates_point_t){ sr.x + sr.w - 1, mid.y });
    GT_ASSERT(gates_range_value(t, s) == 50);
    pointer(t, GATES_POINTER_MOVE, (gates_point_t){ sr.x - 40, mid.y });
    GT_ASSERT(gates_range_value(t, s) == -50);
    pointer(t, GATES_POINTER_MOVE, (gates_point_t){ mid.x + 3, mid.y });
    gates_i64 near = gates_range_value(t, s);
    GT_ASSERT(near % 5 == 0 && near >= 0 && near <= 10);    /* in steps */
    pointer(t, GATES_POINTER_UP, (gates_point_t){ mid.x + 3, mid.y });
    GT_ASSERT(gates_node_eq(gates_tree_focus(t), s));
    /* Vertical: up is more. */
    gates_tree_set_focus(t, v);
    GT_ASSERT(key(t, GATES_KEY_UP));
    GT_ASSERT(gates_range_value(t, v) == 1);
    gates_point_t top = { vr.x + vr.w / 2, vr.y + 2 };
    pointer(t, GATES_POINTER_DOWN, top);
    pointer(t, GATES_POINTER_UP, top);
    GT_ASSERT(gates_range_value(t, v) == 10);                /* page = 10 steps */
    /* At the top of a vertical slider sits its maximum's thumb: drag it down to the minimum. */
    pointer(t, GATES_POINTER_DOWN, top);
    pointer(t, GATES_POINTER_MOVE, (gates_point_t){ top.x, vr.y + vr.h - 1 });
    pointer(t, GATES_POINTER_UP, (gates_point_t){ top.x, vr.y + vr.h - 1 });
    GT_ASSERT(gates_range_value(t, v) == 0);
    /* Ticks paint; accessibility. */
    GT_ASSERT_OK(gates_slider_set_ticks(t, s, 2));
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    int ticks = 0;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(&dl, i);
        if (c->kind == GATES_DRAW_RECT && c->rect.w == 1 && c->rect.y >= sr.y && c->rect.y < sr.y + sr.h) ticks++;
    }
    GT_ASSERT(ticks == 11);                                   /* -50..50 every 10 */
    gates_draw_list_deinit(&dl);
    gates_access_info_t info;
    GT_ASSERT_OK(gates_access_info(t, s, 0, &info));
    GT_ASSERT(info.role == GATES_ROLE_SLIDER && info.has_range && info.range_min == -50 && info.range_max == 50);
    GT_ASSERT(info.range_scale == 1 && info.range_step == 5 && info.range_page == 20);
    GT_ASSERT_OK(gates_access_set_range_value(t, s, 12));
    GT_ASSERT(gates_range_value(t, s) == 12);                 /* any value in range, not only steps */
    GT_ASSERT_OK(gates_access_set_range_value(t, s, 999));
    GT_ASSERT(gates_range_value(t, s) == 50);
    gates_access_issue_t issues[8];
    GT_ASSERT(gates_access_audit(t, theme, issues, 8) == 0);
    GT_ASSERT_OK(gates_widget_set_disabled(t, s, true));
    GT_ASSERT(gates_access_set_range_value(t, s, 0) == PROVEN_ERR_INVALID_STATE);
    gates_tree_destroy(t);
}

/* -- GRID and WRAP layouts ------------------------------------------------------------- */

static gates_rect_t rect(gates_tree_t *t, gates_node_t n) { return gates_node_layout_rect(t, n); }

static gates_node_t label(gates_tree_t *t, gates_node_t parent, const char *s) {
    gates_node_t n;
    GT_ASSERT_OK(gates_label_create(t, parent, (gates_str_t){ .ptr = (const gates_u8 *)s, .size = strlen(s) }, &n));
    return n;
}

static void test_grid(void) {
    gates_tree_t *t;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_i32 adv = be->metrics(be->ctx, GATES_FONT_UI).advance;
    gates_i32 lh = be->metrics(be->ctx, GATES_FONT_UI).line_height;
    gates_node_t root = gates_tree_root(t), g;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_panel_create(t, root, &g));
    GT_ASSERT_OK(gates_layout_set(t, g, GATES_LAYOUT_KIND_GRID));
    GT_ASSERT_OK(gates_layout_set_grid(t, g, 3));
    GT_ASSERT_OK(gates_layout_set_gap(t, g, 4));
    GT_ASSERT(gates_layout_set_grid(t, g, 0) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_layout_set_grid(t, g, GATES_GRID_MAX_COLUMNS + 1) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_layout_set_grid_column_grow(t, g, 3, 1) == PROVEN_ERR_INVALID_ARG);
    /* Row 0: a | bbbb | cc     Row 1: dddddd | (hidden) e | f     Row 2: "wide span" over two | gg */
    gates_node_t a = label(t, g, "a"), b = label(t, g, "bbbb"), c = label(t, g, "cc");
    gates_node_t d = label(t, g, "dddddd"), hid = label(t, g, "hidden"), e = label(t, g, "e"), f = label(t, g, "f");
    gates_node_t w = label(t, g, "wide span"), gg = label(t, g, "gg");
    GT_ASSERT_OK(gates_node_set_hidden(t, hid, true));
    GT_ASSERT_OK(gates_layout_set_child_span(t, w, 2));
    gates_node_t btn;
    GT_ASSERT_OK(gates_button_create(t, g, GATES_STR("tall"), nullptr, nullptr, &btn)); /* row 3, col 0 */
    layout(t);
    GT_ASSERT(gates_layout_validate(t, root));
    gates_rect_t ga = rect(t, g);
    /* Columns: 0 = widest of a, dddddd, (span), tall button; 1 = bbbb/e; 2 = cc/f/gg. */
    gates_i32 btn_w = gates_node_preferred_size(t, btn).w;
    gates_i32 col0 = 6 * adv > btn_w ? 6 * adv : btn_w;
    gates_i32 col1 = 4 * adv;
    GT_ASSERT(rect(t, a).x == ga.x && rect(t, d).x == ga.x);
    GT_ASSERT(rect(t, b).x == ga.x + col0 + 4 && rect(t, e).x == rect(t, b).x);
    GT_ASSERT(rect(t, c).x == rect(t, b).x + col1 + 4 && rect(t, f).x == rect(t, c).x);
    GT_ASSERT(rect(t, a).w == col0);                          /* a cell's width by default */
    /* Rows: each as tall as its tallest; the hidden label takes no cell. */
    GT_ASSERT(rect(t, d).y == rect(t, a).y + lh + 4);
    GT_ASSERT(rect(t, w).y == rect(t, d).y + lh + 4 && rect(t, gg).y == rect(t, w).y);
    /* The span covers two columns and the gap between them. */
    GT_ASSERT(rect(t, w).w == col0 + 4 + col1 && rect(t, gg).x == rect(t, c).x);
    /* A taller child makes a taller row; shorter ones are centred in it. */
    gates_i32 bh = gates_node_preferred_size(t, btn).h;
    GT_ASSERT(rect(t, btn).y == rect(t, w).y + lh + 4 && rect(t, btn).h == bh);
    /* Grow: spare width goes to column 1; START keeps a child's own width. */
    GT_ASSERT_OK(gates_layout_set_grid_column_grow(t, g, 1, 1));
    GT_ASSERT_OK(gates_layout_set_child_align(t, e, GATES_ALIGN_START_V));
    GT_ASSERT_OK(gates_layout_set_child_align(t, f, GATES_ALIGN_END_V));
    layout(t);
    GT_ASSERT(rect(t, c).x + rect(t, c).w == ga.x + VW);      /* the grid fills the width */
    GT_ASSERT(rect(t, b).w > col1 && rect(t, e).w == adv);
    GT_ASSERT(rect(t, f).x + rect(t, f).w == rect(t, c).x + rect(t, c).w);
    /* The grid's own size: all columns and rows. */
    gates_size_t gp = gates_node_preferred_size(t, g);
    GT_ASSERT(gp.w == col0 + col1 + 2 * adv + 2 * 4 && gp.h == 3 * lh + bh + 3 * 4);
    GT_ASSERT(gates_layout_set_child_span(t, w, 0) == PROVEN_ERR_INVALID_ARG);
    /* A short cell beside a taller one is centred in the row. */
    gates_node_t beside = label(t, g, "s");
    layout(t);
    GT_ASSERT(rect(t, beside).y == rect(t, btn).y + (bh - lh) / 2 && rect(t, beside).x == rect(t, b).x);
    gates_tree_destroy(t);

    /* The default is two columns; a span wider than its columns widens the last of them. */
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_panel_create(t, root, &g));
    GT_ASSERT_OK(gates_layout_set(t, g, GATES_LAYOUT_KIND_GRID));
    gates_node_t p0 = label(t, g, "aa"), p1 = label(t, g, "b"), p2 = label(t, g, "c");
    gates_node_t long_span = label(t, g, "a very long spanning text");
    GT_ASSERT_OK(gates_layout_set_child_span(t, long_span, 2));
    layout(t);
    GT_ASSERT(rect(t, p2).x == rect(t, p0).x && rect(t, p2).y > rect(t, p0).y);   /* third cell: row 2 */
    GT_ASSERT(rect(t, p1).x == rect(t, p0).x + 2 * adv);                           /* no gap set */
    gates_i32 span_w = 25 * adv;
    GT_ASSERT(rect(t, long_span).w == span_w && gates_node_preferred_size(t, g).w == span_w);
    GT_ASSERT(rect(t, long_span).y > rect(t, p2).y);          /* a span that does not fit starts a row */
    /* Row grow weights (0.8.0): the spare height goes to rows 1 and 2 as 1:3; rows
     * that do not exist take no share; a row's cells stay centred in it. */
    GT_ASSERT_OK(gates_layout_set_child_grow(t, g, 1));
    GT_ASSERT_OK(gates_layout_set_grid_row_grow(t, g, 1, 1));
    GT_ASSERT_OK(gates_layout_set_grid_row_grow(t, g, 2, 3));
    GT_ASSERT_OK(gates_layout_set_grid_row_grow(t, g, GATES_GRID_MAX_GROW_ROWS - 1, 9));
    GT_ASSERT(gates_layout_set_grid_row_grow(t, g, GATES_GRID_MAX_GROW_ROWS, 1) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_layout_set_grid_row_grow(t, GATES_NODE_NULL, 0, 1) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_layout_set_grid_row_grow(t, g, 2, 2)); /* 1:2 - the rounding goes to row 2 */
    GT_ASSERT_OK(gates_layout_set_gap(t, g, 4));
    gates_node_t p4 = label(t, g, "d"); /* row 3, no weight */
    layout(t);
    gates_rect_t gr = rect(t, g);
    GT_ASSERT(gr.h == VH);
    gates_i32 spare = VH - 4 * lh - 3 * 4, r1 = spare * 1 / 3, r2 = spare - r1;
    GT_ASSERT(spare % 3 != 0); /* the case with a remainder */
    GT_ASSERT(rect(t, p0).y == gr.y && rect(t, p1).y == gr.y);            /* row 0 keeps its height */
    GT_ASSERT(rect(t, p2).y == gr.y + lh + 4 + r1 / 2);
    GT_ASSERT(rect(t, long_span).y == gr.y + 2 * lh + 8 + r1 + r2 / 2);
    GT_ASSERT(rect(t, p4).y == gr.y + 3 * lh + 12 + r1 + r2 && rect(t, p4).y + lh == gr.y + gr.h);
    GT_ASSERT(gates_layout_validate(t, root));
    /* Less than enough height: rows keep their own heights (none shrinks). */
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VW, 2 * lh }, be));
    GT_ASSERT(rect(t, p2).y == rect(t, g).y + lh + 4 && rect(t, long_span).y == rect(t, g).y + 2 * lh + 8);
    gates_tree_destroy(t);
}

/* A grid given less height than it needs (the window's root): rows keep their own heights. */
static void test_grid_short(void) {
    gates_tree_t *t;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_i32 lh = be->metrics(be->ctx, GATES_FONT_UI).line_height;
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_GRID));
    GT_ASSERT_OK(gates_layout_set_grid(t, root, 1));
    gates_node_t r0 = label(t, root, "a"), r1 = label(t, root, "b"), r2 = label(t, root, "c");
    GT_ASSERT_OK(gates_layout_set_grid_row_grow(t, root, 1, 1));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VW, lh }, be));
    GT_ASSERT(rect(t, r0).y == 0 && rect(t, r1).y == lh && rect(t, r2).y == 2 * lh);
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VW, 3 * lh + 10 }, be));
    GT_ASSERT(rect(t, r1).y == lh + 5 && rect(t, r2).y == 2 * lh + 10);
    gates_tree_destroy(t);
}

static void test_wrap(void) {
    gates_tree_t *t;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), wrap, after, chip[6];
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_panel_create(t, root, &wrap));
    GT_ASSERT_OK(gates_layout_set(t, wrap, GATES_LAYOUT_KIND_WRAP));
    GT_ASSERT_OK(gates_layout_set_gap(t, wrap, 6));
    for (int i = 0; i < 6; i++) {
        GT_ASSERT_OK(gates_button_create(t, wrap, GATES_STR("chip"), nullptr, nullptr, &chip[i]));
    }
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("after"), nullptr, nullptr, &after));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 300 }, be));
    gates_i32 cw = gates_node_preferred_size(t, chip[0]).w, ch = gates_node_preferred_size(t, chip[0]).h;
    gates_i32 per = (200 + 6) / (cw + 6);                     /* chips per line */
    GT_ASSERT(per >= 1 && per < 6);
    GT_ASSERT(rect(t, chip[0]).y == rect(t, chip[per - 1]).y);
    GT_ASSERT(rect(t, chip[per]).y == rect(t, chip[0]).y + ch + 6 && rect(t, chip[per]).x == rect(t, chip[0]).x);
    GT_ASSERT(rect(t, chip[1]).x == rect(t, chip[0]).x + cw + 6);
    gates_i32 lines = (6 + per - 1) / per;
    GT_ASSERT(rect(t, wrap).h == lines * ch + (lines - 1) * 6);  /* the height follows the width */
    GT_ASSERT(rect(t, after).y == rect(t, wrap).y + rect(t, wrap).h);
    GT_ASSERT(gates_layout_validate(t, root));
    /* Wider: one line; the next sibling moves up. */
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 800, 300 }, be));
    GT_ASSERT(rect(t, chip[5]).y == rect(t, chip[0]).y && rect(t, wrap).h == ch);
    GT_ASSERT(rect(t, after).y == rect(t, wrap).y + ch);
    /* A child wider than the line gets a line of its own (the first stays on the first line). */
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ cw - 5, 300 }, be));
    GT_ASSERT(rect(t, chip[0]).y == rect(t, wrap).y);
    GT_ASSERT(rect(t, chip[1]).y > rect(t, chip[0]).y && rect(t, chip[1]).x == rect(t, chip[0]).x);
    gates_tree_destroy(t);
}

/* -- group box --------------------------------------------------------------------------- */

static void test_group(void) {
    gates_tree_t *t;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), plain, pc, fold, fc, name, opt, after;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_group_create(t, root, GATES_STR("&Identity"), false, &plain, &pc));
    GT_ASSERT_OK(gates_textbox_create(t, pc, GATES_STR(""), 10, &name));
    GT_ASSERT_OK(gates_node_set_access_name(t, name, GATES_STR("Name")));
    GT_ASSERT_OK(gates_group_create(t, root, GATES_STR("&More options"), true, &fold, &fc));
    GT_ASSERT_OK(gates_checkbox_create(t, fc, GATES_STR("Verbose"), false, nullptr, nullptr, &opt));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("After"), nullptr, nullptr, &after));
    rec_t rec = {0};
    GT_ASSERT_OK(gates_widget_set_handler(t, fold, record, &rec));
    layout(t);
    GT_ASSERT(gates_node_kind(t, plain) == GATES_NODE_GROUP && gates_group_expanded(t, plain));
    /* The content sits inside the frame, below the title. */
    gates_rect_t pr = rect(t, plain), nr = rect(t, name);
    GT_ASSERT(nr.x > pr.x && nr.y > pr.y + 8 && nr.y + nr.h < pr.y + pr.h);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    char buf[256] = "";
    gates_usize_t k = 0;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *cmd = gates_draw_list_at(&dl, i);
        if (cmd->kind != GATES_DRAW_TEXT) continue;
        gates_str_t s = gates_draw_cmd_text(&dl, cmd);
        for (gates_usize_t j = 0; j < s.size && k + 1 < sizeof buf; j++) buf[k++] = (char)s.ptr[j];
    }
    buf[k] = 0;
    GT_ASSERT(strstr(buf, "Identity") && strstr(buf, "More options") && !strstr(buf, "&"));
    gates_draw_list_deinit(&dl);
    /* Tab order: the name box, the collapsible title, its content, After. */
    gates_tree_set_focus(t, name);
    GT_ASSERT(key(t, GATES_KEY_TAB));
    gates_node_t head = gates_tree_focus(t);
    GT_ASSERT(gates_node_kind(t, head) == GATES_NODE_GROUPHEAD);
    GT_ASSERT(rect(t, head).h >= 24);                         /* a pointer target */
    /* A plain group's title is not a Tab stop: the first stop is the name box. */
    gates_tree_set_focus(t, GATES_NODE_NULL);
    GT_ASSERT(key(t, GATES_KEY_TAB));
    GT_ASSERT(gates_node_eq(gates_tree_focus(t), name));
    gates_tree_set_focus(t, head);
    GT_ASSERT(key(t, GATES_KEY_TAB));
    GT_ASSERT(gates_node_eq(gates_tree_focus(t), opt));
    /* Space (on release), Enter and a click toggle; the content goes and comes back. */
    gates_tree_set_focus(t, head);
    gates_key_event_t up = { .key = GATES_KEY_SPACE, .down = false };
    GT_ASSERT(key(t, GATES_KEY_SPACE));
    GT_ASSERT(gates_group_expanded(t, fold));
    GT_ASSERT(gates_input_key(t, &up));
    GT_ASSERT(!gates_group_expanded(t, fold));
    layout(t);
    GT_ASSERT(!gates_widget_focusable(t, opt));
    GT_ASSERT(rect(t, after).y < rect(t, head).y + rect(t, head).h + 20);  /* moved up */
    dispatch(t);
    GT_ASSERT(rec.n == 1 && rec.kind[0] == GATES_EVENT_VALUE_CHANGED && !rec.checked[0]);
    GT_ASSERT(key(t, GATES_KEY_ENTER));
    GT_ASSERT(gates_group_expanded(t, fold));
    dispatch(t);                                             /* (unread toggles coalesce) */
    GT_ASSERT(rec.n == 2 && rec.checked[1]);
    layout(t);
    click(t, head);
    GT_ASSERT(!gates_group_expanded(t, fold));
    dispatch(t);
    GT_ASSERT(rec.n == 3 && !rec.checked[2]);
    /* The program: silent. */
    GT_ASSERT_OK(gates_group_set_expanded(t, fold, true));
    dispatch(t);
    GT_ASSERT(rec.n == 3 && gates_widget_focusable(t, opt));
    GT_ASSERT(gates_group_set_expanded(t, name, true) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_group_set_expanded(t, plain, false) == PROVEN_ERR_INVALID_STATE);
    /* Mnemonics: a plain group's title focuses its first control; a collapsible one toggles. */
    layout(t);
    gates_tree_set_focus(t, after);
    GT_ASSERT(gates_input_mnemonic(t, 'i'));
    GT_ASSERT(gates_node_eq(gates_tree_focus(t), name));
    GT_ASSERT(gates_input_mnemonic(t, 'm'));
    GT_ASSERT(!gates_group_expanded(t, fold) && gates_node_eq(gates_tree_focus(t), head));
    /* Accessibility: groups named by their titles; the collapsible one expands. */
    gates_access_info_t info;
    GT_ASSERT_OK(gates_access_info(t, plain, 0, &info));
    GT_ASSERT(info.role == GATES_ROLE_GROUP && seq(info.name, "Identity") && !(info.states & GATES_ACCESS_EXPANDABLE));
    GT_ASSERT_OK(gates_access_info(t, fold, 0, &info));
    GT_ASSERT(info.role == GATES_ROLE_GROUP && seq(info.name, "More options"));
    GT_ASSERT((info.states & GATES_ACCESS_EXPANDABLE) && !(info.states & GATES_ACCESS_EXPANDED));
    GT_ASSERT(seq(info.access_key, "Alt+M") && (info.actions & GATES_ACCESS_EXPAND));
    gates_access_ref_t f = gates_access_focus_ref(t);
    GT_ASSERT(gates_node_eq(f.node, fold) && f.item == 0);   /* the title's focus is the group's */
    GT_ASSERT_OK(gates_access_expand(t, fold, 0, true));
    GT_ASSERT(gates_group_expanded(t, fold));
    rec.n = 0;
    dispatch(t);
    GT_ASSERT(rec.n == 1 && rec.checked[0]);
    GT_ASSERT(gates_access_expand(t, plain, 0, true) == PROVEN_ERR_INVALID_ARG);
    layout(t);
    gates_access_issue_t issues[8];
    GT_ASSERT(gates_access_audit(t, theme, issues, 8) == 0);
    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_bubbling();
    test_defer();
    test_range_text();
    test_spin();
    test_slider();
    test_grid();
    test_grid_short();
    test_wrap();
    test_group();
    return gt_report("test_inputs");
}
