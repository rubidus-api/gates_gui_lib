/* gates_gui_lib - text metrics contract and backend seam (0.2.0).
 *
 * The core depends only on this contract. Per font a backend reports ascent,
 * descent, line height, an average character width (a sizing hint), and the
 * advance of every code point; a string's width is exactly the sum of its code
 * points' advances (no kerning, ligatures or shaping), so the core computes
 * caret positions, selections and hit tests itself. Rendering happens freely
 * inside the rect the core assigns, with code point k at the sum of the
 * advances before it. Proportional and fixed-pitch faces both satisfy it.
 * Platform-free header. */
#ifndef GATES_TEXT_H
#define GATES_TEXT_H

#include <gates/render.h>

/* A font: which face a text uses (0.2.0). Sizes are a theme matter, not
 * chosen here. GATES_FONT_UI is the platform's UI face (proportional, the
 * default); GATES_FONT_MONO a fixed-pitch face for code, logs, aligned columns. */
typedef gates_i32 gates_font_t;
#define GATES_FONT_UI      0
#define GATES_FONT_MONO    1
#define GATES_FONT_INHERIT (-1)    /* gates_node_set_font: take the parent's */

typedef struct gates_text_metrics_t {
    gates_i32 advance;      /* average character width: a sizing hint (text box cols, steps) */
    gates_i32 ascent;       /* px above the baseline */
    gates_i32 descent;      /* px below the baseline */
    gates_i32 line_height;  /* >= ascent + descent */
} gates_text_metrics_t;

struct gates_text_backend_t {
    void *ctx;
    gates_text_metrics_t (*metrics)(void *ctx, gates_font_t font);
    /* Single-line measure: {sum of glyph_advance over the code points, line_height}. */
    gates_size_t (*measure)(void *ctx, gates_font_t font, gates_str_t text);
    /* Rasterize text with its top-left at rect origin, clipped to rect, clip
     * and target; code point k starts at the sum of the advances before it.
     * Rendering style is backend-local. */
    void (*draw)(void *ctx, gates_pixels_t target, gates_rect_t rect, gates_rect_t clip,
                 gates_font_t font, gates_str_t text, gates_color_t color);
    /* Optional: draw at `dpi`. Metrics stay logical (96 dpi); rect
     * and clip are device pixels; code point k starts at rect.x + gates_px(its
     * logical offset), so text scales with the layout and stays in its rect. */
    void (*draw_scaled)(void *ctx, gates_pixels_t target, gates_rect_t rect, gates_rect_t clip,
                        gates_font_t font, gates_str_t text, gates_color_t color,
                        gates_u32 dpi);
    /* Required (0.2.0): the advance of one code point in logical units, >= 0.
     * Stable for the process lifetime. U+FFFD stands for malformed bytes. */
    gates_i32 (*glyph_advance)(void *ctx, gates_font_t font, gates_u32 codepoint);
};

/* -- widths (src/text/gates_text_width.c, 0.2.0) ---------------------------- */

/* The width of a single-line string: the sum of its code points' advances
 * (0 for a null backend). */
gates_i32 gates_text_width(const gates_text_backend_t *backend, gates_font_t font, gates_str_t text);
/* The byte offset of the code-point boundary nearest to x (x measured from the
 * start of the text; before the start -> 0, past the end -> text.size). */
gates_u32 gates_text_offset_at_x(const gates_text_backend_t *backend, gates_font_t font,
                                 gates_str_t text, gates_i32 x);

/* -- shared UTF-8 and cell rules (src/text/gates_text_utf8.c) --------------
 * Decoding is shared by every backend and the edit core. Cells (1 narrow, 2
 * wide) are a helper for fixed-pitch backends; layout no longer uses them. */

/* Decodes the codepoint starting at `at`; returns bytes consumed (>= 1).
 * Malformed input consumes one byte and yields U+FFFD. */
gates_u32 gates_text_decode(gates_str_t text, gates_u32 at, gates_u32 *out_cp);

/* Display cells for one codepoint: 2 for Hangul/CJK/fullwidth, else 1. */
gates_u32 gates_text_cell_width(gates_u32 codepoint);

/* Cells occupied by a whole string. */
gates_u32 gates_text_cells(gates_str_t text);

/* Byte offset of the codepoint boundary before/after `at` (clamped). */
gates_u32 gates_text_prev_offset(gates_str_t text, gates_u32 at);
gates_u32 gates_text_next_offset(gates_str_t text, gates_u32 at);

/* The builtin reference backend: embedded 8x16 monospace
 * bitmap font (vendored public-domain font8x8, rows doubled). Deterministic
 * on every platform; ASCII glyphs, replacement box otherwise; the same face
 * for every font (advances by the cell rule: 8 per narrow, 16 per wide code
 * point). The explicit opt-in for pixel-exact output. */
const gates_text_backend_t *gates_text_backend_builtin(void);

/* The Win32 GDI backend: real system glyphs including Hangul. GATES_FONT_UI is
 * the system message font (proportional), GATES_FONT_MONO a fixed-pitch face.
 * Only available in builds that include src/platform/win32; it satisfies the
 * contract by placing every glyph at its logical offset itself. */
const gates_text_backend_t *gates_text_backend_win32_gdi(void);

#endif /* GATES_TEXT_H */
