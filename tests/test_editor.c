/* T055: the multi-line editor node (plan-0022 stage 2) - typing, lines,
 * caret movement, deletion runs, selection, clipboard, Tab, read-only, the
 * byte limit, undo/redo and the modified mark, program edits, painting only
 * the lines shown, tabs, pointer and wheel, scrolling, events, accessibility
 * and allocation failure. Builtin text: 8 px per cell, 16 px lines. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/editor.h>
#include <gates/access.h>
#include <gates/clipboard.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const gates_text_backend_t *be;
static const gates_theme_t *theme;

typedef struct rec_t {
    int text, sel;
    gates_u64 last_item;
    gates_usize_t last_text_size;
} rec_t;

static void on_ev(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    rec_t *r = user;
    if (ev->kind == GATES_EVENT_TEXT_CHANGED) { r->text++; r->last_text_size = ev->text.size; }
    if (ev->kind == GATES_EVENT_SELECTION_CHANGED) { r->sel++; r->last_item = ev->item; }
}

/* A fake clipboard. */
static char clip[256];
static gates_err_t clip_get(void *ctx, gates_allocator_t a, gates_u8 **out, gates_usize_t *n) {
    (void)ctx;
    *n = strlen(clip);
    *out = nullptr;
    if (*n == 0) return GATES_OK;
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, *n, 1);
    if (!proven_is_ok(r.err)) return r.err;
    memcpy(r.value.ptr, clip, *n);
    *out = (gates_u8 *)r.value.ptr;
    return GATES_OK;
}
static gates_err_t clip_set(void *ctx, gates_str_t t) {
    (void)ctx;
    snprintf(clip, sizeof clip, "%.*s", (int)t.size, (const char *)t.ptr);
    return GATES_OK;
}

typedef struct app_t {
    gates_tree_t *t;
    gates_node_t before, ed, after;
    rec_t rec;
} app_t;

static void layout(gates_tree_t *t) {
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 400, 300 }, be));
}

static void make(app_t *a, const gates_editor_desc_t *d) {
    memset(a, 0, sizeof *a);
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &a->t));
    gates_node_t root = gates_tree_root(a->t);
    GT_ASSERT_OK(gates_layout_set(a->t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_button_create(a->t, root, GATES_STR("before"), nullptr, nullptr, &a->before));
    GT_ASSERT_OK(gates_editor_create(a->t, root, d, &a->ed));
    GT_ASSERT_OK(gates_layout_set_child_grow(a->t, a->ed, 1));
    GT_ASSERT_OK(gates_button_create(a->t, root, GATES_STR("after"), nullptr, nullptr, &a->after));
    GT_ASSERT_OK(gates_widget_set_handler(a->t, a->ed, on_ev, &a->rec));
    gates_clipboard_t cb = { .get_text = clip_get, .set_text = clip_set };
    gates_tree_set_clipboard(a->t, &cb);
    layout(a->t);
    gates_tree_set_focus(a->t, a->ed);
}

static void done(app_t *a) { gates_tree_destroy(a->t); }

static bool key_mods(gates_tree_t *t, gates_key_t k, bool ctrl, bool shift) {
    gates_key_event_t e = { .key = k, .down = true, .ctrl = ctrl, .shift = shift };
    bool r = gates_input_key(t, &e);
    e.down = false;
    (void)gates_input_key(t, &e);
    return r;
}
static bool key(gates_tree_t *t, gates_key_t k) { return key_mods(t, k, false, false); }

static void type(gates_tree_t *t, const char *s) {
    for (gates_str_t u = { (const gates_u8 *)s, strlen(s) }; u.size > 0;) {
        gates_u32 cp;
        gates_u32 n = gates_text_decode(u, 0, &cp);
        if (cp == '\n') (void)key(t, GATES_KEY_ENTER);
        else (void)gates_input_char(t, cp);
        u.ptr += n;
        u.size -= n;
    }
}

static bool text_is(app_t *a, const char *z) {
    char buf[4096];
    const gates_text_buffer_t *b = gates_editor_buffer(a->t, a->ed);
    gates_u32 n = gates_text_buffer_copy(b, 0, gates_text_buffer_length(b), (gates_u8 *)buf, sizeof buf);
    bool ok = n == strlen(z) && memcmp(buf, z, n) == 0;
    if (!ok) printf("  text is '%.*s', want '%s'\n", (int)n, buf, z);
    return ok;
}

static gates_u32 caret(app_t *a) {
    gates_u32 c = 0;
    gates_editor_selection(a->t, a->ed, nullptr, &c);
    return c;
}

static gates_u32 anchor(app_t *a) {
    gates_u32 x = 0;
    gates_editor_selection(a->t, a->ed, &x, nullptr);
    return x;
}

/* The text area's top-left (border 1 + inset 3). */
static gates_point_t origin(app_t *a) {
    gates_rect_t r = gates_node_layout_rect(a->t, a->ed);
    return (gates_point_t){ r.x + 4, r.y + 4 };
}

static void press(app_t *a, gates_point_t p, gates_u32 clicks) {
    gates_pointer_event_t e = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT, .pos = p, .clicks = clicks };
    (void)gates_input_pointer(a->t, &e);
}
static void move(app_t *a, gates_point_t p) {
    gates_pointer_event_t e = { .action = GATES_POINTER_MOVE, .pos = p };
    (void)gates_input_pointer(a->t, &e);
}
static void release(app_t *a, gates_point_t p) {
    gates_pointer_event_t e = { .action = GATES_POINTER_UP, .button = GATES_BUTTON_LEFT, .pos = p };
    (void)gates_input_pointer(a->t, &e);
}

/* -- creation, program edits ---------------------------------------------------------------- */

static void test_create_and_program(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t ed;
    GT_ASSERT(gates_editor_create(t, gates_tree_root(t), nullptr, &ed) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_create(nullptr, gates_tree_root(t), &(gates_editor_desc_t){0}, &ed) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_create(t, (gates_node_t){ .index = 999, .generation = 3 }, &(gates_editor_desc_t){0}, &ed) ==
              PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_editor_create(t, gates_tree_root(t), &(gates_editor_desc_t){0}, &ed));
    GT_ASSERT(gates_node_kind(t, ed) == GATES_NODE_EDITOR);
    GT_ASSERT(gates_editor_length(t, ed) == 0 && gates_text_buffer_line_count(gates_editor_buffer(t, ed)) == 1);
    /* Not UTF-8, bad ranges: refused. */
    GT_ASSERT(gates_editor_set_text(t, ed, (gates_str_t){ (const gates_u8 *)"a\xC0\xAF", 3 }) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_set_text(t, ed, (gates_str_t){ (const gates_u8 *)"\xED\xA0\x80", 3 }) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_set_text(t, ed, (gates_str_t){ (const gates_u8 *)"\xE0\x80", 2 }) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_set_text(t, ed, (gates_str_t){ (const gates_u8 *)"\xF4\x90\x80\x80", 4 }) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_editor_set_text(t, ed, GATES_STR("\xEA\xB0\x80 ok\nline two")));
    GT_ASSERT(!gates_editor_modified(t, ed) && !gates_editor_can_undo(t, ed));
    GT_ASSERT(gates_editor_replace(t, ed, 1, 2, GATES_STR("x"), true) == PROVEN_ERR_INVALID_ARG); /* inside a code point */
    GT_ASSERT(gates_editor_replace(t, ed, 4, 2, GATES_STR("x"), true) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_replace(t, ed, 0, 99, GATES_STR("x"), true) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_replace(t, ed, 0, 0, (gates_str_t){ (const gates_u8 *)"\xFF", 1 }, true) == PROVEN_ERR_INVALID_ARG);
    /* The selection snaps to code points. */
    GT_ASSERT_OK(gates_editor_set_selection(t, ed, 1, 5));
    gates_u32 an = 9, ca = 9;
    gates_editor_selection(t, ed, &an, &ca);
    GT_ASSERT(an == 0 && ca == 5);
    GT_ASSERT_OK(gates_editor_set_selection(t, ed, 99, 99));
    gates_editor_selection(t, ed, &an, &ca);
    GT_ASSERT(an == gates_editor_length(t, ed) && ca == an);
    /* An undoable program edit: the caret keeps its place in the text. */
    GT_ASSERT_OK(gates_editor_set_selection(t, ed, 7, 7)); /* before "\n" */
    GT_ASSERT_OK(gates_editor_replace(t, ed, 0, 3, GATES_STR("Hi"), true));
    gates_editor_selection(t, ed, &an, &ca);
    GT_ASSERT(ca == 6 && an == 6);
    GT_ASSERT(gates_editor_can_undo(t, ed) && gates_editor_modified(t, ed));
    GT_ASSERT_OK(gates_editor_undo(t, ed));
    GT_ASSERT(!gates_editor_modified(t, ed));
    GT_ASSERT(gates_editor_undo(t, ed) == PROVEN_ERR_INVALID_STATE);
    GT_ASSERT_OK(gates_editor_redo(t, ed));
    GT_ASSERT(gates_editor_redo(t, ed) == PROVEN_ERR_INVALID_STATE);
    /* A program edit that is not undoable clears the history. */
    GT_ASSERT_OK(gates_editor_replace(t, ed, 0, 0, GATES_STR(">"), false));
    GT_ASSERT(!gates_editor_can_undo(t, ed) && !gates_editor_can_redo(t, ed) && gates_editor_modified(t, ed));
    gates_editor_set_unmodified(t, ed);
    GT_ASSERT(!gates_editor_modified(t, ed));
    /* Read-only switch. */
    GT_ASSERT_OK(gates_editor_set_read_only(t, ed, true));
    GT_ASSERT(gates_editor_read_only(t, ed));
    /* Wrong nodes. */
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT(gates_editor_buffer(t, root) == nullptr && gates_editor_length(t, root) == 0);
    GT_ASSERT(gates_editor_set_text(t, root, GATES_STR("x")) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_undo(t, root) == PROVEN_ERR_INVALID_ARG && gates_editor_redo(t, root) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_set_selection(t, root, 0, 0) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_scroll_to(t, root, 0) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_set_read_only(t, root, true) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(!gates_editor_can_undo(t, root) && !gates_editor_modified(t, root) && !gates_editor_read_only(t, root));
    GT_ASSERT(gates_editor_first_line(t, root) == 0 && gates_editor_visible_lines(t, root) == 0);
    gates_editor_set_unmodified(t, root);
    gates_tree_destroy(t);
}

/* -- typing and history ------------------------------------------------------------------------ */

static void test_typing_and_history(void) {
    app_t a;
    make(&a, &(gates_editor_desc_t){0});
    type(a.t, "hello\nworld");
    GT_ASSERT(text_is(&a, "hello\nworld"));
    GT_ASSERT(caret(&a) == 11 && anchor(&a) == 11);
    GT_ASSERT(gates_text_buffer_line_count(gates_editor_buffer(a.t, a.ed)) == 2);
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(a.rec.text == 1 && a.rec.last_text_size == 0); /* coalesced; no text copy */
    GT_ASSERT(a.rec.sel == 1 && a.rec.last_item == 11);        /* the caret at delivery */
    GT_ASSERT(gates_editor_modified(a.t, a.ed));
    /* Typing runs merge; Enter is a step of its own. */
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false));
    GT_ASSERT(text_is(&a, "hello\n") && caret(&a) == 6);
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false));
    GT_ASSERT(text_is(&a, "hello"));
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false));
    GT_ASSERT(text_is(&a, "") && !gates_editor_modified(a.t, a.ed));
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false)); /* nothing left: still the editor's key */
    GT_ASSERT(key_mods(a.t, GATES_KEY_Y, true, false));
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, true)); /* Ctrl+Shift+Z redoes */
    GT_ASSERT(key_mods(a.t, GATES_KEY_Y, true, false));
    GT_ASSERT(text_is(&a, "hello\nworld") && caret(&a) == 11);
    /* A deletion run is one step; moving the caret ends a run. */
    GT_ASSERT(key(a.t, GATES_KEY_BACKSPACE));
    GT_ASSERT(key(a.t, GATES_KEY_BACKSPACE));
    GT_ASSERT(key(a.t, GATES_KEY_BACKSPACE));
    GT_ASSERT(text_is(&a, "hello\nwo"));
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false));
    GT_ASSERT(text_is(&a, "hello\nworld"));
    GT_ASSERT(key_mods(a.t, GATES_KEY_HOME, true, false));
    GT_ASSERT(key(a.t, GATES_KEY_DELETE));
    GT_ASSERT(key(a.t, GATES_KEY_DELETE));
    GT_ASSERT(text_is(&a, "llo\nworld"));
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false));
    GT_ASSERT(text_is(&a, "hello\nworld") && caret(&a) == 0);
    type(a.t, "a");
    GT_ASSERT(key(a.t, GATES_KEY_RIGHT));
    type(a.t, "b");
    GT_ASSERT(text_is(&a, "ahbello\nworld"));
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false));
    GT_ASSERT(text_is(&a, "ahello\nworld")); /* two steps: the move ended the run */
    /* A redo tail goes when a new edit comes. */
    type(a.t, "Q");
    GT_ASSERT(!gates_editor_can_redo(a.t, a.ed));
    /* A caret that moved away and back still ends the run. */
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("")));
    type(a.t, "ab");
    GT_ASSERT(key(a.t, GATES_KEY_LEFT) && key(a.t, GATES_KEY_RIGHT));
    type(a.t, "c");
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false) && text_is(&a, "ab"));
    /* Undo gives back the selection the edit replaced. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 2, 0));
    type(a.t, "X");
    GT_ASSERT(text_is(&a, "X"));
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false) && text_is(&a, "ab"));
    GT_ASSERT(anchor(&a) == 2 && caret(&a) == 0);
    /* A mark in a redo tail that goes is gone: never "unmodified" by accident. */
    type(a.t, "q");
    gates_editor_set_unmodified(a.t, a.ed);
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false) && gates_editor_modified(a.t, a.ed));
    type(a.t, "r");
    GT_ASSERT(gates_editor_modified(a.t, a.ed));
    /* The modified mark: set, then undo past it and back. */
    gates_editor_set_unmodified(a.t, a.ed);
    type(a.t, "R");
    GT_ASSERT(gates_editor_modified(a.t, a.ed));
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false));
    GT_ASSERT(!gates_editor_modified(a.t, a.ed)); /* typing after the mark was a new step */
    done(&a);
}

/* -- moving ------------------------------------------------------------------------------------ */

static void test_moving(void) {
    app_t a;
    make(&a, &(gates_editor_desc_t){0});
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("abcdef\r\nab\nabcdef  xyz_1 ,.\n    indented")));
    /* Right over "\r\n" is one step. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 6, 6));
    GT_ASSERT(key(a.t, GATES_KEY_RIGHT) && caret(&a) == 8);
    GT_ASSERT(key(a.t, GATES_KEY_LEFT) && caret(&a) == 6);
    /* Up/Down keep the column the caret came from. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 5, 5));
    GT_ASSERT(key(a.t, GATES_KEY_DOWN) && caret(&a) == 10);  /* "ab" is shorter: its end */
    GT_ASSERT(key(a.t, GATES_KEY_DOWN) && caret(&a) == 16);  /* back to column 5 */
    GT_ASSERT(key(a.t, GATES_KEY_UP) && caret(&a) == 10);
    GT_ASSERT(key(a.t, GATES_KEY_UP) && caret(&a) == 5);
    GT_ASSERT(key(a.t, GATES_KEY_UP) && caret(&a) == 0);     /* past the first line: its start */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 30, 30));
    GT_ASSERT(key(a.t, GATES_KEY_DOWN) && caret(&a) == gates_editor_length(a.t, a.ed)); /* last line: the end */
    /* Home: the first non-blank, then the line start; End; Ctrl+Home/End. */
    gates_u32 len = gates_editor_length(a.t, a.ed);
    GT_ASSERT(key(a.t, GATES_KEY_HOME) && caret(&a) == len - 8);
    GT_ASSERT(key(a.t, GATES_KEY_HOME) && caret(&a) == len - 12);
    GT_ASSERT(key(a.t, GATES_KEY_HOME) && caret(&a) == len - 8);
    GT_ASSERT(key(a.t, GATES_KEY_END) && caret(&a) == len);
    GT_ASSERT(key_mods(a.t, GATES_KEY_HOME, true, false) && caret(&a) == 0);
    GT_ASSERT(key_mods(a.t, GATES_KEY_END, true, false) && caret(&a) == len);
    /* Words: Ctrl+Right past the run then blanks; Ctrl+Left back. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 11, 11)); /* "abcdef  xyz_1 ,." */
    GT_ASSERT(key_mods(a.t, GATES_KEY_RIGHT, true, false) && caret(&a) == 19);
    GT_ASSERT(key_mods(a.t, GATES_KEY_RIGHT, true, false) && caret(&a) == 25);
    GT_ASSERT(key_mods(a.t, GATES_KEY_RIGHT, true, false) && caret(&a) == 27);
    GT_ASSERT(key_mods(a.t, GATES_KEY_RIGHT, true, false) && caret(&a) == 28); /* the line break */
    GT_ASSERT(key_mods(a.t, GATES_KEY_LEFT, true, false) && caret(&a) == 27);
    GT_ASSERT(key_mods(a.t, GATES_KEY_LEFT, true, false) && caret(&a) == 25);
    GT_ASSERT(key_mods(a.t, GATES_KEY_LEFT, true, false) && caret(&a) == 19);
    GT_ASSERT(key_mods(a.t, GATES_KEY_LEFT, true, false) && caret(&a) == 11);
    /* Shift extends; a plain arrow collapses a selection to its side. */
    GT_ASSERT(key_mods(a.t, GATES_KEY_RIGHT, false, true) && key_mods(a.t, GATES_KEY_RIGHT, false, true));
    GT_ASSERT(anchor(&a) == 11 && caret(&a) == 13);
    GT_ASSERT(key(a.t, GATES_KEY_LEFT) && caret(&a) == 11 && anchor(&a) == 11);
    GT_ASSERT(key_mods(a.t, GATES_KEY_DOWN, false, true) && anchor(&a) == 11);
    GT_ASSERT(key(a.t, GATES_KEY_RIGHT) && anchor(&a) == caret(&a) && caret(&a) > 11);
    /* Ctrl+A. */
    GT_ASSERT(key_mods(a.t, GATES_KEY_A, true, false) && anchor(&a) == 0 && caret(&a) == len);
    /* Keys the editor leaves: Alt, Ctrl+Up, Escape, plain letters (typed as characters). */
    gates_key_event_t alt = { .key = GATES_KEY_LEFT, .down = true, .alt = true };
    GT_ASSERT(!gates_input_key(a.t, &alt));
    GT_ASSERT(!key_mods(a.t, GATES_KEY_UP, true, false));
    done(&a);
}

/* -- deleting, selections, clipboard ------------------------------------------------------------ */

static void test_deleting_and_clipboard(void) {
    app_t a;
    make(&a, &(gates_editor_desc_t){0});
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("\xEA\xB0\x80\xEB\x82\x98\r\nx word two")));
    /* A Korean syllable at a time; "\r\n" as one. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 6, 6));
    GT_ASSERT(key(a.t, GATES_KEY_BACKSPACE));
    GT_ASSERT(text_is(&a, "\xEA\xB0\x80\r\nx word two") && caret(&a) == 3);
    GT_ASSERT(key(a.t, GATES_KEY_DELETE));
    GT_ASSERT(text_is(&a, "\xEA\xB0\x80x word two"));
    /* Ctrl+Backspace / Ctrl+Delete take a word. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 10, 10));
    GT_ASSERT(key_mods(a.t, GATES_KEY_BACKSPACE, true, false));
    GT_ASSERT(text_is(&a, "\xEA\xB0\x80x two"));
    GT_ASSERT(key_mods(a.t, GATES_KEY_DELETE, true, false));
    GT_ASSERT(text_is(&a, "\xEA\xB0\x80x "));
    /* At the edges nothing happens. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 0, 0));
    GT_ASSERT(key(a.t, GATES_KEY_BACKSPACE) && text_is(&a, "\xEA\xB0\x80x "));
    /* Typing replaces a selection. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 0, 4));
    type(a.t, "Z");
    GT_ASSERT(text_is(&a, "Z ") && caret(&a) == 1);
    /* Clipboard: copy, cut, paste with line breaks and tabs kept ("\r\n" becomes "\n"). */
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("copy me")));
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 0, 4));
    clip[0] = 0;
    GT_ASSERT(key_mods(a.t, GATES_KEY_C, true, false) && strcmp(clip, "copy") == 0);
    GT_ASSERT(key_mods(a.t, GATES_KEY_X, true, false) && text_is(&a, " me"));
    snprintf(clip, sizeof clip, "a\r\nb\tc\rd\x01");
    GT_ASSERT(key_mods(a.t, GATES_KEY_V, true, false));
    GT_ASSERT(text_is(&a, "a\nb\tc\nd me"));
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false) && text_is(&a, " me"));
    /* Cutting nothing, or without a clipboard, keeps the text. */
    GT_ASSERT(key_mods(a.t, GATES_KEY_X, true, false) && text_is(&a, " me"));
    gates_tree_set_clipboard(a.t, nullptr);
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 0, 2));
    GT_ASSERT(key_mods(a.t, GATES_KEY_X, true, false) && text_is(&a, " me"));
    GT_ASSERT(key_mods(a.t, GATES_KEY_V, true, false) && text_is(&a, " me"));
    done(&a);
}

/* -- Tab, read-only, limit ----------------------------------------------------------------------- */

static void test_tab_read_only_limit(void) {
    app_t a;
    make(&a, &(gates_editor_desc_t){0});
    /* By default Tab moves focus: no keyboard trap. */
    GT_ASSERT(key(a.t, GATES_KEY_TAB));
    GT_ASSERT(gates_node_eq(gates_tree_focus(a.t), a.after) && text_is(&a, ""));
    done(&a);
    make(&a, &(gates_editor_desc_t){ .tab_inserts = true, .tab_width = 4 });
    GT_ASSERT(key(a.t, GATES_KEY_TAB) && text_is(&a, "\t") && gates_node_eq(gates_tree_focus(a.t), a.ed));
    GT_ASSERT(key_mods(a.t, GATES_KEY_TAB, false, true)); /* Shift+Tab still leaves */
    GT_ASSERT(gates_node_eq(gates_tree_focus(a.t), a.before));
    done(&a);
    /* Read-only: selectable and copyable, not editable. */
    make(&a, &(gates_editor_desc_t){ .read_only = true });
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("fixed")));
    GT_ASSERT(gates_input_char(a.t, 'x') == GATES_INPUT_IGNORED);
    GT_ASSERT(!key(a.t, GATES_KEY_BACKSPACE) && !key(a.t, GATES_KEY_ENTER));
    GT_ASSERT(key_mods(a.t, GATES_KEY_A, true, false));
    GT_ASSERT(key_mods(a.t, GATES_KEY_C, true, false) && strcmp(clip, "fixed") == 0);
    GT_ASSERT(!key_mods(a.t, GATES_KEY_X, true, false) && text_is(&a, "fixed"));
    GT_ASSERT(key_mods(a.t, GATES_KEY_V, true, false) && text_is(&a, "fixed"));
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false) && text_is(&a, "fixed"));
    GT_ASSERT(gates_editor_undo(a.t, a.ed) == PROVEN_ERR_INVALID_STATE);
    done(&a);
    /* The byte limit refuses what would pass it. */
    make(&a, &(gates_editor_desc_t){ .max_bytes = 4 });
    type(a.t, "abcde");
    GT_ASSERT(text_is(&a, "abcd"));
    GT_ASSERT(gates_input_take_error(a.t) == PROVEN_ERR_OUT_OF_BOUNDS);
    GT_ASSERT(gates_editor_replace(a.t, a.ed, 0, 0, GATES_STR("zz"), true) == PROVEN_ERR_OUT_OF_BOUNDS);
    done(&a);
}

/* -- painting ------------------------------------------------------------------------------------- */

static int count_text(const gates_draw_list_t *dl, gates_rect_t inside) {
    int n = 0;
    for (gates_u32 i = 0; i < dl->len; i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(dl, i);
        if (c->kind == GATES_DRAW_TEXT && c->rect.y >= inside.y && c->rect.y < inside.y + inside.h) n++;
    }
    return n;
}

static const gates_draw_cmd_t *text_cmd(const gates_draw_list_t *dl, const char *s) {
    for (gates_u32 i = 0; i < dl->len; i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(dl, i);
        gates_str_t t = gates_draw_cmd_text(dl, c);
        if (c->kind == GATES_DRAW_TEXT && t.size == strlen(s) && memcmp(t.ptr, s, t.size) == 0) return c;
    }
    return nullptr;
}

static void paint(app_t *a, gates_draw_list_t *dl) {
    gates_draw_list_reset(dl);
    GT_ASSERT_OK(gates_paint_tree(a->t, dl, theme, be));
}

static void test_painting(void) {
    app_t a;
    make(&a, &(gates_editor_desc_t){ .tab_width = 4 });
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    /* A long text: only the lines shown are painted. */
    static char big[40000];
    int n = 0;
    for (int i = 0; i < 2000; i++) n += snprintf(big + n, sizeof big - (size_t)n, "line %d\n", i);
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)big, (gates_usize_t)n }));
    layout(a.t);
    gates_u32 vis = gates_editor_visible_lines(a.t, a.ed);
    GT_ASSERT(vis >= 5 && vis < 40);
    paint(&a, &dl);
    gates_rect_t er = gates_node_layout_rect(a.t, a.ed);
    GT_ASSERT(count_text(&dl, er) >= (int)vis && count_text(&dl, er) <= (int)vis + 1);
    gates_point_t o = origin(&a);
    const gates_draw_cmd_t *c = text_cmd(&dl, "line 1");
    GT_ASSERT(c != nullptr && c->rect.x == o.x && c->rect.y == o.y + 16);
    GT_ASSERT(text_cmd(&dl, "line 500") == nullptr);
    /* Scrolled to a line far down: it is painted, the top ones are not. */
    GT_ASSERT_OK(gates_editor_scroll_to(a.t, a.ed, gates_text_buffer_line_start(gates_editor_buffer(a.t, a.ed), 1500)));
    gates_u32 first = gates_editor_first_line(a.t, a.ed);
    GT_ASSERT(first <= 1500 && 1500 < first + vis);
    paint(&a, &dl);
    GT_ASSERT(text_cmd(&dl, "line 1500") != nullptr && text_cmd(&dl, "line 1") == nullptr);
    /* Tabs split the text; runs start at tab stops (4 spaces = 32 px). */
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("a\tbc\tdef\n\tx")));
    layout(a.t);
    paint(&a, &dl);
    o = origin(&a);
    const gates_draw_cmd_t *ta = text_cmd(&dl, "a"), *tb = text_cmd(&dl, "bc"), *td = text_cmd(&dl, "def"),
                           *tx = text_cmd(&dl, "x");
    GT_ASSERT(ta != nullptr && ta->rect.x == o.x);
    GT_ASSERT(tb != nullptr && tb->rect.x == o.x + 32);
    GT_ASSERT(td != nullptr && td->rect.x == o.x + 64);
    GT_ASSERT(tx != nullptr && tx->rect.x == o.x + 32 && tx->rect.y == o.y + 16);
    /* Selection over a line end is drawn past the text; selected text in its colour. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 3, 10));
    paint(&a, &dl);
    gates_color_t sel_bg = gates_theme_color(theme, GATES_COLOR_SELECTION_BG);
    gates_color_t sel_fg = gates_theme_color(theme, GATES_COLOR_SELECTION_FG);
    bool row0 = false, row1 = false;
    for (gates_u32 i = 0; i < dl.len; i++) {
        const gates_draw_cmd_t *d = gates_draw_list_at(&dl, i);
        if (d->kind != GATES_DRAW_RECT || d->color.r != sel_bg.r || d->color.g != sel_bg.g || d->color.b != sel_bg.b) continue;
        if (d->rect.y == o.y && d->rect.x == o.x + 40 && d->rect.w == 88 + 8 - 40) row0 = true; /* "c\tdef" + the break */
        if (d->rect.y == o.y + 16 && d->rect.x == o.x && d->rect.w == 32) row1 = true;       /* "\t" */
    }
    GT_ASSERT(row0 && row1);
    const gates_draw_cmd_t *sc = text_cmd(&dl, "c");
    GT_ASSERT(sc != nullptr && sc->color.r == sel_fg.r && sc->color.b == sel_fg.b);
    /* The caret: a 1-unit bar at its place, only while focused, remembered for the IME. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 2, 2));
    paint(&a, &dl);
    gates_color_t fg = gates_theme_color(theme, GATES_COLOR_CONTROL_FG);
    bool bar = false;
    for (gates_u32 i = 0; i < dl.len; i++) {
        const gates_draw_cmd_t *d = gates_draw_list_at(&dl, i);
        if (d->kind == GATES_DRAW_RECT && d->rect.w == 1 && d->rect.h == 16 && d->rect.x == o.x + 32 && d->rect.y == o.y &&
            d->color.r == fg.r) bar = true;
    }
    GT_ASSERT(bar);
    gates_tree_set_focus(a.t, a.before);
    paint(&a, &dl);
    bar = false;
    for (gates_u32 i = 0; i < dl.len; i++) {
        const gates_draw_cmd_t *d = gates_draw_list_at(&dl, i);
        if (d->kind == GATES_DRAW_RECT && d->rect.w == 1 && d->rect.h == 16 && d->rect.x == o.x + 32) bar = true;
    }
    GT_ASSERT(!bar);
    /* A long line scrolls sideways to keep the caret shown. */
    gates_tree_set_focus(a.t, a.ed);
    char longl[300];
    memset(longl, 'w', 299);
    longl[299] = 0;
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("short")));
    GT_ASSERT_OK(gates_editor_replace(a.t, a.ed, 5, 5, (gates_str_t){ (const gates_u8 *)longl, 299 }, false));
    layout(a.t);
    GT_ASSERT(key_mods(a.t, GATES_KEY_END, true, false));
    paint(&a, &dl);
    const gates_draw_cmd_t *lc = nullptr;
    for (gates_u32 i = 0; i < dl.len && lc == nullptr; i++) {
        const gates_draw_cmd_t *d = gates_draw_list_at(&dl, i);
        if (d->kind == GATES_DRAW_TEXT && gates_draw_cmd_text(&dl, d).size == 304) lc = d;
    }
    GT_ASSERT(lc != nullptr && lc->rect.x < origin(&a).x); /* scrolled left */
    /* A run left of the view is not drawn at all. */
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("left\t")));
    GT_ASSERT_OK(gates_editor_replace(a.t, a.ed, 5, 5, (gates_str_t){ (const gates_u8 *)longl, 299 }, false));
    GT_ASSERT(key_mods(a.t, GATES_KEY_END, true, false));
    paint(&a, &dl);
    GT_ASSERT(text_cmd(&dl, "left") == nullptr);
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 0, 0));
    paint(&a, &dl);
    GT_ASSERT(text_cmd(&dl, "left") != nullptr);
    gates_draw_list_deinit(&dl);
    done(&a);
}

/* -- pointer and wheel ----------------------------------------------------------------------------- */

static void test_pointer(void) {
    app_t a;
    make(&a, &(gates_editor_desc_t){0});
    static char big[20000];
    int n = 0;
    for (int i = 0; i < 400; i++) n += snprintf(big + n, sizeof big - (size_t)n, "word%03d next\n", i);
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)big, (gates_usize_t)n }));
    layout(a.t);
    gates_tree_set_focus(a.t, a.before);
    gates_point_t o = origin(&a);
    /* A press places the caret (and focuses): line 1, column 3 (x at 3.4 cells). */
    press(&a, (gates_point_t){ o.x + 27, o.y + 16 + 5 }, 1);
    release(&a, (gates_point_t){ o.x + 27, o.y + 16 + 5 });
    GT_ASSERT(gates_node_eq(gates_tree_focus(a.t), a.ed));
    GT_ASSERT(caret(&a) == 13 + 3 && anchor(&a) == caret(&a));
    press(&a, (gates_point_t){ o.x + 29, o.y + 16 + 5 }, 1); /* 3.6 cells: the nearer edge is 4 */
    release(&a, (gates_point_t){ o.x + 29, o.y + 16 + 5 });
    GT_ASSERT(caret(&a) == 13 + 4);
    /* A drag selects; past the right end of a line, its end. */
    press(&a, (gates_point_t){ o.x + 1, o.y + 1 }, 1);
    move(&a, (gates_point_t){ o.x + 300, o.y + 32 + 3 });
    release(&a, (gates_point_t){ o.x + 300, o.y + 32 + 3 });
    GT_ASSERT(anchor(&a) == 0 && caret(&a) == 26 + 12);
    /* A double click selects the word under the pointer. */
    press(&a, (gates_point_t){ o.x + 17, o.y + 16 * 2 + 4 }, 1);
    release(&a, (gates_point_t){ o.x + 17, o.y + 16 * 2 + 4 });
    press(&a, (gates_point_t){ o.x + 17, o.y + 16 * 2 + 4 }, 2);
    release(&a, (gates_point_t){ o.x + 17, o.y + 16 * 2 + 4 });
    GT_ASSERT(anchor(&a) == 26 && caret(&a) == 33);
    /* The wheel scrolls three lines a notch; never past the ends. */
    gates_pointer_event_t w = { .action = GATES_POINTER_WHEEL, .pos = { o.x + 10, o.y + 10 }, .wheel = { 0, -1 } };
    (void)gates_input_pointer(a.t, &w);
    GT_ASSERT(gates_editor_first_line(a.t, a.ed) == 3);
    w.wheel.y = 5;
    (void)gates_input_pointer(a.t, &w);
    GT_ASSERT(gates_editor_first_line(a.t, a.ed) == 0);
    /* The vertical thumb drags the view; a press on the track pages. */
    gates_rect_t r = gates_node_layout_rect(a.t, a.ed);
    gates_point_t thumb = { r.x + r.w - 5, r.y + 4 };
    press(&a, thumb, 1);
    move(&a, (gates_point_t){ thumb.x, r.y + r.h / 2 });
    release(&a, (gates_point_t){ thumb.x, r.y + r.h / 2 });
    gates_u32 mid = gates_editor_first_line(a.t, a.ed);
    GT_ASSERT(mid > 100 && mid < 300);
    press(&a, (gates_point_t){ thumb.x, r.y + r.h - 20 }, 1);
    release(&a, (gates_point_t){ thumb.x, r.y + r.h - 20 });
    GT_ASSERT(gates_editor_first_line(a.t, a.ed) == mid + gates_editor_visible_lines(a.t, a.ed));
    /* PageDown moves the caret and the view by a page. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 0, 0));
    gates_u32 vis = gates_editor_visible_lines(a.t, a.ed);
    GT_ASSERT(key(a.t, GATES_KEY_PAGE_DOWN));
    GT_ASSERT(gates_text_buffer_line_of(gates_editor_buffer(a.t, a.ed), caret(&a)) == vis);
    GT_ASSERT(gates_editor_first_line(a.t, a.ed) == vis); /* the view moved by a page too */
    GT_ASSERT(key(a.t, GATES_KEY_PAGE_UP) && caret(&a) == 0);
    done(&a);
}

/* -- accessibility --------------------------------------------------------------------------------- */

static void test_access(void) {
    app_t a;
    make(&a, &(gates_editor_desc_t){0});
    GT_ASSERT_OK(gates_node_set_access_name(a.t, a.ed, GATES_STR("Notes")));
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("first\nsecond")));
    GT_ASSERT_OK(gates_editor_replace(a.t, a.ed, 0, 0, GATES_STR(">"), false)); /* the gap in the middle */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 1, 3));
    gates_access_info_t info;
    GT_ASSERT_OK(gates_access_info(a.t, a.ed, 0, &info));
    GT_ASSERT(info.role == GATES_ROLE_EDIT);
    GT_ASSERT(info.value.size == 13 && memcmp(info.value.ptr, ">first\nsecond", 13) == 0);
    GT_ASSERT(info.anchor == 1 && info.caret == 3);
    GT_ASSERT((info.actions & GATES_ACCESS_SET_VALUE) != 0 && (info.states & GATES_ACCESS_READ_ONLY) == 0);
    GT_ASSERT_OK(gates_editor_set_read_only(a.t, a.ed, true));
    GT_ASSERT_OK(gates_access_info(a.t, a.ed, 0, &info));
    GT_ASSERT((info.states & GATES_ACCESS_READ_ONLY) != 0 && (info.actions & GATES_ACCESS_SET_VALUE) == 0);
    gates_access_issue_t issues[4];
    GT_ASSERT(gates_access_audit(a.t, theme, issues, 4) == 0);
    done(&a);
}

/* -- allocation failure ---------------------------------------------------------------------------- */

typedef struct fail_alloc_t {
    gates_allocator_t inner;
    int left;
} fail_alloc_t;
static proven_result_mem_mut_t fa_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->left == 0) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    if (f->left > 0) f->left--;
    return f->inner.alloc_fn(f->inner.ctx, size, align);
}
static proven_result_mem_mut_t fa_realloc(void *ctx, void *p, proven_size_t os, proven_size_t ns, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->left == 0) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    if (f->left > 0) f->left--;
    return f->inner.realloc_fn(f->inner.ctx, p, os, ns, align);
}
static void fa_free(void *ctx, void *p) {
    fail_alloc_t *f = ctx;
    f->inner.free_fn(f->inner.ctx, p);
}

static void test_failures(void) {
    bool made = false;
    for (int k = 0; k < 40 && !made; k++) {
        fail_alloc_t f = { .inner = proven_heap_allocator(), .left = -1 };
        gates_allocator_t al = { .ctx = &f, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc, .free_fn = fa_free };
        gates_tree_t *t = nullptr;
        GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = al }, &t));
        f.left = k;
        gates_node_t ed = GATES_NODE_NULL;
        gates_err_t err = gates_editor_create(t, gates_tree_root(t), &(gates_editor_desc_t){0}, &ed);
        f.left = -1;
        made = gates_is_ok(err);
        if (!made) GT_ASSERT(err == PROVEN_ERR_NOMEM && gates_node_child_count(t, gates_tree_root(t)) == 0);
        gates_tree_destroy(t);
    }
    GT_ASSERT(made);
    /* Typing while memory runs out: every keystroke lands whole or not at all. */
    for (int k = 0; k < 30; k++) {
        fail_alloc_t f = { .inner = proven_heap_allocator(), .left = -1 };
        gates_allocator_t al = { .ctx = &f, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc, .free_fn = fa_free };
        gates_tree_t *t = nullptr;
        GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = al }, &t));
        gates_node_t ed;
        GT_ASSERT_OK(gates_editor_create(t, gates_tree_root(t), &(gates_editor_desc_t){0}, &ed));
        GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 300, 200 }, be));
        gates_tree_set_focus(t, ed);
        f.left = k;
        char want[64] = "";
        size_t wn = 0;
        for (int i = 0; i < 20; i++) {
            gates_u32 before = gates_editor_length(t, ed);
            (void)gates_input_char(t, (gates_u32)('a' + i));
            if (gates_editor_length(t, ed) == before + 1) want[wn++] = (char)('a' + i);
            else GT_ASSERT(gates_editor_length(t, ed) == before);
        }
        f.left = -1;
        char got[64];
        const gates_text_buffer_t *tb = gates_editor_buffer(t, ed);
        GT_ASSERT(gates_text_buffer_copy(tb, 0, 64, (gates_u8 *)got, 64) == wn && memcmp(got, want, wn) == 0);
        /* Undo never replays a wrong step: undoing everything and redoing it gives the text back. */
        gates_u32 undone = 0;
        while (gates_editor_can_undo(t, ed)) {
            GT_ASSERT_OK(gates_editor_undo(t, ed));
            undone++;
        }
        GT_ASSERT(gates_editor_length(t, ed) <= wn);
        if (undone > 0) GT_ASSERT(gates_editor_length(t, ed) == 0); /* a whole history reaches the start */
        while (gates_editor_can_redo(t, ed)) GT_ASSERT_OK(gates_editor_redo(t, ed));
        GT_ASSERT(gates_text_buffer_copy(tb, 0, 64, (gates_u8 *)got, 64) == wn && memcmp(got, want, wn) == 0);
        gates_tree_destroy(t);
    }
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_create_and_program();
    test_typing_and_history();
    test_moving();
    test_deleting_and_clipboard();
    test_tab_read_only_limit();
    test_painting();
    test_pointer();
    test_access();
    test_failures();
    return gt_report("test_editor");
}
