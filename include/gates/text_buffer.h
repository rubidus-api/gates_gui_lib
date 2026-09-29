/* gates_gui_lib - text buffer (RFC-0006): UTF-8 text with lines, styles and
 * marks, for the multi-line editor and for programs handling large texts.
 *
 * Bytes live in a gap buffer: an edit near the last one costs little, and a
 * read never moves the gap - it returns the text as at most two spans (before
 * and after the gap). Only gates_text_buffer_contiguous moves it, when a
 * program asks for one span. Offsets are bytes; the buffer does not check
 * UTF-8 (the editor does, and edits only at code point boundaries).
 *
 * Lines end after each "\n" (a "\r" before it belongs to the line end; a lone
 * "\r" is text). There is always at least one line; a text ending in "\n" has
 * an empty last line. Line starts are kept in an index updated per edit with a
 * pending shift (Scintilla's partitioning), so an edit near the end of a long
 * text does not rewrite every start after it.
 *
 * Every byte has a style byte (0 = plain); inserted text takes style 0. Marks
 * are positions that move with edits: an insertion exactly at a LEFT mark
 * goes after it (the mark stays before the new text), at a RIGHT mark before
 * it (the mark moves past the new text); a deleted range containing a mark
 * moves it to where the range was. A replace is a delete then an insert.
 * Mark ids are never reused. Platform-free; not thread-safe. */
#ifndef GATES_TEXT_BUFFER_H
#define GATES_TEXT_BUFFER_H

#include <gates/types.h>

typedef struct gates_text_buffer gates_text_buffer_t;

#define GATES_TEXT_BUFFER_MAX ((gates_u32)1 << 30)   /* bytes */

/* The allocator: zero -> the proven heap. */
[[nodiscard]] gates_err_t gates_text_buffer_create(gates_allocator_t alloc, gates_text_buffer_t **out);
void gates_text_buffer_destroy(gates_text_buffer_t *b);

gates_u32 gates_text_buffer_length(const gates_text_buffer_t *b);
/* Replaces [begin, end) with text (copied). INVALID_ARG for a bad range,
 * OUT_OF_BOUNDS past GATES_TEXT_BUFFER_MAX, NOMEM; on failure nothing changes.
 * Read what a replace removes (for undo) before calling it. */
[[nodiscard]] gates_err_t gates_text_buffer_replace(gates_text_buffer_t *b, gates_u32 begin, gates_u32 end,
                                                    gates_str_t text);
[[nodiscard]] gates_err_t gates_text_buffer_set_text(gates_text_buffer_t *b, gates_str_t text);

/* Reading: [begin, end) (clamped) as two spans (the second empty unless the
 * range crosses the gap); a copy (returns the bytes copied, at most cap); one
 * byte (0 past the end); the whole text as one span (moves the gap to the end:
 * valid until the next edit). */
void gates_text_buffer_span(const gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_str_t *first,
                            gates_str_t *second);
gates_u32 gates_text_buffer_copy(const gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_u8 *out,
                                 gates_u32 cap);
gates_u8 gates_text_buffer_byte(const gates_text_buffer_t *b, gates_u32 offset);
gates_str_t gates_text_buffer_contiguous(gates_text_buffer_t *b);

/* Lines, from 0. line_start of a line past the last is the length; line_end is
 * where the line's text ends (before "\n" or "\r\n"); line_of is the line
 * containing an offset (the last line for the length and beyond). */
gates_u32 gates_text_buffer_line_count(const gates_text_buffer_t *b);
gates_u32 gates_text_buffer_line_start(const gates_text_buffer_t *b, gates_u32 line);
gates_u32 gates_text_buffer_line_end(const gates_text_buffer_t *b, gates_u32 line);
gates_u32 gates_text_buffer_line_of(const gates_text_buffer_t *b, gates_u32 offset);

/* Styles: one byte per text byte. */
gates_u8 gates_text_buffer_style(const gates_text_buffer_t *b, gates_u32 offset);
void gates_text_buffer_set_style(gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_u8 style);
/* The styles of [begin, end) as two spans, like gates_text_buffer_span. */
void gates_text_buffer_style_span(const gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_str_t *first,
                                  gates_str_t *second);

/* Marks. */
typedef gates_u32 gates_mark_id_t;
typedef enum gates_mark_gravity_t {
    GATES_MARK_LEFT = 0,
    GATES_MARK_RIGHT,
} gates_mark_gravity_t;
[[nodiscard]] gates_err_t gates_text_buffer_mark_add(gates_text_buffer_t *b, gates_u32 offset,
                                                     gates_mark_gravity_t gravity, gates_mark_id_t *out);
/* NOT_FOUND for an unknown id. */
[[nodiscard]] gates_err_t gates_text_buffer_mark_remove(gates_text_buffer_t *b, gates_mark_id_t id);
[[nodiscard]] gates_err_t gates_text_buffer_mark_set(gates_text_buffer_t *b, gates_mark_id_t id, gates_u32 offset);
/* The mark's offset, or false for an unknown id. */
bool gates_text_buffer_mark_offset(const gates_text_buffer_t *b, gates_mark_id_t id, gates_u32 *offset);

/* Find the needle from `from`: forward, the first match starting at or after
 * it; backward, the last match ending at or before it. Case-insensitive
 * compares ASCII letters only. */
typedef enum gates_find_flags_t {
    GATES_FIND_BACKWARD = 1u << 0,
    GATES_FIND_IGNORE_CASE = 1u << 1,
} gates_find_flags_t;
bool gates_text_buffer_find(const gates_text_buffer_t *b, gates_u32 from, gates_str_t needle, gates_u32 flags,
                            gates_u32 *match_begin);

#endif /* GATES_TEXT_BUFFER_H */
