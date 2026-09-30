/* gates_gui_lib - software renderer (RFC-0001 section 22). Platform-free.
 * Reference implementation: correctness and determinism over speed. */
#include <gates/render.h>
#include <gates/text.h>
#include "gates_image_internal.h"

#define GATES_CLIP_STACK_MAX 64

typedef struct soft_ctx_t {
    gates_pixels_t px;
    gates_rect_t clip;                          /* current effective clip */
    gates_rect_t stack[GATES_CLIP_STACK_MAX];   /* saved clips */
    gates_i32 depth;
} soft_ctx_t;

static gates_u32 *row_at(const gates_pixels_t *px, gates_i32 y) {
    return (gates_u32 *)(void *)((gates_u8 *)px->ptr + (gates_usize_t)y * px->stride_bytes);
}

static gates_u32 blend_px(gates_u32 dst, gates_color_t src) {
    if (src.a == 255) {
        return gates_pixel_pack(src);
    }
    if (src.a == 0) {
        return dst;
    }
    gates_color_t d = gates_pixel_unpack(dst);
    gates_u32 a = src.a;
    gates_u32 na = 255u - a;
    gates_color_t out = {
        .r = (gates_u8)((src.r * a + d.r * na + 127u) / 255u),
        .g = (gates_u8)((src.g * a + d.g * na + 127u) / 255u),
        .b = (gates_u8)((src.b * a + d.b * na + 127u) / 255u),
        .a = (gates_u8)((a * 255u + (gates_u32)d.a * na + 127u) / 255u),
    };
    return gates_pixel_pack(out);
}

static void fill_rect(soft_ctx_t *ctx, gates_rect_t rect, gates_color_t color) {
    gates_rect_t r = gates_rect_intersect(rect, ctx->clip);
    if (gates_rect_is_empty(r) || color.a == 0) {
        return;
    }
    for (gates_i32 y = r.y; y < r.y + r.h; y++) {
        gates_u32 *row = row_at(&ctx->px, y);
        if (color.a == 255) {
            gates_u32 packed = gates_pixel_pack(color);
            for (gates_i32 x = r.x; x < r.x + r.w; x++) {
                row[x] = packed;
            }
        } else {
            for (gates_i32 x = r.x; x < r.x + r.w; x++) {
                row[x] = blend_px(row[x], color);
            }
        }
    }
}

/* One source channel at (sx, sy) in 1/256ths, bilinear, edges clamped. */
static gates_color_t sample(const struct gates_image *im, gates_i32 fx, gates_i32 fy) {
    gates_i32 x0 = fx >> 8, y0 = fy >> 8, ax = fx & 255, ay = fy & 255;
    gates_i32 x1 = x0 + 1, y1 = y0 + 1;
    if (x0 < 0) { x0 = 0; ax = 0; }
    if (y0 < 0) { y0 = 0; ay = 0; }
    if (x0 >= im->w - 1) { x0 = im->w - 1; ax = 0; }
    if (y0 >= im->h - 1) { y0 = im->h - 1; ay = 0; }
    if (x1 > im->w - 1) x1 = im->w - 1;
    if (y1 > im->h - 1) y1 = im->h - 1;
    gates_color_t p00 = gates_pixel_unpack(im->px[y0 * im->w + x0]), p10 = gates_pixel_unpack(im->px[y0 * im->w + x1]);
    gates_color_t p01 = gates_pixel_unpack(im->px[y1 * im->w + x0]), p11 = gates_pixel_unpack(im->px[y1 * im->w + x1]);
    gates_i32 w00 = (256 - ax) * (256 - ay), w10 = ax * (256 - ay), w01 = (256 - ax) * ay, w11 = ax * ay;
    /* Weight colours by alpha so transparent neighbours do not darken the edge. */
    gates_i32 a = p00.a * w00 + p10.a * w10 + p01.a * w01 + p11.a * w11;
    gates_color_t out = { .a = (gates_u8)((a + 32768) >> 16) };
    if (a > 0) {
        out.r = (gates_u8)(((gates_i64)p00.r * p00.a * w00 + (gates_i64)p10.r * p10.a * w10 +
                            (gates_i64)p01.r * p01.a * w01 + (gates_i64)p11.r * p11.a * w11 + a / 2) / a);
        out.g = (gates_u8)(((gates_i64)p00.g * p00.a * w00 + (gates_i64)p10.g * p10.a * w10 +
                            (gates_i64)p01.g * p01.a * w01 + (gates_i64)p11.g * p11.a * w11 + a / 2) / a);
        out.b = (gates_u8)(((gates_i64)p00.b * p00.a * w00 + (gates_i64)p10.b * p10.a * w10 +
                            (gates_i64)p01.b * p01.a * w01 + (gates_i64)p11.b * p11.a * w11 + a / 2) / a);
    }
    return out;
}

/* The image scaled into `dst` (device pixels): pixel centres map onto pixel
 * centres, so a 1:1 draw is exact. */
static void draw_image(soft_ctx_t *ctx, gates_rect_t dst, const struct gates_image *im) {
    gates_rect_t r = gates_rect_intersect(dst, ctx->clip);
    if (im == nullptr || im->w <= 0 || im->h <= 0 || gates_rect_is_empty(r) || dst.w <= 0 || dst.h <= 0) {
        return;
    }
    for (gates_i32 y = r.y; y < r.y + r.h; y++) {
        gates_u32 *row = row_at(&ctx->px, y);
        /* source y in 1/256ths: ((y - dst.y) + 0.5) * h / dst.h - 0.5 */
        gates_i32 fy = (gates_i32)(((gates_i64)(2 * (y - dst.y) + 1) * im->h * 256) / (2 * dst.h)) - 128;
        for (gates_i32 x = r.x; x < r.x + r.w; x++) {
            gates_i32 fx = (gates_i32)(((gates_i64)(2 * (x - dst.x) + 1) * im->w * 256) / (2 * dst.w)) - 128;
            row[x] = blend_px(row[x], sample(im, fx, fy));
        }
    }
}

/* Border as four non-overlapping strips - corners are drawn exactly once,
 * which matters for translucent colors. */
static void fill_border(soft_ctx_t *ctx, gates_rect_t r, gates_i32 t, gates_color_t color) {
    if (r.w <= 0 || r.h <= 0 || t < 1) {
        return;
    }
    if (t * 2 >= r.w || t * 2 >= r.h) {
        fill_rect(ctx, r, color); /* border thicker than the rect: solid */
        return;
    }
    fill_rect(ctx, (gates_rect_t){ r.x, r.y, r.w, t }, color);                    /* top */
    fill_rect(ctx, (gates_rect_t){ r.x, r.y + r.h - t, r.w, t }, color);          /* bottom */
    fill_rect(ctx, (gates_rect_t){ r.x, r.y + t, t, r.h - 2 * t }, color);        /* left */
    fill_rect(ctx, (gates_rect_t){ r.x + r.w - t, r.y + t, t, r.h - 2 * t }, color); /* right */
}

static void put_px(soft_ctx_t *ctx, gates_i32 x, gates_i32 y, gates_color_t color) {
    if (!gates_rect_contains(ctx->clip, (gates_point_t){ x, y })) {
        return;
    }
    gates_u32 *row = row_at(&ctx->px, y);
    row[x] = blend_px(row[x], color);
}

/* Bresenham; endpoints inclusive; per-pixel clip. */
static void draw_line(soft_ctx_t *ctx, gates_point_t p0, gates_point_t p1, gates_color_t color) {
    gates_i32 dx = p1.x > p0.x ? p1.x - p0.x : p0.x - p1.x;
    gates_i32 dy = p1.y > p0.y ? p1.y - p0.y : p0.y - p1.y;
    gates_i32 sx = p0.x < p1.x ? 1 : -1;
    gates_i32 sy = p0.y < p1.y ? 1 : -1;
    gates_i32 err = dx - dy;
    gates_i32 x = p0.x;
    gates_i32 y = p0.y;
    for (;;) {
        put_px(ctx, x, y, color);
        if (x == p1.x && y == p1.y) {
            break;
        }
        gates_i32 e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx)  { err += dx; y += sy; }
    }
}

gates_err_t gates_render_soft(const gates_draw_list_t *dl, gates_pixels_t target,
                              const gates_text_backend_t *text_backend) {
    return gates_render_soft_scaled(dl, target, text_backend, GATES_DPI_BASE);
}

gates_err_t gates_render_soft_scaled(const gates_draw_list_t *dl, gates_pixels_t target,
                                     const gates_text_backend_t *text_backend, gates_u32 dpi) {
    if (dl == nullptr || target.ptr == nullptr || target.w <= 0 || target.h <= 0) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (target.stride_bytes < (gates_u32)target.w * 4u) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (!gates_draw_list_balanced(dl)) {
        return PROVEN_ERR_INVALID_STATE;
    }

    soft_ctx_t ctx = {
        .px = target,
        .clip = { 0, 0, target.w, target.h },
        .depth = 0,
    };

    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        const gates_draw_cmd_t *cmd = gates_draw_list_at(dl, i);
        switch (cmd->kind) {
        case GATES_DRAW_RECT:
            fill_rect(&ctx, gates_rect_px(cmd->rect, dpi), cmd->color);
            break;
        case GATES_DRAW_BORDER:
            fill_border(&ctx, gates_rect_px(cmd->rect, dpi), gates_thickness_px(cmd->thickness, dpi),
                        cmd->color);
            break;
        case GATES_DRAW_LINE:
            draw_line(&ctx, (gates_point_t){ gates_px(cmd->p0.x, dpi), gates_px(cmd->p0.y, dpi) },
                      (gates_point_t){ gates_px(cmd->p1.x, dpi), gates_px(cmd->p1.y, dpi) },
                      cmd->color);
            break;
        case GATES_DRAW_CLIP_PUSH:
            if (ctx.depth >= GATES_CLIP_STACK_MAX) {
                return PROVEN_ERR_OUT_OF_BOUNDS;
            }
            ctx.stack[ctx.depth++] = ctx.clip;
            ctx.clip = gates_rect_intersect(ctx.clip, gates_rect_px(cmd->rect, dpi));
            break;
        case GATES_DRAW_CLIP_POP:
            /* Balance was pre-validated; depth 0 here would be a list bug. */
            if (ctx.depth <= 0) {
                return PROVEN_ERR_INVALID_STATE;
            }
            ctx.clip = ctx.stack[--ctx.depth];
            break;
        case GATES_DRAW_TEXT:
            if (text_backend == nullptr || text_backend->draw == nullptr) {
                return PROVEN_ERR_UNSUPPORTED;
            }
            if (text_backend->draw_scaled != nullptr && dpi != 0 && dpi != GATES_DPI_BASE) {
                text_backend->draw_scaled(text_backend->ctx, target, gates_rect_px(cmd->rect, dpi),
                                          ctx.clip, cmd->font, gates_draw_cmd_text(dl, cmd),
                                          cmd->color, dpi);
            } else {
                text_backend->draw(text_backend->ctx, target, gates_rect_px(cmd->rect, dpi), ctx.clip,
                                   cmd->font, gates_draw_cmd_text(dl, cmd), cmd->color);
            }
            break;
        case GATES_DRAW_IMAGE:
            draw_image(&ctx, gates_rect_px(cmd->rect, dpi), cmd->image);
            break;
        default:
            return PROVEN_ERR_INVALID_ARG;
        }
    }
    return GATES_OK;
}
