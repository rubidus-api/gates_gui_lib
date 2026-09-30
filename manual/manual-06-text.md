# Chapter 6 - Text entry

Headers: `gates/widget.h` (text box), `gates/text_edit.h`, `gates/clipboard.h`, `gates/input.h`,
`gates/ui.h`.

## The text box

A text box edits one line of UTF-8 text. The person types, selects with Shift and the pointer
(Shift+click extends, a double click takes a word, a triple click all of it),
moves by character and to the start or end (Home, End), copies, cuts and pastes (Ctrl+C, Ctrl+X, Ctrl+V), and undoes and
redoes (Ctrl+Z, Ctrl+Y). Each successful edit queues TEXT_CHANGED with the committed text; the
program's own `gates_textbox_set_text` is silent and clears the undo history. Under every text
box is the edit core (`gates/text_edit.h`): text, caret, selection and an open composition,
all as byte offsets into UTF-8.

| Setting | Effect |
|---|---|
| `gates_textbox_set_read_only` | focus, selection and copy work; editing, paste, cut, undo and composition are refused |
| `gates_textbox_set_password` | one `*` per character, no copy or cut, events carry no text, no undo, no IME |
| `gates_textbox_set_max_bytes` | a maximum length in UTF-8 bytes (see below) |
| `gates_textbox_set_undo_limits` | undo history bounds; defaults 64 entries, 16384 bytes |
| `gates_textbox_set_invalid` | the error look (a form sets it for you) |

## The limit is a question

When input would pass the maximum length, gates inserts nothing and asks: a LIMIT_EXCEEDED
event carries the refused input and how many of its bytes would fit. The program puts the
question to the person - keep what fits, or drop it - and answers with
`gates_textbox_accept_fit` or `gates_textbox_discard_rejected`. Silently cutting a pasted
account number in half is the failure this avoids.

<!-- example: manual/examples/ex_07_text.c -->
```c
/* manual example (host): a text box with a limit - the overflow is a question.
 * expect: offered 7 bytes, 5 fit; kept "Seoul" */
#include <gates/gates.h>

#include <stdio.h>

static gates_u32 offered, fit;

static void on_box(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    (void)user;
    if (ev->kind == GATES_EVENT_LIMIT_EXCEEDED) {
        offered = (gates_u32)ev->text.size;
        fit = ev->fit_bytes;
    }
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t city;
    if (!gates_is_ok(gates_textbox_create(t, gates_tree_root(t), GATES_STR(""), 20, &city)) ||
        !gates_is_ok(gates_textbox_set_max_bytes(t, city, 5)) ||
        !gates_is_ok(gates_widget_set_handler(t, city, on_box, nullptr))) {
        return 1;
    }
    gates_tree_set_focus(t, city);
    /* Typed or pasted input (an IME delivers its result the same way). */
    (void)gates_input_commit(t, GATES_STR("Seoul!!"));
    (void)gates_tree_dispatch_events(t, 0);
    /* Nothing was inserted: the box holds the input as an offer. Here the
     * answer is "keep what fits"; gates_textbox_discard_rejected drops it. */
    if (!gates_is_ok(gates_textbox_accept_fit(t, city))) return 1;
    gates_str_t text = gates_textbox_text(t, city);
    printf("offered %u bytes, %u fit; kept \"%.*s\"\n", offered, fit, (int)text.size, (const char *)text.ptr);
    gates_tree_destroy(t);
    return 0;
}
```

## The installed input method

gates implements no input method. On Windows the installed IME (Korean, Japanese, Chinese, ...)
works in every text box through IMM32: the syllable being composed is shown inline at the
caret, the candidate window opens under the caret, and the finished text arrives as one edit.
A read-only or password box turns the IME off while it has focus. For a program that feeds
text itself - tests, automation - `gates_input_commit` delivers text as an IME would, and
`gates_input_preedit` shows a composition.

## Unicode limits in 0.8.0

- Text is UTF-8 everywhere; invalid input is refused, never repaired silently.
- Every character has its own advance, taken from the font: text is proportional in the UI
  font and fixed-pitch in the mono font (chapter 8). A string is exactly as wide as the sum of
  its characters, so the caret, a selection and a click always land between characters.
- There is no kerning, no ligatures and no shaping: scripts that need shaping (Arabic, the
  Indic scripts, Thai) show their characters one by one, and right-to-left text is not
  reordered.
- Editing moves by code point, not by grapheme cluster: a base letter and a combining mark are
  two steps.
- The Win32 backend draws real system glyphs, taking characters its face lacks (Hangul in
  Segoe UI) from the system's fallback fonts; the builtin backend (tests, headless) is
  fixed-pitch and draws ASCII, with a box of the right width for everything else.
- A text box is one line; many lines are the editor's (below).

## The clipboard

The window provides the platform clipboard (`gates/clipboard.h`): text crosses it as UTF-8 and
is converted at the edge. A tree without a clipboard provider (headless) makes copy, cut and
paste do nothing.

## Many lines: the editor

`gates_editor_create` (gates/editor.h) makes a multi-line editor over a text buffer. It paints only
the lines it shows, so a long file costs what the window costs. Arrows, Home/End, PageUp/PageDown
and Ctrl with them move the caret (Shift selects; Shift+click extends, a double click takes a
word, a triple click the line with its break), Enter keeps the text's own line ending, Tab
moves focus unless the description asks for tabs to be typed, and Ctrl+A/C/X/V/Z/Y work as
everywhere. A person's edits are undoable - a typing or deleting run is one step - and
`gates_editor_modified` tells whether the text differs from what was set or saved. The program
reads the text through `gates_editor_buffer` and changes it with `gates_editor_set_text` or
`gates_editor_replace`; TEXT_CHANGED carries no text, so a long file is never copied into an event.

<!-- example: manual/examples/ex_06_editor.c -->
```c
/* manual example (host): a multi-line editor - typing, lines, undo and the modified mark.
 * expect: 3 lines, caret on line 2; after undo: 2 lines, modified: no */
#include <gates/gates.h>

#include <stdio.h>

static void press(gates_tree_t *t, gates_key_t key, bool ctrl) {
    gates_key_event_t ev = { .key = key, .ctrl = ctrl, .down = true };
    (void)gates_input_key(t, &ev);
    ev.down = false;
    (void)gates_input_key(t, &ev);
}

static void type(gates_tree_t *t, const char *s) {
    for (; *s != 0; s++) (void)gates_input_char(t, (gates_u32)(unsigned char)*s);
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t notes;
    gates_editor_desc_t desc = { .rows = 8, .cols = 40 };
    if (!gates_is_ok(gates_editor_create(t, gates_tree_root(t), &desc, &notes)) ||
        !gates_is_ok(gates_editor_set_text(t, notes, GATES_STR("Shopping\nMilk"))) ||
        !gates_is_ok(gates_node_set_access_name(t, notes, GATES_STR("Notes"))) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 360, 200 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* A person goes to the end, starts a new line and types. */
    gates_tree_set_focus(t, notes);
    press(t, GATES_KEY_END, true);
    press(t, GATES_KEY_ENTER, false);
    type(t, "Bread");
    const gates_text_buffer_t *text = gates_editor_buffer(t, notes);
    gates_u32 caret = 0;
    gates_editor_selection(t, notes, nullptr, &caret);
    printf("%u lines, caret on line %u; ", gates_text_buffer_line_count(text), gates_text_buffer_line_of(text, caret));

    /* Ctrl+Z takes the typing back, then the new line: the text is as it was set. */
    press(t, GATES_KEY_Z, true);
    press(t, GATES_KEY_Z, true);
    printf("after undo: %u lines, modified: %s\n", gates_text_buffer_line_count(text),
           gates_editor_modified(t, notes) ? "yes" : "no");
    gates_tree_destroy(t);
    return 0;
}
```

The description turns on soft wrap (rows break after the last blank that fits; Up and Down then
move by rows, Home and End go to the row's start and end before the line's, and a caret at the
end of a row stays drawn there), a line number gutter and auto-indent. When Tab types tabs, Tab and Shift+Tab
indent and unindent the selected lines and Ctrl+Tab moves focus on, so the keyboard is never
trapped. Highlighting is the program's: `gates_editor_set_styles` maps style bytes to colours,
and a styler (`gates_editor_set_styler`) is asked, just before painting, to style the lines about
to show whose styles are stale - an edit makes its line stale again - so style work follows the
view, not the length of the text. `gates_editor_find` selects the next match; marks
(`gates_editor_mark_add`) keep a place while the text around it changes.
An input method composes at the caret, drawn underlined in the text; the result arrives as one
edit. Screen readers read the editor's text by character, word and line - a line as shown, a
wrapped row (`gates_access_text_line`); a paragraph is a text line - with one rectangle per row
for a range (`gates_access_text_rects`).

<!-- example: manual/examples/ex_06_highlight.c -->
```c
/* manual example (host): highlighting through a styler, and find.
 * expect: styled up to line 11 of 200; "TODO" found on line 150 */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

enum { PLAIN, NUMBER, KEYWORD };

/* The program's highlighter: numbers and the word "TODO". It only styles the
 * range it is given - the editor asks for what is about to show. */
static gates_u32 styled_to;
static void highlight(void *user, gates_text_buffer_t *text, gates_u32 from, gates_u32 to) {
    (void)user;
    gates_text_buffer_set_style(text, from, to, PLAIN);
    for (gates_u32 i = from; i < to; i++) {
        gates_u8 c = gates_text_buffer_byte(text, i);
        if (c >= '0' && c <= '9') gates_text_buffer_set_style(text, i, i + 1, NUMBER);
    }
    gates_u32 at = from;
    while (gates_text_buffer_find(text, at, GATES_STR("TODO"), 0, &at) && at + 4 <= to) {
        gates_text_buffer_set_style(text, at, at + 4, KEYWORD);
        at += 4;
    }
    styled_to = to;
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    static char text[8000];
    int n = 0;
    for (int i = 1; i <= 200; i++) n += snprintf(text + n, sizeof text - (size_t)n, i == 150 ? "TODO %d\n" : "step %d\n", i);
    gates_node_t ed;
    gates_editor_desc_t desc = { .rows = 10, .line_numbers = true, .wrap = true };
    const gates_editor_style_t styles[] = { {0}, { .token = GATES_COLOR_FOCUS_RING }, { .token = GATES_COLOR_ERROR } };
    if (!gates_is_ok(gates_editor_create(t, gates_tree_root(t), &desc, &ed)) ||
        !gates_is_ok(gates_editor_set_text(t, ed, (gates_str_t){ (const gates_u8 *)text, (gates_usize_t)n })) ||
        !gates_is_ok(gates_editor_set_styles(t, ed, styles, 3)) ||
        !gates_is_ok(gates_editor_set_styler(t, ed, highlight, nullptr)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 320, 200 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* Painting styles only the lines shown (and the next one). */
    gates_draw_list_t dl;
    if (!gates_is_ok(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0)) ||
        !gates_is_ok(gates_paint_tree(t, &dl, gates_theme_light(), gates_text_backend_builtin()))) {
        return 1;
    }
    const gates_text_buffer_t *buf = gates_editor_buffer(t, ed);
    printf("styled up to line %u of %u; ", gates_text_buffer_line_of(buf, styled_to), gates_text_buffer_line_count(buf) - 1);

    /* Find selects the match and scrolls to it. */
    if (gates_editor_find(t, ed, GATES_STR("todo"), GATES_FIND_IGNORE_CASE, true)) {
        gates_u32 caret = 0;
        gates_editor_selection(t, ed, nullptr, &caret);
        printf("\"TODO\" found on line %u\n", gates_text_buffer_line_of(buf, caret) + 1);
    }
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
    return 0;
}
```

## Large texts

`gates/text_buffer.h` holds UTF-8 text of any length a tool edits (up to 1 GiB): a gap buffer
with an index of line starts, a style byte per byte, marks that move with edits, and search.
Reading never moves the gap - a range comes back as at most two spans - so a program can read
the lines it shows while editing elsewhere. The multi-line editor is built on it; a program can
also use it alone to load, search and change a large text.
