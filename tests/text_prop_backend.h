/* A deterministic proportional text backend for host tests (0.2.0).
 * GATES_FONT_UI: synthetic proportional widths - narrow letters 3, 'm'/'w' 12,
 * space 4, other ASCII 7, wide (Hangul, CJK, fullwidth) 16, combining marks 0.
 * GATES_FONT_MONO: the cell rule, 8 per cell. A named face (0.12.0): 10 per code
 * point, so tests see which face measured. A sized font (0.10.0) scales
 * every advance and the line. Draws nothing. */
#ifndef GATES_TEXT_PROP_BACKEND_H
#define GATES_TEXT_PROP_BACKEND_H

#include <gates/text.h>

static gates_i32 tp_advance_face(gates_font_t font, gates_u32 cp) {
    if (font == GATES_FONT_MONO) return (gates_i32)gates_text_cell_width(cp) * 8;
    if (gates_font_face_name(font).size > 0) return 10;
    if (cp >= 0x300 && cp <= 0x36F) return 0;
    if (gates_text_cell_width(cp) == 2) return 16;
    switch (cp) {
    case 'i': case 'l': case 'j': case '.': case ',': case '!': case '|': return 3;
    case 'm': case 'w': case 'M': case 'W': return 12;
    case ' ': return 4;
    default: return 7;
    }
}

static gates_i32 tp_advance(void *ctx, gates_font_t font, gates_u32 cp) {
    (void)ctx;
    return gates_font_scale(font, tp_advance_face(gates_font_face(font), cp));
}

static gates_text_metrics_t tp_metrics(void *ctx, gates_font_t font) {
    (void)ctx;
    gates_i32 lh = gates_font_scale(font, 16), ascent = gates_font_scale(font, 12);
    return (gates_text_metrics_t){ .advance = gates_font_scale(font, gates_font_face(font) == GATES_FONT_MONO ? 8 : 7),
                                   .ascent = ascent, .descent = lh - ascent, .line_height = lh };
}

static gates_size_t tp_measure(void *ctx, gates_font_t font, gates_str_t text) {
    gates_i32 w = 0;
    for (gates_u32 i = 0; i < text.size;) {
        gates_u32 cp;
        i += gates_text_decode(text, i, &cp);
        w += tp_advance(ctx, font, cp);
    }
    return (gates_size_t){ w, gates_font_scale(font, 16) };
}

static void tp_draw(void *ctx, gates_pixels_t target, gates_rect_t rect, gates_rect_t clip, gates_font_t font,
                    gates_str_t text, gates_color_t color) {
    (void)ctx; (void)target; (void)rect; (void)clip; (void)font; (void)text; (void)color;
}

static const gates_text_backend_t tp_backend = {
    .ctx = nullptr, .metrics = tp_metrics, .measure = tp_measure, .draw = tp_draw, .glyph_advance = tp_advance,
};

#endif /* GATES_TEXT_PROP_BACKEND_H */
