/* gates_gui_lib - builtin reference text backend (RFC-0002 section 5).
 * Embedded 8x16 monospace cells (advances by the cell rule, RFC-0004): vendored public-domain font8x8 glyphs
 * (ASCII), each 8x8 row doubled to 16 px. Deterministic everywhere.
 * Platform-free. */
#include <gates/text.h>

/* The vendored header defines a global array; give it a gates name so the
 * library exports nothing outside its prefix (vendor files stay unedited). */
#define font8x8_basic gates_i_font8x8_basic
#include "../../vendor/font8x8/font8x8_basic.h"

#define CELL_W 8
#define CELL_H 16
#define ASCENT 12
#define DESCENT 4

/* -- backend --------------------------------------------------------------- */

static gates_text_metrics_t builtin_metrics(void *ctx, gates_font_t font) {
    (void)ctx; (void)font; /* one built-in face and size */
    return (gates_text_metrics_t){
        .advance = CELL_W, .ascent = ASCENT, .descent = DESCENT, .line_height = CELL_H,
    };
}

/* RFC-0004: the reference backend keeps the cell rule for every font. */
static gates_i32 builtin_advance(void *ctx, gates_font_t font, gates_u32 cp) {
    (void)ctx; (void)font;
    return (gates_i32)gates_text_cell_width(cp) * CELL_W;
}

static gates_size_t builtin_measure(void *ctx, gates_font_t font, gates_str_t text) {
    (void)ctx; (void)font;
    return (gates_size_t){ (gates_i32)gates_text_cells(text) * CELL_W, CELL_H };
}

static gates_u32 *pix_row(gates_pixels_t *px, gates_i32 y) {
    return (gates_u32 *)(void *)((gates_u8 *)px->ptr + (gates_usize_t)y * px->stride_bytes);
}

/* Blits one glyph cell (cells wide) at (x0,y0), clipped. Glyph rows are the
 * 8 font8x8 rows doubled; the replacement "box" is drawn for cp > 0x7E. */
static void blit_cell(gates_pixels_t *px, gates_rect_t clip, gates_i32 x0, gates_i32 y0,
                      gates_u32 cp, gates_i32 cells, gates_color_t color) {
    for (gates_i32 y = 0; y < CELL_H; y++) {
        gates_i32 ty = y0 + y;
        if (ty < clip.y || ty >= clip.y + clip.h) {
            continue;
        }
        gates_u32 *row = pix_row(px, ty);
        for (gates_i32 x = 0; x < cells * CELL_W; x++) {
            gates_i32 tx = x0 + x;
            if (tx < clip.x || tx >= clip.x + clip.w) {
                continue;
            }
            bool on;
            if (cp <= 0x7E) {
                gates_u8 bits = (gates_u8)font8x8_basic[cp][y / 2];
                on = (bits >> (x & 7)) & 1u;
            } else {
                /* Replacement box: 1px outline inset by 1, across all cells. */
                gates_i32 w = cells * CELL_W;
                on = (y >= 1 && y <= CELL_H - 2 && x >= 1 && x <= w - 2)
                     && (y == 1 || y == CELL_H - 2 || x == 1 || x == w - 2);
            }
            if (on) {
                row[tx] = gates_pixel_pack(color);
            }
        }
    }
}

static void builtin_draw(void *ctx, gates_pixels_t target, gates_rect_t rect,
                         gates_rect_t clip, gates_font_t font, gates_str_t text,
                         gates_color_t color) {
    (void)ctx; (void)font;
    if (target.ptr == nullptr || color.a == 0) {
        return;
    }
    gates_rect_t bounds = gates_rect_intersect(
        gates_rect_intersect(rect, clip),
        (gates_rect_t){ 0, 0, target.w, target.h });
    if (gates_rect_is_empty(bounds)) {
        return;
    }
    gates_i32 pen_x = rect.x;
    gates_i32 pen_y = rect.y;
    gates_u32 i = 0;
    while (i < text.size) {
        gates_u32 cp;
        i += gates_text_decode(text, i, &cp);
        gates_i32 cells = (gates_i32)gates_text_cell_width(cp);
        if (pen_x >= bounds.x + bounds.w) {
            break; /* fully right of the assigned area: clipped, never reflowed */
        }
        blit_cell(&target, bounds, pen_x, pen_y, cp, cells, color);
        pen_x += cells * CELL_W;
    }
}

const gates_text_backend_t *gates_text_backend_builtin(void) {
    static const gates_text_backend_t backend = {
        .ctx = nullptr,
        .metrics = builtin_metrics,
        .measure = builtin_measure,
        .draw = builtin_draw,
        .glyph_advance = builtin_advance,
    };
    return &backend;
}
