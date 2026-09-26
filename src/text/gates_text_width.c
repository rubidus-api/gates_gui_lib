/* gates_gui_lib - text widths from per-code-point advances (RFC-0004).
 * Platform-free. */
#include <gates/text.h>

gates_i32 gates_text_width(const gates_text_backend_t *backend, gates_font_t font, gates_str_t text) {
    if (backend == nullptr || backend->glyph_advance == nullptr) return 0;
    gates_i32 w = 0;
    for (gates_u32 i = 0; i < text.size;) {
        gates_u32 cp;
        i += gates_text_decode(text, i, &cp);
        w += backend->glyph_advance(backend->ctx, font, cp);
    }
    return w;
}

gates_u32 gates_text_offset_at_x(const gates_text_backend_t *backend, gates_font_t font,
                                 gates_str_t text, gates_i32 x) {
    if (x <= 0 || backend == nullptr || backend->glyph_advance == nullptr) return 0;
    gates_i32 left = 0;
    for (gates_u32 i = 0; i < text.size;) {
        gates_u32 cp;
        gates_u32 next = i + gates_text_decode(text, i, &cp);
        gates_i32 right = left + backend->glyph_advance(backend->ctx, font, cp);
        if (x < right) return (x - left) * 2 < right - left ? i : next; /* the nearer edge */
        left = right;
        i = next;
    }
    return (gates_u32)text.size;
}
