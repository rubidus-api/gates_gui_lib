/* gates_gui_lib - UTF-8 text edit core.
 *
 * Owns a UTF-8 buffer, a caret, a selection and an uncommitted preedit range
 * (filled by an IME adapter). Platform-free and allocator-injected; the
 * textbox widget and any future editor build on it.
 *
 * Editing unit: one codepoint. Korean NFC syllables are single codepoints, so
 * caret motion and backspace are correct for Hangul. Grapheme clusters
 * (combining marks, emoji ZWJ sequences) are a documented v0.x deferral. */
#ifndef GATES_TEXT_EDIT_H
#define GATES_TEXT_EDIT_H

#include <gates/text.h>

typedef struct gates_text_edit_t {
    gates_allocator_t alloc;
    gates_u8 *buf;          /* committed UTF-8, never null-terminated */
    gates_u32 len;
    gates_u32 cap;
    gates_u32 caret;        /* byte offset in buf */
    gates_u32 anchor;       /* selection is [min(anchor,caret), max(...)) */
    gates_u8 *preedit;      /* uncommitted composition text */
    gates_u32 preedit_len;
    gates_u32 preedit_cap;
    gates_u32 preedit_at;   /* byte offset where the preedit is displayed */
} gates_text_edit_t;

typedef enum gates_caret_move_t {
    GATES_CARET_LEFT,
    GATES_CARET_RIGHT,
    GATES_CARET_HOME,
    GATES_CARET_END,
} gates_caret_move_t;

/* `alloc` {0} = the heap; `initial` is copied, the caret after it. */
[[nodiscard]] gates_err_t gates_text_edit_init(gates_text_edit_t *ed, gates_allocator_t alloc,
                                               gates_str_t initial);
void gates_text_edit_deinit(gates_text_edit_t *ed);

/* The committed text (borrowed until the next change), the caret, and the
 * selection [begin, end) - empty when the anchor is at the caret. */
gates_str_t gates_text_edit_text(const gates_text_edit_t *ed);
gates_u32 gates_text_edit_caret(const gates_text_edit_t *ed);
bool gates_text_edit_has_selection(const gates_text_edit_t *ed);
gates_u32 gates_text_edit_sel_begin(const gates_text_edit_t *ed);
gates_u32 gates_text_edit_sel_end(const gates_text_edit_t *ed);

/* Replaces all text (copied); caret and anchor at the end. */
[[nodiscard]] gates_err_t gates_text_edit_set_text(gates_text_edit_t *ed, gates_str_t text);
/* Ensures room for `total_bytes` of committed text, so a following insert up
 * to that size cannot fail. Nothing else changes. */
[[nodiscard]] gates_err_t gates_text_edit_reserve(gates_text_edit_t *ed, gates_u32 total_bytes);
/* Replaces the selection (if any) with `text` and leaves the caret after it. */
[[nodiscard]] gates_err_t gates_text_edit_insert(gates_text_edit_t *ed, gates_str_t text);
/* Delete the selection, else one codepoint before/after the caret. */
[[nodiscard]] gates_err_t gates_text_edit_backspace(gates_text_edit_t *ed);
[[nodiscard]] gates_err_t gates_text_edit_delete(gates_text_edit_t *ed);

/* Left/Right by a code point, Home/End to the ends; `extend` keeps the
 * anchor. An unextended Left/Right with a selection collapses it to that edge. */
void gates_text_edit_move(gates_text_edit_t *ed, gates_caret_move_t how, bool extend);
void gates_text_edit_select_all(gates_text_edit_t *ed);
/* Clamped to a codepoint boundary. */
void gates_text_edit_set_caret(gates_text_edit_t *ed, gates_u32 byte_offset, bool extend);

/* Preedit (IME composition). The committed buffer is untouched;
 * the preedit is displayed at the selection end (the caret when nothing is
 * selected). A failed set keeps the previous preedit. */
[[nodiscard]] gates_err_t gates_text_edit_set_preedit(gates_text_edit_t *ed, gates_str_t text);
void gates_text_edit_clear_preedit(gates_text_edit_t *ed);
gates_str_t gates_text_edit_preedit(const gates_text_edit_t *ed);

/* Cell geometry for rendering and hit testing (the cell rules). */
gates_u32 gates_text_edit_cells_before(const gates_text_edit_t *ed, gates_u32 byte_offset);
/* Byte offset of the codepoint boundary at or before `cell`. */
gates_u32 gates_text_edit_offset_at_cell(const gates_text_edit_t *ed, gates_u32 cell);

#endif /* GATES_TEXT_EDIT_H */
