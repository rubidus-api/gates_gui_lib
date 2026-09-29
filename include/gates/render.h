/* gates_gui_lib — software renderer, the reference renderer (RFC-0001 §21-§22).
 *
 * Consumes a balanced gates_draw_list_t and rasterizes into a caller-provided
 * 32-bit BGRA8 pixel buffer (byte order B,G,R,A — GDI-DIB native, one Phase 1
 * format). Deterministic, platform-free, testable on plain memory. */
#ifndef GATES_RENDER_H
#define GATES_RENDER_H

#include <gates/draw.h>

typedef struct gates_pixels_t {
    void *ptr;                /* first pixel of the top-left row */
    gates_i32 w;              /* pixels */
    gates_i32 h;              /* pixels */
    gates_u32 stride_bytes;   /* >= w * 4 */
} gates_pixels_t;

/* Text rendering is delegated to a text backend (RFC-0002); pass nullptr to
 * reject TEXT commands with PROVEN_ERR_UNSUPPORTED. */
typedef struct gates_text_backend_t gates_text_backend_t;

/* Blending is straight-alpha src-over with integer rounding:
 *   out = (src * a + dst * (255 - a) + 127) / 255
 * IMAGE commands draw the image scaled into their rect (bilinear over the image's
 * pixel centres, edges clamped, straight-alpha src-over); TEXT requires a text backend. An unbalanced draw list returns PROVEN_ERR_INVALID_STATE. */
[[nodiscard]] gates_err_t gates_render_soft(const gates_draw_list_t *dl,
                                            gates_pixels_t target,
                                            const gates_text_backend_t *text_backend);
/* The same for a draw list in logical units rendered at `dpi` (plan-0013):
 * rects and clips scale by their edges (gates_rect_px), thicknesses never drop
 * below one pixel, text goes to the backend's draw_scaled (or to draw, at the
 * scaled rect, when it has none). gates_render_soft is this at 96 dpi. */
[[nodiscard]] gates_err_t gates_render_soft_scaled(const gates_draw_list_t *dl,
                                                   gates_pixels_t target,
                                                   const gates_text_backend_t *text_backend,
                                                   gates_u32 dpi);

/* BGRA8 pixel pack/unpack helpers (shared by renderer, present, and tests). */
static inline gates_u32 gates_pixel_pack(gates_color_t c) {
    return (gates_u32)c.b | ((gates_u32)c.g << 8) | ((gates_u32)c.r << 16)
         | ((gates_u32)c.a << 24);
}

static inline gates_color_t gates_pixel_unpack(gates_u32 px) {
    return (gates_color_t){ .b = (gates_u8)(px & 0xff),
                            .g = (gates_u8)((px >> 8) & 0xff),
                            .r = (gates_u8)((px >> 16) & 0xff),
                            .a = (gates_u8)((px >> 24) & 0xff) };
}

#endif /* GATES_RENDER_H */
