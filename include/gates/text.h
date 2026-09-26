/* gates_gui_lib — text metrics contract and backend seam (RFC-0002).
 *
 * The core depends only on this contract: per font size a backend reports
 * cell advance / ascent / descent / line height and measures single-line
 * UTF-8 strings; rendering happens freely inside the rect the core assigns.
 * Monospace only in v0.x; wide glyphs (Hangul, CJK, ...) occupy 2 cells and
 * cell counts must be identical across backends. Platform-free header. */
#ifndef GATES_TEXT_H
#define GATES_TEXT_H

#include <gates/render.h>

typedef struct gates_text_metrics_t {
    gates_i32 advance;      /* width of one monospace cell, px */
    gates_i32 ascent;       /* px above the baseline */
    gates_i32 descent;      /* px below the baseline */
    gates_i32 line_height;  /* >= ascent + descent */
} gates_text_metrics_t;

struct gates_text_backend_t {
    void *ctx;
    gates_text_metrics_t (*metrics)(void *ctx, gates_i32 font_size);
    /* Single-line measure; must equal {cells(text)*advance, line_height}. */
    gates_size_t (*measure)(void *ctx, gates_i32 font_size, gates_str_t text);
    /* Rasterize text with its top-left at rect origin, clipped to
     * rect ∩ clip ∩ target. Rendering style is backend-local (RFC-0002 §1). */
    void (*draw)(void *ctx, gates_pixels_t target, gates_rect_t rect, gates_rect_t clip,
                 gates_i32 font_size, gates_str_t text, gates_color_t color);
    /* Optional (plan-0013): draw at `dpi`. Metrics stay logical (96 dpi); rect
     * and clip are device pixels; cell k starts at rect.x + gates_px(k * advance)
     * relative to the logical origin, so the cell grid scales with the layout. */
    void (*draw_scaled)(void *ctx, gates_pixels_t target, gates_rect_t rect, gates_rect_t clip,
                        gates_i32 font_size, gates_str_t text, gates_color_t color,
                        gates_u32 dpi);
};

/* -- shared UTF-8 and cell rules (src/text/gates_text_utf8.c) --------------
 * Every backend and the edit core use these, so cell counts agree by
 * construction (RFC-0002 §3 parity rule). */

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

/* The builtin reference backend (RFC-0002 §5): embedded 8x16 monospace
 * bitmap font (vendored public-domain font8x8, rows doubled). Deterministic
 * on every platform; ASCII glyphs, replacement box otherwise; ignores
 * font_size (one built-in size). The only backend tests use, and the
 * explicit opt-in for pixel-exact output (RFC-0002 §6). */
const gates_text_backend_t *gates_text_backend_builtin(void);

/* The Win32 GDI backend (RFC-0002 §5): real system glyphs including Hangul.
 * Only available in builds that include src/platform/win32; it satisfies the
 * same §3 contract by placing every cell itself. */
const gates_text_backend_t *gates_text_backend_win32_gdi(void);

#endif /* GATES_TEXT_H */
