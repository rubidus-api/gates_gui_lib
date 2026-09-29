# Chapter 6 - Text entry

Headers: `gates/widget.h` (text box), `gates/text_edit.h`, `gates/clipboard.h`, `gates/input.h`,
`gates/ui.h`.

## The text box

A text box edits one line of UTF-8 text. The person types, selects with Shift and the pointer,
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

## Unicode limits in 0.6.0

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
- One line per text box; there is no multi-line editor yet.

## The clipboard

The window provides the platform clipboard (`gates/clipboard.h`): text crosses it as UTF-8 and
is converted at the edge. A tree without a clipboard provider (headless) makes copy, cut and
paste do nothing.

## Many lines: the editor

`gates_editor_create` (gates/editor.h) makes a multi-line editor over a text buffer. It paints only
the lines it shows, so a long file costs what the window costs. Arrows, Home/End, PageUp/PageDown
and Ctrl with them move the caret (Shift selects), Enter keeps the text's own line ending, Tab
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

## Large texts

`gates/text_buffer.h` holds UTF-8 text of any length a tool edits (up to 1 GiB): a gap buffer
with an index of line starts, a style byte per byte, marks that move with edits, and search.
Reading never moves the gap - a range comes back as at most two spans - so a program can read
the lines it shows while editing elsewhere. The multi-line editor is built on it; a program can
also use it alone to load, search and change a large text.
