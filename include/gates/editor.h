/* gates_gui_lib - multi-line text editor (RFC-0006, plan-0022).
 *
 * One node over a text buffer (gates/text_buffer.h): it lays out and paints
 * only the lines it shows, so a long text costs what the view costs. Tabs
 * expand to the next multiple of the tab width (in space advances); widths
 * come from the text backend, so proportional and monospace fonts both work.
 *
 * Keys: arrows (Ctrl: by word), Up/Down keep the column the caret came from,
 * Home (first non-blank, then the line start) and End, Ctrl+Home/End,
 * PageUp/PageDown, Shift extends every move; Backspace/Delete (Ctrl: a word),
 * Enter (the line's own line ending), Tab when tab_inserts (otherwise Tab
 * moves focus, so the keyboard is never trapped), Ctrl+A/C/X/V/Z/Y. Pointer:
 * a press places the caret (Shift extends), a drag selects, a double click
 * selects a word, the wheel scrolls (Shift: sideways), the scrollbars work
 * as everywhere.
 *
 * Options: soft wrap (rows break after the last blank that fits, else at the
 * last code point; with wrap, Up/Down move by rows and the scrollbar counts
 * lines), a line number gutter, auto-indent (Enter repeats the line's
 * leading blanks). When tab_inserts, Tab and Shift+Tab indent and unindent
 * the selected lines (or type a tab), and Ctrl+Tab / Ctrl+Shift+Tab move
 * focus, so the keyboard is still never trapped.
 *
 * Highlighting: a style table maps style bytes (gates/text_buffer.h) to
 * colours. A styler callback, when set, is asked before painting to style the
 * text from the first line whose styles may be stale up to the last line
 * shown ("from" is always a line start); it sets styles with
 * gates_text_buffer_set_style on the buffer it is given and must not change
 * the text. An edit makes its line stale again. Selected text is drawn in
 * the selection colour.
 *
 * A person's edit is undoable (typing and deleting runs merge), reports
 * GATES_EVENT_TEXT_CHANGED (ev->text is empty: read what you need), and moves
 * the caret; caret and selection moves report GATES_EVENT_SELECTION_CHANGED
 * (ev->item = the caret offset). Program changes are silent. Offsets are UTF-8
 * bytes; the editor keeps the caret and edits on code point boundaries and
 * refuses text that is not UTF-8. Platform-free. */
#ifndef GATES_EDITOR_H
#define GATES_EDITOR_H

#include <gates/tree.h>
#include <gates/theme.h>
#include <gates/text_buffer.h>

typedef struct gates_editor_desc_t {
    bool tab_inserts;            /* Tab types a tab (Ctrl+Tab still leaves when not in tabs) */
    gates_u32 tab_width;         /* in space advances; 0 -> 4 */
    bool read_only;              /* selectable and copyable, not editable */
    gates_u32 max_bytes;         /* 0 -> GATES_TEXT_BUFFER_MAX; typing past it is refused */
    gates_u32 rows;              /* preferred height in lines; 0 -> 10 */
    gates_u32 cols;              /* preferred width in average characters; 0 -> 40 */
    bool wrap;                   /* soft wrap at the view's width (no sideways scrolling) */
    bool line_numbers;           /* a gutter with line numbers */
    bool auto_indent;            /* Enter repeats the line's leading blanks */
} gates_editor_desc_t;

[[nodiscard]] gates_err_t gates_editor_create(gates_tree_t *tree, gates_node_t parent,
                                              const gates_editor_desc_t *desc, gates_node_t *out_editor);

/* The text, to read (lines, spans, find). Change it through the editor. */
const gates_text_buffer_t *gates_editor_buffer(const gates_tree_t *tree, gates_node_t editor);
gates_u32 gates_editor_length(const gates_tree_t *tree, gates_node_t editor);

/* Replaces all text: caret at 0, scrolled to the top, history cleared and
 * the text counted as unmodified. INVALID_ARG for text that is not UTF-8. */
[[nodiscard]] gates_err_t gates_editor_set_text(gates_tree_t *tree, gates_node_t editor, gates_str_t text);
/* Replaces [begin, end) (code point boundaries); `undoable` records it in the
 * history as one step. The caret keeps its place in the text. Silent. */
[[nodiscard]] gates_err_t gates_editor_replace(gates_tree_t *tree, gates_node_t editor, gates_u32 begin,
                                               gates_u32 end, gates_str_t text, bool undoable);

/* The selection is [min(anchor, caret), max(...)); both are clamped to code
 * point boundaries. Setting it scrolls the caret into view; silent. */
void gates_editor_selection(const gates_tree_t *tree, gates_node_t editor, gates_u32 *anchor, gates_u32 *caret);
[[nodiscard]] gates_err_t gates_editor_set_selection(gates_tree_t *tree, gates_node_t editor, gates_u32 anchor,
                                                     gates_u32 caret);
/* Scrolls so the offset's line is shown (and its column, without wrap). */
[[nodiscard]] gates_err_t gates_editor_scroll_to(gates_tree_t *tree, gates_node_t editor, gates_u32 offset);
/* The first line shown, and how many whole lines fit (valid after layout). */
gates_u32 gates_editor_first_line(const gates_tree_t *tree, gates_node_t editor);
gates_u32 gates_editor_visible_lines(const gates_tree_t *tree, gates_node_t editor);

/* History of a person's edits (and undoable program edits). */
[[nodiscard]] gates_err_t gates_editor_undo(gates_tree_t *tree, gates_node_t editor);
[[nodiscard]] gates_err_t gates_editor_redo(gates_tree_t *tree, gates_node_t editor);
bool gates_editor_can_undo(const gates_tree_t *tree, gates_node_t editor);
bool gates_editor_can_redo(const gates_tree_t *tree, gates_node_t editor);
/* Modified: the text differs from the last set_text or set_unmodified (undo
 * back to it counts as unmodified again). */
bool gates_editor_modified(const gates_tree_t *tree, gates_node_t editor);
void gates_editor_set_unmodified(gates_tree_t *tree, gates_node_t editor);

/* Highlighting. Style byte i draws with styles[i] (i < count; style 0 and
 * bytes past the table draw as plain text). Copied; count at most 256. */
typedef struct gates_editor_style_t {
    gates_color_token_t token;   /* the colour from the theme ... */
    bool use_rgb;                /* ... or this one */
    gates_color_t rgb;
} gates_editor_style_t;
[[nodiscard]] gates_err_t gates_editor_set_styles(gates_tree_t *tree, gates_node_t editor,
                                                  const gates_editor_style_t *styles, gates_u32 count);
typedef void (*gates_editor_styler_fn)(void *user, gates_text_buffer_t *buffer, gates_u32 from, gates_u32 to);
/* Null removes it. Setting one makes every line stale. */
[[nodiscard]] gates_err_t gates_editor_set_styler(gates_tree_t *tree, gates_node_t editor, gates_editor_styler_fn fn,
                                                  void *user);
/* Styles a range from the program (a search hit, a diagnostic); a styler may restyle it. */
[[nodiscard]] gates_err_t gates_editor_set_style(gates_tree_t *tree, gates_node_t editor, gates_u32 begin,
                                                 gates_u32 end, gates_u8 style);

/* Marks that move with edits (gates/text_buffer.h); read them through the buffer. */
[[nodiscard]] gates_err_t gates_editor_mark_add(gates_tree_t *tree, gates_node_t editor, gates_u32 offset,
                                                gates_mark_gravity_t gravity, gates_mark_id_t *out);
[[nodiscard]] gates_err_t gates_editor_mark_remove(gates_tree_t *tree, gates_node_t editor, gates_mark_id_t id);

/* Finds the needle after the selection (before it with GATES_FIND_BACKWARD),
 * starting over at the other end when wrap_around; selects it and scrolls to
 * it. false when there is none. Silent. */
bool gates_editor_find(gates_tree_t *tree, gates_node_t editor, gates_str_t needle, gates_u32 flags, bool wrap_around);

[[nodiscard]] gates_err_t gates_editor_set_read_only(gates_tree_t *tree, gates_node_t editor, bool read_only);
bool gates_editor_read_only(const gates_tree_t *tree, gates_node_t editor);

#endif /* GATES_EDITOR_H */
