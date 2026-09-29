/* T055: the multi-line editor node (plan-0022 stage 2) - typing, lines,
 * caret movement, deletion runs, selection, clipboard, Tab, read-only, the
 * byte limit, undo/redo and the modified mark, program edits, painting only
 * the lines shown, tabs, pointer and wheel, scrolling, events, accessibility
 * and allocation failure. Builtin text: 8 px per cell, 16 px lines. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/editor.h>
#include <gates/layout.h>
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
    GT_ASSERT(key_mods(a.t, GATES_KEY_TAB, false, true) && text_is(&a, "")); /* Shift+Tab unindents */
    GT_ASSERT(gates_node_eq(gates_tree_focus(a.t), a.ed));
    GT_ASSERT(key_mods(a.t, GATES_KEY_TAB, true, true)); /* Ctrl+Shift+Tab leaves */
    GT_ASSERT(gates_node_eq(gates_tree_focus(a.t), a.before));
    gates_tree_set_focus(a.t, a.ed);
    GT_ASSERT(key_mods(a.t, GATES_KEY_TAB, true, false)); /* Ctrl+Tab leaves forward */
    GT_ASSERT(gates_node_eq(gates_tree_focus(a.t), a.after));
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
    /* Below the last row: the end of the text. */
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("ab\ncd")));
    press(&a, (gates_point_t){ o.x + 2, o.y + 16 * 5 }, 1);
    release(&a, (gates_point_t){ o.x + 2, o.y + 16 * 5 });
    GT_ASSERT(caret(&a) == 5);
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)big, (gates_usize_t)n }));
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

/* -- stage 3: wrap, gutter, highlighting, marks, find, indenting --------------------------------- */

/* The editor's own text commands (not the buttons around it), in order. */
static gates_rect_t text_zone;
static int texts(const gates_draw_list_t *dl, const gates_draw_cmd_t **out, int cap) {
    int n = 0;
    for (gates_u32 i = 0; i < dl->len && n < cap; i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(dl, i);
        if (c->kind == GATES_DRAW_TEXT && gates_rect_contains(text_zone, (gates_point_t){ c->rect.x + 1, c->rect.y + 1 })) {
            out[n++] = c;
        }
    }
    return n;
}

static bool cmd_is(const gates_draw_list_t *dl, const gates_draw_cmd_t *c, const char *z) {
    gates_str_t t = gates_draw_cmd_text(dl, c);
    return t.size == strlen(z) && memcmp(t.ptr, z, t.size) == 0;
}

static void test_wrap(void) {
    app_t a;
    make(&a, &(gates_editor_desc_t){ .wrap = true });
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_rect_t er = gates_node_layout_rect(a.t, a.ed);
    text_zone = er;
    gates_i32 textw = er.w - 2 - 6 - GATES_SCROLLBAR_PX; /* border, inset, the scrollbar kept with wrap */
    gates_i32 cols = textw / 8;
    /* Words: a row ends after the last blank that fits. */
    char line[400] = "";
    for (int i = 0; i < 30; i++) strcat(line, "word ");
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)line, strlen(line) }));
    layout(a.t);
    paint(&a, &dl);
    const gates_draw_cmd_t *tc[64];
    int n = texts(&dl, tc, 64);
    gates_point_t o = origin(&a);
    int per_row = (cols + 1) / 5; /* "word " x k: the last blank may hang past the edge */
    GT_ASSERT(n >= 2);
    GT_ASSERT(tc[0]->rect.y == o.y && tc[1]->rect.y == o.y + 16 && tc[1]->rect.x == o.x);
    GT_ASSERT(gates_draw_cmd_text(&dl, tc[0]).size == (gates_usize_t)per_row * 5);
    /* No sideways scrolling with wrap. */
    GT_ASSERT(key_mods(a.t, GATES_KEY_END, true, false));
    GT_ASSERT(gates_editor_first_line(a.t, a.ed) == 0);
    paint(&a, &dl);
    n = texts(&dl, tc, 64);
    GT_ASSERT(tc[0]->rect.x == o.x);
    /* Up/Down move by rows and keep the column. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 7, 7)); /* row 0, column 7 */
    GT_ASSERT(key(a.t, GATES_KEY_DOWN) && caret(&a) == (gates_u32)per_row * 5 + 7);
    GT_ASSERT(key(a.t, GATES_KEY_UP) && caret(&a) == 7);
    GT_ASSERT(key(a.t, GATES_KEY_UP) && caret(&a) == 0); /* past the first row: the start */
    /* A long word breaks where it must; the rows cover every byte once. */
    char word[300];
    memset(word, 'x', 299);
    word[299] = 0;
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)word, 299 }));
    paint(&a, &dl);
    n = texts(&dl, tc, 64);
    gates_usize_t total = 0;
    for (int i = 0; i < n; i++) {
        total += gates_draw_cmd_text(&dl, tc[i]).size;
        GT_ASSERT(tc[i]->rect.y == o.y + 16 * i && gates_draw_cmd_text(&dl, tc[i]).size <= (gates_usize_t)cols);
    }
    GT_ASSERT(total == 299);
    /* A press on the second row places the caret there. */
    press(&a, (gates_point_t){ o.x + 8 * 3 + 2, o.y + 16 + 4 }, 1);
    release(&a, (gates_point_t){ o.x + 8 * 3 + 2, o.y + 16 + 4 });
    GT_ASSERT(caret(&a) == (gates_u32)cols + 3);
    /* The end of a row is shown at the next row's start: Down from row 0's end lands in row 1. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, (gates_u32)cols - 1, (gates_u32)cols - 1));
    GT_ASSERT(key(a.t, GATES_KEY_DOWN) && caret(&a) == 2u * (gates_u32)cols - 1);
    /* Many wrapped lines: keeping the caret shown scrolls by rows, PageDown pages by rows. */
    static char many[20000];
    int m = 0;
    for (int i = 0; i < 40; i++) m += snprintf(many + m, sizeof many - (size_t)m, "%.*s\n", cols + 10, word);
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)many, (gates_usize_t)m }));
    layout(a.t);
    gates_u32 vis = gates_editor_visible_lines(a.t, a.ed);
    GT_ASSERT(key(a.t, GATES_KEY_PAGE_DOWN));
    GT_ASSERT(gates_text_buffer_line_of(gates_editor_buffer(a.t, a.ed), caret(&a)) == vis / 2); /* two rows a line */
    GT_ASSERT(key_mods(a.t, GATES_KEY_END, true, false));
    paint(&a, &dl);
    n = texts(&dl, tc, 64);
    GT_ASSERT(n > 0 && cmd_is(&dl, tc[n - 1], "xxxxxxxxxx")); /* the last line's second row is the last shown */
    gates_pointer_event_t w = { .action = GATES_POINTER_WHEEL, .pos = { o.x + 5, o.y + 5 }, .wheel = { 0, 1 } };
    gates_u32 before = gates_editor_first_line(a.t, a.ed);
    (void)gates_input_pointer(a.t, &w);
    GT_ASSERT(gates_editor_first_line(a.t, a.ed) == before - 2 || gates_editor_first_line(a.t, a.ed) == before - 1);
    /* The wheel stops with the last row at the bottom. */
    GT_ASSERT(key_mods(a.t, GATES_KEY_END, true, false));
    gates_u32 end_first = gates_editor_first_line(a.t, a.ed);
    w.wheel.y = -5;
    (void)gates_input_pointer(a.t, &w);
    GT_ASSERT(gates_editor_first_line(a.t, a.ed) == end_first);
    paint(&a, &dl);
    n = texts(&dl, tc, 64);
    GT_ASSERT(n > 0 && tc[n - 1]->rect.y == o.y + 16 * ((gates_i32)vis - 2)); /* the empty last line is the bottom row */
    /* A blank that just fills a row hangs past it: the next row starts with the word. */
    char hang[300];
    memset(hang, 'x', (size_t)cols);
    memcpy(hang + cols, " bbb", 5);
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)hang, strlen(hang) }));
    paint(&a, &dl);
    n = texts(&dl, tc, 64);
    GT_ASSERT(n == 2 && cmd_is(&dl, tc[1], "bbb"));
    /* The caret at a row boundary is drawn at the next row's start. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, (gates_u32)cols + 1, (gates_u32)cols + 1));
    paint(&a, &dl);
    bool at_next = false;
    for (gates_u32 i = 0; i < dl.len; i++) {
        const gates_draw_cmd_t *d = gates_draw_list_at(&dl, i);
        if (d->kind == GATES_DRAW_RECT && d->rect.w == 1 && d->rect.h == 16 && d->rect.x == o.x && d->rect.y == o.y + 16) {
            at_next = true;
        }
    }
    GT_ASSERT(at_next);
    /* A caret on a row boundary belongs to the next row: Down keeps column 0. */
    char three[400];
    memset(three, 'x', (size_t)cols);
    three[cols] = ' ';
    memset(three + cols + 1, 'y', (size_t)cols);
    three[2 * cols + 1] = ' ';
    memcpy(three + 2 * cols + 2, "zz", 3);
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)three, strlen(three) }));
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, (gates_u32)cols + 1, (gates_u32)cols + 1));
    GT_ASSERT(key(a.t, GATES_KEY_DOWN) && caret(&a) == 2u * ((gates_u32)cols + 1));
    /* Up from far right in a long row into a short row stays in the short row. */
    char shortrow[300] = "aaaa ";
    memset(shortrow + 5, 'y', (size_t)cols);
    shortrow[5 + cols] = 0;
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)shortrow, strlen(shortrow) }));
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 5 + 20, 5 + 20));
    GT_ASSERT(key(a.t, GATES_KEY_UP) && caret(&a) == 4);
    /* Wrap can be turned off and on later: off, the long word is one row again. */
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)word, 299 }));
    GT_ASSERT_OK(gates_editor_set_wrap(a.t, a.ed, false));
    GT_ASSERT_OK(gates_editor_set_wrap(a.t, a.ed, false));
    paint(&a, &dl);
    n = texts(&dl, tc, 64);
    GT_ASSERT(n == 1 && gates_draw_cmd_text(&dl, tc[0]).size == 299);
    GT_ASSERT_OK(gates_editor_set_wrap(a.t, a.ed, true));
    paint(&a, &dl);
    n = texts(&dl, tc, 64);
    GT_ASSERT(n > 1);
    GT_ASSERT(gates_editor_set_wrap(a.t, gates_tree_root(a.t), true) == PROVEN_ERR_INVALID_ARG);
    /* A narrower view wraps again. */
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)word, 299 }));
    GT_ASSERT_OK(gates_layout_run(a.t, (gates_size_t){ 200, 300 }, be));
    text_zone = gates_node_layout_rect(a.t, a.ed);
    paint(&a, &dl);
    n = texts(&dl, tc, 64);
    GT_ASSERT(n > 0 && gates_draw_cmd_text(&dl, tc[0]).size < (gates_usize_t)cols);
    gates_draw_list_deinit(&dl);
    done(&a);
}

static void test_gutter(void) {
    app_t a;
    make(&a, &(gates_editor_desc_t){ .line_numbers = true });
    static char big[20000];
    int n = 0;
    for (int i = 0; i < 1200; i++) n += snprintf(big + n, sizeof big - (size_t)n, "l%d\n", i);
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)big, (gates_usize_t)n }));
    layout(a.t);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    paint(&a, &dl);
    gates_rect_t er = gates_node_layout_rect(a.t, a.ed);
    gates_i32 gw = 4 * 8 + 6; /* four digits for 1201 lines */
    const gates_draw_cmd_t *l0 = text_cmd(&dl, "l0"), *n1 = text_cmd(&dl, "1"), *n2 = text_cmd(&dl, "2");
    GT_ASSERT(l0 != nullptr && l0->rect.x == er.x + 1 + gw + 3);
    GT_ASSERT(n1 != nullptr && n1->rect.x + n1->rect.w == er.x + 1 + gw - 3 && n1->rect.y == l0->rect.y);
    GT_ASSERT(n2 != nullptr && n2->rect.y == l0->rect.y + 16);
    /* Down from the bottom row scrolls by one line, not a page. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 0, 0));
    for (gates_u32 i = 0; i < gates_editor_visible_lines(a.t, a.ed); i++) GT_ASSERT(key(a.t, GATES_KEY_DOWN));
    GT_ASSERT(gates_editor_first_line(a.t, a.ed) == 1);
    /* The gutter can go and come back. */
    GT_ASSERT_OK(gates_editor_set_line_numbers(a.t, a.ed, false));
    GT_ASSERT_OK(gates_editor_set_line_numbers(a.t, a.ed, false));
    paint(&a, &dl);
    const gates_draw_cmd_t *l1 = text_cmd(&dl, "l1"); /* the view starts at line 1 here */
    GT_ASSERT(text_cmd(&dl, "2") == nullptr && l1 != nullptr && l1->rect.x == er.x + 1 + 3);
    GT_ASSERT_OK(gates_editor_set_line_numbers(a.t, a.ed, true));
    paint(&a, &dl);
    GT_ASSERT(text_cmd(&dl, "2") != nullptr);
    GT_ASSERT(gates_editor_set_line_numbers(a.t, gates_tree_root(a.t), true) == PROVEN_ERR_INVALID_ARG);
    /* Ctrl+End: the caret on the bottom row. */
    GT_ASSERT(key_mods(a.t, GATES_KEY_END, true, false));
    GT_ASSERT(gates_editor_first_line(a.t, a.ed) == 1201 - gates_editor_visible_lines(a.t, a.ed));
    /* Numbers follow scrolling. */
    GT_ASSERT_OK(gates_editor_scroll_to(a.t, a.ed, gates_text_buffer_line_start(gates_editor_buffer(a.t, a.ed), 1000)));
    paint(&a, &dl);
    GT_ASSERT(text_cmd(&dl, "1001") != nullptr && text_cmd(&dl, "1") == nullptr);
    /* A press in the gutter goes to its line's start. */
    gates_point_t o = origin(&a);
    gates_u32 first = gates_editor_first_line(a.t, a.ed);
    press(&a, (gates_point_t){ er.x + 5, o.y + 16 + 3 }, 1);
    release(&a, (gates_point_t){ er.x + 5, o.y + 16 + 3 });
    GT_ASSERT(caret(&a) == gates_text_buffer_line_start(gates_editor_buffer(a.t, a.ed), first + 1));
    gates_draw_list_deinit(&dl);
    done(&a);
    /* With wrap, a line's number is on its first row only. */
    make(&a, &(gates_editor_desc_t){ .line_numbers = true, .wrap = true });
    char longl[400];
    memset(longl, 'z', 300);
    memcpy(longl + 300, "\nsecond", 8);
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)longl, 307 }));
    layout(a.t);
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    paint(&a, &dl);
    const gates_draw_cmd_t *one = text_cmd(&dl, "1"), *two = text_cmd(&dl, "2"), *sec = text_cmd(&dl, "second");
    GT_ASSERT(one != nullptr && two != nullptr && sec != nullptr && two->rect.y == sec->rect.y && two->rect.y > one->rect.y + 16);
    int ones = 0;
    for (gates_u32 i = 0; i < dl.len; i++) {
        const gates_draw_cmd_t *d = gates_draw_list_at(&dl, i);
        if (d->kind == GATES_DRAW_TEXT && gates_draw_cmd_text(&dl, d).size == 1 &&
            (*gates_draw_cmd_text(&dl, d).ptr == '1' || *gates_draw_cmd_text(&dl, d).ptr == '3')) ones++;
    }
    GT_ASSERT(ones == 1);
    gates_draw_list_deinit(&dl);
    done(&a);
}

typedef struct styler_t {
    int calls;
    gates_u32 from[16], to[16];
} styler_t;

/* A tiny highlighter: digits are style 1, the word "if" style 2. */
static void styler(void *user, gates_text_buffer_t *b, gates_u32 from, gates_u32 to) {
    styler_t *s = user;
    if (s->calls < 16) {
        s->from[s->calls] = from;
        s->to[s->calls] = to;
    }
    s->calls++;
    gates_text_buffer_set_style(b, from, to, 0);
    for (gates_u32 i = from; i < to; i++) {
        gates_u8 c = gates_text_buffer_byte(b, i);
        if (c >= '0' && c <= '9') gates_text_buffer_set_style(b, i, i + 1, 1);
        if (c == 'i' && gates_text_buffer_byte(b, i + 1) == 'f') gates_text_buffer_set_style(b, i, i + 2, 2);
    }
}

static void test_highlighting(void) {
    app_t a;
    make(&a, &(gates_editor_desc_t){0});
    static char big[20000];
    int n = 0;
    for (int i = 0; i < 500; i++) n += snprintf(big + n, sizeof big - (size_t)n, "if x%d\n", i % 10);
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)big, (gates_usize_t)n }));
    layout(a.t);
    gates_editor_style_t styles[3] = { {0}, { .use_rgb = true, .rgb = GATES_RGBA(200, 0, 0, 255) },
                                       { .token = GATES_COLOR_ERROR } };
    GT_ASSERT(gates_editor_set_styles(a.t, a.ed, styles, 257) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_set_styles(a.t, a.ed, nullptr, 2) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_editor_set_styles(a.t, a.ed, styles, 3));
    styler_t st = {0};
    GT_ASSERT_OK(gates_editor_set_styler(a.t, a.ed, styler, &st));
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    paint(&a, &dl);
    /* Asked once, from the start to the end of the lines shown - not the whole text. */
    GT_ASSERT(st.calls == 1 && st.from[0] == 0 && st.to[0] > 0 && st.to[0] < (gates_u32)n / 4);
    GT_ASSERT(st.to[0] == gates_text_buffer_line_start(gates_editor_buffer(a.t, a.ed),
                                                      gates_editor_first_line(a.t, a.ed) + gates_editor_visible_lines(a.t, a.ed) + 2) ||
              st.to[0] == gates_text_buffer_line_start(gates_editor_buffer(a.t, a.ed),
                                                      gates_editor_first_line(a.t, a.ed) + gates_editor_visible_lines(a.t, a.ed) + 1));
    /* Runs are cut at style changes and drawn in their colours. */
    const gates_draw_cmd_t *kw = text_cmd(&dl, "if"), *num = text_cmd(&dl, "3"), *x = text_cmd(&dl, " x");
    gates_color_t err_c = gates_theme_color(theme, GATES_COLOR_ERROR), plain = gates_theme_color(theme, GATES_COLOR_CONTROL_FG);
    GT_ASSERT(kw != nullptr && kw->color.r == err_c.r && kw->color.g == err_c.g);
    GT_ASSERT(num != nullptr && num->color.r == 200 && num->color.g == 0);
    GT_ASSERT(x != nullptr && x->color.r == plain.r);
    /* Nothing stale: no call. An edit makes its line stale from its start. */
    paint(&a, &dl);
    GT_ASSERT(st.calls == 1);
    gates_u32 l3 = gates_text_buffer_line_start(gates_editor_buffer(a.t, a.ed), 3);
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, l3 + 2, l3 + 2));
    type(a.t, "7");
    paint(&a, &dl);
    GT_ASSERT(st.calls == 2 && st.from[1] == l3);
    /* Undo makes it stale too. */
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false));
    paint(&a, &dl);
    GT_ASSERT(st.calls == 3 && st.from[2] == l3);
    /* Scrolling down styles only what comes into view. */
    gates_u32 before_to = st.to[2];
    GT_ASSERT_OK(gates_editor_scroll_to(a.t, a.ed, gates_text_buffer_line_start(gates_editor_buffer(a.t, a.ed), 400)));
    paint(&a, &dl);
    GT_ASSERT(st.calls == 4 && st.from[3] == before_to);
    /* Selected text is drawn in the selection colour whatever its style. */
    gates_u32 l400 = gates_text_buffer_line_start(gates_editor_buffer(a.t, a.ed), 400);
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, l400, l400 + 2));
    paint(&a, &dl);
    gates_color_t sel_fg = gates_theme_color(theme, GATES_COLOR_SELECTION_FG);
    const gates_draw_cmd_t *tc[64];
    text_zone = gates_node_layout_rect(a.t, a.ed);
    int k = texts(&dl, tc, 64);
    bool sel_kw = false;
    for (int i = 0; i < k; i++) {
        if (cmd_is(&dl, tc[i], "if") && tc[i]->color.r == sel_fg.r && tc[i]->color.g == sel_fg.g && tc[i]->color.b == sel_fg.b) {
            sel_kw = true;
        }
    }
    GT_ASSERT(sel_kw);
    /* Without a styler, program styles stay; set_style checks its range. */
    GT_ASSERT_OK(gates_editor_set_styler(a.t, a.ed, nullptr, nullptr));
    GT_ASSERT(gates_editor_set_style(a.t, a.ed, 5, 2, 1) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_set_style(a.t, a.ed, 0, (gates_u32)n + 1, 1) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_editor_set_style(a.t, a.ed, l400, l400 + 4, 1));
    GT_ASSERT(gates_text_buffer_style(gates_editor_buffer(a.t, a.ed), l400 + 3) == 1);
    paint(&a, &dl);
    GT_ASSERT(st.calls == 4);
    /* Wrong nodes. */
    gates_node_t root = gates_tree_root(a.t);
    GT_ASSERT(gates_editor_set_styles(a.t, root, styles, 1) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_set_styler(a.t, root, styler, &st) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_set_style(a.t, root, 0, 0, 1) == PROVEN_ERR_INVALID_ARG);
    gates_draw_list_deinit(&dl);
    done(&a);
}

static void test_marks_find(void) {
    app_t a;
    make(&a, &(gates_editor_desc_t){0});
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("alpha Beta gamma beta\nbeta")));
    gates_mark_id_t mk = 0;
    GT_ASSERT_OK(gates_editor_mark_add(a.t, a.ed, 11, GATES_MARK_LEFT, &mk));
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 0, 0));
    type(a.t, ">>");
    gates_u32 off = 0;
    GT_ASSERT(gates_text_buffer_mark_offset(gates_editor_buffer(a.t, a.ed), mk, &off) && off == 13);
    GT_ASSERT_OK(gates_editor_mark_remove(a.t, a.ed, mk));
    GT_ASSERT(gates_editor_mark_remove(a.t, a.ed, mk) == PROVEN_ERR_NOT_FOUND);
    GT_ASSERT(gates_editor_mark_add(a.t, gates_tree_root(a.t), 0, GATES_MARK_LEFT, &mk) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_editor_mark_remove(a.t, gates_tree_root(a.t), 1) == PROVEN_ERR_INVALID_ARG);
    /* Find: after the selection, selecting the hit; case; wrapping; backward. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 0, 0));
    GT_ASSERT(gates_editor_find(a.t, a.ed, GATES_STR("beta"), 0, false));
    GT_ASSERT(anchor(&a) == 19 && caret(&a) == 23);
    GT_ASSERT(gates_editor_find(a.t, a.ed, GATES_STR("beta"), 0, false) && anchor(&a) == 24);
    GT_ASSERT(!gates_editor_find(a.t, a.ed, GATES_STR("beta"), 0, false) && anchor(&a) == 24); /* unchanged */
    GT_ASSERT(gates_editor_find(a.t, a.ed, GATES_STR("beta"), GATES_FIND_IGNORE_CASE, true) && anchor(&a) == 8);
    GT_ASSERT(gates_editor_find(a.t, a.ed, GATES_STR("BETA"), GATES_FIND_BACKWARD | GATES_FIND_IGNORE_CASE, true));
    GT_ASSERT(anchor(&a) == 24); /* from the start backward: wrapped to the end */
    GT_ASSERT(!gates_editor_find(a.t, a.ed, GATES_STR("zeta"), 0, true));
    GT_ASSERT(!gates_editor_find(a.t, a.ed, GATES_STR(""), 0, true));
    GT_ASSERT(!gates_editor_find(a.t, gates_tree_root(a.t), GATES_STR("beta"), 0, true));
    done(&a);
}

static void test_indenting(void) {
    app_t a;
    make(&a, &(gates_editor_desc_t){ .auto_indent = true, .tab_inserts = true, .tab_width = 4 });
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("    code")));
    GT_ASSERT(key_mods(a.t, GATES_KEY_END, true, false));
    GT_ASSERT(key(a.t, GATES_KEY_ENTER));
    type(a.t, "more");
    GT_ASSERT(text_is(&a, "    code\n    more"));
    /* Enter in the leading blanks repeats only those before the caret. */
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("\t\tx")));
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 1, 1));
    GT_ASSERT(key(a.t, GATES_KEY_ENTER) && text_is(&a, "\t\n\t\tx"));
    /* Tab over lines indents each non-empty one; one undo step; the lines stay selected. */
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("one\n\ntwo\nthree\nfour")));
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 1, 11)); /* "one" .. into "three" */
    GT_ASSERT(key(a.t, GATES_KEY_TAB));
    GT_ASSERT(text_is(&a, "\tone\n\n\ttwo\n\tthree\nfour"));
    GT_ASSERT(anchor(&a) == 0 && caret(&a) == 17);
    GT_ASSERT(key_mods(a.t, GATES_KEY_TAB, false, true)); /* Shift+Tab takes them back */
    GT_ASSERT(text_is(&a, "one\n\ntwo\nthree\nfour"));
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false) && text_is(&a, "\tone\n\n\ttwo\n\tthree\nfour"));
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false) && text_is(&a, "one\n\ntwo\nthree\nfour"));
    /* A selection ending at a line's start leaves that line alone. */
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 0, 5)); /* "one\n\n" ends at line 2's start */
    GT_ASSERT(key(a.t, GATES_KEY_TAB) && text_is(&a, "\tone\n\ntwo\nthree\nfour"));
    /* Shift+Tab on one line: a tab, or up to tab-width spaces; the caret stays in the text. */
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("      six")));
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 8, 8));
    GT_ASSERT(key_mods(a.t, GATES_KEY_TAB, false, true) && text_is(&a, "  six") && caret(&a) == 4);
    GT_ASSERT(key_mods(a.t, GATES_KEY_TAB, false, true) && text_is(&a, "six") && caret(&a) == 2);
    bool had = gates_editor_can_undo(a.t, a.ed);
    GT_ASSERT(key_mods(a.t, GATES_KEY_TAB, false, true) && text_is(&a, "six")); /* nothing to take: no step */
    GT_ASSERT(had && gates_editor_can_undo(a.t, a.ed));
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false) && text_is(&a, "  six"));
    /* A "\r\n" text keeps its line ends when indented. */
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("a\r\nb\r\n")));
    GT_ASSERT(key_mods(a.t, GATES_KEY_A, true, false));
    GT_ASSERT(key(a.t, GATES_KEY_TAB) && text_is(&a, "\ta\r\n\tb\r\n"));
    done(&a);
}

/* -- stage 4: input methods and accessibility by line ------------------------------------------- */

typedef struct pre_rec_t {
    int n;
    char last[32];
} pre_rec_t;

static void on_pre(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    pre_rec_t *r = user;
    if (ev->kind != GATES_EVENT_PREEDIT_CHANGED) return;
    r->n++;
    snprintf(r->last, sizeof r->last, "%.*s", (int)ev->text.size, (const char *)ev->text.ptr);
}

static void test_ime(void) {
    app_t a;
    make(&a, &(gates_editor_desc_t){0});
    pre_rec_t pr = {0};
    GT_ASSERT_OK(gates_widget_set_handler(a.t, a.ed, on_pre, &pr));
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("ab\ncd")));
    GT_ASSERT_OK(gates_editor_set_selection(a.t, a.ed, 4, 4)); /* between c and d */
    GT_ASSERT(!gates_input_composing(a.t));
    GT_ASSERT(gates_input_preedit(a.t, GATES_STR("\xEA\xB0\x80\xEB\x82\x98"), 4) == GATES_INPUT_CONSUMED); /* cursor snaps to 3 */
    GT_ASSERT(gates_input_composing(a.t) && text_is(&a, "ab\ncd"));
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(pr.n == 1 && strcmp(pr.last, "\xEA\xB0\x80\xEB\x82\x98") == 0);
    /* Drawn at the caret, underlined, the rest of the row after it; the caret rect inside it. */
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    paint(&a, &dl);
    gates_point_t o = origin(&a);
    const gates_draw_cmd_t *pc = text_cmd(&dl, "\xEA\xB0\x80\xEB\x82\x98"), *d = text_cmd(&dl, "d");
    GT_ASSERT(pc != nullptr && pc->rect.x == o.x + 8 && pc->rect.y == o.y + 16 && pc->rect.w == 32);
    GT_ASSERT(d != nullptr && d->rect.x == o.x + 8 + 32);
    bool underline = false;
    for (gates_u32 i = 0; i < dl.len; i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(&dl, i);
        if (c->kind == GATES_DRAW_RECT && c->rect.x == o.x + 8 && c->rect.w == 32 && c->rect.h == 1 && c->rect.y == o.y + 30) underline = true;
    }
    GT_ASSERT(underline);
    gates_rect_t cr;
    GT_ASSERT(gates_input_caret_rect(a.t, &cr) && cr.x == o.x + 8 + 16 && cr.y == o.y + 16);
    /* The result replaces the selection as one step; the composition ends. */
    GT_ASSERT(gates_input_commit(a.t, GATES_STR("\xEA\xB0\x80")) == GATES_INPUT_CONSUMED);
    GT_ASSERT(!gates_input_composing(a.t) && text_is(&a, "ab\nc\xEA\xB0\x80" "d") && caret(&a) == 7);
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(pr.n == 2 && pr.last[0] == 0);
    GT_ASSERT(key_mods(a.t, GATES_KEY_Z, true, false) && text_is(&a, "ab\ncd"));
    /* An empty result only ends it; a cancel drops it; nothing composing: ignored. */
    GT_ASSERT(gates_input_preedit(a.t, GATES_STR("x"), 1) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_input_commit(a.t, GATES_STR("")) == GATES_INPUT_CONSUMED && !gates_input_composing(a.t) && text_is(&a, "ab\ncd"));
    GT_ASSERT(gates_input_preedit(a.t, GATES_STR("y"), 1) == GATES_INPUT_CONSUMED);
    GT_ASSERT(gates_input_preedit_cancel(a.t) == GATES_INPUT_CONSUMED && !gates_input_composing(a.t));
    GT_ASSERT(gates_input_preedit_cancel(a.t) == GATES_INPUT_IGNORED);
    GT_ASSERT(gates_input_preedit(a.t, GATES_STR(""), 0) == GATES_INPUT_CONSUMED && !gates_input_composing(a.t));
    /* Focus leaving drops a composition. */
    GT_ASSERT(gates_input_preedit(a.t, GATES_STR("z"), 1) == GATES_INPUT_CONSUMED);
    int before = pr.n;
    (void)gates_tree_dispatch_events(a.t, 0);
    gates_tree_set_focus(a.t, a.after);
    gates_tree_set_focus(a.t, a.ed);
    GT_ASSERT(!gates_input_composing(a.t) && text_is(&a, "ab\ncd"));
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(pr.n == before + 2);
    /* Read-only: no composition. */
    GT_ASSERT_OK(gates_editor_set_read_only(a.t, a.ed, true));
    GT_ASSERT(gates_input_preedit(a.t, GATES_STR("q"), 1) == GATES_INPUT_IGNORED);
    GT_ASSERT(gates_input_commit(a.t, GATES_STR("q")) == GATES_INPUT_IGNORED && text_is(&a, "ab\ncd"));
    gates_draw_list_deinit(&dl);
    done(&a);
}

static void test_access_lines(void) {
    app_t a;
    make(&a, &(gates_editor_desc_t){0});
    GT_ASSERT_OK(gates_node_set_access_name(a.t, a.ed, GATES_STR("Notes")));
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, GATES_STR("one\ntwo two\nthree")));
    gates_point_t o = origin(&a);
    /* A range over three lines: one rectangle per row. */
    gates_rect_t r[8];
    GT_ASSERT(gates_access_text_rects(a.t, a.ed, 1, 14, r, 8) == 3);
    GT_ASSERT(r[0].x == o.x + 8 && r[0].y == o.y && r[0].w == 16);
    GT_ASSERT(r[1].x == o.x && r[1].y == o.y + 16 && r[1].w == 56);
    GT_ASSERT(r[2].x == o.x && r[2].y == o.y + 32 && r[2].w == 16);
    GT_ASSERT(gates_access_text_rects(a.t, a.ed, 1, 14, r, 2) == 2); /* at most cap */
    gates_rect_t one;
    GT_ASSERT(gates_access_text_rect(a.t, a.ed, 5, 7, &one) && one.x == o.x + 8 && one.w == 16 && one.y == o.y + 16);
    /* An empty range: a zero-width rectangle at its place. */
    GT_ASSERT(gates_access_text_rects(a.t, a.ed, 4, 4, r, 8) == 1 && r[0].w == 0 && r[0].x == o.x && r[0].y == o.y + 16);
    /* The offset under a point; a selection through the model; the value set as a person would. */
    GT_ASSERT(gates_access_text_offset_at(a.t, a.ed, (gates_point_t){ o.x + 17, o.y + 20 }) == 6);
    GT_ASSERT_OK(gates_access_select_text(a.t, a.ed, 4, 7));
    GT_ASSERT(anchor(&a) == 4 && caret(&a) == 7);
    GT_ASSERT(gates_access_select_text(a.t, a.ed, 0, 99) == PROVEN_ERR_OUT_OF_BOUNDS);
    GT_ASSERT_OK(gates_access_set_value(a.t, a.ed, GATES_STR("new\ntext")));
    GT_ASSERT(text_is(&a, "new\ntext") && gates_editor_can_undo(a.t, a.ed));
    (void)gates_tree_dispatch_events(a.t, 0);
    GT_ASSERT(a.rec.text >= 1);
    GT_ASSERT(gates_access_set_value(a.t, a.ed, (gates_str_t){ (const gates_u8 *)"\xFF", 1 }) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_editor_set_read_only(a.t, a.ed, true));
    GT_ASSERT(gates_access_set_value(a.t, a.ed, GATES_STR("no")) == PROVEN_ERR_PERMISSION);
    GT_ASSERT_OK(gates_access_select_text(a.t, a.ed, 0, 3)); /* selection works read-only */
    /* Scrolled away: rows not shown give nothing. */
    static char big[20000];
    int n = 0;
    for (int i = 0; i < 300; i++) n += snprintf(big + n, sizeof big - (size_t)n, "line %d\n", i);
    GT_ASSERT_OK(gates_editor_set_read_only(a.t, a.ed, false));
    GT_ASSERT_OK(gates_editor_set_text(a.t, a.ed, (gates_str_t){ (const gates_u8 *)big, (gates_usize_t)n }));
    layout(a.t);
    GT_ASSERT(gates_access_text_rects(a.t, a.ed, 3000, 3005, r, 8) == 0);
    GT_ASSERT(gates_access_text_rects(a.t, a.ed, 0, 5, nullptr, 8) == 0 && gates_access_text_rects(a.t, a.ed, 0, 5, r, 0) == 0);
    done(&a);
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
    test_wrap();
    test_gutter();
    test_highlighting();
    test_marks_find();
    test_indenting();
    test_ime();
    test_access_lines();
    return gt_report("test_editor");
}
