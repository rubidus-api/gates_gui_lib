/* T022: text editing contract - selection API, clipboard, read-only, maximum
 * length with a pending offer, password, bounded undo/redo
 * (docs/tests/cases/T022-text-editing.md, plan-0008, RFC-0003 4.2 and 6.1). */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/clipboard.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <string.h>

#define HAN "\xED\x95\x9C" /* U+D55C, 3 bytes, 2 cells */

static const gates_text_backend_t *be;
static const gates_theme_t *theme;

/* -- helpers ---------------------------------------------------------------- */

static gates_str_t lit(const char *s) {
    return (gates_str_t){ .ptr = (const gates_u8 *)s, .size = strlen(s) };
}

static bool str_is(gates_str_t s, const char *expect) {
    gates_usize_t n = strlen(expect);
    return s.size == n && (n == 0 || memcmp(s.ptr, expect, n) == 0);
}

typedef struct fake_clip_t {
    char data[256];
    gates_usize_t len;
    bool has;
    gates_err_t fail; /* returned by get/set when not OK */
    int sets;
} fake_clip_t;

static gates_err_t clip_get(void *ctx, gates_allocator_t alloc, gates_u8 **out,
                            gates_usize_t *out_len) {
    fake_clip_t *c = ctx;
    *out = nullptr;
    *out_len = 0;
    if (c->fail != GATES_OK) {
        return c->fail;
    }
    if (!c->has || c->len == 0) {
        return GATES_OK;
    }
    proven_result_mem_mut_t r = alloc.alloc_fn(alloc.ctx, c->len, 1);
    if (!proven_is_ok(r.err)) {
        return r.err;
    }
    memcpy(r.value.ptr, c->data, c->len);
    *out = (gates_u8 *)r.value.ptr;
    *out_len = c->len;
    return GATES_OK;
}

static gates_err_t clip_set(void *ctx, gates_str_t text) {
    fake_clip_t *c = ctx;
    if (c->fail != GATES_OK) {
        return c->fail;
    }
    c->sets++;
    c->len = text.size < sizeof c->data ? text.size : sizeof c->data;
    memcpy(c->data, text.ptr, c->len);
    c->has = true;
    return GATES_OK;
}

static void clip_put(fake_clip_t *c, const char *bytes, gates_usize_t n) {
    memcpy(c->data, bytes, n);
    c->len = n;
    c->has = true;
}

static bool clip_is(const fake_clip_t *c, const char *s) {
    return c->has && c->len == strlen(s) && memcmp(c->data, s, c->len) == 0;
}

typedef struct rec_t {
    gates_event_kind_t kind[32];
    gates_event_origin_t origin[32];
    char text[32][64];
    gates_usize_t len[32];
    gates_u32 limit[32];
    gates_u32 fit[32];
    int n;
} rec_t;

static void record(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    rec_t *r = user;
    if (r->n >= 32) return;
    int i = r->n++;
    r->kind[i] = ev->kind;
    r->origin[i] = ev->origin;
    r->len[i] = ev->text.size < 64 ? ev->text.size : 64;
    if (r->len[i] > 0) memcpy(r->text[i], ev->text.ptr, r->len[i]);
    r->limit[i] = ev->limit;
    r->fit[i] = ev->fit_bytes;
}

static bool rec_text_is(const rec_t *r, int i, const char *s) {
    return r->len[i] == strlen(s) && memcmp(r->text[i], s, r->len[i]) == 0;
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

typedef struct ui_t {
    gates_tree_t *t;
    gates_node_t tb;
    fake_clip_t clip;
    rec_t rec;
} ui_t;

/* A focused textbox holding `text`, caret at the end, with a fake clipboard
 * and an event recorder. */
static void make_ui_alloc(ui_t *u, gates_allocator_t alloc, const char *text) {
    memset(u, 0, sizeof *u);
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = alloc }, &u->t));
    gates_node_t root = gates_tree_root(u->t);
    GT_ASSERT_OK(gates_layout_set(u->t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_textbox_create(u->t, root, lit(text), 20, &u->tb));
    GT_ASSERT_OK(gates_layout_run(u->t, (gates_size_t){ 400, 100 }, be));
    gates_clipboard_t cb = { .ctx = &u->clip, .get_text = clip_get, .set_text = clip_set };
    gates_tree_set_clipboard(u->t, &cb);
    GT_ASSERT_OK(gates_widget_set_handler(u->t, u->tb, record, &u->rec));
    gates_tree_set_focus(u->t, u->tb);
}

static void make_ui(ui_t *u, const char *text) {
    make_ui_alloc(u, (gates_allocator_t){0}, text);
}

static bool tb_is(const ui_t *u, const char *s) {
    return str_is(gates_textbox_text(u->t, u->tb), s);
}

static bool key(gates_tree_t *t, gates_key_t k, bool ctrl, bool shift) {
    gates_key_event_t e = { .key = k, .down = true, .ctrl = ctrl, .shift = shift };
    return gates_input_key(t, &e);
}

static void type(gates_tree_t *t, const char *s) {
    for (; *s; s++) {
        GT_ASSERT(gates_input_char(t, (gates_u8)*s) == GATES_INPUT_CONSUMED);
    }
}

static void sel(ui_t *u, gates_u32 a, gates_u32 c) {
    GT_ASSERT_OK(gates_textbox_set_selection(u->t, u->tb, a, c));
}

static void flush(ui_t *u) {
    (void)gates_tree_dispatch_events(u->t, 0);
}

/* -- tests ---------------------------------------------------------------- */

static void test_selection_api(void) {
    ui_t u;
    make_ui(&u, "a" HAN "b"); /* bytes: a=0, HAN=1..3, b=4, len 5 */
    gates_u32 a = 99, c = 99;
    GT_ASSERT_OK(gates_textbox_selection(u.t, u.tb, &a, &c));
    GT_ASSERT(a == 5 && c == 5);
    sel(&u, 4, 1);
    GT_ASSERT_OK(gates_textbox_selection(u.t, u.tb, &a, &c));
    GT_ASSERT(a == 4 && c == 1);
    /* Offsets inside the Hangul syllable or past the end are rejected, not clamped. */
    GT_ASSERT(gates_textbox_set_selection(u.t, u.tb, 2, 2) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_textbox_set_selection(u.t, u.tb, 0, 6) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_textbox_selection(u.t, u.tb, &a, &c));
    GT_ASSERT(a == 4 && c == 1); /* unchanged */
    GT_ASSERT(gates_textbox_set_selection(u.t, gates_tree_root(u.t), 0, 0) ==
              PROVEN_ERR_INVALID_ARG);

    GT_ASSERT(gates_input_preedit(u.t, lit("g"), 1) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_textbox_set_selection(u.t, u.tb, 0, 0) == PROVEN_ERR_BUSY);
    GT_ASSERT(gates_input_preedit_cancel(u.t) == GATES_INPUT_CONSUMED);

    /* copy_text: size query, too small, exact. */
    gates_usize_t need = 0;
    char buf[8];
    GT_ASSERT_OK(gates_textbox_copy_text(u.t, u.tb, nullptr, 0, &need));
    GT_ASSERT(need == 5);
    GT_ASSERT(gates_textbox_copy_text(u.t, u.tb, (gates_u8 *)buf, 4, &need) ==
              PROVEN_ERR_OVERFLOW);
    GT_ASSERT_OK(gates_textbox_copy_text(u.t, u.tb, (gates_u8 *)buf, sizeof buf, &need));
    GT_ASSERT(need == 5 && memcmp(buf, "a" HAN "b", 5) == 0);
    gates_tree_destroy(u.t);
}

static void test_replace_selection(void) {
    ui_t u;
    make_ui(&u, "hello");
    sel(&u, 1, 4);
    GT_ASSERT_OK(gates_textbox_replace_selection(u.t, u.tb, lit("EY")));
    GT_ASSERT(tb_is(&u, "hEYo"));
    GT_ASSERT(gates_tree_pending_events(u.t) == 0); /* programmatic: silent */
    GT_ASSERT(gates_textbox_can_undo(u.t, u.tb));
    GT_ASSERT(key(u.t, GATES_KEY_Z, true, false));
    GT_ASSERT(tb_is(&u, "hello"));

    GT_ASSERT(gates_input_preedit(u.t, lit("g"), 1) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_textbox_replace_selection(u.t, u.tb, lit("x")) == PROVEN_ERR_BUSY);
    GT_ASSERT(gates_input_preedit_cancel(u.t) == GATES_INPUT_CONSUMED);

    GT_ASSERT_OK(gates_textbox_set_max_bytes(u.t, u.tb, 6));
    sel(&u, 5, 5);
    GT_ASSERT(gates_textbox_replace_selection(u.t, u.tb, lit("xy")) == PROVEN_ERR_OUT_OF_BOUNDS);
    GT_ASSERT(tb_is(&u, "hello"));
    gates_tree_destroy(u.t);
}

static void test_clipboard_keys(void) {
    ui_t u;
    make_ui(&u, "abcdef");
    sel(&u, 1, 3);
    GT_ASSERT(key(u.t, GATES_KEY_C, true, false));
    GT_ASSERT(clip_is(&u.clip, "bc"));
    GT_ASSERT(tb_is(&u, "abcdef"));
    GT_ASSERT(gates_tree_pending_events(u.t) == 0);

    GT_ASSERT(key(u.t, GATES_KEY_X, true, false));
    GT_ASSERT(tb_is(&u, "adef"));
    flush(&u);
    GT_ASSERT(u.rec.n == 1 && rec_text_is(&u.rec, 0, "adef"));

    /* Paste: line breaks and tabs become one space, other controls vanish. */
    clip_put(&u.clip, "1\r\n2\r3\n4\t5\x01" "6", 12);
    GT_ASSERT(key(u.t, GATES_KEY_V, true, false));
    GT_ASSERT(tb_is(&u, "a1 2 3 4 56def"));
    flush(&u);
    GT_ASSERT(u.rec.n == 2); /* one paste, one event */

    /* Invalid UTF-8 from a provider becomes U+FFFD, never stored raw. */
    sel(&u, 0, 14);
    clip_put(&u.clip, "x\xC3y", 3);
    GT_ASSERT(key(u.t, GATES_KEY_V, true, false));
    GT_ASSERT(tb_is(&u, "x\xEF\xBF\xBDy"));

    /* A busy clipboard changes nothing and is reported. */
    u.clip.fail = PROVEN_ERR_BUSY;
    GT_ASSERT(key(u.t, GATES_KEY_V, true, false));
    GT_ASSERT(tb_is(&u, "x\xEF\xBF\xBDy"));
    GT_ASSERT(gates_input_take_error(u.t) == PROVEN_ERR_BUSY);
    u.clip.fail = GATES_OK;

    /* Ctrl+Alt (AltGr) is not a shortcut. */
    clip_put(&u.clip, "Q", 1);
    gates_key_event_t altgr = { .key = GATES_KEY_V, .down = true, .ctrl = true, .alt = true };
    GT_ASSERT(!gates_input_key(u.t, &altgr));
    GT_ASSERT(tb_is(&u, "x\xEF\xBF\xBDy"));

    /* Without a provider: nothing happens. */
    gates_tree_set_clipboard(u.t, nullptr);
    GT_ASSERT(key(u.t, GATES_KEY_V, true, false));
    GT_ASSERT(tb_is(&u, "x\xEF\xBF\xBDy"));
    gates_tree_destroy(u.t);
}

static int count_caret_rects(const gates_draw_list_t *dl) {
    int n = 0;
    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(dl, i);
        if (c->kind == GATES_DRAW_RECT && c->rect.w == 1) n++;
    }
    return n;
}

static bool has_text(const gates_draw_list_t *dl, const char *s) {
    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(dl, i);
        if (c->kind == GATES_DRAW_TEXT &&
            str_is((gates_str_t){ .ptr = dl->text + c->text_offset, .size = c->text_len }, s)) {
            return true;
        }
    }
    return false;
}

static void test_read_only(void) {
    ui_t u;
    make_ui(&u, "fixed");
    GT_ASSERT_OK(gates_textbox_set_read_only(u.t, u.tb, true));
    GT_ASSERT(gates_textbox_read_only(u.t, u.tb));
    GT_ASSERT(gates_input_char(u.t, 'x') == GATES_INPUT_IGNORED);
    GT_ASSERT(!key(u.t, GATES_KEY_BACKSPACE, false, false));
    GT_ASSERT(!key(u.t, GATES_KEY_DELETE, false, false));
    GT_ASSERT(gates_input_preedit(u.t, lit("g"), 1) == GATES_INPUT_IGNORED);
    GT_ASSERT(gates_input_commit(u.t, lit("g")) == GATES_INPUT_IGNORED);
    clip_put(&u.clip, "Z", 1);
    GT_ASSERT(!key(u.t, GATES_KEY_V, true, false));
    /* Selection and copy still work; cut does not delete. */
    GT_ASSERT(key(u.t, GATES_KEY_HOME, false, false));
    GT_ASSERT(key(u.t, GATES_KEY_RIGHT, false, true));
    GT_ASSERT(key(u.t, GATES_KEY_RIGHT, false, true));
    GT_ASSERT(key(u.t, GATES_KEY_C, true, false));
    GT_ASSERT(clip_is(&u.clip, "fi"));
    GT_ASSERT(!key(u.t, GATES_KEY_X, true, false));
    GT_ASSERT(!key(u.t, GATES_KEY_Z, true, false));
    GT_ASSERT(tb_is(&u, "fixed"));
    GT_ASSERT(gates_tree_pending_events(u.t) == 0);

    /* Painted without a caret. */
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(u.t, &dl, theme, be));
    GT_ASSERT(count_caret_rects(&dl) == 0);
    GT_ASSERT_OK(gates_textbox_set_read_only(u.t, u.tb, false));
    /* History survives read-only but cannot be used while it lasts. */
    GT_ASSERT(key(u.t, GATES_KEY_END, false, false));
    type(u.t, "!");
    GT_ASSERT(gates_textbox_can_undo(u.t, u.tb));
    GT_ASSERT_OK(gates_textbox_set_read_only(u.t, u.tb, true));
    GT_ASSERT(!gates_textbox_can_undo(u.t, u.tb));
    GT_ASSERT(!key(u.t, GATES_KEY_Z, true, false));
    GT_ASSERT(tb_is(&u, "fixed!"));
    GT_ASSERT_OK(gates_textbox_set_read_only(u.t, u.tb, false));
    GT_ASSERT(gates_textbox_can_undo(u.t, u.tb));
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(u.t, &dl, theme, be));
    GT_ASSERT(count_caret_rects(&dl) == 1);
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(u.t);
}

static void test_password(void) {
    ui_t u;
    make_ui(&u, "a" HAN "b");
    GT_ASSERT_OK(gates_textbox_set_password(u.t, u.tb, true));
    GT_ASSERT(gates_textbox_password(u.t, u.tb));

    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(u.t, &dl, theme, be));
    GT_ASSERT(has_text(&dl, "***"));          /* one star per codepoint */
    GT_ASSERT(!has_text(&dl, "a" HAN "b"));

    /* The caret after three codepoints sits three cells in (not four). */
    gates_rect_t cr;
    GT_ASSERT(gates_input_caret_rect(u.t, &cr));
    gates_rect_t inner_start = cr;
    sel(&u, 0, 0);
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(u.t, &dl, theme, be));
    GT_ASSERT(gates_input_caret_rect(u.t, &cr));
    gates_text_metrics_t m = be->metrics(be->ctx, 0);
    GT_ASSERT(inner_start.x - cr.x == 3 * m.advance);

    sel(&u, 0, 5);
    GT_ASSERT(!key(u.t, GATES_KEY_C, true, false));
    GT_ASSERT(!key(u.t, GATES_KEY_X, true, false));
    GT_ASSERT(u.clip.sets == 0);
    GT_ASSERT(tb_is(&u, "a" HAN "b"));

    sel(&u, 5, 5);
    type(u.t, "z");
    flush(&u);
    GT_ASSERT(u.rec.n == 1 && u.rec.kind[0] == GATES_EVENT_TEXT_CHANGED && u.rec.len[0] == 0);
    GT_ASSERT(!gates_textbox_can_undo(u.t, u.tb));
    GT_ASSERT(gates_input_preedit(u.t, lit("g"), 1) == GATES_INPUT_IGNORED);
    char buf[16];
    gates_usize_t need = 0;
    GT_ASSERT_OK(gates_textbox_copy_text(u.t, u.tb, (gates_u8 *)buf, sizeof buf, &need));
    GT_ASSERT(need == 6 && memcmp(buf, "a" HAN "bz", 6) == 0);
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(u.t);
}

static void test_max_length_offer(void) {
    ui_t u;
    make_ui(&u, "abc");
    GT_ASSERT(gates_textbox_set_max_bytes(u.t, u.tb, 2) == PROVEN_ERR_OUT_OF_BOUNDS);
    GT_ASSERT_OK(gates_textbox_set_max_bytes(u.t, u.tb, 5));
    GT_ASSERT(gates_textbox_max_bytes(u.t, u.tb) == 5);
    GT_ASSERT(gates_textbox_set_text(u.t, u.tb, lit("abcdef")) == PROVEN_ERR_OUT_OF_BOUNDS);

    type(u.t, "d");                                   /* 4 bytes */
    /* Paste "e" + HAN (4 bytes) needs 8: refused whole, the offer keeps it. */
    clip_put(&u.clip, "e" HAN, 4);
    GT_ASSERT(key(u.t, GATES_KEY_V, true, false));
    GT_ASSERT(tb_is(&u, "abcd"));
    GT_ASSERT(gates_input_take_error(u.t) == PROVEN_ERR_OUT_OF_BOUNDS);
    flush(&u);
    GT_ASSERT(u.rec.n == 2);
    GT_ASSERT(u.rec.kind[1] == GATES_EVENT_LIMIT_EXCEEDED);
    GT_ASSERT(u.rec.limit[1] == 5 && u.rec.fit[1] == 1);   /* only "e" fits */
    GT_ASSERT(rec_text_is(&u.rec, 1, "e" HAN));

    /* The person said "insert what fits". */
    GT_ASSERT_OK(gates_textbox_accept_fit(u.t, u.tb));
    GT_ASSERT(tb_is(&u, "abcde"));
    flush(&u);
    GT_ASSERT(u.rec.kind[2] == GATES_EVENT_TEXT_CHANGED && u.rec.origin[2] == GATES_ORIGIN_USER);
    GT_ASSERT(gates_textbox_accept_fit(u.t, u.tb) == PROVEN_ERR_INVALID_STATE); /* used */
    GT_ASSERT(key(u.t, GATES_KEY_Z, true, false));  /* the accept is one undo unit */
    GT_ASSERT(tb_is(&u, "abcd"));

    /* Typing past the limit: FAILED and an offer; a later edit makes it stale. */
    type(u.t, "e");
    GT_ASSERT(gates_input_char(u.t, 'f') == GATES_INPUT_FAILED);
    GT_ASSERT(gates_input_take_error(u.t) == PROVEN_ERR_OUT_OF_BOUNDS);
    GT_ASSERT(key(u.t, GATES_KEY_BACKSPACE, false, false));
    GT_ASSERT(gates_textbox_accept_fit(u.t, u.tb) == PROVEN_ERR_INVALID_STATE);

    /* IME commit past the limit: the composition ends, nothing is inserted. */
    GT_ASSERT(gates_input_preedit(u.t, lit(HAN), 3) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_input_commit(u.t, lit(HAN)) == GATES_INPUT_FAILED);
    GT_ASSERT(!gates_input_composing(u.t));
    GT_ASSERT(tb_is(&u, "abcd"));
    gates_textbox_discard_rejected(u.t, u.tb);
    GT_ASSERT(gates_textbox_accept_fit(u.t, u.tb) == PROVEN_ERR_INVALID_STATE);

    /* Password box: the event carries sizes but no text. */
    GT_ASSERT_OK(gates_textbox_set_password(u.t, u.tb, true));
    u.rec.n = 0;
    clip_put(&u.clip, "xyz", 3);
    GT_ASSERT(key(u.t, GATES_KEY_V, true, false)); /* paste works in password boxes */
    GT_ASSERT(tb_is(&u, "abcd"));
    flush(&u);
    bool saw = false;
    for (int i = 0; i < u.rec.n; i++) {
        if (u.rec.kind[i] == GATES_EVENT_LIMIT_EXCEEDED) {
            saw = true;
            GT_ASSERT(u.rec.len[i] == 0 && u.rec.fit[i] == 1);
        }
    }
    GT_ASSERT(saw);
    gates_tree_destroy(u.t);
}

static void test_undo_units(void) {
    ui_t u;
    make_ui(&u, "abc");
    GT_ASSERT(!gates_textbox_can_undo(u.t, u.tb));
    type(u.t, "xyz");                              /* one typing run */
    GT_ASSERT(key(u.t, GATES_KEY_Z, true, false));
    GT_ASSERT(tb_is(&u, "abc"));
    gates_u32 a, c;
    GT_ASSERT_OK(gates_textbox_selection(u.t, u.tb, &a, &c));
    GT_ASSERT(c == 3);
    GT_ASSERT(gates_textbox_can_redo(u.t, u.tb));
    GT_ASSERT(key(u.t, GATES_KEY_Y, true, false));
    GT_ASSERT(tb_is(&u, "abcxyz"));
    GT_ASSERT(key(u.t, GATES_KEY_Z, true, false));
    GT_ASSERT(key(u.t, GATES_KEY_Z, true, true));  /* Ctrl+Shift+Z redoes too */
    GT_ASSERT(tb_is(&u, "abcxyz"));

    /* A caret move ends the run. */
    type(u.t, "1");
    GT_ASSERT(key(u.t, GATES_KEY_LEFT, false, false));
    type(u.t, "2");
    GT_ASSERT(tb_is(&u, "abcxyz21"));
    GT_ASSERT(key(u.t, GATES_KEY_Z, true, false));
    GT_ASSERT(tb_is(&u, "abcxyz1"));
    GT_ASSERT(key(u.t, GATES_KEY_Z, true, false));
    GT_ASSERT(tb_is(&u, "abcxyz"));

    /* A new edit drops the redo branch. */
    type(u.t, "q");
    GT_ASSERT(!gates_textbox_can_redo(u.t, u.tb));

    /* Backspace runs merge; each undo is a user TEXT_CHANGED. */
    u.rec.n = 0;
    GT_ASSERT(key(u.t, GATES_KEY_BACKSPACE, false, false));
    GT_ASSERT(key(u.t, GATES_KEY_BACKSPACE, false, false));
    GT_ASSERT(tb_is(&u, "abcxy"));
    GT_ASSERT(key(u.t, GATES_KEY_Z, true, false));
    GT_ASSERT(tb_is(&u, "abcxyzq"));
    flush(&u);
    GT_ASSERT(u.rec.n >= 1 && u.rec.kind[u.rec.n - 1] == GATES_EVENT_TEXT_CHANGED);
    GT_ASSERT(rec_text_is(&u.rec, u.rec.n - 1, "abcxyzq"));
    GT_ASSERT(u.rec.origin[u.rec.n - 1] == GATES_ORIGIN_USER);

    /* A programmatic set clears the history. */
    GT_ASSERT_OK(gates_textbox_set_text(u.t, u.tb, lit("new")));
    GT_ASSERT(!gates_textbox_can_undo(u.t, u.tb) && !gates_textbox_can_redo(u.t, u.tb));
    GT_ASSERT(!key(u.t, GATES_KEY_Z, true, false) || tb_is(&u, "new"));
    GT_ASSERT(tb_is(&u, "new"));
    gates_tree_destroy(u.t);
}

static void test_undo_limits(void) {
    ui_t u;
    make_ui(&u, "");
    GT_ASSERT_OK(gates_textbox_set_undo_limits(u.t, u.tb, 2, 1024));
    for (int i = 0; i < 4; i++) {
        type(u.t, "a");
        GT_ASSERT(key(u.t, GATES_KEY_HOME, false, false));  /* break the run */
        GT_ASSERT(key(u.t, GATES_KEY_END, false, false));
    }
    GT_ASSERT(tb_is(&u, "aaaa"));
    GT_ASSERT(key(u.t, GATES_KEY_Z, true, false));
    GT_ASSERT(key(u.t, GATES_KEY_Z, true, false));
    GT_ASSERT(tb_is(&u, "aa"));
    GT_ASSERT(!gates_textbox_can_undo(u.t, u.tb));   /* only two entries kept */

    /* Byte bound: an entry larger than the whole budget is not kept. */
    GT_ASSERT_OK(gates_textbox_set_undo_limits(u.t, u.tb, 8, 4));
    GT_ASSERT(!gates_textbox_can_undo(u.t, u.tb));   /* shrinking dropped the history */
    GT_ASSERT_OK(gates_textbox_replace_selection(u.t, u.tb, lit("0123456789")));
    GT_ASSERT(tb_is(&u, "aa0123456789"));
    GT_ASSERT(!gates_textbox_can_undo(u.t, u.tb));
    gates_tree_destroy(u.t);
}

/* Owner rule (2026-09-25): when undo memory runs out, the oldest history goes
 * and the edit still happens. */
static void test_undo_memory_pressure(void) {
    fail_alloc_t fa = { .inner = proven_heap_allocator() };
    gates_allocator_t alloc = { .ctx = &fa, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc,
                                .free_fn = fa_free };
    ui_t u;
    make_ui_alloc(&u, alloc, "abc");
    type(u.t, "d");
    GT_ASSERT(key(u.t, GATES_KEY_LEFT, false, false));
    GT_ASSERT(key(u.t, GATES_KEY_RIGHT, false, false));
    flush(&u);                                  /* queue storage exists now */
    GT_ASSERT(gates_textbox_can_undo(u.t, u.tb));

    fa.fail = true;
    GT_ASSERT(gates_input_char(u.t, 'e') == GATES_INPUT_CONSUMED);
    GT_ASSERT(tb_is(&u, "abcde"));
    GT_ASSERT(!gates_textbox_can_undo(u.t, u.tb)); /* history sacrificed, not the edit */
    fa.fail = false;
    flush(&u);
    gates_tree_destroy(u.t);
}

static void test_edit_commit_bridge(void) {
    ui_t u;
    make_ui(&u, "abc");
    type(u.t, "d");
    gates_u32 rev = gates_widget_revision(u.t, u.tb);
    gates_text_edit_t *ed = gates_textbox_edit(u.t, u.tb);
    GT_ASSERT_OK(gates_text_edit_set_text(ed, lit("raw")));
    gates_textbox_edit_commit(u.t, u.tb);
    GT_ASSERT(gates_widget_revision(u.t, u.tb) == rev + 1);
    GT_ASSERT(!gates_textbox_can_undo(u.t, u.tb));
    GT_ASSERT(tb_is(&u, "raw"));
    gates_tree_destroy(u.t);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_selection_api();
    test_replace_selection();
    test_clipboard_keys();
    test_read_only();
    test_password();
    test_max_length_offer();
    test_undo_units();
    test_undo_limits();
    test_undo_memory_pressure();
    test_edit_commit_bridge();
    return gt_report("test_text_editing");
}
