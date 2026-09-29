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

## Unicode limits in 0.3.0

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
