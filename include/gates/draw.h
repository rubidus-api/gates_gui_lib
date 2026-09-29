/* gates_gui_lib — draw command list (RFC-0001 §20, Phase 1 subset).
 *
 * Widgets never call a renderer directly; they emit commands into a
 * gates_draw_list_t, which a renderer consumes (§21). Phase 1 primitives:
 * solid rect, border rect, line, clip push/pop; TEXT (RFC-0002) and IMAGE
 * (plan-0020: an image of gates/image.h drawn scaled into its rect). Platform-free. */
#ifndef GATES_DRAW_H
#define GATES_DRAW_H

#include <gates/geometry.h>

/* Semantic RGBA color (the render target format is the renderer's business). */
typedef struct gates_color_t {
    gates_u8 r;
    gates_u8 g;
    gates_u8 b;
    gates_u8 a;
} gates_color_t;

#define GATES_RGBA(rr, gg, bb, aa) ((gates_color_t){ (rr), (gg), (bb), (aa) })
#define GATES_RGB(rr, gg, bb)      GATES_RGBA((rr), (gg), (bb), 255)

/* Order per RFC-0001 §20. */
typedef enum gates_draw_kind_t {
    GATES_DRAW_RECT,
    GATES_DRAW_BORDER,
    GATES_DRAW_TEXT,
    GATES_DRAW_IMAGE,      /* plan-0020 */
    GATES_DRAW_LINE,
    GATES_DRAW_CLIP_PUSH,
    GATES_DRAW_CLIP_POP,
} gates_draw_kind_t;

typedef struct gates_draw_cmd_t {
    gates_draw_kind_t kind;
    gates_rect_t rect;        /* RECT, BORDER, CLIP_PUSH, TEXT (assigned area) */
    gates_point_t p0;         /* LINE */
    gates_point_t p1;         /* LINE */
    gates_color_t color;
    gates_i32 thickness;      /* BORDER (>=1) */
    gates_u32 text_offset;    /* TEXT: into the list's frame text arena */
    gates_u32 text_len;       /* TEXT: bytes */
    gates_i32 font;           /* TEXT: a gates_font_t (GATES_FONT_UI = 0, the default) */
    const struct gates_image *image; /* IMAGE: borrowed for the frame (gates/image.h) */
} gates_draw_cmd_t;

/* Growable command list; treat the fields as read-only outside gates code.
 * TEXT bytes are COPIED into a frame-scoped arena (RFC-0002 §4) — no
 * lifetime coupling to the caller's string; reset() reclaims. */
typedef struct gates_draw_list_t {
    gates_allocator_t alloc;
    gates_draw_cmd_t *cmds;
    gates_u32 len;
    gates_u32 cap;
    gates_i32 clip_depth;     /* running CLIP_PUSH minus CLIP_POP */
    gates_u8 *text;           /* frame text arena */
    gates_u32 text_len;
    gates_u32 text_cap;
} gates_draw_list_t;

/* Zeroed allocator -> proven heap allocator; initial_capacity 0 -> default. */
[[nodiscard]] gates_err_t gates_draw_list_init(gates_draw_list_t *dl,
                                               gates_allocator_t alloc,
                                               gates_u32 initial_capacity);
void gates_draw_list_deinit(gates_draw_list_t *dl);

/* Clears commands and the clip depth; keeps the allocation for reuse. */
void gates_draw_list_reset(gates_draw_list_t *dl);

[[nodiscard]] gates_err_t gates_draw_rect(gates_draw_list_t *dl, gates_rect_t rect,
                                          gates_color_t color);
[[nodiscard]] gates_err_t gates_draw_border(gates_draw_list_t *dl, gates_rect_t rect,
                                            gates_i32 thickness, gates_color_t color);
[[nodiscard]] gates_err_t gates_draw_line(gates_draw_list_t *dl, gates_point_t p0,
                                          gates_point_t p1, gates_color_t color);
[[nodiscard]] gates_err_t gates_draw_clip_push(gates_draw_list_t *dl, gates_rect_t rect);
[[nodiscard]] gates_err_t gates_draw_clip_pop(gates_draw_list_t *dl);

/* Emits TEXT for the assigned rect; text bytes are copied into the list. */
[[nodiscard]] gates_err_t gates_draw_text(gates_draw_list_t *dl, gates_rect_t rect,
                                          gates_str_t text, gates_i32 font,
                                          gates_color_t color);

/* Emits IMAGE: the image drawn scaled into rect (bilinear, alpha blended). The
 * image is borrowed: it must live until the list is rendered. */
[[nodiscard]] gates_err_t gates_draw_image(gates_draw_list_t *dl, gates_rect_t rect,
                                           const struct gates_image *image);

/* Borrowed view of a TEXT command's bytes (valid until reset/deinit). */
gates_str_t gates_draw_cmd_text(const gates_draw_list_t *dl, const gates_draw_cmd_t *cmd);

static inline gates_u32 gates_draw_list_len(const gates_draw_list_t *dl) {
    return dl == nullptr ? 0 : dl->len;
}

/* nullptr when out of range. */
const gates_draw_cmd_t *gates_draw_list_at(const gates_draw_list_t *dl, gates_u32 i);

/* True when every CLIP_PUSH has been popped (renderers require this). */
static inline bool gates_draw_list_balanced(const gates_draw_list_t *dl) {
    return dl != nullptr && dl->clip_depth == 0;
}

#endif /* GATES_DRAW_H */
